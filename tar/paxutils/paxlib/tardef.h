/* This file is part of GNU paxutils

   Copyright (C) 2005, 2007 Free Software Foundation, Inc.

   Written by Sergey Poznyakoff

   GNU paxutils is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 3, or (at your option) any later
   version.

   GNU paxutils program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
   Public License for more details.

   You should have received a copy of the GNU General Public License along
   with GNU paxutils; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

struct tar_stat_info
{
  char *orig_file_name;     /* name of file read from the archive header */
  char *file_name;          /* name of file for the current archive entry
			       after being normalized.  */
  int had_trailing_slash;   /* nonzero if the current archive entry had a
			       trailing slash before it was normalized. */
  char *link_name;          /* name of link for the current archive entry.  */

  unsigned int  devminor;   /* device minor number */
  unsigned int  devmajor;   /* device major number */
  char          *uname;     /* user name of owner */
  char          *gname;     /* group name of owner */
  struct stat   stat;       /* regular filesystem stat */

  /* Nanosecond parts of file timestamps (if available) */
  unsigned long atime_nsec;
  unsigned long mtime_nsec;
  unsigned long ctime_nsec;
  
  off_t archive_file_size;  /* Size of file as stored in the archive.
			       Equals stat.st_size for non-sparse files */

  bool   is_sparse;         /* Is the file sparse */ 
  
  size_t sparse_map_avail;  /* Index to the first unused element in
			       sparse_map array. Zero if the file is
			       not sparse */
  size_t sparse_map_size;   /* Size of the sparse map */
  struct sp_array *sparse_map; 
};

void tar_archive_create (paxbuf_t *pbuf, const char *filename,
			 int mode, size_t bfactor);
