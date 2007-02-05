#!/bin/sh 

# Generate GPG signatures on a PuTTY release/snapshot directory as
# delivered by Buildscr.

# Usage: sign.sh <builddir> <keytype>
# e.g. sign.sh build.out Snapshots
#   or sign.sh 0.60 Releases

set -e

sign() {
  # Check for the prior existence of the signature, so we can
  # re-run this script if it encounters an error part way
  # through.
  echo "----- Signing $2 with '$keyname'"
  test -f "$3" || \
    gpg --load-extension=idea "$1" -u "$keyname" -o "$3" "$2"
}

cd "$1"
for t in DSA RSA; do
  keyname="$2 ($t)"
  echo "===== Signing with '$keyname'"
  for i in putty*src.zip putty*.tar.gz x86/*.exe x86/*.zip; do
    sign --detach-sign "$i" "$i.$t"
  done
  sign --clearsign md5sums md5sums.$t
done
