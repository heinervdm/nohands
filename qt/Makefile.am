# $Id: $

INCLUDES = -I$(top_srcdir)/include -include config.h $(QT_INCLUDES)

bin_PROGRAMS = hfstandalone

uic_files = nohands.ui prefs.ui scandialog.ui
uic_built_hdrs = $(uic_files:.ui=.h)
uic_built_srcs = $(uic_files:.ui=.cpp)
moc_inputs = nohands.h prefs.h scandialog.h events-qt.h
moc_built_srcs = $(moc_inputs:.h=.moc.cpp)
commonbuiltsrc = $(uic_built_hdrs) $(uic_built_srcs) $(moc_built_srcs)
commonsrc = events-qt.h nohands.ui.h prefs.ui.h scandialog.ui.h 

nodist_hfstandalone_SOURCES = $(commonbuiltsrc)
hfstandalone_SOURCES = $(commonsrc) hfstandalone.cpp
hfstandalone_LDADD = $(QT_LDFLAGS) $(QT_LIBS) -L../libhfp -lhfp $(libhfp_LIBS)
hfstandalone_DEPENDENCIES = ../libhfp/libhfp.a

CLEANFILES = $(commonbuiltsrc) hfstandalone.moc.cpp

.ui.h: $(@:.h=.ui)
	$(UIC) $^ -o $@

.ui.cpp: $(@:.cpp=.h) $(@:.cpp=.ui)
	$(UIC) -impl $(@:.cpp=.h) $(@:.cpp=.ui) -o $@

$(moc_built_srcs): $(@:.moc.cpp=.h)
	$(MOC) $(@:.moc.cpp=.h) -o $@

hfstandalone.moc.cpp: hfstandalone.cpp
	$(MOC) $< -o $@

hfstandalone.o: nohands.h scandialog.h prefs.h hfstandalone.moc.cpp
