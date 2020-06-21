/* Test suite for GNU tar - SEEK_HOLE detector.

   Copyright 2015-2017 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 3, or (at your option) any later
   version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
   Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program.  If not, see <http://www.gnu.org/licenses/>.

   Description:  detect whether it is possible to work with SEEK_HOLE on
   particular operating system and file system. */

#include "config.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

enum {
    EX_OK = 0,    /* SEEK_HOLE support */
    EX_FAIL,      /* test failed - no SEEK_HOLE support */
    EX_BAD,       /* test is not relevant */
};

int
check_seek_hole (int fd)
{
#ifdef SEEK_HOLE
  struct stat stat;
  off_t offset;

  /* hole of 100MB */
  if (lseek (fd, 100*1024*1024, SEEK_END) < 0)
    return EX_BAD;

  /* piece of data */
  if (write (fd, "data\n", 5) != 5)
    return EX_BAD;

  /* another hole */
  if (lseek (fd, 100*1024*1024, SEEK_END) < 0)
    return EX_BAD;

  /* piece of data */
  if (write (fd, "data\n", 5) != 5)
    return EX_BAD;

  if (fstat (fd, &stat))
    return EX_BAD;

  offset = lseek (fd, 0, SEEK_DATA);
  if (offset == (off_t)-1)
    return EX_FAIL;

  offset = lseek (fd, offset, SEEK_HOLE);
  if (offset == (off_t)-1 || offset == stat.st_size)
    return EX_FAIL;

  return EX_OK;
#else
  return EX_BAD;
#endif
}

int
main ()
{
#ifdef SEEK_HOLE
  int rc;
  char template[] = "testseekhole-XXXXXX";
  int fd = mkstemp (template);
  if (fd == -1)
    return EX_BAD;
  rc = check_seek_hole (fd);
  close (fd);
  unlink (template);

  return rc;
#else
  return EX_FAIL;
#endif
}
