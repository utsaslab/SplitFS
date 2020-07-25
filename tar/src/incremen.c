/* GNU dump extensions to tar.

   Copyright 1988, 1992-1994, 1996-1997, 1999-2001, 2003-2009,
   2013-2014, 2016-2017 Free Software Foundation, Inc.

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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <system.h>
#include <hash.h>
#include <quotearg.h>
#include "common.h"

/* Incremental dump specialities.  */

/* Which child files to save under a directory.  */
enum children
  {
    NO_CHILDREN,
    CHANGED_CHILDREN,
    ALL_CHILDREN
  };

#define DIRF_INIT     0x0001    /* directory structure is initialized
				   (procdir called at least once) */
#define DIRF_NFS      0x0002    /* directory is mounted on nfs */
#define DIRF_FOUND    0x0004    /* directory is found on fs */
#define DIRF_NEW      0x0008    /* directory is new (not found
				   in the previous dump) */
#define DIRF_RENAMED  0x0010    /* directory is renamed */

#define DIR_IS_INITED(d) ((d)->flags & DIRF_INIT)
#define DIR_IS_NFS(d) ((d)->flags & DIRF_NFS)
#define DIR_IS_FOUND(d) ((d)->flags & DIRF_FOUND)
/* #define DIR_IS_NEW(d) ((d)->flags & DIRF_NEW) FIXME: not used */
#define DIR_IS_RENAMED(d) ((d)->flags & DIRF_RENAMED)

#define DIR_SET_FLAG(d,f) (d)->flags |= (f)
#define DIR_CLEAR_FLAG(d,f) (d)->flags &= ~(f)

struct dumpdir                 /* Dump directory listing */
{
  char *contents;              /* Actual contents */
  size_t total;                /* Total number of elements */
  size_t elc;                  /* Number of D/N/Y elements. */
  char **elv;                  /* Array of D/N/Y elements */
};

/* Directory attributes.  */
struct directory
  {
    struct directory *next;
    struct timespec mtime;      /* Modification time */
    dev_t device_number;	/* device number for directory */
    ino_t inode_number;		/* inode number for directory */
    struct dumpdir *dump;       /* Directory contents */
    struct dumpdir *idump;      /* Initial contents if the directory was
				   rescanned */
    enum children children;     /* What to save under this directory */
    unsigned flags;             /* See DIRF_ macros above */
    struct directory *orig;     /* If the directory was renamed, points to
				   the original directory structure */
    const char *tagfile;        /* Tag file, if the directory falls under
				   exclusion_tag_under */
    char *caname;               /* canonical name */
    char *name;	     	        /* file name of directory */
  };

static struct dumpdir *
dumpdir_create0 (const char *contents, const char *cmask)
{
  struct dumpdir *dump;
  size_t i, total, ctsize, len;
  char *p;
  const char *q;

  for (i = 0, total = 0, ctsize = 1, q = contents; *q; total++, q += len)
    {
      len = strlen (q) + 1;
      ctsize += len;
      if (!cmask || strchr (cmask, *q))
	i++;
    }
  dump = xmalloc (sizeof (*dump) + ctsize);
  dump->contents = (char*)(dump + 1);
  memcpy (dump->contents, contents, ctsize);
  dump->total = total;
  dump->elc = i;
  dump->elv = xcalloc (i + 1, sizeof (dump->elv[0]));

  for (i = 0, p = dump->contents; *p; p += strlen (p) + 1)
    {
      if (!cmask || strchr (cmask, *p))
	dump->elv[i++] = p + 1;
    }
  dump->elv[i] = NULL;
  return dump;
}

static struct dumpdir *
dumpdir_create (const char *contents)
{
  return dumpdir_create0 (contents, "YND");
}

static void
dumpdir_free (struct dumpdir *dump)
{
  free (dump->elv);
  free (dump);
}

static int
compare_dirnames (const void *first, const void *second)
{
  char const *const *name1 = first;
  char const *const *name2 = second;
  return strcmp (*name1, *name2);
}

/* Locate NAME in the dumpdir array DUMP.
   Return pointer to the slot in DUMP->contents, or NULL if not found */
static char *
dumpdir_locate (struct dumpdir *dump, const char *name)
{
  char **ptr;
  if (!dump)
    return NULL;

  ptr = bsearch (&name, dump->elv, dump->elc, sizeof (dump->elv[0]),
		 compare_dirnames);
  return ptr ? *ptr - 1: NULL;
}

struct dumpdir_iter
{
  struct dumpdir *dump; /* Dumpdir being iterated */
  int all;              /* Iterate over all entries, not only D/N/Y */
  size_t next;          /* Index of the next element */
};

static char *
dumpdir_next (struct dumpdir_iter *itr)
{
  size_t cur = itr->next;
  char *ret = NULL;

  if (itr->all)
    {
      ret = itr->dump->contents + cur;
      if (*ret == 0)
	return NULL;
      itr->next += strlen (ret) + 1;
    }
  else if (cur < itr->dump->elc)
    {
      ret = itr->dump->elv[cur] - 1;
      itr->next++;
    }

  return ret;
}

static char *
dumpdir_first (struct dumpdir *dump, int all, struct dumpdir_iter **pitr)
{
  struct dumpdir_iter *itr = xmalloc (sizeof (*itr));
  itr->dump = dump;
  itr->all = all;
  itr->next = 0;
  *pitr = itr;
  return dumpdir_next (itr);
}

/* Return size in bytes of the dumpdir array P */
size_t
dumpdir_size (const char *p)
{
  size_t totsize = 0;

  while (*p)
    {
      size_t size = strlen (p) + 1;
      totsize += size;
      p += size;
    }
  return totsize + 1;
}


static struct directory *dirhead, *dirtail;
static Hash_table *directory_table;
static Hash_table *directory_meta_table;

#if HAVE_ST_FSTYPE_STRING
  static char const nfs_string[] = "nfs";
# define NFS_FILE_STAT(st) (strcmp ((st).st_fstype, nfs_string) == 0)
#else
# define ST_DEV_MSB(st) (~ (dev_t) 0 << (sizeof (st).st_dev * CHAR_BIT - 1))
# define NFS_FILE_STAT(st) (((st).st_dev & ST_DEV_MSB (st)) != 0)
#endif

/* Calculate the hash of a directory.  */
static size_t
hash_directory_canonical_name (void const *entry, size_t n_buckets)
{
  struct directory const *directory = entry;
  return hash_string (directory->caname, n_buckets);
}

/* Compare two directories for equality of their names. */
static bool
compare_directory_canonical_names (void const *entry1, void const *entry2)
{
  struct directory const *directory1 = entry1;
  struct directory const *directory2 = entry2;
  return strcmp (directory1->caname, directory2->caname) == 0;
}

static size_t
hash_directory_meta (void const *entry, size_t n_buckets)
{
  struct directory const *directory = entry;
  /* FIXME: Work out a better algorytm */
  return (directory->device_number + directory->inode_number) % n_buckets;
}

/* Compare two directories for equality of their device and inode numbers. */
static bool
compare_directory_meta (void const *entry1, void const *entry2)
{
  struct directory const *directory1 = entry1;
  struct directory const *directory2 = entry2;
  return directory1->device_number == directory2->device_number
            && directory1->inode_number == directory2->inode_number;
}

/* Make a directory entry for given relative NAME and canonical name CANAME.
   The latter is "stolen", i.e. the returned directory contains pointer to
   it. */
static struct directory *
make_directory (const char *name, char *caname)
{
  size_t namelen = strlen (name);
  struct directory *directory = xmalloc (sizeof (*directory));
  directory->next = NULL;
  directory->dump = directory->idump = NULL;
  directory->orig = NULL;
  directory->flags = false;
  if (namelen > 1 && ISSLASH (name[namelen - 1]))
    namelen--;
  directory->name = xmalloc (namelen + 1);
  memcpy (directory->name, name, namelen);
  directory->name[namelen] = 0;
  directory->caname = caname;
  directory->tagfile = NULL;
  return directory;
}

static void
free_directory (struct directory *dir)
{
  free (dir->caname);
  free (dir->name);
  free (dir);
}

static struct directory *
attach_directory (const char *name)
{
  char *cname = normalize_filename (chdir_current, name);
  struct directory *dir = make_directory (name, cname);
  if (dirtail)
    dirtail->next = dir;
  else
    dirhead = dir;
  dirtail = dir;
  return dir;
}


static void
dirlist_replace_prefix (const char *pref, const char *repl)
{
  struct directory *dp;
  size_t pref_len = strlen (pref);
  size_t repl_len = strlen (repl);
  for (dp = dirhead; dp; dp = dp->next)
    replace_prefix (&dp->name, pref, pref_len, repl, repl_len);
}

void
clear_directory_table (void)
{
  struct directory *dp;

  if (directory_table)
    hash_clear (directory_table);
  if (directory_meta_table)
    hash_clear (directory_meta_table);
  for (dp = dirhead; dp; )
    {
      struct directory *next = dp->next;
      free_directory (dp);
      dp = next;
    }
  dirhead = dirtail = NULL;
}

/* Create and link a new directory entry for directory NAME, having a
   device number DEV and an inode number INO, with NFS indicating
   whether it is an NFS device and FOUND indicating whether we have
   found that the directory exists.  */
static struct directory *
note_directory (char const *name, struct timespec mtime,
		dev_t dev, ino_t ino, bool nfs, bool found,
		const char *contents)
{
  struct directory *directory = attach_directory (name);

  directory->mtime = mtime;
  directory->device_number = dev;
  directory->inode_number = ino;
  directory->children = CHANGED_CHILDREN;
  if (nfs)
    DIR_SET_FLAG (directory, DIRF_NFS);
  if (found)
    DIR_SET_FLAG (directory, DIRF_FOUND);
  if (contents)
    directory->dump = dumpdir_create (contents);
  else
    directory->dump = NULL;

  if (! ((directory_table
	  || (directory_table = hash_initialize (0, 0,
						 hash_directory_canonical_name,
						 compare_directory_canonical_names,
						 0)))
	 && hash_insert (directory_table, directory)))
    xalloc_die ();

  if (! ((directory_meta_table
	  || (directory_meta_table = hash_initialize (0, 0,
						      hash_directory_meta,
						      compare_directory_meta,
						      0)))
	 && hash_insert (directory_meta_table, directory)))
    xalloc_die ();

  return directory;
}

/* Return a directory entry for a given file NAME, or zero if none found.  */
static struct directory *
find_directory (const char *name)
{
  if (! directory_table)
    return 0;
  else
    {
      char *caname = normalize_filename (chdir_current, name);
      struct directory *dir = make_directory (name, caname);
      struct directory *ret = hash_lookup (directory_table, dir);
      free_directory (dir);
      return ret;
    }
}

#if 0
/* Remove directory entry for the given CANAME */
void
remove_directory (const char *caname)
{
  struct directory *dir = make_directory (caname, xstrdup (caname));
  struct directory *ret = hash_delete (directory_table, dir);
  if (ret)
    free_directory (ret);
  free_directory (dir);
}
#endif

/* If first OLD_PREFIX_LEN bytes of DIR->NAME name match OLD_PREFIX,
   replace them with NEW_PREFIX. */
void
rebase_directory (struct directory *dir,
		  const char *old_prefix, size_t old_prefix_len,
		  const char *new_prefix, size_t new_prefix_len)
{
  replace_prefix (&dir->name, old_prefix, old_prefix_len,
		  new_prefix, new_prefix_len);
}

/* Return a directory entry for a given combination of device and inode
   numbers, or zero if none found.  */
static struct directory *
find_directory_meta (dev_t dev, ino_t ino)
{
  if (! directory_meta_table)
    return 0;
  else
    {
      struct directory *dir = make_directory ("", NULL);
      struct directory *ret;
      dir->device_number = dev;
      dir->inode_number = ino;
      ret = hash_lookup (directory_meta_table, dir);
      free_directory (dir);
      return ret;
    }
}

void
update_parent_directory (struct tar_stat_info *parent)
{
  struct directory *directory = find_directory (parent->orig_file_name);
  if (directory)
    {
      struct stat st;
      if (fstat (parent->fd, &st) != 0)
	stat_diag (directory->name);
      else
	directory->mtime = get_stat_mtime (&st);
    }
}

#define PD_FORCE_CHILDREN 0x10
#define PD_FORCE_INIT     0x20
#define PD_CHILDREN(f) ((f) & 3)

static struct directory *
procdir (const char *name_buffer, struct tar_stat_info *st,
	 int flag,
	 char *entry)
{
  struct directory *directory;
  struct stat *stat_data = &st->stat;
  bool nfs = NFS_FILE_STAT (*stat_data);
  bool perhaps_renamed = false;

  if ((directory = find_directory (name_buffer)) != NULL)
    {
      if (DIR_IS_INITED (directory))
	{
	  if (flag & PD_FORCE_INIT)
	    {
	      assign_string (&directory->name, name_buffer);
	    }
	  else
	    {
	      *entry = 'N'; /* Avoid duplicating this directory */
	      return directory;
	    }
	}

      if (strcmp (directory->name, name_buffer))
	{
	  *entry = 'N';
	  return directory;
	}

      /* With NFS, the same file can have two different devices
	 if an NFS directory is mounted in multiple locations,
	 which is relatively common when automounting.
	 To avoid spurious incremental redumping of
	 directories, consider all NFS devices as equal,
	 relying on the i-node to establish differences.  */

      if (! ((!check_device_option
	      || (DIR_IS_NFS (directory) && nfs)
	      || directory->device_number == stat_data->st_dev)
	     && directory->inode_number == stat_data->st_ino))
	{
	  /* FIXME: find_directory_meta ignores nfs */
	  struct directory *d = find_directory_meta (stat_data->st_dev,
						     stat_data->st_ino);
	  if (d)
	    {
	      if (strcmp (d->name, name_buffer))
		{
		  WARNOPT (WARN_RENAME_DIRECTORY,
			   (0, 0,
			    _("%s: Directory has been renamed from %s"),
			    quotearg_colon (name_buffer),
			    quote_n (1, d->name)));
		  directory->orig = d;
		  DIR_SET_FLAG (directory, DIRF_RENAMED);
		  dirlist_replace_prefix (d->name, name_buffer);
		}
	      directory->children = CHANGED_CHILDREN;
	    }
	  else
	    {
	      perhaps_renamed = true;
	      directory->children = ALL_CHILDREN;
	      directory->device_number = stat_data->st_dev;
	      directory->inode_number = stat_data->st_ino;
	    }
	  if (nfs)
	    DIR_SET_FLAG (directory, DIRF_NFS);
	}
      else
	directory->children = CHANGED_CHILDREN;

      DIR_SET_FLAG (directory, DIRF_FOUND);
    }
  else
    {
      struct directory *d = find_directory_meta (stat_data->st_dev,
						 stat_data->st_ino);

      directory = note_directory (name_buffer,
				  get_stat_mtime (stat_data),
				  stat_data->st_dev,
				  stat_data->st_ino,
				  nfs,
				  true,
				  NULL);

      if (d)
	{
	  if (strcmp (d->name, name_buffer))
	    {
	      WARNOPT (WARN_RENAME_DIRECTORY,
		       (0, 0, _("%s: Directory has been renamed from %s"),
			quotearg_colon (name_buffer),
			quote_n (1, d->name)));
	      directory->orig = d;
	      DIR_SET_FLAG (directory, DIRF_RENAMED);
	      dirlist_replace_prefix (d->name, name_buffer);
	    }
	  directory->children = CHANGED_CHILDREN;
	}
      else
	{
	  DIR_SET_FLAG (directory, DIRF_NEW);
	  WARNOPT (WARN_NEW_DIRECTORY,
		   (0, 0, _("%s: Directory is new"),
		    quotearg_colon (name_buffer)));
	  directory->children =
	    (listed_incremental_option
	     || (OLDER_STAT_TIME (*stat_data, m)
		 || (after_date_option
		     && OLDER_STAT_TIME (*stat_data, c))))
	    ? ALL_CHILDREN
	    : CHANGED_CHILDREN;
	}
    }

  if (one_file_system_option && st->parent
      && stat_data->st_dev != st->parent->stat.st_dev)
    {
      WARNOPT (WARN_XDEV,
	       (0, 0,
		_("%s: directory is on a different filesystem; not dumped"),
		quotearg_colon (directory->name)));
      directory->children = NO_CHILDREN;
      /* If there is any dumpdir info in that directory, remove it */
      if (directory->dump)
	{
	  dumpdir_free (directory->dump);
	  directory->dump = NULL;
	}
      perhaps_renamed = false;
    }

  else if (flag & PD_FORCE_CHILDREN)
    {
      directory->children = PD_CHILDREN(flag);
      if (directory->children == NO_CHILDREN)
	*entry = 'N';
    }

  if (perhaps_renamed)
    WARNOPT (WARN_RENAME_DIRECTORY,
	     (0, 0, _("%s: Directory has been renamed"),
	      quotearg_colon (name_buffer)));

  DIR_SET_FLAG (directory, DIRF_INIT);

  if (directory->children != NO_CHILDREN)
    {
      const char *tag_file_name;

      switch (check_exclusion_tags (st, &tag_file_name))
	{
	case exclusion_tag_all:
	  /* This warning can be duplicated by code in dump_file0, but only
	     in case when the topmost directory being archived contains
	     an exclusion tag. */
	  exclusion_tag_warning (name_buffer, tag_file_name,
				 _("directory not dumped"));
	  *entry = 'N';
	  directory->children = NO_CHILDREN;
	  break;

	case exclusion_tag_contents:
	  exclusion_tag_warning (name_buffer, tag_file_name,
				 _("contents not dumped"));
	  directory->children = NO_CHILDREN;
	  directory->tagfile = tag_file_name;
	  break;

	case exclusion_tag_under:
	  exclusion_tag_warning (name_buffer, tag_file_name,
				 _("contents not dumped"));
	  directory->tagfile = tag_file_name;
	  break;

	case exclusion_tag_none:
	  break;
	}
    }

  return directory;
}

/* Compare dumpdir array from DIRECTORY with directory listing DIR and
   build a new dumpdir template.

   DIR must be returned by a previous call to savedir().

   File names in DIRECTORY->dump->contents must be sorted
   alphabetically.

   DIRECTORY->dump is replaced with the created template. Each entry is
   prefixed with ' ' if it was present in DUMP and with 'Y' otherwise. */

static void
makedumpdir (struct directory *directory, const char *dir)
{
  size_t i,
         dirsize,  /* Number of elements in DIR */
         len;      /* Length of DIR, including terminating nul */
  const char *p;
  char const **array;
  char *new_dump, *new_dump_ptr;
  struct dumpdir *dump;

  if (directory->children == ALL_CHILDREN)
    dump = NULL;
  else if (DIR_IS_RENAMED (directory))
    dump = directory->orig->idump ?
           directory->orig->idump : directory->orig->dump;
  else
    dump = directory->dump;

  /* Count the size of DIR and the number of elements it contains */
  dirsize = 0;
  len = 0;
  for (p = dir; *p; p += strlen (p) + 1, dirsize++)
    len += strlen (p) + 2;
  len++;

  /* Create a sorted directory listing */
  array = xcalloc (dirsize, sizeof array[0]);
  for (i = 0, p = dir; *p; p += strlen (p) + 1, i++)
    array[i] = p;

  qsort (array, dirsize, sizeof (array[0]), compare_dirnames);

  /* Prepare space for new dumpdir */
  new_dump = xmalloc (len);
  new_dump_ptr = new_dump;

  /* Fill in the dumpdir template */
  for (i = 0; i < dirsize; i++)
    {
      const char *loc = dumpdir_locate (dump, array[i]);
      if (loc)
	{
	  if (directory->tagfile)
	    *new_dump_ptr = 'I';
	  else
	    *new_dump_ptr = ' ';
	  new_dump_ptr++;
	}
      else if (directory->tagfile)
	*new_dump_ptr++ = 'I';
      else
	*new_dump_ptr++ = 'Y'; /* New entry */

      /* Copy the file name */
      for (p = array[i]; (*new_dump_ptr++ = *p++); )
	;
    }
  *new_dump_ptr = 0;
  directory->idump = directory->dump;
  directory->dump = dumpdir_create0 (new_dump, NULL);
  free (new_dump);
  free (array);
}

/* Create a dumpdir containing only one entry: that for the
   tagfile. */
static void
maketagdumpdir (struct directory *directory)
{
  size_t len = strlen (directory->tagfile) + 1;
  char *new_dump = xmalloc (len + 2);
  new_dump[0] = 'Y';
  memcpy (new_dump + 1, directory->tagfile, len);
  new_dump[len + 1] = 0;

  directory->idump = directory->dump;
  directory->dump = dumpdir_create0 (new_dump, NULL);
  free (new_dump);
}

/* Recursively scan the directory identified by ST.  */
struct directory *
scan_directory (struct tar_stat_info *st)
{
  char const *dir = st->orig_file_name;
  char *dirp = get_directory_entries (st);
  dev_t device = st->stat.st_dev;
  bool cmdline = ! st->parent;
  namebuf_t nbuf;
  char *tmp;
  struct directory *directory;
  char ch;

  if (! dirp)
    savedir_error (dir);

  info_attach_exclist (st);

  tmp = xstrdup (dir);
  zap_slashes (tmp);

  directory = procdir (tmp, st,
		       (cmdline ? PD_FORCE_INIT : 0),
		       &ch);

  free (tmp);

  nbuf = namebuf_create (dir);

  if (dirp)
    {
      if (directory->children != NO_CHILDREN)
	{
	  char *entry;	/* directory entry being scanned */
	  struct dumpdir_iter *itr;

	  makedumpdir (directory, dirp);

	  for (entry = dumpdir_first (directory->dump, 1, &itr);
	       entry;
	       entry = dumpdir_next (itr))
	    {
	      char *full_name = namebuf_name (nbuf, entry + 1);

	      if (*entry == 'I') /* Ignored entry */
		*entry = 'N';
	      else if (excluded_name (full_name, st))
		*entry = 'N';
	      else
		{
		  int fd = st->fd;
		  void (*diag) (char const *) = 0;
		  struct tar_stat_info stsub;
		  tar_stat_init (&stsub);

		  if (fd < 0)
		    {
		      errno = - fd;
		      diag = open_diag;
		    }
		  else if (fstatat (fd, entry + 1, &stsub.stat,
				    fstatat_flags) != 0)
		    diag = stat_diag;
		  else if (S_ISDIR (stsub.stat.st_mode))
		    {
		      int subfd = subfile_open (st, entry + 1,
						open_read_flags);
		      if (subfd < 0)
			diag = open_diag;
		      else
			{
			  stsub.fd = subfd;
			  if (fstat (subfd, &stsub.stat) != 0)
			    diag = stat_diag;
			}
		    }

		  if (diag)
		    {
		      file_removed_diag (full_name, false, diag);
		      *entry = 'N';
		    }
		  else if (S_ISDIR (stsub.stat.st_mode))
		    {
		      int pd_flag = 0;
		      if (!recursion_option)
			pd_flag |= PD_FORCE_CHILDREN | NO_CHILDREN;
		      else if (directory->children == ALL_CHILDREN)
			pd_flag |= PD_FORCE_CHILDREN | ALL_CHILDREN;
		      *entry = 'D';

		      stsub.parent = st;
		      procdir (full_name, &stsub, pd_flag, entry);
		      restore_parent_fd (&stsub);
		    }
		  else if (one_file_system_option &&
			   device != stsub.stat.st_dev)
		    *entry = 'N';
		  else if (*entry == 'Y')
		    /* New entry, skip further checks */;
		  /* FIXME: if (S_ISHIDDEN (stat_data.st_mode))?? */
		  else if (OLDER_STAT_TIME (stsub.stat, m)
			   && (!after_date_option
			       || OLDER_STAT_TIME (stsub.stat, c)))
		    *entry = 'N';
		  else
		    *entry = 'Y';

		  tar_stat_destroy (&stsub);
		}
	    }
	  free (itr);
	}
      else if (directory->tagfile)
	maketagdumpdir (directory);
    }

  namebuf_free (nbuf);

  free (dirp);

  return directory;
}

/* Return pointer to the contents of the directory DIR */
const char *
directory_contents (struct directory *dir)
{
  if (!dir)
    return NULL;
  return dir->dump ? dir->dump->contents : NULL;
}

/* A "safe" version of directory_contents, which never returns NULL. */
const char *
safe_directory_contents (struct directory *dir)
{
  const char *ret = directory_contents (dir);
  return ret ? ret : "\0\0\0\0";
}


static void
obstack_code_rename (struct obstack *stk, char const *from, char const *to)
{
  char const *s;

  s = from[0] == 0 ? from :
                     safer_name_suffix (from, false, absolute_names_option);
  obstack_1grow (stk, 'R');
  obstack_grow (stk, s, strlen (s) + 1);

  s = to[0] == 0 ? to:
                   safer_name_suffix (to, false, absolute_names_option);
  obstack_1grow (stk, 'T');
  obstack_grow (stk, s, strlen (s) + 1);
}

static void
store_rename (struct directory *dir, struct obstack *stk)
{
  if (DIR_IS_RENAMED (dir))
    {
      struct directory *prev, *p;

      /* Detect eventual cycles and clear DIRF_RENAMED flag, so these entries
	 are ignored when hit by this function next time.
	 If the chain forms a cycle, prev points to the entry DIR is renamed
	 from. In this case it still retains DIRF_RENAMED flag, which will be
	 cleared in the 'else' branch below */
      for (prev = dir; prev && prev->orig != dir; prev = prev->orig)
	DIR_CLEAR_FLAG (prev, DIRF_RENAMED);

      if (prev == NULL)
	{
	  for (p = dir; p && p->orig; p = p->orig)
	    obstack_code_rename (stk, p->orig->name, p->name);
	}
      else
	{
	  char *temp_name;

	  DIR_CLEAR_FLAG (prev, DIRF_RENAMED);

	  /* Break the cycle by using a temporary name for one of its
	     elements.
	     First, create a temp name stub entry. */
	  temp_name = dir_name (dir->name);
	  obstack_1grow (stk, 'X');
	  obstack_grow (stk, temp_name, strlen (temp_name) + 1);

	  obstack_code_rename (stk, dir->name, "");

	  for (p = dir; p != prev; p = p->orig)
	    obstack_code_rename (stk, p->orig->name, p->name);

	  obstack_code_rename (stk, "", prev->name);
	  free (temp_name);
	}
    }
}

void
append_incremental_renames (struct directory *dir)
{
  struct obstack stk;
  size_t size;
  struct directory *dp;
  const char *dump;

  if (dirhead == NULL)
    return;

  obstack_init (&stk);
  dump = directory_contents (dir);
  if (dump)
    {
      size = dumpdir_size (dump) - 1;
      obstack_grow (&stk, dump, size);
    }
  else
    size = 0;

  for (dp = dirhead; dp; dp = dp->next)
    store_rename (dp, &stk);

  /* FIXME: Is this the right thing to do when DIR is null?  */
  if (dir && obstack_object_size (&stk) != size)
    {
      obstack_1grow (&stk, 0);
      dumpdir_free (dir->dump);
      dir->dump = dumpdir_create (obstack_finish (&stk));
    }
  obstack_free (&stk, NULL);
}



static FILE *listed_incremental_stream;

/* Version of incremental format snapshots (directory files) used by this
   tar. Currently it is supposed to be a single decimal number. 0 means
   incremental snapshots as per tar version before 1.15.2.

   The current tar version supports incremental versions from
   0 up to TAR_INCREMENTAL_VERSION, inclusive.
   It is able to create only snapshots of TAR_INCREMENTAL_VERSION */

#define TAR_INCREMENTAL_VERSION 2

/* Read incremental snapshot formats 0 and 1 */
static void
read_incr_db_01 (int version, const char *initbuf)
{
  int n;
  uintmax_t u;
  char *buf = NULL;
  size_t bufsize = 0;
  char *ebuf;
  long lineno = 1;

  if (version == 1)
    {
      if (getline (&buf, &bufsize, listed_incremental_stream) <= 0)
	{
	  read_error (listed_incremental_option);
	  free (buf);
	  return;
	}
      ++lineno;
    }
  else
    {
      buf = strdup (initbuf);
      bufsize = strlen (buf) + 1;
    }

  newer_mtime_option = decode_timespec (buf, &ebuf, false);

  if (! valid_timespec (newer_mtime_option))
    ERROR ((0, errno, "%s:%ld: %s",
	    quotearg_colon (listed_incremental_option),
	    lineno,
	    _("Invalid time stamp")));
  else
    {
      if (version == 1 && *ebuf)
	{
	  char const *buf_ns = ebuf + 1;
	  errno = 0;
	  u = strtoumax (buf_ns, &ebuf, 10);
	  if (!errno && BILLION <= u)
	    errno = ERANGE;
	  if (errno || buf_ns == ebuf)
	    {
	      ERROR ((0, errno, "%s:%ld: %s",
		      quotearg_colon (listed_incremental_option),
		      lineno,
		      _("Invalid time stamp")));
	      newer_mtime_option.tv_sec = TYPE_MINIMUM (time_t);
	      newer_mtime_option.tv_nsec = -1;
	    }
	  else
	    newer_mtime_option.tv_nsec = u;
	}
    }

  while (0 < (n = getline (&buf, &bufsize, listed_incremental_stream)))
    {
      dev_t dev;
      ino_t ino;
      bool nfs = buf[0] == '+';
      char *strp = buf + nfs;
      struct timespec mtime;

      lineno++;

      if (buf[n - 1] == '\n')
	buf[n - 1] = '\0';

      if (version == 1)
	{
	  mtime = decode_timespec (strp, &ebuf, false);
	  strp = ebuf;
	  if (!valid_timespec (mtime) || *strp != ' ')
	    ERROR ((0, errno, "%s:%ld: %s",
		    quotearg_colon (listed_incremental_option), lineno,
		    _("Invalid modification time")));

	  errno = 0;
	  u = strtoumax (strp, &ebuf, 10);
	  if (!errno && BILLION <= u)
	    errno = ERANGE;
	  if (errno || strp == ebuf || *ebuf != ' ')
	    {
	      ERROR ((0, errno, "%s:%ld: %s",
		      quotearg_colon (listed_incremental_option), lineno,
		      _("Invalid modification time (nanoseconds)")));
	      mtime.tv_nsec = -1;
	    }
	  else
	    mtime.tv_nsec = u;
	  strp = ebuf;
	}
      else
	mtime.tv_sec = mtime.tv_nsec = 0;

      dev = strtosysint (strp, &ebuf,
			 TYPE_MINIMUM (dev_t), TYPE_MAXIMUM (dev_t));
      strp = ebuf;
      if (errno || *strp != ' ')
	ERROR ((0, errno, "%s:%ld: %s",
		quotearg_colon (listed_incremental_option), lineno,
		_("Invalid device number")));

      ino = strtosysint (strp, &ebuf,
			 TYPE_MINIMUM (ino_t), TYPE_MAXIMUM (ino_t));
      strp = ebuf;
      if (errno || *strp != ' ')
	ERROR ((0, errno, "%s:%ld: %s",
		quotearg_colon (listed_incremental_option), lineno,
		_("Invalid inode number")));

      strp++;
      unquote_string (strp);
      note_directory (strp, mtime, dev, ino, nfs, false, NULL);
    }
  free (buf);
}

/* Read a nul-terminated string from FP and store it in STK.
   Store the number of bytes read (including nul terminator) in PCOUNT.

   Return the last character read or EOF on end of file. */
static int
read_obstack (FILE *fp, struct obstack *stk, size_t *pcount)
{
  int c;
  size_t i;

  for (i = 0, c = getc (fp); c != EOF && c != 0; c = getc (fp), i++)
    obstack_1grow (stk, c);
  obstack_1grow (stk, 0);

  *pcount = i;
  return c;
}

/* Read from file FP a null-terminated string and convert it to an
   integer.  FIELDNAME is the intended use of the integer, useful for
   diagnostics.  MIN_VAL and MAX_VAL are its minimum and maximum
   permissible values; MIN_VAL must be nonpositive and MAX_VAL positive.
   Store into *PVAL the resulting value, converted to intmax_t.

   Throw a fatal error if the string cannot be converted or if the
   converted value is out of range.

   Return true if successful, false if end of file.  */

static bool
read_num (FILE *fp, char const *fieldname,
	  intmax_t min_val, uintmax_t max_val, intmax_t *pval)
{
  int i;
  char buf[INT_BUFSIZE_BOUND (intmax_t)];
  char offbuf[INT_BUFSIZE_BOUND (off_t)];
  char minbuf[INT_BUFSIZE_BOUND (intmax_t)];
  char maxbuf[INT_BUFSIZE_BOUND (intmax_t)];
  int conversion_errno;
  int c = getc (fp);
  bool negative = c == '-';

  for (i = 0; (i == 0 && negative) || ISDIGIT (c); i++)
    {
      buf[i] = c;
      if (i == sizeof buf - 1)
	FATAL_ERROR ((0, 0,
		      _("%s: byte %s: %s %.*s... too long"),
		      quotearg_colon (listed_incremental_option),
		      offtostr (ftello (fp), offbuf),
		      fieldname, i + 1, buf));
      c = getc (fp);
    }

  buf[i] = 0;

  if (c < 0)
    {
      if (ferror (fp))
	read_fatal (listed_incremental_option);
      if (i != 0)
	FATAL_ERROR ((0, 0, "%s: %s",
		      quotearg_colon (listed_incremental_option),
		      _("Unexpected EOF in snapshot file")));
      return false;
    }

  if (c)
    {
      unsigned uc = c;
      FATAL_ERROR ((0, 0,
		    _("%s: byte %s: %s %s followed by invalid byte 0x%02x"),
		    quotearg_colon (listed_incremental_option),
		    offtostr (ftello (fp), offbuf),
		    fieldname, buf, uc));
    }

  *pval = strtosysint (buf, NULL, min_val, max_val);
  conversion_errno = errno;

  switch (conversion_errno)
    {
    case ERANGE:
      FATAL_ERROR ((0, conversion_errno,
		    _("%s: byte %s: (valid range %s..%s)\n\t%s %s"),
		    quotearg_colon (listed_incremental_option),
		    offtostr (ftello (fp), offbuf),
		    imaxtostr (min_val, minbuf),
		    umaxtostr (max_val, maxbuf), fieldname, buf));
    default:
      FATAL_ERROR ((0, conversion_errno,
		    _("%s: byte %s: %s %s"),
		    quotearg_colon (listed_incremental_option),
		    offtostr (ftello (fp), offbuf), fieldname, buf));
    case 0:
      break;
    }

  return true;
}

/* Read from FP two NUL-terminated strings representing a struct
   timespec.  Return the resulting value in PVAL.

   Throw a fatal error if the string cannot be converted.  */

static void
read_timespec (FILE *fp, struct timespec *pval)
{
  intmax_t s, ns;

  if (read_num (fp, "sec", TYPE_MINIMUM (time_t), TYPE_MAXIMUM (time_t), &s)
      && read_num (fp, "nsec", 0, BILLION - 1, &ns))
    {
      pval->tv_sec = s;
      pval->tv_nsec = ns;
    }
  else
    {
      FATAL_ERROR ((0, 0, "%s: %s",
		    quotearg_colon (listed_incremental_option),
		    _("Unexpected EOF in snapshot file")));
    }
}

/* Read incremental snapshot format 2 */
static void
read_incr_db_2 (void)
{
  struct obstack stk;
  char offbuf[INT_BUFSIZE_BOUND (off_t)];

  obstack_init (&stk);

  read_timespec (listed_incremental_stream, &newer_mtime_option);

  for (;;)
    {
      intmax_t i;
      struct timespec mtime;
      dev_t dev;
      ino_t ino;
      bool nfs;
      char *name;
      char *content;
      size_t s;

      if (! read_num (listed_incremental_stream, "nfs", 0, 1, &i))
	return; /* Normal return */

      nfs = i;

      read_timespec (listed_incremental_stream, &mtime);

      if (! read_num (listed_incremental_stream, "dev",
		      TYPE_MINIMUM (dev_t), TYPE_MAXIMUM (dev_t), &i))
	break;
      dev = i;

      if (! read_num (listed_incremental_stream, "ino",
		      TYPE_MINIMUM (ino_t), TYPE_MAXIMUM (ino_t), &i))
	break;
      ino = i;

      if (read_obstack (listed_incremental_stream, &stk, &s))
	break;

      name = obstack_finish (&stk);

      while (read_obstack (listed_incremental_stream, &stk, &s) == 0 && s > 1)
	;
      if (getc (listed_incremental_stream) != 0)
	FATAL_ERROR ((0, 0, _("%s: byte %s: %s"),
		      quotearg_colon (listed_incremental_option),
		      offtostr (ftello (listed_incremental_stream), offbuf),
		      _("Missing record terminator")));

      content = obstack_finish (&stk);
      note_directory (name, mtime, dev, ino, nfs, false, content);
      obstack_free (&stk, content);
    }
  FATAL_ERROR ((0, 0, "%s: %s",
		quotearg_colon (listed_incremental_option),
		_("Unexpected EOF in snapshot file")));
}

/* Display (to stdout) the range of allowed values for each field
   in the snapshot file.  The array below should be kept in sync
   with any changes made to the read_num() calls in the parsing
   loop inside read_incr_db_2().

   (This function is invoked via the --show-snapshot-field-ranges
   command line option.) */

struct field_range
{
  char const *fieldname;
  intmax_t min_val;
  uintmax_t max_val;
};

static struct field_range const field_ranges[] = {
  { "nfs", 0, 1 },
  { "timestamp_sec", TYPE_MINIMUM (time_t), TYPE_MAXIMUM (time_t) },
  { "timestamp_nsec", 0, BILLION - 1 },
  { "dev", TYPE_MINIMUM (dev_t), TYPE_MAXIMUM (dev_t) },
  { "ino", TYPE_MINIMUM (ino_t), TYPE_MAXIMUM (ino_t) },
  { NULL, 0, 0 }
};

void
show_snapshot_field_ranges (void)
{
  struct field_range const *p;
  char minbuf[SYSINT_BUFSIZE];
  char maxbuf[SYSINT_BUFSIZE];

  printf("This tar's snapshot file field ranges are\n");
  printf ("   (%-15s => [ %s, %s ]):\n\n", "field name", "min", "max");

  for (p=field_ranges; p->fieldname != NULL; p++)
    {
      printf ("    %-15s => [ %s, %s ],\n", p->fieldname,
	      sysinttostr (p->min_val, p->min_val, p->max_val, minbuf),
	      sysinttostr (p->max_val, p->min_val, p->max_val, maxbuf));

    }

  printf("\n");
}

/* Read incremental snapshot file (directory file).
   If the file has older incremental version, make sure that it is processed
   correctly and that tar will use the most conservative backup method among
   possible alternatives (i.e. prefer ALL_CHILDREN over CHANGED_CHILDREN,
   etc.) This ensures that the snapshots are updated to the recent version
   without any loss of data. */
void
read_directory_file (void)
{
  int fd;
  char *buf = NULL;
  size_t bufsize = 0;
  int flags = O_RDWR | O_CREAT;

  if (incremental_level == 0)
    flags |= O_TRUNC;
  /* Open the file for both read and write.  That way, we can write
     it later without having to reopen it, and don't have to worry if
     we chdir in the meantime.  */
  fd = open (listed_incremental_option, flags, MODE_RW);
  if (fd < 0)
    {
      open_error (listed_incremental_option);
      return;
    }

  listed_incremental_stream = fdopen (fd, "r+");
  if (! listed_incremental_stream)
    {
      open_error (listed_incremental_option);
      close (fd);
      return;
    }

  /* Consume the first name from the name list and reset the
     list afterwards.  This is done to change to the new
     directory, if the first name is a chdir request (-C dir),
     which is necessary to recreate absolute file names. */
  name_from_list ();
  blank_name_list ();

  if (0 < getline (&buf, &bufsize, listed_incremental_stream))
    {
      char *ebuf;
      uintmax_t incremental_version;

      if (strncmp (buf, PACKAGE_NAME, sizeof PACKAGE_NAME - 1) == 0)
	{
	  ebuf = buf + sizeof PACKAGE_NAME - 1;
	  if (*ebuf++ != '-')
	    ERROR((1, 0, _("Bad incremental file format")));
	  for (; *ebuf != '-'; ebuf++)
	    if (!*ebuf)
	      ERROR((1, 0, _("Bad incremental file format")));

	  incremental_version = strtoumax (ebuf + 1, NULL, 10);
	}
      else
	incremental_version = 0;

      switch (incremental_version)
	{
	case 0:
	case 1:
	  read_incr_db_01 (incremental_version, buf);
	  break;

	case TAR_INCREMENTAL_VERSION:
	  read_incr_db_2 ();
	  break;

	default:
	  ERROR ((1, 0, _("Unsupported incremental format version: %"PRIuMAX),
		  incremental_version));
	}

    }

  if (ferror (listed_incremental_stream))
    read_error (listed_incremental_option);
  free (buf);
}

/* Output incremental data for the directory ENTRY to the file DATA.
   Return nonzero if successful, preserving errno on write failure.  */
static bool
write_directory_file_entry (void *entry, void *data)
{
  struct directory const *directory = entry;
  FILE *fp = data;

  if (DIR_IS_FOUND (directory))
    {
      char buf[SYSINT_BUFSIZE];
      char const *s;

      s = DIR_IS_NFS (directory) ? "1" : "0";
      fwrite (s, 2, 1, fp);
      s = sysinttostr (directory->mtime.tv_sec, TYPE_MINIMUM (time_t),
		       TYPE_MAXIMUM (time_t), buf);
      fwrite (s, strlen (s) + 1, 1, fp);
      s = imaxtostr (directory->mtime.tv_nsec, buf);
      fwrite (s, strlen (s) + 1, 1, fp);
      s = sysinttostr (directory->device_number,
		       TYPE_MINIMUM (dev_t), TYPE_MAXIMUM (dev_t), buf);
      fwrite (s, strlen (s) + 1, 1, fp);
      s = sysinttostr (directory->inode_number,
		       TYPE_MINIMUM (ino_t), TYPE_MAXIMUM (ino_t), buf);
      fwrite (s, strlen (s) + 1, 1, fp);

      fwrite (directory->name, strlen (directory->name) + 1, 1, fp);
      if (directory->dump)
	{
	  const char *p;
	  struct dumpdir_iter *itr;

	  for (p = dumpdir_first (directory->dump, 0, &itr);
	       p;
	       p = dumpdir_next (itr))
	    fwrite (p, strlen (p) + 1, 1, fp);
	  free (itr);
	}
      fwrite ("\0\0", 2, 1, fp);
    }

  return ! ferror (fp);
}

void
write_directory_file (void)
{
  FILE *fp = listed_incremental_stream;
  char buf[UINTMAX_STRSIZE_BOUND];
  char *s;

  if (! fp)
    return;

  if (fseeko (fp, 0L, SEEK_SET) != 0)
    seek_error (listed_incremental_option);
  if (sys_truncate (fileno (fp)) != 0)
    truncate_error (listed_incremental_option);

  fprintf (fp, "%s-%s-%d\n", PACKAGE_NAME, PACKAGE_VERSION,
	   TAR_INCREMENTAL_VERSION);

  s = (TYPE_SIGNED (time_t)
       ? imaxtostr (start_time.tv_sec, buf)
       : umaxtostr (start_time.tv_sec, buf));
  fwrite (s, strlen (s) + 1, 1, fp);
  s = umaxtostr (start_time.tv_nsec, buf);
  fwrite (s, strlen (s) + 1, 1, fp);

  if (! ferror (fp) && directory_table)
    hash_do_for_each (directory_table, write_directory_file_entry, fp);

  if (ferror (fp))
    write_error (listed_incremental_option);
  if (fclose (fp) != 0)
    close_error (listed_incremental_option);
}


/* Restoration of incremental dumps.  */

static void
get_gnu_dumpdir (struct tar_stat_info *stat_info)
{
  size_t size;
  size_t copied;
  union block *data_block;
  char *to;
  char *archive_dir;

  size = stat_info->stat.st_size;

  archive_dir = xmalloc (size);
  to = archive_dir;

  set_next_block_after (current_header);
  mv_begin_read (stat_info);

  for (; size > 0; size -= copied)
    {
      mv_size_left (size);
      data_block = find_next_block ();
      if (!data_block)
	ERROR ((1, 0, _("Unexpected EOF in archive")));
      copied = available_space_after (data_block);
      if (copied > size)
	copied = size;
      memcpy (to, data_block->buffer, copied);
      to += copied;
      set_next_block_after ((union block *)
			    (data_block->buffer + copied - 1));
    }

  mv_end ();

  stat_info->dumpdir = archive_dir;
  stat_info->skipped = true; /* For skip_member() and friends
				to work correctly */
}

/* Return T if STAT_INFO represents a dumpdir archive member.
   Note: can invalidate current_header. It happens if flush_archive()
   gets called within get_gnu_dumpdir() */
bool
is_dumpdir (struct tar_stat_info *stat_info)
{
  if (stat_info->is_dumpdir && !stat_info->dumpdir)
    get_gnu_dumpdir (stat_info);
  return stat_info->is_dumpdir;
}

static bool
dumpdir_ok (char *dumpdir)
{
  char *p;
  int has_tempdir = 0;
  int expect = 0;

  for (p = dumpdir; *p; p += strlen (p) + 1)
    {
      if (expect && *p != expect)
	{
	  unsigned char uc = *p;
	  ERROR ((0, 0,
		  _("Malformed dumpdir: expected '%c' but found %#3o"),
		  expect, uc));
	  return false;
	}
      switch (*p)
	{
	case 'X':
	  if (has_tempdir)
	    {
	      ERROR ((0, 0,
		      _("Malformed dumpdir: 'X' duplicated")));
	      return false;
	    }
	  else
	    has_tempdir = 1;
	  break;

	case 'R':
	  if (p[1] == 0)
	    {
	      if (!has_tempdir)
		{
		  ERROR ((0, 0,
			  _("Malformed dumpdir: empty name in 'R'")));
		  return false;
		}
	      else
		has_tempdir = 0;
	    }
	  expect = 'T';
	  break;

	case 'T':
	  if (expect != 'T')
	    {
	      ERROR ((0, 0,
		      _("Malformed dumpdir: 'T' not preceded by 'R'")));
	      return false;
	    }
	  if (p[1] == 0 && !has_tempdir)
	    {
	      ERROR ((0, 0,
		      _("Malformed dumpdir: empty name in 'T'")));
	      return false;
	    }
	  expect = 0;
	  break;

	case 'N':
	case 'Y':
	case 'D':
	  break;

	default:
	  /* FIXME: bail out? */
	  break;
	}
    }

  if (expect)
    {
      ERROR ((0, 0,
	      _("Malformed dumpdir: expected '%c' but found end of data"),
	      expect));
      return false;
    }

  if (has_tempdir)
    WARNOPT (WARN_BAD_DUMPDIR,
	     (0, 0, _("Malformed dumpdir: 'X' never used")));

  return true;
}

/* Examine the directories under directory_name and delete any
   files that were not there at the time of the back-up. */
static bool
try_purge_directory (char const *directory_name)
{
  char *current_dir;
  char *cur, *arc, *p;
  char *temp_stub = NULL;
  struct dumpdir *dump;

  if (!is_dumpdir (&current_stat_info))
    return false;

  current_dir = tar_savedir (directory_name, 0);

  if (!current_dir)
    /* The directory doesn't exist now.  It'll be created.  In any
       case, we don't have to delete any files out of it.  */
    return false;

  /* Verify if dump directory is sane */
  if (!dumpdir_ok (current_stat_info.dumpdir))
    return false;

  /* Process renames */
  for (arc = current_stat_info.dumpdir; *arc; arc += strlen (arc) + 1)
    {
      if (*arc == 'X')
	{
#define TEMP_DIR_TEMPLATE "tar.XXXXXX"
	  size_t len = strlen (arc + 1);
	  temp_stub = xrealloc (temp_stub, len + 1 + sizeof TEMP_DIR_TEMPLATE);
	  memcpy (temp_stub, arc + 1, len);
	  temp_stub[len] = '/';
	  memcpy (temp_stub + len + 1, TEMP_DIR_TEMPLATE,
		  sizeof TEMP_DIR_TEMPLATE);
	  if (!mkdtemp (temp_stub))
	    {
	      ERROR ((0, errno,
		      _("Cannot create temporary directory using template %s"),
		      quote (temp_stub)));
	      free (temp_stub);
	      free (current_dir);
	      return false;
	    }
	}
      else if (*arc == 'R')
	{
	  char *src, *dst;
	  src = arc + 1;
	  arc += strlen (arc) + 1;
	  dst = arc + 1;

	  /* Ensure that neither source nor destination are absolute file
	     names (unless permitted by -P option), and that they do not
	     contain dubious parts (e.g. ../).

	     This is an extra safety precaution. Besides, it might be
	     necessary to extract from archives created with tar versions
	     prior to 1.19. */

	  if (*src)
	    src = safer_name_suffix (src, false, absolute_names_option);
	  if (*dst)
	    dst = safer_name_suffix (dst, false, absolute_names_option);

	  if (*src == 0)
	    src = temp_stub;
	  else if (*dst == 0)
	    dst = temp_stub;

	  if (!rename_directory (src, dst))
	    {
	      free (temp_stub);
	      free (current_dir);
	      /* FIXME: Make sure purge_directory(dst) will return
		 immediately */
	      return false;
	    }
	}
    }

  free (temp_stub);

  /* Process deletes */
  dump = dumpdir_create (current_stat_info.dumpdir);
  p = NULL;
  for (cur = current_dir; *cur; cur += strlen (cur) + 1)
    {
      const char *entry;
      struct stat st;
      free (p);
      p = make_file_name (directory_name, cur);

      if (deref_stat (p, &st) != 0)
	{
	  if (errno != ENOENT) /* FIXME: Maybe keep a list of renamed
				  dirs and check it here? */
	    {
	      stat_diag (p);
	      WARN ((0, 0, _("%s: Not purging directory: unable to stat"),
		     quotearg_colon (p)));
	    }
	  continue;
	}

      if (!(entry = dumpdir_locate (dump, cur))
	  || (*entry == 'D' && !S_ISDIR (st.st_mode))
	  || (*entry == 'Y' && S_ISDIR (st.st_mode)))
	{
	  if (one_file_system_option && st.st_dev != root_device)
	    {
	      WARN ((0, 0,
		     _("%s: directory is on a different device: not purging"),
		     quotearg_colon (p)));
	      continue;
	    }

	  if (! interactive_option || confirm ("delete", p))
	    {
	      if (verbose_option)
		fprintf (stdlis, _("%s: Deleting %s\n"),
			 program_name, quote (p));
	      if (! remove_any_file (p, RECURSIVE_REMOVE_OPTION))
		{
		  int e = errno;
		  ERROR ((0, e, _("%s: Cannot remove"), quotearg_colon (p)));
		}
	    }
	}
    }
  free (p);
  dumpdir_free (dump);

  free (current_dir);
  return true;
}

void
purge_directory (char const *directory_name)
{
  if (!try_purge_directory (directory_name))
    skip_member ();
}

void
list_dumpdir (char *buffer, size_t size)
{
  int state = 0;
  while (size)
    {
      switch (*buffer)
	{
	case 'Y':
	case 'N':
	case 'D':
	case 'R':
	case 'T':
	case 'X':
	  fprintf (stdlis, "%c", *buffer);
	  if (state == 0)
	    {
	      fprintf (stdlis, " ");
	      state = 1;
	    }
	  buffer++;
	  size--;
	  break;

	case 0:
	  fputc ('\n', stdlis);
	  buffer++;
	  size--;
	  state = 0;
	  break;

	default:
	  fputc (*buffer, stdlis);
	  buffer++;
	  size--;
	}
    }
}
