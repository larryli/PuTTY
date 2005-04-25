#!/bin/sh 

# Build a Unix source distribution from the PuTTY CVS area.
#
# Pass an argument of the form `2004-02-08' to have the archive
# tagged as a development snapshot; of the form `0.54' to have it
# tagged as a release.

case "$1" in
  ????-??-??)
    case "$1" in *[!-0-9]*) echo "Malformed snapshot ID '$1'" >&2;exit 1;;esac
    arcsuffix="-`cat LATEST.VER`-$1"
    ver="-DSNAPSHOT=$1"
    docver=
    ;;
  '')
    arcsuffix=
    ver=
    docver=
    ;;
  *)
    case "$1" in *[!.0-9a-z]*) echo "Malformed release ID '$1'">&2;exit 1;;esac
    arcsuffix="-$1"
    ver="-DRELEASE=$1"
    docver="VERSION=\"PuTTY release $1\""
    ;;
esac

perl mkfiles.pl
(cd doc && make -s ${docver:+"$docver"})
# Track down automake's copy of install-sh
cp `aclocal --print-ac-dir | sed 's/aclocal$/automake/'`/install-sh unix/.
(cd unix && autoreconf  && rm -rf aclocal.m4 autom4te.cache)

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
if test "x$ver" != "x"; then
  (cd uxarc/$arcname;
   md5sum `find . -name '*.[ch]' -print` > manifest;
   echo "$ver" > version.def)
fi
tar -C uxarc -chzof $arcname.tar.gz $arcname
rm -rf uxarc
