#!/bin/sh

set -e

# These are text files.
text=`{ find . -name CVS -prune -o \
               -name .cvsignore -prune -o \
               -name .svn -prune -o \
               -name .git -prune -o \
               -name LATEST.VER -prune -o \
               -name CHECKLST.txt -prune -o \
               -name mksrcarc.sh -prune -o \
               -name '*.chm' -prune -o \
               -name '*.cur' -prune -o \
               -type f -print | sed 's/^\.\///'; } | \
      grep -ivE 'test/.*\.txt|MODULE|website.url' | grep -vF .ico | grep -vF .icns`
# These are files which I'm _sure_ should be treated as text, but
# which zip might complain about, so we direct its moans to
# /dev/null! Apparently its heuristics are doubtful of UTF-8 text
# files.
bintext=test/*.txt
# These are actual binary files which we don't want transforming.
bin=`{ ls -1 windows/*.ico windows/website.url; \
       find . -name '*.chm' -print -o -name '*.cur' -print; }`

verbosely() {
    echo "$@"
    "$@"
}

verbosely zip -l putty-src.zip $text
verbosely zip -l putty-src.zip $bintext
verbosely zip putty-src.zip $bin
