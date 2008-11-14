# $Id: $

INCLUDES = -I$(top_srcdir)/include -include config.h $(QT_INCLUDES)

bin_PROGRAMS = hfstandalone

uic_files = nohands.ui prefs.ui scandialog.ui
uic_built_hdrs = $(uic_files:.ui=.h)
uic_built_srcs = $(uic_files:.ui=.cpp)
moc_inputs = nohands.h prefs.h scandialog.h events-qt.h
moc_built_srcs = $(moc_inputs:.h=.moc)
commonbuiltsrc = $(uic_built_hdrs) $(uic_built_srcs) $(moc_built_srcs)
commonsrc = events-qt.h nohands.ui.h prefs.ui.h scandialog.ui.h 

nodist_hfstandalone_SOURCES = $(commonbuiltsrc)
hfstandalone_SOURCES = $(commonsrc) hfstandalone.cpp
hfstandalone_LDADD = $(QT_LDFLAGS) $(QT_LIBS) -L../libhfp -lhfp $(libhfp_LIBS)
hfstandalone_DEPENDENCIES = ../libhfp/libhfp.a

CLEANFILES = $(commonbuiltsrc) hfstandalone.moc
EXTRA_DIST = $(uic_files)

.ui.h: $(@:.h=.ui)
	$(UIC) $^ -o $@

.ui.cpp: $(@:.cpp=.h) $(@:.cpp=.ui)
	$(UIC) -impl $(@:.cpp=.h) $< -o $@

.h.moc: $(@:.moc=.h)
	$(MOC) $< -o $@

hfstandalone.moc: hfstandalone.cpp
	$(MOC) $< -o $@

hfstandalone.o: nohands.h scandialog.h prefs.h hfstandalone.moc events-qt.moc
nohands.o: nohands.cpp nohands.ui.h nohands.moc
prefs.o: prefs.cpp prefs.ui.h prefs.moc
scandialog.o: scandialog.cpp scandialog.ui.h scandialog.moc
