/* Create a tar archive.

   Copyright 1985, 1992-1994, 1996-1997, 1999-2001, 2003-2007,
   2009-2010, 2012-2014, 2016-2017 Free Software Foundation, Inc.

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

   Written by John Gilmore, on 1985-08-25.  */

#include <system.h>

#include <areadlink.h>
#include <quotearg.h>

#include "common.h"
#include <hash.h>

/* Error number to use when an impostor is discovered.
   Pretend the impostor isn't there.  */
enum { IMPOSTOR_ERRNO = ENOENT };

struct link
  {
    dev_t dev;
    ino_t ino;
    nlink_t nlink;
    char name[1];
  };

struct exclusion_tag
{
  const char *name;
  size_t length;
  enum exclusion_tag_type type;
  bool (*predicate) (int fd);
  struct exclusion_tag *next;
};

static struct exclusion_tag *exclusion_tags;

void
add_exclusion_tag (const char *name, enum exclusion_tag_type type,
		   bool (*predicate) (int fd))
{
  struct exclusion_tag *tag = xmalloc (sizeof tag[0]);
  tag->next = exclusion_tags;
  tag->name = name;
  tag->type = type;
  tag->predicate = predicate;
  tag->length = strlen (name);
  exclusion_tags = tag;
}

void
exclusion_tag_warning (const char *dirname, const char *tagname,
		       const char *message)
{
  if (verbose_option)
    WARNOPT (WARN_CACHEDIR,
	     (0, 0,
	      _("%s: contains a cache directory tag %s; %s"),
	      quotearg_colon (dirname),
	      quotearg_n (1, tagname),
	      message));
}

enum exclusion_tag_type
check_exclusion_tags (struct tar_stat_info const *st, char const **tag_file_name)
{
  struct exclusion_tag *tag;

  for (tag = exclusion_tags; tag; tag = tag->next)
    {
      int tagfd = subfile_open (st, tag->name, open_read_flags);
      if (0 <= tagfd)
	{
	  bool satisfied = !tag->predicate || tag->predicate (tagfd);
	  close (tagfd);
	  if (satisfied)
	    {
	      if (tag_file_name)
		*tag_file_name = tag->name;
	      return tag->type;
	    }
	}
    }

  return exclusion_tag_none;
}

/* Exclusion predicate to test if the named file (usually "CACHEDIR.TAG")
   contains a valid header, as described at:
	http://www.brynosaurus.com/cachedir
   Applications can write this file into directories they create
   for use as caches containing purely regenerable, non-precious data,
   allowing us to avoid archiving them if --exclude-caches is specified. */

#define CACHEDIR_SIGNATURE "Signature: 8a477f597d28d172789f06886806bc55"
#define CACHEDIR_SIGNATURE_SIZE (sizeof CACHEDIR_SIGNATURE - 1)

bool
cachedir_file_p (int fd)
{
  char tagbuf[CACHEDIR_SIGNATURE_SIZE];

  return
    (read (fd, tagbuf, CACHEDIR_SIGNATURE_SIZE) == CACHEDIR_SIGNATURE_SIZE
     && memcmp (tagbuf, CACHEDIR_SIGNATURE, CACHEDIR_SIGNATURE_SIZE) == 0);
}


/* The maximum uintmax_t value that can be represented with DIGITS digits,
   assuming that each digit is BITS_PER_DIGIT wide.  */
#define MAX_VAL_WITH_DIGITS(digits, bits_per_digit) \
   ((digits) * (bits_per_digit) < sizeof (uintmax_t) * CHAR_BIT \
    ? ((uintmax_t) 1 << ((digits) * (bits_per_digit))) - 1 \
    : (uintmax_t) -1)

/* The maximum uintmax_t value that can be represented with octal
   digits and a trailing NUL in BUFFER.  */
#define MAX_OCTAL_VAL(buffer) MAX_VAL_WITH_DIGITS (sizeof (buffer) - 1, LG_8)

/* Convert VALUE to an octal representation suitable for tar headers.
   Output to buffer WHERE with size SIZE.
   The result is undefined if SIZE is 0 or if VALUE is too large to fit.  */

static void
to_octal (uintmax_t value, char *where, size_t size)
{
  uintmax_t v = value;
  size_t i = size;

  do
    {
      where[--i] = '0' + (v & ((1 << LG_8) - 1));
      v >>= LG_8;
    }
  while (i);
}

/* Copy at most LEN bytes from the string SRC to DST.  Terminate with
   NUL unless SRC is LEN or more bytes long.  */

static void
tar_copy_str (char *dst, const char *src, size_t len)
{
  size_t i;
  for (i = 0; i < len; i++)
    if (! (dst[i] = src[i]))
      break;
}

/* Same as tar_copy_str, but always terminate with NUL if using
   is OLDGNU format */

static void
tar_name_copy_str (char *dst, const char *src, size_t len)
{
  tar_copy_str (dst, src, len);
  if (archive_format == OLDGNU_FORMAT)
    dst[len-1] = 0;
}

/* Convert NEGATIVE VALUE to a base-256 representation suitable for
   tar headers.  NEGATIVE is 1 if VALUE was negative before being cast
   to uintmax_t, 0 otherwise.  Output to buffer WHERE with size SIZE.
   The result is undefined if SIZE is 0 or if VALUE is too large to
   fit.  */

static void
to_base256 (int negative, uintmax_t value, char *where, size_t size)
{
  uintmax_t v = value;
  uintmax_t propagated_sign_bits =
    ((uintmax_t) - negative << (CHAR_BIT * sizeof v - LG_256));
  size_t i = size;

  do
    {
      where[--i] = v & ((1 << LG_256) - 1);
      v = propagated_sign_bits | (v >> LG_256);
    }
  while (i);
}

#define GID_TO_CHARS(val, where) gid_to_chars (val, where, sizeof (where))
#define MAJOR_TO_CHARS(val, where) major_to_chars (val, where, sizeof (where))
#define MINOR_TO_CHARS(val, where) minor_to_chars (val, where, sizeof (where))
#define MODE_TO_CHARS(val, where) mode_to_chars (val, where, sizeof (where))
#define UID_TO_CHARS(val, where) uid_to_chars (val, where, sizeof (where))

#define UNAME_TO_CHARS(name, buf) string_to_chars (name, buf, sizeof (buf))
#define GNAME_TO_CHARS(name, buf) string_to_chars (name, buf, sizeof (buf))

static bool
to_chars (int negative, uintmax_t value, size_t valsize,
	  uintmax_t (*substitute) (int *),
	  char *where, size_t size, const char *type);

static bool
to_chars_subst (int negative, int gnu_format, uintmax_t value, size_t valsize,
		uintmax_t (*substitute) (int *),
		char *where, size_t size, const char *type)
{
  uintmax_t maxval = (gnu_format
		      ? MAX_VAL_WITH_DIGITS (size - 1, LG_256)
		      : MAX_VAL_WITH_DIGITS (size - 1, LG_8));
  char valbuf[UINTMAX_STRSIZE_BOUND + 1];
  char maxbuf[UINTMAX_STRSIZE_BOUND];
  char minbuf[UINTMAX_STRSIZE_BOUND + 1];
  char const *minval_string;
  char const *maxval_string = STRINGIFY_BIGINT (maxval, maxbuf);
  char const *value_string;

  if (gnu_format)
    {
      uintmax_t m = maxval + 1 ? maxval + 1 : maxval / 2 + 1;
      char *p = STRINGIFY_BIGINT (m, minbuf + 1);
      *--p = '-';
      minval_string = p;
    }
  else
    minval_string = "0";

  if (negative)
    {
      char *p = STRINGIFY_BIGINT (- value, valbuf + 1);
      *--p = '-';
      value_string = p;
    }
  else
    value_string = STRINGIFY_BIGINT (value, valbuf);

  if (substitute)
    {
      int negsub;
      uintmax_t sub = substitute (&negsub) & maxval;
      /* NOTE: This is one of the few places where GNU_FORMAT differs from
	 OLDGNU_FORMAT.  The actual differences are:

	 1. In OLDGNU_FORMAT all strings in a tar header end in \0
	 2. Incremental archives use oldgnu_header.

	 Apart from this they are completely identical. */
      uintmax_t s = (negsub &= archive_format == GNU_FORMAT) ? - sub : sub;
      char subbuf[UINTMAX_STRSIZE_BOUND + 1];
      char *sub_string = STRINGIFY_BIGINT (s, subbuf + 1);
      if (negsub)
	*--sub_string = '-';
      WARN ((0, 0, _("value %s out of %s range %s..%s; substituting %s"),
	     value_string, type, minval_string, maxval_string,
	     sub_string));
      return to_chars (negsub, s, valsize, 0, where, size, type);
    }
  else
    ERROR ((0, 0, _("value %s out of %s range %s..%s"),
	    value_string, type, minval_string, maxval_string));
  return false;
}

/* Convert NEGATIVE VALUE (which was originally of size VALSIZE) to
   external form, using SUBSTITUTE (...) if VALUE won't fit.  Output
   to buffer WHERE with size SIZE.  NEGATIVE is 1 iff VALUE was
   negative before being cast to uintmax_t; its original bitpattern
   can be deduced from VALSIZE, its original size before casting.
   TYPE is the kind of value being output (useful for diagnostics).
   Prefer the POSIX format of SIZE - 1 octal digits (with leading zero
   digits), followed by '\0'.  If this won't work, and if GNU or
   OLDGNU format is allowed, use '\200' followed by base-256, or (if
   NEGATIVE is nonzero) '\377' followed by two's complement base-256.
   If neither format works, use SUBSTITUTE (...)  instead.  Pass to
   SUBSTITUTE the address of an 0-or-1 flag recording whether the
   substitute value is negative.  */

static bool
to_chars (int negative, uintmax_t value, size_t valsize,
	  uintmax_t (*substitute) (int *),
	  char *where, size_t size, const char *type)
{
  int gnu_format = (archive_format == GNU_FORMAT
		    || archive_format == OLDGNU_FORMAT);

  /* Generate the POSIX octal representation if the number fits.  */
  if (! negative && value <= MAX_VAL_WITH_DIGITS (size - 1, LG_8))
    {
      where[size - 1] = '\0';
      to_octal (value, where, size - 1);
      return true;
    }
  else if (gnu_format)
    {
      /* Try to cope with the number by using traditional GNU format
	 methods */

      /* Generate the base-256 representation if the number fits.  */
      if (((negative ? -1 - value : value)
	   <= MAX_VAL_WITH_DIGITS (size - 1, LG_256)))
	{
	  where[0] = negative ? -1 : 1 << (LG_256 - 1);
	  to_base256 (negative, value, where + 1, size - 1);
	  return true;
	}

      /* Otherwise, if the number is negative, and if it would not cause
	 ambiguity on this host by confusing positive with negative
	 values, then generate the POSIX octal representation of the value
	 modulo 2**(field bits).  The resulting tar file is
	 machine-dependent, since it depends on the host word size.  Yuck!
	 But this is the traditional behavior.  */
      else if (negative && valsize * CHAR_BIT <= (size - 1) * LG_8)
	{
	  static int warned_once;
	  if (! warned_once)
	    {
	      warned_once = 1;
	      WARN ((0, 0, _("Generating negative octal headers")));
	    }
	  where[size - 1] = '\0';
	  to_octal (value & MAX_VAL_WITH_DIGITS (valsize * CHAR_BIT, 1),
		    where, size - 1);
	  return true;
	}
      /* Otherwise fall back to substitution, if possible: */
    }
  else
    substitute = NULL; /* No substitution for formats, other than GNU */

  return to_chars_subst (negative, gnu_format, value, valsize, substitute,
			 where, size, type);
}

static uintmax_t
gid_substitute (int *negative)
{
  gid_t r;
#ifdef GID_NOBODY
  r = GID_NOBODY;
#else
  static gid_t gid_nobody;
  if (!gid_nobody && !gname_to_gid ("nobody", &gid_nobody))
    gid_nobody = -2;
  r = gid_nobody;
#endif
  *negative = r < 0;
  return r;
}

static bool
gid_to_chars (gid_t v, char *p, size_t s)
{
  return to_chars (v < 0, (uintmax_t) v, sizeof v, gid_substitute, p, s, "gid_t");
}

static bool
major_to_chars (major_t v, char *p, size_t s)
{
  return to_chars (v < 0, (uintmax_t) v, sizeof v, 0, p, s, "major_t");
}

static bool
minor_to_chars (minor_t v, char *p, size_t s)
{
  return to_chars (v < 0, (uintmax_t) v, sizeof v, 0, p, s, "minor_t");
}

static bool
mode_to_chars (mode_t v, char *p, size_t s)
{
  /* In the common case where the internal and external mode bits are the same,
     and we are not using POSIX or GNU format,
     propagate all unknown bits to the external mode.
     This matches historical practice.
     Otherwise, just copy the bits we know about.  */
  int negative;
  uintmax_t u;
  if (S_ISUID == TSUID && S_ISGID == TSGID && S_ISVTX == TSVTX
      && S_IRUSR == TUREAD && S_IWUSR == TUWRITE && S_IXUSR == TUEXEC
      && S_IRGRP == TGREAD && S_IWGRP == TGWRITE && S_IXGRP == TGEXEC
      && S_IROTH == TOREAD && S_IWOTH == TOWRITE && S_IXOTH == TOEXEC
      && archive_format != POSIX_FORMAT
      && archive_format != USTAR_FORMAT
      && archive_format != GNU_FORMAT)
    {
      negative = v < 0;
      u = v;
    }
  else
    {
      negative = 0;
      u = ((v & S_ISUID ? TSUID : 0)
	   | (v & S_ISGID ? TSGID : 0)
	   | (v & S_ISVTX ? TSVTX : 0)
	   | (v & S_IRUSR ? TUREAD : 0)
	   | (v & S_IWUSR ? TUWRITE : 0)
	   | (v & S_IXUSR ? TUEXEC : 0)
	   | (v & S_IRGRP ? TGREAD : 0)
	   | (v & S_IWGRP ? TGWRITE : 0)
	   | (v & S_IXGRP ? TGEXEC : 0)
	   | (v & S_IROTH ? TOREAD : 0)
	   | (v & S_IWOTH ? TOWRITE : 0)
	   | (v & S_IXOTH ? TOEXEC : 0));
    }
  return to_chars (negative, u, sizeof v, 0, p, s, "mode_t");
}

bool
off_to_chars (off_t v, char *p, size_t s)
{
  return to_chars (v < 0, (uintmax_t) v, sizeof v, 0, p, s, "off_t");
}

bool
time_to_chars (time_t v, char *p, size_t s)
{
  return to_chars (v < 0, (uintmax_t) v, sizeof v, 0, p, s, "time_t");
}

static uintmax_t
uid_substitute (int *negative)
{
  uid_t r;
#ifdef UID_NOBODY
  r = UID_NOBODY;
#else
  static uid_t uid_nobody;
  if (!uid_nobody && !uname_to_uid ("nobody", &uid_nobody))
    uid_nobody = -2;
  r = uid_nobody;
#endif
  *negative = r < 0;
  return r;
}

static bool
uid_to_chars (uid_t v, char *p, size_t s)
{
  return to_chars (v < 0, (uintmax_t) v, sizeof v, uid_substitute, p, s, "uid_t");
}

static bool
uintmax_to_chars (uintmax_t v, char *p, size_t s)
{
  return to_chars (0, v, sizeof v, 0, p, s, "uintmax_t");
}

static void
string_to_chars (char const *str, char *p, size_t s)
{
  tar_copy_str (p, str, s);
  p[s - 1] = '\0';
}


/* A directory is always considered dumpable.
   Otherwise, only regular and contiguous files are considered dumpable.
   Such a file is dumpable if it is sparse and both --sparse and --totals
   are specified.
   Otherwise, it is dumpable unless any of the following conditions occur:

   a) it is empty *and* world-readable, or
   b) current archive is /dev/null */

static bool
file_dumpable_p (struct stat const *st)
{
  if (S_ISDIR (st->st_mode))
    return true;
  if (! (S_ISREG (st->st_mode) || S_ISCTG (st->st_mode)))
    return false;
  if (dev_null_output)
    return totals_option && sparse_option && ST_IS_SPARSE (*st);
  return ! (st->st_size == 0 && (st->st_mode & MODE_R) == MODE_R);
}


/* Writing routines.  */

/* Write the EOT block(s).  Zero at least two blocks, through the end
   of the record.  Old tar, as previous versions of GNU tar, writes
   garbage after two zeroed blocks.  */
void
write_eot (void)
{
  union block *pointer = find_next_block ();
  memset (pointer->buffer, 0, BLOCKSIZE);
  set_next_block_after (pointer);
  pointer = find_next_block ();
  memset (pointer->buffer, 0, available_space_after (pointer));
  set_next_block_after (pointer);
}

/* Write a "private" header */
union block *
start_private_header (const char *name, size_t size, time_t t)
{
  union block *header = find_next_block ();

  memset (header->buffer, 0, sizeof (union block));

  tar_name_copy_str (header->header.name, name, NAME_FIELD_SIZE);
  OFF_TO_CHARS (size, header->header.size);

  TIME_TO_CHARS (t < 0 ? 0 : min (t, MAX_OCTAL_VAL (header->header.mtime)),
		 header->header.mtime);
  MODE_TO_CHARS (S_IFREG|S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH, header->header.mode);
  UID_TO_CHARS (0, header->header.uid);
  GID_TO_CHARS (0, header->header.gid);
  memcpy (header->header.magic, TMAGIC, TMAGLEN);
  memcpy (header->header.version, TVERSION, TVERSLEN);
  return header;
}

/* Create a new header and store there at most NAME_FIELD_SIZE bytes of
   the file name */

static union block *
write_short_name (struct tar_stat_info *st)
{
  union block *header = find_next_block ();
  memset (header->buffer, 0, sizeof (union block));
  tar_name_copy_str (header->header.name, st->file_name, NAME_FIELD_SIZE);
  return header;
}

/* Write a GNUTYPE_LONGLINK or GNUTYPE_LONGNAME block.  */
static void
write_gnu_long_link (struct tar_stat_info *st, const char *p, char type)
{
  size_t size = strlen (p) + 1;
  size_t bufsize;
  union block *header;

  header = start_private_header ("././@LongLink", size, 0);
  if (! numeric_owner_option)
    {
      static char *uname, *gname;
      if (!uname)
	{
	  uid_to_uname (0, &uname);
	  gid_to_gname (0, &gname);
	}
      UNAME_TO_CHARS (uname, header->header.uname);
      GNAME_TO_CHARS (gname, header->header.gname);
    }

  strcpy (header->buffer + offsetof (struct posix_header, magic),
	  OLDGNU_MAGIC);
  header->header.typeflag = type;
  finish_header (st, header, -1);

  header = find_next_block ();

  bufsize = available_space_after (header);

  while (bufsize < size)
    {
      memcpy (header->buffer, p, bufsize);
      p += bufsize;
      size -= bufsize;
      set_next_block_after (header + (bufsize - 1) / BLOCKSIZE);
      header = find_next_block ();
      bufsize = available_space_after (header);
    }
  memcpy (header->buffer, p, size);
  memset (header->buffer + size, 0, bufsize - size);
  set_next_block_after (header + (size - 1) / BLOCKSIZE);
}

static size_t
split_long_name (const char *name, size_t length)
{
  size_t i;

  if (length > PREFIX_FIELD_SIZE + 1)
    length = PREFIX_FIELD_SIZE + 1;
  else if (ISSLASH (name[length - 1]))
    length--;
  for (i = length - 1; i > 0; i--)
    if (ISSLASH (name[i]))
      break;
  return i;
}

static union block *
write_ustar_long_name (const char *name)
{
  size_t length = strlen (name);
  size_t i, nlen;
  union block *header;

  if (length > PREFIX_FIELD_SIZE + NAME_FIELD_SIZE + 1)
    {
      ERROR ((0, 0, _("%s: file name is too long (max %d); not dumped"),
	      quotearg_colon (name),
	      PREFIX_FIELD_SIZE + NAME_FIELD_SIZE + 1));
      return NULL;
    }

  i = split_long_name (name, length);
  if (i == 0 || (nlen = length - i - 1) > NAME_FIELD_SIZE || nlen == 0)
    {
      ERROR ((0, 0,
	      _("%s: file name is too long (cannot be split); not dumped"),
	      quotearg_colon (name)));
      return NULL;
    }

  header = find_next_block ();
  memset (header->buffer, 0, sizeof (header->buffer));
  memcpy (header->header.prefix, name, i);
  memcpy (header->header.name, name + i + 1, length - i - 1);

  return header;
}

/* Write a long link name, depending on the current archive format */
static void
write_long_link (struct tar_stat_info *st)
{
  switch (archive_format)
    {
    case POSIX_FORMAT:
      xheader_store ("linkpath", st, NULL);
      break;

    case V7_FORMAT:			/* old V7 tar format */
    case USTAR_FORMAT:
    case STAR_FORMAT:
      ERROR ((0, 0,
	      _("%s: link name is too long; not dumped"),
	      quotearg_colon (st->link_name)));
      break;

    case OLDGNU_FORMAT:
    case GNU_FORMAT:
      write_gnu_long_link (st, st->link_name, GNUTYPE_LONGLINK);
      break;

    default:
      abort(); /*FIXME*/
    }
}

static union block *
write_long_name (struct tar_stat_info *st)
{
  switch (archive_format)
    {
    case POSIX_FORMAT:
      xheader_store ("path", st, NULL);
      break;

    case V7_FORMAT:
      if (strlen (st->file_name) > NAME_FIELD_SIZE-1)
	{
	  ERROR ((0, 0, _("%s: file name is too long (max %d); not dumped"),
		  quotearg_colon (st->file_name),
		  NAME_FIELD_SIZE - 1));
	  return NULL;
	}
      break;

    case USTAR_FORMAT:
    case STAR_FORMAT:
      return write_ustar_long_name (st->file_name);

    case OLDGNU_FORMAT:
    case GNU_FORMAT:
      write_gnu_long_link (st, st->file_name, GNUTYPE_LONGNAME);
      break;

    default:
      abort(); /*FIXME*/
    }
  return write_short_name (st);
}

union block *
write_extended (bool global, struct tar_stat_info *st, union block *old_header)
{
  union block *header, hp;
  char *p;
  int type;
  time_t t;

  if (st->xhdr.buffer || st->xhdr.stk == NULL)
    return old_header;

  xheader_finish (&st->xhdr);
  memcpy (hp.buffer, old_header, sizeof (hp));
  if (global)
    {
      type = XGLTYPE;
      p = xheader_ghdr_name ();
      t = start_time.tv_sec;
    }
  else
    {
      type = XHDTYPE;
      p = xheader_xhdr_name (st);
      t = set_mtime_option ? mtime_option.tv_sec : st->stat.st_mtime;
    }
  xheader_write (type, p, t, &st->xhdr);
  free (p);
  header = find_next_block ();
  memcpy (header, &hp.buffer, sizeof (hp.buffer));
  return header;
}

static union block *
write_header_name (struct tar_stat_info *st)
{
  if (archive_format == POSIX_FORMAT && !string_ascii_p (st->file_name))
    {
      xheader_store ("path", st, NULL);
      return write_short_name (st);
    }
  else if (NAME_FIELD_SIZE - (archive_format == OLDGNU_FORMAT)
	   < strlen (st->file_name))
    return write_long_name (st);
  else
    return write_short_name (st);
}


/* Header handling.  */

/* Make a header block for the file whose stat info is st,
   and return its address.  */

union block *
start_header (struct tar_stat_info *st)
{
  union block *header;
  char const *uname = NULL;
  char const *gname = NULL;

  header = write_header_name (st);
  if (!header)
    return NULL;

  /* Override some stat fields, if requested to do so.  */
  owner_map_translate (st->stat.st_uid, &st->stat.st_uid, &uname);
  group_map_translate (st->stat.st_gid, &st->stat.st_gid, &gname);

  if (mode_option)
    st->stat.st_mode =
      ((st->stat.st_mode & ~MODE_ALL)
       | mode_adjust (st->stat.st_mode, S_ISDIR (st->stat.st_mode) != 0,
		      initial_umask, mode_option, NULL));

  /* Paul Eggert tried the trivial test ($WRITER cf a b; $READER tvf a)
     for a few tars and came up with the following interoperability
     matrix:

	      WRITER
	1 2 3 4 5 6 7 8 9   READER
	. . . . . . . . .   1 = SunOS 4.2 tar
	# . . # # . . # #   2 = NEC SVR4.0.2 tar
	. . . # # . . # .   3 = Solaris 2.1 tar
	. . . . . . . . .   4 = GNU tar 1.11.1
	. . . . . . . . .   5 = HP-UX 8.07 tar
	. . . . . . . . .   6 = Ultrix 4.1
	. . . . . . . . .   7 = AIX 3.2
	. . . . . . . . .   8 = Hitachi HI-UX 1.03
	. . . . . . . . .   9 = Omron UNIOS-B 4.3BSD 1.60Beta

	     . = works
	     # = "impossible file type"

     The following mask for old archive removes the '#'s in column 4
     above, thus making GNU tar both a universal donor and a universal
     acceptor for Paul's test.  */

  if (archive_format == V7_FORMAT || archive_format == USTAR_FORMAT)
    MODE_TO_CHARS (st->stat.st_mode & MODE_ALL, header->header.mode);
  else
    MODE_TO_CHARS (st->stat.st_mode, header->header.mode);

  {
    uid_t uid = st->stat.st_uid;
    if (archive_format == POSIX_FORMAT
	&& MAX_OCTAL_VAL (header->header.uid) < uid)
      {
	xheader_store ("uid", st, NULL);
	uid = 0;
      }
    if (!UID_TO_CHARS (uid, header->header.uid))
      return NULL;
  }

  {
    gid_t gid = st->stat.st_gid;
    if (archive_format == POSIX_FORMAT
	&& MAX_OCTAL_VAL (header->header.gid) < gid)
      {
	xheader_store ("gid", st, NULL);
	gid = 0;
      }
    if (!GID_TO_CHARS (gid, header->header.gid))
      return NULL;
  }

  {
    off_t size = st->stat.st_size;
    if (archive_format == POSIX_FORMAT
	&& MAX_OCTAL_VAL (header->header.size) < size)
      {
	xheader_store ("size", st, NULL);
	size = 0;
      }
    if (!OFF_TO_CHARS (size, header->header.size))
      return NULL;
  }

  {
    struct timespec mtime;

    switch (set_mtime_option)
      {
      case USE_FILE_MTIME:
	mtime = st->mtime;
	break;

      case FORCE_MTIME:
	mtime = mtime_option;
	break;

      case CLAMP_MTIME:
	mtime = timespec_cmp (st->mtime, mtime_option) > 0
	           ? mtime_option : st->mtime;
	break;
      }

    if (archive_format == POSIX_FORMAT)
      {
	if (MAX_OCTAL_VAL (header->header.mtime) < mtime.tv_sec
	    || mtime.tv_nsec != 0)
	  xheader_store ("mtime", st, &mtime);
	if (MAX_OCTAL_VAL (header->header.mtime) < mtime.tv_sec)
	  mtime.tv_sec = 0;
      }
    if (!TIME_TO_CHARS (mtime.tv_sec, header->header.mtime))
      return NULL;
  }

  /* FIXME */
  if (S_ISCHR (st->stat.st_mode)
      || S_ISBLK (st->stat.st_mode))
    {
      major_t devmajor = major (st->stat.st_rdev);
      minor_t devminor = minor (st->stat.st_rdev);

      if (archive_format == POSIX_FORMAT
	  && MAX_OCTAL_VAL (header->header.devmajor) < devmajor)
	{
	  xheader_store ("devmajor", st, NULL);
	  devmajor = 0;
	}
      if (!MAJOR_TO_CHARS (devmajor, header->header.devmajor))
	return NULL;

      if (archive_format == POSIX_FORMAT
	  && MAX_OCTAL_VAL (header->header.devminor) < devminor)
	{
	  xheader_store ("devminor", st, NULL);
	  devminor = 0;
	}
      if (!MINOR_TO_CHARS (devminor, header->header.devminor))
	return NULL;
    }
  else if (archive_format != GNU_FORMAT && archive_format != OLDGNU_FORMAT)
    {
      if (!(MAJOR_TO_CHARS (0, header->header.devmajor)
	    && MINOR_TO_CHARS (0, header->header.devminor)))
	return NULL;
    }

  if (archive_format == POSIX_FORMAT)
    {
      xheader_store ("atime", st, NULL);
      xheader_store ("ctime", st, NULL);
    }
  else if (incremental_option)
    if (archive_format == OLDGNU_FORMAT || archive_format == GNU_FORMAT)
      {
	TIME_TO_CHARS (st->atime.tv_sec, header->oldgnu_header.atime);
	TIME_TO_CHARS (st->ctime.tv_sec, header->oldgnu_header.ctime);
      }

  header->header.typeflag = archive_format == V7_FORMAT ? AREGTYPE : REGTYPE;

  switch (archive_format)
    {
    case V7_FORMAT:
      break;

    case OLDGNU_FORMAT:
    case GNU_FORMAT:   /*FIXME?*/
      /* Overwrite header->header.magic and header.version in one blow.  */
      strcpy (header->buffer + offsetof (struct posix_header, magic),
	      OLDGNU_MAGIC);
      break;

    case POSIX_FORMAT:
    case USTAR_FORMAT:
      memcpy (header->header.magic, TMAGIC, TMAGLEN);
      memcpy (header->header.version, TVERSION, TVERSLEN);
      break;

    default:
      abort ();
    }

  if (archive_format == V7_FORMAT || numeric_owner_option)
    {
      /* header->header.[ug]name are left as the empty string.  */
    }
  else
    {
      if (uname)
	st->uname = xstrdup (uname);
      else
	uid_to_uname (st->stat.st_uid, &st->uname);

      if (gname)
	st->gname = xstrdup (gname);
      else
	gid_to_gname (st->stat.st_gid, &st->gname);

      if (archive_format == POSIX_FORMAT
	  && (strlen (st->uname) > UNAME_FIELD_SIZE
	      || !string_ascii_p (st->uname)))
	xheader_store ("uname", st, NULL);
      UNAME_TO_CHARS (st->uname, header->header.uname);

      if (archive_format == POSIX_FORMAT
	  && (strlen (st->gname) > GNAME_FIELD_SIZE
	      || !string_ascii_p (st->gname)))
	xheader_store ("gname", st, NULL);
      GNAME_TO_CHARS (st->gname, header->header.gname);
    }

  if (archive_format == POSIX_FORMAT)
    {
      if (acls_option > 0)
        {
          if (st->acls_a_ptr)
            xheader_store ("SCHILY.acl.access", st, NULL);
          if (st->acls_d_ptr)
            xheader_store ("SCHILY.acl.default", st, NULL);
        }
      if ((selinux_context_option > 0) && st->cntx_name)
        xheader_store ("RHT.security.selinux", st, NULL);
      if (xattrs_option > 0)
        {
          size_t scan_xattr = 0;
          struct xattr_array *xattr_map = st->xattr_map;

          while (scan_xattr < st->xattr_map_size)
            {
              xheader_store (xattr_map[scan_xattr].xkey, st, &scan_xattr);
              ++scan_xattr;
            }
        }
    }

  return header;
}

void
simple_finish_header (union block *header)
{
  size_t i;
  int sum;
  char *p;

  memcpy (header->header.chksum, CHKBLANKS, sizeof header->header.chksum);

  sum = 0;
  p = header->buffer;
  for (i = sizeof *header; i-- != 0; )
    /* We can't use unsigned char here because of old compilers, e.g. V7.  */
    sum += 0xFF & *p++;

  /* Fill in the checksum field.  It's formatted differently from the
     other fields: it has [6] digits, a null, then a space -- rather than
     digits, then a null.  We use to_chars.
     The final space is already there, from
     checksumming, and to_chars doesn't modify it.

     This is a fast way to do:

     sprintf(header->header.chksum, "%6o", sum);  */

  uintmax_to_chars ((uintmax_t) sum, header->header.chksum, 7);

  set_next_block_after (header);
}

/* Finish off a filled-in header block and write it out.  We also
   print the file name and/or full info if verbose is on.  If BLOCK_ORDINAL
   is not negative, is the block ordinal of the first record for this
   file, which may be a preceding long name or long link record.  */
void
finish_header (struct tar_stat_info *st,
	       union block *header, off_t block_ordinal)
{
  /* Note: It is important to do this before the call to write_extended(),
     so that the actual ustar header is printed */
  if (verbose_option
      && header->header.typeflag != GNUTYPE_LONGLINK
      && header->header.typeflag != GNUTYPE_LONGNAME
      && header->header.typeflag != XHDTYPE
      && header->header.typeflag != XGLTYPE)
    {
      /* FIXME: This global is used in print_header, sigh.  */
      current_format = archive_format;
      print_header (st, header, block_ordinal);
    }

  header = write_extended (false, st, header);
  simple_finish_header (header);
}


void
pad_archive (off_t size_left)
{
  union block *blk;
  while (size_left > 0)
    {
      blk = find_next_block ();
      memset (blk->buffer, 0, BLOCKSIZE);
      set_next_block_after (blk);
      size_left -= BLOCKSIZE;
    }
}

static enum dump_status
dump_regular_file (int fd, struct tar_stat_info *st)
{
  off_t size_left = st->stat.st_size;
  off_t block_ordinal;
  union block *blk;

  block_ordinal = current_block_ordinal ();
  blk = start_header (st);
  if (!blk)
    return dump_status_fail;

  /* Mark contiguous files, if we support them.  */
  if (archive_format != V7_FORMAT && S_ISCTG (st->stat.st_mode))
    blk->header.typeflag = CONTTYPE;

  finish_header (st, blk, block_ordinal);

  mv_begin_write (st->file_name, st->stat.st_size, st->stat.st_size);
  while (size_left > 0)
    {
      size_t bufsize, count;

      blk = find_next_block ();

      bufsize = available_space_after (blk);

      if (size_left < bufsize)
	{
	  /* Last read -- zero out area beyond.  */
	  bufsize = size_left;
	  count = bufsize % BLOCKSIZE;
	  if (count)
	    memset (blk->buffer + size_left, 0, BLOCKSIZE - count);
	}

      count = (fd <= 0) ? bufsize : blocking_read (fd, blk->buffer, bufsize);
      if (count == SAFE_READ_ERROR)
	{
	  read_diag_details (st->orig_file_name,
	                     st->stat.st_size - size_left, bufsize);
	  pad_archive (size_left);
	  return dump_status_short;
	}
      size_left -= count;
      set_next_block_after (blk + (bufsize - 1) / BLOCKSIZE);

      if (count != bufsize)
	{
	  char buf[UINTMAX_STRSIZE_BOUND];
	  memset (blk->buffer + count, 0, bufsize - count);
	  WARNOPT (WARN_FILE_SHRANK,
		   (0, 0,
		    ngettext ("%s: File shrank by %s byte; padding with zeros",
			      "%s: File shrank by %s bytes; padding with zeros",
			      size_left),
		    quotearg_colon (st->orig_file_name),
		    STRINGIFY_BIGINT (size_left, buf)));
	  if (! ignore_failed_read_option)
	    set_exit_status (TAREXIT_DIFFERS);
	  pad_archive (size_left - (bufsize - count));
	  return dump_status_short;
	}
    }
  return dump_status_ok;
}


/* Copy info from the directory identified by ST into the archive.
   DIRECTORY contains the directory's entries.  */

static void
dump_dir0 (struct tar_stat_info *st, char const *directory)
{
  bool top_level = ! st->parent;
  const char *tag_file_name;
  union block *blk = NULL;
  off_t block_ordinal = current_block_ordinal ();

  st->stat.st_size = 0;	/* force 0 size on dir */

  blk = start_header (st);
  if (!blk)
    return;

  info_attach_exclist (st);

  if (incremental_option && archive_format != POSIX_FORMAT)
    blk->header.typeflag = GNUTYPE_DUMPDIR;
  else /* if (standard_option) */
    blk->header.typeflag = DIRTYPE;

  /* If we're gnudumping, we aren't done yet so don't close it.  */

  if (!incremental_option)
    finish_header (st, blk, block_ordinal);
  else if (gnu_list_name->directory)
    {
      if (archive_format == POSIX_FORMAT)
	{
	  xheader_store ("GNU.dumpdir", st,
			 safe_directory_contents (gnu_list_name->directory));
	  finish_header (st, blk, block_ordinal);
	}
      else
	{
	  off_t size_left;
	  off_t totsize;
	  size_t bufsize;
	  ssize_t count;
	  const char *buffer, *p_buffer;

	  block_ordinal = current_block_ordinal ();
	  buffer = safe_directory_contents (gnu_list_name->directory);
	  totsize = dumpdir_size (buffer);
	  OFF_TO_CHARS (totsize, blk->header.size);
	  finish_header (st, blk, block_ordinal);
	  p_buffer = buffer;
	  size_left = totsize;

	  mv_begin_write (st->file_name, totsize, totsize);
	  while (size_left > 0)
	    {
	      blk = find_next_block ();
	      bufsize = available_space_after (blk);
	      if (size_left < bufsize)
		{
		  bufsize = size_left;
		  count = bufsize % BLOCKSIZE;
		  if (count)
		    memset (blk->buffer + size_left, 0, BLOCKSIZE - count);
		}
	      memcpy (blk->buffer, p_buffer, bufsize);
	      size_left -= bufsize;
	      p_buffer += bufsize;
	      set_next_block_after (blk + (bufsize - 1) / BLOCKSIZE);
	    }
	}
      return;
    }

  if (!recursion_option)
    return;

  if (one_file_system_option
      && !top_level
      && st->parent->stat.st_dev != st->stat.st_dev)
    {
      if (verbose_option)
	WARNOPT (WARN_XDEV,
		 (0, 0,
		  _("%s: file is on a different filesystem; not dumped"),
		  quotearg_colon (st->orig_file_name)));
    }
  else
    {
      char *name_buf;
      size_t name_size;

      switch (check_exclusion_tags (st, &tag_file_name))
	{
	case exclusion_tag_all:
	  /* Handled in dump_file0 */
	  break;

	case exclusion_tag_none:
	  {
	    char const *entry;
	    size_t entry_len;
	    size_t name_len;

	    name_buf = xstrdup (st->orig_file_name);
	    name_size = name_len = strlen (name_buf);

	    /* Now output all the files in the directory.  */
	    for (entry = directory; (entry_len = strlen (entry)) != 0;
		 entry += entry_len + 1)
	      {
		if (name_size < name_len + entry_len)
		  {
		    name_size = name_len + entry_len;
		    name_buf = xrealloc (name_buf, name_size + 1);
		  }
		strcpy (name_buf + name_len, entry);
		if (!excluded_name (name_buf, st))
		  dump_file (st, entry, name_buf);
	      }

	    free (name_buf);
	  }
	  break;

	case exclusion_tag_contents:
	  exclusion_tag_warning (st->orig_file_name, tag_file_name,
				 _("contents not dumped"));
	  name_size = strlen (st->orig_file_name) + strlen (tag_file_name) + 1;
	  name_buf = xmalloc (name_size);
	  strcpy (name_buf, st->orig_file_name);
	  strcat (name_buf, tag_file_name);
	  dump_file (st, tag_file_name, name_buf);
	  free (name_buf);
	  break;

	case exclusion_tag_under:
	  exclusion_tag_warning (st->orig_file_name, tag_file_name,
				 _("contents not dumped"));
	  break;
	}
    }
}

/* Ensure exactly one trailing slash.  */
static void
ensure_slash (char **pstr)
{
  size_t len = strlen (*pstr);
  while (len >= 1 && ISSLASH ((*pstr)[len - 1]))
    len--;
  if (!ISSLASH ((*pstr)[len]))
    *pstr = xrealloc (*pstr, len + 2);
  (*pstr)[len++] = '/';
  (*pstr)[len] = '\0';
}

/* If we just ran out of file descriptors, release a file descriptor
   in the directory chain somewhere leading from DIR->parent->parent
   up through the root.  Return true if successful, false (preserving
   errno == EMFILE) otherwise.

   Do not release DIR's file descriptor, or DIR's parent, as other
   code assumes that they work.  On some operating systems, another
   process can claim file descriptor resources as we release them, and
   some calls or their emulations require multiple file descriptors,
   so callers should not give up if a single release doesn't work.  */

static bool
open_failure_recover (struct tar_stat_info const *dir)
{
  if (errno == EMFILE && dir && dir->parent)
    {
      struct tar_stat_info *p;
      for (p = dir->parent->parent; p; p = p->parent)
	if (0 < p->fd && (! p->parent || p->parent->fd <= 0))
	  {
	    tar_stat_close (p);
	    return true;
	  }
      errno = EMFILE;
    }

  return false;
}

/* Return the directory entries of ST, in a dynamically allocated buffer,
   each entry followed by '\0' and the last followed by an extra '\0'.
   Return null on failure, setting errno.  */
char *
get_directory_entries (struct tar_stat_info *st)
{
  while (! (st->dirstream = fdopendir (st->fd)))
    if (! open_failure_recover (st))
      return 0;
  return streamsavedir (st->dirstream, savedir_sort_order);
}

/* Dump the directory ST.  Return true if successful, false (emitting
   diagnostics) otherwise.  Get ST's entries, recurse through its
   subdirectories, and clean up file descriptors afterwards.  */
static bool
dump_dir (struct tar_stat_info *st)
{
  char *directory = get_directory_entries (st);
  if (! directory)
    {
      savedir_diag (st->orig_file_name);
      return false;
    }

  dump_dir0 (st, directory);

  restore_parent_fd (st);
  free (directory);
  return true;
}


/* Number of links a file can have without having to be entered into
   the link table.  Typically this is 1, but in trickier circumstances
   it is 0.  */
static nlink_t trivial_link_count;


/* Main functions of this module.  */

void
create_archive (void)
{
  struct name const *p;

  trivial_link_count = name_count <= 1 && ! dereference_option;

  open_archive (ACCESS_WRITE);
  buffer_write_global_xheader ();

  if (incremental_option)
    {
      size_t buffer_size = 1000;
      char *buffer = xmalloc (buffer_size);
      const char *q;

      collect_and_sort_names ();

      while ((p = name_from_list ()) != NULL)
	if (!excluded_name (p->name, NULL))
	  dump_file (0, p->name, p->name);

      blank_name_list ();
      while ((p = name_from_list ()) != NULL)
	if (!excluded_name (p->name, NULL))
	  {
	    struct tar_stat_info st;
	    size_t plen = strlen (p->name);
	    if (buffer_size <= plen)
	      {
		while ((buffer_size *= 2) <= plen)
		  continue;
		buffer = xrealloc (buffer, buffer_size);
	      }
	    memcpy (buffer, p->name, plen);
	    if (! ISSLASH (buffer[plen - 1]))
	      buffer[plen++] = DIRECTORY_SEPARATOR;
	    tar_stat_init (&st);
	    q = directory_contents (p->directory);
	    if (q)
	      while (*q)
		{
		  size_t qlen = strlen (q);
		  if (*q == 'Y')
		    {
		      if (! st.orig_file_name)
			{
			  int fd = openat (chdir_fd, p->name,
					   open_searchdir_flags);
			  if (fd < 0)
			    {
			      file_removed_diag (p->name, !p->parent,
						 open_diag);
			      break;
			    }
			  st.fd = fd;
			  if (fstat (fd, &st.stat) != 0)
			    {
			      file_removed_diag (p->name, !p->parent,
						 stat_diag);
			      break;
			    }
			  st.orig_file_name = xstrdup (p->name);
			}
		      if (buffer_size < plen + qlen)
			{
			  while ((buffer_size *=2 ) < plen + qlen)
			    continue;
			  buffer = xrealloc (buffer, buffer_size);
 			}
		      strcpy (buffer + plen, q + 1);
		      dump_file (&st, q + 1, buffer);
		    }
		  q += qlen + 1;
		}
	    tar_stat_destroy (&st);
	  }
      free (buffer);
    }
  else
    {
      const char *name;
      while ((name = name_next (1)) != NULL)
	if (!excluded_name (name, NULL))
	  dump_file (0, name, name);
    }

  write_eot ();
  close_archive ();
  finish_deferred_unlinks ();
  if (listed_incremental_option)
    write_directory_file ();
}


/* Calculate the hash of a link.  */
static size_t
hash_link (void const *entry, size_t n_buckets)
{
  struct link const *l = entry;
  uintmax_t num = l->dev ^ l->ino;
  return num % n_buckets;
}

/* Compare two links for equality.  */
static bool
compare_links (void const *entry1, void const *entry2)
{
  struct link const *link1 = entry1;
  struct link const *link2 = entry2;
  return ((link1->dev ^ link2->dev) | (link1->ino ^ link2->ino)) == 0;
}

static void
unknown_file_error (char const *p)
{
  WARNOPT (WARN_FILE_IGNORED,
	   (0, 0, _("%s: Unknown file type; file ignored"),
	    quotearg_colon (p)));
  if (!ignore_failed_read_option)
    set_exit_status (TAREXIT_FAILURE);
}


/* Handling of hard links */

/* Table of all non-directories that we've written so far.  Any time
   we see another, we check the table and avoid dumping the data
   again if we've done it once already.  */
static Hash_table *link_table;

/* Try to dump stat as a hard link to another file in the archive.
   Return true if successful.  */
static bool
dump_hard_link (struct tar_stat_info *st)
{
  if (link_table
      && (trivial_link_count < st->stat.st_nlink || remove_files_option))
    {
      struct link lp;
      struct link *duplicate;
      off_t block_ordinal;
      union block *blk;

      lp.ino = st->stat.st_ino;
      lp.dev = st->stat.st_dev;

      if ((duplicate = hash_lookup (link_table, &lp)))
	{
	  /* We found a link.  */
	  char const *link_name = safer_name_suffix (duplicate->name, true,
	                                             absolute_names_option);
	  if (duplicate->nlink)
	    duplicate->nlink--;

	  block_ordinal = current_block_ordinal ();
	  assign_string (&st->link_name, link_name);
	  if (NAME_FIELD_SIZE - (archive_format == OLDGNU_FORMAT)
	      < strlen (link_name))
	    write_long_link (st);

	  st->stat.st_size = 0;
	  blk = start_header (st);
	  if (!blk)
	    return false;
	  tar_copy_str (blk->header.linkname, link_name, NAME_FIELD_SIZE);

	  blk->header.typeflag = LNKTYPE;
	  finish_header (st, blk, block_ordinal);

	  if (remove_files_option)
	    queue_deferred_unlink (st->orig_file_name, false);

	  return true;
	}
    }
  return false;
}

static void
file_count_links (struct tar_stat_info *st)
{
  if (hard_dereference_option)
    return;
  if (trivial_link_count < st->stat.st_nlink)
    {
      struct link *duplicate;
      char *linkname = NULL;
      struct link *lp;

      assign_string (&linkname, safer_name_suffix (st->orig_file_name, true,
						   absolute_names_option));
      transform_name (&linkname, XFORM_LINK);

      lp = xmalloc (offsetof (struct link, name)
				 + strlen (linkname) + 1);
      lp->ino = st->stat.st_ino;
      lp->dev = st->stat.st_dev;
      lp->nlink = st->stat.st_nlink;
      strcpy (lp->name, linkname);
      free (linkname);

      if (! ((link_table
	      || (link_table = hash_initialize (0, 0, hash_link,
						compare_links, 0)))
	     && (duplicate = hash_insert (link_table, lp))))
	xalloc_die ();

      if (duplicate != lp)
	abort ();
      lp->nlink--;
    }
}

/* For each dumped file, check if all its links were dumped. Emit
   warnings if it is not so. */
void
check_links (void)
{
  struct link *lp;

  if (!link_table)
    return;

  for (lp = hash_get_first (link_table); lp;
       lp = hash_get_next (link_table, lp))
    {
      if (lp->nlink)
	{
	  WARN ((0, 0, _("Missing links to %s."), quote (lp->name)));
	}
    }
}

/* Assuming DIR is the working directory, open FILE, using FLAGS to
   control the open.  A null DIR means to use ".".  If we are low on
   file descriptors, try to release one or more from DIR's parents to
   reuse it.  */
int
subfile_open (struct tar_stat_info const *dir, char const *file, int flags)
{
  int fd;

  static bool initialized;
  if (! initialized)
    {
      /* Initialize any tables that might be needed when file
	 descriptors are exhausted, and whose initialization might
	 require a file descriptor.  This includes the system message
	 catalog and tar's message catalog.  */
      initialized = true;
      strerror (ENOENT);
      gettext ("");
    }

  while ((fd = openat (dir ? dir->fd : chdir_fd, file, flags)) < 0
	 && open_failure_recover (dir))
    continue;
  return fd;
}

/* Restore the file descriptor for ST->parent, if it was temporarily
   closed to conserve file descriptors.  On failure, set the file
   descriptor to the negative of the corresponding errno value.  Call
   this every time a subdirectory is ascended from.  */
void
restore_parent_fd (struct tar_stat_info const *st)
{
  struct tar_stat_info *parent = st->parent;
  if (parent && ! parent->fd)
    {
      int parentfd = openat (st->fd, "..", open_searchdir_flags);
      struct stat parentstat;

      if (parentfd < 0)
	parentfd = - errno;
      else if (! (fstat (parentfd, &parentstat) == 0
		  && parent->stat.st_ino == parentstat.st_ino
		  && parent->stat.st_dev == parentstat.st_dev))
	{
	  close (parentfd);
	  parentfd = IMPOSTOR_ERRNO;
	}

      if (parentfd < 0)
	{
	  int origfd = openat (chdir_fd, parent->orig_file_name,
			       open_searchdir_flags);
	  if (0 <= origfd)
	    {
	      if (fstat (parentfd, &parentstat) == 0
		  && parent->stat.st_ino == parentstat.st_ino
		  && parent->stat.st_dev == parentstat.st_dev)
		parentfd = origfd;
	      else
		close (origfd);
	    }
	}

      parent->fd = parentfd;
    }
}

/* Dump a single file, recursing on directories.  ST is the file's
   status info, NAME its name relative to the parent directory, and P
   its full name (which may be relative to the working directory).  */

/* FIXME: One should make sure that for *every* path leading to setting
   exit_status to failure, a clear diagnostic has been issued.  */

static void
dump_file0 (struct tar_stat_info *st, char const *name, char const *p)
{
  union block *header;
  char type;
  off_t original_size;
  struct timespec original_ctime;
  off_t block_ordinal = -1;
  int fd = 0;
  bool is_dir;
  struct tar_stat_info const *parent = st->parent;
  bool top_level = ! parent;
  int parentfd = top_level ? chdir_fd : parent->fd;
  void (*diag) (char const *) = 0;

  if (interactive_option && !confirm ("add", p))
    return;

  assign_string (&st->orig_file_name, p);
  assign_string (&st->file_name,
                 safer_name_suffix (p, false, absolute_names_option));

  transform_name (&st->file_name, XFORM_REGFILE);

  if (parentfd < 0 && ! top_level)
    {
      errno = - parentfd;
      diag = open_diag;
    }
  else if (fstatat (parentfd, name, &st->stat, fstatat_flags) != 0)
    diag = stat_diag;
  else if (file_dumpable_p (&st->stat))
    {
      fd = subfile_open (parent, name, open_read_flags);
      if (fd < 0)
	diag = open_diag;
      else
	{
	  st->fd = fd;
	  if (fstat (fd, &st->stat) != 0)
	    diag = stat_diag;
	}
    }
  if (diag)
    {
      file_removed_diag (p, top_level, diag);
      return;
    }

  st->archive_file_size = original_size = st->stat.st_size;
  st->atime = get_stat_atime (&st->stat);
  st->mtime = get_stat_mtime (&st->stat);
  st->ctime = original_ctime = get_stat_ctime (&st->stat);

#ifdef S_ISHIDDEN
  if (S_ISHIDDEN (st->stat.st_mode))
    {
      char *new = (char *) alloca (strlen (p) + 2);
      if (new)
	{
	  strcpy (new, p);
	  strcat (new, "@");
	  p = new;
	}
    }
#endif

  /* See if we want only new files, and check if this one is too old to
     put in the archive.

     This check is omitted if incremental_option is set *and* the
     requested file is not explicitly listed in the command line.  */

  if (! (incremental_option && ! top_level)
      && !S_ISDIR (st->stat.st_mode)
      && OLDER_TAR_STAT_TIME (*st, m)
      && (!after_date_option || OLDER_TAR_STAT_TIME (*st, c)))
    {
      if (!incremental_option && verbose_option)
	WARNOPT (WARN_FILE_UNCHANGED,
		 (0, 0, _("%s: file is unchanged; not dumped"),
		  quotearg_colon (p)));
      return;
    }

  /* See if we are trying to dump the archive.  */
  if (sys_file_is_archive (st))
    {
      WARNOPT (WARN_IGNORE_ARCHIVE,
	       (0, 0, _("%s: file is the archive; not dumped"),
		quotearg_colon (p)));
      return;
    }

  is_dir = S_ISDIR (st->stat.st_mode) != 0;

  if (!is_dir && dump_hard_link (st))
    return;

  if (is_dir || S_ISREG (st->stat.st_mode) || S_ISCTG (st->stat.st_mode))
    {
      bool ok;
      struct stat final_stat;

      xattrs_acls_get (parentfd, name, st, 0, !is_dir);
      xattrs_selinux_get (parentfd, name, st, fd);
      xattrs_xattrs_get (parentfd, name, st, fd);

      if (is_dir)
	{
	  const char *tag_file_name;
	  ensure_slash (&st->orig_file_name);
	  ensure_slash (&st->file_name);

	  if (check_exclusion_tags (st, &tag_file_name) == exclusion_tag_all)
	    {
	      exclusion_tag_warning (st->orig_file_name, tag_file_name,
				     _("directory not dumped"));
	      return;
	    }

	  ok = dump_dir (st);

	  fd = st->fd;
	  parentfd = top_level ? chdir_fd : parent->fd;
	}
      else
	{
	  enum dump_status status;

	  if (fd && sparse_option && ST_IS_SPARSE (st->stat))
	    {
	      status = sparse_dump_file (fd, st);
	      if (status == dump_status_not_implemented)
		status = dump_regular_file (fd, st);
	    }
	  else
	    status = dump_regular_file (fd, st);

	  switch (status)
	    {
	    case dump_status_ok:
	    case dump_status_short:
	      file_count_links (st);
	      break;

	    case dump_status_fail:
	      break;

	    case dump_status_not_implemented:
	      abort ();
	    }

	  ok = status == dump_status_ok;
	}

      if (ok)
	{
	  if (fd < 0)
	    {
	      errno = - fd;
	      ok = false;
	    }
	  else if (fd == 0)
	    {
	      if (parentfd < 0 && ! top_level)
		{
		  errno = - parentfd;
		  ok = false;
		}
	      else
		ok = fstatat (parentfd, name, &final_stat, fstatat_flags) == 0;
	    }
	  else
	    ok = fstat (fd, &final_stat) == 0;

	  if (! ok)
	    file_removed_diag (p, top_level, stat_diag);
	}

      if (ok)
	{
	  if ((timespec_cmp (get_stat_ctime (&final_stat), original_ctime) != 0
	       /* Original ctime will change if the file is a directory and
		  --remove-files is given */
	       && !(remove_files_option && is_dir))
	      || original_size < final_stat.st_size)
	    {
	      WARNOPT (WARN_FILE_CHANGED,
		       (0, 0, _("%s: file changed as we read it"),
			quotearg_colon (p)));
	      set_exit_status (TAREXIT_DIFFERS);
	    }
	  else if (atime_preserve_option == replace_atime_preserve
		   && fd && (is_dir || original_size != 0)
		   && set_file_atime (fd, parentfd, name, st->atime) != 0)
	    utime_error (p);
	}

      ok &= tar_stat_close (st);
      if (ok && remove_files_option)
	queue_deferred_unlink (p, is_dir);

      return;
    }
#ifdef HAVE_READLINK
  else if (S_ISLNK (st->stat.st_mode))
    {
      st->link_name = areadlinkat_with_size (parentfd, name, st->stat.st_size);
      if (!st->link_name)
	{
	  if (errno == ENOMEM)
	    xalloc_die ();
	  file_removed_diag (p, top_level, readlink_diag);
	  return;
	}
      transform_name (&st->link_name, XFORM_SYMLINK);
      if (NAME_FIELD_SIZE - (archive_format == OLDGNU_FORMAT)
	  < strlen (st->link_name))
	write_long_link (st);

      xattrs_selinux_get (parentfd, name, st, 0);
      xattrs_xattrs_get (parentfd, name, st, 0);

      block_ordinal = current_block_ordinal ();
      st->stat.st_size = 0;	/* force 0 size on symlink */
      header = start_header (st);
      if (!header)
	return;
      tar_copy_str (header->header.linkname, st->link_name, NAME_FIELD_SIZE);
      header->header.typeflag = SYMTYPE;
      finish_header (st, header, block_ordinal);
      /* nothing more to do to it */

      if (remove_files_option)
	queue_deferred_unlink (p, false);

      file_count_links (st);
      return;
    }
#endif
  else if (S_ISCHR (st->stat.st_mode))
    {
      type = CHRTYPE;
      xattrs_acls_get (parentfd, name, st, 0, true);
      xattrs_selinux_get (parentfd, name, st, 0);
      xattrs_xattrs_get (parentfd, name, st, 0);
    }
  else if (S_ISBLK (st->stat.st_mode))
    {
      type = BLKTYPE;
      xattrs_acls_get (parentfd, name, st, 0, true);
      xattrs_selinux_get (parentfd, name, st, 0);
      xattrs_xattrs_get (parentfd, name, st, 0);
    }
  else if (S_ISFIFO (st->stat.st_mode))
    {
      type = FIFOTYPE;
      xattrs_acls_get (parentfd, name, st, 0, true);
      xattrs_selinux_get (parentfd, name, st, 0);
      xattrs_xattrs_get (parentfd, name, st, 0);
    }
  else if (S_ISSOCK (st->stat.st_mode))
    {
      WARNOPT (WARN_FILE_IGNORED,
	       (0, 0, _("%s: socket ignored"), quotearg_colon (p)));
      return;
    }
  else if (S_ISDOOR (st->stat.st_mode))
    {
      WARNOPT (WARN_FILE_IGNORED,
	       (0, 0, _("%s: door ignored"), quotearg_colon (p)));
      return;
    }
  else
    {
      unknown_file_error (p);
      return;
    }

  if (archive_format == V7_FORMAT)
    {
      unknown_file_error (p);
      return;
    }

  block_ordinal = current_block_ordinal ();
  st->stat.st_size = 0;	/* force 0 size */
  header = start_header (st);
  if (!header)
    return;
  header->header.typeflag = type;

  if (type != FIFOTYPE)
    {
      MAJOR_TO_CHARS (major (st->stat.st_rdev),
		      header->header.devmajor);
      MINOR_TO_CHARS (minor (st->stat.st_rdev),
		      header->header.devminor);
    }

  finish_header (st, header, block_ordinal);
  if (remove_files_option)
    queue_deferred_unlink (p, false);
}

/* Dump a file, recursively.  PARENT describes the file's parent
   directory, NAME is the file's name relative to PARENT, and FULLNAME
   its full name, possibly relative to the working directory.  NAME
   may contain slashes at the top level of invocation.  */

void
dump_file (struct tar_stat_info *parent, char const *name,
	   char const *fullname)
{
  struct tar_stat_info st;
  tar_stat_init (&st);
  st.parent = parent;
  dump_file0 (&st, name, fullname);
  if (parent && listed_incremental_option)
    update_parent_directory (parent);
  tar_stat_destroy (&st);
}
