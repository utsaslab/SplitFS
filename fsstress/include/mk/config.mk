#
#    config.mk.in.
#
#    Copyright (C) 2009, Cisco Systems Inc.
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License along
#    with this program; if not, write to the Free Software Foundation, Inc.,
#    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Ngie Cooper, July 2009
#

# See this page for more info about LEX*:
# http://www.gnu.org/software/hello/manual/autoconf/Particular-Programs.html

# Application specifying variables. You should never have to change these.
AR			:= ar
CC			:= gcc
LEX			:= flex
RANLIB			:= ranlib
STRIP			:= strip
YACC			:= bison -y

AIO_LIBS		:= 
CAP_LIBS		:= 
ACL_LIBS		:= 
CRYPTO_LIBS		:= -lcrypto
LEXLIB			:= -lfl
NUMA_LIBS		:= 
SELINUX_LIBS		:= 
HAVE_RPC		:= 1
TIRPC_CFLAGS		:= 
TIRPC_LIBS		:= 
KEYUTILS_LIBS		:= 
HAVE_FTS_H		:= 1
LIBMNL_LIBS		:= 
LIBMNL_CFLAGS		:= 

prefix			:= /opt/ltp

datarootdir		:= ${prefix}/share
includedir		:= ${prefix}/include
exec_prefix		:= ${prefix}
bindir			:= ${exec_prefix}/bin
libdir			:= ${exec_prefix}/lib
mandir			:= ${datarootdir}/man

CPPFLAGS		:= 
CFLAGS			:= -g -O2
LDLIBS			:= 
LDFLAGS			:= 

DEBUG_CFLAGS		?= -g

# for -fstrict-aliasing see doc/build-system-guide.txt
OPT_CFLAGS		?= -O2 -fno-strict-aliasing -pipe

WCFLAGS			?= -Wall -W -Wold-style-definition

LDFLAGS			+= $(WLDFLAGS)
CFLAGS			+= $(DEBUG_CFLAGS) $(OPT_CFLAGS) $(WCFLAGS)

LINUX_VERSION		:= 4.13.0
LINUX_DIR		:= /lib/modules/4.13.0/build
LINUX_VERSION_MAJOR	:= 4
LINUX_VERSION_PATCH	:= 13
WITH_MODULES		:= yes

HOST_CPU		:= x86_64

ifeq ($(strip $(prefix)),)
$(error you are using $$(prefix) incorrectly -- set it to $(abs_top_srcdir) if you want to build in the source tree)
endif

export datarootdir includedir libdir mandir prefix
