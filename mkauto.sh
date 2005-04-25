#! /bin/sh
# This script makes the autoconf mechanism for the Unix port work.
# It's separate from mkfiles.pl because it won't work (and isn't needed)
# on a non-Unix system.

# Track down automake's copy of install-sh
cp `aclocal --print-ac-dir | sed 's/aclocal$/automake/'`/install-sh unix/.
(cd unix && autoreconf  && rm -rf aclocal.m4 autom4te.cache)
