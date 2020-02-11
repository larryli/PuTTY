#!/bin/sh

# Script to make the bitmap files that go into the PuTTY MSI installer.

set -e

# For convenience, allow this script to be run from the Windows
# subdirectory as well as the top level of the source tree.
if test -f installer.wxs -a ! -f putty.h -a -f ../putty.h; then
  cd ..
fi

convert -size 164x312 'gradient:blue-white' -distort SRT -90 -swirl 180 \
        \( icons/putty-48.png -geometry +28+24 \) -composite \
        \( icons/pscp-48.png -geometry +88+96 \) -composite \
        \( icons/puttygen-48.png -geometry +28+168 \) -composite \
        \( icons/pageant-48.png -geometry +88+240 \) -composite \
        windows/msidialog.bmp

convert -size 493x58 canvas:white \
        \( icons/putty-48.png -geometry +440+5 \) -composite \
        windows/msibanner.bmp
