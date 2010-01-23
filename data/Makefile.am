# Dbus service file
if BUILD_DBUS_SERVICE
servicedir = $(DBUS_SERVICE_PATH)
service_in_files = hfpd.service.in
service_DATA = $(service_in_files:.service.in=.service)

# Rule to make the service file with bindir expanded
$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@bindir\@|$(bindir)|" $< > $@
endif

# Glade XML files
hfconsole_IMAGES = icon.png agconnect.png audioconnect.png \
	contacts.png dialpad.png tel-cell.png vr.png \
	battery-0.png battery-1.png battery-2.png battery-3.png \
	battery-4.png battery-5.png \
	mute.png nobt.png noag.png message.png \
	pickup.png redial.png hangup.png holdcall.png swapcall.png \
	signal-0.png signal-1.png signal-2.png signal-3.png signal-4.png \
	signal-5.png signal.png signal-none.png 

glade_DATA = hfconsole.glade $(hfconsole_IMAGES)
gladedir = $(datadir)/hfconsole

bin_PROGRAMS = hfconsole

hfconsole_SOURCES = hfconsole.in

hfconsole: hfconsole.in Makefile
	@sed -e "s|\@gladedir\@|$(gladedir)|" $< > $@
	@chmod +x $@

EXTRA_DIST = $(service_in_files) $(glade_DATA)
CLEANFILES = $(service_DATA) $(hfconsole_PROGRAMS)
