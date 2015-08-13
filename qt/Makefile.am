# $Id: $

AM_CPPFLAGS = -I$(top_srcdir)/include -include config.h $(QT_CFLAGS)

bin_PROGRAMS = hfstandalone

uic_files = nohands.ui prefs.ui scandialog.ui
uic_built_hdrs = $(uic_files:.ui=.h)
moc_inputs = nohandswidget.h prefswidget.h scandialogwidget.h events-qt.h
moc_built_srcs = $(moc_inputs:.h=.moc)
commonbuiltsrc = $(uic_built_hdrs) $(moc_built_srcs)
commonsrc = events-qt.h nohandswidget.h prefswidget.h scandialogwidget.h

nodist_hfstandalone_SOURCES = $(commonbuiltsrc)
hfstandalone_SOURCES = $(commonsrc) hfstandalone.cpp
hfstandalone_LDADD = $(QT_LDFLAGS) $(QT_LIBS) -L../libhfp -lhfp $(libhfp_LIBS)
hfstandalone_LDFLAGS = -pthread
hfstandalone_DEPENDENCIES = ../libhfp/libhfp.a

CLEANFILES = $(commonbuiltsrc) hfstandalone.moc
EXTRA_DIST = $(uic_files)

.ui.h: $(@:.h=.ui)
	$(UIC) $^ -o $@

.h.moc: $(@:.moc=.h)
	$(MOC) $< -o $@

hfstandalone.moc: hfstandalone.cpp
	$(MOC) $< -o $@

hfstandalone.o: nohands.h nohandswidget.h nohandswidget.moc scandialog.h scandialogwidget.h scandialogwidget.moc prefs.h prefswidget.h prefswidget.moc hfstandalone.moc events-qt.moc
