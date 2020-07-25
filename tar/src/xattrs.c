/* Support for extended attributes.

   Copyright (C) 2006-2014, 2016-2017 Free Software Foundation, Inc.

   This file is part of GNU tar.

   GNU tar is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   GNU tar is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

   Written by James Antill, on 2006-07-27.  */

#include <config.h>
#include <system.h>

#include <fnmatch.h>
#include <quotearg.h>

#include "common.h"

#include "xattr-at.h"
#include "selinux-at.h"

struct xattrs_mask_map
{
  const char **masks;
  size_t size;
  size_t used;
};

/* list of fnmatch patterns */
static struct
{
  /* lists of fnmatch patterns */
  struct xattrs_mask_map incl;
  struct xattrs_mask_map excl;
} xattrs_setup;

/* disable posix acls when problem found in gnulib script m4/acl.m4 */
#if ! USE_ACL
# undef HAVE_POSIX_ACLS
#endif

#ifdef HAVE_POSIX_ACLS
# include "acl.h"
# include <sys/acl.h>
#endif

#ifdef HAVE_POSIX_ACLS

/* acl-at wrappers, TODO: move to gnulib in future? */
static acl_t acl_get_file_at (int, const char *, acl_type_t);
static int acl_set_file_at (int, const char *, acl_type_t, acl_t);
static int file_has_acl_at (int, char const *, struct stat const *);
static int acl_delete_def_file_at (int, char const *);

/* acl_get_file_at */
#define AT_FUNC_NAME acl_get_file_at
#define AT_FUNC_RESULT acl_t
#define AT_FUNC_FAIL (acl_t)NULL
#define AT_FUNC_F1 acl_get_file
#define AT_FUNC_POST_FILE_PARAM_DECLS   , acl_type_t type
#define AT_FUNC_POST_FILE_ARGS          , type
#include "at-func.c"
#undef AT_FUNC_NAME
#undef AT_FUNC_F1
#undef AT_FUNC_RESULT
#undef AT_FUNC_FAIL
#undef AT_FUNC_POST_FILE_PARAM_DECLS
#undef AT_FUNC_POST_FILE_ARGS

/* acl_set_file_at */
#define AT_FUNC_NAME acl_set_file_at
#define AT_FUNC_F1 acl_set_file
#define AT_FUNC_POST_FILE_PARAM_DECLS   , acl_type_t type, acl_t acl
#define AT_FUNC_POST_FILE_ARGS          , type, acl
#include "at-func.c"
#undef AT_FUNC_NAME
#undef AT_FUNC_F1
#undef AT_FUNC_POST_FILE_PARAM_DECLS
#undef AT_FUNC_POST_FILE_ARGS

/* acl_delete_def_file_at */
#define AT_FUNC_NAME acl_delete_def_file_at
#define AT_FUNC_F1 acl_delete_def_file
#define AT_FUNC_POST_FILE_PARAM_DECLS
#define AT_FUNC_POST_FILE_ARGS
#include "at-func.c"
#undef AT_FUNC_NAME
#undef AT_FUNC_F1
#undef AT_FUNC_POST_FILE_PARAM_DECLS
#undef AT_FUNC_POST_FILE_ARGS

/* gnulib file_has_acl_at */
#define AT_FUNC_NAME file_has_acl_at
#define AT_FUNC_F1 file_has_acl
#define AT_FUNC_POST_FILE_PARAM_DECLS   , struct stat const *st
#define AT_FUNC_POST_FILE_ARGS          , st
#include "at-func.c"
#undef AT_FUNC_NAME
#undef AT_FUNC_F1
#undef AT_FUNC_POST_FILE_PARAM_DECLS
#undef AT_FUNC_POST_FILE_ARGS

/* convert unix permissions into an ACL ... needed due to "default" ACLs */
static acl_t
perms2acl (int perms)
{
  char val[] = "user::---,group::---,other::---";
  /*            0123456789 123456789 123456789 123456789 */

  /* user */
  if (perms & 0400)
    val[6] = 'r';
  if (perms & 0200)
    val[7] = 'w';
  if (perms & 0100)
    val[8] = 'x';

  /* group */
  if (perms & 0040)
    val[17] = 'r';
  if (perms & 0020)
    val[18] = 'w';
  if (perms & 0010)
    val[19] = 'x';

  /* other */
  if (perms & 0004)
    val[28] = 'r';
  if (perms & 0002)
    val[29] = 'w';
  if (perms & 0001)
    val[30] = 'x';

  return acl_from_text (val);
}

static char *
skip_to_ext_fields (char *ptr)
{
  /* skip tag name (user/group/default/mask) */
  ptr += strcspn (ptr, ":,\n");

  if (*ptr != ':')
    return ptr;
  ++ptr;

  ptr += strcspn (ptr, ":,\n"); /* skip user/group name */

  if (*ptr != ':')
    return ptr;
  ++ptr;

  ptr += strcspn (ptr, ":,\n"); /* skip perms */

  return ptr;
}

/* The POSIX draft allows extra fields after the three main ones. Star
   uses this to add a fourth field for user/group which is the numeric ID.
   This function removes such extra fields by overwriting them with the
   characters that follow. */
static char *
fixup_extra_acl_fields (char *ptr)
{
  char *src = ptr;
  char *dst = ptr;

  while (*src)
    {
      const char *old = src;
      size_t len = 0;

      src = skip_to_ext_fields (src);
      len = src - old;
      if (old != dst)
        memmove (dst, old, len);
      dst += len;

      if (*src == ':')          /* We have extra fields, skip them all */
        src += strcspn (src, "\n,");

      if ((*src == '\n') || (*src == ','))
        *dst++ = *src++;        /* also done when dst == src, but that's ok */
    }
  if (src != dst)
    *dst = 0;

  return ptr;
}

/* Set the "system.posix_acl_access/system.posix_acl_default" extended
   attribute.  Called only when acls_option > 0. */
static void
xattrs__acls_set (struct tar_stat_info const *st,
                  char const *file_name, int type,
                  char *ptr, size_t len, bool def)
{
  acl_t acl;

  if (ptr)
    {
      /* assert (strlen (ptr) == len); */
      ptr = fixup_extra_acl_fields (ptr);
      acl = acl_from_text (ptr);
    }
  else if (def)
    {
      /* No "default" IEEE 1003.1e ACL set for directory.  At this moment,
         FILE_NAME may already have inherited default acls from parent
         directory;  clean them up. */
      if (acl_delete_def_file_at (chdir_fd, file_name))
        WARNOPT (WARN_XATTR_WRITE,
                (0, errno,
                 _("acl_delete_def_file_at: Cannot drop default POSIX ACLs "
                   "for file '%s'"),
                 file_name));
      return;
    }
  else
    acl = perms2acl (st->stat.st_mode);

  if (!acl)
    {
      call_arg_warn ("acl_from_text", file_name);
      return;
    }

  if (acl_set_file_at (chdir_fd, file_name, type, acl) == -1)
    /* warn even if filesystem does not support acls */
    WARNOPT (WARN_XATTR_WRITE,
	     (0, errno,
	      _ ("acl_set_file_at: Cannot set POSIX ACLs for file '%s'"),
	      file_name));

  acl_free (acl);
}

static void
xattrs__acls_get_a (int parentfd, const char *file_name,
                    struct tar_stat_info *st,
                    char **ret_ptr, size_t * ret_len)
{
  char *val = NULL;
  ssize_t len;
  acl_t acl;

  if (!(acl = acl_get_file_at (parentfd, file_name, ACL_TYPE_ACCESS)))
    {
      if (errno != ENOTSUP)
        call_arg_warn ("acl_get_file_at", file_name);
      return;
    }

  val = acl_to_text (acl, &len);
  acl_free (acl);

  if (!val)
    {
      call_arg_warn ("acl_to_text", file_name);
      return;
    }

  *ret_ptr = xstrdup (val);
  *ret_len = len;

  acl_free (val);
}

/* "system.posix_acl_default" */
static void
xattrs__acls_get_d (int parentfd, char const *file_name,
                    struct tar_stat_info *st,
                    char **ret_ptr, size_t * ret_len)
{
  char *val = NULL;
  ssize_t len;
  acl_t acl;

  if (!(acl = acl_get_file_at (parentfd, file_name, ACL_TYPE_DEFAULT)))
    {
      if (errno != ENOTSUP)
        call_arg_warn ("acl_get_file_at", file_name);
      return;
    }

  val = acl_to_text (acl, &len);
  acl_free (acl);

  if (!val)
    {
      call_arg_warn ("acl_to_text", file_name);
      return;
    }

  *ret_ptr = xstrdup (val);
  *ret_len = len;

  acl_free (val);
}
#endif /* HAVE_POSIX_ACLS */

static void
acls_one_line (const char *prefix, char delim,
               const char *aclstring, size_t len)
{
  /* support both long and short text representation of posix acls */
  struct obstack stk;
  int pref_len = strlen (prefix);
  const char *oldstring = aclstring;
  int pos = 0;

  if (!aclstring || !len)
    return;

  obstack_init (&stk);
  while (pos <= len)
    {
      int move = strcspn (aclstring, ",\n");
      if (!move)
        break;

      if (oldstring != aclstring)
        obstack_1grow (&stk, delim);

      obstack_grow (&stk, prefix, pref_len);
      obstack_grow (&stk, aclstring, move);

      aclstring += move + 1;
    }

  obstack_1grow (&stk, '\0');

  fprintf (stdlis, "%s", (char *) obstack_finish (&stk));

  obstack_free (&stk, NULL);
}

void
xattrs_acls_get (int parentfd, char const *file_name,
                 struct tar_stat_info *st, int fd, int xisfile)
{
  if (acls_option > 0)
    {
#ifndef HAVE_POSIX_ACLS
      static int done = 0;
      if (!done)
        WARN ((0, 0, _("POSIX ACL support is not available")));
      done = 1;
#else
      int err = file_has_acl_at (parentfd, file_name, &st->stat);
      if (err == 0)
        return;
      if (err == -1)
        {
          call_arg_warn ("file_has_acl_at", file_name);
          return;
        }

      xattrs__acls_get_a (parentfd, file_name, st,
                          &st->acls_a_ptr, &st->acls_a_len);
      if (!xisfile)
        xattrs__acls_get_d (parentfd, file_name, st,
                            &st->acls_d_ptr, &st->acls_d_len);
#endif
    }
}

void
xattrs_acls_set (struct tar_stat_info const *st,
                 char const *file_name, char typeflag)
{
  if (acls_option > 0 && typeflag != SYMTYPE)
    {
#ifndef HAVE_POSIX_ACLS
      static int done = 0;
      if (!done)
        WARN ((0, 0, _("POSIX ACL support is not available")));
      done = 1;
#else
      xattrs__acls_set (st, file_name, ACL_TYPE_ACCESS,
                        st->acls_a_ptr, st->acls_a_len, false);
      if (typeflag == DIRTYPE || typeflag == GNUTYPE_DUMPDIR)
        xattrs__acls_set (st, file_name, ACL_TYPE_DEFAULT,
                          st->acls_d_ptr, st->acls_d_len, true);
#endif
    }
}

static void
mask_map_realloc (struct xattrs_mask_map *map)
{
  if (map->used == map->size)
    {
      if (map->size == 0)
	map->size = 4;
      map->masks = x2nrealloc (map->masks, &map->size, sizeof (map->masks[0]));
    }
}

void
xattrs_mask_add (const char *mask, bool incl)
{
  struct xattrs_mask_map *mask_map =
    incl ? &xattrs_setup.incl : &xattrs_setup.excl;
  /* ensure there is enough space */
  mask_map_realloc (mask_map);
  /* just assign pointers -- we silently expect that pointer "mask" is valid
     through the whole program (pointer to argv array) */
  mask_map->masks[mask_map->used++] = mask;
}

static void
clear_mask_map (struct xattrs_mask_map *mask_map)
{
  if (mask_map->size)
    free (mask_map->masks);
}

void
xattrs_clear_setup (void)
{
  clear_mask_map (&xattrs_setup.incl);
  clear_mask_map (&xattrs_setup.excl);
}

static bool xattrs_masked_out (const char *kw, bool archiving);

/* get xattrs from file given by FILE_NAME or FD (when non-zero)
   xattrs are checked against the user supplied include/exclude mask
   if no mask is given this includes all the user.*, security.*, system.*,
   etc. available domains */
void
xattrs_xattrs_get (int parentfd, char const *file_name,
                   struct tar_stat_info *st, int fd)
{
  if (xattrs_option > 0)
    {
#ifndef HAVE_XATTRS
      static int done = 0;
      if (!done)
        WARN ((0, 0, _("XATTR support is not available")));
      done = 1;
#else
      static size_t xsz = 1024;
      static char *xatrs = NULL;
      ssize_t xret = -1;

      if (!xatrs)
	xatrs = x2nrealloc (xatrs, &xsz, 1);

      while (((fd == 0) ?
              ((xret =
                llistxattrat (parentfd, file_name, xatrs, xsz)) == -1) :
	      ((xret = flistxattr (fd, xatrs, xsz)) == -1))
             && (errno == ERANGE))
        {
	  xatrs = x2nrealloc (xatrs, &xsz, 1);
        }

      if (xret == -1)
        call_arg_warn ((fd == 0) ? "llistxattrat" : "flistxattr", file_name);
      else
        {
          const char *attr = xatrs;
          static size_t asz = 1024;
          static char *val = NULL;

          if (!val)
            val = x2nrealloc (val, &asz, 1);

          while (xret > 0)
            {
              size_t len = strlen (attr);
              ssize_t aret = 0;

              while (((fd == 0)
                      ? ((aret = lgetxattrat (parentfd, file_name, attr,
                                              val, asz)) == -1)
                      : ((aret = fgetxattr (fd, attr, val, asz)) == -1))
                     && (errno == ERANGE))
                {
		  val = x2nrealloc (val, &asz, 1);
                }

              if (aret != -1)
                {
                  if (!xattrs_masked_out (attr, true))
                    xheader_xattr_add (st, attr, val, aret);
                }
              else if (errno != ENOATTR)
                call_arg_warn ((fd == 0) ? "lgetxattrat"
                               : "fgetxattr", file_name);

              attr += len + 1;
              xret -= len + 1;
            }
        }
#endif
    }
}

#ifdef HAVE_XATTRS
static void
xattrs__fd_set (struct tar_stat_info const *st,
                char const *file_name, char typeflag,
                const char *attr, const char *ptr, size_t len)
{
  if (ptr)
    {
      const char *sysname = "setxattrat";
      int ret = -1;

      if (typeflag != SYMTYPE)
        ret = setxattrat (chdir_fd, file_name, attr, ptr, len, 0);
      else
        {
          sysname = "lsetxattr";
          ret = lsetxattrat (chdir_fd, file_name, attr, ptr, len, 0);
        }

      if (ret == -1)
        WARNOPT (WARN_XATTR_WRITE,
		 (0, errno,
		  _("%s: Cannot set '%s' extended attribute for file '%s'"),
		  sysname, attr, file_name));
    }
}
#endif

/* lgetfileconat is called against FILE_NAME iff the FD parameter is set to
   zero, otherwise the fgetfileconat is used against correct file descriptor */
void
xattrs_selinux_get (int parentfd, char const *file_name,
                    struct tar_stat_info *st, int fd)
{
  if (selinux_context_option > 0)
    {
#if HAVE_SELINUX_SELINUX_H != 1
      static int done = 0;
      if (!done)
        WARN ((0, 0, _("SELinux support is not available")));
      done = 1;
#else
      int result = fd ?
	            fgetfilecon (fd, &st->cntx_name)
                    : lgetfileconat (parentfd, file_name, &st->cntx_name);

      if (result == -1 && errno != ENODATA && errno != ENOTSUP)
        call_arg_warn (fd ? "fgetfilecon" : "lgetfileconat", file_name);
#endif
    }
}

void
xattrs_selinux_set (struct tar_stat_info const *st,
                    char const *file_name, char typeflag)
{
  if (selinux_context_option > 0)
    {
#if HAVE_SELINUX_SELINUX_H != 1
      static int done = 0;
      if (!done)
        WARN ((0, 0, _("SELinux support is not available")));
      done = 1;
#else
      const char *sysname = "setfilecon";
      int ret;

      if (!st->cntx_name)
        return;

      if (typeflag != SYMTYPE)
        {
          ret = setfileconat (chdir_fd, file_name, st->cntx_name);
          sysname = "setfileconat";
        }
      else
        {
          ret = lsetfileconat (chdir_fd, file_name, st->cntx_name);
          sysname = "lsetfileconat";
        }

      if (ret == -1)
        WARNOPT (WARN_XATTR_WRITE,
		 (0, errno,
		  _("%s: Cannot set SELinux context for file '%s'"),
		  sysname, file_name));
#endif
    }
}

static bool
xattrs_matches_mask (const char *kw, struct xattrs_mask_map *mm)
{
  int i;

  if (!mm->size)
    return false;

  for (i = 0; i < mm->used; i++)
    if (fnmatch (mm->masks[i], kw, 0) == 0)
      return true;

  return false;
}

#define USER_DOT_PFX "user."

static bool
xattrs_kw_included (const char *kw, bool archiving)
{
  if (xattrs_setup.incl.size)
    return xattrs_matches_mask (kw, &xattrs_setup.incl);
  else if (archiving)
    return true;
  else
    return strncmp (kw, USER_DOT_PFX, sizeof (USER_DOT_PFX) - 1) == 0;
}

static bool
xattrs_kw_excluded (const char *kw, bool archiving)
{
  return xattrs_setup.excl.size ?
    xattrs_matches_mask (kw, &xattrs_setup.excl) : false;
}

/* Check whether the xattr with keyword KW should be discarded from list of
   attributes that are going to be archived/excluded (set ARCHIVING=true for
   archiving, false for excluding) */
static bool
xattrs_masked_out (const char *kw, bool archiving)
{
  return xattrs_kw_included (kw, archiving) ?
    xattrs_kw_excluded (kw, archiving) : true;
}

void
xattrs_xattrs_set (struct tar_stat_info const *st,
                   char const *file_name, char typeflag, int later_run)
{
  if (xattrs_option > 0)
    {
#ifndef HAVE_XATTRS
      static int done = 0;
      if (!done)
        WARN ((0, 0, _("XATTR support is not available")));
      done = 1;
#else
      size_t scan = 0;

      if (!st->xattr_map_size)
        return;

      for (; scan < st->xattr_map_size; ++scan)
        {
          char *keyword = st->xattr_map[scan].xkey;
          keyword += strlen ("SCHILY.xattr.");

          /* TODO: this 'later_run' workaround is temporary solution -> once
             capabilities should become fully supported by it's API and there
             should exist something like xattrs_capabilities_set() call.
             For a regular files: all extended attributes are restored during
             the first run except 'security.capability' which is restored in
             'later_run == 1'.  */
          if (typeflag == REGTYPE
              && later_run == !!strcmp (keyword, "security.capability"))
            continue;

          if (xattrs_masked_out (keyword, false /* extracting */ ))
            /* we don't want to restore this keyword */
            continue;

          xattrs__fd_set (st, file_name, typeflag, keyword,
                          st->xattr_map[scan].xval_ptr,
                          st->xattr_map[scan].xval_len);
        }
#endif
    }
}

void
xattrs_print_char (struct tar_stat_info const *st, char *output)
{
  int i;

  if (verbose_option < 2)
    {
      *output = 0;
      return;
    }

  if (xattrs_option > 0 || selinux_context_option > 0 || acls_option > 0)
    {
      /* placeholders */
      *output = ' ';
      output[1] = 0;
    }

  if (xattrs_option > 0 && st->xattr_map_size)
    for (i = 0; i < st->xattr_map_size; ++i)
      {
        char *keyword = st->xattr_map[i].xkey + strlen ("SCHILY.xattr.");
        if (!xattrs_masked_out (keyword, false /* like extracting */ ))
	  {
	    *output = '*';
	    break;
	  }
      }

  if (selinux_context_option > 0 && st->cntx_name)
    *output = '.';

  if (acls_option > 0 && (st->acls_a_len || st->acls_d_len))
    *output = '+';
}

void
xattrs_print (struct tar_stat_info const *st)
{
  if (verbose_option < 3)
    return;

  /* selinux */
  if (selinux_context_option > 0 && st->cntx_name)
    fprintf (stdlis, "  s: %s\n", st->cntx_name);

  /* acls */
  if (acls_option > 0 && (st->acls_a_len || st->acls_d_len))
    {
      fprintf (stdlis, "  a: ");
      acls_one_line ("", ',', st->acls_a_ptr, st->acls_a_len);
      acls_one_line ("default:", ',', st->acls_d_ptr, st->acls_d_len);
      fprintf (stdlis, "\n");
    }

  /* xattrs */
  if (xattrs_option > 0 && st->xattr_map_size)
    {
      int i;

      for (i = 0; i < st->xattr_map_size; ++i)
        {
          char *keyword = st->xattr_map[i].xkey + strlen ("SCHILY.xattr.");
          if (!xattrs_masked_out (keyword, false /* like extracting */ ))
	    fprintf (stdlis, "  x: %lu %s\n",
		     (unsigned long) st->xattr_map[i].xval_len, keyword);
        }
    }
}
