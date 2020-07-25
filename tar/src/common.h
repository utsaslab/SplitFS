/* Common declarations for the tar program.

   Copyright 1988, 1992-1994, 1996-1997, 1999-2010, 2012-2017 Free
   Software Foundation, Inc.

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

/* Declare the GNU tar archive format.  */
#include "tar.h"

/* The checksum field is filled with this while the checksum is computed.  */
#define CHKBLANKS	"        "	/* 8 blanks, no null */

/* Some constants from POSIX are given names.  */
#define NAME_FIELD_SIZE   100
#define PREFIX_FIELD_SIZE 155
#define UNAME_FIELD_SIZE   32
#define GNAME_FIELD_SIZE   32



/* Some various global definitions.  */

/* Name of file to use for interacting with user.  */

/* GLOBAL is defined to empty in tar.c only, and left alone in other *.c
   modules.  Here, we merely set it to "extern" if it is not already set.
   GNU tar does depend on the system loader to preset all GLOBAL variables to
   neutral (or zero) values, explicit initialization is usually not done.  */
#ifndef GLOBAL
# define GLOBAL extern
#endif

#if 7 <= __GNUC__
# define FALLTHROUGH __attribute__ ((__fallthrough__))
#else
# define FALLTHROUGH ((void) 0)
#endif

#define TAREXIT_SUCCESS PAXEXIT_SUCCESS
#define TAREXIT_DIFFERS PAXEXIT_DIFFERS
#define TAREXIT_FAILURE PAXEXIT_FAILURE


#include "arith.h"
#include <backupfile.h>
#include <exclude.h>
#include <full-write.h>
#include <modechange.h>
#include <quote.h>
#include <safe-read.h>
#include <stat-time.h>
#include <timespec.h>
#define obstack_chunk_alloc xmalloc
#define obstack_chunk_free free
#include <obstack.h>
#include <progname.h>
#include <xvasprintf.h>

#include <paxlib.h>

/* Log base 2 of common values.  */
#define LG_8 3
#define LG_64 6
#define LG_256 8

_GL_INLINE_HEADER_BEGIN
#ifndef COMMON_INLINE
# define COMMON_INLINE _GL_INLINE
#endif

/* Information gleaned from the command line.  */

/* Main command option.  */

enum subcommand
{
  UNKNOWN_SUBCOMMAND,		/* none of the following */
  APPEND_SUBCOMMAND,		/* -r */
  CAT_SUBCOMMAND,		/* -A */
  CREATE_SUBCOMMAND,		/* -c */
  DELETE_SUBCOMMAND,		/* -D */
  DIFF_SUBCOMMAND,		/* -d */
  EXTRACT_SUBCOMMAND,		/* -x */
  LIST_SUBCOMMAND,		/* -t */
  UPDATE_SUBCOMMAND,		/* -u */
  TEST_LABEL_SUBCOMMAND,        /* --test-label */
};

GLOBAL enum subcommand subcommand_option;

/* Selected format for output archive.  */
GLOBAL enum archive_format archive_format;

/* Size of each record, once in blocks, once in bytes.  Those two variables
   are always related, the second being BLOCKSIZE times the first.  They do
   not have _option in their name, even if their values is derived from
   option decoding, as these are especially important in tar.  */
GLOBAL int blocking_factor;
GLOBAL size_t record_size;

GLOBAL bool absolute_names_option;

/* Display file times in UTC */
GLOBAL bool utc_option;
/* Output file timestamps to the full resolution */
GLOBAL bool full_time_option;

/* This variable tells how to interpret newer_mtime_option, below.  If zero,
   files get archived if their mtime is not less than newer_mtime_option.
   If nonzero, files get archived if *either* their ctime or mtime is not less
   than newer_mtime_option.  */
GLOBAL int after_date_option;

enum atime_preserve
{
  no_atime_preserve,
  replace_atime_preserve,
  system_atime_preserve
};
GLOBAL enum atime_preserve atime_preserve_option;

GLOBAL bool backup_option;

/* Type of backups being made.  */
GLOBAL enum backup_type backup_type;

GLOBAL bool block_number_option;

GLOBAL unsigned checkpoint_option;
#define DEFAULT_CHECKPOINT 10

/* Specified name of compression program, or "gzip" as implied by -z.  */
GLOBAL const char *use_compress_program_option;

GLOBAL bool dereference_option;
GLOBAL bool hard_dereference_option;

/* Patterns that match file names to be excluded.  */
GLOBAL struct exclude *excluded;

enum exclusion_tag_type
  {
    exclusion_tag_none,
     /* Exclude the directory contents, but preserve the directory
	itself and the exclusion tag file */
    exclusion_tag_contents,
    /* Exclude everything below the directory, preserving the directory
       itself */
    exclusion_tag_under,
    /* Exclude entire directory  */
    exclusion_tag_all,
  };

/* Specified value to be put into tar file in place of stat () results, or
   just null and -1 if such an override should not take place.  */
GLOBAL char const *group_name_option;
GLOBAL gid_t group_option;

GLOBAL bool ignore_failed_read_option;

GLOBAL bool ignore_zeros_option;

GLOBAL bool incremental_option;

/* Specified name of script to run at end of each tape change.  */
GLOBAL const char *info_script_option;

GLOBAL bool interactive_option;

/* If nonzero, extract only Nth occurrence of each named file */
GLOBAL uintmax_t occurrence_option;

enum old_files
{
  DEFAULT_OLD_FILES,          /* default */
  NO_OVERWRITE_DIR_OLD_FILES, /* --no-overwrite-dir */
  OVERWRITE_OLD_FILES,        /* --overwrite */
  UNLINK_FIRST_OLD_FILES,     /* --unlink-first */
  KEEP_OLD_FILES,             /* --keep-old-files */
  SKIP_OLD_FILES,             /* --skip-old-files */
  KEEP_NEWER_FILES	      /* --keep-newer-files */
};
GLOBAL enum old_files old_files_option;

GLOBAL bool keep_directory_symlink_option;

/* Specified file name for incremental list.  */
GLOBAL const char *listed_incremental_option;
/* Incremental dump level */
GLOBAL int incremental_level;
/* Check device numbers when doing incremental dumps. */
GLOBAL bool check_device_option;

/* Specified mode change string.  */
GLOBAL struct mode_change *mode_option;

/* Initial umask, if needed for mode change string.  */
GLOBAL mode_t initial_umask;

GLOBAL bool multi_volume_option;

/* Specified threshold date and time.  Files having an older time stamp
   do not get archived (also see after_date_option above).  */
GLOBAL struct timespec newer_mtime_option;

enum set_mtime_option_mode
{
  USE_FILE_MTIME,
  FORCE_MTIME,
  CLAMP_MTIME,
};

/* Override actual mtime if set to FORCE_MTIME or CLAMP_MTIME */
GLOBAL enum set_mtime_option_mode set_mtime_option;
/* Value to use when forcing or clamping the mtime header field. */
GLOBAL struct timespec mtime_option;

/* Return true if mtime_option or newer_mtime_option is initialized.  */
#define TIME_OPTION_INITIALIZED(opt) (0 <= (opt).tv_nsec)

/* Return true if the struct stat ST's M time is less than
   newer_mtime_option.  */
#define OLDER_STAT_TIME(st, m) \
  (timespec_cmp (get_stat_##m##time (&(st)), newer_mtime_option) < 0)

/* Likewise, for struct tar_stat_info ST.  */
#define OLDER_TAR_STAT_TIME(st, m) \
  (timespec_cmp ((st).m##time, newer_mtime_option) < 0)

/* Zero if there is no recursion, otherwise FNM_LEADING_DIR.  */
GLOBAL int recursion_option;

GLOBAL bool numeric_owner_option;

GLOBAL bool one_file_system_option;

/* Create a top-level directory for extracting based on the archive name.  */
GLOBAL bool one_top_level_option;
GLOBAL char *one_top_level_dir;

/* Specified value to be put into tar file in place of stat () results, or
   just null and -1 if such an override should not take place.  */
GLOBAL char const *owner_name_option;
GLOBAL uid_t owner_option;

GLOBAL bool recursive_unlink_option;

GLOBAL bool read_full_records_option;

GLOBAL bool remove_files_option;

/* Specified remote shell command.  */
GLOBAL const char *rsh_command_option;

GLOBAL bool same_order_option;

/* If positive, preserve ownership when extracting.  */
GLOBAL int same_owner_option;

/* If positive, preserve permissions when extracting.  */
GLOBAL int same_permissions_option;

/* If positive, save the SELinux context.  */
GLOBAL int selinux_context_option;

/* If positive, save the ACLs.  */
GLOBAL int acls_option;

/* If positive, save the user and root xattrs.  */
GLOBAL int xattrs_option;

/* When set, strip the given number of file name components from the file name
   before extracting */
GLOBAL size_t strip_name_components;

GLOBAL bool show_omitted_dirs_option;

GLOBAL bool sparse_option;
GLOBAL unsigned tar_sparse_major;
GLOBAL unsigned tar_sparse_minor;

enum hole_detection_method
  {
    HOLE_DETECTION_DEFAULT,
    HOLE_DETECTION_RAW,
    HOLE_DETECTION_SEEK
  };

GLOBAL enum hole_detection_method hole_detection;

GLOBAL bool starting_file_option;

/* Specified maximum byte length of each tape volume (multiple of 1024).  */
GLOBAL tarlong tape_length_option;

GLOBAL bool to_stdout_option;

GLOBAL bool totals_option;

GLOBAL bool touch_option;

GLOBAL char *to_command_option;
GLOBAL bool ignore_command_error_option;

/* Restrict some potentially harmful tar options */
GLOBAL bool restrict_option;

/* Return true if the extracted files are not being written to disk */
#define EXTRACT_OVER_PIPE (to_stdout_option || to_command_option)

/* Count how many times the option has been set, multiple setting yields
   more verbose behavior.  Value 0 means no verbosity, 1 means file name
   only, 2 means file name and all attributes.  More than 2 is just like 2.  */
GLOBAL int verbose_option;

GLOBAL bool verify_option;

/* Specified name of file containing the volume number.  */
GLOBAL const char *volno_file_option;

/* Specified value or pattern.  */
GLOBAL const char *volume_label_option;

/* Other global variables.  */

/* File descriptor for archive file.  */
GLOBAL int archive;

/* Nonzero when outputting to /dev/null.  */
GLOBAL bool dev_null_output;

/* Timestamps: */
GLOBAL struct timespec start_time;        /* when we started execution */
GLOBAL struct timespec volume_start_time; /* when the current volume was
					     opened*/
GLOBAL struct timespec last_stat_time;    /* when the statistics was last
					     computed */

GLOBAL struct tar_stat_info current_stat_info;

/* List of tape drive names, number of such tape drives,
   and current cursor in list.  */
GLOBAL const char **archive_name_array;
GLOBAL size_t archive_names;
GLOBAL const char **archive_name_cursor;

/* Output index file name.  */
GLOBAL char const *index_file_name;

/* Opaque structure for keeping directory meta-data */
struct directory;

/* Structure for keeping track of filenames and lists thereof.  */
struct name
  {
    struct name *next;          /* Link to the next element */
    struct name *prev;          /* Link to the previous element */

    char *name;                 /* File name or globbing pattern */
    size_t length;		/* cached strlen (name) */
    int matching_flags;         /* wildcard flags if name is a pattern */
    bool cmdline;               /* true if this name was given in the
				   command line */

    int change_dir;		/* Number of the directory to change to.
				   Set with the -C option. */
    uintmax_t found_count;	/* number of times a matching file has
				   been found */

    /* The following members are used for incremental dumps only,
       if this struct name represents a directory;
       see incremen.c */
    struct directory *directory;/* directory meta-data and contents */
    struct name *parent;        /* pointer to the parent hierarchy */
    struct name *child;         /* pointer to the first child */
    struct name *sibling;       /* pointer to the next sibling */
    char *caname;               /* canonical name */
  };

/* Obnoxious test to see if dimwit is trying to dump the archive.  */
GLOBAL dev_t ar_dev;
GLOBAL ino_t ar_ino;

/* Flags for reading, searching, and fstatatting files.  */
GLOBAL int open_read_flags;
GLOBAL int open_searchdir_flags;
GLOBAL int fstatat_flags;

GLOBAL int seek_option;
GLOBAL bool seekable_archive;

GLOBAL dev_t root_device;

/* Unquote filenames */
GLOBAL bool unquote_option;

GLOBAL int savedir_sort_order;

/* Show file or archive names after transformation.
   In particular, when creating archive in verbose mode, list member names
   as stored in the archive */
GLOBAL bool show_transformed_names_option;

/* Delay setting modification times and permissions of extracted directories
   until the end of extraction. This variable helps correctly restore directory
   timestamps from archives with an unusual member order. It is automatically
   set for incremental archives. */
GLOBAL bool delay_directory_restore_option;

/* Declarations for each module.  */

/* FIXME: compare.c should not directly handle the following variable,
   instead, this should be done in buffer.c only.  */

enum access_mode
{
  ACCESS_READ,
  ACCESS_WRITE,
  ACCESS_UPDATE
};
extern enum access_mode access_mode;

/* Module buffer.c.  */

extern FILE *stdlis;
extern bool write_archive_to_stdout;
extern char *volume_label;
extern size_t volume_label_count;
extern char *continued_file_name;
extern uintmax_t continued_file_size;
extern uintmax_t continued_file_offset;
extern off_t records_written;

char *drop_volume_label_suffix (const char *label);

size_t available_space_after (union block *pointer);
off_t current_block_ordinal (void);
void close_archive (void);
void closeout_volume_number (void);
double compute_duration (void);
union block *find_next_block (void);
void flush_read (void);
void flush_write (void);
void flush_archive (void);
void init_volume_number (void);
void open_archive (enum access_mode mode);
void print_total_stats (void);
void reset_eof (void);
void set_next_block_after (union block *block);
void clear_read_error_count (void);
void xclose (int fd);
void archive_write_error (ssize_t status) __attribute__ ((noreturn));
void archive_read_error (void);
off_t seek_archive (off_t size);
void set_start_time (void);

#define TF_READ    0
#define TF_WRITE   1
#define TF_DELETED 2
int format_total_stats (FILE *fp, char const *const *formats, int eor, int eol);
void print_total_stats (void);

void mv_begin_write (const char *file_name, off_t totsize, off_t sizeleft);

void mv_begin_read (struct tar_stat_info *st);
void mv_end (void);
void mv_size_left (off_t size);

void buffer_write_global_xheader (void);

const char *first_decompress_program (int *pstate);
const char *next_decompress_program (int *pstate);

/* Module create.c.  */

enum dump_status
  {
    dump_status_ok,
    dump_status_short,
    dump_status_fail,
    dump_status_not_implemented
  };

void add_exclusion_tag (const char *name, enum exclusion_tag_type type,
			bool (*predicate) (int));
bool cachedir_file_p (int fd);
char *get_directory_entries (struct tar_stat_info *st);

void create_archive (void);
void pad_archive (off_t size_left);
void dump_file (struct tar_stat_info *parent, char const *name,
		char const *fullname);
union block *start_header (struct tar_stat_info *st);
void finish_header (struct tar_stat_info *st, union block *header,
		    off_t block_ordinal);
void simple_finish_header (union block *header);
union block * write_extended (bool global, struct tar_stat_info *st,
			      union block *old_header);
union block *start_private_header (const char *name, size_t size, time_t t);
void write_eot (void);
void check_links (void);
int subfile_open (struct tar_stat_info const *dir, char const *file, int flags);
void restore_parent_fd (struct tar_stat_info const *st);
void exclusion_tag_warning (const char *dirname, const char *tagname,
			    const char *message);
enum exclusion_tag_type check_exclusion_tags (struct tar_stat_info const *st,
					      const char **tag_file_name);

#define OFF_TO_CHARS(val, where) off_to_chars (val, where, sizeof (where))
#define TIME_TO_CHARS(val, where) time_to_chars (val, where, sizeof (where))

bool off_to_chars (off_t off, char *buf, size_t size);
bool time_to_chars (time_t t, char *buf, size_t size);

/* Module diffarch.c.  */

extern bool now_verifying;

void diff_archive (void);
void diff_init (void);
void verify_volume (void);

/* Module extract.c.  */

void extr_init (void);
void extract_archive (void);
void extract_finish (void);
bool rename_directory (char *src, char *dst);

void remove_delayed_set_stat (const char *fname);

/* Module delete.c.  */

void delete_archive_members (void);

/* Module incremen.c.  */

struct directory *scan_directory (struct tar_stat_info *st);
const char *directory_contents (struct directory *dir);
const char *safe_directory_contents (struct directory *dir);

void rebase_directory (struct directory *dir,
		       const char *samp, size_t slen,
		       const char *repl, size_t rlen);

void append_incremental_renames (struct directory *dir);
void show_snapshot_field_ranges (void);
void read_directory_file (void);
void write_directory_file (void);
void purge_directory (char const *directory_name);
void list_dumpdir (char *buffer, size_t size);
void update_parent_directory (struct tar_stat_info *st);

size_t dumpdir_size (const char *p);
bool is_dumpdir (struct tar_stat_info *stat_info);
void clear_directory_table (void);

/* Module list.c.  */

enum read_header
{
  HEADER_STILL_UNREAD,		/* for when read_header has not been called */
  HEADER_SUCCESS,		/* header successfully read and checksummed */
  HEADER_SUCCESS_EXTENDED,	/* likewise, but we got an extended header */
  HEADER_ZERO_BLOCK,		/* zero block where header expected */
  HEADER_END_OF_FILE,		/* true end of file while header expected */
  HEADER_FAILURE		/* ill-formed header, or bad checksum */
};

/* Operation mode for read_header: */

enum read_header_mode
{
  read_header_auto,             /* process extended headers automatically */
  read_header_x_raw,            /* return raw extended headers (return
				   HEADER_SUCCESS_EXTENDED) */
  read_header_x_global          /* when POSIX global extended header is read,
				   decode it and return
				   HEADER_SUCCESS_EXTENDED */
};
extern union block *current_header;
extern enum archive_format current_format;
extern size_t recent_long_name_blocks;
extern size_t recent_long_link_blocks;

void decode_header (union block *header, struct tar_stat_info *stat_info,
		    enum archive_format *format_pointer, int do_user_group);
void transform_stat_info (int typeflag, struct tar_stat_info *stat_info);
char const *tartime (struct timespec t, bool full_time);

#define OFF_FROM_HEADER(where) off_from_header (where, sizeof (where))
#define UINTMAX_FROM_HEADER(where) uintmax_from_header (where, sizeof (where))

off_t off_from_header (const char *buf, size_t size);
uintmax_t uintmax_from_header (const char *buf, size_t size);

void list_archive (void);
void test_archive_label (void);
void print_for_mkdir (char *dirname, int length, mode_t mode);
void print_header (struct tar_stat_info *st, union block *blk,
	           off_t block_ordinal);
void read_and (void (*do_something) (void));
enum read_header read_header (union block **return_block,
			      struct tar_stat_info *info,
			      enum read_header_mode m);
enum read_header tar_checksum (union block *header, bool silent);
void skip_file (off_t size);
void skip_member (void);

/* Module misc.c.  */

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) < (b) ? (b) : (a))

char const *quote_n_colon (int n, char const *arg);
void assign_string (char **dest, const char *src);
int unquote_string (char *str);
char *zap_slashes (char *name);
char *normalize_filename (int cdidx, const char *name);
void normalize_filename_x (char *name);
void replace_prefix (char **pname, const char *samp, size_t slen,
		     const char *repl, size_t rlen);
char *tar_savedir (const char *name, int must_exist);

typedef struct namebuf *namebuf_t;
namebuf_t namebuf_create (const char *dir);
void namebuf_free (namebuf_t buf);
char *namebuf_name (namebuf_t buf, const char *name);

const char *tar_dirname (void);

/* Represent N using a signed integer I such that (uintmax_t) I == N.
   With a good optimizing compiler, this is equivalent to (intmax_t) i
   and requires zero machine instructions.  */
#if ! (UINTMAX_MAX / 2 <= INTMAX_MAX)
# error "represent_uintmax returns intmax_t to represent uintmax_t"
#endif
COMMON_INLINE intmax_t
represent_uintmax (uintmax_t n)
{
  if (n <= INTMAX_MAX)
    return n;
  else
    {
      /* Avoid signed integer overflow on picky platforms.  */
      intmax_t nd = n - INTMAX_MIN;
      return nd + INTMAX_MIN;
    }
}

enum { SYSINT_BUFSIZE =
	 max (UINTMAX_STRSIZE_BOUND, INT_BUFSIZE_BOUND (intmax_t)) };
char *sysinttostr (uintmax_t, intmax_t, uintmax_t, char buf[SYSINT_BUFSIZE]);
intmax_t strtosysint (char const *, char **, intmax_t, uintmax_t);
void code_ns_fraction (int ns, char *p);
char const *code_timespec (struct timespec ts, char *sbuf);
enum { BILLION = 1000000000, LOG10_BILLION = 9 };
enum { TIMESPEC_STRSIZE_BOUND =
         UINTMAX_STRSIZE_BOUND + LOG10_BILLION + sizeof "-." - 1 };
struct timespec decode_timespec (char const *, char **, bool);

/* Return true if T does not represent an out-of-range or invalid value.  */
COMMON_INLINE bool
valid_timespec (struct timespec t)
{
  return 0 <= t.tv_nsec;
}

bool must_be_dot_or_slash (char const *);

enum remove_option
{
  ORDINARY_REMOVE_OPTION,
  RECURSIVE_REMOVE_OPTION,

  /* FIXME: The following value is never used. It seems to be intended
     as a placeholder for a hypothetical option that should instruct tar
     to recursively remove subdirectories in purge_directory(),
     as opposed to the functionality of --recursive-unlink
     (RECURSIVE_REMOVE_OPTION value), which removes them in
     prepare_to_extract() phase. However, with the addition of more
     meta-info to the incremental dumps, this should become unnecessary */
  WANT_DIRECTORY_REMOVE_OPTION
};
int remove_any_file (const char *file_name, enum remove_option option);
bool maybe_backup_file (const char *file_name, bool this_is_the_archive);
void undo_last_backup (void);

int deref_stat (char const *name, struct stat *buf);

size_t blocking_read (int fd, void *buf, size_t count);
size_t blocking_write (int fd, void const *buf, size_t count);

extern int chdir_current;
extern int chdir_fd;
int chdir_arg (char const *dir);
void chdir_do (int dir);
int chdir_count (void);

void close_diag (char const *name);
void open_diag (char const *name);
void read_diag_details (char const *name, off_t offset, size_t size);
void readlink_diag (char const *name);
void savedir_diag (char const *name);
void seek_diag_details (char const *name, off_t offset);
void stat_diag (char const *name);
void file_removed_diag (const char *name, bool top_level,
			void (*diagfn) (char const *name));
void write_error_details (char const *name, size_t status, size_t size);
void write_fatal (char const *name) __attribute__ ((noreturn));
void write_fatal_details (char const *name, ssize_t status, size_t size)
     __attribute__ ((noreturn));

pid_t xfork (void);
void xpipe (int fd[2]);

void *page_aligned_alloc (void **ptr, size_t size);
int set_file_atime (int fd, int parentfd, char const *file,
		    struct timespec atime);

/* Module names.c.  */

extern size_t name_count;
extern struct name *gnu_list_name;

void gid_to_gname (gid_t gid, char **gname);
int gname_to_gid (char const *gname, gid_t *pgid);
void uid_to_uname (uid_t uid, char **uname);
int uname_to_uid (char const *uname, uid_t *puid);

void name_init (void);
bool name_more_files (void);
void name_add_name (const char *name);
void name_term (void);
const char *name_next (int change_dirs);
void name_gather (void);
struct name *addname (char const *string, int change_dir,
		      bool cmdline, struct name *parent);
void remname (struct name *name);
bool name_match (const char *name);
void names_notfound (void);
void label_notfound (void);
void collect_and_sort_names (void);
struct name *name_scan (const char *name);
struct name const *name_from_list (void);
void blank_name_list (void);
char *make_file_name (const char *dir_name, const char *name);
size_t stripped_prefix_len (char const *file_name, size_t num);
bool all_names_found (struct tar_stat_info *st);

void add_avoided_name (char const *name);
bool is_avoided_name (char const *name);

bool contains_dot_dot (char const *name);

#define ISFOUND(c) (occurrence_option == 0			\
		    ? (c)->found_count != 0			\
		    : (c)->found_count == occurrence_option)
#define WASFOUND(c) (occurrence_option == 0			\
		     ? (c)->found_count != 0			\
		     : (c)->found_count >= occurrence_option)

/* Module tar.c.  */

void usage (int);

int confirm (const char *message_action, const char *name);

void tar_stat_init (struct tar_stat_info *st);
bool tar_stat_close (struct tar_stat_info *st);
void tar_stat_destroy (struct tar_stat_info *st);
void usage (int) __attribute__ ((noreturn));
int tar_timespec_cmp (struct timespec a, struct timespec b);
const char *archive_format_string (enum archive_format fmt);
const char *subcommand_string (enum subcommand c);
void set_exit_status (int val);

void request_stdin (const char *option);

/* Where an option comes from: */
enum option_source
  {
    OPTS_ENVIRON,        /* Environment variable TAR_OPTIONS */
    OPTS_COMMAND_LINE,   /* Command line */
    OPTS_FILE            /* File supplied by --files-from */
  };

/* Option location */
struct option_locus
{
  enum option_source source;  /* Option origin */
  char const *name;           /* File or variable name */
  size_t line;                /* Number of input line if source is OPTS_FILE */
  struct option_locus *prev;  /* Previous occurrence of the option of same
				 class */
};

void more_options (int argc, char **argv, struct option_locus *loc);

/* Module update.c.  */

extern char *output_start;

void update_archive (void);

/* Module attrs.c.  */
#include "xattrs.h"

/* Module xheader.c.  */

void xheader_decode (struct tar_stat_info *stat);
void xheader_decode_global (struct xheader *xhdr);
void xheader_store (char const *keyword, struct tar_stat_info *st,
		    void const *data);
void xheader_read (struct xheader *xhdr, union block *header, off_t size);
void xheader_write (char type, char *name, time_t t, struct xheader *xhdr);
void xheader_write_global (struct xheader *xhdr);
void xheader_finish (struct xheader *hdr);
void xheader_destroy (struct xheader *hdr);
char *xheader_xhdr_name (struct tar_stat_info *st);
char *xheader_ghdr_name (void);
void xheader_set_option (char *string);
void xheader_string_begin (struct xheader *xhdr);
void xheader_string_add (struct xheader *xhdr, char const *s);
bool xheader_string_end (struct xheader *xhdr, char const *keyword);
bool xheader_keyword_deleted_p (const char *kw);
char *xheader_format_name (struct tar_stat_info *st, const char *fmt,
			   size_t n);
void xheader_xattr_init (struct tar_stat_info *st);
void xheader_xattr_free (struct xattr_array *vals, size_t sz);
void xheader_xattr_copy (const struct tar_stat_info *st,
                         struct xattr_array **vals, size_t *sz);
void xheader_xattr_add (struct tar_stat_info *st,
                        const char *key, const char *val, size_t len);

/* Module system.c */

void sys_detect_dev_null_output (void);
void sys_save_archive_dev_ino (void);
void sys_wait_for_child (pid_t, bool);
void sys_spawn_shell (void);
bool sys_compare_uid (struct stat *a, struct stat *b);
bool sys_compare_gid (struct stat *a, struct stat *b);
bool sys_file_is_archive (struct tar_stat_info *p);
bool sys_compare_links (struct stat *link_data, struct stat *stat_data);
int sys_truncate (int fd);
pid_t sys_child_open_for_compress (void);
pid_t sys_child_open_for_uncompress (void);
size_t sys_write_archive_buffer (void);
bool sys_get_archive_stat (void);
int sys_exec_command (char *file_name, int typechar, struct tar_stat_info *st);
void sys_wait_command (void);
int sys_exec_info_script (const char **archive_name, int volume_number);
void sys_exec_checkpoint_script (const char *script_name,
				 const char *archive_name,
				 int checkpoint_number);

/* Module compare.c */
void report_difference (struct tar_stat_info *st, const char *message, ...)
  __attribute__ ((format (printf, 2, 3)));

/* Module sparse.c */
bool sparse_member_p (struct tar_stat_info *st);
bool sparse_fixup_header (struct tar_stat_info *st);
enum dump_status sparse_dump_file (int, struct tar_stat_info *st);
enum dump_status sparse_extract_file (int fd, struct tar_stat_info *st,
				      off_t *size);
enum dump_status sparse_skip_file (struct tar_stat_info *st);
bool sparse_diff_file (int, struct tar_stat_info *st);

/* Module utf8.c */
bool string_ascii_p (const char *str);
bool utf8_convert (bool to_utf, char const *input, char **output);

/* Module transform.c */
#define XFORM_REGFILE  0x01
#define XFORM_LINK     0x02
#define XFORM_SYMLINK  0x04
#define XFORM_ALL      (XFORM_REGFILE|XFORM_LINK|XFORM_SYMLINK)

void set_transform_expr (const char *expr);
bool transform_name (char **pinput, int type);
bool transform_name_fp (char **pinput, int type,
			char *(*fun)(char *, void *), void *);
bool transform_program_p (void);

/* Module suffix.c */
void set_compression_program_by_suffix (const char *name, const char *defprog);
char *strip_compression_suffix (const char *name);

/* Module checkpoint.c */
void checkpoint_compile_action (const char *str);
void checkpoint_finish_compile (void);
void checkpoint_run (bool do_write);
void checkpoint_finish (void);
void checkpoint_flush_actions (void);

/* Module warning.c */
#define WARN_ALONE_ZERO_BLOCK    0x00000001
#define WARN_BAD_DUMPDIR         0x00000002
#define WARN_CACHEDIR            0x00000004
#define WARN_CONTIGUOUS_CAST     0x00000008
#define WARN_FILE_CHANGED        0x00000010
#define WARN_FILE_IGNORED        0x00000020
#define WARN_FILE_REMOVED        0x00000040
#define WARN_FILE_SHRANK         0x00000080
#define WARN_FILE_UNCHANGED      0x00000100
#define WARN_FILENAME_WITH_NULS  0x00000200
#define WARN_IGNORE_ARCHIVE      0x00000400
#define WARN_IGNORE_NEWER        0x00000800
#define WARN_NEW_DIRECTORY       0x00001000
#define WARN_RENAME_DIRECTORY    0x00002000
#define WARN_SYMLINK_CAST        0x00004000
#define WARN_TIMESTAMP           0x00008000
#define WARN_UNKNOWN_CAST        0x00010000
#define WARN_UNKNOWN_KEYWORD     0x00020000
#define WARN_XDEV                0x00040000
#define WARN_DECOMPRESS_PROGRAM  0x00080000
#define WARN_EXISTING_FILE       0x00100000
#define WARN_XATTR_WRITE         0x00200000
#define WARN_RECORD_SIZE         0x00400000
#define WARN_FAILED_READ         0x00800000

/* These warnings are enabled by default in verbose mode: */
#define WARN_VERBOSE_WARNINGS    (WARN_RENAME_DIRECTORY|WARN_NEW_DIRECTORY|\
				  WARN_DECOMPRESS_PROGRAM|WARN_EXISTING_FILE|\
		                  WARN_RECORD_SIZE)
#define WARN_ALL                 (~WARN_VERBOSE_WARNINGS)

void set_warning_option (const char *arg);

extern int warning_option;

#define WARNING_ENABLED(opt) (warning_option & (opt))

#define WARNOPT(opt,args)			\
  do						\
    {						\
      if (WARNING_ENABLED(opt)) WARN (args);	\
    }						\
  while (0)

/* Module unlink.c */

void queue_deferred_unlink (const char *name, bool is_dir);
void finish_deferred_unlinks (void);

/* Module exit.c */
extern void (*fatal_exit_hook) (void);

/* Module exclist.c */
#define EXCL_DEFAULT       0x00
#define EXCL_RECURSIVE     0x01
#define EXCL_NON_RECURSIVE 0x02

void excfile_add (const char *name, int flags);
void info_attach_exclist (struct tar_stat_info *dir);
void info_free_exclist (struct tar_stat_info *dir);
bool excluded_name (char const *name, struct tar_stat_info *st);
void exclude_vcs_ignores (void);

/* Module map.c */
void owner_map_read (char const *name);
int owner_map_translate (uid_t uid, uid_t *new_uid, char const **new_name);
void group_map_read (char const *file);
int group_map_translate (gid_t gid, gid_t *new_gid, char const **new_name);


_GL_INLINE_HEADER_END
