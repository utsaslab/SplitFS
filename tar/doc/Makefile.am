# Makefile for GNU tar documentation.

# Copyright 1994-1997, 1999-2001, 2003, 2006-2007, 2013-2014, 2016-2017
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

info_TEXINFOS = tar.texi
tar_TEXINFOS = \
 dumpdir.texi\
 tar-snapshot-edit.texi\
 fdl.texi\
 freemanuals.texi\
 genfile.texi\
 header.texi\
 intern.texi\
 parse-datetime.texi\
 recipes.texi\
 rendition.texi\
 snapshot.texi\
 sparse.texi\
 value.texi

dist_man_MANS=tar.1 $(RMT_8)
if PU_RMT_COND
  RMT_8=rmt.8
endif

EXTRA_DIST = gendocs_template mastermenu.el texify.sed untabify.el rmt.8

# The rendering level is anyone of PUBLISH, DISTRIB or PROOF.
# Just call 'make RENDITION=PROOF [target]' if you want PROOF rendition.
RENDITION = DISTRIB

MAKEINFOFLAGS=-D$(RENDITION)

header.texi: $(top_srcdir)/src/tar.h
	sed -f $(srcdir)/texify.sed $(top_srcdir)/src/tar.h \
	  | expand >$@

master-menu: $(tar_TEXINFOS)
	emacs -batch -l mastermenu.el -f make-master-menu $(info_TEXINFOS)

untabify:
	emacs -batch -l untabify.el $(info_TEXINFOS) $(tar_TEXINFOS)

final: untabify master-menu

# Checking
check-format:
	@if test -n "`cat $(info_TEXINFOS) $(tar_TEXINFOS) | tr -d -c '\t'`"; then \
		echo "Sources contain tabs; run make untabify"; \
		false; \
	fi

check-options:
	@ARGP_HELP_FMT='usage-indent=0,short-opt-col=0,long-opt-col=0,doc-opt-col=0,opt-doc-col=0,header-col=0,rmargin=1' \
	$(top_builddir)/src/tar --usage | \
	sed -n 's/^\[--\([^]\=\[]*\).*/\1/p' | sort | uniq > opts.$$$$;\
	$(MAKEINFO) $(AM_MAKEINFOFLAGS) $(MAKEINFOFLAGS) -I $(srcdir) -E - \
            $(info_TEXINFOS) | \
	sed -n '/^@macro/,/^@end macro/d;s/@opindex *\([^@,]*\).*/\1/p' \
	     | sort | uniq > docs.$$$$;\
	status=0;\
	join -v1 opts.$$$$ docs.$$$$ > report.$$$$;\
	if test -s report.$$$$; then \
          echo 'Not documented options:'; \
          cat report.$$$$; \
          status=1; \
        fi; \
	join -v2 opts.$$$$ docs.$$$$ > report.$$$$;\
	if test -s report.$$$$; then \
	  echo 'Non-existing options:';\
          cat report.$$$$; \
          status=1; \
        fi; \
	rm opts.$$$$ docs.$$$$ report.$$$$;\
	test $$status -ne 0 && exit $$status

check-refs:
	@for file in $(info_TEXINFOS) $(tar_TEXINFOS); \
	do \
	  sed -e = $$file | \
           sed -n 'N;/@FIXME-.*ref/{s/\(^[0-9][0-9]*\).*@FIXME-.*ref{\([^}]*\).*/'$$file':\1: \2/gp}'; \
	done > $@-t; \
	if [ -s $@-t ]; then \
	  echo "Unresolved cross-references:"; \
	  cat $@-t;\
	  rm $@-t; \
	else \
	  rm -f $@-t; \
	fi

check-fixmes:
	@for file in $(info_TEXINFOS); \
	do \
	  sed -e = $$file | \
           sed -n 'N;/@FIXME{/{s/\(^[0-9][0-9]*\).*@FIXME{\([^}]*\).*/'$$file':\1: \2/gp}'; \
	done > $@-t; \
	if [ -s $@-t ]; then \
	  echo "Unresolved FIXMEs:"; \
	  cat $@-t; \
	  rm $@-t; \
	  false; \
	else \
          rm -f $@-t; \
	fi

check-unrevised:
	@grep -Hn @UNREVISED $(info_TEXINFOS) > $@-t; \
	if [ -s $@-t ]; then \
	  echo "Unrevised nodes:"; \
	  cat $@-t; \
	  rm $@-t; \
	  false;\
	else \
          rm $@-t; \
	fi

all-check-docs: check-format check-options check-refs check-fixmes check-unrevised

check-docs:
	$(MAKE) -k all-check-docs

#

clean-local:
	rm -rf manual

GENDOCS=gendocs.sh

TEXI2DVI=texi2dvi -t '@set $(RENDITION)' -E

# Make sure you set TEXINPUTS
# Usual value is:
# /usr/share/texmf/pdftex/plain/misc:/usr/share/texmf/pdftex/config
manual:
	TEXINPUTS=$(srcdir):$(top_srcdir)/build-tex:$(TEXINPUTS) \
	 MAKEINFO="$(MAKEINFO) $(MAKEINFOFLAGS)" \
	 TEXI2DVI="$(TEXI2DVI) -t @finalout" \
	$(GENDOCS) --texi2html tar 'GNU tar manual'
