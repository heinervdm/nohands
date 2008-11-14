# Dbus service file
if BUILD_DBUS_SERVICE
servicedir = $(DBUS_SERVICE_PATH)
service_in_files = hfpd.service.in
service_DATA = $(service_in_files:.service.in=.service)

# Rule to make the service file with bindir expanded
$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@bindir\@|$(bindir)|" $< > $@
endif

# Glade XML file
glade_DATA = hfconsole.glade dsp-sidebar.png
gladedir = $(datadir)/hfconsole

bin_PROGRAMS = hfconsole

hfconsole_SOURCES = hfconsole.in

hfconsole: hfconsole.in Makefile
	@sed -e "s|\@gladedir\@|$(gladedir)|" $< > $@
	@chmod +x $@

EXTRA_DIST = $(service_in_files) $(glade_DATA)
CLEANFILES = $(service_DATA) $(hfconsole_PROGRAMS)
