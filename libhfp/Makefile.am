# $Id: $

AM_CXXFLAGS = -Wshadow -fno-exceptions
INCLUDES = -I$(top_srcdir)/include -include config.h

noinst_LIBRARIES = libhfp.a
libhfp_a_SOURCES = bt.cpp rfcomm.cpp hfp.cpp soundio-pump.cpp \
	soundio-manager.cpp soundio-util.cpp soundio-alsa.cpp \
	soundio-oss.cpp events-indep.cpp oplatency.h
