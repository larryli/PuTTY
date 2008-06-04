#! /bin/sh
# This script makes the autoconf mechanism for the Unix port work.
# It's separate from mkfiles.pl because it won't work (and isn't needed)
# on a non-Unix system.

# It's nice to be able to run this from inside the unix subdir as
# well as from outside.
test -f unix.h && cd ..

# Persuade automake to give us a copy of its install-sh. This is a
# pain because I don't actually want to have to _use_ automake.
# Instead, I construct a trivial unrelated automake project in a
# temporary subdirectory, run automake so that it'll copy
# install-sh into that directory, then copy it back out again.
# Hideous, but it should work.

mkdir automake-grievous-hack
cat > automake-grievous-hack/hello.c << EOF
#include <stdio.h>
int main(int argc, char **argv)
{
    printf("hello, world\n");
    return 0;
}
EOF
cat > automake-grievous-hack/Makefile.am << EOF
bin_PROGRAMS = hello
hello_SOURCES = hello.c
EOF
cat > automake-grievous-hack/configure.ac << EOF
AC_INIT
AM_INIT_AUTOMAKE(hello, 1.0)
AC_CONFIG_FILES([Makefile])
AC_PROG_CC
AC_OUTPUT
EOF
echo Some news > automake-grievous-hack/NEWS
echo Some text > automake-grievous-hack/README
echo Some people > automake-grievous-hack/AUTHORS
echo Some changes > automake-grievous-hack/ChangeLog
rm -f install-sh # this won't work if we accidentally have one _here_
(cd automake-grievous-hack && autoreconf -i && \
  cp install-sh ../unix/install-sh)
rm -rf automake-grievous-hack

# That was the hard bit. Now run autoconf on our real configure.in.
(cd unix && autoreconf && rm -rf aclocal.m4 autom4te.cache)
