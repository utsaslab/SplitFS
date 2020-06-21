/* Check if filesystem timestamps are consistent with the system time.
   Copyright (C) 2016-2017 Free Software Foundation, Inc.
      
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
*/
#include <config.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stat-time.h>
#include <timespec.h>

#define TEMPLATE "ckmtime.XXXXXX"
#define BILLION 1000000000

/* Some filesystems can slightly offset the timestamps of newly created files.
   To compensate for it, tar testsuite waits at least 1 second before creating
   next level of incremental backups.

   However, NFS mounts can offset the timestamps by bigger amounts.  
   
   This program returns with success (0) if a newly created file is assigned
   mtime matching the system time to the nearest second.
*/
int
main (int argc, char **argv)
{
  int fd;
  char name[sizeof(TEMPLATE)];
  struct stat st;
  struct timespec ts, td;
  double diff;
  
  gettime (&ts);
  
  strcpy (name, TEMPLATE);
  umask (077);
  fd = mkstemp (name);
  assert (fd != -1);
  unlink (name);
  assert (fstat (fd, &st) == 0);
  close (fd);

  td = timespec_sub (get_stat_mtime (&st), ts);
  diff = td.tv_sec * BILLION + td.tv_nsec;
  if (diff < 0)
    diff = - diff;
  if (diff / BILLION >= 1)
    {
      fprintf (stderr, "file timestamp unreliable\n");
      return 1;
    }
  return 0;
}
