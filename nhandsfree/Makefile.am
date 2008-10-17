# $Id: $

INCLUDES = -I$(top_srcdir)/include -include config.h $(libnghost_CFLAGS)

pkglib_LTLIBRARIES = libhandsfree.la
EXTRA_PROGRAMS = soundtest

libhandsfree_la_SOURCES = nhandsfree.cpp nhandsfree.h
libhandsfree_la_LDFLAGS = $(libnghost_LIBS)

soundtest_SOURCES = soundtest.cpp
soundtest_LDADD = -L../libhfp -lhfp $(libhfp_LIBS)
soundtest_DEPENDENCIES = ../libhfp/libhfp.a

CLEANFILES = soundtest
