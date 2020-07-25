/* This file is part of GNU tar.
   Copyright 2007, 2009, 2013-2014, 2016-2017 Free Software Foundation,
   Inc.

   Written by Sergey Poznyakoff.

   GNU tar is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 3, or (at your option) any later
   version.

   GNU tar is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
   Public License for more details.

   You should have received a copy of the GNU General Public License along
   with GNU tar.  If not, see <http://www.gnu.org/licenses/>.  */

#include <system.h>
#include "common.h"

struct compression_suffix
{
  const char *suffix;
  size_t length;
  const char *program;
};

static struct compression_suffix compression_suffixes[] = {
#define __CAT2__(a,b) a ## b
#define S(s,p) #s, sizeof (#s) - 1, __CAT2__(p,_PROGRAM)
  { "tar", 3, NULL },
  { S(gz,   GZIP) },
  { S(tgz,  GZIP) },
  { S(taz,  GZIP) },
  { S(Z,    COMPRESS) },
  { S(taZ,  COMPRESS) },
  { S(bz2,  BZIP2) },
  { S(tbz,  BZIP2) },
  { S(tbz2, BZIP2) },
  { S(tz2,  BZIP2) },
  { S(lz,   LZIP) },
  { S(lzma, LZMA) },
  { S(tlz,  LZMA) },
  { S(lzo,  LZOP) },
  { S(xz,   XZ) },
  { S(txz,  XZ) }, /* Slackware */
  { S(zst,  ZSTD) },
  { S(tzst, ZSTD) },
  { NULL }
#undef S
#undef __CAT2__
};

static struct compression_suffix const *
find_compression_suffix (const char *name, size_t *ret_len)
{
  char *suf = strrchr (name, '.');

  if (suf)
    {
      size_t len;
      struct compression_suffix *p;

      suf++;
      len = strlen (suf);

      for (p = compression_suffixes; p->suffix; p++)
	{
	  if (p->length == len && memcmp (p->suffix, suf, len) == 0)
	    {
	      if (ret_len)
		*ret_len = strlen (name) - len - 1;
	      return p;
	    }
	}
    }
  return NULL;
}

static const char *
find_compression_program (const char *name, const char *defprog)
{
  struct compression_suffix const *p = find_compression_suffix (name, NULL);
  if (p)
    return p->program;
  return defprog;
}

void
set_compression_program_by_suffix (const char *name, const char *defprog)
{
  const char *program = find_compression_program (name, defprog);
  if (program)
    use_compress_program_option = program;
}

char *
strip_compression_suffix (const char *name)
{
  char *s = NULL;
  size_t len;
  struct compression_suffix const *p = find_compression_suffix (name, &len);

  if (p)
    {
      /* Strip an additional ".tar" suffix, but only if the just-stripped
	 "outer" suffix did not begin with "t".  */
      if (len > 4 && strncmp (name + len - 4, ".tar", 4) == 0
	  && p->suffix[0] != 't')
	len -= 4;
      if (len == 0)
	return NULL;
      s = xmalloc (len + 1);
      memcpy (s, name, len);
      s[len] = 0;
    }
  return s;
}
