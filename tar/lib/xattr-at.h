/* Prototypes for openat-style fd-relative functions for operating with
   extended file attributes.

   Copyright 2012-2014, 2016-2017 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef XATTRS_AT_H
#define XATTRS_AT_H

#include <sys/types.h>
#if defined(HAVE_SYS_XATTR_H)
# include <sys/xattr.h>
#elif defined(HAVE_ATTR_XATTR_H)
# include <attr/xattr.h>
#endif

#ifndef ENOATTR
# define ENOATTR ENODATA        /* No such attribute */
#endif

/* These are the dir-fd-relative variants of the functions without the
   "at" suffix.  For example, setxattrat (AT_FDCWD, path, name, value, size,
   flags &c) is usually equivalent to setxattr (file, name, value, size,
   flags).  For more info use the setxattr(2), getxattr(2) or listxattr(2)
   manpages. */

/* dir-fd-relative setxattr.  Operation sets the VALUE of the extended
   attribute identified by NAME and associated with the given PATH in the
   filesystem relatively to directory identified by DIR_FD.  See the
   setxattr(2) manpage for the description of all parameters. */
int setxattrat (int dir_fd, const char *path, const char *name,
                const void *value, size_t size, int flags);

/* dir-fd-relative lsetxattr.  This function is just like setxattrat,
   except when DIR_FD and FILE specify a symlink:  lsetxattrat operates on the
   symlink, while the setxattrat operates on the referent of the symlink.  */
int lsetxattrat (int dir_fd, const char *path, const char *name,
                 const void *value, size_t size, int flags);

/* dir-fd-relative getxattr.  Operation gets the VALUE of the extended
   attribute idenfified by NAME and associated with the given PATH in the
   filesystem relatively to directory identified by DIR_FD.  For more info
   about all parameters see the getxattr(2) manpage. */
ssize_t getxattrat (int dir_fd, const char *path, const char *name,
                    void *value, size_t size);

/* dir-fd-relative lgetxattr.  This function is just like getxattrat,
   except when DIR_FD and FILE specify a symlink:  lgetxattrat operates on the
   symlink, while the getxattrat operates on the referent of the symlink.  */
ssize_t lgetxattrat (int dir_fd, const char *path, const char *name,
                     void *value, size_t size);

/* dir-fd-relative listxattr.  Obtain the list of extended attrubtes names.  For
   more info see the listxattr(2) manpage. */
ssize_t listxattrat (int dir_fd, const char *path, char *list, size_t size);

/* dir-fd-relative llistxattr.  This function is just like listxattrat,
   except when DIR_FD and FILE specify a symlink:  llistxattr operates on the
   symlink, while the listxattrat operates on the referent of the symlink.  */
ssize_t llistxattrat (int dir_fd, const char *path, char *list, size_t size);

#endif /* XATTRS_AT_H */
