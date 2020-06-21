/* Replacement <attr/xattr.h> for platforms that lack it.
   Copyright 2012-2014, 2016-2017 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef TAR_ATTR_XATTR_H
#define TAR_ATTR_XATTR_H
#include <errno.h>
#ifndef ENOATTR
# define ENOATTR ENODATA        /* No such attribute */
#endif

/* setting */
static inline int setxattr (const char *path, const char *name, const void
                            *value, size_t size, int flags)
{ errno = ENOTSUP; return -1; }

static inline int lsetxattr (const char *path, const char *name, const void
                             *value, size_t size, int flags)
{ errno = ENOTSUP; return -1; }

static inline int fsetxattr (int filedes, const char *name, const void *value,
                             size_t size, int flags)
{ errno = ENOTSUP; return -1; }


/* getting */
static inline ssize_t getxattr (const char *path, const char *name, void *value,
                                size_t size)
{ errno = ENOTSUP; return -1; }
static inline ssize_t lgetxattr (const char *path, const char *name, void
                                 *value, size_t size)
{ errno = ENOTSUP; return -1; }
static inline ssize_t fgetxattr (int filedes, const char *name, void *value,
                                 size_t size)
{ errno = ENOTSUP; return -1; }


/* listing */
static inline ssize_t listxattr (const char *path, char *list, size_t size)
{ errno = ENOTSUP; return -1; }

static inline ssize_t llistxattr (const char *path, char *list, size_t size)
{ errno = ENOTSUP; return -1; }

static inline ssize_t flistxattr (int filedes, char *list, size_t size)
{ errno = ENOTSUP; return -1; }

#endif
