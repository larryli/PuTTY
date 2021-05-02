#!/bin/sh

# Build a Unix source distribution from the PuTTY CVS area.
#
# Expects the following arguments:
#  - the suffix to put on the Unix source tarball
#  - the options to put on the 'make' command line for the docs

arcsuffix="$1"

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
       -name '*.zip' -prune -o \
       -name '*.tar.gz' -prune -o \
       -type f -exec ln -s $PWD/{} uxarc/$arcname/{} \;

tar -C uxarc -chzof $arcname.tar.gz $arcname
rm -rf uxarc
