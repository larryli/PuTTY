#!/bin/bash 

# Build a Unix source distribution from the PuTTY CVS area.
#
# Pass an argument of the form `2004-02-08' to have the archive
# tagged as a development snapshot; of the form `0.54' to have it
# tagged as a release; of the form `r1234' to have it tagged as a
# custom build. Otherwise it'll be tagged as unidentified.

case "$1" in
  ????-??-??)
    case "$1" in *[!-0-9]*) echo "Malformed snapshot ID '$1'" >&2;exit 1;;esac
    autoconfver="`cat LATEST.VER`-$1"
    arcsuffix="-$autoconfver"
    ver="-DSNAPSHOT=$1"
    docver=
    ;;
  r*)
    autoconfver="$1"
    arcsuffix="-$autoconfver"
    ver="-DSVN_REV=${1#r}"
    docver=
    ;;
  '')
    autoconfver="X.XX" # got to put something in here!
    arcsuffix=
    ver=
    docver=
    ;;
  *pre)
    set -- "${1%pre}" "$2"
    case "$1" in *[!.0-9a-z~]*) echo "Malformed prerelease ID '$1'">&2;exit 1;;esac
    case "$2" in *[!.0-9a-z~]*) echo "Malformed prerelease revision '$1'">&2;exit 1;;esac
    autoconfver="$1~pre$2"
    arcsuffix="-$autoconfver"
    ver="-DPRERELEASE=$1 -DSVN_REV=$2"
    docver="VERSION=\"PuTTY prerelease $1:r$2\""
    ;;
  *)
    case "$1" in *[!.0-9a-z~]*) echo "Malformed release ID '$1'">&2;exit 1;;esac
    autoconfver="$1"
    arcsuffix="-$autoconfver"
    ver="-DRELEASE=$1"
    docver="VERSION=\"PuTTY release $1\""
    ;;
esac

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
if test "x$ver" != "x"; then
  (cd uxarc/$arcname;
   md5sum `find . -name '*.[ch]' -print` > manifest;
   echo "$ver" > version.def)
fi
sed "s/^AC_INIT(putty,.*/AC_INIT(putty, $autoconfver)/" configure.ac > uxarc/$arcname/configure.ac
(cd uxarc/$arcname && sh mkauto.sh) 2>errors || { cat errors >&2; exit 1; }

tar -C uxarc -chzof $arcname.tar.gz $arcname
rm -rf uxarc
