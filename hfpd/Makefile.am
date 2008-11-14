# $Id: $

AM_CXXFLAGS = -Wshadow -fno-exceptions
INCLUDES = -I$(top_srcdir)/include -include config.h $(DBUS_CFLAGS)

bin_PROGRAMS = hfpd
EXTRA_PROGRAMS = hfpdtext

hfpd_SOURCES = hfpd.cpp dbus.cpp util.cpp configfile.cpp objects.cpp \
		configfile.h util.h dbus.h dbus-marshall.h proto.h \
		objects.h
hfpd_LDADD = -L../libhfp -lhfp $(libhfp_LIBS) $(DBUS_LIBS)
hfpd_DEPENDENCIES = ../libhfp/libhfp.a

hfpdtext_SOURCES = hfpdtext.cpp net.cpp util.cpp configfile.cpp \
		configfile.h util.h net.h
hfpdtext_LDADD = -L../libhfp -lhfp $(libhfp_LIBS)
hfpdtext_DEPENDENCIES = ../libhfp/libhfp.a
CLEANFILES = hfpdtext
