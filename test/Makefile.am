# $Id: $

INCLUDES = -I$(top_srcdir)/include -include config.h $(libnghost_CFLAGS)
AM_CXXFLAGS = -Wshadow

noinst_PROGRAMS = soundtest timertest

soundtest_SOURCES = soundtest.cpp
soundtest_LDADD = -L../libhfp -lhfp $(libhfp_LIBS)
soundtest_DEPENDENCIES = ../libhfp/libhfp.a

timertest_SOURCES = timertest.cpp
timertest_LDADD = -L../libhfp -lhfp $(libhfp_LIBS)
timertest_DEPENDENCIES = ../libhfp/libhfp.a
