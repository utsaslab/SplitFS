# Make GNU tar scripts.

# Copyright 2004, 2006-2007, 2013-2014, 2016-2017 Free Software
# Foundation, Inc.

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

BACKUP_LIBEXEC_SCRIPTS_LIST=backup.sh dump-remind
BACKUP_SBIN_SCRIPTS_LIST=backup restore
libexec_SCRIPTS=@BACKUP_LIBEXEC_SCRIPTS@
AM_INSTALLCHECK_STD_OPTIONS_EXEMPT=backup.sh dump-remind
sbin_SCRIPTS=@BACKUP_SBIN_SCRIPTS@
EXTRA_SCRIPTS=tarcat
EXTRA_DIST=\
 backup.sh.in\
 backup.in\
 restore.in\
 dump-remind.in\
 backup-specs
CLEANFILES=backup.sh backup restore dump-remind

SED_CMD="s,\@libexecdir\@,$(libexecdir),;\
         s,\@sysconfdir\@,$(sysconfdir),;\
         s,\@PACKAGE_NAME\@,$(PACKAGE_NAME),;\
         s,\@VERSION\@,$(VERSION),;\
         s,\@PACKAGE_BUGREPORT\@,$(PACKAGE_BUGREPORT),;\
         s,\@DATE_FORMAT_OK\@,$(DATE_FORMAT_OK),;@BACKUP_SED_COND@"

backup.sh: $(srcdir)/backup.sh.in
	sed $(SED_CMD) $? > $@

backup: $(srcdir)/backup.in
	sed $(SED_CMD) $? > $@

restore: $(srcdir)/restore.in
	sed $(SED_CMD) $? > $@

dump-remind: $(srcdir)/dump-remind.in
	sed $(SED_CMD) $? > $@
