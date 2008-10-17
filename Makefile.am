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

SUBDIRS = libhfp hfpd $(NHANDSFREE) $(QT) data
include m4/doxy.am
