/* openat-style fd-relative functions for operating with extended file
   attributes.

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

#include <config.h>

#include "xattr-at.h"
#include "openat.h"

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "dirname.h" /* solely for definition of IS_ABSOLUTE_FILE_NAME */
#include "save-cwd.h"

#include "openat-priv.h"

#ifdef HAVE_XATTRS

/* setxattrat */
#define AT_FUNC_NAME setxattrat
#define AT_FUNC_F1 setxattr
#define AT_FUNC_POST_FILE_PARAM_DECLS   , const char *name, const void *value \
                                        , size_t size, int flags
#define AT_FUNC_POST_FILE_ARGS          , name, value, size, flags
#include "at-func.c"
#undef AT_FUNC_NAME
#undef AT_FUNC_F1
#undef AT_FUNC_POST_FILE_PARAM_DECLS
#undef AT_FUNC_POST_FILE_ARGS

/* lsetxattrat */
#define AT_FUNC_NAME lsetxattrat
#define AT_FUNC_F1 lsetxattr
#define AT_FUNC_POST_FILE_PARAM_DECLS   , const char *name, const void *value \
                                        , size_t size, int flags
#define AT_FUNC_POST_FILE_ARGS          , name, value, size, flags
#include "at-func.c"
#undef AT_FUNC_NAME
#undef AT_FUNC_F1
#undef AT_FUNC_POST_FILE_PARAM_DECLS
#undef AT_FUNC_POST_FILE_ARGS

/* getxattrat */
#define AT_FUNC_NAME getxattrat
#define AT_FUNC_RESULT ssize_t
#define AT_FUNC_F1 getxattr
#define AT_FUNC_POST_FILE_PARAM_DECLS   , const char *name, void *value \
                                        , size_t size
#define AT_FUNC_POST_FILE_ARGS          , name, value, size
#include "at-func.c"
#undef AT_FUNC_NAME
#undef AT_FUNC_F1
#undef AT_FUNC_RESULT
#undef AT_FUNC_POST_FILE_PARAM_DECLS
#undef AT_FUNC_POST_FILE_ARGS

/* lgetxattrat */
#define AT_FUNC_NAME lgetxattrat
#define AT_FUNC_RESULT ssize_t
#define AT_FUNC_F1 lgetxattr
#define AT_FUNC_POST_FILE_PARAM_DECLS   , const char *name, void *value \
                                        , size_t size
#define AT_FUNC_POST_FILE_ARGS          , name, value, size
#include "at-func.c"
#undef AT_FUNC_NAME
#undef AT_FUNC_F1
#undef AT_FUNC_RESULT
#undef AT_FUNC_POST_FILE_PARAM_DECLS
#undef AT_FUNC_POST_FILE_ARGS

/* listxattrat */
#define AT_FUNC_NAME listxattrat
#define AT_FUNC_RESULT ssize_t
#define AT_FUNC_F1 listxattr
#define AT_FUNC_POST_FILE_PARAM_DECLS   , char *list , size_t size
#define AT_FUNC_POST_FILE_ARGS          , list , size
#include "at-func.c"
#undef AT_FUNC_NAME
#undef AT_FUNC_F1
#undef AT_FUNC_RESULT
#undef AT_FUNC_POST_FILE_PARAM_DECLS
#undef AT_FUNC_POST_FILE_ARGS

/* llistxattrat */
#define AT_FUNC_NAME llistxattrat
#define AT_FUNC_RESULT ssize_t
#define AT_FUNC_F1 llistxattr
#define AT_FUNC_POST_FILE_PARAM_DECLS   , char *list , size_t size
#define AT_FUNC_POST_FILE_ARGS          , list , size
#include "at-func.c"
#undef AT_FUNC_NAME
#undef AT_FUNC_F1
#undef AT_FUNC_RESULT
#undef AT_FUNC_POST_FILE_PARAM_DECLS
#undef AT_FUNC_POST_FILE_ARGS

#endif /* HAVE_XATTRS */
