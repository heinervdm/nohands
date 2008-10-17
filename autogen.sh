#!/bin/sh
aclocal -I m4
autoheader
libtoolize
automake -a
autoconf

