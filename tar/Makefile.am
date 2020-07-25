# Main Makefile for GNU tar.

# Copyright 1994-1997, 1999-2001, 2003, 2007, 2009, 2013-2014, 2016-2017
# Free Software Foundation, Inc.

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

ACLOCAL_AMFLAGS = -I m4
EXTRA_DIST = ChangeLog.1 Make.rules
SUBDIRS = doc gnu lib rmt src scripts po tests

dist-hook:
	$(MAKE) changelog_dir=$(distdir) ChangeLog
	-rm -f $(distdir).cpio
	find $(distdir) | cpio -Hcrc -o | \
          eval GZIP= gzip $(GZIP_ENV) -c > $(distdir).cpio.gz

distclean-local:
	-rm -f $(distdir).cpio.gz

include Make.rules

gen_start_date = 2009-03-06
prev_change_log = ChangeLog.CVS
changelog_dir = .
changelog_amend_file=ChangeLog.amend
