#!/bin/sh 

# Generate GPG signatures on a PuTTY release/snapshot directory as
# delivered by Buildscr.

# Usage: sh sign.sh [-r] <builddir>
# e.g.   sh sign.sh putty              (probably in the build.out directory)
#   or   sh sign.sh -r 0.60            (-r means use the release keys)

set -e

keyname=EEF20295D15F7E8A
preliminary=false

while :; do
    case "$1" in
        -r)
            shift
            keyname=9DFE2648B43434E4
            ;;
        -p)
            shift
            preliminary=true
            ;;
        -*)
            echo "Unknown option '$1'" >&2
            exit 1
            ;;
        *)
            break
            ;;
    esac
done

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
if $preliminary; then
    sign --clearsign sha512sums ../sha512sums-preliminary.gpg
else
    for i in putty*src.zip putty*.tar.gz \
             w32/*.exe w32/*.zip w32/*.msi \
             w64/*.exe w64/*.zip w64/*.msi \
             w32old/*.exe w32old/*.zip; do
        sign --detach-sign "$i" "$i.gpg"
    done
    for i in md5sums sha1sums sha256sums sha512sums; do
        sign --clearsign "$i" "$i.gpg"
    done
fi
