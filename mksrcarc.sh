#!/bin/sh
perl mkfiles.pl
text=`{ find . -name CVS -prune -o \
               -name .cvsignore -prune -o \
               -name .svn -prune -o \
               -name LATEST.VER -prune -o \
               -name CHECKLST.txt -prune -o \
               -name mksrcarc.sh -prune -o \
               -name '*.dsp' -prune -o \
               -name '*.dsw' -prune -o \
               -type f -print | sed 's/^\.\///'; } | \
      grep -ivE MODULE\|putty.iss\|website.url | grep -vF .ico`
bin=`{ ls -1 windows/*.ico windows/putty.iss windows/website.url; \
       find . -name '*.dsp' -print -o -name '*.dsw' -print; }`
zip -k -l putty-src.zip $text > /dev/null
zip -k putty-src.zip $bin > /dev/null
