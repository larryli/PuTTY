#!/bin/sh 

# Generate GPG signatures on a PuTTY release/snapshot directory as
# delivered by Buildscr.

# Usage: sh sign.sh [-r] <builddir>
# e.g.   sh sign.sh putty              (probably in the build.out directory)
#   or   sh sign.sh -r 0.60            (-r means use the release keys)

set -e

keyname=EEF20295D15F7E8A

if test "x$1" = "x-r"; then
    shift
    keyname=9DFE2648B43434E4
fi

sign() {
  # Check for the prior existence of the signature, so we can
  # re-run this script if it encounters an error part way
  # through.
  echo "----- Signing $2 with key '$keyname'"
  test -f "$3" || \
    gpg --load-extension=idea "$1" -u "$keyname" -o "$3" "$2"
}

cd "$1"
echo "===== Signing with key '$keyname'"
for i in putty*src.zip putty*.tar.gz x86/*.exe x86/*.zip; do
    sign --detach-sign "$i" "$i.gpg"
done
for i in md5sums sha1sums sha256sums sha512sums; do
    sign --clearsign "$i" "$i.gpg"
done
