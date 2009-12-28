#!/bin/sh
autoheader || exit 1
mkdir -p aclocal || exit 1
aclocal -I ./aclocal || exit 1
autoconf || exit 1
automake -ac --add-missing || exit 1
