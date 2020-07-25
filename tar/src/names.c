/* Various processing of names.

   Copyright 1988, 1992, 1994, 1996-2001, 2003-2007, 2009, 2013-2017
   Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 3, or (at your option) any later
   version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
   Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <system.h>

#include <fnmatch.h>
#include <hash.h>
#include <quotearg.h>
#include <wordsplit.h>
#include <argp.h>

#include "common.h"

static void name_add_option (int option, const char *arg);
static void name_add_dir (const char *name);
static void name_add_file (const char *name);

enum
  {
    ADD_FILE_OPTION = 256,
    EXCLUDE_BACKUPS_OPTION,
    EXCLUDE_CACHES_OPTION,
    EXCLUDE_CACHES_UNDER_OPTION,
    EXCLUDE_CACHES_ALL_OPTION,
    EXCLUDE_OPTION,
    EXCLUDE_IGNORE_OPTION,
    EXCLUDE_IGNORE_RECURSIVE_OPTION,
    EXCLUDE_TAG_OPTION,
    EXCLUDE_TAG_UNDER_OPTION,
    EXCLUDE_TAG_ALL_OPTION,
    EXCLUDE_VCS_OPTION,
    EXCLUDE_VCS_IGNORES_OPTION,
    IGNORE_CASE_OPTION,
    NO_IGNORE_CASE_OPTION,
    ANCHORED_OPTION,
    NO_ANCHORED_OPTION,
    RECURSION_OPTION,
    NO_RECURSION_OPTION,
    UNQUOTE_OPTION,
    NO_UNQUOTE_OPTION,
    NO_VERBATIM_FILES_FROM_OPTION,
    NO_WILDCARDS_MATCH_SLASH_OPTION,
    NO_WILDCARDS_OPTION,
    NULL_OPTION,
    NO_NULL_OPTION,
    VERBATIM_FILES_FROM_OPTION,
    WILDCARDS_MATCH_SLASH_OPTION,
    WILDCARDS_OPTION
  };

static struct argp_option names_options[] = {
#define GRID 100
  {NULL, 0, NULL, 0,
   N_("Local file name selection:"), GRID },

  {"add-file", ADD_FILE_OPTION, N_("FILE"), 0,
   N_("add given FILE to the archive (useful if its name starts with a dash)"), GRID+1 },
  {"directory", 'C', N_("DIR"), 0,
   N_("change to directory DIR"), GRID+1 },
  {"files-from", 'T', N_("FILE"), 0,
   N_("get names to extract or create from FILE"), GRID+1 },
  {"null", NULL_OPTION, 0, 0,
   N_("-T reads null-terminated names; implies --verbatim-files-from"),
      GRID+1 },
  {"no-null", NO_NULL_OPTION, 0, 0,
   N_("disable the effect of the previous --null option"), GRID+1 },
  {"unquote", UNQUOTE_OPTION, 0, 0,
   N_("unquote input file or member names (default)"), GRID+1 },
  {"no-unquote", NO_UNQUOTE_OPTION, 0, 0,
   N_("do not unquote input file or member names"), GRID+1 },
  {"verbatim-files-from", VERBATIM_FILES_FROM_OPTION, 0, 0,
   N_("-T reads file names verbatim (no escape or option handling)"), GRID+1 },
  {"no-verbatim-files-from", NO_VERBATIM_FILES_FROM_OPTION, 0, 0,
   N_("-T treats file names starting with dash as options (default)"),
      GRID+1 },
  {"exclude", EXCLUDE_OPTION, N_("PATTERN"), 0,
   N_("exclude files, given as a PATTERN"), GRID+1 },
  {"exclude-from", 'X', N_("FILE"), 0,
   N_("exclude patterns listed in FILE"), GRID+1 },
  {"exclude-caches", EXCLUDE_CACHES_OPTION, 0, 0,
   N_("exclude contents of directories containing CACHEDIR.TAG, "
      "except for the tag file itself"), GRID+1 },
  {"exclude-caches-under", EXCLUDE_CACHES_UNDER_OPTION, 0, 0,
   N_("exclude everything under directories containing CACHEDIR.TAG"),
   GRID+1 },
  {"exclude-caches-all", EXCLUDE_CACHES_ALL_OPTION, 0, 0,
   N_("exclude directories containing CACHEDIR.TAG"), GRID+1 },
  {"exclude-tag", EXCLUDE_TAG_OPTION, N_("FILE"), 0,
   N_("exclude contents of directories containing FILE, except"
      " for FILE itself"), GRID+1 },
  {"exclude-ignore", EXCLUDE_IGNORE_OPTION, N_("FILE"), 0,
    N_("read exclude patterns for each directory from FILE, if it exists"),
   GRID+1 },
  {"exclude-ignore-recursive", EXCLUDE_IGNORE_RECURSIVE_OPTION, N_("FILE"), 0,
    N_("read exclude patterns for each directory and its subdirectories "
       "from FILE, if it exists"), GRID+1 },
  {"exclude-tag-under", EXCLUDE_TAG_UNDER_OPTION, N_("FILE"), 0,
   N_("exclude everything under directories containing FILE"), GRID+1 },
  {"exclude-tag-all", EXCLUDE_TAG_ALL_OPTION, N_("FILE"), 0,
   N_("exclude directories containing FILE"), GRID+1 },
  {"exclude-vcs", EXCLUDE_VCS_OPTION, NULL, 0,
   N_("exclude version control system directories"), GRID+1 },
  {"exclude-vcs-ignores", EXCLUDE_VCS_IGNORES_OPTION, NULL, 0,
   N_("read exclude patterns from the VCS ignore files"), GRID+1 },
  {"exclude-backups", EXCLUDE_BACKUPS_OPTION, NULL, 0,
   N_("exclude backup and lock files"), GRID+1 },
  {"recursion", RECURSION_OPTION, 0, 0,
   N_("recurse into directories (default)"), GRID+1 },
  {"no-recursion", NO_RECURSION_OPTION, 0, 0,
   N_("avoid descending automatically in directories"), GRID+1 },
#undef GRID

#define GRID 120
  {NULL, 0, NULL, 0,
   N_("File name matching options (affect both exclude and include patterns):"),
   GRID },
  {"anchored", ANCHORED_OPTION, 0, 0,
   N_("patterns match file name start"), GRID+1 },
  {"no-anchored", NO_ANCHORED_OPTION, 0, 0,
   N_("patterns match after any '/' (default for exclusion)"), GRID+1 },
  {"ignore-case", IGNORE_CASE_OPTION, 0, 0,
   N_("ignore case"), GRID+1 },
  {"no-ignore-case", NO_IGNORE_CASE_OPTION, 0, 0,
   N_("case sensitive matching (default)"), GRID+1 },
  {"wildcards", WILDCARDS_OPTION, 0, 0,
   N_("use wildcards (default for exclusion)"), GRID+1 },
  {"no-wildcards", NO_WILDCARDS_OPTION, 0, 0,
   N_("verbatim string matching"), GRID+1 },
  {"wildcards-match-slash", WILDCARDS_MATCH_SLASH_OPTION, 0, 0,
   N_("wildcards match '/' (default for exclusion)"), GRID+1 },
  {"no-wildcards-match-slash", NO_WILDCARDS_MATCH_SLASH_OPTION, 0, 0,
   N_("wildcards do not match '/'"), GRID+1 },
#undef GRID

  {NULL}
};

static struct argp_option const *
file_selection_option (int key)
{
  struct argp_option *p;

  for (p = names_options;
       !(p->name == NULL && p->key == 0 && p->doc == NULL); p++)
    if (p->key == key)
      return p;
  return NULL;
}

static char const *
file_selection_option_name (int key)
{
  struct argp_option const *opt = file_selection_option (key);
  return opt ? opt->name : NULL;
}

static bool
is_file_selection_option (int key)
{
  return file_selection_option (key) != NULL;
}

/* Either NL or NUL, as decided by the --null option.  */
static char filename_terminator = '\n';
/* Treat file names read from -T input verbatim */
static bool verbatim_files_from_option;

static error_t
names_parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case 'C':
      name_add_dir (arg);
      break;

    case 'T':
      name_add_file (arg);
      break;

    case ADD_FILE_OPTION:
      name_add_name (arg);
      break;

    default:
      if (is_file_selection_option (key))
	name_add_option (key, arg);
      else
	return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

/* Wildcard matching settings */
enum wildcards
  {
    default_wildcards, /* For exclusion == enable_wildcards,
			  for inclusion == disable_wildcards */
    disable_wildcards,
    enable_wildcards
  };

static enum wildcards wildcards = default_wildcards;
  /* Wildcard settings (--wildcards/--no-wildcards) */
static int matching_flags = 0;
  /* exclude_fnmatch options */
static int include_anchored = EXCLUDE_ANCHORED;
  /* Pattern anchoring options used for file inclusion */

#define EXCLUDE_OPTIONS						\
  (((wildcards != disable_wildcards) ? EXCLUDE_WILDCARDS : 0)	\
  | matching_flags						\
  | recursion_option)

#define INCLUDE_OPTIONS						    \
  (((wildcards == enable_wildcards) ? EXCLUDE_WILDCARDS : 0)	    \
  | include_anchored						    \
  | matching_flags						    \
  | recursion_option)

static char const * const vcs_file_table[] = {
  /* CVS: */
  "CVS",
  ".cvsignore",
  /* RCS: */
  "RCS",
  /* SCCS: */
  "SCCS",
  /* SVN: */
  ".svn",
  /* git: */
  ".git",
  ".gitignore",
  ".gitattributes",
  ".gitmodules",
  /* Arch: */
  ".arch-ids",
  "{arch}",
  "=RELEASE-ID",
  "=meta-update",
  "=update",
  /* Bazaar */
  ".bzr",
  ".bzrignore",
  ".bzrtags",
  /* Mercurial */
  ".hg",
  ".hgignore",
  ".hgtags",
  /* darcs */
  "_darcs",
  NULL
};

static char const * const backup_file_table[] = {
  ".#*",
  "*~",
  "#*#",
  NULL
};

static void
add_exclude_array (char const * const * fv, int opts)
{
  int i;

  for (i = 0; fv[i]; i++)
    add_exclude (excluded, fv[i], opts);
}

static void
handle_file_selection_option (int key, const char *arg)
{
  switch (key)
    {
    case EXCLUDE_BACKUPS_OPTION:
      add_exclude_array (backup_file_table, EXCLUDE_WILDCARDS);
      break;

    case EXCLUDE_OPTION:
      add_exclude (excluded, arg, EXCLUDE_OPTIONS);
      break;

    case EXCLUDE_CACHES_OPTION:
      add_exclusion_tag ("CACHEDIR.TAG", exclusion_tag_contents,
			 cachedir_file_p);
      break;

    case EXCLUDE_CACHES_UNDER_OPTION:
      add_exclusion_tag ("CACHEDIR.TAG", exclusion_tag_under,
			 cachedir_file_p);
      break;

    case EXCLUDE_CACHES_ALL_OPTION:
      add_exclusion_tag ("CACHEDIR.TAG", exclusion_tag_all,
			 cachedir_file_p);
      break;

    case EXCLUDE_IGNORE_OPTION:
      excfile_add (arg, EXCL_NON_RECURSIVE);
      break;

    case EXCLUDE_IGNORE_RECURSIVE_OPTION:
      excfile_add (arg, EXCL_RECURSIVE);
      break;

    case EXCLUDE_TAG_OPTION:
      add_exclusion_tag (arg, exclusion_tag_contents, NULL);
      break;

    case EXCLUDE_TAG_UNDER_OPTION:
      add_exclusion_tag (arg, exclusion_tag_under, NULL);
      break;

    case EXCLUDE_TAG_ALL_OPTION:
      add_exclusion_tag (arg, exclusion_tag_all, NULL);
      break;

    case EXCLUDE_VCS_OPTION:
      add_exclude_array (vcs_file_table, 0);
      break;

    case EXCLUDE_VCS_IGNORES_OPTION:
      exclude_vcs_ignores ();
      break;

    case RECURSION_OPTION:
      recursion_option = FNM_LEADING_DIR;
      break;

    case NO_RECURSION_OPTION:
      recursion_option = 0;
      break;

    case UNQUOTE_OPTION:
      unquote_option = true;
      break;

    case NO_UNQUOTE_OPTION:
      unquote_option = false;
      break;

    case NULL_OPTION:
      filename_terminator = '\0';
      verbatim_files_from_option = true;
      break;

    case NO_NULL_OPTION:
      filename_terminator = '\n';
      verbatim_files_from_option = false;
      break;

    case 'X':
      if (add_exclude_file (add_exclude, excluded, arg, EXCLUDE_OPTIONS, '\n')
	  != 0)
	{
	  int e = errno;
	  FATAL_ERROR ((0, e, "%s", quotearg_colon (arg)));
	}
      break;

    case ANCHORED_OPTION:
      matching_flags |= EXCLUDE_ANCHORED;
      break;

    case NO_ANCHORED_OPTION:
      include_anchored = 0; /* Clear the default for comman line args */
      matching_flags &= ~ EXCLUDE_ANCHORED;
      break;

    case IGNORE_CASE_OPTION:
      matching_flags |= FNM_CASEFOLD;
      break;

    case NO_IGNORE_CASE_OPTION:
      matching_flags &= ~ FNM_CASEFOLD;
      break;

    case WILDCARDS_OPTION:
      wildcards = enable_wildcards;
      break;

    case NO_WILDCARDS_OPTION:
      wildcards = disable_wildcards;
      break;

    case WILDCARDS_MATCH_SLASH_OPTION:
      matching_flags &= ~ FNM_FILE_NAME;
      break;

    case NO_WILDCARDS_MATCH_SLASH_OPTION:
      matching_flags |= FNM_FILE_NAME;
      break;

    case VERBATIM_FILES_FROM_OPTION:
      verbatim_files_from_option = true;
      break;

    case NO_VERBATIM_FILES_FROM_OPTION:
      verbatim_files_from_option = false;
      break;

    default:
      FATAL_ERROR ((0, 0, "unhandled positional option %d", key));
    }
}

static struct argp names_argp = {
  names_options,
  names_parse_opt,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

struct argp_child names_argp_children[] = {
  { &names_argp, 0, "", 0 },
  { NULL }
};

/* User and group names.  */

/* Make sure you link with the proper libraries if you are running the
   Yellow Peril (thanks for the good laugh, Ian J.!), or, euh... NIS.
   This code should also be modified for non-UNIX systems to do something
   reasonable.  */

static char *cached_uname;
static char *cached_gname;

static uid_t cached_uid;	/* valid only if cached_uname is not empty */
static gid_t cached_gid;	/* valid only if cached_gname is not empty */

/* These variables are valid only if nonempty.  */
static char *cached_no_such_uname;
static char *cached_no_such_gname;

/* These variables are valid only if nonzero.  It's not worth optimizing
   the case for weird systems where 0 is not a valid uid or gid.  */
static uid_t cached_no_such_uid;
static gid_t cached_no_such_gid;

/* Given UID, find the corresponding UNAME.  */
void
uid_to_uname (uid_t uid, char **uname)
{
  struct passwd *passwd;

  if (uid != 0 && uid == cached_no_such_uid)
    {
      *uname = xstrdup ("");
      return;
    }

  if (!cached_uname || uid != cached_uid)
    {
      passwd = getpwuid (uid);
      if (passwd)
	{
	  cached_uid = uid;
	  assign_string (&cached_uname, passwd->pw_name);
	}
      else
	{
	  cached_no_such_uid = uid;
	  *uname = xstrdup ("");
	  return;
	}
    }
  *uname = xstrdup (cached_uname);
}

/* Given GID, find the corresponding GNAME.  */
void
gid_to_gname (gid_t gid, char **gname)
{
  struct group *group;

  if (gid != 0 && gid == cached_no_such_gid)
    {
      *gname = xstrdup ("");
      return;
    }

  if (!cached_gname || gid != cached_gid)
    {
      group = getgrgid (gid);
      if (group)
	{
	  cached_gid = gid;
	  assign_string (&cached_gname, group->gr_name);
	}
      else
	{
	  cached_no_such_gid = gid;
	  *gname = xstrdup ("");
	  return;
	}
    }
  *gname = xstrdup (cached_gname);
}

/* Given UNAME, set the corresponding UID and return 1, or else, return 0.  */
int
uname_to_uid (char const *uname, uid_t *uidp)
{
  struct passwd *passwd;

  if (cached_no_such_uname
      && strcmp (uname, cached_no_such_uname) == 0)
    return 0;

  if (!cached_uname
      || uname[0] != cached_uname[0]
      || strcmp (uname, cached_uname) != 0)
    {
      passwd = getpwnam (uname);
      if (passwd)
	{
	  cached_uid = passwd->pw_uid;
	  assign_string (&cached_uname, passwd->pw_name);
	}
      else
	{
	  assign_string (&cached_no_such_uname, uname);
	  return 0;
	}
    }
  *uidp = cached_uid;
  return 1;
}

/* Given GNAME, set the corresponding GID and return 1, or else, return 0.  */
int
gname_to_gid (char const *gname, gid_t *gidp)
{
  struct group *group;

  if (cached_no_such_gname
      && strcmp (gname, cached_no_such_gname) == 0)
    return 0;

  if (!cached_gname
      || gname[0] != cached_gname[0]
      || strcmp (gname, cached_gname) != 0)
    {
      group = getgrnam (gname);
      if (group)
	{
	  cached_gid = group->gr_gid;
	  assign_string (&cached_gname, gname);
	}
      else
	{
	  assign_string (&cached_no_such_gname, gname);
	  return 0;
	}
    }
  *gidp = cached_gid;
  return 1;
}


static struct name *
make_name (const char *file_name)
{
  struct name *p = xzalloc (sizeof (*p));
  if (!file_name)
    file_name = "";
  p->name = xstrdup (file_name);
  p->length = strlen (p->name);
  return p;
}

static void
free_name (struct name *p)
{
  if (p)
    {
      free (p->name);
      free (p->caname);
      free (p);
    }
}


/* Names from the command call.  */

static struct name *namelist;	/* first name in list, if any */
static struct name *nametail;	/* end of name list */

/* File name arguments are processed in two stages: first a
   name element list (see below) is filled, then the names from it
   are moved into the namelist.

   This awkward process is needed only to implement --same-order option,
   which is meant to help process large archives on machines with
   limited memory.  With this option on, namelist contains at most one
   entry, which diminishes the memory consumption.

   However, I very much doubt if we still need this -- Sergey */

/* A name_list element contains entries of three types: */

enum nelt_type
  {
    NELT_NAME,   /* File name */
    NELT_CHDIR,  /* Change directory request */
    NELT_FILE,   /* Read file names from that file */
    NELT_NOOP,   /* No operation */
    NELT_OPTION  /* Filename-selection option */
  };

struct name_elt        /* A name_array element. */
{
  struct name_elt *next, *prev;
  enum nelt_type type; /* Element type, see NELT_* constants above */
  union
  {
    const char *name;  /* File or directory name */
    struct             /* File, if type == NELT_FILE */
    {
      const char *name;/* File name */
      size_t line;     /* Input line number */
      int term;        /* File name terminator in the list */
      bool verbatim;   /* Verbatim handling of file names: no white-space
			  trimming, no option processing */
      FILE *fp;
    } file;
    struct
    {
      int option;
      char const *arg;
    } opt; /* NELT_OPTION */
  } v;
};

static struct name_elt *name_head;/* store a list of names */
size_t name_count;	 	  /* how many of the entries are file names? */

static struct name_elt *
name_elt_alloc (void)
{
  struct name_elt *elt;

  elt = xmalloc (sizeof (*elt));
  if (!name_head)
    {
      name_head = elt;
      name_head->prev = name_head->next = NULL;
      name_head->type = NELT_NOOP;
      elt = xmalloc (sizeof (*elt));
    }

  elt->prev = name_head->prev;
  if (name_head->prev)
    name_head->prev->next = elt;
  elt->next = name_head;
  name_head->prev = elt;
  return elt;
}

static void
name_list_adjust (void)
{
  if (name_head)
    while (name_head->prev)
      name_head = name_head->prev;
}

/* For error-reporting purposes, keep a doubly-linked list of unconsumed file
   selection options.  The option is deemed unconsumed unless followed by one
   or more file/member name arguments.  When archive creation is requested,
   each file selection option encountered is pushed into the list.  The list
   is cleared upon encountering a file name argument.

   If the list is not empty when all arguments have been processed, an error
   message is issued reporting the options that had no effect.

   For simplicity, only a tail pointer of the list is maintained.
*/

struct name_elt *unconsumed_option_tail;

/* Push an option to the list */
static void
unconsumed_option_push (struct name_elt *elt)
{
  elt->next = NULL;
  elt->prev = unconsumed_option_tail;
  if (unconsumed_option_tail)
    unconsumed_option_tail->next = elt;
  unconsumed_option_tail = elt;
}

/* Clear the unconsumed option list */
static void
unconsumed_option_free (void)
{
  while (unconsumed_option_tail)
    {
      struct name_elt *elt = unconsumed_option_tail;
      unconsumed_option_tail = unconsumed_option_tail->prev;
      free (elt);
    }
}

/* Report any options that have not been consumed */
static void
unconsumed_option_report (void)
{
  if (unconsumed_option_tail)
    {
      struct name_elt *elt;

      ERROR ((0, 0, _("The following options were used after any non-optional arguments in archive create or update mode.  These options are positional and affect only arguments that follow them.  Please, rearrange them properly.")));

      elt = unconsumed_option_tail;
      while (elt->prev)
	elt = elt->prev;

      while (elt)
	{
	  switch (elt->type)
	    {
	    case NELT_CHDIR:
	      ERROR ((0, 0, _("-C %s has no effect"), quote (elt->v.name)));
	      break;

	    case NELT_OPTION:
	      if (elt->v.opt.arg)
		ERROR ((0, 0, _("--%s %s has no effect"),
			file_selection_option_name (elt->v.opt.option),
			quote (elt->v.opt.arg)));
	      else
		ERROR ((0, 0, _("--%s has no effect"),
			file_selection_option_name (elt->v.opt.option)));
	      break;

	    default:
	      break;
	    }
	  elt = elt->next;
	}

      unconsumed_option_free ();
    }
}

static void
name_list_advance (void)
{
  struct name_elt *elt = name_head;
  name_head = elt->next;
  if (name_head)
    name_head->prev = NULL;
  if (elt->type == NELT_OPTION || elt->type == NELT_CHDIR)
    {
      if (subcommand_option == CREATE_SUBCOMMAND
	  || subcommand_option == UPDATE_SUBCOMMAND)
	unconsumed_option_push (elt);
    }
  else
    {
      if (elt->type != NELT_NOOP)
	unconsumed_option_free ();
      free (elt);
    }
}

/* Return true if there are names or options in the list */
bool
name_more_files (void)
{
  return name_count > 0;
}

/* Add to name_array the file NAME with fnmatch options MATFLAGS */
void
name_add_name (const char *name)
{
  struct name_elt *ep = name_elt_alloc ();

  ep->type = NELT_NAME;
  ep->v.name = name;
  name_count++;
}

static void
name_add_option (int option, const char *arg)
{
  struct name_elt *elt = name_elt_alloc ();
  elt->type = NELT_OPTION;
  elt->v.opt.option = option;
  elt->v.opt.arg = arg;
}

/* Add to name_array a chdir request for the directory NAME */
static void
name_add_dir (const char *name)
{
  struct name_elt *ep = name_elt_alloc ();
  ep->type = NELT_CHDIR;
  ep->v.name = name;
}

static void
name_add_file (const char *name)
{
  struct name_elt *ep = name_elt_alloc ();

  ep->type = NELT_FILE;
  ep->v.file.name = name;
  ep->v.file.line = 0;
  ep->v.file.fp = NULL;
  name_count++;
}

/* Names from external name file.  */

static char *name_buffer;	/* buffer to hold the current file name */
static size_t name_buffer_length; /* allocated length of name_buffer */

/* Set up to gather file names for tar.  They can either come from a
   file or were saved from decoding arguments.  */
void
name_init (void)
{
  name_buffer = xmalloc (NAME_FIELD_SIZE + 2);
  name_buffer_length = NAME_FIELD_SIZE;
  name_list_adjust ();
}

void
name_term (void)
{
  free (name_buffer);
}

/* Prevent recursive inclusion of the same file */
struct file_id_list
{
  struct file_id_list *next;
  ino_t ino;
  dev_t dev;
  const char *from_file;
};

static struct file_id_list *file_id_list;

/* Return the name of the file from which the file names and options
   are being read.
*/
static const char *
file_list_name (void)
{
  struct name_elt *elt;

  for (elt = name_head; elt; elt = elt->next)
    if (elt->type == NELT_FILE && elt->v.file.fp)
      return elt->v.file.name;
  return _("command line");
}

static int
add_file_id (const char *filename)
{
  struct file_id_list *p;
  struct stat st;
  const char *reading_from;

  if (stat (filename, &st))
    stat_fatal (filename);
  reading_from = file_list_name ();
  for (p = file_id_list; p; p = p->next)
    if (p->ino == st.st_ino && p->dev == st.st_dev)
      {
	int oldc = set_char_quoting (NULL, ':', 1);
	ERROR ((0, 0,
		_("%s: file list requested from %s already read from %s"),
		quotearg_n (0, filename),
		reading_from, p->from_file));
	set_char_quoting (NULL, ':', oldc);
	return 1;
      }
  p = xmalloc (sizeof *p);
  p->next = file_id_list;
  p->ino = st.st_ino;
  p->dev = st.st_dev;
  p->from_file = reading_from;
  file_id_list = p;
  return 0;
}

/* Chop trailing slashes.  */
static void
chopslash (char *str)
{
  char *p = str + strlen (str) - 1;
  while (p > str && ISSLASH (*p))
    *p-- = '\0';
}

enum read_file_list_state  /* Result of reading file name from the list file */
  {
    file_list_success,     /* OK, name read successfully */
    file_list_end,         /* End of list file */
    file_list_zero,        /* Zero separator encountered where it should not */
    file_list_skip         /* Empty (zero-length) entry encountered, skip it */
  };

/* Read from FP a sequence of characters up to TERM and put them
   into STK.
 */
static enum read_file_list_state
read_name_from_file (struct name_elt *ent)
{
  int c;
  size_t counter = 0;
  FILE *fp = ent->v.file.fp;
  int term = ent->v.file.term;

  ++ent->v.file.line;
  for (c = getc (fp); c != EOF && c != term; c = getc (fp))
    {
      if (counter == name_buffer_length)
	name_buffer = x2realloc (name_buffer, &name_buffer_length);
      name_buffer[counter++] = c;
      if (c == 0)
	{
	  /* We have read a zero separator. The file possibly is
	     zero-separated */
	  return file_list_zero;
	}
    }

  if (counter == 0 && c != EOF)
    return file_list_skip;

  if (counter == name_buffer_length)
    name_buffer = x2realloc (name_buffer, &name_buffer_length);
  name_buffer[counter] = 0;
  chopslash (name_buffer);
  return (counter == 0 && c == EOF) ? file_list_end : file_list_success;
}

static int
handle_option (const char *str, struct name_elt const *ent)
{
  struct wordsplit ws;
  int i;
  struct option_locus loc;

  while (*str && isspace (*str))
    ++str;
  if (*str != '-')
    return 1;

  ws.ws_offs = 1;
  if (wordsplit (str, &ws, WRDSF_DEFFLAGS|WRDSF_DOOFFS))
    FATAL_ERROR ((0, 0, _("cannot split string '%s': %s"),
		  str, wordsplit_strerror (&ws)));
  ws.ws_wordv[0] = (char *) program_name;
  loc.source = OPTS_FILE;
  loc.name = ent->v.file.name;
  loc.line = ent->v.file.line;
  more_options (ws.ws_wordc+ws.ws_offs, ws.ws_wordv, &loc);
  for (i = 0; i < ws.ws_wordc+ws.ws_offs; i++)
    ws.ws_wordv[i] = NULL;

  wordsplit_free (&ws);
  return 0;
}

static int
read_next_name (struct name_elt *ent, struct name_elt *ret)
{
  if (!ent->v.file.fp)
    {
      if (!strcmp (ent->v.file.name, "-"))
	{
	  request_stdin ("-T");
	  ent->v.file.fp = stdin;
	}
      else
	{
	  if (add_file_id (ent->v.file.name))
	    {
	      name_list_advance ();
	      return 1;
	    }
	  if ((ent->v.file.fp = fopen (ent->v.file.name, "r")) == NULL)
	    open_fatal (ent->v.file.name);
	}
      ent->v.file.term = filename_terminator;
      ent->v.file.verbatim = verbatim_files_from_option;
    }

  while (1)
    {
      switch (read_name_from_file (ent))
	{
	case file_list_skip:
	  continue;

	case file_list_zero:
	  WARNOPT (WARN_FILENAME_WITH_NULS,
		   (0, 0, N_("%s: file name read contains nul character"),
		    quotearg_colon (ent->v.file.name)));
	  ent->v.file.term = 0;
	  FALLTHROUGH;
	case file_list_success:
	  if (!ent->v.file.verbatim)
	    {
	      if (unquote_option)
		unquote_string (name_buffer);
	      if (handle_option (name_buffer, ent) == 0)
		{
		  name_list_adjust ();
		  return 1;
		}
	    }
	  ret->type = NELT_NAME;
	  ret->v.name = name_buffer;
	  return 0;

	case file_list_end:
	  if (strcmp (ent->v.file.name, "-"))
	    fclose (ent->v.file.fp);
	  ent->v.file.fp = NULL;
	  name_list_advance ();
	  return 1;
	}
    }
}

static void
copy_name (struct name_elt *ep)
{
  const char *source;
  size_t source_len;

  source = ep->v.name;
  source_len = strlen (source);
  if (name_buffer_length < source_len)
    {
      do
	{
	  name_buffer_length *= 2;
	  if (! name_buffer_length)
	    xalloc_die ();
	}
      while (name_buffer_length < source_len);

      free (name_buffer);
      name_buffer = xmalloc(name_buffer_length + 2);
    }
  strcpy (name_buffer, source);
  chopslash (name_buffer);
}


/* Get the next NELT_NAME element from name_array.  Result is in
   static storage and can't be relied upon across two calls.

   If CHANGE_DIRS is true, treat any entries of type NELT_CHDIR as
   the request to change to the given directory.

*/
static struct name_elt *
name_next_elt (int change_dirs)
{
  static struct name_elt entry;
  struct name_elt *ep;

  while ((ep = name_head) != NULL)
    {
      switch (ep->type)
	{
	case NELT_NOOP:
	  name_list_advance ();
	  break;

	case NELT_FILE:
	  if (read_next_name (ep, &entry) == 0)
	    return &entry;
	  continue;

	case NELT_CHDIR:
	  if (change_dirs)
	    {
	      chdir_do (chdir_arg (xstrdup (ep->v.name)));
	      name_list_advance ();
	      break;
	    }
	  FALLTHROUGH;
	case NELT_NAME:
	  copy_name (ep);
	  if (unquote_option)
	    unquote_string (name_buffer);
	  entry.type = ep->type;
	  entry.v.name = name_buffer;
	  name_list_advance ();
	  return &entry;

	case NELT_OPTION:
	  handle_file_selection_option (ep->v.opt.option, ep->v.opt.arg);
	  name_list_advance ();
	  continue;
	}
    }

  unconsumed_option_report ();

  return NULL;
}

const char *
name_next (int change_dirs)
{
  struct name_elt *nelt = name_next_elt (change_dirs);
  return nelt ? nelt->v.name : NULL;
}

/* Gather names in a list for scanning.  Could hash them later if we
   really care.

   If the names are already sorted to match the archive, we just read
   them one by one.  name_gather reads the first one, and it is called
   by name_match as appropriate to read the next ones.  At EOF, the
   last name read is just left in the buffer.  This option lets users
   of small machines extract an arbitrary number of files by doing
   "tar t" and editing down the list of files.  */

void
name_gather (void)
{
  /* Buffer able to hold a single name.  */
  static struct name *buffer = NULL;

  struct name_elt *ep;

  if (same_order_option)
    {
      static int change_dir;

      while ((ep = name_next_elt (0)) && ep->type == NELT_CHDIR)
	change_dir = chdir_arg (xstrdup (ep->v.name));

      if (ep)
	{
	  free_name (buffer);
	  buffer = make_name (ep->v.name);
	  buffer->change_dir = change_dir;
	  buffer->next = 0;
	  buffer->found_count = 0;
	  buffer->matching_flags = INCLUDE_OPTIONS;
	  buffer->directory = NULL;
	  buffer->parent = NULL;
	  buffer->cmdline = true;

	  namelist = nametail = buffer;
	}
      else if (change_dir)
	addname (0, change_dir, false, NULL);
    }
  else
    {
      /* Non sorted names -- read them all in.  */
      int change_dir = 0;

      for (;;)
	{
	  int change_dir0 = change_dir;
	  while ((ep = name_next_elt (0)) && ep->type == NELT_CHDIR)
	    change_dir = chdir_arg (xstrdup (ep->v.name));

	  if (ep)
	    addname (ep->v.name, change_dir, true, NULL);
	  else
	    {
	      if (change_dir != change_dir0)
		addname (NULL, change_dir, false, NULL);
	      break;
	    }
	}
    }
}

/*  Add a name to the namelist.  */
struct name *
addname (char const *string, int change_dir, bool cmdline, struct name *parent)
{
  struct name *name = make_name (string);

  name->prev = nametail;
  name->next = NULL;
  name->found_count = 0;
  name->matching_flags = INCLUDE_OPTIONS;
  name->change_dir = change_dir;
  name->directory = NULL;
  name->parent = parent;
  name->cmdline = cmdline;

  if (nametail)
    nametail->next = name;
  else
    namelist = name;
  nametail = name;
  return name;
}

/* Find a match for FILE_NAME (whose string length is LENGTH) in the name
   list.  */
static struct name *
namelist_match (char const *file_name, size_t length)
{
  struct name *p;

  for (p = namelist; p; p = p->next)
    {
      if (p->name[0]
	  && exclude_fnmatch (p->name, file_name, p->matching_flags))
	return p;
    }

  return NULL;
}

void
remname (struct name *name)
{
  struct name *p;

  if ((p = name->prev) != NULL)
    p->next = name->next;
  else
    namelist = name->next;

  if ((p = name->next) != NULL)
    p->prev = name->prev;
  else
    nametail = name->prev;
}

/* Return true if and only if name FILE_NAME (from an archive) matches any
   name from the namelist.  */
bool
name_match (const char *file_name)
{
  size_t length = strlen (file_name);

  while (1)
    {
      struct name *cursor = namelist;

      if (!cursor)
	return true;

      if (cursor->name[0] == 0)
	{
	  chdir_do (cursor->change_dir);
	  namelist = NULL;
	  nametail = NULL;
	  return true;
	}

      cursor = namelist_match (file_name, length);
      if (cursor)
	{
	  if (!(ISSLASH (file_name[cursor->length]) && recursion_option)
	      || cursor->found_count == 0)
	    cursor->found_count++; /* remember it matched */
	  if (starting_file_option)
	    {
	      free (namelist);
	      namelist = NULL;
	      nametail = NULL;
	    }
	  chdir_do (cursor->change_dir);

	  /* We got a match.  */
	  return ISFOUND (cursor);
	}

      /* Filename from archive not found in namelist.  If we have the whole
	 namelist here, just return 0.  Otherwise, read the next name in and
	 compare it.  If this was the last name, namelist->found_count will
	 remain on.  If not, we loop to compare the newly read name.  */

      if (same_order_option && namelist->found_count)
	{
	  name_gather ();	/* read one more */
	  if (namelist->found_count)
	    return false;
	}
      else
	return false;
    }
}

/* Returns true if all names from the namelist were processed.
   P is the stat_info of the most recently processed entry.
   The decision is postponed until the next entry is read if:

   1) P ended with a slash (i.e. it was a directory)
   2) P matches any entry from the namelist *and* represents a subdirectory
   or a file lying under this entry (in the terms of directory structure).

   This is necessary to handle contents of directories. */
bool
all_names_found (struct tar_stat_info *p)
{
  struct name const *cursor;
  size_t len;

  if (!p->file_name || occurrence_option == 0 || p->had_trailing_slash)
    return false;
  len = strlen (p->file_name);
  for (cursor = namelist; cursor; cursor = cursor->next)
    {
      if ((cursor->name[0] && !WASFOUND (cursor))
	  || (len >= cursor->length && ISSLASH (p->file_name[cursor->length])))
	return false;
    }
  return true;
}

static int
regex_usage_warning (const char *name)
{
  static int warned_once = 0;

  /* Warn about implicit use of the wildcards in command line arguments.
     (Default for tar prior to 1.15.91, but changed afterwards) */
  if (wildcards == default_wildcards
      && fnmatch_pattern_has_wildcards (name, 0))
    {
      warned_once = 1;
      WARN ((0, 0,
	     _("Pattern matching characters used in file names")));
      WARN ((0, 0,
	     _("Use --wildcards to enable pattern matching,"
	       " or --no-wildcards to suppress this warning")));
    }
  return warned_once;
}

/* Print the names of things in the namelist that were not matched.  */
void
names_notfound (void)
{
  struct name const *cursor;

  for (cursor = namelist; cursor; cursor = cursor->next)
    if (!WASFOUND (cursor) && cursor->name[0])
      {
	regex_usage_warning (cursor->name);
	ERROR ((0, 0,
		(cursor->found_count == 0) ?
		     _("%s: Not found in archive") :
		     _("%s: Required occurrence not found in archive"),
		quotearg_colon (cursor->name)));
      }

  /* Don't bother freeing the name list; we're about to exit.  */
  namelist = NULL;
  nametail = NULL;

  if (same_order_option)
    {
      const char *name;

      while ((name = name_next (1)) != NULL)
	{
	  regex_usage_warning (name);
	  ERROR ((0, 0, _("%s: Not found in archive"),
		  quotearg_colon (name)));
	}
    }
}

void
label_notfound (void)
{
  struct name const *cursor;

  if (!namelist)
    return;

  for (cursor = namelist; cursor; cursor = cursor->next)
    if (WASFOUND (cursor))
      return;

  if (verbose_option)
    error (0, 0, _("Archive label mismatch"));
  set_exit_status (TAREXIT_DIFFERS);

  for (cursor = namelist; cursor; cursor = cursor->next)
    {
      if (regex_usage_warning (cursor->name))
	break;
    }

  /* Don't bother freeing the name list; we're about to exit.  */
  namelist = NULL;
  nametail = NULL;

  if (same_order_option)
    {
      const char *name;

      while ((name = name_next (1)) != NULL
	     && regex_usage_warning (name) == 0)
	;
    }
}

/* Sorting name lists.  */

/* Sort *singly* linked LIST of names, of given LENGTH, using COMPARE
   to order names.  Return the sorted list.  Note that after calling
   this function, the 'prev' links in list elements are messed up.

   Apart from the type 'struct name' and the definition of SUCCESSOR,
   this is a generic list-sorting function, but it's too painful to
   make it both generic and portable
   in C.  */

static struct name *
merge_sort_sll (struct name *list, int length,
		int (*compare) (struct name const*, struct name const*))
{
  struct name *first_list;
  struct name *second_list;
  int first_length;
  int second_length;
  struct name *result;
  struct name **merge_point;
  struct name *cursor;
  int counter;

# define SUCCESSOR(name) ((name)->next)

  if (length == 1)
    return list;

  if (length == 2)
    {
      if ((*compare) (list, SUCCESSOR (list)) > 0)
	{
	  result = SUCCESSOR (list);
	  SUCCESSOR (result) = list;
	  SUCCESSOR (list) = 0;
	  return result;
	}
      return list;
    }

  first_list = list;
  first_length = (length + 1) / 2;
  second_length = length / 2;
  for (cursor = list, counter = first_length - 1;
       counter;
       cursor = SUCCESSOR (cursor), counter--)
    continue;
  second_list = SUCCESSOR (cursor);
  SUCCESSOR (cursor) = 0;

  first_list = merge_sort_sll (first_list, first_length, compare);
  second_list = merge_sort_sll (second_list, second_length, compare);

  merge_point = &result;
  while (first_list && second_list)
    if ((*compare) (first_list, second_list) < 0)
      {
	cursor = SUCCESSOR (first_list);
	*merge_point = first_list;
	merge_point = &SUCCESSOR (first_list);
	first_list = cursor;
      }
    else
      {
	cursor = SUCCESSOR (second_list);
	*merge_point = second_list;
	merge_point = &SUCCESSOR (second_list);
	second_list = cursor;
      }
  if (first_list)
    *merge_point = first_list;
  else
    *merge_point = second_list;

  return result;

#undef SUCCESSOR
}

/* Sort doubly linked LIST of names, of given LENGTH, using COMPARE
   to order names.  Return the sorted list.  */
static struct name *
merge_sort (struct name *list, int length,
	    int (*compare) (struct name const*, struct name const*))
{
  struct name *head, *p, *prev;
  head = merge_sort_sll (list, length, compare);
  /* Fixup prev pointers */
  for (prev = NULL, p = head; p; prev = p, p = p->next)
    p->prev = prev;
  return head;
}

/* A comparison function for sorting names.  Put found names last;
   break ties by string comparison.  */

static int
compare_names_found (struct name const *n1, struct name const *n2)
{
  int found_diff = WASFOUND (n2) - WASFOUND (n1);
  return found_diff ? found_diff : strcmp (n1->name, n2->name);
}

/* Simple comparison by names. */
static int
compare_names (struct name const *n1, struct name const *n2)
{
  return strcmp (n1->name, n2->name);
}


/* Add all the dirs under ST to the namelist NAME, descending the
   directory hierarchy recursively.  */

static void
add_hierarchy_to_namelist (struct tar_stat_info *st, struct name *name)
{
  const char *buffer;

  name->directory = scan_directory (st);
  buffer = directory_contents (name->directory);
  if (buffer)
    {
      struct name *child_head = NULL, *child_tail = NULL;
      size_t name_length = name->length;
      size_t allocated_length = (name_length >= NAME_FIELD_SIZE
				 ? name_length + NAME_FIELD_SIZE
				 : NAME_FIELD_SIZE);
      char *namebuf = xmalloc (allocated_length + 1);
				/* FIXME: + 2 above?  */
      const char *string;
      size_t string_length;
      int change_dir = name->change_dir;

      strcpy (namebuf, name->name);
      if (! ISSLASH (namebuf[name_length - 1]))
	{
	  namebuf[name_length++] = '/';
	  namebuf[name_length] = '\0';
	}

      for (string = buffer; *string; string += string_length + 1)
	{
	  string_length = strlen (string);
	  if (*string == 'D')
	    {
	      struct name *np;
	      struct tar_stat_info subdir;
	      int subfd;

	      if (allocated_length <= name_length + string_length)
		{
		  do
		    {
		      allocated_length *= 2;
		      if (! allocated_length)
			xalloc_die ();
		    }
		  while (allocated_length <= name_length + string_length);

		  namebuf = xrealloc (namebuf, allocated_length + 1);
		}
	      strcpy (namebuf + name_length, string + 1);
	      np = addname (namebuf, change_dir, false, name);
	      if (!child_head)
		child_head = np;
	      else
		child_tail->sibling = np;
	      child_tail = np;

	      tar_stat_init (&subdir);
	      subdir.parent = st;
	      if (st->fd < 0)
		{
		  subfd = -1;
		  errno = - st->fd;
		}
	      else
		subfd = subfile_open (st, string + 1,
				      open_read_flags | O_DIRECTORY);
	      if (subfd < 0)
		open_diag (namebuf);
	      else
		{
		  subdir.fd = subfd;
		  if (fstat (subfd, &subdir.stat) != 0)
		    stat_diag (namebuf);
		  else if (! (O_DIRECTORY || S_ISDIR (subdir.stat.st_mode)))
		    {
		      errno = ENOTDIR;
		      open_diag (namebuf);
		    }
		  else
		    {
		      subdir.orig_file_name = xstrdup (namebuf);
		      add_hierarchy_to_namelist (&subdir, np);
		      restore_parent_fd (&subdir);
		    }
		}

	      tar_stat_destroy (&subdir);
	    }
	}

      free (namebuf);
      name->child = child_head;
    }
}

/* Auxiliary functions for hashed table of struct name's. */

static size_t
name_hash (void const *entry, size_t n_buckets)
{
  struct name const *name = entry;
  return hash_string (name->caname, n_buckets);
}

/* Compare two directories for equality of their names. */
static bool
name_compare (void const *entry1, void const *entry2)
{
  struct name const *name1 = entry1;
  struct name const *name2 = entry2;
  return strcmp (name1->caname, name2->caname) == 0;
}


/* Rebase 'name' member of CHILD and all its siblings to
   the new PARENT. */
static void
rebase_child_list (struct name *child, struct name *parent)
{
  size_t old_prefix_len = child->parent->length;
  size_t new_prefix_len = parent->length;
  char *new_prefix = parent->name;

  for (; child; child = child->sibling)
    {
      size_t size = child->length - old_prefix_len + new_prefix_len;
      char *newp = xmalloc (size + 1);
      strcpy (newp, new_prefix);
      strcat (newp, child->name + old_prefix_len);
      free (child->name);
      child->name = newp;
      child->length = size;

      rebase_directory (child->directory,
			child->parent->name, old_prefix_len,
			new_prefix, new_prefix_len);
    }
}

/* Collect all the names from argv[] (or whatever), expand them into a
   directory tree, and sort them.  This gets only subdirectories, not
   all files.  */

void
collect_and_sort_names (void)
{
  struct name *name;
  struct name *next_name, *prev_name = NULL;
  int num_names;
  Hash_table *nametab;

  name_gather ();

  if (!namelist)
    addname (".", 0, false, NULL);

  if (listed_incremental_option)
    {
      switch (chdir_count ())
	{
	case 0:
	  break;

	case 1:
	  if (namelist->change_dir == 0)
	    USAGE_ERROR ((0, 0,
			  _("Using -C option inside file list is not "
			    "allowed with --listed-incremental")));
	  break;

	default:
	  USAGE_ERROR ((0, 0,
			_("Only one -C option is allowed with "
			  "--listed-incremental")));
	}

      read_directory_file ();
    }

  num_names = 0;
  for (name = namelist; name; name = name->next, num_names++)
    {
      struct tar_stat_info st;

      if (name->found_count || name->directory)
	continue;
      if (name->matching_flags & EXCLUDE_WILDCARDS)
	/* NOTE: EXCLUDE_ANCHORED is not relevant here */
	/* FIXME: just skip regexps for now */
	continue;
      chdir_do (name->change_dir);

      if (name->name[0] == 0)
	continue;

      tar_stat_init (&st);

      if (deref_stat (name->name, &st.stat) != 0)
	{
	  stat_diag (name->name);
	  continue;
	}
      if (S_ISDIR (st.stat.st_mode))
	{
	  int dir_fd = openat (chdir_fd, name->name,
			       open_read_flags | O_DIRECTORY);
	  if (dir_fd < 0)
	    open_diag (name->name);
	  else
	    {
	      st.fd = dir_fd;
	      if (fstat (dir_fd, &st.stat) != 0)
		stat_diag (name->name);
	      else if (O_DIRECTORY || S_ISDIR (st.stat.st_mode))
		{
		  st.orig_file_name = xstrdup (name->name);
		  name->found_count++;
		  add_hierarchy_to_namelist (&st, name);
		}
	      else
		{
		  errno = ENOTDIR;
		  open_diag (name->name);
		}
	    }
	}

      tar_stat_destroy (&st);
    }

  namelist = merge_sort (namelist, num_names, compare_names);

  num_names = 0;
  nametab = hash_initialize (0, 0, name_hash, name_compare, NULL);
  for (name = namelist; name; name = next_name)
    {
      next_name = name->next;
      name->caname = normalize_filename (name->change_dir, name->name);
      if (prev_name)
	{
	  struct name *p = hash_lookup (nametab, name);
	  if (p)
	    {
	      /* Keep the one listed in the command line */
	      if (!name->parent)
		{
		  if (p->child)
		    rebase_child_list (p->child, name);
		  hash_delete (nametab, name);
		  /* FIXME: remove_directory (p->caname); ? */
		  remname (p);
		  free_name (p);
		  num_names--;
		}
	      else
		{
		  if (name->child)
		    rebase_child_list (name->child, p);
		  /* FIXME: remove_directory (name->caname); ? */
		  remname (name);
		  free_name (name);
		  continue;
		}
	    }
	}
      name->found_count = 0;
      if (!hash_insert (nametab, name))
	xalloc_die ();
      prev_name = name;
      num_names++;
    }
  nametail = prev_name;
  hash_free (nametab);

  namelist = merge_sort (namelist, num_names, compare_names_found);

  if (listed_incremental_option)
    {
      for (name = namelist; name && name->name[0] == 0; name++)
	;
      if (name)
	append_incremental_renames (name->directory);
    }
}

/* This is like name_match, except that
    1. It returns a pointer to the name it matched, and doesn't set FOUND
    in structure. The caller will have to do that if it wants to.
    2. If the namelist is empty, it returns null, unlike name_match, which
    returns TRUE. */
struct name *
name_scan (const char *file_name)
{
  size_t length = strlen (file_name);

  while (1)
    {
      struct name *cursor = namelist_match (file_name, length);
      if (cursor)
	return cursor;

      /* Filename from archive not found in namelist.  If we have the whole
	 namelist here, just return 0.  Otherwise, read the next name in and
	 compare it.  If this was the last name, namelist->found_count will
	 remain on.  If not, we loop to compare the newly read name.  */

      if (same_order_option && namelist && namelist->found_count)
	{
	  name_gather ();	/* read one more */
	  if (namelist->found_count)
	    return 0;
	}
      else
	return 0;
    }
}

/* This returns a name from the namelist which doesn't have ->found
   set.  It sets ->found before returning, so successive calls will
   find and return all the non-found names in the namelist.  */
struct name *gnu_list_name;

struct name const *
name_from_list (void)
{
  if (!gnu_list_name)
    gnu_list_name = namelist;
  while (gnu_list_name
	 && (gnu_list_name->found_count || gnu_list_name->name[0] == 0))
    gnu_list_name = gnu_list_name->next;
  if (gnu_list_name)
    {
      gnu_list_name->found_count++;
      chdir_do (gnu_list_name->change_dir);
      return gnu_list_name;
    }
  return NULL;
}

void
blank_name_list (void)
{
  struct name *name;

  gnu_list_name = 0;
  for (name = namelist; name; name = name->next)
    name->found_count = 0;
}

/* Yield a newly allocated file name consisting of DIR_NAME concatenated to
   NAME, with an intervening slash if DIR_NAME does not already end in one. */
char *
make_file_name (const char *directory_name, const char *name)
{
  size_t dirlen = strlen (directory_name);
  size_t namelen = strlen (name) + 1;
  int slash = dirlen && ! ISSLASH (directory_name[dirlen - 1]);
  char *buffer = xmalloc (dirlen + slash + namelen);
  memcpy (buffer, directory_name, dirlen);
  buffer[dirlen] = '/';
  memcpy (buffer + dirlen + slash, name, namelen);
  return buffer;
}



/* Return the size of the prefix of FILE_NAME that is removed after
   stripping NUM leading file name components.  NUM must be
   positive.  */

size_t
stripped_prefix_len (char const *file_name, size_t num)
{
  char const *p = file_name + FILE_SYSTEM_PREFIX_LEN (file_name);
  while (ISSLASH (*p))
    p++;
  while (*p)
    {
      bool slash = ISSLASH (*p);
      p++;
      if (slash)
	{
	  if (--num == 0)
	    return p - file_name;
	  while (ISSLASH (*p))
	    p++;
	}
    }
  return -1;
}

/* Return nonzero if NAME contains ".." as a file name component.  */
bool
contains_dot_dot (char const *name)
{
  char const *p = name + FILE_SYSTEM_PREFIX_LEN (name);

  for (;; p++)
    {
      if (p[0] == '.' && p[1] == '.' && (ISSLASH (p[2]) || !p[2]))
	return 1;

      while (! ISSLASH (*p))
	{
	  if (! *p++)
	    return 0;
	}
    }
}
