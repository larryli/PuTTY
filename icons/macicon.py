#!/usr/bin/env python3

# Generate Mac OS X .icns files, or at least the simple subformats
# that don't involve JPEG encoding and the like.
#
# Sources: https://en.wikipedia.org/wiki/Apple_Icon_Image_format and
# some details implicitly documented by the source code of 'libicns'.

import sys
import struct
import subprocess

assert sys.version_info[:2] >= (3,0), "This is Python 3 code"

# The file format has a typical IFF-style (type, length, data) chunk
# structure, with one outer chunk containing subchunks for various
# different icon sizes and formats.
def make_chunk(chunkid, data):
    assert len(chunkid) == 4
    return chunkid + struct.pack(">I", len(data) + 8) + data

# Monochrome icons: a single chunk containing a 1 bpp image followed
# by a 1 bpp transparency mask. Both uncompressed, unless you count
# packing the bits into bytes.
def make_mono_icon(size, rgba):
    assert len(rgba) == size * size

    # We assume our input image was monochrome, so that the R,G,B
    # channels are all the same; we want the image and then the mask,
    # so we take the R channel followed by the alpha channel. However,
    # we have to flip the former, because in the output format the
    # image has 0=white and 1=black, while the mask has 0=transparent
    # and 1=opaque.
    pixels = [rgba[index][chan] ^ flip for (chan, flip) in [(0,0xFF),(3,0)]
              for index in range(len(rgba))]

    # Encode in 1-bit big-endian format.
    data = b''
    for i in range(0, len(pixels), 8):
        byte = 0
        for j in range(8):
            if pixels[i+j] >= 0x80:
                byte |= 0x80 >> j
        data += bytes(byte)

    # This size-32 chunk id is an anomaly in what would otherwise be a
    # consistent system of using {s,l,h,t} for {16,32,48,128}-pixel
    # icon sizes.
    chunkid = { 16: b"ics#", 32: b"ICN#", 48: b"ich#" }[size]
    return make_chunk(chunkid, data)

# Mask for full-colour icons: a chunk containing an 8 bpp alpha
# bitmap, uncompressed. The RGB data appears in a separate chunk.
def make_colour_mask(size, rgba):
    assert len(rgba) == size * size

    data = bytes(map(lambda pix: pix[3], rgba))

    chunkid = { 16: b"s8mk", 32: b"l8mk", 48: b"h8mk", 128: b"t8mk" }[size]
    return make_chunk(chunkid, data)

# Helper routine for deciding when to start and stop run-length
# encoding.
def runof3(string, position):
    return (position < len(string) and
            string[position:position+3] == string[position] * 3)

# RGB data for full-colour icons: a chunk containing 8 bpp red, green
# and blue images, each run-length encoded (see comment inside the
# function), and then concatenated.
def make_colour_icon(size, rgba):
    assert len(rgba) == size * size

    data = b""

    # Mysterious extra zero header word appearing only in the size-128
    # icon chunk. libicns doesn't know what it's for, and neither do
    # I.
    if size == 128:
        data += b"\0\0\0\0"

    # Handle R,G,B channels in sequence. (Ignore the alpha channel; it
    # goes into the separate mask chunk constructed above.)
    for chan in range(3):
        pixels = bytes([rgba[index][chan] for index in range(len(rgba))])

        # Run-length encode each channel using the following format:
        #  * byte 0x80-0xFF followed by one literal byte means repeat
        #    that byte 3-130 times
        #  * byte 0x00-0x7F followed by n+1 literal bytes means emit
        #    those bytes once each.
        pos = 0
        while pos < len(pixels):
            start = pos
            if runof3(pixels, start):
                pos += 3
                pixval = pixels[start]
                while (pos - start < 130 and
                       pos < len(pixels) and
                       pixels[pos] == pixval):
                    pos += 1
                data += bytes(0x80 + pos-start - 3) + pixval
            else:
                while (pos - start < 128 and
                       pos < len(pixels) and
                       not runof3(pixels, pos)):
                    pos += 1
                data += bytes(0x00 + pos-start - 1) + pixels[start:pos]

    chunkid = { 16: b"is32", 32: b"il32", 48: b"ih32", 128: b"it32" }[size]
    return make_chunk(chunkid, data)

# Load an image file from disk and turn it into a simple list of
# 4-tuples giving 8-bit R,G,B,A values for each pixel.
#
# To avoid adding any build dependency on ImageMagick or Python
# imaging libraries, none of which comes as standard on OS X, I insist
# here that the file is in RGBA .pam format (as mkicon.py will have
# generated it).
def load_rgba(filename):
    with open(filename, "rb") as f:
        assert f.readline() == b"P7\n"
        for line in iter(f.readline, ''):
            words = line.decode("ASCII").rstrip("\n").split()
            if words[0] == "WIDTH":
                width = int(words[1])
            elif words[0] == "HEIGHT":
                height = int(words[1])
            elif words[0] == "DEPTH":
                assert int(words[1]) == 4
            elif words[0] == "TUPLTYPE":
                assert words[1] == "RGB_ALPHA"
            elif words[0] == "ENDHDR":
                break

        assert width == height
        data = f.read()
        assert len(data) == width*height*4
        rgba = [list(data[i:i+4]) for i in range(0, len(data), 4)]
        return width, rgba

data = b""

# Trivial argument format: each argument is a filename prefixed with
# "mono:", "colour:" or "output:". The first two indicate image files
# to use as part of the icon, and the last gives the output file name.
# Icon subformat chunks are written out in the order of the arguments.
for arg in sys.argv[1:]:
    kind, filename = arg.split(":", 2)
    if kind == "output":
        outfile = filename
    else:
        size, rgba = load_rgba(filename)
        if kind == "mono":
            data += make_mono_icon(size, rgba)
        elif kind == "colour":
            data += make_colour_icon(size, rgba) + make_colour_mask(size, rgba)
        else:
            assert False, "bad argument '%s'" % arg

data = make_chunk(b"icns", data)

with open(outfile, "wb") as f:
    f.write(data)
