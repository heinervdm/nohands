if BUILD_NGHOST
NHANDSFREE=nhandsfree
else !BUILD_NGHOST
NHANDSFREE=
endif !BUILD_NGHOST

if BUILD_QT
QT=qt
else !BUILD_QT
QT=
endif !BUILD_QT

if BUILD_DBUS
DBUS=hfpd
else !BUILD_DBUS
DBUS=
endif !BUILD_DBUS

SUBDIRS = libhfp $(DBUS) $(NHANDSFREE) $(QT) data
include m4/doxy.am
