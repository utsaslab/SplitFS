# Makefile for GNU tar library.			-*- Makefile -*-

# Copyright 1994-1997, 1999-2001, 2003-2007, 2009-2010, 2013-2014,
# 2016-2017 Free Software Foundation, Inc.

# This file is part of GNU tar.

# GNU tar is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.

# GNU tar is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

noinst_LIBRARIES=libtar.a
rmt-command.h : Makefile
	$(AM_V_GEN)rm -f $@-t $@
	$(AM_V_at)echo "#ifndef DEFAULT_RMT_COMMAND" >> $@-t
	$(AM_V_at)echo "# define DEFAULT_RMT_COMMAND \"$(DEFAULT_RMT_DIR)/`echo rmt | sed '$(transform)'`$(EXEEXT)\"" >> $@-t
	$(AM_V_at)echo "#endif" >> $@-t
	$(AM_V_at)mv $@-t $@
BUILT_SOURCES = rmt-command.h
CLEANFILES = rmt-command.h rmt-command.h-t
AM_CPPFLAGS = -I$(top_srcdir)/gnu -I../ -I../gnu
AM_CFLAGS = $(GNULIB_WARN_CFLAGS) $(WERROR_CFLAGS)

noinst_HEADERS = \
 paxlib.h\
 rmt.h\
 stdopen.h\
 system.h\
 system-ioctl.h\
 wordsplit.h\
 xattr-at.h

libtar_a_SOURCES = \
  paxerror.c paxexit-status.c paxlib.h paxnames.c \
  rtapelib.c \
  rmt.h \
  stdopen.c stdopen.h \
  system.h system-ioctl.h \
  wordsplit.c\
  xattr-at.c

if !TAR_COND_XATTR_H
BUILT_SOURCES += attr/xattr.h
attr/xattr.h: attr-xattr.in.h $(top_builddir)/config.status
	$(AM_V_at)$(MKDIR_P) attr
	$(AM_V_GEN)rm -f $@-t $@ && \
	 cp $(srcdir)/attr-xattr.in.h attr/xattr.h
endif

MOSTLYCLEANFILES = attr/xattr.h

EXTRA_DIST = attr-xattr.in.h
