/* xsparse - expands compressed sparse file images extracted from GNU tar
   archives.

   Copyright 2006-2007, 2010, 2013-2014, 2016-2017 Free Software
   Foundation, Inc.

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

   Written by Sergey Poznyakoff  */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>

/* Bound on length of the string representing an off_t.
   See INT_STRLEN_BOUND in intprops.h for explanation */
#define OFF_T_STRLEN_BOUND ((sizeof (off_t) * CHAR_BIT) * 146 / 485 + 1)
#define OFF_T_STRSIZE_BOUND (OFF_T_STRLEN_BOUND+1)

#define BLOCKSIZE 512

struct sp_array
{
  off_t offset;
  off_t numbytes;
};

char *progname;
int verbose;

void
die (int code, char *fmt, ...)
{
  va_list ap;

  fprintf (stderr, "%s: ", progname);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fprintf (stderr, "\n");
  exit (code);
}

void *
emalloc (size_t size)
{
  char *p = malloc (size);
  if (!p)
    die (1, "not enough memory");
  return p;
}

off_t
string_to_off (char *p, char **endp)
{
  off_t v = 0;

  for (; *p; p++)
    {
      int digit = *p - '0';
      off_t x = v * 10;
      if (9 < (unsigned) digit)
	{
	  if (endp)
	    {
	      *endp = p;
	      break;
	    }
	  die (1, "number parse error near %s", p);
	}
      else if (x / 10 != v)
	die (1, "number out of allowed range, near %s", p);
      v = x + digit;
      if (v < 0)
	die (1, "negative number");
    }
  if (endp)
    *endp = p;
  return v;
}

size_t
string_to_size (char *p, char **endp)
{
  off_t v = string_to_off (p, endp);
  size_t ret = v;
  if (ret != v)
    die (1, "number too big");
  return ret;
}

size_t sparse_map_size;
struct sp_array *sparse_map;

void
get_line (char *s, int size, FILE *stream)
{
  char *p = fgets (s, size, stream);
  size_t len;

  if (!p)
    die (1, "unexpected end of file");
  len = strlen (p);
  if (s[len - 1] != '\n')
    die (1, "buffer overflow");
  s[len - 1] = 0;
}

int
get_var (FILE *fp, char **name, char **value)
{
  static char *buffer;
  static size_t bufsize = OFF_T_STRSIZE_BOUND;
  char *p, *q;

  buffer = emalloc (bufsize);
  do
    {
      size_t len, s;

      if (!fgets (buffer, bufsize, fp))
	return 0;
      len = strlen (buffer);
      if (len == 0)
	return 0;

      s = string_to_size (buffer, &p);
      if (*p != ' ')
	die (1, "malformed header: expected space but found %s", p);
      if (buffer[len-1] != '\n')
	{
	  if (bufsize < s + 1)
	    {
	      bufsize = s + 1;
	      buffer = realloc (buffer, bufsize);
	      if (!buffer)
		die (1, "not enough memory");
	    }
	  if (!fgets (buffer + len, s - len + 1, fp))
	    die (1, "unexpected end of file or read error");
	}
      p++;
    }
  while (memcmp (p, "GNU.sparse.", 11));

  p += 11;
  q = strchr (p, '=');
  if (!q)
    die (1, "malformed header: expected '=' not found");
  *q++ = 0;
  q[strlen (q) - 1] = 0;
  *name = p;
  *value = q;
  return 1;
}

char *outname;
off_t outsize;
unsigned version_major;
unsigned version_minor;

void
read_xheader (char *name)
{
  char *kw, *val;
  FILE *fp = fopen (name, "r");
  char *expect = NULL;
  size_t i = 0;

  if (verbose)
    printf ("Reading extended header file\n");

  while (get_var (fp, &kw, &val))
    {
      if (verbose)
	printf ("Found variable GNU.sparse.%s = %s\n", kw, val);

      if (expect && strcmp (kw, expect))
	die (1, "bad keyword sequence: expected '%s' but found '%s'",
	     expect, kw);
      expect = NULL;
      if (strcmp (kw, "name") == 0)
	{
	  outname = emalloc (strlen (val) + 1);
	  strcpy (outname, val);
	}
      else if (strcmp (kw, "major") == 0)
	{
	  version_major = string_to_size (val, NULL);
	}
      else if (strcmp (kw, "minor") == 0)
	{
	  version_minor = string_to_size (val, NULL);
	}
      else if (strcmp (kw, "realsize") == 0
	       || strcmp (kw, "size") == 0)
	{
	  outsize = string_to_off (val, NULL);
	}
      else if (strcmp (kw, "numblocks") == 0)
	{
	  sparse_map_size = string_to_size (val, NULL);
	  sparse_map = emalloc (sparse_map_size * sizeof *sparse_map);
	}
      else if (strcmp (kw, "offset") == 0)
	{
	  sparse_map[i].offset = string_to_off (val, NULL);
	  expect = "numbytes";
	}
      else if (strcmp (kw, "numbytes") == 0)
	{
	  sparse_map[i++].numbytes = string_to_off (val, NULL);
	}
      else if (strcmp (kw, "map") == 0)
	{
	  for (i = 0; i < sparse_map_size; i++)
	    {
	      sparse_map[i].offset = string_to_off (val, &val);
	      if (*val != ',')
		die (1, "bad GNU.sparse.map: expected ',' but found '%c'",
		     *val);
	      sparse_map[i].numbytes = string_to_off (val+1, &val);
	      if (*val != ',')
		{
		  if (!(*val == 0 && i == sparse_map_size-1))
		    die (1, "bad GNU.sparse.map: expected ',' but found '%c'",
			 *val);
		}
	      else
		val++;
	    }
	  if (*val)
	    die (1, "bad GNU.sparse.map: garbage at the end");
	}
    }
  if (expect)
    die (1, "bad keyword sequence: expected '%s' not found", expect);
  if (version_major == 0 && sparse_map_size == 0)
    die (1, "size of the sparse map unknown");
  if (i != sparse_map_size)
    die (1, "not all sparse entries supplied");
  fclose (fp);
}

void
read_map (FILE *ifp)
{
  size_t i;
  char nbuf[OFF_T_STRSIZE_BOUND];

  if (verbose)
    printf ("Reading v.1.0 sparse map\n");

  get_line (nbuf, sizeof nbuf, ifp);
  sparse_map_size = string_to_size (nbuf, NULL);
  sparse_map = emalloc (sparse_map_size * sizeof *sparse_map);

  for (i = 0; i < sparse_map_size; i++)
    {
      get_line (nbuf, sizeof nbuf, ifp);
      sparse_map[i].offset = string_to_off (nbuf, NULL);
      get_line (nbuf, sizeof nbuf, ifp);
      sparse_map[i].numbytes = string_to_off (nbuf, NULL);
    }

  fseeko (ifp, ((ftell (ifp) + BLOCKSIZE - 1) / BLOCKSIZE) * BLOCKSIZE,
	  SEEK_SET);
}

void
expand_sparse (FILE *sfp, int ofd)
{
  size_t i;
  off_t max_numbytes = 0;
  size_t maxbytes;
  char *buffer;

  for (i = 0; i < sparse_map_size; i++)
    if (max_numbytes < sparse_map[i].numbytes)
      max_numbytes = sparse_map[i].numbytes;

  maxbytes = max_numbytes < SIZE_MAX ? max_numbytes : SIZE_MAX;

  for (buffer = malloc (maxbytes); !buffer; maxbytes /= 2)
    if (maxbytes == 0)
      die (1, "not enough memory");

  for (i = 0; i < sparse_map_size; i++)
    {
      off_t size = sparse_map[i].numbytes;

      if (size == 0)
	ftruncate (ofd, sparse_map[i].offset);
      else
	{
	  lseek (ofd, sparse_map[i].offset, SEEK_SET);
	  while (size)
	    {
	      size_t rdsize = (size < maxbytes) ? size : maxbytes;
	      if (rdsize != fread (buffer, 1, rdsize, sfp))
		die (1, "read error (%d)", errno);
	      if (rdsize != write (ofd, buffer, rdsize))
		die (1, "write error (%d)", errno);
	      size -= rdsize;
	    }
	}
    }
  free (buffer);
}

void
usage (int code)
{
  printf ("Usage: %s [OPTIONS] infile [outfile]\n", progname);
  printf ("%s: expand sparse files extracted from GNU archives\n",
	  progname);
  printf ("\nOPTIONS are:\n\n");
  printf ("  -h           Display this help list\n");
  printf ("  -n           Dry run: do nothing, print what would have been done\n");
  printf ("  -v           Increase verbosity level\n");
  printf ("  -x FILE      Parse extended header FILE\n\n");

  exit (code);
}

void
guess_outname (char *name)
{
  char *p;
  char *s;

  if (name[0] == '.' && name[1] == '/')
    name += 2;

  p = name + strlen (name) - 1;
  s = NULL;

  for (; p > name && *p != '/'; p--)
    ;
  if (*p == '/')
    s = p + 1;
  if (p != name)
    {
      for (p--; p > name && *p != '/'; p--)
	;
    }

  if (*p != '/')
    {
      if (s)
	outname = s;
      else
	{
	  outname = emalloc (4 + strlen (name));
	  strcpy (outname, "../");
	  strcpy (outname + 3, name);
	}
    }
  else
    {
      size_t len = p - name + 1;
      outname = emalloc (len + strlen (s) + 1);
      memcpy (outname, name, len);
      strcpy (outname + len, s);
    }
}

int
main (int argc, char **argv)
{
  int c;
  int dry_run = 0;
  char *xheader_file = NULL;
  char *inname;
  FILE *ifp;
  struct stat st;
  int ofd;

  progname = argv[0];
  while ((c = getopt (argc, argv, "hnvx:")) != EOF)
    {
      switch (c)
	{
	case 'h':
	  usage (0);
	  break;

	case 'x':
	  xheader_file = optarg;
	  break;

	case 'n':
	  dry_run = 1;
	case 'v':
	  verbose++;
	  break;

	default:
	  exit (1);
	}
    }

  argc -= optind;
  argv += optind;

  if (argc == 0 || argc > 2)
    usage (1);

  if (xheader_file)
    read_xheader (xheader_file);

  inname = argv[0];
  if (argv[1])
    outname = argv[1];

  if (stat (inname, &st))
    die (1, "cannot stat %s (%d)", inname, errno);

  ifp = fopen (inname, "r");
  if (ifp == NULL)
    die (1, "cannot open file %s (%d)", inname, errno);

  if (!xheader_file || version_major == 1)
    read_map (ifp);

  if (!outname)
    guess_outname (inname);

  ofd = open (outname, O_RDWR|O_CREAT|O_TRUNC, st.st_mode);
  if (ofd == -1)
    die (1, "cannot open file %s (%d)", outname, errno);

  if (verbose)
    printf ("Expanding file '%s' to '%s'\n", inname, outname);

  if (dry_run)
    {
      printf ("Finished dry run\n");
      return 0;
    }

  expand_sparse (ifp, ofd);

  fclose (ifp);
  close (ofd);

  if (verbose)
    printf ("Done\n");

  if (outsize)
    {
      if (stat (outname, &st))
	die (1, "cannot stat output file %s (%d)", outname, errno);
      if (st.st_size != outsize)
	die (1, "expanded file has wrong size");
    }

  return 0;
}
