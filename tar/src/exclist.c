/* Per-directory exclusion files for tar.

   Copyright 2014, 2016-2017 Free Software Foundation, Inc.

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
*/
#include <system.h>
#include <quotearg.h>
#include <fnmatch.h>
#include <wordsplit.h>
#include "common.h"

typedef void (*add_fn) (struct exclude *, char const *, int, void *);

struct vcs_ignore_file
{
  char const *filename;
  int flags;
  add_fn addfn;
  void *(*initfn) (void *);
  void *data;
};

static struct vcs_ignore_file *get_vcs_ignore_file (const char *name);

struct excfile
{
  struct excfile *next;
  int flags;
  char name[1];
};

static struct excfile *excfile_head, *excfile_tail;

void
excfile_add (const char *name, int flags)
{
  struct excfile *p = xmalloc (sizeof (*p) + strlen (name));
  p->next = NULL;
  p->flags = flags;
  strcpy (p->name, name);
  if (excfile_tail)
    excfile_tail->next = p;
  else
    excfile_head = p;
  excfile_tail = p;
}

struct exclist
{
  struct exclist *next, *prev;
  int flags;
  struct exclude *excluded;
};

void
info_attach_exclist (struct tar_stat_info *dir)
{
  struct excfile *file;
  struct exclist *head = NULL, *tail = NULL, *ent;
  struct vcs_ignore_file *vcsfile;

  if (dir->exclude_list)
    return;
  for (file = excfile_head; file; file = file->next)
    {
      if (faccessat (dir ? dir->fd : chdir_fd, file->name, F_OK, 0) == 0)
	{
	  FILE *fp;
	  struct exclude *ex = NULL;
	  int fd = subfile_open (dir, file->name, O_RDONLY);
	  if (fd == -1)
	    {
	      open_error (file->name);
	      continue;
	    }
	  fp = fdopen (fd, "r");
	  if (!fp)
	    {
	      ERROR ((0, errno, _("%s: fdopen failed"), file->name));
	      close (fd);
	      continue;
	    }

	  if (!ex)
	    ex = new_exclude ();

	  vcsfile = get_vcs_ignore_file (file->name);

	  if (vcsfile->initfn)
	    vcsfile->data = vcsfile->initfn (vcsfile->data);

	  if (add_exclude_fp (vcsfile->addfn, ex, fp,
			      EXCLUDE_WILDCARDS|EXCLUDE_ANCHORED, '\n',
			      vcsfile->data))
	    {
	      int e = errno;
	      FATAL_ERROR ((0, e, "%s", quotearg_colon (file->name)));
	    }
	  fclose (fp);

	  ent = xmalloc (sizeof (*ent));
	  ent->excluded = ex;
	  ent->flags = file->flags == EXCL_DEFAULT
	               ? file->flags : vcsfile->flags;
	  ent->prev = tail;
	  ent->next = NULL;

	  if (tail)
	    tail->next = ent;
	  else
	    head = ent;
	  tail = ent;
	}
    }
  dir->exclude_list = head;
}

void
info_free_exclist (struct tar_stat_info *dir)
{
  struct exclist *ep = dir->exclude_list;

  while (ep)
    {
      struct exclist *next = ep->next;
      free_exclude (ep->excluded);
      free (ep);
      ep = next;
    }

  dir->exclude_list = NULL;
}


/* Return nonzero if file NAME is excluded.  */
bool
excluded_name (char const *name, struct tar_stat_info *st)
{
  struct exclist *ep;
  const char *rname = NULL;
  char *bname = NULL;
  bool result;
  int nr = 0;

  name += FILE_SYSTEM_PREFIX_LEN (name);

  /* Try global exclusion list first */
  if (excluded_file_name (excluded, name))
    return true;

  if (!st)
    return false;

  for (result = false; st && !result; st = st->parent, nr = EXCL_NON_RECURSIVE)
    {
      for (ep = st->exclude_list; ep; ep = ep->next)
	{
	  if (ep->flags & nr)
	    continue;
	  if ((result = excluded_file_name (ep->excluded, name)))
	    break;

	  if (!rname)
	    {
	      rname = name;
	      /* Skip leading ./ */
	      while (*rname == '.' && ISSLASH (rname[1]))
		rname += 2;
	    }
	  if ((result = excluded_file_name (ep->excluded, rname)))
	    break;

	  if (!bname)
	    bname = base_name (name);
	  if ((result = excluded_file_name (ep->excluded, bname)))
	    break;
	}
    }

  free (bname);

  return result;
}

static void
cvs_addfn (struct exclude *ex, char const *pattern, int options, void *data)
{
  struct wordsplit ws;
  size_t i;

  if (wordsplit (pattern, &ws,
		 WRDSF_NOVAR | WRDSF_NOCMD | WRDSF_SQUEEZE_DELIMS))
    return;
  for (i = 0; i < ws.ws_wordc; i++)
    add_exclude (ex, ws.ws_wordv[i], options);
  wordsplit_free (&ws);
}

static void
git_addfn (struct exclude *ex, char const *pattern, int options, void *data)
{
  while (isspace (*pattern))
    ++pattern;
  if (*pattern == 0 || *pattern == '#')
    return;
  if (*pattern == '\\' && pattern[1] == '#')
    ++pattern;
  add_exclude (ex, pattern, options);
}

static void
bzr_addfn (struct exclude *ex, char const *pattern, int options, void *data)
{
  while (isspace (*pattern))
    ++pattern;
  if (*pattern == 0 || *pattern == '#')
    return;
  if (*pattern == '!')
    {
      if (*++pattern == '!')
	++pattern;
      else
	options |= EXCLUDE_INCLUDE;
    }
  /* FIXME: According to the docs, globbing patterns are rsync-style,
            and regexps are perl-style. */
  if (strncmp (pattern, "RE:", 3) == 0)
    {
      pattern += 3;
      options &= ~EXCLUDE_WILDCARDS;
      options |= EXCLUDE_REGEX;
    }
  add_exclude (ex, pattern, options);
}

static void *
hg_initfn (void *data)
{
  static int hg_options;
  int *hgopt = data ? data : &hg_options;
  *hgopt = EXCLUDE_REGEX;
  return hgopt;
}

static void
hg_addfn (struct exclude *ex, char const *pattern, int options, void *data)
{
  int *hgopt = data;
  size_t len;

  while (isspace (*pattern))
    ++pattern;
  if (*pattern == 0 || *pattern == '#')
    return;
  if (strncmp (pattern, "syntax:", 7) == 0)
    {
      for (pattern += 7; isspace (*pattern); ++pattern)
	;
      if (strcmp (pattern, "regexp") == 0)
	/* FIXME: Regexps must be perl-style */
	*hgopt = EXCLUDE_REGEX;
      else if (strcmp (pattern, "glob") == 0)
	*hgopt = EXCLUDE_WILDCARDS;
      /* Ignore unknown syntax */
      return;
    }

  len = strlen(pattern);
  if (pattern[len-1] == '/')
    {
      char *p;

      --len;
      p = xmalloc (len+1);
      memcpy (p, pattern, len);
      p[len] = 0;
      pattern = p;
      exclude_add_pattern_buffer (ex, p);
      options |= FNM_LEADING_DIR|EXCLUDE_ALLOC;
    }

  add_exclude (ex, pattern,
	       ((*hgopt == EXCLUDE_REGEX)
		? (options & ~EXCLUDE_WILDCARDS)
		: (options & ~EXCLUDE_REGEX)) | *hgopt);
}

static struct vcs_ignore_file vcs_ignore_files[] = {
  { ".cvsignore", EXCL_NON_RECURSIVE, cvs_addfn, NULL, NULL },
  { ".gitignore", 0, git_addfn, NULL, NULL },
  { ".bzrignore", 0, bzr_addfn, NULL, NULL },
  { ".hgignore",  0, hg_addfn, hg_initfn, NULL },
  { NULL, 0, git_addfn, NULL, NULL }
};

static struct vcs_ignore_file *
get_vcs_ignore_file (const char *name)
{
  struct vcs_ignore_file *p;

  for (p = vcs_ignore_files; p->filename; p++)
    if (strcmp (p->filename, name) == 0)
      break;

  return p;
}

void
exclude_vcs_ignores (void)
{
  struct vcs_ignore_file *p;

  for (p = vcs_ignore_files; p->filename; p++)
    excfile_add (p->filename, EXCL_DEFAULT);
}
