rmtdir=$(DEFAULT_RMT_DIR)
rmt_PROGRAMS = @PU_RMT_PROG@
EXTRA_PROGRAMS = rmt

rmt_SOURCES = rmt.c

AM_CPPFLAGS = -I$(top_srcdir)/gnu -I../ -I../gnu -I$(top_srcdir)/lib

LDADD = ../gnu/libgnu.a $(LIBINTL)

rmt_LDADD = $(LDADD) $(LIB_SETSOCKOPT)

rmt.o: ../gnu/configmake.h
