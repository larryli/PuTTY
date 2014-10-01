#!/bin/sh

# Build a Unix source distribution from the PuTTY CVS area.
#
# Expects the following arguments:
#  - the version number to write into configure.ac
#  - the suffix to put on the Unix source tarball
#  - the options to put on the 'make' command line for the docs

autoconfver="$1"
arcsuffix="$2"
docver="$3"

perl mkfiles.pl
(cd doc && make -s ${docver:+"$docver"})

relver=`cat LATEST.VER`
arcname="putty$arcsuffix"
mkdir uxarc
mkdir uxarc/$arcname
find . -name uxarc -prune -o \
       -name CVS -prune -o \
       -name .svn -prune -o \
       -name . -o \
       -type d -exec mkdir uxarc/$arcname/{} \;
find . -name uxarc -prune -o \
       -name CVS -prune -o \
       -name .cvsignore -prune -o \
       -name .svn -prune -o \
       -name configure.ac -prune -o \
       -name '*.zip' -prune -o \
       -name '*.tar.gz' -prune -o \
       -type f -exec ln -s $PWD/{} uxarc/$arcname/{} \;
sed "s/^AC_INIT(putty,.*/AC_INIT(putty, $autoconfver)/" configure.ac > uxarc/$arcname/configure.ac
(cd uxarc/$arcname && sh mkauto.sh) 2>errors || { cat errors >&2; exit 1; }

tar -C uxarc -chzof $arcname.tar.gz $arcname
rm -rf uxarc
