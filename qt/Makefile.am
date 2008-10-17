# $Id: $

INCLUDES = -I$(top_srcdir)/include -include config.h $(QT_INCLUDES)

bin_PROGRAMS = standalone

uic_files = nohands.ui prefs.ui scandialog.ui
uic_built_hdrs = $(uic_files:.ui=.h)
uic_built_srcs = $(uic_files:.ui=.cpp)
moc_inputs = nohands.h prefs.h scandialog.h events-qt.h
moc_built_srcs = $(moc_inputs:.h=.moc.cpp)
commonbuiltsrc = $(uic_built_hdrs) $(uic_built_srcs) $(moc_built_srcs)
commonsrc = events-qt.h nohands.ui.h prefs.ui.h scandialog.ui.h 

nodist_standalone_SOURCES = $(commonbuiltsrc)
standalone_SOURCES = $(commonsrc) standalone.cpp
standalone_LDADD = $(QT_LDFLAGS) $(QT_LIBS) -L../libhfp -lhfp $(libhfp_LIBS)
standalone_DEPENDENCIES = ../libhfp/libhfp.a

CLEANFILES = $(commonbuiltsrc) standalone.moc.cpp

.ui.h: $(@:.h=.ui)
	$(UIC) $^ -o $@

.ui.cpp: $(@:.cpp=.h) $(@:.cpp=.ui)
	$(UIC) -impl $(@:.cpp=.h) $(@:.cpp=.ui) -o $@

$(moc_built_srcs): $(@:.moc.cpp=.h)
	$(MOC) $(@:.moc.cpp=.h) -o $@

standalone.moc.cpp: standalone.cpp
	$(MOC) $< -o $@

standalone.o: nohands.h scandialog.h prefs.h standalone.moc.cpp
