dnl Special Autoconf macros for GNU tar         -*- autoconf -*-

dnl Copyright 2009, 2013-2014, 2016-2017 Free Software Foundation, Inc.
dnl
dnl This file is part of GNU tar.
dnl
dnl GNU tar is free software; you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation; either version 3 of the License, or
dnl (at your option) any later version.
dnl
dnl GNU tar is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.
dnl
dnl You should have received a copy of the GNU General Public License
dnl along with this program.  If not, see <http://www.gnu.org/licenses/>.

AC_DEFUN([TAR_COMPR_PROGRAM],[
 m4_pushdef([tar_compr_define],translit($1,[a-z+-],[A-ZX_])[_PROGRAM])
 m4_pushdef([tar_compr_var],[tar_cv_compressor_]translit($1,[+-],[x_]))
 AC_ARG_WITH($1,
             AC_HELP_STRING([--with-]$1[=PROG],
	                    [use PROG as ]$1[ compressor program]),
             [tar_compr_var=${withval}],
	     [tar_compr_var=m4_if($2,,$1,$2)])
 AC_DEFINE_UNQUOTED(tar_compr_define, "$tar_compr_var",
                    [Define to the program name of ]$1[ compressor program])])

# Provide <attr/xattr.h>, if necessary

AC_DEFUN([TAR_HEADERS_ATTR_XATTR_H],
[
  AC_ARG_WITH([xattrs],
    AS_HELP_STRING([--without-xattrs], [don't use linux extended attributes]),
    [], [with_xattrs=maybe]
  )

  # First check for <sys/xattr.h>
  AC_CHECK_HEADERS([sys/xattr.h])
  AM_CONDITIONAL([TAR_COND_XATTR_H],[test "$ac_cv_header_sys_xattr_h" = yes])
  if test "$ac_cv_header_sys_xattr_h" != yes; then
    AC_CHECK_HEADERS([attr/xattr.h])
    AM_CONDITIONAL([TAR_COND_XATTR_H],[test "$ac_cv_header_attr_xattr_h" = yes])
  fi

  if test "$with_xattrs" != no; then
    for i in getxattr  fgetxattr  lgetxattr \
             setxattr  fsetxattr  lsetxattr \
             listxattr flistxattr llistxattr
    do
      AC_SEARCH_LIBS($i, attr)
      eval found=\$ac_cv_search_$i
      test "$found" = "no" && break
    done

    if test "$found" != no; then
      AC_DEFINE([HAVE_XATTRS],,[Define when we have working linux xattrs.])
    fi
  fi
])
