#!/bin/sh 

# Shell script to be run on Mac OS X, which uses `SetFile' to set
# up the appropriate file metadata so that you can run MPW and have
# it build classic-Mac PuTTY.

SETFILE=/Developer/Tools/SetFile

# I want to be able to run this either from the `mac' subdirectory
# or from the main `putty' source directory.
if test -f mac_res.r -a -f ../putty.h; then
  cd ..
fi
if test ! -f putty.h; then
  echo 'putty.h not found.' >&2
  echo 'This script should be run in the PuTTY source directory.' >&2
  exit 1
fi

# Now we can assume we're in the main PuTTY source dir.
find . -name .svn -prune -o -name '*.[chr]' -exec $SETFILE -t TEXT {} \;
$SETFILE -t TEXT mac/mkputty.mpw
