#!/usr/bin/env python3

# Reference implementation of DES.
#
# As discussed in sshdes.c itself, this module implements DES in two
# different ways. The class DES is close to the official spec, with
# S-box contents you might recognise; the class SGTDES changes a lot
# of the details but in a way that compensate for each other, so it
# should end up overall functionally equivalent. But SGTDES's S-boxes
# look like the ones in sshdes.c, so diagnostics from this code can be
# used in the event that sshdes.c needs to be debugged.

import sys
import struct
import functools
import argparse

assert sys.version_info[:2] >= (3,0), "This is Python 3 code"

def bitor(x, y):
    return x | y
def split_words(val, width=32):
    mask = ((1<<width)-1)
    return mask & (val >> width), mask & val
def combine_words(hi, lo, width=32):
    mask = ((1<<width)-1)
    return ((mask & hi) << width) | (mask & lo)
def ror(val, shift, width=32):
    mask = ((1<<width)-1)
    return mask & ((val >> (shift % width)) | (val << (-shift % width)))
def rol(val, shift, width=32):
    return ror(val, -shift, width)
def bitselect(bits, val):
    # bits[i] gives the input bit index of the output bit at index i
    return functools.reduce(
        bitor, ((1 & (val >> inbit)) << outbit
                for outbit, inbit in enumerate(bits)))
def SB(hexstring):
    return [int(c,16) for c in hexstring]

def debug(string):
    sys.stdout.write(string + "\n")

class DESBase(object):
    def __init__(self):
        # Automatically construct FP by inverting IP
        self.FP = [None] * 64
        for i, j in enumerate(self.IP):
            self.FP[j] = i

    def f(self, word, key_material):
        debug("computing f({:08x}, {}):".format(
            word, " ".join(map("{:02x}".format,key_material))))
        sbox_inputs = [0x3F & (ror(word, offset) ^ key_element)
                       for offset, key_element in
                       zip(self.sbox_index_offsets, key_material)]
        sbox_outputs = [sbox[sbox_input] for sbox, sbox_input
                        in zip(self.sboxes, sbox_inputs)]
        debug("  S-boxes: {} -> {}".format(
            " ".join(map("{:02x}".format,sbox_inputs)),
            " ".join(map("{:x}".format,sbox_outputs))))
        word = functools.reduce(
            bitor, (v << (4*i) for i,v in enumerate(sbox_outputs)))
        debug("  S output = {:08x}".format(word))
        word = bitselect(self.P, word)
        debug("  P output = {:08x}".format(word))
        return word

    def cipher(self, integer, key_schedule):
        L, R = split_words(bitselect(self.IP, integer))
        debug("cipher start {:016x} -> {:08x} {:08x}".format(integer, L, R))
        for roundIndex, key_material in enumerate(key_schedule):
            L, R = R, L ^ self.f(R, key_material)
            debug("after round {:2d}: {:08x} {:08x}".format(roundIndex, L, R))
        output = bitselect(self.FP, combine_words(R, L))
        debug("cipher end {:08x} {:08x} -> {:016x}".format(R, L, output))
        return output

    def encipher(self, integer):
        return self.cipher(integer, self.key_schedule)
    def decipher(self, integer):
        return self.cipher(integer, list(reversed(self.key_schedule)))

    def setkey(self, key):
        self.key_schedule = []

        CD = bitselect(self.PC1, key)
        debug("initial CD = {:014x}".format(CD))
        for roundIndex, shift in enumerate(self.key_setup_shifts):
            C, D = split_words(CD, 28)
            C = rol(C, shift, 28)
            D = rol(D, shift, 28)
            CD = combine_words(C, D, 28)
            self.key_schedule.append(
                [bitselect(bits, CD) for bits in self.PC2])
            debug("CD[{:d}] = {:014x} -> {}):".format(
                roundIndex, CD, " ".join(
                    map("{:02x}".format,self.key_schedule[-1]))))

    # The PC1 permutation is fixed and arbitrary
    PC1 = [
        0x3c, 0x34, 0x2c, 0x24, 0x3b, 0x33, 0x2b,
        0x23, 0x1b, 0x13, 0x0b, 0x03, 0x3a, 0x32,
        0x2a, 0x22, 0x1a, 0x12, 0x0a, 0x02, 0x39,
        0x31, 0x29, 0x21, 0x19, 0x11, 0x09, 0x01,
        0x1c, 0x14, 0x0c, 0x04, 0x3d, 0x35, 0x2d,
        0x25, 0x1d, 0x15, 0x0d, 0x05, 0x3e, 0x36,
        0x2e, 0x26, 0x1e, 0x16, 0x0e, 0x06, 0x3f,
        0x37, 0x2f, 0x27, 0x1f, 0x17, 0x0f, 0x07,
    ]

    PC2 = [
        [0x18, 0x1b, 0x14, 0x06, 0x0e, 0x0a],
        [0x03, 0x16, 0x00, 0x11, 0x07, 0x0c],
        [0x08, 0x17, 0x0b, 0x05, 0x10, 0x1a],
        [0x01, 0x09, 0x13, 0x19, 0x04, 0x0f],
        [0x36, 0x2b, 0x24, 0x1d, 0x31, 0x28],
        [0x30, 0x1e, 0x34, 0x2c, 0x25, 0x21],
        [0x2e, 0x23, 0x32, 0x29, 0x1c, 0x35],
        [0x33, 0x37, 0x20, 0x2d, 0x27, 0x2a],
    ]

    key_setup_shifts = [1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1]

    # IP is better understood as a permutation and flipping of the
    # bits _in the index of each actual bit_ than as a long list of
    # individual indices
    IP = [bitselect([5,3,4,0,1,2], index ^ 0x27) for index in range(64)]

class DES(DESBase):
    sboxes = [
        SB('d12f8d486af3b714ac9536eb500ec97272b14e1794cae82d0f6ca9d0f335568b'),
        SB('4db02be7f40981da3ec3957c52af6816164bbdd8c1347ae7a9f5608f0e52932c'),
        SB('ca1fa4f2972c698506d13d4ee07b53b894e3f25c2985cf3a7b0e41a716d0b86d'),
        SB('2ecb421c74a7bd6185503ffad309e8964b281cb7a1de728df69fc0596a3405e3'),
        SB('7dd8eb35066f90a31427825cb1ca4ef9a36f9006cab17dd8f91435eb5c27824e'),
        SB('ad0790e96334f65a12d8c57ebc4b2f81d16a4d9086f93807b41f2ec35ba5e27c'),
        SB('f31d84e76fb2384e9c7021dac6095ba50de87ab1a34fd4125b86c76c90352ef9'),
        SB('e04fd7142ef2bd813aa66ccb599503784f1ce882d46921b7f5cb937e3aa0560d'),
    ]
    P = [
        0x07, 0x1c, 0x15, 0x0a, 0x1a, 0x02, 0x13, 0x0d,
        0x17, 0x1d, 0x05, 0x00, 0x12, 0x08, 0x18, 0x1e,
        0x16, 0x01, 0x0e, 0x1b, 0x06, 0x09, 0x11, 0x1f,
        0x0f, 0x04, 0x14, 0x03, 0x0b, 0x0c, 0x19, 0x10,
    ]
    sbox_index_offsets = [4*i-1 for i in range(8)]

class SGTDES(DESBase):
    sboxes = [
        SB('e41f8e2839f5d7429ac653bd600bac7171d42b47c2a9b81e0f3a9ce0f556638d'),
        SB('4db02be7f40981da3ec3957c52af6816164bbdd8c1347ae7a9f5608f0e52932c'),
        SB('c52f58f16b1c964a09e23e8dd0b7a37468d3f1ac164acf35b70d825b29e0749e'),
        SB('4ead241a72c7db6183305ffcb509e8962d481ad7c1be748bf69fa0396c5203e5'),
        SB('edd1b76c0aaf5036482e12c974938bf536af500a9374edd1f5486cb7c92e128b'),
        SB('9e07a0da5334f56921e8c67dbc4b1f82e2594ea085fa3807b42f1dc36b96d17c'),
        SB('f31d84e76fb2384e9c7021dac6095ba50de87ab1a34fd4125b86c76c90352ef9'),
        SB('d08feb281df17e4235599cc7a66a03b48f2cd441e896127bfac763bd3550a90e'),
    ]
    P = [
        0x1d, 0x14, 0x0b, 0x1a, 0x01, 0x10, 0x0e, 0x17,
        0x1c, 0x05, 0x02, 0x13, 0x09, 0x18, 0x1f, 0x16,
        0x00, 0x0d, 0x1b, 0x06, 0x08, 0x11, 0x1e, 0x0f,
        0x04, 0x15, 0x03, 0x0a, 0x0c, 0x19, 0x12, 0x07
    ]
    sbox_index_offsets = [4*i-2 for i in range(8)]
    IP = [DES.IP[i ^ ((i^(i+1)) & 0x1F)] for i in range(64)]

def main():
    hexstr = lambda s: int(s, 16)

    parser = argparse.ArgumentParser(description='')
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--des", action="store_const", dest="cipher", const=DES,
                       help="Use the official DES definition.")
    group.add_argument("--sgtdes", action="store_const", dest="cipher",
                       const=SGTDES, help="Use the equivalent SGT-DES.")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--encipher", "-e", action="store_const", dest="method",
                       const="encipher", help="Encipher.")
    group.add_argument("--decipher", "-d", action="store_const", dest="method",
                       const="decipher", help="Decipher.")
    parser.add_argument("key", type=hexstr, help="Cipher key (hex, 8 bytes, "
                        "low bit of each byte unused).")
    parser.add_argument("input", type=hexstr,
                        help="Cipher input (hex, 8 bytes).")
    parser.set_defaults(const=SGTDES) # main purpose is to debug sshdes.c
    args = parser.parse_args()

    des = args.cipher()
    des.setkey(args.key)
    method = getattr(des, args.method)
    output = method(args.input)

    sys.stdout.write("{} with key {:016x}: {:016x} -> {:016x}\n".format(
        args.method, args.key, args.input, output))

if __name__ == '__main__':
    main()
