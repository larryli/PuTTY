#! /bin/sh
# This script makes the autoconf mechanism for the Unix port work.
# It's separate from mkfiles.pl because it won't work (and isn't needed)
# on a non-Unix system.

# It's nice to be able to run this from inside the unix subdir as
# well as from outside.
test -f unix.h && cd ..

# Run autoconf on our real configure.in.
autoreconf -i && rm -rf autom4te.cache
