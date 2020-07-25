/* Support for extended attributes.

   Copyright (C) 2006-2014, 2016-2017 Free Software Foundation, Inc.

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

   Written by James Antill, on 2006-07-27.  */

#ifndef GUARD_XATTTRS_H
#define GUARD_XATTTRS_H

/* Add include/exclude fnmatch pattern for xattr key domain.  Set INCL parameter
   to true/false if you want to add include/exclude pattern */
extern void xattrs_mask_add (const char *mask, bool incl);

/* clear helping structures when tar finishes */
extern void xattrs_clear_setup (void);

extern void xattrs_acls_get (int parentfd, char const *file_name,
                             struct tar_stat_info *st, int fd, int xisfile);
extern void xattrs_selinux_get (int parentfd, char const *file_name,
                                struct tar_stat_info *st, int fd);
extern void xattrs_xattrs_get (int parentfd, char const *file_name,
                               struct tar_stat_info *st, int fd);

extern void xattrs_acls_set (struct tar_stat_info const *st,
                             char const *file_name, char typeflag);
extern void xattrs_selinux_set (struct tar_stat_info const *st,
                                char const *file_name, char typeflag);
extern void xattrs_xattrs_set (struct tar_stat_info const *st,
                               char const *file_name, char typeflag,
                               int later_run);

extern void xattrs_print_char (struct tar_stat_info const *st, char *output);
extern void xattrs_print (struct tar_stat_info const *st);

#endif /* GUARD_XATTTRS_H */
