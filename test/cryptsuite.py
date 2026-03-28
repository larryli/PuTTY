#!/usr/bin/env python3

import sys
import unittest
import struct
import itertools
import functools
import contextlib
import hashlib
import binascii
from base64 import b64decode as b64
import json
try:
    from math import gcd
except ImportError:
    from fractions import gcd

from eccref import *
from testcrypt import *
from ssh import *
from ca import CertType, make_signature_preimage, sign_cert_via_testcrypt

assert sys.version_info[:2] >= (3,0), "This is Python 3 code"

def unhex(s):
    return binascii.unhexlify(s.replace(" ", "").replace("\n", ""))

def rsa_bare(e, n):
    rsa = rsa_new()
    get_rsa_ssh1_pub(ssh_uint32(nbits(n)) + ssh1_mpint(e) + ssh1_mpint(n),
                     rsa, 'exponent_first')
    return rsa

def find_non_square_mod(p):
    # Find a non-square mod p, using the Jacobi symbol
    # calculation function from eccref.py.
    return next(z for z in itertools.count(2) if jacobi(z, p) == -1)

def fibonacci_scattered(n=10):
    # Generate a list of Fibonacci numbers with power-of-2 indices
    # (F_1, F_2, F_4, ...), to be used as test inputs of varying
    # sizes. Also put F_0 = 0 into the list as a bonus.
    yield 0
    a, b, c = 0, 1, 1
    while True:
        yield b
        n -= 1
        if n <= 0:
            break
        a, b, c = (a**2+b**2, b*(a+c), b**2+c**2)

def fibonacci(n=10):
    # Generate the full Fibonacci sequence starting from F_0 = 0.
    a, b = 0, 1
    while True:
        yield a
        n -= 1
        if n <= 0:
            break
        a, b = b, a+b

def mp_mask(mp):
    # Return the value that mp would represent if all its bits
    # were set. Useful for masking a true mathematical output
    # value (e.g. from an operation that can over/underflow, like
    # mp_sub or mp_anything_into) to check it's right within the
    # ability of that particular mp_int to represent.
    return ((1 << mp_max_bits(mp))-1)

def adjtuples(iterable, n):
    # Return all the contiguous n-tuples of an iterable, including
    # overlapping ones. E.g. if called on [0,1,2,3,4] with n=3 it
    # would return (0,1,2), (1,2,3), (2,3,4) and then stop.
    it = iter(iterable)
    toret = [next(it) for _ in range(n-1)]
    for element in it:
        toret.append(element)
        yield tuple(toret)
        toret[:1] = []

def last(iterable):
    # Return the last element of an iterable, or None if it is empty.
    it = iter(iterable)
    toret = None
    for toret in it:
        pass
    return toret

def le_integer(x, nbits):
    assert nbits % 8 == 0
    return bytes([0xFF & (x >> (8*n)) for n in range(nbits//8)])

def be_integer(x, nbits):
    return bytes(reversed(le_integer(x, nbits)))

def decode_le_integer(s):
    return sum(byte << (8*i) for i,byte in enumerate(s))

@contextlib.contextmanager
def queued_random_data(nbytes, seed):
    hashsize = 512 // 8
    data = b''.join(
        hashlib.sha512("preimage:{:d}:{}".format(i, seed).encode('ascii'))
        .digest() for i in range((nbytes + hashsize - 1) // hashsize))
    data = data[:nbytes]
    random_queue(data)
    yield None
    random_clear()

@contextlib.contextmanager
def queued_specific_random_data(data):
    random_queue(data)
    yield None
    random_clear()

@contextlib.contextmanager
def random_prng(seed):
    random_make_prng('sha256', seed)
    yield None
    random_clear()

def hash_str(alg, message):
    h = ssh_hash_new(alg)
    ssh_hash_update(h, message)
    return ssh_hash_final(h)

def hash_str_iter(alg, message_iter):
    h = ssh_hash_new(alg)
    for string in message_iter:
        ssh_hash_update(h, string)
    return ssh_hash_final(h)

def mac_str(alg, key, message, cipher=None):
    m = ssh2_mac_new(alg, cipher)
    ssh2_mac_setkey(m, key)
    ssh2_mac_start(m)
    ssh2_mac_update(m, "dummy")
    # Make sure ssh_mac_start erases previous state
    ssh2_mac_start(m)
    ssh2_mac_update(m, message)
    return ssh2_mac_genresult(m)

def lcm(a, b):
    return a * b // gcd(a, b)

def get_implementations(alg):
    return get_implementations_commasep(alg).decode("ASCII").split(",")

def get_aes_impls():
    return [impl.rsplit("_", 1)[-1]
            for impl in get_implementations("aes128_cbc")
            if impl.startswith("aes128_cbc_")]

def get_aesgcm_impls():
    return [impl.split("_", 1)[1]
            for impl in get_implementations("aesgcm")
            if impl.startswith("aesgcm_")]

class MyTestBase(unittest.TestCase):
    "Intermediate class that adds useful helper methods."
    def assertEqualBin(self, x, y):
        # Like assertEqual, but produces more legible error reports
        # for random-looking binary data.
        self.assertEqual(binascii.hexlify(x), binascii.hexlify(y))

class mpint(MyTestBase):
    def testCreation(self):
        self.assertEqual(int(mp_new(128)), 0)
        self.assertEqual(int(mp_from_bytes_be(b'ABCDEFGHIJKLMNOP')),
                         0x4142434445464748494a4b4c4d4e4f50)
        self.assertEqual(int(mp_from_bytes_le(b'ABCDEFGHIJKLMNOP')),
                         0x504f4e4d4c4b4a494847464544434241)
        self.assertEqual(int(mp_from_integer(12345)), 12345)
        decstr = '91596559417721901505460351493238411077414937428167'
        self.assertEqual(int(mp_from_decimal_pl(decstr)), int(decstr, 10))
        self.assertEqual(int(mp_from_decimal(decstr)), int(decstr, 10))
        self.assertEqual(int(mp_from_decimal("")), 0)
        # For hex, test both upper and lower case digits
        hexstr = 'ea7cb89f409ae845215822e37D32D0C63EC43E1381C2FF8094'
        self.assertEqual(int(mp_from_hex_pl(hexstr)), int(hexstr, 16))
        self.assertEqual(int(mp_from_hex(hexstr)), int(hexstr, 16))
        self.assertEqual(int(mp_from_hex("")), 0)
        p2 = mp_power_2(123)
        self.assertEqual(int(p2), 1 << 123)
        p2c = mp_copy(p2)
        self.assertEqual(int(p2c), 1 << 123)
        # Check mp_copy really makes a copy, not an alias (ok, that's
        # testing the testcrypt system more than it's testing the
        # underlying C functions)
        mp_set_bit(p2c, 120, 1)
        self.assertEqual(int(p2c), (1 << 123) + (1 << 120))
        self.assertEqual(int(p2), 1 << 123)

    def testBytesAndBits(self):
        x = mp_new(128)
        self.assertEqual(mp_get_byte(x, 2), 0)
        mp_set_bit(x, 2*8+3, 1)
        self.assertEqual(mp_get_byte(x, 2), 1<<3)
        self.assertEqual(mp_get_bit(x, 2*8+3), 1)
        mp_set_bit(x, 2*8+3, 0)
        self.assertEqual(mp_get_byte(x, 2), 0)
        self.assertEqual(mp_get_bit(x, 2*8+3), 0)
        # Currently I expect 128 to be a multiple of any
        # BIGNUM_INT_BITS value we might be running with, so these
        # should be exact equality
        self.assertEqual(mp_max_bytes(x), 128/8)
        self.assertEqual(mp_max_bits(x), 128)

        nb = lambda hexstr: mp_get_nbits(mp_from_hex(hexstr))
        self.assertEqual(nb('00000000000000000000000000000000'), 0)
        self.assertEqual(nb('00000000000000000000000000000001'), 1)
        self.assertEqual(nb('00000000000000000000000000000002'), 2)
        self.assertEqual(nb('00000000000000000000000000000003'), 2)
        self.assertEqual(nb('00000000000000000000000000000004'), 3)
        self.assertEqual(nb('000003ffffffffffffffffffffffffff'), 106)
        self.assertEqual(nb('000003ffffffffff0000000000000000'), 106)
        self.assertEqual(nb('80000000000000000000000000000000'), 128)
        self.assertEqual(nb('ffffffffffffffffffffffffffffffff'), 128)

    def testDecAndHex(self):
        def checkHex(hexstr):
            n = mp_from_hex(hexstr)
            i = int(hexstr, 16)
            self.assertEqual(mp_get_hex(n),
                             "{:x}".format(i).encode('ascii'))
            self.assertEqual(mp_get_hex_uppercase(n),
                             "{:X}".format(i).encode('ascii'))
        checkHex("0")
        checkHex("f")
        checkHex("00000000000000000000000000000000000000000000000000")
        checkHex("d5aa1acd5a9a1f6b126ed416015390b8dc5fceee4c86afc8c2")
        checkHex("ffffffffffffffffffffffffffffffffffffffffffffffffff")

        def checkDec(hexstr):
            n = mp_from_hex(hexstr)
            i = int(hexstr, 16)
            self.assertEqual(mp_get_decimal(n),
                             "{:d}".format(i).encode('ascii'))
        checkDec("0")
        checkDec("f")
        checkDec("00000000000000000000000000000000000000000000000000")
        checkDec("d5aa1acd5a9a1f6b126ed416015390b8dc5fceee4c86afc8c2")
        checkDec("ffffffffffffffffffffffffffffffffffffffffffffffffff")
        checkDec("f" * 512)

    def testComparison(self):
        inputs = [
            "0", "1", "2", "10", "314159265358979", "FFFFFFFFFFFFFFFF",

            # Test over-long versions of some of the same numbers we
            # had short forms of above
            "0000000000000000000000000000000000000000000000000000000000000000"
            "0000000000000000000000000000000000000000000000000000000000000000",

            "0000000000000000000000000000000000000000000000000000000000000000"
            "0000000000000000000000000000000000000000000000000000000000000001",

            "0000000000000000000000000000000000000000000000000000000000000000"
            "0000000000000000000000000000000000000000000000000000000000000002",

            "0000000000000000000000000000000000000000000000000000000000000000"
            "000000000000000000000000000000000000000000000000FFFFFFFFFFFFFFFF",

            "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
            "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
        ]
        values = [(mp_from_hex(s), int(s, 16)) for s in inputs]
        for am, ai in values:
            for bm, bi in values:
                self.assertEqual(mp_cmp_eq(am, bm) == 1, ai == bi)
                self.assertEqual(mp_cmp_hs(am, bm) == 1, ai >= bi)
                if (bi >> 64) == 0:
                    self.assertEqual(mp_eq_integer(am, bi) == 1, ai == bi)
                    self.assertEqual(mp_hs_integer(am, bi) == 1, ai >= bi)

                # mp_{min,max}{,_into} is a reasonable thing to test
                # here as well
                self.assertEqual(int(mp_min(am, bm)), min(ai, bi))
                self.assertEqual(int(mp_max(am, bm)), max(ai, bi))
                am_small = mp_copy(am if ai<bi else bm)
                mp_min_into(am_small, am, bm)
                self.assertEqual(int(am_small), min(ai, bi))
                am_big = mp_copy(am if ai>bi else bm)
                mp_max_into(am_big, am, bm)
                self.assertEqual(int(am_big), max(ai, bi))

        # Test mp_{eq,hs}_integer in the case where the integer is as
        # large as possible and the bignum contains very few words. In
        # modes where BIGNUM_INT_BITS < 64, this used to go wrong.
        mp10 = mp_new(4)
        mp_copy_integer_into(mp10, 10)
        highbit = 1 << 63
        self.assertEqual(mp_hs_integer(mp10, highbit | 9), 0)
        self.assertEqual(mp_hs_integer(mp10, highbit | 10), 0)
        self.assertEqual(mp_hs_integer(mp10, highbit | 11), 0)
        self.assertEqual(mp_eq_integer(mp10, highbit | 9), 0)
        self.assertEqual(mp_eq_integer(mp10, highbit | 10), 0)
        self.assertEqual(mp_eq_integer(mp10, highbit | 11), 0)

    def testConditionals(self):
        testnumbers = [(mp_copy(n),n) for n in fibonacci_scattered()]
        for am, ai in testnumbers:
            for bm, bi in testnumbers:
                cm = mp_copy(am)
                mp_select_into(cm, am, bm, 0)
                self.assertEqual(int(cm), ai & mp_mask(am))
                mp_select_into(cm, am, bm, 1)
                self.assertEqual(int(cm), bi & mp_mask(am))

                mp_cond_add_into(cm, am, bm, 0)
                self.assertEqual(int(cm), ai & mp_mask(am))
                mp_cond_add_into(cm, am, bm, 1)
                self.assertEqual(int(cm), (ai+bi) & mp_mask(am))

                mp_cond_sub_into(cm, am, bm, 0)
                self.assertEqual(int(cm), ai & mp_mask(am))
                mp_cond_sub_into(cm, am, bm, 1)
                self.assertEqual(int(cm), (ai-bi) & mp_mask(am))

                maxbits = max(mp_max_bits(am), mp_max_bits(bm))
                cm = mp_new(maxbits)
                dm = mp_new(maxbits)
                mp_copy_into(cm, am)
                mp_copy_into(dm, bm)

                self.assertEqual(int(cm), ai)
                self.assertEqual(int(dm), bi)
                mp_cond_swap(cm, dm, 0)
                self.assertEqual(int(cm), ai)
                self.assertEqual(int(dm), bi)
                mp_cond_swap(cm, dm, 1)
                self.assertEqual(int(cm), bi)
                self.assertEqual(int(dm), ai)

                if bi != 0:
                    mp_cond_clear(cm, 0)
                    self.assertEqual(int(cm), bi)
                    mp_cond_clear(cm, 1)
                    self.assertEqual(int(cm), 0)

    def testBasicArithmetic(self):
        testnumbers = list(fibonacci_scattered(5))
        testnumbers.extend([1 << (1 << i) for i in range(3,10)])
        testnumbers.extend([(1 << (1 << i)) - 1 for i in range(3,10)])

        testnumbers = [(mp_copy(n),n) for n in testnumbers]

        for am, ai in testnumbers:
            for bm, bi in testnumbers:
                self.assertEqual(int(mp_add(am, bm)), ai + bi)
                self.assertEqual(int(mp_mul(am, bm)), ai * bi)
                # Cope with underflow in subtraction
                diff = mp_sub(am, bm)
                self.assertEqual(int(diff), (ai - bi) & mp_mask(diff))

                for bits in range(64, 512, 64):
                    cm = mp_new(bits)
                    mp_add_into(cm, am, bm)
                    self.assertEqual(int(cm), (ai + bi) & mp_mask(cm))
                    mp_mul_into(cm, am, bm)
                    self.assertEqual(int(cm), (ai * bi) & mp_mask(cm))
                    mp_sub_into(cm, am, bm)
                    self.assertEqual(int(cm), (ai - bi) & mp_mask(cm))

        # A test cherry-picked from the old bignum test script,
        # involving two numbers whose product has a single 1 bit miles
        # in the air and then all 0s until a bunch of cruft at the
        # bottom, the aim being to test that carry propagation works
        # all the way up.
        ai, bi = 0xb4ff6ed2c633847562087ed9354c5c17be212ac83b59c10c316250f50b7889e5b058bf6bfafd12825225ba225ede0cba583ffbd0882de88c9e62677385a6dbdedaf81959a273eb7909ebde21ae5d12e2a584501a6756fe50ccb93b93f0d6ee721b6052a0d88431e62f410d608532868cdf3a6de26886559e94cc2677eea9bd797918b70e2717e95b45918bd1f86530cb9989e68b632c496becff848aa1956cd57ed46676a65ce6dd9783f230c8796909eef5583fcfe4acbf9c8b4ea33a08ec3fd417cf7175f434025d032567a00fc329aee154ca20f799b961fbab8f841cb7351f561a44aea45746ceaf56874dad99b63a7d7af2769d2f185e2d1c656cc6630b5aba98399fa57, 0xb50a77c03ac195225021dc18d930a352f27c0404742f961ca828c972737bad3ada74b1144657ab1d15fe1b8aefde8784ad61783f3c8d4584aa5f22a4eeca619f90563ae351b5da46770df182cf348d8e23b25fda07670c6609118e916a57ce4043608752c91515708327e36f5bb5ebd92cd4cfb39424167a679870202b23593aa524bac541a3ad322c38102a01e9659b06a4335c78d50739a51027954ac2bf03e500f975c2fa4d0ab5dd84cc9334f219d2ae933946583e384ed5dbf6498f214480ca66987b867df0f69d92e4e14071e4b8545212dd5e29ff0248ed751e168d78934da7930bcbe10e9a212128a68de5d749c61f5e424cf8cf6aa329674de0cf49c6f9b4c8b8cc3
        am = mp_copy(ai)
        bm = mp_copy(bi)
        self.assertEqual(int(mp_mul(am, bm)), ai * bi)

        # A regression test for a bug that came up during development
        # of mpint.c, relating to an intermediate value overflowing
        # its container.
        ai, bi = (2**8512 * 2 // 3), (2**4224 * 11 // 15)
        am = mp_copy(ai)
        bm = mp_copy(bi)
        self.assertEqual(int(mp_mul(am, bm)), ai * bi)

    def testAddInteger(self):
        initial = mp_copy(4444444444444444444444444)

        x = mp_new(mp_max_bits(initial) + 64)

        # mp_{add,sub,copy}_integer_into should be able to cope with
        # any uintmax_t. Test a number that requires more than 32 bits.
        mp_add_integer_into(x, initial, 123123123123123)
        self.assertEqual(int(x), 4444444444567567567567567)
        mp_sub_integer_into(x, initial, 123123123123123)
        self.assertEqual(int(x), 4444444444321321321321321)
        mp_copy_integer_into(x, 123123123123123)
        self.assertEqual(int(x), 123123123123123)

        # mp_mul_integer_into only takes a uint16_t integer input
        mp_mul_integer_into(x, initial, 10001)
        self.assertEqual(int(x), 44448888888888888888888884444)

    def testDivision(self):
        divisors = [1, 2, 3, 2**16+1, 2**32-1, 2**32+1, 2**128-159,
                    141421356237309504880168872420969807856967187537694807]
        quotients = [0, 1, 2, 2**64-1, 2**64, 2**64+1, 17320508075688772935]
        for d in divisors:
            for q in quotients:
                remainders = {0, 1, d-1, 2*d//3}
                for r in sorted(remainders):
                    if r >= d:
                        continue # silly cases with tiny divisors
                    n = q*d + r
                    mq = mp_new(max(nbits(q), 1))
                    mr = mp_new(max(nbits(r), 1))
                    mp_divmod_into(n, d, mq, mr)
                    self.assertEqual(int(mq), q)
                    self.assertEqual(int(mr), r)
                    self.assertEqual(int(mp_div(n, d)), q)
                    self.assertEqual(int(mp_mod(n, d)), r)

                    # Make sure divmod_into can handle not getting one
                    # of its output pointers (or even both).
                    mp_clear(mq)
                    mp_divmod_into(n, d, mq, None)
                    self.assertEqual(int(mq), q)
                    mp_clear(mr)
                    mp_divmod_into(n, d, None, mr)
                    self.assertEqual(int(mr), r)
                    mp_divmod_into(n, d, None, None)
                    # No tests we can do after that last one - we just
                    # insist that it isn't allowed to have crashed!

    def testNthRoot(self):
        roots = [1, 13, 1234567654321,
                 57721566490153286060651209008240243104215933593992]
        tests = []
        tests.append((0, 2, 0, 0))
        tests.append((0, 3, 0, 0))
        for r in roots:
            for n in 2, 3, 5:
                tests.append((r**n, n, r, 0))
                tests.append((r**n+1, n, r, 1))
                tests.append((r**n-1, n, r-1, r**n - (r-1)**n - 1))
        for x, n, eroot, eremainder in tests:
            with self.subTest(x=x):
                mx = mp_copy(x)
                remainder = mp_copy(mx)
                root = mp_nthroot(x, n, remainder)
                self.assertEqual(int(root), eroot)
                self.assertEqual(int(remainder), eremainder)
        self.assertEqual(int(mp_nthroot(2*10**100, 2, None)),
                         141421356237309504880168872420969807856967187537694)
        self.assertEqual(int(mp_nthroot(3*10**150, 3, None)),
                         144224957030740838232163831078010958839186925349935)

    def testBitwise(self):
        p = 0x3243f6a8885a308d313198a2e03707344a4093822299f31d0082efa98ec4e
        e = 0x2b7e151628aed2a6abf7158809cf4f3c762e7160f38b4da56a784d9045190
        x = mp_new(nbits(p))

        mp_and_into(x, p, e)
        self.assertEqual(int(x), p & e)

        mp_or_into(x, p, e)
        self.assertEqual(int(x), p | e)

        mp_xor_into(x, p, e)
        self.assertEqual(int(x), p ^ e)

        mp_bic_into(x, p, e)
        self.assertEqual(int(x), p & ~e)

    def testInversion(self):
        # Test mp_invert_mod_2to.
        testnumbers = [(mp_copy(n),n) for n in fibonacci_scattered()
                       if n & 1]
        for power2 in [1, 2, 3, 5, 13, 32, 64, 127, 128, 129]:
            for am, ai in testnumbers:
                bm = mp_invert_mod_2to(am, power2)
                bi = int(bm)
                self.assertEqual(((ai * bi) & ((1 << power2) - 1)), 1)

                # mp_reduce_mod_2to is a much simpler function, but
                # this is as good a place as any to test it.
                rm = mp_copy(am)
                mp_reduce_mod_2to(rm, power2)
                self.assertEqual(int(rm), ai & ((1 << power2) - 1))

        # Test mp_invert proper.
        moduli = [2, 3, 2**16+1, 2**32-1, 2**32+1, 2**128-159,
                  141421356237309504880168872420969807856967187537694807,
                  2**128-1]
        for m in moduli:
            # Prepare a MontyContext for the monty_invert test below
            # (unless m is even, in which case we can't)
            mc = monty_new(m) if m & 1 else None

            to_invert = {1, 2, 3, 7, 19, m-1, 5*m//17, (m-1)//2, (m+1)//2}
            for x in sorted(to_invert):
                if gcd(x, m) != 1:
                    continue # filter out non-invertible cases
                inv = int(mp_invert(x, m))
                assert x * inv % m == 1

                # Test monty_invert too, while we're here
                if mc is not None:
                    self.assertEqual(
                        int(monty_invert(mc, monty_import(mc, x))),
                        int(monty_import(mc, inv)))

    def testGCD(self):
        powerpairs = [(0,0), (1,0), (1,1), (2,1), (2,2), (75,3), (17,23)]
        for a2, b2 in powerpairs:
            for a3, b3 in powerpairs:
                for a5, b5 in powerpairs:
                    a = 2**a2 * 3**a3 * 5**a5 * 17 * 19 * 23
                    b = 2**b2 * 3**b3 * 5**b5 * 65423
                    d = 2**min(a2, b2) * 3**min(a3, b3) * 5**min(a5, b5)

                    ma = mp_copy(a)
                    mb = mp_copy(b)

                    self.assertEqual(int(mp_gcd(ma, mb)), d)

                    md = mp_new(nbits(d))
                    mA = mp_new(nbits(b))
                    mB = mp_new(nbits(a))
                    mp_gcd_into(ma, mb, md, mA, mB)
                    self.assertEqual(int(md), d)
                    A = int(mA)
                    B = int(mB)
                    self.assertEqual(a*A - b*B, d)
                    self.assertTrue(0 <= A < b//d)
                    self.assertTrue(0 <= B < a//d)

                    self.assertEqual(mp_coprime(ma, mb), 1 if d==1 else 0)

                    # Make sure gcd_into can handle not getting some
                    # of its output pointers.
                    mp_clear(md)
                    mp_gcd_into(ma, mb, md, None, None)
                    self.assertEqual(int(md), d)
                    mp_clear(mA)
                    mp_gcd_into(ma, mb, None, mA, None)
                    self.assertEqual(int(mA), A)
                    mp_clear(mB)
                    mp_gcd_into(ma, mb, None, None, mB)
                    self.assertEqual(int(mB), B)
                    mp_gcd_into(ma, mb, None, None, None)
                    # No tests we can do after that last one - we just
                    # insist that it isn't allowed to have crashed!

    def testMonty(self):
        moduli = [5, 19, 2**16+1, 2**31-1, 2**128-159, 2**255-19,
                  293828847201107461142630006802421204703,
                  113064788724832491560079164581712332614996441637880086878209969852674997069759]

        for m in moduli:
            mc = monty_new(m)

            # Import some numbers
            inputs = [(monty_import(mc, n), n)
                      for n in sorted({0, 1, 2, 3, 2*m//3, m-1})]

            # Check modulus and identity
            self.assertEqual(int(monty_modulus(mc)), m)
            self.assertEqual(int(monty_identity(mc)), int(inputs[1][0]))

            # Check that all those numbers export OK
            for mn, n in inputs:
                self.assertEqual(int(monty_export(mc, mn)), n)

            for ma, a in inputs:
                for mb, b in inputs:
                    xprod = int(monty_export(mc, monty_mul(mc, ma, mb)))
                    self.assertEqual(xprod, a*b % m)

                    xsum = int(monty_export(mc, monty_add(mc, ma, mb)))
                    self.assertEqual(xsum, (a+b) % m)

                    xdiff = int(monty_export(mc, monty_sub(mc, ma, mb)))
                    self.assertEqual(xdiff, (a-b) % m)

                    # Test the ordinary mp_mod{add,sub,mul} at the
                    # same time, even though those don't do any
                    # montying at all

                    xprod = int(mp_modmul(a, b, m))
                    self.assertEqual(xprod, a*b % m)

                    xsum = int(mp_modadd(a, b, m))
                    self.assertEqual(xsum, (a+b) % m)

                    xdiff = int(mp_modsub(a, b, m))
                    self.assertEqual(xdiff, (a-b) % m)

            for ma, a in inputs:
                # Compute a^0, a^1, a^1, a^2, a^3, a^5, ...
                indices = list(fibonacci())
                powers = [int(monty_export(mc, monty_pow(mc, ma, power)))
                          for power in indices]
                # Check the first two make sense
                self.assertEqual(powers[0], 1)
                self.assertEqual(powers[1], a)
                # Check the others using the Fibonacci identity:
                # F_n + F_{n+1} = F_{n+2}, so a^{F_n} a^{F_{n+1}} = a^{F_{n+2}}
                for p0, p1, p2 in adjtuples(powers, 3):
                    self.assertEqual(p2, p0 * p1 % m)

                # Test the ordinary mp_modpow here as well, while
                # we've got the machinery available
                for index, power in zip(indices, powers):
                    self.assertEqual(int(mp_modpow(a, index, m)), power)

        # A regression test for a bug I encountered during initial
        # development of mpint.c, in which an incomplete reduction
        # happened somewhere in an intermediate value.
        b, e, m = 0x2B5B93812F253FF91F56B3B4DAD01CA2884B6A80719B0DA4E2159A230C6009EDA97C5C8FD4636B324F9594706EE3AD444831571BA5E17B1B2DFA92DEA8B7E, 0x25, 0xC8FCFD0FD7371F4FE8D0150EFC124E220581569587CCD8E50423FA8D41E0B2A0127E100E92501E5EE3228D12EA422A568C17E0AD2E5C5FCC2AE9159D2B7FB8CB
        assert(int(mp_modpow(b, e, m)) == pow(b, e, m))

        # Make sure mp_modpow can handle a base larger than the
        # modulus, by pre-reducing it
        assert(int(mp_modpow(1<<877, 907, 999979)) == pow(2, 877*907, 999979))

    def testModsqrt(self):
        moduli = [
            5, 19, 2**16+1, 2**31-1, 2**128-159, 2**255-19,
            293828847201107461142630006802421204703,
            113064788724832491560079164581712332614996441637880086878209969852674997069759,
            0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF6FFFFFFFF00000001]
        for p in moduli:
            # Count the factors of 2 in the group. (That is, we want
            # p-1 to be an odd multiple of 2^{factors_of_2}.)
            factors_of_2 = nbits((p-1) & (1-p)) - 1
            assert (p & ((2 << factors_of_2)-1)) == ((1 << factors_of_2)+1)

            z = find_non_square_mod(p)

            sc = modsqrt_new(p, z)

            def ptest(x):
                root, success = mp_modsqrt(sc, x)
                r = int(root)
                self.assertTrue(success)
                self.assertEqual((r * r - x) % p, 0)

            def ntest(x):
                root, success = mp_modsqrt(sc, x)
                self.assertFalse(success)

            # Make up some more or less random values mod p to square
            v1 = pow(3, nbits(p), p)
            v2 = pow(5, v1, p)
            test_roots = [0, 1, 2, 3, 4, 3*p//4, v1, v2, v1+1, 12873*v1, v1*v2]
            known_squares = {r*r % p for r in test_roots}
            for s in known_squares:
                ptest(s)
                if s != 0:
                    ntest(z*s % p)

            # Make sure we've tested a value that is in each of the
            # subgroups of order (p-1)/2^k but not in the next one
            # (with the exception of k=0, which just means 'have we
            # tested a non-square?', which we have in the above loop).
            #
            # We do this by starting with a known non-square; then
            # squaring it (factors_of_2) times will return values
            # nested deeper and deeper in those subgroups.
            vbase = z
            for k in range(factors_of_2):
                # Adjust vbase by an arbitrary odd power of
                # z, so that it won't look too much like the previous
                # value.
                vbase = vbase * pow(z, (vbase + v1 + v2) | 1, p) % p

                # Move vbase into the next smaller group by squaring
                # it.
                vbase = pow(vbase, 2, p)

                ptest(vbase)

    def testShifts(self):
        x = ((1<<900) // 9949) | 1
        for i in range(2049):
            mp = mp_copy(x)

            mp_lshift_fixed_into(mp, mp, i)
            self.assertEqual(int(mp), (x << i) & mp_mask(mp))

            mp_copy_into(mp, x)
            mp_lshift_safe_into(mp, mp, i)
            self.assertEqual(int(mp), (x << i) & mp_mask(mp))

            mp_copy_into(mp, x)
            mp_rshift_fixed_into(mp, mp, i)
            self.assertEqual(int(mp), x >> i)

            mp_copy_into(mp, x)
            mp_rshift_safe_into(mp, mp, i)
            self.assertEqual(int(mp), x >> i)

            self.assertEqual(int(mp_rshift_fixed(x, i)), x >> i)

            self.assertEqual(int(mp_rshift_safe(x, i)), x >> i)

    def testRandom(self):
        # Test random_bits to ensure it correctly masks the return
        # value, and uses exactly as many random bytes as we expect it
        # to.
        for bits in range(512):
            bytes_needed = (bits + 7) // 8
            with queued_random_data(bytes_needed, "random_bits test"):
                mp = mp_random_bits(bits)
                self.assertTrue(int(mp) < (1 << bits))
                self.assertEqual(random_queue_len(), 0)

        # Test mp_random_in_range to ensure it returns things in the
        # right range.
        for rangesize in [2, 3, 19, 35]:
            for lo in [0, 1, 0x10001, 1<<512]:
                hi = lo + rangesize
                bytes_needed = mp_max_bytes(hi) + 16
                for trial in range(rangesize*3):
                    with queued_random_data(
                            bytes_needed,
                            "random_in_range {:d}".format(trial)):
                        v = int(mp_random_in_range(lo, hi))
                        self.assertTrue(lo <= v < hi)

class ecc(MyTestBase):
    def testWeierstrassSimple(self):
        # Simple tests using a Weierstrass curve I made up myself,
        # which (unlike the ones used for serious crypto) is small
        # enough that you can fit all the coordinates for a curve on
        # to your retina in one go.

        p = 3141592661
        a, b = -3 % p, 12345
        rc = WeierstrassCurve(p, a, b)
        wc = ecc_weierstrass_curve(p, a, b, None)

        def check_point(wp, rp):
            self.assertTrue(ecc_weierstrass_point_valid(wp))
            is_id = ecc_weierstrass_is_identity(wp)
            x, y = ecc_weierstrass_get_affine(wp)
            if rp.infinite:
                self.assertEqual(is_id, 1)
            else:
                self.assertEqual(is_id, 0)
                self.assertEqual(int(x), int(rp.x))
                self.assertEqual(int(y), int(rp.y))

        def make_point(x, y):
            wp = ecc_weierstrass_point_new(wc, x, y)
            rp = rc.point(x, y)
            check_point(wp, rp)
            return wp, rp

        # Some sample points, including the identity and also a pair
        # of mutual inverses.
        wI, rI = ecc_weierstrass_point_new_identity(wc), rc.point()
        wP, rP = make_point(102, 387427089)
        wQ, rQ = make_point(1000, 546126574)
        wmP, rmP = make_point(102, p - 387427089)

        # Check the simple arithmetic functions.
        check_point(ecc_weierstrass_add(wP, wQ), rP + rQ)
        check_point(ecc_weierstrass_add(wQ, wP), rP + rQ)
        check_point(ecc_weierstrass_double(wP), rP + rP)
        check_point(ecc_weierstrass_double(wQ), rQ + rQ)

        # Check all the special cases with add_general:
        # Adding two finite unequal non-mutually-inverse points
        check_point(ecc_weierstrass_add_general(wP, wQ), rP + rQ)
        # Doubling a finite point
        check_point(ecc_weierstrass_add_general(wP, wP), rP + rP)
        check_point(ecc_weierstrass_add_general(wQ, wQ), rQ + rQ)
        # Adding the identity to a point (both ways round)
        check_point(ecc_weierstrass_add_general(wI, wP), rP)
        check_point(ecc_weierstrass_add_general(wI, wQ), rQ)
        check_point(ecc_weierstrass_add_general(wP, wI), rP)
        check_point(ecc_weierstrass_add_general(wQ, wI), rQ)
        # Doubling the identity
        check_point(ecc_weierstrass_add_general(wI, wI), rI)
        # Adding a point to its own inverse, giving the identity.
        check_point(ecc_weierstrass_add_general(wmP, wP), rI)
        check_point(ecc_weierstrass_add_general(wP, wmP), rI)

        # Verify that point_valid fails if we pass it nonsense.
        bogus = ecc_weierstrass_point_new(wc, int(rP.x), int(rP.y * 3))
        self.assertFalse(ecc_weierstrass_point_valid(bogus))

        # Make sure add_general still correctly doubles a point if we
        # add two _different_ representations of the same point to
        # each other.
        wP2 = ecc_weierstrass_point_change_denominator(wP, 2)
        self.assertTrue(ecc_weierstrass_point_valid(wP2))
        check_point(ecc_weierstrass_add_general(wP, wP2), rP + rP)

        # Re-instantiate the curve with the ability to take square
        # roots, and check that we can reconstruct P and Q from their
        # x coordinate and y parity only.
        wc = ecc_weierstrass_curve(p, a, b, find_non_square_mod(p))

        x, yp = int(rP.x), (int(rP.y) & 1)
        check_point(ecc_weierstrass_point_new_from_x(wc, x, yp), rP)
        check_point(ecc_weierstrass_point_new_from_x(wc, x, yp ^ 1), rmP)
        x, yp = int(rQ.x), (int(rQ.y) & 1)
        check_point(ecc_weierstrass_point_new_from_x(wc, x, yp), rQ)

    def testMontgomerySimple(self):
        p, a, b = 3141592661, 0xabc, 0xde

        rc = MontgomeryCurve(p, a, b)
        mc = ecc_montgomery_curve(p, a, b)

        rP = rc.cpoint(0x1001)
        rQ = rc.cpoint(0x20001)
        rdiff = rP - rQ
        rsum = rP + rQ

        def make_mpoint(rp):
            return ecc_montgomery_point_new(mc, int(rp.x))

        mP = make_mpoint(rP)
        mQ = make_mpoint(rQ)
        mdiff = make_mpoint(rdiff)
        msum = make_mpoint(rsum)

        def check_point(mp, rp):
            x = ecc_montgomery_get_affine(mp)
            self.assertEqual(int(x), int(rp.x))

        check_point(ecc_montgomery_diff_add(mP, mQ, mdiff), rsum)
        check_point(ecc_montgomery_diff_add(mQ, mP, mdiff), rsum)
        check_point(ecc_montgomery_diff_add(mP, mQ, msum), rdiff)
        check_point(ecc_montgomery_diff_add(mQ, mP, msum), rdiff)
        check_point(ecc_montgomery_double(mP), rP + rP)
        check_point(ecc_montgomery_double(mQ), rQ + rQ)

        zero = ecc_montgomery_point_new(mc, 0)
        self.assertEqual(ecc_montgomery_is_identity(zero), False)
        identity = ecc_montgomery_double(zero)
        ecc_montgomery_get_affine(identity)
        self.assertEqual(ecc_montgomery_is_identity(identity), True)

    def testEdwardsSimple(self):
        p, d, a = 3141592661, 2688750488, 367934288

        rc = TwistedEdwardsCurve(p, d, a)
        ec = ecc_edwards_curve(p, d, a, None)

        def check_point(ep, rp):
            x, y = ecc_edwards_get_affine(ep)
            self.assertEqual(int(x), int(rp.x))
            self.assertEqual(int(y), int(rp.y))

        def make_point(x, y):
            ep = ecc_edwards_point_new(ec, x, y)
            rp = rc.point(x, y)
            check_point(ep, rp)
            return ep, rp

        # Some sample points, including the identity and also a pair
        # of mutual inverses.
        eI, rI = make_point(0, 1)
        eP, rP = make_point(196270812, 1576162644)
        eQ, rQ = make_point(1777630975, 2717453445)
        emP, rmP = make_point(p - 196270812, 1576162644)

        # Check that the ordinary add function handles all the special
        # cases.

        # Adding two finite unequal non-mutually-inverse points
        check_point(ecc_edwards_add(eP, eQ), rP + rQ)
        check_point(ecc_edwards_add(eQ, eP), rP + rQ)
        # Doubling a finite point
        check_point(ecc_edwards_add(eP, eP), rP + rP)
        check_point(ecc_edwards_add(eQ, eQ), rQ + rQ)
        # Adding the identity to a point (both ways round)
        check_point(ecc_edwards_add(eI, eP), rP)
        check_point(ecc_edwards_add(eI, eQ), rQ)
        check_point(ecc_edwards_add(eP, eI), rP)
        check_point(ecc_edwards_add(eQ, eI), rQ)
        # Doubling the identity
        check_point(ecc_edwards_add(eI, eI), rI)
        # Adding a point to its own inverse, giving the identity.
        check_point(ecc_edwards_add(emP, eP), rI)
        check_point(ecc_edwards_add(eP, emP), rI)

        # Re-instantiate the curve with the ability to take square
        # roots, and check that we can reconstruct P and Q from their
        # y coordinate and x parity only.
        ec = ecc_edwards_curve(p, d, a, find_non_square_mod(p))

        y, xp = int(rP.y), (int(rP.x) & 1)
        check_point(ecc_edwards_point_new_from_y(ec, y, xp), rP)
        check_point(ecc_edwards_point_new_from_y(ec, y, xp ^ 1), rmP)
        y, xp = int(rQ.y), (int(rQ.x) & 1)
        check_point(ecc_edwards_point_new_from_y(ec, y, xp), rQ)

    # For testing point multiplication, let's switch to the full-sized
    # standard curves, because I want to have tested those a bit too.

    def testWeierstrassMultiply(self):
        curve = p256
        wc = ecc_weierstrass_curve(curve.p, int(curve.a), int(curve.b), None)
        wG = ecc_weierstrass_point_new(wc, int(curve.G.x), int(curve.G.y))
        self.assertTrue(ecc_weierstrass_point_valid(wG))

        ints = set(i % curve.G_order for i in fibonacci_scattered(10))
        ints.remove(0) # the zero multiple isn't expected to work
        ints.add(curve.G_order - 1)
        for i in sorted(ints):
            wGi = ecc_weierstrass_multiply(wG, i)
            x, y = ecc_weierstrass_get_affine(wGi)
            rGi = curve.G * i
            self.assertEqual(int(x), int(rGi.x))
            self.assertEqual(int(y), int(rGi.y))

    def testMontgomeryMultiply(self):
        curve = curve25519
        mc = ecc_montgomery_curve(
            curve.p, int(curve.a), int(curve.b))
        mG = ecc_montgomery_point_new(mc, int(curve.G.x))

        ints = set(i % curve.G_order for i in fibonacci_scattered(10))
        ints.remove(0) # the zero multiple isn't expected to work
        ints.add(curve.G_order - 1)
        for i in sorted(ints):
            mGi = ecc_montgomery_multiply(mG, i)
            x = ecc_montgomery_get_affine(mGi)
            rGi = curve.G * i
            self.assertEqual(int(x), int(rGi.x))

    def testEdwardsMultiply(self):
        curve = ed25519
        ec = ecc_edwards_curve(curve.p, int(curve.d), int(curve.a), None)
        eG = ecc_edwards_point_new(ec, int(curve.G.x), int(curve.G.y))

        ints = set(i % curve.G_order for i in fibonacci_scattered(10))
        ints.remove(0) # the zero multiple isn't expected to work
        ints.add(curve.G_order - 1)
        for i in sorted(ints):
            eGi = ecc_edwards_multiply(eG, i)
            x, y = ecc_edwards_get_affine(eGi)
            rGi = curve.G * i
            self.assertEqual(int(x), int(rGi.x))
            self.assertEqual(int(y), int(rGi.y))

    def testWeierstrassBogusAssertionRegression(self):
        curve = p256
        wc = ecc_weierstrass_curve(curve.p, int(curve.a), int(curve.b), None)
        # The point described by these coordinates has the property
        # that 10*P and 11*P have the same affine y-coordinate. So
        # when the Montgomery-ladder based multiply routine wants to
        # make 21*P, it must add those two points, triggering a bug in
        # which it failed an assertion if the two inputs to an
        # addition were on the same horizontal line.
        tx = 0x858d6d6329394a7720d4c9cb4dbcb38ccff10ef6faa9fc45fc0067a5021ff53e
        ty = 0x32e9d51b7216745493e8ddc0d67fe15d6e39fa0e71cfc82e00045ca2763e0d74
        rP = curve.point(tx, ty)
        assert (10*rP).y == (11*rP).y

        wP = ecc_weierstrass_point_new(wc, tx, ty)
        self.assertTrue(ecc_weierstrass_point_valid(wP))

        i = 21
        wPi = ecc_weierstrass_multiply(wP, i)
        x, y = ecc_weierstrass_get_affine(wPi)
        rPi = rP * i
        self.assertEqual(int(x), int(rPi.x))
        self.assertEqual(int(y), int(rPi.y))

class keygen(MyTestBase):
    def testPrimeCandidateSource(self):
        def inspect(pcs):
            # Returns (pcs->limit, pcs->factor, pcs->addend) as Python integers
            return tuple(map(int, pcs_inspect(pcs)))

        # Test accumulating modular congruence requirements, by
        # inspecting the internal values computed during
        # require_residue. We ensure that the addend satisfies all our
        # congruences and the factor is the lcm of all the moduli
        # (hence, the arithmetic progression defined by those
        # parameters is precisely the set of integers satisfying the
        # requirements); we also ensure that the limiting values
        # (addend itself at the low end, and addend + (limit-1) *
        # factor at the high end) are the maximal subsequence of that
        # progression that are within the originally specified range.

        def check(pcs, lo, hi, mod_res_pairs):
            limit, factor, addend = inspect(pcs)

            for mod, res in mod_res_pairs:
                self.assertEqual(addend % mod, res % mod)

            self.assertEqual(factor, functools.reduce(
                lcm, [mod for mod, res in mod_res_pairs]))

            self.assertFalse(lo <= addend +      (-1) * factor < hi)
            self.assertTrue (lo <= addend                      < hi)
            self.assertTrue (lo <= addend + (limit-1) * factor < hi)
            self.assertFalse(lo <= addend +  limit    * factor < hi)

        pcs = pcs_new(64)
        check(pcs, 2**63, 2**64, [(2, 1)])
        pcs_require_residue(pcs, 3, 2)
        check(pcs, 2**63, 2**64, [(2, 1), (3, 2)])
        pcs_require_residue_1(pcs, 7)
        check(pcs, 2**63, 2**64, [(2, 1), (3, 2), (7, 1)])
        pcs_require_residue(pcs, 16, 7)
        check(pcs, 2**63, 2**64, [(2, 1), (3, 2), (7, 1), (16, 7)])
        pcs_require_residue(pcs, 49, 8)
        check(pcs, 2**63, 2**64, [(2, 1), (3, 2), (7, 1), (16, 7), (49, 8)])

        # Now test-generate some actual values, and ensure they
        # satisfy all the congruences, and also avoid one residue mod
        # 5 that we told them to. Also, give a nontrivial range.
        pcs = pcs_new_with_firstbits(64, 0xAB, 8)
        pcs_require_residue(pcs, 0x100, 0xCD)
        pcs_require_residue_1(pcs, 65537)
        pcs_avoid_residue_small(pcs, 5, 3)
        pcs_ready(pcs)
        with random_prng("test seed"):
            for i in range(100):
                n = int(pcs_generate(pcs))
                self.assertTrue((0xAB<<56) < n < (0xAC<<56))
                self.assertEqual(n % 0x100, 0xCD)
                self.assertEqual(n % 65537, 1)
                self.assertNotEqual(n % 5, 3)

                # I'm not actually testing here that the outputs of
                # pcs_generate are non-multiples of _all_ primes up to
                # 2^16. But checking this many for 100 turns is enough
                # to be pretty sure. (If you take the product of
                # (1-1/p) over all p in the list below, you find that
                # a given random number has about a 13% chance of
                # avoiding being a multiple of any of them. So 100
                # trials without a mistake gives you 0.13^100 < 10^-88
                # as the probability of it happening by chance. More
                # likely the code is actually working :-)

                for p in [2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59,61]:
                    self.assertNotEqual(n % p, 0)

    def testPocklePositive(self):
        def add_small(po, *ps):
            for p in ps:
                self.assertEqual(pockle_add_small_prime(po, p), 'POCKLE_OK')
        def add(po, *args):
            self.assertEqual(pockle_add_prime(po, *args), 'POCKLE_OK')

        # Transcription of the proof that 2^130-5 is prime from
        # Theorem 3.1 from http://cr.yp.to/mac/poly1305-20050329.pdf
        po = pockle_new()
        p1 = (2**130 - 6) // 1517314646
        p2 = (p1 - 1) // 222890620702
        add_small(po, 37003, 221101)
        add(po, p2, [37003, 221101], 2)
        add(po, p1, [p2], 2)
        add(po, 2**130 - 5, [p1], 2)

        # My own proof that 2^255-19 is prime
        po = pockle_new()
        p1 = 8574133
        p2 = 1919519569386763
        p3 = 75445702479781427272750846543864801
        p4 = (2**255 - 20) // (65147*12)
        p = 2**255 - 19
        add_small(po, p1)
        add(po, p2, [p1], 2)
        add(po, p3, [p2], 2)
        add(po, p4, [p3], 2)
        add(po, p, [p4], 2)

        # And the prime used in Ed448, while I'm here
        po = pockle_new()
        p1 = 379979
        p2 = 1764234391
        p3 = 97859369123353
        p4 = 34741861125639557
        p5 = 36131535570665139281
        p6 = 167773885276849215533569
        p7 = 596242599987116128415063
        p = 2**448 - 2**224 - 1
        add_small(po, p1, p2)
        add(po, p3, [p1], 2)
        add(po, p4, [p2], 2)
        add(po, p5, [p4], 2)
        add(po, p6, [p3], 3)
        add(po, p7, [p5], 3)
        add(po, p, [p6, p7], 2)

        p = 4095744004479977
        factors = [2, 79999] # just enough factors to exceed cbrt(p)
        po = pockle_new()
        for q in factors:
            add_small(po, q)
        add(po, p, factors, 3)

        # The order of the generator in Ed25519
        po = pockle_new()
        p1a, p1b = 132667, 137849
        p2 = 3044861653679985063343
        p3 = 198211423230930754013084525763697
        p = 2**252 + 0x14def9dea2f79cd65812631a5cf5d3ed
        add_small(po, p1a, p1b)
        add(po, p2, [p1a, p1b], 2)
        add(po, p3, [p2], 2)
        add(po, p, [p3], 2)

        # And the one in Ed448
        po = pockle_new()
        p1 = 766223
        p2 = 3009341
        p3 = 7156907
        p4 = 671065561
        p5 = 342682509629
        p6 = 6730519843040614479184435237013
        p = 2**446 - 0x8335dc163bb124b65129c96fde933d8d723a70aadc873d6d54a7bb0d
        add_small(po, p1, p2, p3, p4)
        add(po, p5, [p1], 2)
        add(po, p6, [p3,p4], 2)
        add(po, p, [p2,p5,p6], 2)

        # Combined certificate for the moduli and generator orders of
        # the three NIST curves, generated by contrib/proveprime.py
        # (with some cosmetic tidying)
        p256 = 2**256 - 2**224 + 2**192 + 2**96 - 1
        p384 = 2**384 - 2**128 - 2**96 + 2**32 - 1
        p521 = 2**521 - 1
        order256 = p256 - 0x4319055358e8617b0c46353d039cdaae
        order384 = p384 - 0x389cb27e0bc8d21fa7e5f24cb74f58851313e696333ad68c
        t = 0x5ae79787c40d069948033feb708f65a2fc44a36477663b851449048e16ec79bf6
        order521 = p521 - t
        p0 = order384 // 12895580879789762060783039592702
        p1 = 1059392654943455286185473617842338478315215895509773412096307
        p2 = 55942463741690639
        p3 = 37344768852931
        p4 = order521 // 1898873518475180724503002533770555108536
        p5 = p4 // 994165722
        p6 = 144471089338257942164514676806340723
        p7 = p384 // 2054993070433694
        p8 = 1357291859799823621
        po = pockle_new()
        add_small(po, 2, 3, 5, 11, 17, 19, 31, 41, 53, 67, 71, 109, 131, 149,
                  157, 257, 521, 641, 1613, 2731, 3407, 6317, 8191, 8389,
                  14461, 17449, 38189, 38557, 42641, 51481, 61681, 65537,
                  133279, 248431, 312289, 409891, 490463, 858001, 6700417,
                  187019741)
        add(po, p3, [149, 11, 5, 3, 2], 3)
        add(po, p2, [p3], 2)
        add(po, p8, [6317, 67, 2, 2], 2)
        add(po, p6, [133279, 14461, 109, 3], 7)
        add(po, p1, [p2, 248431], 2)
        add(po, order256, [187019741, 38189, 17449, 3407, 131, 71, 2, 2, 2, 2],
            7)
        add(po, p256, [6700417, 490463, 65537, 641, 257, 17, 5, 5, 3, 2], 6)
        add(po, p0, [p1], 2)
        add(po, p7, [p8, 312289, 38557, 8389, 11, 2], 3)
        add(po, p5, [p6, 19], 2)
        add(po, order384, [p0], 2)
        add(po, p384, [p7], 2)
        add(po, p4, [p5], 2)
        add(po, order521, [p4], 2)
        add(po, p521, [858001, 409891, 61681, 51481, 42641, 8191, 2731, 1613,
                       521, 157, 131, 53, 41, 31, 17, 11, 5, 5, 3, 2], 3)

    def testPockleNegative(self):
        def add_small(po, p):
            self.assertEqual(pockle_add_small_prime(po, p), 'POCKLE_OK')

        po = pockle_new()
        self.assertEqual(pockle_add_small_prime(po, 0),
                         'POCKLE_PRIME_SMALLER_THAN_2')
        self.assertEqual(pockle_add_small_prime(po, 1),
                         'POCKLE_PRIME_SMALLER_THAN_2')
        self.assertEqual(pockle_add_small_prime(po, 2**61 - 1),
                         'POCKLE_SMALL_PRIME_NOT_SMALL')
        self.assertEqual(pockle_add_small_prime(po, 4),
                         'POCKLE_SMALL_PRIME_NOT_PRIME')

        po = pockle_new()
        self.assertEqual(pockle_add_prime(po, 1919519569386763, [8574133], 2),
                         'POCKLE_FACTOR_NOT_KNOWN_PRIME')

        po = pockle_new()
        add_small(po, 8574133)
        self.assertEqual(pockle_add_prime(po, 1919519569386765, [8574133], 2),
                         'POCKLE_FACTOR_NOT_A_FACTOR')

        p = 4095744004479977
        factors = [2, 79997] # not quite enough factors to reach cbrt(p)
        po = pockle_new()
        for q in factors:
            add_small(po, q)
        self.assertEqual(pockle_add_prime(po, p, factors, 3),
                         'POCKLE_PRODUCT_OF_FACTORS_TOO_SMALL')

        p = 1999527 * 3999053
        factors = [999763]
        po = pockle_new()
        for q in factors:
            add_small(po, q)
        self.assertEqual(pockle_add_prime(po, p, factors, 3),
                         'POCKLE_DISCRIMINANT_IS_SQUARE')

        p = 9999929 * 9999931
        factors = [257, 2593]
        po = pockle_new()
        for q in factors:
            add_small(po, q)
        self.assertEqual(pockle_add_prime(po, p, factors, 3),
                         'POCKLE_FERMAT_TEST_FAILED')

        p = 1713000920401 # a Carmichael number
        po = pockle_new()
        add_small(po, 561787)
        self.assertEqual(pockle_add_prime(po, p, [561787], 2),
                         'POCKLE_WITNESS_POWER_IS_1')

        p = 4294971121
        factors = [3, 5, 11, 17]
        po = pockle_new()
        for q in factors:
            add_small(po, q)
        self.assertEqual(pockle_add_prime(po, p, factors, 17),
                         'POCKLE_WITNESS_POWER_NOT_COPRIME')

        po = pockle_new()
        add_small(po, 2)
        self.assertEqual(pockle_add_prime(po, 1, [2], 1),
                         'POCKLE_PRIME_SMALLER_THAN_2')

    def testMillerRabin(self):
        # A prime congruent to 3 mod 4, so M-R can only do one
        # iteration: either a^{(p-1)/2} == +1, or -1. Either counts as
        # a pass; the latter also means the number is potentially a
        # primitive root.
        n = 0xe76e6aaa42b5d7423aa4da5613eb21c3
        mr = miller_rabin_new(n)
        self.assertEqual(miller_rabin_test(mr, 2), "passed+ppr")
        self.assertEqual(miller_rabin_test(mr, 4), "passed")

        # The 'potential primitive root' test only means that M-R
        # didn't _rule out_ the number being a primitive root, by
        # finding that any of the powers _it tested_ less than n-1
        # came out to be 1. In this case, 2 really is a primitive
        # root, but since 13 | n-1, the 13th powers mod n form a
        # multiplicative subgroup. So 2^13 is not a primitive root,
        # and yet, M-R can't tell the difference, because it only
        # tried the exponent (n-1)/2, not the actual counterexample
        # (n-1)/13.
        self.assertEqual(miller_rabin_test(mr, 2**13), "passed+ppr")

        # A prime congruent to 1 mod a reasonably large power of 2, so
        # M-R has lots of scope to have different things happen. 3 is
        # a primitive root, so we expect that 3, 3^2, 3^4, ..., 3^256
        # should all pass for different reasons, with only the first
        # of them returning passed+ppr.
        n = 0xb1b65ebe489ff0ab4597bb67c3d22d01
        mr = miller_rabin_new(n)
        w = 3
        self.assertEqual(miller_rabin_test(mr, w), "passed+ppr")
        for i in range(1, 10):
            w = w * w % n
            self.assertEqual(miller_rabin_test(mr, w), "passed")

        # A prime with an _absurdly_ large power-of-2 factor in its
        # multiplicative group.
        n = 0x600000000000000000000000000000000000000000000001
        mr = miller_rabin_new(n)
        w = 10
        self.assertEqual(miller_rabin_test(mr, w), "passed+ppr")
        for i in range(1, 200):
            w = w * w % n
            self.assertEqual(miller_rabin_test(mr, w), "passed")

        # A blatantly composite number. But we still expect to see a
        # pass if we give the witness 1 (which will give a maximal
        # trailing string of 1s), or -1 (which will give -1 when
        # raised to the maximal odd factor of n-1, or indeed any other
        # odd power).
        n = 0x1010101010101010101010101010101
        mr = miller_rabin_new(n)
        self.assertEqual(miller_rabin_test(mr, 1), "passed")
        self.assertEqual(miller_rabin_test(mr, n-1), "passed")
        self.assertEqual(miller_rabin_test(mr, 2), "failed")

        # A Carmichael number, as a proper test that M-R detects
        # things the Fermat test would not.
        #
        # (Its prime factorisation is 26823115100268314289505807 *
        # 53646230200536628579011613 * 80469345300804942868517419,
        # which is enough to re-check its Carmichaelness.)
        n = 0xffffffffffffffffcf8032f3e044b4a8b1b1bf0b526538eae953d90f44d65511
        mr = miller_rabin_new(n)
        self.assertEqual(miller_rabin_test(mr, 16), "passed")
        assert(pow(2, n-1, n) == 1) # Fermat test would pass, but ...
        self.assertEqual(miller_rabin_test(mr, 2), "failed") # ... this fails

        # A white-box test for the side-channel-safe M-R
        # implementation, which has to check a^e against +-1 for every
        # exponent e of the form floor((n-1) / power of 2), so as to
        # avoid giving away exactly how many of the trailing values of
        # that sequence are significant to the test.
        #
        # When the power of 2 is large enough that the division was
        # not exact, the results of these comparisons are _not_
        # significant to the test, and we're required to ignore them!
        #
        # This pair of values has the property that none of the values
        # legitimately computed by M-R is either +1 _or_ -1, but if
        # you shift n-1 right by one too many bits (losing the lowest
        # set bit of 0x6d00 to get 0x36), then _that_ power of the
        # witness integer is -1. This should not cause a spurious pass.
        n = 0x6d01
        mr = miller_rabin_new(n)
        self.assertEqual(miller_rabin_test(mr, 0x251), "failed")

class ntru(MyTestBase):
    def testMultiply(self):
        self.assertEqual(
            ntru_ring_multiply([1,1,1,1,1,1], [1,1,1,1,1,1], 11, 59),
            [1,2,3,4,5,6,5,4,3,2,1])
        self.assertEqual(ntru_ring_multiply(
            [1,0,1,2,0,0,1,2,0,1,2], [2,0,0,1,0,1,2,2,2,0,2], 11, 3),
                         [1,0,0,0,0,0,0,0,0,0,0])

    def testInvert(self):
        # Over GF(3), x^11-x-1 factorises as
        # (x^3+x^2+2) * (x^8+2*x^7+x^6+2*x^4+2*x^3+x^2+x+1)
        # so we expect that 2,0,1,1 has no inverse, being one of those factors.
        self.assertEqual(ntru_ring_invert([0], 11, 3), None)
        self.assertEqual(ntru_ring_invert([1], 11, 3),
                         [1,0,0,0,0,0,0,0,0,0,0])
        self.assertEqual(ntru_ring_invert([2,0,1,1], 11, 3), None)
        self.assertEqual(ntru_ring_invert([1,0,1,2,0,0,1,2,0,1,2], 11, 3),
                         [2,0,0,1,0,1,2,2,2,0,2])

        self.assertEqual(ntru_ring_invert([1,0,1,2,0,0,1,2,0,1,2], 11, 59),
                         [1,26,10,1,38,48,34,37,53,3,53])

    def testMod3Round3(self):
        # Try a prime congruent to 1 mod 3
        self.assertEqual(ntru_mod3([4,5,6,0,1,2,3], 7, 7),
                         [0,1,-1,0,1,-1,0])
        self.assertEqual(ntru_round3([4,5,6,0,1,2,3], 7, 7),
                         [-3,-3,0,0,0,3,3])

        # And one congruent to 2 mod 3
        self.assertEqual(ntru_mod3([6,7,8,9,10,0,1,2,3,4,5], 11, 11),
                         [1,-1,0,1,-1,0,1,-1,0,1,-1])
        self.assertEqual(ntru_round3([6,7,8,9,10,0,1,2,3,4,5], 11, 11),
                         [-6,-3,-3,-3,0,0,0,3,3,3,6])

    def testBiasScale(self):
        self.assertEqual(ntru_bias([0,1,2,3,4,5,6,7,8,9,10], 4, 11, 11),
                         [4,5,6,7,8,9,10,0,1,2,3])
        self.assertEqual(ntru_scale([0,1,2,3,4,5,6,7,8,9,10], 4, 11, 11),
                         [0,4,8,1,5,9,2,6,10,3,7])

    def testEncode(self):
        # Test a small case. Worked through in detail:
        #
        # Pass 1:
        #   Input list is (89:123, 90:234, 344:345, 432:456, 222:567)
        #   (89:123, 90:234) -> (89+123*90 : 123*234) = (11159:28782)
        #   Emit low byte of 11159 = 0x97, and get (43:113)
        #   (344:345, 432:456) -> (344+345*432 : 345*456) = (149384:157320)
        #   Emit low byte of 149384 = 0x88, and get (583:615)
        #   Odd pair (222:567) is copied to end of new list
        #   Final list is (43:113, 583:615, 222:567)
        # Pass 2:
        #   Input list is (43:113, 583:615, 222:567)
        #   (43:113, 583:615) -> (43+113*583, 113*615) = (65922:69495)
        #   Emit low byte of 65922 = 0x82, and get (257:272)
        #   Odd pair (222:567) is copied to end of new list
        #   Final list is (257:272, 222:567)
        # Pass 3:
        #   Input list is (257:272, 222:567)
        #   (257:272, 222:567) -> (257+272*222, 272*567) = (60641:154224)
        #   Emit low byte of 60641 = 0xe1, and get (236:603)
        #   Final list is (236:603)
        # Cleanup:
        #   Emit low byte of 236 = 0xec, and get (0:3)
        #   Emit low byte of 0 = 0x00, and get (0:1)

        ms = [123,234,345,456,567]
        rs = [89,90,344,432,222]
        encoding = unhex('978882e1ec00')
        sched = ntru_encode_schedule(ms)
        self.assertEqual(sched.encode(rs), encoding)
        self.assertEqual(sched.decode(encoding), rs)

        # Encode schedules for sntrup761 public keys and ciphertexts
        pubsched = ntru_encode_schedule([4591]*761)
        self.assertEqual(pubsched.length(), 1158)
        ciphersched = ntru_encode_schedule([1531]*761)
        self.assertEqual(ciphersched.length(), 1007)

        # Test round-trip encoding using those schedules
        testlist = list(range(761))
        pubtext = pubsched.encode(testlist)
        self.assertEqual(pubsched.decode(pubtext), testlist)
        ciphertext = ciphersched.encode(testlist)
        self.assertEqual(ciphersched.decode(ciphertext), testlist)

    def testCore(self):
        # My own set of NTRU Prime parameters, satisfying all the
        # requirements and tiny enough for convenient testing
        p, q, w = 11, 59, 3

        with random_prng('ntru keygen seed'):
            keypair = ntru_keygen(p, q, w)
            plaintext = ntru_gen_short(p, w)

        ciphertext = ntru_encrypt(plaintext, ntru_pubkey(keypair), p, q)
        recovered = ntru_decrypt(ciphertext, keypair)
        self.assertEqual(plaintext, recovered)

class crypt(MyTestBase):
    def testSSH1Fingerprint(self):
        # Example key and reference fingerprint value generated by
        # OpenSSH 6.7 ssh-keygen
        rsa = rsa_bare(65537, 984185866443261798625575612408956568591522723900235822424492423996716524817102482330189709310179009158443944785704183009867662230534501187034891091310377917105259938712348098594526746211645472854839799025154390701673823298369051411)
        fp = rsa_ssh1_fingerprint(rsa)
        self.assertEqual(
            fp, b"768 96:12:c8:bc:e6:03:75:86:e8:c7:b9:af:d8:0c:15:75")

    def testSSH2Fingerprints(self):
        # A sensible key blob that we can make sense of.
        sensible_blob = b64(
            'AAAAC3NzaC1lZDI1NTE5AAAAICWiV0VAD4lQ7taUN7vZ5Rkc'
            'SLJBW5ubn6ZINwCOzpn3')
        self.assertEqual(ssh2_fingerprint_blob(sensible_blob, "sha256"),
                         b'ssh-ed25519 255 SHA256:'
                         b'E4VmaHW0sUF7SUgSEOmMJ8WBtt0e/j3zbsKvyqfFnu4')
        self.assertEqual(ssh2_fingerprint_blob(sensible_blob, "md5"),
                         b'ssh-ed25519 255 '
                         b'35:73:80:df:a3:2c:1a:f2:2c:a6:5c:84:ce:48:6a:7e')

        # A key blob with an unknown algorithm name, so that we can't
        # extract the bit count.
        silly_blob = ssh_string(b'foo') + ssh_string(b'key data')
        self.assertEqual(ssh2_fingerprint_blob(silly_blob, "sha256"),
                         b'foo SHA256:'
                         b'mvfJTB4PaRI7hxYaYwn0sH8G6zW1HbLkbWnZE2YIKc4')
        self.assertEqual(ssh2_fingerprint_blob(silly_blob, "md5"),
                         b'foo '
                         b'5f:5f:97:94:97:be:01:5c:f6:3f:e3:6e:55:46:ea:52')

        # A key blob without even a valid algorithm-name string at the start.
        very_silly_blob = b'foo'
        self.assertEqual(ssh2_fingerprint_blob(very_silly_blob, "sha256"),
                         b'SHA256:'
                         b'LCa0a2j/xo/5m0U8HTBBNBNCLXBkg7+g+YpeiGJm564')
        self.assertEqual(ssh2_fingerprint_blob(very_silly_blob, "md5"),
                         b'ac:bd:18:db:4c:c2:f8:5c:ed:ef:65:4f:cc:c4:a4:d8')

        # A certified key.
        cert_blob = b64(
            'AAAAIHNzaC1lZDI1NTE5LWNlcnQtdjAxQG9wZW5zc2guY29tAAAAIJ4Ds9YwRHxs'
            'xdtUitRbZGz0MgKGZSBVrTHI1AbvetofAAAAIMt0/CMBL+64GQ/r/JyGxo6oHs86'
            'i9bOHhMJYbDbxEJfAAAAAAAAAG8AAAABAAAAAmlkAAAADAAAAAh1c2VybmFtZQAA'
            'AAAAAAPoAAAAAAAAB9AAAAAAAAAAAAAAAAAAAAE+AAAAIHNzaC1lZDI1NTE5LWNl'
            'cnQtdjAxQG9wZW5zc2guY29tAAAAICl5MiUNt8hoAAHT0v00JYOkWe2UW31+Qq5Q'
            'HYKWGyVjAAAAIMUJEFAmSV/qtoxSmVOHUgTMKYjqkDy8fTfsfCKV+sN7AAAAAAAA'
            'AG8AAAABAAAAAmlkAAAAEgAAAA5kb2Vzbid0IG1hdHRlcgAAAAAAAAPoAAAAAAAA'
            'B9AAAAAAAAAAAAAAAAAAAAAzAAAAC3NzaC1lZDI1NTE5AAAAIMUJEFAmSV/qtoxS'
            'mVOHUgTMKYjqkDy8fTfsfCKV+sN7AAAAUwAAAAtzc2gtZWQyNTUxOQAAAEAXbRz3'
            'lBmoU4FVge29jn04MfubF6U0CoPG1nbeZSgDN2iz7qtZ75XIk5O/Z/W9nA8jwsiz'
            'iSEMItjvR7HEN8MIAAAAUwAAAAtzc2gtZWQyNTUxOQAAAECszhkY8bUbSCjmHEMP'
            'LjcOX6OaeBzPIYYYXJzpLn+m+CIwDXRIxyvON5/d/TomgAFNJutfOEsqIzy5OAvl'
            'p5IO')
        self.assertEqual(ssh2_fingerprint_blob(cert_blob, "sha256"),
                         b'ssh-ed25519-cert-v01@openssh.com 255 '
                         b'SHA256:42JaqhHUNa5CoKxGWqtKXF0Awz7b0aPrtgBZ9VLLHfY')
        self.assertEqual(ssh2_fingerprint_blob(cert_blob, "md5"),
                         b'ssh-ed25519-cert-v01@openssh.com 255 '
                         b'8e:40:00:e0:1f:4a:9c:b3:c8:e9:05:59:04:03:44:b3')
        self.assertEqual(ssh2_fingerprint_blob(cert_blob, "sha256-cert"),
                         b'ssh-ed25519-cert-v01@openssh.com 255 '
                         b'SHA256:W/+SDEg7S+/dAn4SrodJ2c8bYvt13XXA7YYlQ6E8R5U')
        self.assertEqual(ssh2_fingerprint_blob(cert_blob, "md5-cert"),
                         b'ssh-ed25519-cert-v01@openssh.com 255 '
                         b'03:cf:aa:8e:aa:c3:a0:97:bb:2e:7e:57:9d:08:b5:be')


    def testAES(self):
        # My own test cases, generated by a mostly independent
        # reference implementation of AES in Python. ('Mostly'
        # independent in that it was written by me.)

        def vector(cipherbase, key, iv, plaintext, ciphertext):
            for cipher in get_implementations(cipherbase):
                c = ssh_cipher_new(cipher)
                if c is None: return # skip test if HW AES not available
                ssh_cipher_setkey(c, key)
                ssh_cipher_setiv(c, iv)
                self.assertEqualBin(
                    ssh_cipher_encrypt(c, plaintext), ciphertext)
                ssh_cipher_setiv(c, iv)
                self.assertEqualBin(
                    ssh_cipher_decrypt(c, ciphertext), plaintext)

        # Tests of CBC mode.

        key = unhex(
            '98483c6eb40b6c31a448c22a66ded3b5e5e8d5119cac8327b655c8b5c4836489')
        iv = unhex('38f87b0b9b736160bfc0cbd8447af6ee')
        plaintext = unhex('''
        ee16271827b12d828f61d56fddccc38ccaa69601da2b36d3af1a34c51947b71a
        362f05e07bf5e7766c24599799b252ad2d5954353c0c6ca668c46779c2659c94
        8df04e4179666e335470ff042e213c8bcff57f54842237fbf9f3c7e6111620ac
        1c007180edd25f0e337c2a49d890a7173f6b52d61e3d2a21ddc8e41513a0e825
        afd5932172270940b01014b5b7fb8495946151520a126518946b44ea32f9b2a9
        ''')

        vector('aes128_cbc', key[:16], iv, plaintext, unhex('''
        547ee90514cb6406d5bb00855c8092892c58299646edda0b4e7c044247795c8d
        3c3eb3d91332e401215d4d528b94a691969d27b7890d1ae42fe3421b91c989d5
        113fefa908921a573526259c6b4f8e4d90ea888e1d8b7747457ba3a43b5b79b9
        34873ebf21102d14b51836709ee85ed590b7ca618a1e884f5c57c8ea73fe3d0d
        6bf8c082dd602732bde28131159ed0b6e9cf67c353ffdd010a5a634815aaa963'''))

        vector('aes192_cbc', key[:24], iv, plaintext, unhex('''
        e3dee5122edd3fec5fab95e7db8c784c0cb617103e2a406fba4ae3b4508dd608
        4ff5723a670316cc91ed86e413c11b35557c56a6f5a7a2c660fc6ee603d73814
        73a287645be0f297cdda97aef6c51faeb2392fec9d33adb65138d60f954babd9
        8ee0daab0d1decaa8d1e07007c4a3c7b726948025f9fb72dd7de41f74f2f36b4
        23ac6a5b4b6b39682ec74f57d9d300e547f3c3e467b77f5e4009923b2f94c903'''))

        vector('aes256_cbc', key[:32], iv, plaintext, unhex('''
        088c6d4d41997bea79c408925255266f6c32c03ea465a5f607c2f076ec98e725
        7e0beed79609b3577c16ebdf17d7a63f8865278e72e859e2367de81b3b1fe9ab
        8f045e1d008388a3cfc4ff87daffedbb47807260489ad48566dbe73256ce9dd4
        ae1689770a883b29695928f5983f33e8d7aec4668f64722e943b0b671c365709
        dfa86c648d5fb00544ff11bd29121baf822d867e32da942ba3a0d26299bcee13'''))

        # Tests of SDCTR mode, one with a random IV and one with an IV
        # about to wrap round. More vigorous tests of IV carry and
        # wraparound behaviour are in the testAESSDCTR method.

        sdctrIVs = [
            unhex('38f87b0b9b736160bfc0cbd8447af6ee'),
            unhex('fffffffffffffffffffffffffffffffe'),
        ]

        vector('aes128_ctr', key[:16], sdctrIVs[0], plaintext[:64], unhex('''
        d0061d7b6e8c4ef4fe5614b95683383f46cdd2766e66b6fb0b0f0b3a24520b2d
        15d869b06cbf685ede064bcf8fb5fb6726cfd68de7016696a126e9e84420af38'''))
        vector('aes128_ctr', key[:16], sdctrIVs[1], plaintext[:64], unhex('''
        49ac67164fd9ce8701caddbbc9a2b06ac6524d4aa0fdac95253971974b8f3bc2
        bb8d7c970f6bcd79b25218cc95582edf7711aae2384f6cf91d8d07c9d9b370bc'''))

        vector('aes192_ctr', key[:24], sdctrIVs[0], plaintext[:64], unhex('''
        0baa86acbe8580845f0671b7ebad4856ca11b74e5108f515e34e54fa90f87a9a
        c6eee26686253c19156f9be64957f0dbc4f8ecd7cabb1f4e0afefe33888faeec'''))
        vector('aes192_ctr', key[:24], sdctrIVs[1], plaintext[:64], unhex('''
        2da1791250100dc0d1461afe1bbfad8fa0320253ba5d7905d837386ba0a3a41f
        01965c770fcfe01cf307b5316afb3981e0e4aa59a6e755f0a5784d9accdc52be'''))

        vector('aes256_ctr', key[:32], sdctrIVs[0], plaintext[:64], unhex('''
        49c7b284222d408544c770137b6ef17ef770c47e24f61fa66e7e46cae4888882
        f980a0f2446956bf47d2aed55ebd2e0694bfc46527ed1fd33efe708fec2f8b1f'''))
        vector('aes256_ctr', key[:32], sdctrIVs[1], plaintext[:64], unhex('''
        f1d013c3913ccb4fc0091e25d165804480fb0a1d5c741bf012bba144afda6db2
        c512f3942018574bd7a8fdd88285a73d25ef81e621aebffb6e9b8ecc8e2549d4'''))

    def testAESSDCTR(self):
        # A thorough test of the IV-incrementing component of SDCTR
        # mode. We set up an AES-SDCTR cipher object with the given
        # input IV; we encrypt two all-zero blocks, expecting the
        # return values to be the AES-ECB encryptions of the input IV
        # and the incremented version. Then we decrypt each of them by
        # feeding them to an AES-CBC cipher object with its IV set to
        # zero.

        def increment(keylen, suffix, iv):
            key = b'\xab' * (keylen//8)
            sdctr = ssh_cipher_new("aes{}_ctr_{}".format(keylen, suffix))
            if sdctr is None: return # skip test if HW AES not available
            ssh_cipher_setkey(sdctr, key)
            cbc = ssh_cipher_new("aes{}_cbc_{}".format(keylen, suffix))
            ssh_cipher_setkey(cbc, key)

            ssh_cipher_setiv(sdctr, iv)
            ec0 = ssh_cipher_encrypt(sdctr, b'\x00' * 16)
            ec1 = ssh_cipher_encrypt(sdctr, b'\x00' * 16)
            ssh_cipher_setiv(cbc, b'\x00' * 16)
            dc0 = ssh_cipher_decrypt(cbc, ec0)
            ssh_cipher_setiv(cbc, b'\x00' * 16)
            dc1 = ssh_cipher_decrypt(cbc, ec1)
            self.assertEqualBin(iv, dc0)
            return dc1

        def test(keylen, suffix, ivInteger):
            mask = (1 << 128) - 1
            ivInteger &= mask
            ivBinary = unhex("{:032x}".format(ivInteger))
            ivIntegerInc = (ivInteger + 1) & mask
            ivBinaryInc = unhex("{:032x}".format((ivIntegerInc)))
            actualResult = increment(keylen, suffix, ivBinary)
            if actualResult is not None:
                self.assertEqualBin(actualResult, ivBinaryInc)

        # Check every input IV you can make by gluing together 32-bit
        # pieces of the form 0, 1 or -1. This should test all the
        # places where carry propagation within the 128-bit integer
        # can go wrong.
        #
        # We also test this at all three AES key lengths, in case the
        # core cipher routines are written separately for each one.

        for suffix in get_aes_impls():
            for keylen in [128, 192, 256]:
                hexTestValues = ["00000000", "00000001", "ffffffff"]
                for ivHexBytes in itertools.product(*([hexTestValues] * 4)):
                    ivInteger = int("".join(ivHexBytes), 16)
                    test(keylen, suffix, ivInteger)

    def testAESParallelism(self):
        # Since at least one of our implementations of AES works in
        # parallel, here's a test that CBC decryption works the same
        # way no matter how the input data is divided up.

        # A pile of conveniently available random-looking test data.
        test_ciphertext = ssh2_mpint(last(fibonacci_scattered(14)))
        test_ciphertext += b"x" * (15 & -len(test_ciphertext)) # pad to a block

        # Test key and IV.
        test_key = b"foobarbazquxquuxFooBarBazQuxQuux"
        test_iv = b"FOOBARBAZQUXQUUX"

        for keylen in [128, 192, 256]:
            decryptions = []

            for suffix in get_aes_impls():
                c = ssh_cipher_new("aes{:d}_cbc_{}".format(keylen, suffix))
                if c is None: continue
                ssh_cipher_setkey(c, test_key[:keylen//8])
                for chunklen in range(16, 16*12, 16):
                    ssh_cipher_setiv(c, test_iv)
                    decryption = b""
                    for pos in range(0, len(test_ciphertext), chunklen):
                        chunk = test_ciphertext[pos:pos+chunklen]
                        decryption += ssh_cipher_decrypt(c, chunk)
                    decryptions.append(decryption)

            for d in decryptions:
                self.assertEqualBin(d, decryptions[0])

    def testCRC32(self):
        # Check the effect of every possible single-byte input to
        # crc32_update. In the traditional implementation with a
        # 256-word lookup table, this exercises every table entry; in
        # _any_ implementation which iterates over the input one byte
        # at a time, it should be a similarly exhaustive test. (But if
        # a more optimised implementation absorbed _more_ than 8 bits
        # at a time, then perhaps this test wouldn't be enough...)

        # It would be nice if there was a functools.iterate() which
        # would apply a function n times. Failing that, making shift1
        # accept and ignore a second argument allows me to iterate it
        # 8 times using functools.reduce.
        shift1 = lambda x, dummy=None: (x >> 1) ^ (0xEDB88320 * (x & 1))
        shift8 = lambda x: functools.reduce(shift1, [None]*8, x)

        # A small selection of choices for the other input to
        # crc32_update, just to check linearity.
        test_prior_values = [0, 0xFFFFFFFF, 0x45CC1F6A, 0xA0C4ADCF, 0xD482CDF1]

        for prior in test_prior_values:
            prior_shifted = shift8(prior)
            for i in range(256):
                exp = shift8(i) ^ prior_shifted
                self.assertEqual(crc32_update(prior, struct.pack("B", i)), exp)

                # Check linearity of the _reference_ implementation, while
                # we're at it!
                self.assertEqual(shift8(i ^ prior), exp)

    def testCRCDA(self):
        def pattern(badblk, otherblks, pat):
            # Arrange copies of the bad block in a pattern
            # corresponding to the given bit string.
            retstr = b""
            while pat != 0:
                retstr += (badblk if pat & 1 else next(otherblks))
                pat >>= 1
            return retstr

        def testCases(pat):
            badblock = b'muhahaha' # the block we'll maliciously repeat

            # Various choices of the other blocks, including all the
            # same, all different, and all different but only in the
            # byte at one end.
            for otherblocks in [
                    itertools.repeat(b'GoodData'),
                    (struct.pack('>Q', i) for i in itertools.count()),
                    (struct.pack('<Q', i) for i in itertools.count())]:
                yield pattern(badblock, otherblocks, pat)

        def positiveTest(pat):
            for data in testCases(pat):
                self.assertTrue(crcda_detect(data, ""))
                self.assertTrue(crcda_detect(data[8:], data[:8]))

        def negativeTest(pat):
            for data in testCases(pat):
                self.assertFalse(crcda_detect(data, ""))
                self.assertFalse(crcda_detect(data[8:], data[:8]))

        # Tests of successful attack detection, derived by taking
        # multiples of the CRC polynomial itself.
        #
        # (The CRC32 polynomial is usually written as 0xEDB88320.
        # That's in bit-reversed form, but then, that's the form we
        # need anyway for these patterns. But it's also missing the
        # leading term - really, 0xEDB88320 is the value you get by
        # reducing X^32 modulo the real poly, i.e. the value you put
        # back in to the CRC to compensate for an X^32 that's just
        # been shifted out. If you put that bit back on - at the
        # bottom, because of the bit-reversal - you get the less
        # familiar-looking 0x1db710641.)
        positiveTest(0x1db710641) # the CRC polynomial P itself
        positiveTest(0x26d930ac3) # (X+1) * P
        positiveTest(0xbdbdf21cf) # (X^3+X^2+X+1) * P
        positiveTest(0x3a66a39b653f6889d)
        positiveTest(0x170db3167dd9f782b9765214c03e71a18f685b7f3)
        positiveTest(0x1751997d000000000000000000000000000000001)
        positiveTest(0x800000000000000000000000000000000f128a2d1)

        # Tests of non-detection.
        negativeTest(0x1db711a41)
        negativeTest(0x3a66a39b453f6889d)
        negativeTest(0x170db3167dd9f782b9765214c03e71b18f685b7f3)
        negativeTest(0x1751997d000000000000000000000001000000001)
        negativeTest(0x800000000000002000000000000000000f128a2d1)

    def testAuxEncryptFns(self):
        # Test helper functions such as aes256_encrypt_pubkey. The
        # test cases are all just things I made up at random, and the
        # expected outputs are generated by running PuTTY's own code;
        # this doesn't independently check them against any other
        # implementation, but it at least means we're protected
        # against code reorganisations changing the behaviour from
        # what it was before.

        p = b'three AES blocks, or six DES, of arbitrary input'

        k = b'thirty-two-byte aes-256 test key'
        iv = b'\0' * 16
        c = unhex('7b112d00c0fc95bc13fcdacfd43281bf'
                  'de9389db1bbcfde79d59a303d41fd2eb'
                  '0955c9477ae4ee3a4d6c1fbe474c0ef6')
        self.assertEqualBin(aes256_encrypt_pubkey(k, iv, p), c)
        self.assertEqualBin(aes256_decrypt_pubkey(k, iv, c), p)

        # same k as in the previous case
        iv = unhex('0102030405060708090a0b0c0d0e0f10')
        c = unhex('9e9c8a91b739677b834397bdd8e70c05'
                  'c3e2cf6cce68d376d798a59848621c6d'
                  '42b9e7101260a438daadd7b742875a36')
        self.assertEqualBin(aes256_encrypt_pubkey(k, iv, p), c)
        self.assertEqualBin(aes256_decrypt_pubkey(k, iv, c), p)

        k = b'3des with keys distinct.'
        iv = b'randomIV'
        c = unhex('be81ff840d885869a54d63b03d7cd8db'
                  'd39ab875e5f7b9da1081f8434cb33c47'
                  'dee5bcd530a3f6c13a9fc73e321a843a')
        self.assertEqualBin(des3_encrypt_pubkey_ossh(k, iv, p), c)
        self.assertEqualBin(des3_decrypt_pubkey_ossh(k, iv, c), p)

        k = b'3des, 2keys only'
        c = unhex('0b845650d73f615cf16ee3ed20535b5c'
                  'd2a8866ee628547bbdad916e2b4b9f19'
                  '67c15bde33c5b03ff7f403b4f8cf2364')
        self.assertEqualBin(des3_encrypt_pubkey(k, p), c)
        self.assertEqualBin(des3_decrypt_pubkey(k, c), p)

        k = b'7 bytes'
        c = unhex('5cac9999cffc980a1d1184d84b71c8cb'
                  '313d12a1d25a7831179aeb11edaca5ad'
                  '9482b224105a61c27137587620edcba8')
        self.assertEqualBin(des_encrypt_xdmauth(k, p), c)
        self.assertEqualBin(des_decrypt_xdmauth(k, c), p)

    def testSSHCiphers(self):
        # Test all the SSH ciphers we support, on the same principle
        # as testAuxCryptFns that we should have test cases to verify
        # that things still work the same today as they did yesterday.

        p = b'64 bytes of test input data, enough to check any cipher mode xyz'
        k = b'sixty-four bytes of test key data, enough to key any cipher pqrs'
        iv = b'16 bytes of IV w'

        ciphers = [
            ("3des_ctr",      24,    8, False, unhex('83c17a29250d3d4fa81250fc0362c54e40456936445b77709a30fccf8b983d57129a969c59070d7c2977f3d25dd7d71163687c7b3cd2edb0d07514e6c77479f5')),
            ("3des_ssh2",     24,    8, True,  unhex('d5f1cc25b8fbc62decc74b432344de674f7249b2e38871f764411eaae17a1097396bd97b66a1e4d49f08c219acaef2a483198ce837f75cc1ef67b37c2432da3e')),
            ("3des_ssh1",     24,    8, False, unhex('d5f1cc25b8fbc62de63590b9b92344adf6dd72753273ff0fb32d4dbc6af858529129f34242f3d557eed3a5c84204eb4f868474294964cf70df5d8f45dfccfc45')),
            ("des_cbc",        8,    8, True,  unhex('051524e77fb40e109d9fffeceacf0f28c940e2f8415ddccc117020bdd2612af5036490b12085d0e46129919b8e499f51cb82a4b341d7a1a1ea3e65201ef248f6')),
            ("aes256_ctr",    32,   16, False, unhex('b87b35e819f60f0f398a37b05d7bcf0b04ad4ebe570bd08e8bfa8606bafb0db2cfcd82baf2ccceae5de1a3c1ae08a8b8fdd884fdc5092031ea8ce53333e62976')),
            ("aes256_cbc",    32,   16, True,  unhex('381cbb2fbcc48118d0094540242bd990dd6af5b9a9890edd013d5cad2d904f34b9261c623a452f32ea60e5402919a77165df12862742f1059f8c4a862f0827c5')),
            ("aes192_ctr",    24,   16, False, unhex('06bcfa7ccf075d723e12b724695a571a0fad67c56287ea609c410ac12749c51bb96e27fa7e1c7ea3b14792bbbb8856efb0617ebec24a8e4a87340d820cf347b8')),
            ("aes192_cbc",    24,   16, True,  unhex('ac97f8698170f9c05341214bd7624d5d2efef8311596163dc597d9fe6c868971bd7557389974612cbf49ea4e7cc6cc302d4cc90519478dd88a4f09b530c141f3')),
            ("aes128_ctr",    16,   16, False, unhex('0ad4ddfd2360ec59d77dcb9a981f92109437c68c5e7f02f92017d9f424f89ab7850473ac0e19274125e740f252c84ad1f6ad138b6020a03bdaba2f3a7378ce1e')),
            ("aes128_cbc",    16,   16, True,  unhex('36de36917fb7955a711c8b0bf149b29120a77524f393ae3490f4ce5b1d5ca2a0d7064ce3c38e267807438d12c0e40cd0d84134647f9f4a5b11804a0cc5070e62')),
            ("blowfish_ctr",  32,    8, False, unhex('079daf0f859363ccf72e975764d709232ec48adc74f88ccd1f342683f0bfa89ca0e8dbfccc8d4d99005d6b61e9cc4e6eaa2fd2a8163271b94bf08ef212129f01')),
            ("blowfish_ssh2", 16,    8, True,  unhex('e986b7b01f17dfe80ee34cac81fa029b771ec0f859ae21ae3ec3df1674bc4ceb54a184c6c56c17dd2863c3e9c068e76fd9aef5673465995f0d648b0bb848017f')),
            ("blowfish_ssh1", 32,    8, True,  unhex('d44092a9035d895acf564ba0365d19570fbb4f125d5a4fd2a1812ee6c8a1911a51bb181fbf7d1a261253cab71ee19346eb477b3e7ecf1d95dd941e635c1a4fbf')),
            ("arcfour256",    32, None, False, unhex('db68db4cd9bbc1d302cce5919ff3181659272f5d38753e464b3122fc69518793fe15dd0fbdd9cd742bd86c5e8a3ae126c17ecc420bd2d5204f1a24874d00fda3')),
            ("arcfour128",    16, None, False, unhex('fd4af54c5642cb29629e50a15d22e4944e21ffba77d0543b27590eafffe3886686d1aefae0484afc9e67edc0e67eb176bbb5340af1919ea39adfe866d066dd05')),
        ]

        for algbase, keylen, ivlen, simple_cbc, c in ciphers:
            for alg in get_implementations(algbase):
                cipher = ssh_cipher_new(alg)
                if cipher is None:
                    continue # hardware-accelerated cipher not available

                ssh_cipher_setkey(cipher, k[:keylen])
                if ivlen is not None:
                    ssh_cipher_setiv(cipher, iv[:ivlen])
                self.assertEqualBin(ssh_cipher_encrypt(cipher, p), c)

                ssh_cipher_setkey(cipher, k[:keylen])
                if ivlen is not None:
                    ssh_cipher_setiv(cipher, iv[:ivlen])
                self.assertEqualBin(ssh_cipher_decrypt(cipher, c), p)

                if simple_cbc:
                    # CBC ciphers (other than the three-layered CBC used
                    # by SSH-1 3DES) have more specific semantics for
                    # their IV than 'some kind of starting state for the
                    # cipher mode': the IV is specifically supposed to
                    # represent the previous block of ciphertext. So we
                    # can check that, by supplying the IV _as_ a
                    # ciphertext block via a call to decrypt(), and seeing
                    # if that causes our test ciphertext to decrypt the
                    # same way as when we provided the same IV via
                    # setiv().
                    ssh_cipher_setkey(cipher, k[:keylen])
                    ssh_cipher_decrypt(cipher, iv[:ivlen])
                    self.assertEqualBin(ssh_cipher_decrypt(cipher, c), p)

    def testChaCha20Poly1305(self):
        # A test case of this cipher taken from a real connection to
        # OpenSSH.
        key = unhex('49e67c5ae596ea7f230e266538d0e373'
                    '177cc8fe08ff7b642c22d736ca975655'
                    'c3fb639010fd297ca03c36b20a182ef4'
                    '0e1272f0c54251c175546ee00b150805')
        len_p = unhex('00000128')
        len_c = unhex('3ff3677b')
        msg_p = unhex('0807000000020000000f736572766572'
                      '2d7369672d616c6773000000db737368'
                      '2d656432353531392c736b2d7373682d'
                      '65643235353139406f70656e7373682e'
                      '636f6d2c7373682d7273612c7273612d'
                      '736861322d3235362c7273612d736861'
                      '322d3531322c7373682d6473732c6563'
                      '6473612d736861322d6e697374703235'
                      '362c65636473612d736861322d6e6973'
                      '74703338342c65636473612d73686132'
                      '2d6e697374703532312c736b2d656364'
                      '73612d736861322d6e69737470323536'
                      '406f70656e7373682e636f6d2c776562'
                      '617574686e2d736b2d65636473612d73'
                      '6861322d6e69737470323536406f7065'
                      '6e7373682e636f6d0000001f7075626c'
                      '69636b65792d686f7374626f756e6440'
                      '6f70656e7373682e636f6d0000000130'
                      'c34aaefcafae6fc2')
        msg_c = unhex('bf587eabf385b1281fa9c755d8515dfd'
                      'c40cb5e993b346e722dce48b1741b4e5'
                      'ce9ae075f6df0a1d2f72f94f73570125'
                      '7011630bbb0c7febd767184c0d5aa810'
                      '47cbce82972129a234b8ac5fc5f2b5be'
                      '9264baca6d13ff3c9813a61e1f23468f'
                      '31964b60fc3f0888a227f02c737b2d27'
                      'b7ae3cd60ede17533863a5bb6bb2d60a'
                      'c998ccd27e8ba56259f676ed04749fad'
                      '4114678fb871add3a40625110637947c'
                      'e91459811622fd3d1fa7eb7efad4b1e8'
                      '97f3e860473935d3d8df0679a8b0df85'
                      'aa4124f2d9ac7207abd10719f465c9ed'
                      '859d2b03bde55315b9024f660ba8d63a'
                      '64e0beb81e532201df830a52cf221484'
                      '18d0c4c7da242346161d7320ac534cb5'
                      'c6b6fec905ee5f424becb9f97c3afbc5'
                      '5ef4ba369e61bce847158f0dc5bd7227'
                      '3b8693642db36f87')
        mac = unhex('09757178642dfc9f2c38ac5999e0fcfd')
        seqno = 3
        c = ssh_cipher_new('chacha20_poly1305')
        m = ssh2_mac_new('poly1305', c)
        c.setkey(key)
        self.assertEqualBin(c.encrypt_length(len_p, seqno), len_c)
        self.assertEqualBin(c.encrypt(msg_p), msg_c)
        m.start()
        m.update(ssh_uint32(seqno) + len_c + msg_c)
        self.assertEqualBin(m.genresult(), mac)
        self.assertEqualBin(c.decrypt_length(len_c, seqno), len_p)
        self.assertEqualBin(c.decrypt(msg_c), msg_p)

    def testRSAKex(self):
        # Round-trip test of the RSA key exchange functions, plus a
        # hardcoded plain/ciphertext pair to guard against the
        # behaviour accidentally changing.
        def blobs(n, e, d, p, q, iqmp):
            # For RSA kex, the public blob is formatted exactly like
            # any other SSH-2 RSA public key. But there's no private
            # key blob format defined by the protocol, so for the
            # purposes of making a test RSA private key, we borrow the
            # function we already had that decodes one out of the wire
            # format used in the SSH-1 agent protocol.
            pubblob = ssh_string(b"ssh-rsa") + ssh2_mpint(e) + ssh2_mpint(n)
            privblob = (ssh_uint32(nbits(n)) + ssh1_mpint(n) + ssh1_mpint(e) +
                        ssh1_mpint(d) + ssh1_mpint(iqmp) +
                        ssh1_mpint(q) + ssh1_mpint(p))
            return pubblob, privblob

        # Parameters for a test key.
        p = 0xf49e4d21c1ec3d1c20dc8656cc29aadb2644a12c98ed6c81a6161839d20d398d
        q = 0xa5f0bc464bf23c4c83cf17a2f396b15136fbe205c07cb3bb3bdb7ed357d1cd13
        n = p*q
        e = 37
        d = int(mp_invert(e, (p-1)*(q-1)))
        iqmp = int(mp_invert(q, p))
        assert iqmp * q % p == 1
        assert d * e % (p-1) == 1
        assert d * e % (q-1) == 1

        pubblob, privblob = blobs(n, e, d, p, q, iqmp)

        pubkey = ssh_rsakex_newkey(pubblob)
        privkey = get_rsa_ssh1_priv_agent(privblob)

        plain = 0x123456789abcdef
        hashalg = 'md5'
        with queued_random_data(64, "rsakex encrypt test"):
            cipher = ssh_rsakex_encrypt(pubkey, hashalg, ssh2_mpint(plain))
        decoded = ssh_rsakex_decrypt(privkey, hashalg, cipher)
        self.assertEqual(int(decoded), plain)
        self.assertEqualBin(cipher, unhex(
            '34277d1060dc0a434d98b4239de9cec59902a4a7d17a763587cdf8c25d57f51a'
            '7964541892e7511798e61dd78429358f4d6a887a50d2c5ebccf0e04f48fc665c'
        ))

    def testMontgomeryKexLowOrderPoints(self):
        # List of all the bad input values for Curve25519 which can
        # end up generating a zero output key. You can find the first
        # five (the ones in canonical representation, i.e. in
        # [0,2^255-19)) by running
        # find_montgomery_power2_order_x_values(curve25519.p, curve25519.a)
        # and then encoding the results little-endian.
        bad_keys_25519 = [
            "0000000000000000000000000000000000000000000000000000000000000000",
            "0100000000000000000000000000000000000000000000000000000000000000",
            "5f9c95bca3508c24b1d0b1559c83ef5b04445cc4581c8e86d8224eddd09f1157",
            "e0eb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b800",
            "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",

            # Input values less than 2^255 are reduced mod p, so those
            # of the above values which are still in that range when
            # you add 2^255-19 to them should also be caught.
            "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
            "eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",

            # Input values are reduced mod 2^255 before reducing mod
            # p. So setting the high-order bit of any of the above 7
            # values should also lead to rejection, because it will be
            # stripped off and then the value will be recognised as
            # one of the above.
            "0000000000000000000000000000000000000000000000000000000000000080",
            "0100000000000000000000000000000000000000000000000000000000000080",
            "5f9c95bca3508c24b1d0b1559c83ef5b04445cc4581c8e86d8224eddd09f11d7",
            "e0eb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b880",
            "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            "eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        ]

        # Same for Curve448, found by the analogous eccref function call
        # find_montgomery_power2_order_x_values(curve448.p, curve448.a)
        bad_keys_448 = [
            # The first three are the bad values in canonical
            # representationm. In Curve448 these are just 0, 1 and -1.
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000',
            '0100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000',
            'fefffffffffffffffffffffffffffffffffffffffffffffffffffffffeffffffffffffffffffffffffffffffffffffffffffffffffffffff',

            # As with Curve25519, we must also include values in
            # non-canonical representation that reduce to one of the
            # above mod p.
            'fffffffffffffffffffffffffffffffffffffffffffffffffffffffffeffffffffffffffffffffffffffffffffffffffffffffffffffffff',
            '00000000000000000000000000000000000000000000000000000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff'

            # But that's all, because Curve448 fits neatly into a
            # whole number of bytes, so there's no secondary reduction
            # mod a power of 2.
        ]

        with random_prng("doesn't matter"):
            ecdh25519 = ecdh_key_new('curve25519', False)
            ecdh448 = ecdh_key_new('curve448', False)
        for pub in bad_keys_25519:
            key = ecdh_key_getkey(ecdh25519, unhex(pub))
            self.assertEqual(key, None)
        for pub in bad_keys_448:
            key = ecdh_key_getkey(ecdh448, unhex(pub))
            self.assertEqual(key, None)

    def testPRNG(self):
        hashalg = 'sha256'
        seed = b"hello, world"
        entropy = b'1234567890' * 100

        # Replicate the generation of some random numbers. to ensure
        # they really are the hashes of what they're supposed to be.
        pr = prng_new(hashalg)
        prng_seed_begin(pr)
        prng_seed_update(pr, seed)
        prng_seed_finish(pr)
        data1 = prng_read(pr, 128)
        data2 = prng_read(pr, 127) # a short read shouldn't confuse things
        prng_add_entropy(pr, 0, entropy) # forces a reseed
        data3 = prng_read(pr, 128)

        le128 = lambda x: le_integer(x, 128)

        key1 = hash_str(hashalg, b'R' + seed)
        expected_data1 = b''.join(
            hash_str(hashalg, key1 + b'G' + le128(counter))
            for counter in range(4))
        # After prng_read finishes, we expect the PRNG to have
        # automatically reseeded itself, so that if its internal state
        # is revealed then the previous output can't be reconstructed.
        key2 = hash_str(hashalg, key1 + b'R')
        expected_data2 = b''.join(
            hash_str(hashalg, key2 + b'G' + le128(counter))
            for counter in range(4,8))
        # There will have been another reseed after the second
        # prng_read, and then another due to the entropy.
        key3 = hash_str(hashalg, key2 + b'R')
        key4 = hash_str(hashalg, key3 + b'R' + hash_str(hashalg, entropy))
        expected_data3 = b''.join(
            hash_str(hashalg, key4 + b'G' + le128(counter))
            for counter in range(8,12))

        self.assertEqualBin(data1, expected_data1)
        self.assertEqualBin(data2, expected_data2[:127])
        self.assertEqualBin(data3, expected_data3)

    def testHashPadding(self):
        # A consistency test for hashes that use MD5/SHA-1/SHA-2 style
        # padding of the message into a whole number of fixed-size
        # blocks. We test-hash a message of every length up to twice
        # the block length, to make sure there's no off-by-1 error in
        # the code that decides how much padding to put on.

        # Source: generated using Python hashlib as an independent
        # implementation. The function below will do it, called with
        # parameters such as (hashlib.sha256,128).
        #
        # def gen_testcase(hashclass, maxlen):
        #    return hashclass(b''.join(hashclass(text[:i]).digest()
        #             for i in range(maxlen))).hexdigest()

        text = """
Lorem ipsum dolor sit amet, consectetur adipisicing elit, sed do
eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad
minim veniam, quis nostrud exercitation ullamco laboris nisi ut
aliquip ex ea commodo consequat. Duis aute irure dolor in
reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla
pariatur. Excepteur sint occaecat cupidatat non proident, sunt in
culpa qui officia deserunt mollit anim id est laborum.
        """.replace('\n', ' ').strip()

        def test(hashname, maxlen, expected):
            assert len(text) >= maxlen
            buf = b''.join(hash_str(hashname, text[:i])
                           for i in range(maxlen))
            self.assertEqualBin(hash_str(hashname, buf), unhex(expected))

        test('md5', 128, '8169d766cc3b8df182b3ce756ae19a15')
        test('sha1', 128, '3691759577deb3b70f427763a9c15acb9dfc0259')
        test('sha256', 128, 'ec539c4d678412c86c13ee4eb9452232'
             '35d4eed3368d876fdf10c9df27396640')
        test('sha512', 256,
             'cb725b4b4ec0ac1174d69427b4d97848b7db4fc01181f99a8049a4d721862578'
             'f91e026778bb2d389a9dd88153405189e6ba438b213c5387284103d2267fd055'
        )

    def testDSA(self):
        p = 0xe93618c54716992ffd54e79df6e1b0edd517f7bbe4a49d64631eb3efe8105f676e8146248cfb4f05720862533210f0c2ab0f9dd61dbc0e5195200c4ebd95364b
        q = 0xf3533bcece2e164ca7c5ce64bc1e395e9a15bbdd
        g = 0x5ac9d0401c27d7abfbc5c17cdc1dc43323cd0ef18b79e1909bdace6d17af675a10d37dde8bd8b70e72a8666592216ccb00614629c27e870e4fbf393b812a9f05
        y = 0xac3ddeb22d65a5a2ded4a28418b2a748d8e5e544ba5e818c137d7b042ef356b0ef6d66cfca0b3ab5affa2969522e7b07bee60562fa4869829a5afce0ad0c4cd0
        x = 0x664f8250b7f1a5093047fe0c7fe4b58e46b73295
        pubblob = ssh_string(b"ssh-dss") + b"".join(map(ssh2_mpint, [p,q,g,y]))
        privblob = ssh2_mpint(x)
        pubkey = ssh_key_new_pub('dsa', pubblob)
        privkey = ssh_key_new_priv('dsa', pubblob, privblob)

        sig = ssh_key_sign(privkey, b"hello, world", 0)
        self.assertTrue(ssh_key_verify(pubkey, sig, b"hello, world"))
        self.assertFalse(ssh_key_verify(pubkey, sig, b"hello, again"))

        badsig0 = unhex('{:040x}{:040x}'.format(1, 0))
        badsigq = unhex('{:040x}{:040x}'.format(1, q))
        self.assertFalse(ssh_key_verify(pubkey, badsig0, "hello, world"))
        self.assertFalse(ssh_key_verify(pubkey, badsigq, "hello, world"))
        self.assertFalse(ssh_key_verify(pubkey, badsig0, "hello, again"))
        self.assertFalse(ssh_key_verify(pubkey, badsigq, "hello, again"))

    def testRFC6979(self):
        # The test case described in detail in RFC 6979 section A.1.
        # We can't actually do the _signature_ for this, because it's
        # based on ECDSA over a finite field of characteristic 2, and
        # we only support prime-order fields. But we don't need to do
        # full ECDSA, only generate the same deterministic nonce that
        # the test case expects.
        k = rfc6979('sha256',
                    0x4000000000000000000020108A2E0CC0D99F8A5EF,
                    0x09A4D6792295A7F730FC3F2B49CBC0F62E862272F, "sample")
        self.assertEqual(int(k), 0x23AF4074C90A02B3FE61D286D5C87F425E6BDD81B)

        # Selected test cases from the rest of Appendix A.
        #
        # We can only use test cases for which we have the appropriate
        # hash function, so I've left out the test cases based on
        # SHA-224. (We could easily implement that, but I don't think
        # it's worth it just for adding further tests of this one
        # function.) Similarly, I've omitted test cases relating to
        # ECDSA curves we don't implement: P192, P224, and all the
        # curves over power-of-2 finite fields.
        #
        # Where possible, we also test the actual signature algorithm,
        # to make sure it delivers the same entire signature as the
        # test case. This demonstrates that the rfc6979() function is
        # being called in the right way and the results are being used
        # as they should be. Here I've had to cut down the test cases
        # even further, because the RFC specifies test cases with a
        # cross product of DSA group and hash function, whereas we
        # have a fixed hash (specified by SSH) for each signature
        # algorithm. And the RFC is clear that you use the same hash
        # for nonce generation and actual signing.

        # A.2.1: 1024-bit DSA
        q = 0x996F967F6C8E388D9E28D01E205FBA957A5698B1
        x = 0x411602CB19A6CCC34494D79D98EF1E7ED5AF25F7
        k = rfc6979('sha1', q, x, "sample")
        self.assertEqual(int(k), 0x7BDB6B0FF756E1BB5D53583EF979082F9AD5BD5B)
        k = rfc6979('sha256', q, x, "sample")
        self.assertEqual(int(k), 0x519BA0546D0C39202A7D34D7DFA5E760B318BCFB)
        k = rfc6979('sha384', q, x, "sample")
        self.assertEqual(int(k), 0x95897CD7BBB944AA932DBC579C1C09EB6FCFC595)
        k = rfc6979('sha512', q, x, "sample")
        self.assertEqual(int(k), 0x09ECE7CA27D0F5A4DD4E556C9DF1D21D28104F8B)
        k = rfc6979('sha1', q, x, "test")
        self.assertEqual(int(k), 0x5C842DF4F9E344EE09F056838B42C7A17F4A6433)
        k = rfc6979('sha256', q, x, "test")
        self.assertEqual(int(k), 0x5A67592E8128E03A417B0484410FB72C0B630E1A)
        k = rfc6979('sha384', q, x, "test")
        self.assertEqual(int(k), 0x220156B761F6CA5E6C9F1B9CF9C24BE25F98CD89)
        k = rfc6979('sha512', q, x, "test")
        self.assertEqual(int(k), 0x65D2C2EEB175E370F28C75BFCDC028D22C7DBE9C)
        # The rest of the public key, for signature testing
        p = 0x86F5CA03DCFEB225063FF830A0C769B9DD9D6153AD91D7CE27F787C43278B447E6533B86B18BED6E8A48B784A14C252C5BE0DBF60B86D6385BD2F12FB763ED8873ABFD3F5BA2E0A8C0A59082EAC056935E529DAF7C610467899C77ADEDFC846C881870B7B19B2B58F9BE0521A17002E3BDD6B86685EE90B3D9A1B02B782B1779
        g = 0x07B0F92546150B62514BB771E2A0C0CE387F03BDA6C56B505209FF25FD3C133D89BBCD97E904E09114D9A7DEFDEADFC9078EA544D2E401AEECC40BB9FBBF78FD87995A10A1C27CB7789B594BA7EFB5C4326A9FE59A070E136DB77175464ADCA417BE5DCE2F40D10A46A3A3943F26AB7FD9C0398FF8C76EE0A56826A8A88F1DBD
        y = 0x5DF5E01DED31D0297E274E1691C192FE5868FEF9E19A84776454B100CF16F65392195A38B90523E2542EE61871C0440CB87C322FC4B4D2EC5E1E7EC766E1BE8D4CE935437DC11C3C8FD426338933EBFE739CB3465F4D3668C5E473508253B1E682F65CBDC4FAE93C2EA212390E54905A86E2223170B44EAA7DA5DD9FFCFB7F3B
        pubblob = ssh_string(b"ssh-dss") + b"".join(map(ssh2_mpint, [p,q,g,y]))
        privblob = ssh2_mpint(x)
        pubkey = ssh_key_new_pub('dsa', pubblob)
        privkey = ssh_key_new_priv('dsa', pubblob, privblob)
        sig = ssh_key_sign(privkey, b"sample", 0)
        # Expected output using SHA-1 as the hash in nonce
        # construction.
        r = 0x2E1A0C2562B2912CAAF89186FB0F42001585DA55
        s = 0x29EFB6B0AFF2D7A68EB70CA313022253B9A88DF5
        ref_sig = ssh_string(b"ssh-dss") + ssh_string(
            be_integer(r, 160) + be_integer(s, 160))
        self.assertEqual(sig, ref_sig)
        # And the other test string.
        sig = ssh_key_sign(privkey, b"test", 0)
        r = 0x42AB2052FD43E123F0607F115052A67DCD9C5C77
        s = 0x183916B0230D45B9931491D4C6B0BD2FB4AAF088
        ref_sig = ssh_string(b"ssh-dss") + ssh_string(
            be_integer(r, 160) + be_integer(s, 160))
        self.assertEqual(sig, ref_sig)

        # A.2.2: 2048-bit DSA
        q = 0xF2C3119374CE76C9356990B465374A17F23F9ED35089BD969F61C6DDE9998C1F
        x = 0x69C7548C21D0DFEA6B9A51C9EAD4E27C33D3B3F180316E5BCAB92C933F0E4DBC
        k = rfc6979('sha1', q, x, "sample")
        self.assertEqual(int(k), 0x888FA6F7738A41BDC9846466ABDB8174C0338250AE50CE955CA16230F9CBD53E)
        k = rfc6979('sha256', q, x, "sample")
        self.assertEqual(int(k), 0x8926A27C40484216F052F4427CFD5647338B7B3939BC6573AF4333569D597C52)
        k = rfc6979('sha384', q, x, "sample")
        self.assertEqual(int(k), 0xC345D5AB3DA0A5BCB7EC8F8FB7A7E96069E03B206371EF7D83E39068EC564920)
        k = rfc6979('sha512', q, x, "sample")
        self.assertEqual(int(k), 0x5A12994431785485B3F5F067221517791B85A597B7A9436995C89ED0374668FC)
        k = rfc6979('sha1', q, x, "test")
        self.assertEqual(int(k), 0x6EEA486F9D41A037B2C640BC5645694FF8FF4B98D066A25F76BE641CCB24BA4F)
        k = rfc6979('sha256', q, x, "test")
        self.assertEqual(int(k), 0x1D6CE6DDA1C5D37307839CD03AB0A5CBB18E60D800937D67DFB4479AAC8DEAD7)
        k = rfc6979('sha384', q, x, "test")
        self.assertEqual(int(k), 0x206E61F73DBE1B2DC8BE736B22B079E9DACD974DB00EEBBC5B64CAD39CF9F91C)
        k = rfc6979('sha512', q, x, "test")
        self.assertEqual(int(k), 0xAFF1651E4CD6036D57AA8B2A05CCF1A9D5A40166340ECBBDC55BE10B568AA0AA)
        # The rest of the public key, for signature testing
        p = 0x9DB6FB5951B66BB6FE1E140F1D2CE5502374161FD6538DF1648218642F0B5C48C8F7A41AADFA187324B87674FA1822B00F1ECF8136943D7C55757264E5A1A44FFE012E9936E00C1D3E9310B01C7D179805D3058B2A9F4BB6F9716BFE6117C6B5B3CC4D9BE341104AD4A80AD6C94E005F4B993E14F091EB51743BF33050C38DE235567E1B34C3D6A5C0CEAA1A0F368213C3D19843D0B4B09DCB9FC72D39C8DE41F1BF14D4BB4563CA28371621CAD3324B6A2D392145BEBFAC748805236F5CA2FE92B871CD8F9C36D3292B5509CA8CAA77A2ADFC7BFD77DDA6F71125A7456FEA153E433256A2261C6A06ED3693797E7995FAD5AABBCFBE3EDA2741E375404AE25B
        g = 0x5C7FF6B06F8F143FE8288433493E4769C4D988ACE5BE25A0E24809670716C613D7B0CEE6932F8FAA7C44D2CB24523DA53FBE4F6EC3595892D1AA58C4328A06C46A15662E7EAA703A1DECF8BBB2D05DBE2EB956C142A338661D10461C0D135472085057F3494309FFA73C611F78B32ADBB5740C361C9F35BE90997DB2014E2EF5AA61782F52ABEB8BD6432C4DD097BC5423B285DAFB60DC364E8161F4A2A35ACA3A10B1C4D203CC76A470A33AFDCBDD92959859ABD8B56E1725252D78EAC66E71BA9AE3F1DD2487199874393CD4D832186800654760E1E34C09E4D155179F9EC0DC4473F996BDCE6EED1CABED8B6F116F7AD9CF505DF0F998E34AB27514B0FFE7
        y = 0x667098C654426C78D7F8201EAC6C203EF030D43605032C2F1FA937E5237DBD949F34A0A2564FE126DC8B715C5141802CE0979C8246463C40E6B6BDAA2513FA611728716C2E4FD53BC95B89E69949D96512E873B9C8F8DFD499CC312882561ADECB31F658E934C0C197F2C4D96B05CBAD67381E7B768891E4DA3843D24D94CDFB5126E9B8BF21E8358EE0E0A30EF13FD6A664C0DCE3731F7FB49A4845A4FD8254687972A2D382599C9BAC4E0ED7998193078913032558134976410B89D2C171D123AC35FD977219597AA7D15C1A9A428E59194F75C721EBCBCFAE44696A499AFA74E04299F132026601638CB87AB79190D4A0986315DA8EEC6561C938996BEADF
        pubblob = ssh_string(b"ssh-dss") + b"".join(map(ssh2_mpint, [p,q,g,y]))
        privblob = ssh2_mpint(x)
        pubkey = ssh_key_new_pub('dsa', pubblob)
        privkey = ssh_key_new_priv('dsa', pubblob, privblob)
        sig = ssh_key_sign(privkey, b"sample", 0)
        # Expected output using SHA-1 as the hash in nonce
        # construction, which is how SSH does things. RFC6979 lists
        # the following 256-bit values for r and s, but we end up only
        # using the low 160 bits of each.
        r = 0x3A1B2DBD7489D6ED7E608FD036C83AF396E290DBD602408E8677DAABD6E7445A
        s = 0xD26FCBA19FA3E3058FFC02CA1596CDBB6E0D20CB37B06054F7E36DED0CDBBCCF
        ref_sig = ssh_string(b"ssh-dss") + ssh_string(
            be_integer(r, 160) + be_integer(s, 160))
        self.assertEqual(sig, ref_sig)
        # And the other test string.
        sig = ssh_key_sign(privkey, b"test", 0)
        r = 0xC18270A93CFC6063F57A4DFA86024F700D980E4CF4E2CB65A504397273D98EA0
        s = 0x414F22E5F31A8B6D33295C7539C1C1BA3A6160D7D68D50AC0D3A5BEAC2884FAA
        ref_sig = ssh_string(b"ssh-dss") + ssh_string(
            be_integer(r, 160) + be_integer(s, 160))
        self.assertEqual(sig, ref_sig)

        # A.2.5: ECDSA with NIST P256
        q = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
        x = 0xC9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721
        k = rfc6979('sha1', q, x, "sample")
        self.assertEqual(int(k), 0x882905F1227FD620FBF2ABF21244F0BA83D0DC3A9103DBBEE43A1FB858109DB4)
        k = rfc6979('sha256', q, x, "sample")
        self.assertEqual(int(k), 0xA6E3C57DD01ABE90086538398355DD4C3B17AA873382B0F24D6129493D8AAD60)
        k = rfc6979('sha384', q, x, "sample")
        self.assertEqual(int(k), 0x09F634B188CEFD98E7EC88B1AA9852D734D0BC272F7D2A47DECC6EBEB375AAD4)
        k = rfc6979('sha512', q, x, "sample")
        self.assertEqual(int(k), 0x5FA81C63109BADB88C1F367B47DA606DA28CAD69AA22C4FE6AD7DF73A7173AA5)
        k = rfc6979('sha1', q, x, "test")
        self.assertEqual(int(k), 0x8C9520267C55D6B980DF741E56B4ADEE114D84FBFA2E62137954164028632A2E)
        k = rfc6979('sha256', q, x, "test")
        self.assertEqual(int(k), 0xD16B6AE827F17175E040871A1C7EC3500192C4C92677336EC2537ACAEE0008E0)
        k = rfc6979('sha384', q, x, "test")
        self.assertEqual(int(k), 0x16AEFFA357260B04B1DD199693960740066C1A8F3E8EDD79070AA914D361B3B8)
        k = rfc6979('sha512', q, x, "test")
        self.assertEqual(int(k), 0x6915D11632ACA3C40D5D51C08DAF9C555933819548784480E93499000D9F0B7F)
        # The public key, for signature testing
        Ux = 0x60FED4BA255A9D31C961EB74C6356D68C049B8923B61FA6CE669622E60F29FB6
        Uy = 0x7903FE1008B8BC99A41AE9E95628BC64F2F1B20C2D7E9F5177A3C294D4462299
        pubblob = ssh_string(b"ecdsa-sha2-nistp256") + ssh_string(b"nistp256") + ssh_string(b'\x04' + be_integer(Ux, 256) + be_integer(Uy, 256))
        privblob = ssh2_mpint(x)
        pubkey = ssh_key_new_pub('p256', pubblob)
        privkey = ssh_key_new_priv('p256', pubblob, privblob)
        sig = ssh_key_sign(privkey, b"sample", 0)
        # Expected output using SHA-256
        r = 0xEFD48B2AACB6A8FD1140DD9CD45E81D69D2C877B56AAF991C34D0EA84EAF3716
        s = 0xF7CB1C942D657C41D436C7A1B6E29F65F3E900DBB9AFF4064DC4AB2F843ACDA8
        ref_sig = ssh_string(b"ecdsa-sha2-nistp256") + ssh_string(ssh2_mpint(r) + ssh2_mpint(s))
        self.assertEqual(sig, ref_sig)
        # And the other test string
        sig = ssh_key_sign(privkey, b"test", 0)
        r = 0xF1ABB023518351CD71D881567B1EA663ED3EFCF6C5132B354F28D3B0B7D38367
        s = 0x019F4113742A2B14BD25926B49C649155F267E60D3814B4C0CC84250E46F0083
        ref_sig = ssh_string(b"ecdsa-sha2-nistp256") + ssh_string(ssh2_mpint(r) + ssh2_mpint(s))
        self.assertEqual(sig, ref_sig)

        # A.2.5: ECDSA with NIST P384
        q = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC7634D81F4372DDF581A0DB248B0A77AECEC196ACCC52973
        x = 0x6B9D3DAD2E1B8C1C05B19875B6659F4DE23C3B667BF297BA9AA47740787137D896D5724E4C70A825F872C9EA60D2EDF5
        k = rfc6979('sha1', q, x, "sample")
        self.assertEqual(int(k), 0x4471EF7518BB2C7C20F62EAE1C387AD0C5E8E470995DB4ACF694466E6AB096630F29E5938D25106C3C340045A2DB01A7)
        k = rfc6979('sha256', q, x, "sample")
        self.assertEqual(int(k), 0x180AE9F9AEC5438A44BC159A1FCB277C7BE54FA20E7CF404B490650A8ACC414E375572342863C899F9F2EDF9747A9B60)
        k = rfc6979('sha384', q, x, "sample")
        self.assertEqual(int(k), 0x94ED910D1A099DAD3254E9242AE85ABDE4BA15168EAF0CA87A555FD56D10FBCA2907E3E83BA95368623B8C4686915CF9)
        k = rfc6979('sha512', q, x, "sample")
        self.assertEqual(int(k), 0x92FC3C7183A883E24216D1141F1A8976C5B0DD797DFA597E3D7B32198BD35331A4E966532593A52980D0E3AAA5E10EC3)
        k = rfc6979('sha1', q, x, "test")
        self.assertEqual(int(k), 0x66CC2C8F4D303FC962E5FF6A27BD79F84EC812DDAE58CF5243B64A4AD8094D47EC3727F3A3C186C15054492E30698497)
        k = rfc6979('sha256', q, x, "test")
        self.assertEqual(int(k), 0x0CFAC37587532347DC3389FDC98286BBA8C73807285B184C83E62E26C401C0FAA48DD070BA79921A3457ABFF2D630AD7)
        k = rfc6979('sha384', q, x, "test")
        self.assertEqual(int(k), 0x015EE46A5BF88773ED9123A5AB0807962D193719503C527B031B4C2D225092ADA71F4A459BC0DA98ADB95837DB8312EA)
        k = rfc6979('sha512', q, x, "test")
        self.assertEqual(int(k), 0x3780C4F67CB15518B6ACAE34C9F83568D2E12E47DEAB6C50A4E4EE5319D1E8CE0E2CC8A136036DC4B9C00E6888F66B6C)
        # The public key, for signature testing
        Ux = 0xEC3A4E415B4E19A4568618029F427FA5DA9A8BC4AE92E02E06AAE5286B300C64DEF8F0EA9055866064A254515480BC13
        Uy = 0x8015D9B72D7D57244EA8EF9AC0C621896708A59367F9DFB9F54CA84B3F1C9DB1288B231C3AE0D4FE7344FD2533264720
        pubblob = ssh_string(b"ecdsa-sha2-nistp384") + ssh_string(b"nistp384") + ssh_string(b'\x04' + be_integer(Ux, 384) + be_integer(Uy, 384))
        privblob = ssh2_mpint(x)
        pubkey = ssh_key_new_pub('p384', pubblob)
        privkey = ssh_key_new_priv('p384', pubblob, privblob)
        sig = ssh_key_sign(privkey, b"sample", 0)
        # Expected output using SHA-384
        r = 0x94EDBB92A5ECB8AAD4736E56C691916B3F88140666CE9FA73D64C4EA95AD133C81A648152E44ACF96E36DD1E80FABE46
        s = 0x99EF4AEB15F178CEA1FE40DB2603138F130E740A19624526203B6351D0A3A94FA329C145786E679E7B82C71A38628AC8
        ref_sig = ssh_string(b"ecdsa-sha2-nistp384") + ssh_string(ssh2_mpint(r) + ssh2_mpint(s))
        self.assertEqual(sig, ref_sig)
        # And the other test string
        sig = ssh_key_sign(privkey, b"test", 0)
        r = 0x8203B63D3C853E8D77227FB377BCF7B7B772E97892A80F36AB775D509D7A5FEB0542A7F0812998DA8F1DD3CA3CF023DB
        s = 0xDDD0760448D42D8A43AF45AF836FCE4DE8BE06B485E9B61B827C2F13173923E06A739F040649A667BF3B828246BAA5A5
        ref_sig = ssh_string(b"ecdsa-sha2-nistp384") + ssh_string(ssh2_mpint(r) + ssh2_mpint(s))
        self.assertEqual(sig, ref_sig)

        # A.2.6: ECDSA with NIST P521
        q = 0x1FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFA51868783BF2F966B7FCC0148F709A5D03BB5C9B8899C47AEBB6FB71E91386409
        x = 0x0FAD06DAA62BA3B25D2FB40133DA757205DE67F5BB0018FEE8C86E1B68C7E75CAA896EB32F1F47C70855836A6D16FCC1466F6D8FBEC67DB89EC0C08B0E996B83538
        k = rfc6979('sha1', q, x, "sample")
        self.assertEqual(int(k), 0x089C071B419E1C2820962321787258469511958E80582E95D8378E0C2CCDB3CB42BEDE42F50E3FA3C71F5A76724281D31D9C89F0F91FC1BE4918DB1C03A5838D0F9)
        k = rfc6979('sha256', q, x, "sample")
        self.assertEqual(int(k), 0x0EDF38AFCAAECAB4383358B34D67C9F2216C8382AAEA44A3DAD5FDC9C32575761793FEF24EB0FC276DFC4F6E3EC476752F043CF01415387470BCBD8678ED2C7E1A0)
        k = rfc6979('sha384', q, x, "sample")
        self.assertEqual(int(k), 0x1546A108BC23A15D6F21872F7DED661FA8431DDBD922D0DCDB77CC878C8553FFAD064C95A920A750AC9137E527390D2D92F153E66196966EA554D9ADFCB109C4211)
        k = rfc6979('sha512', q, x, "sample")
        self.assertEqual(int(k), 0x1DAE2EA071F8110DC26882D4D5EAE0621A3256FC8847FB9022E2B7D28E6F10198B1574FDD03A9053C08A1854A168AA5A57470EC97DD5CE090124EF52A2F7ECBFFD3)
        k = rfc6979('sha1', q, x, "test")
        self.assertEqual(int(k), 0x0BB9F2BF4FE1038CCF4DABD7139A56F6FD8BB1386561BD3C6A4FC818B20DF5DDBA80795A947107A1AB9D12DAA615B1ADE4F7A9DC05E8E6311150F47F5C57CE8B222)
        k = rfc6979('sha256', q, x, "test")
        self.assertEqual(int(k), 0x01DE74955EFAABC4C4F17F8E84D881D1310B5392D7700275F82F145C61E843841AF09035BF7A6210F5A431A6A9E81C9323354A9E69135D44EBD2FCAA7731B909258)
        k = rfc6979('sha384', q, x, "test")
        self.assertEqual(int(k), 0x1F1FC4A349A7DA9A9E116BFDD055DC08E78252FF8E23AC276AC88B1770AE0B5DCEB1ED14A4916B769A523CE1E90BA22846AF11DF8B300C38818F713DADD85DE0C88)
        k = rfc6979('sha512', q, x, "test")
        self.assertEqual(int(k), 0x16200813020EC986863BEDFC1B121F605C1215645018AEA1A7B215A564DE9EB1B38A67AA1128B80CE391C4FB71187654AAA3431027BFC7F395766CA988C964DC56D)
        # The public key, for signature testing
        Ux = 0x1894550D0785932E00EAA23B694F213F8C3121F86DC97A04E5A7167DB4E5BCD371123D46E45DB6B5D5370A7F20FB633155D38FFA16D2BD761DCAC474B9A2F5023A4
        Uy = 0x0493101C962CD4D2FDDF782285E64584139C2F91B47F87FF82354D6630F746A28A0DB25741B5B34A828008B22ACC23F924FAAFBD4D33F81EA66956DFEAA2BFDFCF5
        pubblob = ssh_string(b"ecdsa-sha2-nistp521") + ssh_string(b"nistp521") + ssh_string(b'\x04' + be_integer(Ux, 528) + be_integer(Uy, 528))
        privblob = ssh2_mpint(x)
        pubkey = ssh_key_new_pub('p521', pubblob)
        privkey = ssh_key_new_priv('p521', pubblob, privblob)
        sig = ssh_key_sign(privkey, b"sample", 0)
        # Expected output using SHA-512
        r = 0x0C328FAFCBD79DD77850370C46325D987CB525569FB63C5D3BC53950E6D4C5F174E25A1EE9017B5D450606ADD152B534931D7D4E8455CC91F9B15BF05EC36E377FA
        s = 0x0617CCE7CF5064806C467F678D3B4080D6F1CC50AF26CA209417308281B68AF282623EAA63E5B5C0723D8B8C37FF0777B1A20F8CCB1DCCC43997F1EE0E44DA4A67A
        ref_sig = ssh_string(b"ecdsa-sha2-nistp521") + ssh_string(ssh2_mpint(r) + ssh2_mpint(s))
        self.assertEqual(sig, ref_sig)
        # And the other test string
        sig = ssh_key_sign(privkey, b"test", 0)
        r = 0x13E99020ABF5CEE7525D16B69B229652AB6BDF2AFFCAEF38773B4B7D08725F10CDB93482FDCC54EDCEE91ECA4166B2A7C6265EF0CE2BD7051B7CEF945BABD47EE6D
        s = 0x1FBD0013C674AA79CB39849527916CE301C66EA7CE8B80682786AD60F98F7E78A19CA69EFF5C57400E3B3A0AD66CE0978214D13BAF4E9AC60752F7B155E2DE4DCE3
        ref_sig = ssh_string(b"ecdsa-sha2-nistp521") + ssh_string(ssh2_mpint(r) + ssh2_mpint(s))
        self.assertEqual(sig, ref_sig)

    def testBLAKE2b(self):
        # The standard test vectors for BLAKE2b (in the separate class
        # below) don't satisfy me because they only test one hash
        # size. These additional tests exercise BLAKE2b's configurable
        # output length. The expected results are derived from the
        # BLAKE2 reference implementation.

        def b2_with_len(data, length):
            h = blake2b_new_general(length)
            h.update(data)
            return h.digest()[:length]

        self.assertEqualBin(b2_with_len(b'hello', 1), unhex("29"))
        self.assertEqualBin(b2_with_len(b'hello', 2), unhex("accd"))
        self.assertEqualBin(b2_with_len(b'hello', 3), unhex("980032"))
        self.assertEqualBin(b2_with_len(b'hello', 5), unhex("9baecc38f2"))
        self.assertEqualBin(b2_with_len(b'hello', 8), unhex(
            "a7b6eda801e5347d"))
        self.assertEqualBin(b2_with_len(b'hello', 13), unhex(
            "6eedb122c6707328a66aa34a07"))
        self.assertEqualBin(b2_with_len(b'hello', 21), unhex(
            "c7f0f74a227116547b3d2788e927ee2a76c87d8797"))
        self.assertEqualBin(b2_with_len(b'hello', 34), unhex(
            "2f5fcdf2b870fa254051dd448193a1fb6e92be122efca539ba2aeac0bc6c77d0"
            "dadc"))
        self.assertEqualBin(b2_with_len(b'hello', 55), unhex(
            "daafcf2bd6fccf976cbc234b71cd9f4f7d56fe0eb33a40018707089a215c44a8"
            "4b272d0329ae6d85a0f8acc7e964dc2facb715ba472bb6"))

    def testArgon2LongHash(self):
        # Unit-test the Argon2 long hash function H', which starts off
        # the same as BLAKE2b, but comes with its own method of
        # extending the output length past 64 bytes.
        #
        # I generated these test values using a test program linked
        # against the reference implementation's libargon2.a and
        # calling its blake2b_long function.
        preimage = b'hello, world'

        self.assertEqualBin(argon2_long_hash(1, preimage), unhex("8b"))
        self.assertEqualBin(argon2_long_hash(2, preimage), unhex("1ff9"))
        self.assertEqualBin(argon2_long_hash(63, preimage), unhex(
            "e2c997721f1d64aa8c25e588fb8ab19646ce6d5c2a431fa560fcb813e55dd481"
            "322d2630d95ca6b1b63317b13d6b111e5816170c80c3ca7d5b4bf894096de4"))
        self.assertEqualBin(argon2_long_hash(64, preimage), unhex(
            "0c7ba7ee6d510b4bb5c9b69ac91e25e0b11aa30dd6234b8e61b0fe1537c037b8"
            "8ed5aa59a277e8cc07095c81aff26d08967e4dfdabd32db8b6af6ceb78cf8c47"))
        self.assertEqualBin(argon2_long_hash(65, preimage), unhex(
            "680941abbd8fc80f28c38d623e90903f08709bf76575e2775d4ce01c31b192c8"
            "73038d9a31af8991c8b1ad4f2b1991f4d15f73ab0f4f3add415c297a12eb9ddb"
            "76"))
        self.assertEqualBin(argon2_long_hash(95, preimage), unhex(
            "4be28c51850fed70d9403e1406b6ba68a83d98cf222a4ee162beef60fd3384df"
            "eba3fce9d95f646982eb384ac943ce5263cb03428fd8d261cc41ffdb7ba328fe"
            "098526f2b49593f9e7f38188598ce4693b59f4dd32db30c1be9a9d35784fa0"))
        self.assertEqualBin(argon2_long_hash(96, preimage), unhex(
            "20295ea01e822cca113f668f33e5e481ed5879bfd7de6359ea42d497da97be52"
            "2cdd518d34ae32c44cabd45249b4e697626b0b14b6a33a2bd138be0a4bceeaf4"
            "9528f93acef01b093ee84d8d871d1ee6cf7c10e83ad0619631aed19345166f03"))
        self.assertEqualBin(argon2_long_hash(97, preimage), unhex(
            "d24b31f3ac0baad168d524efc4bafee55fef743fd60b14e28b860d7523e319c7"
            "520e2d5457cc3d06dc1044530afdf6990fa12e38d5802eb642f8e77fcfee2c0b"
            "1f84a28877f2f2f049ed9299e1e0230f98af3a161185970aad21f0ea0f5184cf"
            "90"))
        self.assertEqualBin(argon2_long_hash(127, preimage), unhex(
            "5d1e8380450dbc985418ed1f3700b925ae0719e4486e29131c81bca7083ac6b8"
            "f535c3398488e34d3dc1390de44097f1eee498f10ebe85b579e99a7672023b01"
            "ca5c20e63c595b640e00d80f113a52e3773719889b266ab4c65269c11fb212e4"
            "75f2b769bb26321bb60ecc0d490821e5056d7dfc9def3cd065d3ba90360764"))
        self.assertEqualBin(argon2_long_hash(128, preimage), unhex(
            "be15b316f3483c4d0d00f71a65b974894a2025f441b79b9fe461bc740cb0b039"
            "c4fe914f61c05a612d63ebc50a662b2d59b1996091e5e3474340544ea46a46cb"
            "25c41ff700fafcd96c4f12ddc698cd2426558f960696837ea8170fd2fe284b54"
            "8f585f97919ef14f2b3cbb351eb98872add7ba6d08c1401232df6cc878fbeb22"))
        self.assertEqualBin(argon2_long_hash(129, preimage), unhex(
            "83da464c278dcb12c29b6685fee6d32f0b461337c155369ad0d56b58b0aa5f80"
            "9aa7b56bd41b664c8d768957f8f0e40999fb0178eb53cf83f31d725bf92881bc"
            "900774bce4cdf56b6386ad3de6891d11a0ccd4564a3431fc4c24105a02d0a6a2"
            "434712b9a7471f3223c72a6e64912200d0a3d149a19d06fe9dc8ec09d7ed5a48"
            "bb"))
        self.assertEqualBin(argon2_long_hash(511, preimage), unhex(
            "30c0c0d0467e7665368db0b40a2324a61fb569d35172de2df53a9739a8d18e60"
            "b4f25d521c8855604be3e24ea56302566074323d94c0bd3a33d08f185d8ba5ac"
            "a2bc3fb2e4c4e5ffec5778daea67c6b5913c9cac16f2e5c7b7818e757fa747b3"
            "69e586d616010a752762f69c604238ed8738430366fbdb7493454fa02391a76b"
            "30f241695b9fa8d3a3116227c6bb6f72d325cf104ab153d15f928b22767d467d"
            "4bf7e16176aaa7315954b7872061933c12d548f1f93a8abb9d73791661bee521"
            "b2ae51be373a229dfef32787234c1be5846d133563002b9a029178716ad41e70"
            "1539d3fad300c77607c5217701e3e485d72c980f3f71d525c8148375a2f8d22c"
            "a211ba165330a90b7e0e6baa6073833925c23bdd388ee904f38463c7e6b85475"
            "09b810aae5c9ffc5dd902c2ffe049c338e3ae2c6416d3b874d6a9d384089564c"
            "0d8e4dce9b6e47e1d5ec9087bf526cc9fa35aab1893a0588d31b77fea37e0799"
            "468deacde47629d2960a3519b3bcd4e22364a9cccd3b128cba21cac27f140d53"
            "f79c11e4157e4cb48272eecdf62f52084a27e5b0933bbe66ded17e2df6f8d398"
            "f6c479c3c716457820ad177b8bd9334cb594e03d09fcc4f82d4385e141eacd7d"
            "9ad1e1c4cb42788af70bac0509f0a891e662960955490abf2763373803e8c89c"
            "df632579cb9c647634b30df214a3d67b92fd55d283c42c63b470a48a78cd5b"))
        self.assertEqualBin(argon2_long_hash(512, preimage), unhex(
            "79a6974e29a9a6c069e0156774d35c5014a409f5ffc60013725367a7208d4929"
            "7d228637751768a31a59e27aa89372f1bcc095a6fa331198a5bd5ad053ba2ebb"
            "cbcc501ea55cf142e8d95209228c9ab60cd104d5077472f2a9ecaa071aed6ee9"
            "5de29e188b7399d5b6b7ed897b2bc4dd1ea745eb9974e39ca6fb983380cc537a"
            "c04dfe6caefe85faf206b1613092ebadf791eaa8a5b814c9a79a73a5733b0505"
            "a47163c10a0f7309df6663896df6079a7c88c6879bb591a40abd398c6deda792"
            "1cc3986435b1c840a768b2fa507446f2f77a406b1b2f739f7795db24789c8927"
            "24b4c84b7005445123154f8cd2ba63a7ede672af5d197f846700732025c9931d"
            "1c67c5493417ca394a8f68ba532645815cf7b5102af134ecb4fd9e326f53779a"
            "3039dbef6a0880db9e38b6b61d2f9ead969e4224c2d9c69b5897e5eeb7032e83"
            "334e192ff50017056ccb84d4cc8eee3ab248d2614643d0174fe18c72186dd967"
            "92d8545645ddf4a9b2c7a91c9a71857a399449d7154077a8e9580f1a2d20227d"
            "671b455ccb897cba0491e50892120d7877f7776d653cfdb176fa3f64a9e6f848"
            "cd681c487b488775aaf698294eec813b2cca90d68d63b5d886d61c1a8e922aaa"
            "330fd658ede56e34bcd288048e845eba7b8e2e7cc22ba6c91b523e48017aa878"
            "8ce4f91d0e6d6c6706762fb0cc7f465cee3916684fb21e337cfe1b583e0b1e92"))
        self.assertEqualBin(argon2_long_hash(513, preimage), unhex(
            "32243cfbd7eca582d60b3b8ea3ba3d93783537689c7cbcd1d1cbde46200b8c86"
            "617fc00e8a9ae991a1e2f91c67e07d5f0a777d982c1461d0c5474e4e164b053c"
            "2808559e2b8a5ac4a46a5fcbc825b1d5302c7b0611940194eb494d45ce7113a2"
            "3424b51c199c6a5100ab159ff323eda5feffee4da4155a028a81da9d44e4286b"
            "ac3dab4ffce43a80b6ce97a47ea0ac51ee16e8b4d3b68942afdc20e1c21747c4"
            "94859c3d3883e7dc19ea416a393a3507683d9d03e6a3a91f8f1cb8a7d5d9892e"
            "80c8fb0222527a73a1f59b9dd41770982f2af177a6e96093064534803edd0713"
            "71ede53024cedc291d768325bb4e4def9af1b5569c349b64816496c37a8787b5"
            "4fbe248372ebadb5ce20e03eaa935dc55ff4b8cbe5d6d844c7b71d4656fef22c"
            "5a49f13d75a7a8368a2dbc1e78d732b879bfc5c9467eda2bf4918f0c59037ae3"
            "dee7880a171409dd1a4e143c814e60301ac77237f261fa7519a04e68000530f9"
            "708ed9fda5609d655560a9491f80f5875ad5725e3120686b73319c6a727932e3"
            "20a2174422523498c38fea47aeb20d135ff9fd93c6fa6db0005e0001685d7577"
            "33a82a4dc9dd6556b938f7b8dafd0d670846780b9931b815063708189b17877b"
            "825533bcc250fb576a28be4caa107e6a3a6f7b0c60fb51b0def27008b7e272ac"
            "95d610bfa912339799a2e537ce543d7862dddbe31bb224fda4ae283571847a28"
            "54"))
        self.assertEqualBin(argon2_long_hash(1024, preimage), unhex(
            "951252f6fa152124f381266a358d9b78b88e469d08d5fc78e4ea32253c7fc26c"
            "3ff1c93529ab4ee6fcf00acf29bbaba934a4014ce2625e0806601c55e6ce70d7"
            "121fd82f0904f335c5c7ba07dc6e6adf7582c92f7f255072203ea85844b4fe54"
            "817476a20bb742710ffc42750361be94332d0fc721b192309acfa70da43db6ae"
            "1d0f0bbe8a3250966a4532b36728162073c9eb3e119ea4c1c187c775dbb25a5d"
            "d883e3f65706a5fca897cdc4a8aa7b68ba3f57940c72f3a3396c417e758ba071"
            "95be4afba325237c0e2738a74d96fd1350fb623cb2ad40ea8b1e070cf398b98c"
            "2865ea40225b81f031f2b405409ca01dc5d9903d3d8e1d6381fbe7ccfc8f3dab"
            "eadafd7c976c0ba84a936f78ff7df0f112c089ba88f82bed7f9a6e31a91e5fee"
            "f675755454b948de22695660b243b9eca3bcc89608f83d2baa1d73dd6b8bd4f9"
            "b995ed9cb0f1edc6e98a49ed841b506c1bf59b43f4b3457a376bbff116c1a4f6"
            "07cc62381fc5c19953c68f300c1b51198d40784d812d25810ba404862f04b680"
            "6039a074f612ad8b84e0941ba23c915c3e7162c225fbecffdb7dc1ab559b2b54"
            "32fe8a498c32e918d8e7e33254ff75077f648827705e987f4d90fba971e78e1a"
            "6896b4d775c7359dc950f1e964fa04621aacf3c0988969490f4c72c54caf79e8"
            "481053cc0a27ffcd3580aabf9ef1268d498d8a18bd70e9b8402e011753bb7dc7"
            "e856c00d988fca924ee7cf61979c38cda8a872e4cc4fbdc90c23a0ded71eb944"
            "bb816ab22d9a4380e3e9d1cec818165c2fba6c5d51dcbf452c0cb1779a384937"
            "64d695370e13a301eca7be68d4112d2177381514efbb36fe08fc5bc2970301b8"
            "06f8e5a57a780e894d5276e2025bb775b6d1861e33c54ab6e3eb72947fbe6f91"
            "8174ce24eb4682efbb3c4f01233dc7ce9ef44792e9e876bb03e6751b3d559047"
            "d045127d976aa042fc55c690c9048e200065e7b7de19d9353aa9ac9b3e7611f0"
            "d1c42d069a300455ca1f7420a352bace89215e705106927510c11b3b1c1486d9"
            "f3ab006d2de2ee2c94574f760ce8c246bca229f98c66f06042b14f1fff9a16c0"
            "1550237e16d108ce5597299b1eb406a9ee505a29a6e0fa526b3e6beafd336aea"
            "138b2f31971586f67c5ffffbd6826d1c75666038c43d0bdff4edfc294e064a49"
            "2eed43e2dc78d00abc4e85edcd9563b8251b66f57b0f4b6d17f5a3f35c87c488"
            "dbeeb84fd720286197c2dec8290eccf3a313747de285b9cd3548e90cf81b3838"
            "3ffcc8c2a7f582feb369d05cb96b9b224d05902b3e39e5b96536032e9dddeb9b"
            "9d4f40a9c8f544ca37cf8d39d7c8c6a33880e9184ed017bd642db9590759bd10"
            "7362048ede5c0257feecc4984584592c566f37fba8469c064015339fb4f03023"
            "56ece37fd3655aae2bfc989b9b4c1384efc3503c8866db901802cb36eda9fb00"))

    def testArgon2(self):
        # A few tests of my own of Argon2, derived from the reference
        # implementation.
        pwd = b"password"
        salt = b"salt of at least 16 bytes"
        secret = b"secret"
        assoc = b"associated data"

        # Smallest memory (8Kbyte) and parallelism (1) parameters the
        # reference implementation will accept, but lots of passes
        self.assertEqualBin(
            argon2('i', 8, 16, 1, 24, pwd, salt, secret, assoc), unhex(
                "314da280240a3ca1eedd1f1db417a76eb0741e7df64b8cdf"))
        self.assertEqualBin(
            argon2('d', 8, 16, 1, 24, pwd, salt, secret, assoc), unhex(
                "9cc961cf43e0f86c2d4e202b816dc5bc5b2177e68faa0b08"))
        self.assertEqualBin(
            argon2('id', 8, 16, 1, 24, pwd, salt, secret, assoc), unhex(
                "6cd6c490c582fa597721d772d4e3de166987792491b48c51"))

        # Test a memory cost value that isn't a power of 2. This
        # checks a wraparound case during the conversion of J1 to a
        # block index, and is a regression test for a bug that nearly
        # got past me during original development.
        self.assertEqualBin(
            argon2('i', 104, 16, 2, 24, pwd, salt, secret, assoc), unhex(
                "a561963623f1073c9aa8caecdb600c73ffc6de677ba8d97c"))
        self.assertEqualBin(
            argon2('d', 104, 16, 2, 24, pwd, salt, secret, assoc), unhex(
                "a9014db7f1d468fb25b88fa7fc0deac0f2e7f27e25d2cf6e"))
        self.assertEqualBin(
            argon2('id', 104, 16, 2, 24, pwd, salt, secret, assoc), unhex(
                "64f3212b1e7725ffcf9ae2d1753d63e763bcd6970061a435"))

        # Larger parameters that should exercise the pseudorandom
        # block indexing reasonably thoroughly. Also generate plenty
        # of output data.
        self.assertEqualBin(
            argon2('i', 1024, 5, 16, 77, pwd, salt, secret, assoc), unhex(
                "b008a685ff57730fad0e6f3ef3b9189282c0d9b05303675f43b5f3054724"
                "733fcbe8e2639cc2c930535b31b723339041bcd703bf2483455acf86c0e6"
                "9ed88c545ad40f1f2068855e4d61e99407"))
        self.assertEqualBin(
            argon2('d', 1024, 5, 16, 111, pwd, salt, secret, assoc), unhex(
                "399ffbcd720c47745b9deb391ed0de7d5e0ffe53aef9f8ef7a7918cfa212"
                "53df8cc577affbd5e0c0f8bf6d93c11b2f63973f8fc8f89dccd832fc587e"
                "5d61717be6e88ca33eef5d1e168c028bae632a2a723c6c83f8e755f39171"
                "5eda1c77c8e2fe06fbdd4e56d35262587e7df73cd7"))
        self.assertEqualBin(
            argon2('id', 1024, 5, 16, 123, pwd, salt, secret, assoc), unhex(
                "6636807289cb9b9c032f48dcc31ffed1de4ca6c1b97e1ce768d690486341"
                "2ac84b39d568a81dd01d9ee3ceec6cc23441d95e6abeb4a2024f1f540d56"
                "9b799277c4037ddc7195ba783c9158a901adc7d4a5df8357b34a3869e5d6"
                "aeae2a21201eef5e347de22c922192e8f46274b0c9d33e965155a91e7686"
                "9d530e"))

    def testOpenSSHBcrypt(self):
        # Test case created by making an OpenSSH private key file
        # using their own ssh-keygen, then decrypting it successfully
        # using PuTTYgen and printing the inputs and outputs to
        # openssh_bcrypt in the process. So this output key is known
        # to agree with OpenSSH's own answer.

        self.assertEqualBin(
            openssh_bcrypt('test passphrase',
                           unhex('d0c3b40ace4afeaf8c0f81202ae36718'),
                           16, 48),
            unhex('d78ba86e7273de0e007ab0ba256646823d5c902bc44293ae'
                  '78547e9a7f629be928cc78ff78a75a4feb7aa6f125079c7d'))

    def testRSAVerify(self):
        def blobs(n, e, d, p, q, iqmp):
            pubblob = ssh_string(b"ssh-rsa") + ssh2_mpint(e) + ssh2_mpint(n)
            privblob = (ssh2_mpint(d) + ssh2_mpint(p) +
                        ssh2_mpint(q) + ssh2_mpint(iqmp))
            return pubblob, privblob

        def failure_test(*args):
            pubblob, privblob = blobs(*args)
            key = ssh_key_new_priv('rsa', pubblob, privblob)
            self.assertEqual(key, None)

        def success_test(*args):
            pubblob, privblob = blobs(*args)
            key = ssh_key_new_priv('rsa', pubblob, privblob)
            self.assertNotEqual(key, None)

        # Parameters for a (trivially small) test key.
        n = 0xb5d545a2f6423eabd55ffede53e21628d5d4491541482e10676d9d6f2783b9a5
        e = 0x25
        d = 0x6733db6a546ac99fcc21ba2b28b0c077156e8a705976205a955c6d9cef98f419
        p = 0xe30ebd7348bf10dca72b36f2724dafa7
        q = 0xcd02c87a7f7c08c4e9dc80c9b9bad5d3
        iqmp = 0x60a129b30db9227910efe1608976c513

        # Check the test key makes sense unmodified.
        success_test(n, e, d, p, q, iqmp)

        # Try modifying the values one by one to ensure they are
        # rejected, except iqmp, which sshrsa.c regenerates anyway so
        # it won't matter at all.
        failure_test(n+1, e, d, p, q, iqmp)
        failure_test(n, e+1, d, p, q, iqmp)
        failure_test(n, e, d+1, p, q, iqmp)
        failure_test(n, e, d, p+1, q, iqmp)
        failure_test(n, e, d, p, q+1, iqmp)
        success_test(n, e, d, p, q, iqmp+1)

        # The key should also be accepted with p,q reversed. (Again,
        # iqmp gets regenerated, so it won't matter if that's wrong.)
        success_test(n, e, d, q, p, iqmp)

        # Replace each of p and q with 0, and with 1. These should
        # still fail validation (obviously), but the point is that the
        # validator should also avoid trying to divide by zero in the
        # process.
        failure_test(n, e, d, 0, q, iqmp)
        failure_test(n, e, d, p, 0, iqmp)
        failure_test(n, e, d, 1, q, iqmp)
        failure_test(n, e, d, p, 1, iqmp)

    def testKeyMethods(self):
        # Exercise all the methods of the ssh_key trait on all key
        # types, and ensure that they're consistent with each other.
        # No particular test is done on the rightness of the
        # signatures by any objective standard, only that the output
        # from our signing method can be verified by the corresponding
        # verification method.
        #
        # However, we do include the expected signature text in each
        # case, which checks determinism in the sense of being
        # independent of any random numbers, and also in the sense of
        # tomorrow's change to the code not having accidentally
        # changed the behaviour.

        test_message = b"Message to be signed by crypt.testKeyMethods\n"

        test_keys = [
            ('ed25519', 'AAAAC3NzaC1lZDI1NTE5AAAAIM7jupzef6CD0ps2JYxJp9IlwY49oorOseV5z5JFDFKn', 'AAAAIAf4/WRtypofgdNF2vbZOUFE1h4hvjw4tkGJZyOzI7c3', 255, b'0xf4d6e7f6f4479c23f0764ef43cea1711dbfe02aa2b5a32ff925c7c1fbf0f0db,0x27520c4592cf79e5b1ce8aa23d8ec125d2a7498c25369bd283a07fde9cbae3ce', [(0, 'AAAAC3NzaC1lZDI1NTE5AAAAQN73EqfyA4WneqDhgZ98TlRj9V5Wg8zCrMxTLJN1UtyfAnPUJDtfG/U0vOsP8PrnQxd41DDDnxrAXuqJz8rOagc=')]),
            ('ed448', 'AAAACXNzaC1lZDQ0OAAAADnRI0CQDym5IqUidLNDcSdHe54bYEwqjpjBlab8uKGoe6FRqqejha7+5U/VAHy7BmE23+ju26O9XgA=', 'AAAAObP9klqyiJSJsdFJf+xwZQdkbZGUqXE07K6e5plfRTGjYYkyWJFUNFH4jzIn9xH1TX9z9EGycPaXAA==', 448, b'0x4bf4a2b6586c60d8cdb52c2b45b897f6d2224bc37987489c0d70febb449e8c82964ed5785827be808e44d31dd31e6ff7c99f43e49f419928,0x5ebda3dbeee8df366106bb7c00d54fe5feae85a3a7aa51a17ba8a1b8fca695c1988e2a4c601b9e7b47277143b37422a522b9290f904023d1', [(0, 'AAAACXNzaC1lZDQ0OAAAAHLkSVioGMvLesZp3Tn+Z/sSK0Hl7RHsHP4q9flLzTpZG5h6JDH3VmZBEjTJ6iOLaa0v4FoNt0ng4wAB53WrlQC4h3iAusoGXnPMAKJLmqzplKOCi8HKXk8Xl8fsXbaoyhatv1OZpwJcffmh1x+x+LSgNQA=')]),
            ('p256', 'AAAAE2VjZHNhLXNoYTItbmlzdHAyNTYAAAAIbmlzdHAyNTYAAABBBHkYQ0sQoq5LbJI1VMWhw3bV43TSYi3WVpqIgKcBKK91TcFFlAMZgceOHQ0xAFYcSczIttLvFu+xkcLXrRd4N7Q=', 'AAAAIQCV/1VqiCsHZm/n+bq7lHEHlyy7KFgZBEbzqYaWtbx48Q==', 256, b'nistp256,0x7918434b10a2ae4b6c923554c5a1c376d5e374d2622dd6569a8880a70128af75,0x4dc14594031981c78e1d0d3100561c49ccc8b6d2ef16efb191c2d7ad177837b4', [(0, 'AAAAE2VjZHNhLXNoYTItbmlzdHAyNTYAAABIAAAAIFrd1bjr4GHfWsM9RNJ+y4Z0eVwpRRv3IvNE2moaA1x3AAAAIFWcwwCE69kS4oybMFEUP4r7qFAY8tSb1o8ItSFcSe2+')]),
            ('p384', 'AAAAE2VjZHNhLXNoYTItbmlzdHAzODQAAAAIbmlzdHAzODQAAABhBMYK8PUtfAlJwKaBTIGEuCzH0vqOMa4UbcjrBbTbkGVSUnfo+nuC80NCdj9JJMs1jvfF8GzKLc5z8H3nZyM741/BUFjV7rEHsQFDek4KyWvKkEgKiTlZid19VukNo1q2Hg==', 'AAAAMGsfTmdB4zHdbiQ2euTSdzM6UKEOnrVjMAWwHEYvmG5qUOcBnn62fJDRJy67L+QGdg==', 384, b'nistp384,0xc60af0f52d7c0949c0a6814c8184b82cc7d2fa8e31ae146dc8eb05b4db9065525277e8fa7b82f34342763f4924cb358e,0xf7c5f06cca2dce73f07de767233be35fc15058d5eeb107b101437a4e0ac96bca90480a89395989dd7d56e90da35ab61e', [(0, 'AAAAE2VjZHNhLXNoYTItbmlzdHAzODQAAABoAAAAMFqCJ+gBP4GGc7yCy9F5e4EjkDlvYBYsYWMYFg3Md/ml7Md8pIrN7I0+8bFb99rZjQAAADAsM2kI+QOcgK+oVDaP0qkLRRbWDO1dSU5I2YfETyHVLYFNdRmgdWo6002XTO9jAsk=')]),
            ('p521', 'AAAAE2VjZHNhLXNoYTItbmlzdHA1MjEAAAAIbmlzdHA1MjEAAACFBAFrGthlKM152vu2Ghk+R7iO9/M6e+hTehNZ6+FBwof4HPkPB2/HHXj5+w5ynWyUrWiX5TI2riuJEIrJErcRH5LglADnJDX2w4yrKZ+wDHSz9lwh9p2F+B5R952es6gX3RJRkGA+qhKpKup8gKx78RMbleX8wgRtIu+4YMUnKb1edREiRg==', 'AAAAQgFh7VNJFUljWhhyAEiL0z+UPs/QggcMTd3Vv2aKDeBdCRl5di8r+BMm39L7bRzxRMEtW5NSKlDtE8MFEGdIE9khsw==', 521, b'nistp521,0x16b1ad86528cd79dafbb61a193e47b88ef7f33a7be8537a1359ebe141c287f81cf90f076fc71d78f9fb0e729d6c94ad6897e53236ae2b89108ac912b7111f92e094,0xe72435f6c38cab299fb00c74b3f65c21f69d85f81e51f79d9eb3a817dd125190603eaa12a92aea7c80ac7bf1131b95e5fcc2046d22efb860c52729bd5e75112246', [(0, 'AAAAE2VjZHNhLXNoYTItbmlzdHA1MjEAAACLAAAAQVBkbaCKivgvc+68CULCdPayjzRUYZdj1G2pLyiPWTdmJKVKF/W1oDAtjMZlP53tqCpGxDdrLoJH2A39k6g5MgNjAAAAQgGrNcesPBw/HMopBQ1JqOG1cSlAzjiFT34FvM68ZhdIjbQ0eHFuYs97RekQ8dpxmkuM88e63ATbZy4yDX06pKgmuQ==')]),
            ('dsa', 'AAAAB3NzaC1kc3MAAABhAJyWZzjVddGdyc5JPu/WPrC07vKRAmlqO6TUi49ah96iRcM7/D1aRMVAdYBepQ2mf1fsQTmvoC9KgQa79nN3kHhz0voQBKOuKI1ZAodfVOgpP4xmcXgjaA73Vjz22n4newAAABUA6l7/vIveaiA33YYv+SKcKLQaA8cAAABgbErc8QLw/WDz7mhVRZrU+9x3Tfs68j3eW+B/d7Rz1ZCqMYDk7r/F8dlBdQlYhpQvhuSBgzoFa0+qPvSSxPmutgb94wNqhHlVIUb9ZOJNloNr2lXiPP//Wu51TxXAEvAAAAAAYQCcQ9mufXtZa5RyfwT4NuLivdsidP4HRoLXdlnppfFAbNdbhxE0Us8WZt+a/443bwKnYxgif8dgxv5UROnWTngWu0jbJHpaDcTc9lRyTeSUiZZK312s/Sl7qDk3/Du7RUI=', 'AAAAFGx3ft7G8AQzFsjhle7PWardUXh3', 768, b'0x9c966738d575d19dc9ce493eefd63eb0b4eef29102696a3ba4d48b8f5a87dea245c33bfc3d5a44c54075805ea50da67f57ec4139afa02f4a8106bbf67377907873d2fa1004a3ae288d5902875f54e8293f8c66717823680ef7563cf6da7e277b,0xea5effbc8bde6a2037dd862ff9229c28b41a03c7,0x6c4adcf102f0fd60f3ee6855459ad4fbdc774dfb3af23dde5be07f77b473d590aa3180e4eebfc5f1d94175095886942f86e481833a056b4faa3ef492c4f9aeb606fde3036a8479552146fd64e24d96836bda55e23cffff5aee754f15c012f000,0x9c43d9ae7d7b596b94727f04f836e2e2bddb2274fe074682d77659e9a5f1406cd75b87113452cf1666df9aff8e376f02a76318227fc760c6fe5444e9d64e7816bb48db247a5a0dc4dcf654724de49489964adf5dacfd297ba83937fc3bbb4542', [(0, 'AAAAB3NzaC1kc3MAAAAoyCVHLG2QqdMx7NiCWaThx6tDA5mf7UGl+8By0IzmSldBujsGKNs20g==')]),
            ('rsa', 'AAAAB3NzaC1yc2EAAAABJQAAAGEA2ChX9+mQD/NULFkBrxLDI8d1PHgrInC2u11U4Grqu4oVzKvnFROo6DZeCu6sKhFJE5CnIL7evAthQ9hkXVHDhQ7xGVauzqyHGdIU4/pHRScAYWBv/PZOlNMrSoP/PP91', 'AAAAYCMNdgyGvWpez2EjMLSbQj0nQ3GW8jzvru3zdYwtA3hblNUU9QpWNxDmOMOApkwCzUgsdIPsBxctIeWT2h+v8sVOH+d66LCaNmNR0lp+dQ+iXM67hcGNuxJwRdMupD9ZbQAAADEA7XMrMAb4WuHaFafoTfGrf6Jhdy9Ozjqi1fStuld7Nj9JkoZluiL2dCwIrxqOjwU5AAAAMQDpC1gYiGVSPeDRILr2oxREtXWOsW+/ZZTfZNX7lvoufnp+qvwZPqvZnXQFHyZ8qB0AAAAwQE0wx8TPgcvRVEVv8Wt+o1NFlkJZayWD5hqpe/8AqUMZbqfg/aiso5mvecDLFgfV', 768, b'0x25,0xd82857f7e9900ff3542c5901af12c323c7753c782b2270b6bb5d54e06aeabb8a15ccabe71513a8e8365e0aeeac2a11491390a720bedebc0b6143d8645d51c3850ef11956aeceac8719d214e3fa4745270061606ffcf64e94d32b4a83ff3cff75', [(0, 'AAAAB3NzaC1yc2EAAABgrLSC4635RCsH1b3en58NqLsrH7PKRZyb3YmRasOyr8xIZMSlKZyxNg+kkn9OgBzbH9vChafzarfHyVwtJE2IMt3uwxTIWjwgwH19tc16k8YmNfDzujmB6OFOArmzKJgJ'), (2, 'AAAADHJzYS1zaGEyLTI1NgAAAGAJszr04BZlVBEdRLGOv1rTJwPiid/0I6/MycSH+noahvUH2wjrRhqDuv51F4nKYF5J9vBsEotTSrSF/cnLsliCdvVkEfmvhdcn/jx2LWF2OfjqETiYSc69Dde9UFmAPds='), (4, 'AAAADHJzYS1zaGEyLTUxMgAAAGBxfZ2m+WjvZ5YV5RFm0+w84CgHQ95EPndoAha0PCMc93AUHBmoHnezsJvEGuLovUm35w/0POmUNHI7HzM9PECwXrV0rO6N/HL/oFxJuDYmeqCpjMVmN8QXka+yxs2GEtA=')]),
        ]

        for alg, pubb64, privb64, bits, cachestr, siglist in test_keys:
            # Decode the blobs in the above test data.
            pubblob = b64(pubb64)
            privblob = b64(privb64)

            # Check the method that examines a public blob directly
            # and returns an integer showing the key size.
            self.assertEqual(ssh_key_public_bits(alg, pubblob), bits)

            # Make a public-only and a full ssh_key object.
            pubkey = ssh_key_new_pub(alg, pubblob)
            privkey = ssh_key_new_priv(alg, pubblob, privblob)

            # Test that they re-export the public and private key
            # blobs unchanged.
            self.assertEqual(ssh_key_public_blob(pubkey), pubblob)
            self.assertEqual(ssh_key_public_blob(privkey), pubblob)
            self.assertEqual(ssh_key_private_blob(privkey), privblob)

            # Round-trip through the OpenSSH wire encoding used by the
            # agent protocol (and the newer OpenSSH key file format),
            # and check the result still exports all the same blobs.
            osshblob = ssh_key_openssh_blob(privkey)
            privkey2 = ssh_key_new_priv_openssh(alg, osshblob)
            self.assertEqual(ssh_key_public_blob(privkey2), pubblob)
            self.assertEqual(ssh_key_private_blob(privkey2), privblob)
            self.assertEqual(ssh_key_openssh_blob(privkey2), osshblob)

            # Test that the string description used in the host key
            # cache is as expected.
            for key in [pubkey, privkey, privkey2]:
                self.assertEqual(ssh_key_cache_str(key), cachestr)

            # Now test signatures, separately for each provided flags
            # value.
            for flags, sigb64 in siglist:
                # Decode the signature blob from the test data.
                sigblob = b64(sigb64)

                # Sign our test message, and check it produces exactly
                # the expected signature blob.
                #
                # We do this with both the original private key and
                # the one we round-tripped through OpenSSH wire
                # format, just in case that round trip made some kind
                # of a mess that didn't show up in the re-extraction
                # of the blobs.
                for key in [privkey, privkey2]:
                    self.assertEqual(ssh_key_sign(
                        key, test_message, flags), sigblob)

                if flags != 0:
                    # Currently we only support _generating_
                    # signatures with flags != 0, not verifying them.
                    continue

                # Check the signature verifies successfully, with all
                # three of the key objects we have.
                for key in [pubkey, privkey, privkey2]:
                    self.assertTrue(ssh_key_verify(key, sigblob, test_message))

                # A crude check that at least _something_ doesn't
                # verify successfully: flip a bit of the signature
                # and expect it to fail.
                #
                # We do this twice, at the 1/3 and 2/3 points along
                # the signature's length, so that in the case of
                # signatures in two parts (DSA-like) we try perturbing
                # both parts. Other than that, we don't do much to
                # make this a rigorous cryptographic test.
                for n, d in [(1,3),(2,3)]:
                    sigbytes = list(sigblob)
                    bit = 8 * len(sigbytes) * n // d
                    sigbytes[bit // 8] ^= 1 << (bit % 8)
                    badsig = bytes(sigbytes)
                    for key in [pubkey, privkey, privkey2]:
                        self.assertFalse(ssh_key_verify(
                            key, badsig, test_message))

    def testShortRSASignatures(self):
        key = ssh_key_new_priv('rsa', b64("""
AAAAB3NzaC1yc2EAAAADAQABAAABAQDeoTvwEDg46K7vYrQFFwbo2sBPahNoiw7i
RMbpwuOIH8sAOFAWzDvIZpDODGkobwc2hM8FRlZUg3lTgDaqVuMJOupG0xzOqehu
Fw3kXrm6ScWxaUXs+b5o88sqXBBYs91KmsWqYKTUUwDBuDHdo8Neq8h8SJqspCo4
qctIoLoTrXYMqonfHXZp4bIn5WPN6jNL9pLi7Y+sl8aLe4w73aZzxMphecQfMMVJ
ezmv9zgA7gKw5ErorIGKCF44YRbvNisZA5j2DyLLsd/ztw2ikIEnx9Ng33+tGEBC
zq2RYb4ZtuT9dHXcNiHx3CqLLlFrjl13hQamjwoVy4ydDIdQZjYz
"""), b64("""
AAABADhDwXUrdDoVvFhttpduuWVSG7Y2Vc9fDZTr0uWzRnPZrSFSGhOY7Cb6nPAm
PNFmNgl2SSfJHfpf++K5jZdBPEHR7PGXWzlzwXVJSE6GDiRhjqAGvhBlEdVOf/Ml
r0/rrSq0sO4dXKr4i0FqPtgIElEz0whuBQFKwAzwBJtHW5+rBzGLvoHh560UN4IK
SU3jv/nDMvPohEBfOA0puqYAfZM8PmU/kbgERPLyPp/dfnGBC4LlOnAcak+2RNu6
yQ5v0NKksyYidCI76Ztf3B9asNNN4AbTg8JbzABwjtxR+0bDOOi0RwAJC2Wn2FOy
WiJsQWjz/fMnJW8WVg3DR/va/4EAAACBAP8rwn1vy4Y/S1EixR1XZM9F1OvJQwzN
EKhD1Qbr1YlLX3oZRnulJg/j0HupGnKCRh8DulNbrmXlMFdeZHDVFw87/o73DTCQ
g2qnRNUwJdBkePrA563vVx6OXe5TSF3+3SRNMesAdN8ExvGeOFP10z0FZhS3Zuco
y4WrxQBXVetBAAAAgQDfWmh5CRJEBCJbLHMdZK8QAw2QbKiD0VTw7PkDWJuSbYDB
AvEhoFuXUO/yMIfCCFjUuzO+3iumk8F/lFTkJ620Aah5tzRcKCtyW4YuoQUYMgpW
5/hGIL4M4bvUDGUGI3+SOn8qfAzCCsFD0FdR6ms0pMubaJQmeiI2wyM9ehOIcwAA
AIEAmKEX1YZHNtx/D/SaTsl6z+KwmOluqjzyjrwL16QLpIR1/F7lAjSnMGz3yORp
+314D3yZzKutpalwwsS4+z7838pilVaV7iWqF4TMDKYZ/6/baRXpwxrwFqvWXwQ3
cLerc7bpA/IeVovoTirt0RNxdMPIVv3XsXE7pqatJBwOcQE=
"""))

        def decode(data):
            pos = 0
            alg, data = ssh_decode_string(data, True)
            sig, data = ssh_decode_string(data, True)
            self.assertEqual(data, b'')
            return alg, sig

        # An RSA signature over each hash which comes out a byte short
        # with this key. Found in the obvious manner, by signing
        # "message0", "message1", ... until one was short.
        #
        # We expect the ssh-rsa signature to be stored short, and the
        # other two to be padded with a zero byte.
        blob = ssh_key_sign(key, "message79", 0)
        alg, sig = decode(blob)
        self.assertEqual(alg, b"ssh-rsa")
        self.assertEqual(len(sig), 255) # one byte short
        self.assertNotEqual(sig[0], 0)

        blob = ssh_key_sign(key, "message208", 2)
        alg, sig = decode(blob)
        self.assertEqual(alg, b"rsa-sha2-256")
        self.assertEqual(len(sig), 256) # full-length
        self.assertEqual(sig[0], 0) # and has a leading zero byte

        blob = ssh_key_sign(key, "message461", 4)
        alg, sig = decode(blob)
        self.assertEqual(alg, b"rsa-sha2-512")
        self.assertEqual(len(sig), 256) # full-length
        self.assertEqual(sig[0], 0) # and has a leading zero byte

    def testPPKLoadSave(self):
        # Stability test of PPK load/save functions.
        input_clear_key = b"""\
PuTTY-User-Key-File-3: ssh-ed25519
Encryption: none
Comment: ed25519-key-20200105
Public-Lines: 2
AAAAC3NzaC1lZDI1NTE5AAAAIHJCszOHaI9X/yGLtjn22f0hO6VPMQDVtctkym6F
JH1W
Private-Lines: 1
AAAAIGvvIpl8jyqn8Xufkw6v3FnEGtXF3KWw55AP3/AGEBpY
Private-MAC: 816c84093fc4877e8411b8e5139c5ce35d8387a2630ff087214911d67417a54d
"""
        input_encrypted_key = b"""\
PuTTY-User-Key-File-3: ssh-ed25519
Encryption: aes256-cbc
Comment: ed25519-key-20200105
Public-Lines: 2
AAAAC3NzaC1lZDI1NTE5AAAAIHJCszOHaI9X/yGLtjn22f0hO6VPMQDVtctkym6F
JH1W
Key-Derivation: Argon2id
Argon2-Memory: 8192
Argon2-Passes: 13
Argon2-Parallelism: 1
Argon2-Salt: 37c3911bfefc8c1d11ec579627d2b3d9
Private-Lines: 1
amviz4sVUBN64jLO3gt4HGXJosUArghc4Soi7aVVLb2Tir5Baj0OQClorycuaPRd
Private-MAC: 6f5e588e475e55434106ec2c3569695b03f423228b44993a9e97d52ffe7be5a8
"""
        algorithm = b'ssh-ed25519'
        comment = b'ed25519-key-20200105'
        pp = b'test-passphrase'
        public_blob = unhex(
            '0000000b7373682d65643235353139000000207242b33387688f57ff218bb639'
            'f6d9fd213ba54f3100d5b5cb64ca6e85247d56')

        self.assertEqual(ppk_encrypted_s(input_clear_key), (False, comment))
        self.assertEqual(ppk_encrypted_s(input_encrypted_key), (True, comment))
        self.assertEqual(ppk_encrypted_s("not a key file"), (False, None))

        self.assertEqual(ppk_loadpub_s(input_clear_key),
                         (True, algorithm, public_blob, comment, None))
        self.assertEqual(ppk_loadpub_s(input_encrypted_key),
                         (True, algorithm, public_blob, comment, None))
        self.assertEqual(ppk_loadpub_s("not a key file"),
                         (False, None, b'', None,
                          b'not a public key or a PuTTY SSH-2 private key'))

        k1, c, e = ppk_load_s(input_clear_key, None)
        self.assertEqual((c, e), (comment, None))
        k2, c, e = ppk_load_s(input_encrypted_key, pp)
        self.assertEqual((c, e), (comment, None))
        privblob = ssh_key_private_blob(k1)
        self.assertEqual(ssh_key_private_blob(k2), privblob)

        salt = unhex('37c3911bfefc8c1d11ec579627d2b3d9')
        with queued_specific_random_data(salt):
            self.assertEqual(ppk_save_sb(k1, comment, None,
                                         3, 'id', 8192, 13, 1),
                             input_clear_key)
        with queued_specific_random_data(salt):
            self.assertEqual(ppk_save_sb(k2, comment, None,
                                         3, 'id', 8192, 13, 1),
                             input_clear_key)

        with queued_specific_random_data(salt):
            self.assertEqual(ppk_save_sb(k1, comment, pp,
                                         3, 'id', 8192, 13, 1),
                             input_encrypted_key)
        with queued_specific_random_data(salt):
            self.assertEqual(ppk_save_sb(k2, comment, pp,
                                         3, 'id', 8192, 13, 1),
                             input_encrypted_key)

        # And check we can still handle v2 key files.
        v2_clear_key = b"""\
PuTTY-User-Key-File-2: ssh-ed25519
Encryption: none
Comment: ed25519-key-20200105
Public-Lines: 2
AAAAC3NzaC1lZDI1NTE5AAAAIHJCszOHaI9X/yGLtjn22f0hO6VPMQDVtctkym6F
JH1W
Private-Lines: 1
AAAAIGvvIpl8jyqn8Xufkw6v3FnEGtXF3KWw55AP3/AGEBpY
Private-MAC: 2a629acfcfbe28488a1ba9b6948c36406bc28422
"""
        v2_encrypted_key = b"""\
PuTTY-User-Key-File-2: ssh-ed25519
Encryption: aes256-cbc
Comment: ed25519-key-20200105
Public-Lines: 2
AAAAC3NzaC1lZDI1NTE5AAAAIHJCszOHaI9X/yGLtjn22f0hO6VPMQDVtctkym6F
JH1W
Private-Lines: 1
4/jKlTgC652oa9HLVGrMjHZw7tj0sKRuZaJPOuLhGTvb25Jzpcqpbi+Uf+y+uo+Z
Private-MAC: 5b1f6f4cc43eb0060d2c3e181bc0129343adba2b
"""

        self.assertEqual(ppk_encrypted_s(v2_clear_key), (False, comment))
        self.assertEqual(ppk_encrypted_s(v2_encrypted_key), (True, comment))
        self.assertEqual(ppk_encrypted_s("not a key file"), (False, None))

        self.assertEqual(ppk_loadpub_s(v2_clear_key),
                         (True, algorithm, public_blob, comment, None))
        self.assertEqual(ppk_loadpub_s(v2_encrypted_key),
                         (True, algorithm, public_blob, comment, None))
        self.assertEqual(ppk_loadpub_s("not a key file"),
                         (False, None, b'', None,
                          b'not a public key or a PuTTY SSH-2 private key'))

        k1, c, e = ppk_load_s(v2_clear_key, None)
        self.assertEqual((c, e), (comment, None))
        k2, c, e = ppk_load_s(v2_encrypted_key, pp)
        self.assertEqual((c, e), (comment, None))
        self.assertEqual(ssh_key_private_blob(k1), privblob)
        self.assertEqual(ssh_key_private_blob(k2), privblob)

        self.assertEqual(ppk_save_sb(k2, comment, None,
                                     2, 'id', 8192, 13, 1),
                         v2_clear_key)
        self.assertEqual(ppk_save_sb(k1, comment, pp,
                                     2, 'id', 8192, 13, 1),
                         v2_encrypted_key)

    def testRSA1LoadSave(self):
        # Stability test of SSH-1 RSA key-file load/save functions.
        input_clear_key = unhex(
            "5353482050524956415445204B45592046494C4520464F524D415420312E310A"
            "000000000000000002000200BB115A85B741E84E3D940E690DF96A0CBFDC07CA"
            "70E51DA8234D211DE77341CEF40C214CAA5DCF68BE2127447FD6C84CCB17D057"
            "A74F2365B9D84A78906AEB51000625000000107273612D6B65792D3230323030"
            "313036208E208E0200929EE615C6FC4E4B29585E52570F984F2E97B3144AA5BD"
            "4C6EB2130999BB339305A21FFFA79442462A8397AF8CAC395A3A3827DE10457A"
            "1F1B277ABFB8C069C100FF55B1CAD69B3BD9E42456CF28B1A4B98130AFCE08B2"
            "8BCFFF5FFFED76C5D51E9F0100C5DE76889C62B1090A770AE68F087A19AB5126"
            "E60DF87710093A2AD57B3380FB0100F2068AC47ECB33BF8F13DF402BABF35EE7"
            "26BD32F7564E51502DF5C8F4888B2300000000")
        input_encrypted_key = unhex(
            "5353482050524956415445204b45592046494c4520464f524d415420312e310a"
            "000300000000000002000200bb115a85b741e84e3d940e690df96a0cbfdc07ca"
            "70e51da8234d211de77341cef40c214caa5dcf68be2127447fd6c84ccb17d057"
            "a74f2365b9d84a78906aeb51000625000000107273612d6b65792d3230323030"
            "3130363377f926e811a5f044c52714801ecdcf9dd572ee0a193c4f67e87ab2ce"
            "4569d0c5776fd6028909ed8b6d663bef15d207d3ef6307e7e21dbec56e8d8b4e"
            "894ded34df891bb29bae6b2b74805ac80f7304926abf01ae314dd69c64240761"
            "34f15d50c99f7573252993530ec9c4d5016dd1f5191730cda31a5d95d362628b"
            "2a26f4bb21840d01c8360e4a6ce216c4686d25b8699d45cf361663bb185e2c5e"
            "652012a1e0f9d6d19afbb28506f7775bfd8129")

        comment = b'rsa-key-20200106'
        pp = b'test-passphrase'
        public_blob = unhex(
            "000002000006250200bb115a85b741e84e3d940e690df96a0cbfdc07ca70e51d"
            "a8234d211de77341cef40c214caa5dcf68be2127447fd6c84ccb17d057a74f23"
            "65b9d84a78906aeb51")

        self.assertEqual(rsa1_encrypted_s(input_clear_key), (False, comment))
        self.assertEqual(rsa1_encrypted_s(input_encrypted_key),
                         (True, comment))
        self.assertEqual(rsa1_encrypted_s("not a key file"), (False, None))

        self.assertEqual(rsa1_loadpub_s(input_clear_key),
                         (1, public_blob, comment, None))
        self.assertEqual(rsa1_loadpub_s(input_encrypted_key),
                         (1, public_blob, comment, None))

        k1 = rsa_new()
        status, c, e = rsa1_load_s(input_clear_key, k1, None)
        self.assertEqual((status, c, e), (1, comment, None))
        k2 = rsa_new()
        status, c, e = rsa1_load_s(input_clear_key, k2, None)
        self.assertEqual((status, c, e), (1, comment, None))

        with queued_specific_random_data(unhex("208e")):
            self.assertEqual(rsa1_save_sb(k1, comment, None), input_clear_key)
        with queued_specific_random_data(unhex("208e")):
            self.assertEqual(rsa1_save_sb(k2, comment, None), input_clear_key)

        with queued_specific_random_data(unhex("99f3")):
            self.assertEqual(rsa1_save_sb(k1, comment, pp),
                             input_encrypted_key)
        with queued_specific_random_data(unhex("99f3")):
            self.assertEqual(rsa1_save_sb(k2, comment, pp),
                             input_encrypted_key)

    def testRFC4716(self):
        key = """\
---- BEGIN SSH2 PUBLIC KEY ----
Comment: "rsa-key-20240810"
AAAAB3NzaC1yc2EAAAADAQABAAABAQCKdLtvsewMpsbWQCNs8VOWKlh6eQT0gzbc
IoDLFPk5uVS1HjAEEjIZaXAB86PHTeJhkwEMlMXZ8mUZwAcZkuqKVCSib/VkuMEv
wXa4cOf70XMBUtUgRJ5bJRMsA8PNkZN/OQHyyBLgTXGoFPWq73A3fxPZIe8BSAN+
mPuILX1GHUKbBzT56xRNwB5nHkg0MStEotkIzg3xRNIXB9qyP6ILO4Qax2n7+XJS
lmzr0KDJq5ZNSEZV4IprvAYBeEtvdBfLrRM4kifpVDE7ZrVXtKOIGDsxdEEBeqqy
LzN/Ly+uECsga2hoc+P/ZHMULMZkCfrOyWdeXz7BR/acLZJoT579
---- END SSH2 PUBLIC KEY ----
"""

        comment = b"rsa-key-20240810"
        public_blob = b64("""
AAAAB3NzaC1yc2EAAAADAQABAAABAQCKdLtvsewMpsbWQCNs8VOWKlh6eQT0gzbc
IoDLFPk5uVS1HjAEEjIZaXAB86PHTeJhkwEMlMXZ8mUZwAcZkuqKVCSib/VkuMEv
wXa4cOf70XMBUtUgRJ5bJRMsA8PNkZN/OQHyyBLgTXGoFPWq73A3fxPZIe8BSAN+
mPuILX1GHUKbBzT56xRNwB5nHkg0MStEotkIzg3xRNIXB9qyP6ILO4Qax2n7+XJS
lmzr0KDJq5ZNSEZV4IprvAYBeEtvdBfLrRM4kifpVDE7ZrVXtKOIGDsxdEEBeqqy
LzN/Ly+uECsga2hoc+P/ZHMULMZkCfrOyWdeXz7BR/acLZJoT579
""")

        self.assertEqual(ppk_loadpub_s(key),
                         (True, b'ssh-rsa', public_blob, comment, None))

        self.assertEqual(ppk_loadpub_s(key[:len(key)//2]),
                         (False, None, b'', None,
                          b"invalid end line in SSH-2 public key file"))

    def testOpenSSHCert(self):
        def per_base_keytype_tests(alg, run_validation_tests=False,
                                   run_ca_rsa_tests=False, ca_signflags=None):
            cert_pub = sign_cert_via_testcrypt(
                make_signature_preimage(
                    key_to_certify = base_key.public_blob(),
                    ca_key = ca_key,
                    certtype = CertType.user,
                    keyid = b'id',
                    serial = 111,
                    principals = [b'username'],
                    valid_after = 1000,
                    valid_before = 2000), ca_key, signflags=ca_signflags)

            certified_key = ssh_key_new_priv(alg + '-cert', cert_pub,
                                             base_key.private_blob())

            # Check the simple certificate methods
            self.assertEqual(certified_key.cert_id_string(), b'id')
            self.assertEqual(certified_key.ca_public_blob(),
                             ca_key.public_blob())
            recovered_base_key = certified_key.base_key()
            self.assertEqual(recovered_base_key.public_blob(),
                             base_key.public_blob())
            self.assertEqual(recovered_base_key.private_blob(),
                             base_key.private_blob())

            # Check that an ordinary key also supports base_key()
            redundant_base_key = base_key.base_key()
            self.assertEqual(redundant_base_key.public_blob(),
                             base_key.public_blob())
            self.assertEqual(redundant_base_key.private_blob(),
                             base_key.private_blob())

            # Test signing and verifying using the certified key type
            test_string = b'hello, world'
            base_sig = base_key.sign(test_string, 0)
            certified_sig = certified_key.sign(test_string, 0)
            self.assertEqual(base_sig, certified_sig)
            self.assertEqual(certified_key.verify(base_sig, test_string), True)

            # Check a successful certificate verification
            result, err = certified_key.check_cert(
                False, b'username', 1000, '')
            self.assertEqual(result, True)

            # If the key type is RSA, check that the validator rejects
            # wrong kinds of CA signature
            if run_ca_rsa_tests:
                forbid_all = ",".join(["permit_rsa_sha1=false",
                                       "permit_rsa_sha256=false,"
                                       "permit_rsa_sha512=false"])
                result, err = certified_key.check_cert(
                    False, b'username', 1000, forbid_all)
                self.assertEqual(result, False)

                algname = ("rsa-sha2-512" if ca_signflags == 4 else
                           "rsa-sha2-256" if ca_signflags == 2 else
                           "ssh-rsa")
                self.assertEqual(err, (
                    "Certificate signature uses '{}' signature type "
                    "(forbidden by user configuration)".format(algname)
                    .encode("ASCII")))

                permitflag = ("permit_rsa_sha512" if ca_signflags == 4 else
                              "permit_rsa_sha256" if ca_signflags == 2 else
                              "permit_rsa_sha1")
                result, err = certified_key.check_cert(
                    False, b'username', 1000, "{},{}=true".format(
                        forbid_all, permitflag))
                self.assertEqual(result, True)

            # That's the end of the tests we need to repeat for all
            # the key types. Now we move on to detailed tests of the
            # validation, which are independent of key type, so we
            # only need to test this part once.
            if not run_validation_tests:
                return

            # Check cert verification at the other end of the valid
            # time range
            result, err = certified_key.check_cert(
                False, b'username', 1999, '')
            self.assertEqual(result, True)

            # Oops, wrong certificate type
            result, err = certified_key.check_cert(
                True, b'username', 1000, '')
            self.assertEqual(result, False)
            self.assertEqual(err, b'Certificate type is user; expected host')

            # Oops, wrong username
            result, err = certified_key.check_cert(
                False, b'someoneelse', 1000, '')
            self.assertEqual(result, False)
            self.assertEqual(err, b'Certificate\'s username list ["username"] '
                             b'does not contain expected username "someoneelse"')

            # Oops, time is wrong. (But we can't check the full error
            # message including the translated start/end times, because
            # those vary with LC_TIME.)
            result, err = certified_key.check_cert(
                False, b'someoneelse', 999, '')
            self.assertEqual(result, False)
            self.assertEqual(err[:30], b'Certificate is not valid until')
            result, err = certified_key.check_cert(
                False, b'someoneelse', 2000, '')
            self.assertEqual(result, False)
            self.assertEqual(err[:22], b'Certificate expired at')

            # Modify the certificate so that the signature doesn't validate
            username_position = cert_pub.index(b'username')
            bytelist = list(cert_pub)
            bytelist[username_position] ^= 1
            miscertified_key = ssh_key_new_priv(alg + '-cert', bytes(bytelist),
                                                base_key.private_blob())
            result, err = miscertified_key.check_cert(
                False, b'username', 1000, '')
            self.assertEqual(result, False)
            self.assertEqual(err, b"Certificate's signature is invalid")

            # Make a certificate containing a critical option, to test we
            # reject it
            cert_pub = sign_cert_via_testcrypt(
                make_signature_preimage(
                    key_to_certify = base_key.public_blob(),
                    ca_key = ca_key,
                    certtype = CertType.user,
                    keyid = b'id',
                    serial = 112,
                    principals = [b'username'],
                    critical_options = {b'unknown-option': b'yikes!'}), ca_key)
            certified_key = ssh_key_new_priv(alg + '-cert', cert_pub,
                                               base_key.private_blob())
            result, err = certified_key.check_cert(
                False, b'username', 1000, '')
            self.assertEqual(result, False)
            self.assertEqual(err, b'Certificate specifies an unsupported '
                             b'critical option "unknown-option"')

            # Make a certificate containing a non-critical extension, to
            # test we _accept_ it
            cert_pub = sign_cert_via_testcrypt(
                make_signature_preimage(
                    key_to_certify = base_key.public_blob(),
                    ca_key = ca_key,
                    certtype = CertType.user,
                    keyid = b'id',
                    serial = 113,
                    principals = [b'username'],
                    extensions = {b'unknown-ext': b'whatever, dude'}), ca_key)
            certified_key = ssh_key_new_priv(alg + '-cert', cert_pub,
                                               base_key.private_blob())
            result, err = certified_key.check_cert(
                False, b'username', 1000, '')
            self.assertEqual(result, True)

            # Make a certificate on the CA key, and re-sign the main
            # key using that, to ensure that two-level certs are rejected
            ca_self_certificate = sign_cert_via_testcrypt(
                make_signature_preimage(
                    key_to_certify = ca_key.public_blob(),
                    ca_key = ca_key,
                    certtype = CertType.user,
                    keyid = b'id',
                    serial = 111,
                    principals = [b"doesn't matter"],
                    valid_after = 1000,
                    valid_before = 2000), ca_key, signflags=ca_signflags)
            self_signed_ca_key = ssh_key_new_pub(
                alg + '-cert', ca_self_certificate)
            cert_pub = sign_cert_via_testcrypt(
                make_signature_preimage(
                    key_to_certify = base_key.public_blob(),
                    ca_key = self_signed_ca_key,
                    certtype = CertType.user,
                    keyid = b'id',
                    serial = 111,
                    principals = [b'username'],
                    valid_after = 1000,
                    valid_before = 2000), ca_key, signflags=ca_signflags)
            certified_key = ssh_key_new_priv(alg + '-cert', cert_pub,
                                             base_key.private_blob())
            result, err = certified_key.check_cert(
                False, b'username', 1500, '')
            self.assertEqual(result, False)
            self.assertEqual(
                err, b'Certificate is signed with a certified key '
                b'(forbidden by OpenSSH certificate specification)')

            # Now try a host certificate. We don't need to do _all_ the
            # checks over again, but at least make sure that setting
            # CertType.host leads to the certificate validating with
            # host=True and not with host=False.
            #
            # Also, in this test, give two hostnames.
            cert_pub = sign_cert_via_testcrypt(
                make_signature_preimage(
                    key_to_certify = base_key.public_blob(),
                    ca_key = ca_key,
                    certtype = CertType.host,
                    keyid = b'id',
                    serial = 114,
                    principals = [b'hostname.example.com',
                                  b'hostname2.example.com'],
                    valid_after = 1000,
                    valid_before = 2000), ca_key)

            certified_key = ssh_key_new_priv(alg + '-cert', cert_pub,
                                             base_key.private_blob())

            # Check certificate type
            result, err = certified_key.check_cert(
                True, b'hostname.example.com', 1000, '')
            self.assertEqual(result, True)
            result, err = certified_key.check_cert(
                False, b'hostname.example.com', 1000, '')
            self.assertEqual(result, False)
            self.assertEqual(err, b'Certificate type is host; expected user')

            # Check the second hostname and an unknown one
            result, err = certified_key.check_cert(
                True, b'hostname2.example.com', 1000, '')
            self.assertEqual(result, True)
            result, err = certified_key.check_cert(
                True, b'hostname3.example.com', 1000, '')
            self.assertEqual(result, False)
            self.assertEqual(err, b'Certificate\'s hostname list ['
                             b'"hostname.example.com", "hostname2.example.com"] '
                             b'does not contain expected hostname '
                             b'"hostname3.example.com"')

            # And just for luck, try a totally unknown certificate type,
            # making sure that it's rejected in both modes and gives the
            # right error message
            cert_pub = sign_cert_via_testcrypt(
                make_signature_preimage(
                    key_to_certify = base_key.public_blob(),
                    ca_key = ca_key,
                    certtype = 12345,
                    keyid = b'id',
                    serial = 114,
                    principals = [b'username', b'hostname.example.com'],
                    valid_after = 1000,
                    valid_before = 2000), ca_key)
            certified_key = ssh_key_new_priv(alg + '-cert', cert_pub,
                                             base_key.private_blob())
            result, err = certified_key.check_cert(
                False, b'username', 1000, '')
            self.assertEqual(result, False)
            self.assertEqual(err, b'Certificate type is unknown value 12345; '
                             b'expected user')
            result, err = certified_key.check_cert(
                True, b'hostname.example.com', 1000, '')
            self.assertEqual(result, False)
            self.assertEqual(err, b'Certificate type is unknown value 12345; '
                             b'expected host')

        ca_key = ssh_key_new_priv('ed25519', b64('AAAAC3NzaC1lZDI1NTE5AAAAIMUJEFAmSV/qtoxSmVOHUgTMKYjqkDy8fTfsfCKV+sN7'), b64('AAAAIK4STyaf63xHidqhvUop9/OKiYqSh/YEWLCp1lL5Vs4u'))

        base_key = ssh_key_new_priv('ed25519', b64('AAAAC3NzaC1lZDI1NTE5AAAAIMt0/CMBL+64GQ/r/JyGxo6oHs86i9bOHhMJYbDbxEJf'), b64('AAAAIB38jy02ZWYb4EXrJG9RIljEhqidrG5DdhZvMvoeOTZs'))
        per_base_keytype_tests('ed25519', run_validation_tests=True)

        base_key = ssh_key_new_priv('p256', b64('AAAAE2VjZHNhLXNoYTItbmlzdHAyNTYAAAAIbmlzdHAyNTYAAABBBGc8VXplXScdWckJgAw6Hag5PP7g0JEVdLY5lP2ujvVxU5GwwquYLbX3yyj1zY5h2n9GoXrnRxzR5+5g8wsNjTA='), b64('AAAAICVRicPD5MyOHfKdnC/8IP84t+nQ4bqmMUyX7NHyCKjS'))
        per_base_keytype_tests('p256')

        base_key = ssh_key_new_priv('p384', b64('AAAAE2VjZHNhLXNoYTItbmlzdHAzODQAAAAIbmlzdHAzODQAAABhBLITujAbKwHDEzVDFqWtA+CleAhN/Y+53mHbEoTpU0aof9L+2lHeUshXdxHDLxY69wO5+WfqWJCwSY58PuXIZzIisQkvIKq6LhpzK6C5JpWJ8Kbv7su+qZPf5sYoxx0xZg=='), b64('AAAAMHyQTQYcIA/bR4ZvWS86ohb5Lu0MhzjD8bUb3q8jnROOe3BrE9I8oJcx+l1lddPouA=='))
        per_base_keytype_tests('p384')

        base_key = ssh_key_new_priv('p521', b64('AAAAE2VjZHNhLXNoYTItbmlzdHA1MjEAAAAIbmlzdHA1MjEAAACFBADButwMRGdLkFhWcSDsLhRhgyrLQq1/A0M8x4GgEmesh4iydo4tGKZR14GhHvx150IWTE1Tre4wyH+1FsTfAlpUBgBDQjsZE0D3u3SLp4qjjhzyrJGhEUDd9J6lsr6JrXbTefz5+LkM9m5l86y9PoAgT+F25OiTYlfvR5qx/pzIPoCnpA=='), b64('AAAAQgFV8xBXC7XZNxdW1oWg6yCZjys2AX4beZVehE9A2R/4m11dHnfqoE1FzbRxj9xqwKvHZRhMOJ//DYuhtcG6+6yHsA=='))
        per_base_keytype_tests('p521')

        base_key = ssh_key_new_priv('dsa', b64('AAAAB3NzaC1kc3MAAABCAXgDrF9Fw/Ty+QcoljAGjGL/Ph5+NBQqUYADm4wxF+aazjQXLuZ0VW9OdYBisgDZlYDj/w7y9NxCBgax2BSkhDNxAAAAFQC/YwnFzcom6cRRHPXtOUDLi2I29QAAAEIAqGOUYpfFPwzhgAmYXwWKdK8ouSUplNE29FOpv6NYjyf7k+tLSWF3b8oZdtw6XP8lr4vcKXC9Ik0YpKYKM7iKfb8AAABCAUDCcojlDLQmLHg8HhFCtT/CpayNh4OfmSrP8XOwJnFD/eBaSGuPB5EvGd+m6gr+Pc0RSAlWP1aIzUbYkQ33Yk58'), b64('AAAAFQChVuOTNrCwLSJygxlRQhDwHozwSg=='))
        per_base_keytype_tests('dsa')

        base_key = ssh_key_new_priv('rsa', b64('AAAAB3NzaC1yc2EAAAADAQABAAAAgQDXLnqGPQLL9byoHFQWPiF5Uzcd0KedMRRJmuwyCAWprlh8EN43mL2F7q27Uv54m/ztqW4DsVtiCN6cDYvB9QPNYFR5npwsEAJ06Ro4s9ZpFsZVOvitqeoYIs+jkS8vq5V8X4hwLlJ8vXYPD6rHJhOz6HFpImHmVu40Mu5lq+MCQQ=='), b64('AAAAgH5dBwrJzVilKHK4oBCnz9SFr7pMjAHdjoJi/g2rdFfe0IubBEQ16CY8sb1t0Y5WXEPc2YRFpNp/RurxcX8nOWFPzgNJXEtkKpKO9Juqu5hL4xcf8QKC2aJFk3EXrn/M6dXEdjqN4UhsT6iFTsHKU4b8T6VTtgKzwkOdic/YotaBAAAAQQD6liDTlzTKzLhbypI6l+y2BGA3Kkzz71Y2o7XH/6bZ6HJOFgHuJeL3eNQptzd8Q+ctfvR0fa2PItYydDOlVUeZAAAAQQDb1IsO1/fkflDZhPQT2XOxtrjgQhotKjr6CSmJtDNmo1mOCN+mOgxtDfJ0PNEEM1P9CO2Ia3njtkxt4Ep2EpjpAAAAQQClRxLEHsRK9nMPZ4HW45iyw5dHhYar9pYUql2VnixWQxrHy13ZIaWxi6xwWjuPglrdBgEQfYwH9KGmlFmZXT/Z'))
        per_base_keytype_tests('rsa')

        # Now switch to an RSA certifying key, and test different RSA
        # signature subtypes being used to sign the certificate
        ca_key = ssh_key_new_priv('rsa', b64('AAAAB3NzaC1yc2EAAAADAQABAAAAgQCKHiavhtnAZQLUPtYlzlQmVTHSKq2ChCKZP0cLNtN2YSS0/f4D1hi8W04Qh/JuSXZAdUThTAVjxDmxpiOMNwa/2WDXMuqip47dzZSQxtSdvTfeL9TVC/M1NaOzy8bqFx6pzi37zPATETT4PP1Zt/Pd23ZJYhwjxSyTlqj7529v0w=='), b64('AAAAgCwTZyEIlaCyG28EBm7WI0CAW3/IIsrNxATHjrJjcqQKaB5iF5e90PL66DSaTaEoTFZRlgOXsPiffBHXBO0P+lTyZ2jlq2J2zgeofRH3Yong4BT4xDtqBKtxixgC1MAHmrOnRXjAcDUiLxIGgU0YKSv0uAlgARsUwDsk0GEvK+jBAAAAQQDMi7liRBQ4/Z6a4wDL/rVnIJ9x+2h2UPK9J8U7f97x/THIBtfkbf9O7nDP6onValuSr86tMR24DJZsEXaGPwjDAAAAQQCs3J3D3jNVwwk16oySRSjA5x3tKCEITYMluyXX06cvFew8ldgRCYl1sh8RYAfbBKXhnJD77qIxtVNaF1yl/guxAAAAQFTRdKRUF2wLu/K/Rr34trwKrV6aW0GWyHlLuWvF7FUB85aDmtqYI2BSk92mVCKHBNw2T3cJMabN9JOznjtADiM='))
        per_base_keytype_tests('rsa', run_ca_rsa_tests=True)
        per_base_keytype_tests('rsa', run_ca_rsa_tests=True, ca_signflags=2)
        per_base_keytype_tests('rsa', run_ca_rsa_tests=True, ca_signflags=4)

    def testAESGCMBlockBoundaries(self):
        # For standard AES-GCM test vectors, see the separate tests in
        # standard_test_vectors.testAESGCM. This function will test
        # the local interface, including the skip length and the
        # machinery for incremental MAC update.

        def aesgcm(key, iv, aes_impl, gcm_impl):
            c = ssh_cipher_new('aes{:d}_gcm_{}'.format(8*len(key), aes_impl))
            if c is None: return None, None # skip test if HW AES not available
            m = ssh2_mac_new('aesgcm_{}'.format(gcm_impl), c)
            if m is None: return None, None # skip test if HW GCM not available
            c.setkey(key)
            c.setiv(iv + b'\0'*4)
            m.setkey(b'')
            return c, m

        def test_one(aes_impl, gcm_impl):
            # An actual test from a session with OpenSSH, which
            # demonstrates that the implementation in practice matches up
            # to what the test vectors say. This is its SSH2_MSG_EXT_INFO
            # packet.
            key = unhex('dbf98b2f56c83fb2f9476aa876511225')
            iv = unhex('9af15ecccf2bacaaa9625a6a')
            plain = unhex('1007000000020000000f736572766572'
                          '2d7369672d616c6773000000db737368'
                          '2d656432353531392c736b2d7373682d'
                          '65643235353139406f70656e7373682e'
                          '636f6d2c7373682d7273612c7273612d'
                          '736861322d3235362c7273612d736861'
                          '322d3531322c7373682d6473732c6563'
                          '6473612d736861322d6e697374703235'
                          '362c65636473612d736861322d6e6973'
                          '74703338342c65636473612d73686132'
                          '2d6e697374703532312c736b2d656364'
                          '73612d736861322d6e69737470323536'
                          '406f70656e7373682e636f6d2c776562'
                          '617574686e2d736b2d65636473612d73'
                          '6861322d6e69737470323536406f7065'
                          '6e7373682e636f6d0000001f7075626c'
                          '69636b65792d686f7374626f756e6440'
                          '6f70656e7373682e636f6d0000000130'
                          '5935130804ad4b19ed2789210290c438')
            aad = unhex('00000130')
            cipher = unhex('c4b88f35c1ef8aa6225033c3f185d648'
                           '3c485d84930d5846f7851daacbff49d5'
                           '8cf72169fca7ab3c170376df65dd69de'
                           'c40a94c6b8e3da6d61161ab19be27466'
                           '02e0dfa3330faae291ef4173a20e87a4'
                           'd40728c645baa72916c1958531ef7b54'
                           '27228513e53005e6d17b9bb384b8d8c1'
                           '92b8a10b731459eed5a0fb120c283412'
                           'e34445981df1257f1c35a06196731fed'
                           '1b3115f419e754de0b634bf68768cb02'
                           '29e70bb2259cedb5101ff6a4ac19aaad'
                           '46f1c30697361b45d6c152c3069cee6b'
                           'd46e9785d65ea6bf7fca41f0ac3c8e93'
                           'ce940b0059c39d51e49c17f60d48d633'
                           '5bae4402faab61d8d65221b24b400e65'
                           '89f941ff48310231a42641851ea00832'
                           '2c2d188f4cc6a4ec6002161c407d0a92'
                           'f1697bb319fbec1ca63fa8e7ac171c85'
                           '5b60142bfcf4e5b0a9ada3451799866e')

            c, m = aesgcm(key, iv, aes_impl, gcm_impl)
            if c is None or m is None: return # skip if HW impl unavailable
            len_dec = c.decrypt_length(aad, 123)
            self.assertEqual(len_dec, aad) # length not actually encrypted
            m.start()
            # We expect 4 bytes skipped (the sequence number that
            # ChaCha20-Poly1305 wants at the start of its MAC), and 4
            # bytes AAD. These were initialised by the call to
            # encrypt_length.
            m.update(b'fake' + aad + cipher)
            self.assertEqualBin(m.genresult(),
                                unhex('4a5a6d57d54888b4e58c57a96e00b73a'))
            self.assertEqualBin(c.decrypt(cipher), plain)

            c, m = aesgcm(key, iv, aes_impl, gcm_impl)
            len_enc = c.encrypt_length(aad, 123)
            self.assertEqual(len_enc, aad) # length not actually encrypted
            self.assertEqualBin(c.encrypt(plain), cipher)

            # Test incremental update.
            def testIncremental(skiplen, aad, plain):
                key, iv = b'SomeRandomKeyVal', b'SomeRandomIV'
                mac_input = b'x' * skiplen + aad + plain

                c, m = aesgcm(key, iv, aes_impl, gcm_impl)
                aesgcm_set_prefix_lengths(m, skiplen, len(aad))

                m.start()
                m.update(mac_input)
                reference_mac = m.genresult()

                # Break the input just once, at each possible byte
                # position.
                for i in range(1, len(mac_input)):
                    c.setiv(iv + b'\0'*4)
                    m.setkey(b'')
                    aesgcm_set_prefix_lengths(m, skiplen, len(aad))
                    m.start()
                    m.update(mac_input[:i])
                    m.update(mac_input[i:])
                    self.assertEqualBin(m.genresult(), reference_mac)

                # Feed the entire input in a byte at a time.
                c.setiv(iv + b'\0'*4)
                m.setkey(b'')
                aesgcm_set_prefix_lengths(m, skiplen, len(aad))
                m.start()
                for i in range(len(mac_input)):
                    m.update(mac_input[i:i+1])
                self.assertEqualBin(m.genresult(), reference_mac)

            # Incremental test with more than a full block of each thing
            testIncremental(23, b'abcdefghijklmnopqrst',
                            b'Lorem ipsum dolor sit amet')

            # Incremental test with exactly a full block of each thing
            testIncremental(16, b'abcdefghijklmnop',
                            b'Lorem ipsum dolo')

            # Incremental test with less than a full block of each thing
            testIncremental(7, b'abcdefghij',
                            b'Lorem ipsum')

        for aes_impl in get_aes_impls():
            for gcm_impl in get_aesgcm_impls():
                with self.subTest(aes_impl=aes_impl, gcm_impl=gcm_impl):
                    test_one(aes_impl, gcm_impl)

    def testAESGCMIV(self):
        key = b'SomeRandomKeyVal'

        def test(gcm, cbc, iv_fixed, iv_msg):
            gcm.setiv(ssh_uint32(iv_fixed) + ssh_uint64(iv_msg) + b'fake')

            cbc.setiv(b'\0' * 16)
            preimage = cbc.decrypt(gcm.encrypt(b'\0' * 16))
            self.assertEqualBin(preimage, ssh_uint32(iv_fixed) +
                                ssh_uint64(iv_msg) + ssh_uint32(1))
            cbc.setiv(b'\0' * 16)
            preimage = cbc.decrypt(gcm.encrypt(b'\0' * 16))
            self.assertEqualBin(preimage, ssh_uint32(iv_fixed) +
                                ssh_uint64(iv_msg) + ssh_uint32(2))

            gcm.next_message()
            iv_msg = (iv_msg + 1) & ((1<<64)-1)

            cbc.setiv(b'\0' * 16)
            preimage = cbc.decrypt(gcm.encrypt(b'\0' * 16))
            self.assertEqualBin(preimage, ssh_uint32(iv_fixed) +
                                ssh_uint64(iv_msg) + ssh_uint32(1))
            cbc.setiv(b'\0' * 16)
            preimage = cbc.decrypt(gcm.encrypt(b'\0' * 16))
            self.assertEqualBin(preimage, ssh_uint32(iv_fixed) +
                                ssh_uint64(iv_msg) + ssh_uint32(2))


        for impl in get_aes_impls():
            with self.subTest(aes_impl=impl):
                gcm = ssh_cipher_new('aes{:d}_gcm_{}'.format(8*len(key), impl))
                if gcm is None: continue  # skip if HW AES unavailable
                gcm.setkey(key)

                cbc = ssh_cipher_new('aes{:d}_cbc_{}'.format(8*len(key), impl))
                # assume if gcm_<impl> is available, cbc_<impl> will be too
                cbc.setkey(key)

                # A simple test to ensure the low word gets
                # incremented and that the whole IV looks basically
                # the way we expect it to
                test(gcm, cbc, 0x27182818, 0x3141592653589793)

                # Test that carries are propagated into the high word
                test(gcm, cbc, 0x27182818, 0x00000000FFFFFFFF)

                # Test that carries _aren't_ propagated out of the
                # high word of the message counter into the fixed word
                # at the top
                test(gcm, cbc, 0x27182818, 0xFFFFFFFFFFFFFFFF)

    def testMLKEMValidation(self):
        # Test validation of hostile inputs (wrong length,
        # out-of-range mod q values, mismatching hashes).
        for params in 'mlkem512', 'mlkem768', 'mlkem1024':
            with self.subTest(params=params):
                ek, dk = mlkem_keygen_internal(
                    params,
                    b'arbitrary 32-byte test d string!',
                    b'and another for z, wibbly-wobbly')

                m = b'I suppose we need m as well, ooh'

                # Baseline test: without anything changed, encaps succeeds.
                success, c, k = mlkem_encaps_internal(params, ek, m)
                self.assertTrue(success)

                # We must check ek has the right length
                success, _, _ = mlkem_encaps_internal(params, ek[:-1], m)
                self.assertFalse(success)
                success, _, _ = mlkem_encaps_internal(params, ek + b'!', m)
                self.assertFalse(success)

                # Must reject if a polynomial coefficient is replaced
                # with something out of range. Even if it's _only
                # just_ out of range, the modulus 3329 itself. So
                # replace the first coefficient (first 12 bits) with
                # 3329.
                ek_bytes = list(ek)
                ek_bytes[0] = 3329 & 0xFF
                ek_bytes[1] = (ek_bytes[1] & 0xF0) | (3329 >> 8)
                success, _, _ = mlkem_encaps_internal(
                    params, bytes(ek_bytes), m)
                self.assertFalse(success)

                # Now do the same with the last polynomial
                # coefficient, which occurs 32 bytes before the end of
                # ek. (The last 32 bytes are the matrix seed, which
                # can be anything.)
                ek_bytes = list(ek)
                ek_bytes[-33] = 3329 >> 4
                ek_bytes[-34] = (ek_bytes[-34] & 0x0F) | ((3329 << 4) & 0xF0)
                success, _, _ = mlkem_encaps_internal(
                    params, bytes(ek_bytes), m)
                self.assertFalse(success)

                # Baseline test of decaps.
                self.assertEqual(mlkem_decaps(params, dk, c), (True, k))

                fail = (False, b'') # expected return value on validation fail
                # Modify the length of dk or c, and make sure decaps fails
                self.assertEqual(mlkem_decaps(params, dk[:-1], c), fail)
                self.assertEqual(mlkem_decaps(params, dk + b'?', c), fail)
                self.assertEqual(mlkem_decaps(params, dk, c[:-1]), fail)
                self.assertEqual(mlkem_decaps(params, dk, c + b'*'), fail)

                # Tinker with the enclosed copy of ek, and ensure
                # that's detected.
                eklen = len(ek)
                ekstart = len(dk) - 64 - eklen
                self.assertEqualBin(dk[ekstart:ekstart+eklen], ek)
                dk_bytes = list(dk)
                dk_bytes[ekstart] ^= 1
                self.assertEqual(
                    mlkem_decaps(params, bytes(dk_bytes), c), fail)

    def testEd25519Overflow(self):
        test_key = ssh_key_new_priv('ed25519', b64('AAAAC3NzaC1lZDI1NTE5AAAAIMt0/CMBL+64GQ/r/JyGxo6oHs86i9bOHhMJYbDbxEJf'), b64('AAAAIB38jy02ZWYb4EXrJG9RIljEhqidrG5DdhZvMvoeOTZs'))
        test_string = b'hello, world'
        good_sig = test_key.sign(test_string, 0)
        self.assertTrue(test_key.verify(good_sig, test_string))
        prefixlen = 4 + len('ssh-ed25519') + 4
        self.assertEqual(len(good_sig), prefixlen + 64)
        good_sstr = good_sig[prefixlen+32:]
        good_s = decode_le_integer(good_sstr)
        bad_s = good_s + ed25519.G_order
        bad_sstr = le_integer(bad_s, 256)
        bad_sig = good_sig[:prefixlen+32] + bad_sstr
        self.assertEqual(len(bad_sig), len(good_sig))
        self.assertFalse(test_key.verify(bad_sig, test_string))

    def testWeierstrassBogusAssertionRegressionSignature(self):
        # This P256 public key contains the same test point as in
        # ecc.testWeierstrassBogusAssertionRegression above, which
        # causes an assertion failure if raised to the power 21.
        tx = 0x858d6d6329394a7720d4c9cb4dbcb38ccff10ef6faa9fc45fc0067a5021ff53e
        ty = 0x32e9d51b7216745493e8ddc0d67fe15d6e39fa0e71cfc82e00045ca2763e0d74
        pubblob = ssh_string(b"ecdsa-sha2-nistp256") + ssh_string(b"nistp256") + ssh_string(b'\x04' + be_integer(tx, 256) + be_integer(ty, 256))
        pubkey = ssh_key_new_pub('p256', pubblob)

        # And this signature causes the key to be raised to the power
        # 21, hence any attempt to verify the signature against
        # anything provokes that assertion failure.
        sig = ssh_string(b"ecdsa-sha2-nistp256") + ssh_string(ssh2_mpint(21) + ssh2_mpint(1))

        # The message is unimportant: its hash isn't involved in the
        # calculation leading to the crash. In a fixed version of the
        # code, of course, we expect that this signature is totally
        # bogus, but it should cleanly report failed verification
        # rather than crashing.
        self.assertFalse(ssh_key_verify(pubkey, sig, b'any old message'))

class standard_test_vectors(MyTestBase):
    def testAES(self):
        def vector(cipher, key, plaintext, ciphertext):
            for suffix in get_aes_impls():
                c = ssh_cipher_new("{}_{}".format(cipher, suffix))
                if c is None: return # skip test if HW AES not available
                ssh_cipher_setkey(c, key)

                # The AES test vectors are implicitly in ECB mode,
                # because they're testing the cipher primitive rather
                # than any mode layered on top of it. We fake this by
                # using PuTTY's CBC setting, and clearing the IV to
                # all zeroes before each operation.

                ssh_cipher_setiv(c, b'\x00' * 16)
                self.assertEqualBin(
                    ssh_cipher_encrypt(c, plaintext), ciphertext)

                ssh_cipher_setiv(c, b'\x00' * 16)
                self.assertEqualBin(
                    ssh_cipher_decrypt(c, ciphertext), plaintext)

        # The test vector from FIPS 197 appendix B. (This is also the
        # same key whose key setup phase is shown in detail in
        # appendix A.)
        vector('aes128_cbc',
               unhex('2b7e151628aed2a6abf7158809cf4f3c'),
               unhex('3243f6a8885a308d313198a2e0370734'),
               unhex('3925841d02dc09fbdc118597196a0b32'))

        # The test vectors from FIPS 197 appendix C: the key bytes go
        # 00 01 02 03 ... for as long as needed, and the plaintext
        # bytes go 00 11 22 33 ... FF.
        fullkey = struct.pack("B"*32, *range(32))
        plaintext = struct.pack("B"*16, *[0x11*i for i in range(16)])
        vector('aes128_cbc', fullkey[:16], plaintext,
               unhex('69c4e0d86a7b0430d8cdb78070b4c55a'))
        vector('aes192_cbc', fullkey[:24], plaintext,
               unhex('dda97ca4864cdfe06eaf70a0ec0d7191'))
        vector('aes256_cbc', fullkey[:32], plaintext,
               unhex('8ea2b7ca516745bfeafc49904b496089'))

    def testDES(self):
        c = ssh_cipher_new("des_cbc")
        def vector(key, plaintext, ciphertext):
            key = unhex(key)
            plaintext = unhex(plaintext)
            ciphertext = unhex(ciphertext)

            # Similarly to above, we fake DES ECB by using DES CBC and
            # resetting the IV to zero all the time
            ssh_cipher_setkey(c, key)
            ssh_cipher_setiv(c, b'\x00' * 8)
            self.assertEqualBin(ssh_cipher_encrypt(c, plaintext), ciphertext)
            ssh_cipher_setiv(c, b'\x00' * 8)
            self.assertEqualBin(ssh_cipher_decrypt(c, ciphertext), plaintext)

        # Source: FIPS SP PUB 500-20

        # 'Initial permutation and expansion tests': key fixed at 8
        # copies of the byte 01, but ciphertext and plaintext in turn
        # run through all possible values with exactly 1 bit set.
        # Expected plaintexts and ciphertexts (respectively) listed in
        # the arrays below.
        ipe_key = '01' * 8
        ipe_plaintexts = [
'166B40B44ABA4BD6', '06E7EA22CE92708F', 'D2FD8867D50D2DFE', 'CC083F1E6D9E85F6',
'5B711BC4CEEBF2EE', '0953E2258E8E90A1', 'E07C30D7E4E26E12', '2FBC291A570DB5C4',
'DD7C0BBD61FAFD54', '48221B9937748A23', 'E643D78090CA4207', '8405D1ABE24FB942',
'CE332329248F3228', '1D1CA853AE7C0C5F', '5D86CB23639DBEA9', '1029D55E880EC2D0',
'8DD45A2DDF90796C', 'CAFFC6AC4542DE31', 'EA51D3975595B86B', '8B54536F2F3E64A8',
'866ECEDD8072BB0E', '79E90DBC98F92CCA', 'AB6A20C0620D1C6F', '25EB5FC3F8CF0621',
'4D49DB1532919C9F', '814EEB3B91D90726', '5E0905517BB59BCF', 'CA3A2B036DBC8502',
'FA0752B07D9C4AB8', 'B160E4680F6C696F', 'DF98C8276F54B04B', 'E943D7568AEC0C5C',
'AEB5F5EDE22D1A36', 'E428581186EC8F46', 'E1652C6B138C64A5', 'D106FF0BED5255D7',
'9D64555A9A10B852', 'F02B263B328E2B60', '64FEED9C724C2FAF', '750D079407521363',
'FBE00A8A1EF8AD72', 'A484C3AD38DC9C19', '12A9F5817FF2D65D', 'E7FCE22557D23C97',
'329A8ED523D71AEC', 'E19E275D846A1298', '889DE068A16F0BE6', '2B9F982F20037FA9',
'F356834379D165CD', 'ECBFE3BD3F591A5E', 'E6D5F82752AD63D1', 'ADD0CC8D6E5DEBA1',
'F15D0F286B65BD28', 'B8061B7ECD9A21E5', '424250B37C3DD951', 'D9031B0271BD5A0A',
'0D9F279BA5D87260', '6CC5DEFAAF04512F', '55579380D77138EF', '20B9E767B2FB1456',
'4BD388FF6CD81D4F', '2E8653104F3834EA', 'DD7F121CA5015619', '95F8A5E5DD31D900',
        ]
        ipe_ciphertexts = [
'166B40B44ABA4BD6', '06E7EA22CE92708F', 'D2FD8867D50D2DFE', 'CC083F1E6D9E85F6',
'5B711BC4CEEBF2EE', '0953E2258E8E90A1', 'E07C30D7E4E26E12', '2FBC291A570DB5C4',
'DD7C0BBD61FAFD54', '48221B9937748A23', 'E643D78090CA4207', '8405D1ABE24FB942',
'CE332329248F3228', '1D1CA853AE7C0C5F', '5D86CB23639DBEA9', '1029D55E880EC2D0',
'8DD45A2DDF90796C', 'CAFFC6AC4542DE31', 'EA51D3975595B86B', '8B54536F2F3E64A8',
'866ECEDD8072BB0E', '79E90DBC98F92CCA', 'AB6A20C0620D1C6F', '25EB5FC3F8CF0621',
'4D49DB1532919C9F', '814EEB3B91D90726', '5E0905517BB59BCF', 'CA3A2B036DBC8502',
'FA0752B07D9C4AB8', 'B160E4680F6C696F', 'DF98C8276F54B04B', 'E943D7568AEC0C5C',
'AEB5F5EDE22D1A36', 'E428581186EC8F46', 'E1652C6B138C64A5', 'D106FF0BED5255D7',
'9D64555A9A10B852', 'F02B263B328E2B60', '64FEED9C724C2FAF', '750D079407521363',
'FBE00A8A1EF8AD72', 'A484C3AD38DC9C19', '12A9F5817FF2D65D', 'E7FCE22557D23C97',
'329A8ED523D71AEC', 'E19E275D846A1298', '889DE068A16F0BE6', '2B9F982F20037FA9',
'F356834379D165CD', 'ECBFE3BD3F591A5E', 'E6D5F82752AD63D1', 'ADD0CC8D6E5DEBA1',
'F15D0F286B65BD28', 'B8061B7ECD9A21E5', '424250B37C3DD951', 'D9031B0271BD5A0A',
'0D9F279BA5D87260', '6CC5DEFAAF04512F', '55579380D77138EF', '20B9E767B2FB1456',
'4BD388FF6CD81D4F', '2E8653104F3834EA', 'DD7F121CA5015619', '95F8A5E5DD31D900',
        ]
        ipe_single_bits = ["{:016x}".format(1 << bit) for bit in range(64)]
        for plaintext, ciphertext in zip(ipe_plaintexts, ipe_single_bits):
            vector(ipe_key, plaintext, ciphertext)
        for plaintext, ciphertext in zip(ipe_single_bits, ipe_ciphertexts):
            vector(ipe_key, plaintext, ciphertext)

        # 'Key permutation tests': plaintext fixed at all zeroes, key
        # is a succession of tweaks of the previous key made by
        # replacing each 01 byte in turn with one containing a
        # different single set bit (e.g. 01 20 01 01 01 01 01 01).
        # Expected ciphertexts listed.
        kp_ciphertexts = [
'95A8D72813DAA94D', '0EEC1487DD8C26D5', '7AD16FFB79C45926', 'D3746294CA6A6CF3',
'809F5F873C1FD761', 'C02FAFFEC989D1FC', '4615AA1D33E72F10', '2055123350C00858',
'DF3B99D6577397C8', '31FE17369B5288C9', 'DFDD3CC64DAE1642', '178C83CE2B399D94',
'50F636324A9B7F80', 'A8468EE3BC18F06D', 'A2DC9E92FD3CDE92', 'CAC09F797D031287',
'90BA680B22AEB525', 'CE7A24F350E280B6', '882BFF0AA01A0B87', '25610288924511C2',
'C71516C29C75D170', '5199C29A52C9F059', 'C22F0A294A71F29F', 'EE371483714C02EA',
'A81FBD448F9E522F', '4F644C92E192DFED', '1AFA9A66A6DF92AE', 'B3C1CC715CB879D8',
'19D032E64AB0BD8B', '3CFAA7A7DC8720DC', 'B7265F7F447AC6F3', '9DB73B3C0D163F54',
'8181B65BABF4A975', '93C9B64042EAA240', '5570530829705592', '8638809E878787A0',
'41B9A79AF79AC208', '7A9BE42F2009A892', '29038D56BA6D2745', '5495C6ABF1E5DF51',
'AE13DBD561488933', '024D1FFA8904E389', 'D1399712F99BF02E', '14C1D7C1CFFEC79E',
'1DE5279DAE3BED6F', 'E941A33F85501303', 'DA99DBBC9A03F379', 'B7FC92F91D8E92E9',
'AE8E5CAA3CA04E85', '9CC62DF43B6EED74', 'D863DBB5C59A91A0', 'A1AB2190545B91D7',
'0875041E64C570F7', '5A594528BEBEF1CC', 'FCDB3291DE21F0C0', '869EFD7F9F265A09',
        ]
        kp_key_repl_bytes = ["{:02x}".format(0x80>>i) for i in range(7)]
        kp_keys = ['01'*j + b + '01'*(7-j)
                   for j in range(8) for b in kp_key_repl_bytes]
        kp_plaintext = '0' * 16
        for key, ciphertext in zip(kp_keys, kp_ciphertexts):
            vector(key, kp_plaintext, ciphertext)

        # 'Data permutation test': plaintext fixed at all zeroes,
        # pairs of key and expected ciphertext listed below.
        dp_keys_and_ciphertexts = [
'1046913489980131:88D55E54F54C97B4', '1007103489988020:0C0CC00C83EA48FD',
'10071034C8980120:83BC8EF3A6570183', '1046103489988020:DF725DCAD94EA2E9',
'1086911519190101:E652B53B550BE8B0', '1086911519580101:AF527120C485CBB0',
'5107B01519580101:0F04CE393DB926D5', '1007B01519190101:C9F00FFC74079067',
'3107915498080101:7CFD82A593252B4E', '3107919498080101:CB49A2F9E91363E3',
'10079115B9080140:00B588BE70D23F56', '3107911598080140:406A9A6AB43399AE',
'1007D01589980101:6CB773611DCA9ADA', '9107911589980101:67FD21C17DBB5D70',
'9107D01589190101:9592CB4110430787', '1007D01598980120:A6B7FF68A318DDD3',
'1007940498190101:4D102196C914CA16', '0107910491190401:2DFA9F4573594965',
'0107910491190101:B46604816C0E0774', '0107940491190401:6E7E6221A4F34E87',
'19079210981A0101:AA85E74643233199', '1007911998190801:2E5A19DB4D1962D6',
'10079119981A0801:23A866A809D30894', '1007921098190101:D812D961F017D320',
'100791159819010B:055605816E58608F', '1004801598190101:ABD88E8B1B7716F1',
'1004801598190102:537AC95BE69DA1E1', '1004801598190108:AED0F6AE3C25CDD8',
'1002911498100104:B3E35A5EE53E7B8D', '1002911598190104:61C79C71921A2EF8',
'1002911598100201:E2F5728F0995013C', '1002911698100101:1AEAC39A61F0A464',
        ]
        dp_plaintext = '0' * 16
        for key_and_ciphertext in dp_keys_and_ciphertexts:
            key, ciphertext = key_and_ciphertext.split(":")
            vector(key, dp_plaintext, ciphertext)

        # Tests intended to select every entry in every S-box. Full
        # arbitrary triples (key, plaintext, ciphertext).
        sb_complete_tests = [
            '7CA110454A1A6E57:01A1D6D039776742:690F5B0D9A26939B',
            '0131D9619DC1376E:5CD54CA83DEF57DA:7A389D10354BD271',
            '07A1133E4A0B2686:0248D43806F67172:868EBB51CAB4599A',
            '3849674C2602319E:51454B582DDF440A:7178876E01F19B2A',
            '04B915BA43FEB5B6:42FD443059577FA2:AF37FB421F8C4095',
            '0113B970FD34F2CE:059B5E0851CF143A:86A560F10EC6D85B',
            '0170F175468FB5E6:0756D8E0774761D2:0CD3DA020021DC09',
            '43297FAD38E373FE:762514B829BF486A:EA676B2CB7DB2B7A',
            '07A7137045DA2A16:3BDD119049372802:DFD64A815CAF1A0F',
            '04689104C2FD3B2F:26955F6835AF609A:5C513C9C4886C088',
            '37D06BB516CB7546:164D5E404F275232:0A2AEEAE3FF4AB77',
            '1F08260D1AC2465E:6B056E18759F5CCA:EF1BF03E5DFA575A',
            '584023641ABA6176:004BD6EF09176062:88BF0DB6D70DEE56',
            '025816164629B007:480D39006EE762F2:A1F9915541020B56',
            '49793EBC79B3258F:437540C8698F3CFA:6FBF1CAFCFFD0556',
            '4FB05E1515AB73A7:072D43A077075292:2F22E49BAB7CA1AC',
            '49E95D6D4CA229BF:02FE55778117F12A:5A6B612CC26CCE4A',
            '018310DC409B26D6:1D9D5C5018F728C2:5F4C038ED12B2E41',
            '1C587F1C13924FEF:305532286D6F295A:63FAC0D034D9F793',
        ]
        for test in sb_complete_tests:
            key, plaintext, ciphertext = test.split(":")
            vector(key, plaintext, ciphertext)

    def testMD5(self):
        MD5 = lambda s: hash_str('md5', s)

        # The test vectors from RFC 1321 section A.5.
        self.assertEqualBin(MD5(""),
                            unhex('d41d8cd98f00b204e9800998ecf8427e'))
        self.assertEqualBin(MD5("a"),
                            unhex('0cc175b9c0f1b6a831c399e269772661'))
        self.assertEqualBin(MD5("abc"),
                            unhex('900150983cd24fb0d6963f7d28e17f72'))
        self.assertEqualBin(MD5("message digest"),
                            unhex('f96b697d7cb7938d525a2f31aaf161d0'))
        self.assertEqualBin(MD5("abcdefghijklmnopqrstuvwxyz"),
                            unhex('c3fcd3d76192e4007dfb496cca67e13b'))
        self.assertEqualBin(MD5("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                "abcdefghijklmnopqrstuvwxyz0123456789"),
                            unhex('d174ab98d277d9f5a5611c2c9f419d9f'))
        self.assertEqualBin(MD5("1234567890123456789012345678901234567890"
                                "1234567890123456789012345678901234567890"),
                            unhex('57edf4a22be3c955ac49da2e2107b67a'))

    def testHmacMD5(self):
        # The test vectors from the RFC 2104 Appendix.
        self.assertEqualBin(mac_str('hmac_md5', unhex('0b'*16), "Hi There"),
                         unhex('9294727a3638bb1c13f48ef8158bfc9d'))
        self.assertEqualBin(mac_str('hmac_md5', "Jefe",
                                 "what do ya want for nothing?"),
                         unhex('750c783e6ab0b503eaa86e310a5db738'))
        self.assertEqualBin(mac_str('hmac_md5', unhex('aa'*16), unhex('dd'*50)),
                         unhex('56be34521d144c88dbb8c733f0e8b3f6'))

    def testSHA1(self):
        for hashname in get_implementations("sha1"):
            if ssh_hash_new(hashname) is None:
                continue # skip testing of unavailable HW implementation

            # Test cases from RFC 6234 section 8.5, omitting the ones
            # whose input is not a multiple of 8 bits
            self.assertEqualBin(hash_str(hashname, "abc"), unhex(
                "a9993e364706816aba3e25717850c26c9cd0d89d"))
            self.assertEqualBin(hash_str(hashname,
                "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
                unhex("84983e441c3bd26ebaae4aa1f95129e5e54670f1"))
            self.assertEqualBin(hash_str_iter(hashname,
                ("a" * 1000 for _ in range(1000))), unhex(
                "34aa973cd4c4daa4f61eeb2bdbad27316534016f"))
            self.assertEqualBin(hash_str(hashname,
                "01234567012345670123456701234567" * 20), unhex(
                "dea356a2cddd90c7a7ecedc5ebb563934f460452"))
            self.assertEqualBin(hash_str(hashname, b"\x5e"), unhex(
                "5e6f80a34a9798cafc6a5db96cc57ba4c4db59c2"))
            self.assertEqualBin(hash_str(hashname,
                unhex("9a7dfdf1ecead06ed646aa55fe757146")), unhex(
                "82abff6605dbe1c17def12a394fa22a82b544a35"))
            self.assertEqualBin(hash_str(hashname, unhex(
                "f78f92141bcd170ae89b4fba15a1d59f"
                "3fd84d223c9251bdacbbae61d05ed115"
                "a06a7ce117b7beead24421ded9c32592"
                "bd57edeae39c39fa1fe8946a84d0cf1f"
                "7beead1713e2e0959897347f67c80b04"
                "00c209815d6b10a683836fd5562a56ca"
                "b1a28e81b6576654631cf16566b86e3b"
                "33a108b05307c00aff14a768ed735060"
                "6a0f85e6a91d396f5b5cbe577f9b3880"
                "7c7d523d6d792f6ebc24a4ecf2b3a427"
                "cdbbfb")), unhex(
                "cb0082c8f197d260991ba6a460e76e202bad27b3"))

    def testSHA256(self):
        for hashname in get_implementations("sha256"):
            if ssh_hash_new(hashname) is None:
                continue # skip testing of unavailable HW implementation

            # Test cases from RFC 6234 section 8.5, omitting the ones
            # whose input is not a multiple of 8 bits
            self.assertEqualBin(hash_str(hashname, "abc"),
                                unhex("ba7816bf8f01cfea414140de5dae2223"
                                      "b00361a396177a9cb410ff61f20015ad"))
            self.assertEqualBin(hash_str(hashname,
                "abcdbcdecdefdefgefghfghighijhijk""ijkljklmklmnlmnomnopnopq"),
                                unhex("248d6a61d20638b8e5c026930c3e6039"
                                      "a33ce45964ff2167f6ecedd419db06c1"))
            self.assertEqualBin(
                hash_str_iter(hashname, ("a" * 1000 for _ in range(1000))),
                unhex("cdc76e5c9914fb9281a1c7e284d73e67"
                      "f1809a48a497200e046d39ccc7112cd0"))
            self.assertEqualBin(
                hash_str(hashname, "01234567012345670123456701234567" * 20),
                unhex("594847328451bdfa85056225462cc1d8"
                      "67d877fb388df0ce35f25ab5562bfbb5"))
            self.assertEqualBin(hash_str(hashname, b"\x19"),
                                unhex("68aa2e2ee5dff96e3355e6c7ee373e3d"
                                      "6a4e17f75f9518d843709c0c9bc3e3d4"))
            self.assertEqualBin(
                hash_str(hashname, unhex("e3d72570dcdd787ce3887ab2cd684652")),
                unhex("175ee69b02ba9b58e2b0a5fd13819cea"
                      "573f3940a94f825128cf4209beabb4e8"))
            self.assertEqualBin(hash_str(hashname, unhex(
                "8326754e2277372f4fc12b20527afef0"
                "4d8a056971b11ad57123a7c137760000"
                "d7bef6f3c1f7a9083aa39d810db31077"
                "7dab8b1e7f02b84a26c773325f8b2374"
                "de7a4b5a58cb5c5cf35bcee6fb946e5b"
                "d694fa593a8beb3f9d6592ecedaa66ca"
                "82a29d0c51bcf9336230e5d784e4c0a4"
                "3f8d79a30a165cbabe452b774b9c7109"
                "a97d138f129228966f6c0adc106aad5a"
                "9fdd30825769b2c671af6759df28eb39"
                "3d54d6")), unhex(
                    "97dbca7df46d62c8a422c941dd7e835b"
                    "8ad3361763f7e9b2d95f4f0da6e1ccbc"))

    def testSHA384(self):
        for hashname in get_implementations("sha384"):
            if ssh_hash_new(hashname) is None:
                continue # skip testing of unavailable HW implementation

            # Test cases from RFC 6234 section 8.5, omitting the ones
            # whose input is not a multiple of 8 bits
            self.assertEqualBin(hash_str(hashname, "abc"), unhex(
                'cb00753f45a35e8bb5a03d699ac65007272c32ab0eded163'
                '1a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7'))
            self.assertEqualBin(hash_str(hashname,
                "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"),
                unhex('09330c33f71147e83d192fc782cd1b4753111b173b3b05d2'
                      '2fa08086e3b0f712fcc7c71a557e2db966c3e9fa91746039'))
            self.assertEqualBin(hash_str_iter(hashname,
                ("a" * 1000 for _ in range(1000))), unhex(
                '9d0e1809716474cb086e834e310a4a1ced149e9c00f24852'
                '7972cec5704c2a5b07b8b3dc38ecc4ebae97ddd87f3d8985'))
            self.assertEqualBin(hash_str(hashname,
                "01234567012345670123456701234567" * 20), unhex(
                '2fc64a4f500ddb6828f6a3430b8dd72a368eb7f3a8322a70'
                'bc84275b9c0b3ab00d27a5cc3c2d224aa6b61a0d79fb4596'))
            self.assertEqualBin(hash_str(hashname, b"\xB9"), unhex(
                'bc8089a19007c0b14195f4ecc74094fec64f01f90929282c'
                '2fb392881578208ad466828b1c6c283d2722cf0ad1ab6938'))
            self.assertEqualBin(hash_str(hashname,
                unhex("a41c497779c0375ff10a7f4e08591739")), unhex(
                'c9a68443a005812256b8ec76b00516f0dbb74fab26d66591'
                '3f194b6ffb0e91ea9967566b58109cbc675cc208e4c823f7'))
            self.assertEqualBin(hash_str(hashname, unhex(
                "399669e28f6b9c6dbcbb6912ec10ffcf74790349b7dc8fbe4a8e7b3b5621"
                "db0f3e7dc87f823264bbe40d1811c9ea2061e1c84ad10a23fac1727e7202"
                "fc3f5042e6bf58cba8a2746e1f64f9b9ea352c711507053cf4e5339d5286"
                "5f25cc22b5e87784a12fc961d66cb6e89573199a2ce6565cbdf13dca4038"
                "32cfcb0e8b7211e83af32a11ac17929ff1c073a51cc027aaedeff85aad7c"
                "2b7c5a803e2404d96d2a77357bda1a6daeed17151cb9bc5125a422e941de"
                "0ca0fc5011c23ecffefdd09676711cf3db0a3440720e1615c1f22fbc3c72"
                "1de521e1b99ba1bd5577408642147ed096")), unhex(
                '4f440db1e6edd2899fa335f09515aa025ee177a79f4b4aaf'
                '38e42b5c4de660f5de8fb2a5b2fbd2a3cbffd20cff1288c0'))

    def testSHA512(self):
        for hashname in get_implementations("sha512"):
            if ssh_hash_new(hashname) is None:
                continue # skip testing of unavailable HW implementation

            # Test cases from RFC 6234 section 8.5, omitting the ones
            # whose input is not a multiple of 8 bits
            self.assertEqualBin(hash_str(hashname, "abc"), unhex(
                'ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55'
                'd39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94f'
                'a54ca49f'))
            self.assertEqualBin(hash_str(hashname,
                "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"),
                unhex('8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299'
                'aeadb6889018501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26'
                '545e96e55b874be909'))
            self.assertEqualBin(hash_str_iter(hashname,
                ("a" * 1000 for _ in range(1000))), unhex(
                'e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa9'
                '73ebde0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217'
                'ad8cc09b'))
            self.assertEqualBin(hash_str(hashname,
                "01234567012345670123456701234567" * 20), unhex(
                '89d05ba632c699c31231ded4ffc127d5a894dad412c0e024db872d1abd2b'
                'a8141a0f85072a9be1e2aa04cf33c765cb510813a39cd5a84c4acaa64d3f'
                '3fb7bae9'))
            self.assertEqualBin(hash_str(hashname, b"\xD0"), unhex(
                '9992202938e882e73e20f6b69e68a0a7149090423d93c81bab3f21678d4a'
                'ceeee50e4e8cafada4c85a54ea8306826c4ad6e74cece9631bfa8a549b4a'
                'b3fbba15'))
            self.assertEqualBin(hash_str(hashname,
                unhex("8d4e3c0e3889191491816e9d98bff0a0")), unhex(
                'cb0b67a4b8712cd73c9aabc0b199e9269b20844afb75acbdd1c153c98289'
                '24c3ddedaafe669c5fdd0bc66f630f6773988213eb1b16f517ad0de4b2f0'
                'c95c90f8'))
            self.assertEqualBin(hash_str(hashname, unhex(
                "a55f20c411aad132807a502d65824e31a2305432aa3d06d3e282a8d84e0d"
                "e1de6974bf495469fc7f338f8054d58c26c49360c3e87af56523acf6d89d"
                "03e56ff2f868002bc3e431edc44df2f0223d4bb3b243586e1a7d92493669"
                "4fcbbaf88d9519e4eb50a644f8e4f95eb0ea95bc4465c8821aacd2fe15ab"
                "4981164bbb6dc32f969087a145b0d9cc9c67c22b763299419cc4128be9a0"
                "77b3ace634064e6d99283513dc06e7515d0d73132e9a0dc6d3b1f8b246f1"
                "a98a3fc72941b1e3bb2098e8bf16f268d64f0b0f4707fe1ea1a1791ba2f3"
                "c0c758e5f551863a96c949ad47d7fb40d2")), unhex(
                'c665befb36da189d78822d10528cbf3b12b3eef726039909c1a16a270d48'
                '719377966b957a878e720584779a62825c18da26415e49a7176a894e7510'
                'fd1451f5'))

    def testSHA3(self):
        # Source: all the SHA-3 test strings from
        # https://csrc.nist.gov/projects/cryptographic-standards-and-guidelines/example-values#aHashing
        # which are a multiple of 8 bits long.

        self.assertEqualBin(hash_str('sha3_224', ''), unhex("6b4e03423667dbb73b6e15454f0eb1abd4597f9a1b078e3f5b5a6bc7"))
        self.assertEqualBin(hash_str('sha3_224', unhex('a3')*200), unhex("9376816aba503f72f96ce7eb65ac095deee3be4bf9bbc2a1cb7e11e0"))
        self.assertEqualBin(hash_str('sha3_256', ''), unhex("a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a"))
        self.assertEqualBin(hash_str('sha3_256', unhex('a3')*200), unhex("79f38adec5c20307a98ef76e8324afbfd46cfd81b22e3973c65fa1bd9de31787"))
        self.assertEqualBin(hash_str('sha3_384', ''), unhex("0c63a75b845e4f7d01107d852e4c2485c51a50aaaa94fc61995e71bbee983a2ac3713831264adb47fb6bd1e058d5f004"))
        self.assertEqualBin(hash_str('sha3_384', unhex('a3')*200), unhex("1881de2ca7e41ef95dc4732b8f5f002b189cc1e42b74168ed1732649ce1dbcdd76197a31fd55ee989f2d7050dd473e8f"))
        self.assertEqualBin(hash_str('sha3_512', ''), unhex("a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a615b2123af1f5f94c11e3e9402c3ac558f500199d95b6d3e301758586281dcd26"))
        self.assertEqualBin(hash_str('sha3_512', unhex('a3')*200), unhex("e76dfad22084a8b1467fcf2ffa58361bec7628edf5f3fdc0e4805dc48caeeca81b7c13c30adf52a3659584739a2df46be589c51ca1a4a8416df6545a1ce8ba00"))
        self.assertEqualBin(hash_str('shake256_114bytes', ''), unhex("46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762fd75dc4ddd8c0f200cb05019d67b592f6fc821c49479ab48640292eacb3b7c4be141e96616fb13957692cc7edd0b45ae3dc07223c8e92937bef84bc0eab862853349ec75546f58fb7c2775c38462c5010d846"))
        self.assertEqualBin(hash_str('shake256_114bytes', unhex('a3')*200), unhex("cd8a920ed141aa0407a22d59288652e9d9f1a7ee0c1e7c1ca699424da84a904d2d700caae7396ece96604440577da4f3aa22aeb8857f961c4cd8e06f0ae6610b1048a7f64e1074cd629e85ad7566048efc4fb500b486a3309a8f26724c0ed628001a1099422468de726f1061d99eb9e93604"))

    def testSHA3XOF(self):
        # Cherry-picked examples from CAVS 19.0, testing both SHAKE128
        # and SHAKE256, each with a long input and a long output.

        xof = shake128_xof_from_input(unhex('a6fe00064257aa318b621c5eb311d32bb8004c2fa1a969d205d71762cc5d2e633907992629d1b69d9557ff6d5e8deb454ab00f6e497c89a4fea09e257a6fa2074bd818ceb5981b3e3faefd6e720f2d1edd9c5e4a5c51e5009abf636ed5bca53fe159c8287014a1bd904f5c8a7501625f79ac81eb618f478ce21cae6664acffb30572f059e1ad0fc2912264e8f1ca52af26c8bf78e09d75f3dd9fc734afa8770abe0bd78c90cc2ff448105fb16dd2c5b7edd8611a62e537db9331f5023e16d6ec150cc6e706d7c7fcbfff930c7281831fd5c4aff86ece57ed0db882f59a5fe403105d0592ca38a081fed84922873f538ee774f13b8cc09bd0521db4374aec69f4bae6dcb66455822c0b84c91a3474ffac2ad06f0a4423cd2c6a49d4f0d6242d6a1890937b5d9835a5f0ea5b1d01884d22a6c1718e1f60b3ab5e232947c76ef70b344171083c688093b5f1475377e3069863'))
        self.assertEqualBin(shake_xof_read(xof, 128//8), unhex("3109d9472ca436e805c6b3db2251a9bc"))

        xof = shake128_xof_from_input(unhex('0a13ad2c7a239b4ba73ea6592ae84ea9'))
        self.assertEqualBin(shake_xof_read(xof, 1120//8), unhex("5feaf99c15f48851943ff9baa6e5055d8377f0dd347aa4dbece51ad3a6d9ce0c01aee9fe2260b80a4673a909b532adcdd1e421c32d6460535b5fe392a58d2634979a5a104d6c470aa3306c400b061db91c463b2848297bca2bc26d1864ba49d7ff949ebca50fbf79a5e63716dc82b600bd52ca7437ed774d169f6bf02e46487956fba2230f34cd2a0485484d"))

        xof = shake256_xof_from_input(unhex('dc5a100fa16df1583c79722a0d72833d3bf22c109b8889dbd35213c6bfce205813edae3242695cfd9f59b9a1c203c1b72ef1a5423147cb990b5316a85266675894e2644c3f9578cebe451a09e58c53788fe77a9e850943f8a275f830354b0593a762bac55e984db3e0661eca3cb83f67a6fb348e6177f7dee2df40c4322602f094953905681be3954fe44c4c902c8f6bba565a788b38f13411ba76ce0f9f6756a2a2687424c5435a51e62df7a8934b6e141f74c6ccf539e3782d22b5955d3baf1ab2cf7b5c3f74ec2f9447344e937957fd7f0bdfec56d5d25f61cde18c0986e244ecf780d6307e313117256948d4230ebb9ea62bb302cfe80d7dfebabc4a51d7687967ed5b416a139e974c005fff507a96'))
        self.assertEqualBin(shake_xof_read(xof, 256//8), unhex("2bac5716803a9cda8f9e84365ab0a681327b5ba34fdedfb1c12e6e807f45284b"))

        xof = shake256_xof_from_input(unhex('8d8001e2c096f1b88e7c9224a086efd4797fbf74a8033a2d422a2b6b8f6747e4'))
        self.assertEqualBin(shake_xof_read(xof, 2000//8), unhex("2e975f6a8a14f0704d51b13667d8195c219f71e6345696c49fa4b9d08e9225d3d39393425152c97e71dd24601c11abcfa0f12f53c680bd3ae757b8134a9c10d429615869217fdd5885c4db174985703a6d6de94a667eac3023443a8337ae1bc601b76d7d38ec3c34463105f0d3949d78e562a039e4469548b609395de5a4fd43c46ca9fd6ee29ada5efc07d84d553249450dab4a49c483ded250c9338f85cd937ae66bb436f3b4026e859fda1ca571432f3bfc09e7c03ca4d183b741111ca0483d0edabc03feb23b17ee48e844ba2408d9dcfd0139d2e8c7310125aee801c61ab7900d1efc47c078281766f361c5e6111346235e1dc38325666c"))

    def testBLAKE2b(self):
        # Test case from RFC 7693 appendix A.
        self.assertEqualBin(hash_str('blake2b', b'abc'), unhex(
            "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1"
            "7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923"))

        # A small number of test cases from the larger test vector
        # set, testing multiple blocks and the empty input.
        self.assertEqualBin(hash_str('blake2b', b''), unhex(
            "786a02f742015903c6c6fd852552d272912f4740e15847618a86e217f71f5419"
            "d25e1031afee585313896444934eb04b903a685b1448b755d56f701afe9be2ce"))
        self.assertEqualBin(hash_str('blake2b', unhex('00')), unhex(
            "2fa3f686df876995167e7c2e5d74c4c7b6e48f8068fe0e44208344d480f7904c"
            "36963e44115fe3eb2a3ac8694c28bcb4f5a0f3276f2e79487d8219057a506e4b"))
        self.assertEqualBin(hash_str('blake2b', bytes(range(255))), unhex(
            "5b21c5fd8868367612474fa2e70e9cfa2201ffeee8fafab5797ad58fefa17c9b"
            "5b107da4a3db6320baaf2c8617d5a51df914ae88da3867c2d41f0cc14fa67928"))

        # You can get this test program to run the full version of the
        # test vectors by modifying the source temporarily to set this
        # variable to a pathname where you downloaded the JSON file
        # blake2-kat.json.
        blake2_test_vectors_path = None
        if blake2_test_vectors_path is not None:
            with open(blake2_test_vectors_path) as fh:
                vectors = json.load(fh)
            for vector in vectors:
                if vector['hash'] != 'blake2b':
                    continue
                if len(vector['key']) != 0:
                    continue

                h = blake2b_new_general(len(vector['out']) // 2)
                ssh_hash_update(h, unhex(vector['in']))
                digest = ssh_hash_digest(h)
                self.assertEqualBin(digest, unhex(vector['out']))

    def testArgon2(self):
        # draft-irtf-cfrg-argon2-12 section 5
        self.assertEqualBin(
            argon2('d', 32, 3, 4, 32, b'\x01' * 32, b'\x02' * 16,
                   b'\x03' * 8, b'\x04' * 12),
            unhex("512b391b6f1162975371d30919734294"
                  "f868e3be3984f3c1a13a4db9fabe4acb"))
        self.assertEqualBin(
            argon2('i', 32, 3, 4, 32, b'\x01' * 32, b'\x02' * 16,
                   b'\x03' * 8, b'\x04' * 12),
            unhex("c814d9d1dc7f37aa13f0d77f2494bda1"
                  "c8de6b016dd388d29952a4c4672b6ce8"))
        self.assertEqualBin(
            argon2('id', 32, 3, 4, 32, b'\x01' * 32, b'\x02' * 16,
                   b'\x03' * 8, b'\x04' * 12),
            unhex("0d640df58d78766c08c037a34a8b53c9"
                  "d01ef0452d75b65eb52520e96b01e659"))

    def testHmacSHA(self):
        # Test cases from RFC 6234 section 8.5.
        def vector(key, message, s1=None, s256=None, s512=None):
            if s1 is not None:
                self.assertEqualBin(
                    mac_str('hmac_sha1', key, message), unhex(s1))
            if s256 is not None:
                self.assertEqualBin(
                    mac_str('hmac_sha256', key, message), unhex(s256))
            if s512 is not None:
                self.assertEqualBin(
                    mac_str('hmac_sha512', key, message), unhex(s512))
        vector(
            unhex("0b"*20), "Hi There",
            "b617318655057264e28bc0b6fb378c8ef146be00",
            "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
            "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cde"
            "daa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854")
        vector(
            "Jefe", "what do ya want for nothing?",
            "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79",
            "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
            "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea250554"
            "9758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737")
        vector(
            unhex("aa"*20), unhex('dd'*50),
            "125d7342b9ac11cd91a39af48aa17b4f63f175d3",
            "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565FE",
            "fa73b0089d56a284efb0f0756c890be9b1b5dbdd8ee81a3655f83e33b2279d39"
            "bf3e848279a722c806b485a47e67c807b946a337bee8942674278859e13292fb")
        vector(
            unhex("0102030405060708090a0b0c0d0e0f10111213141516171819"),
            unhex("cd"*50),
            "4c9007f4026250c6bc8414f9bf50c86c2d7235da",
            "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b",
            "b0ba465637458c6990e5a8c5f61d4af7e576d97ff94b872de76f8050361ee3db"
            "a91ca5c11aa25eb4d679275cc5788063a5f19741120c4f2de2adebeb10a298dd")
        vector(
            unhex("aa"*80),
            "Test Using Larger Than Block-Size Key - Hash Key First",
            s1="aa4ae5e15272d00e95705637ce8a3b55ed402112")
        vector(
            unhex("aa"*131),
            "Test Using Larger Than Block-Size Key - Hash Key First",
            s256="60e431591ee0b67f0d8a26aacbf5b77f"
            "8e0bc6213728c5140546040f0ee37f54", s512=
            "80b24263c7c1a3ebb71493c1dd7be8b49b46d1f41b4aeec1121b013783f8f352"
            "6b56d037e05f2598bd0fd2215d6a1e5295e64f73f63f0aec8b915a985d786598")
        vector(
            unhex("aa"*80),
            "Test Using Larger Than Block-Size Key and "
            "Larger Than One Block-Size Data",
            s1="e8e99d0f45237d786d6bbaa7965c7808bbff1a91")
        vector(
            unhex("aa"*131),
            "This is a test using a larger than block-size key and a "
            "larger than block-size data. The key needs to be hashed "
            "before being used by the HMAC algorithm.",
            s256="9b09ffa71b942fcb27635fbcd5b0e944"
            "bfdc63644f0713938a7f51535c3a35e2", s512=
            "e37b6a775dc87dbaa4dfa9f96e5e3ffddebd71f8867289865df5a32d20cdc944"
            "b6022cac3c4982b10d5eeb55c3e4de15134676fb6de0446065c97440fa8c6a58")

    def testEd25519(self):
        def vector(privkey, pubkey, message, signature):
            x, y = ecc_edwards_get_affine(eddsa_public(
                mp_from_bytes_le(privkey), 'ed25519'))
            self.assertEqual(int(y) | ((int(x) & 1) << 255),
                             int(mp_from_bytes_le(pubkey)))
            pubblob = ssh_string(b"ssh-ed25519") + ssh_string(pubkey)
            privblob = ssh_string(privkey)
            sigblob = ssh_string(b"ssh-ed25519") + ssh_string(signature)
            pubkey = ssh_key_new_pub('ed25519', pubblob)
            self.assertTrue(ssh_key_verify(pubkey, sigblob, message))
            privkey = ssh_key_new_priv('ed25519', pubblob, privblob)
            # By testing that the signature is exactly the one expected in
            # the test vector and not some equivalent one generated with a
            # different nonce, we're verifying in particular that we do
            # our deterministic nonce generation in the manner specified
            # by Ed25519. Getting that wrong would lead to no obvious
            # failure, but would surely turn out to be a bad idea sooner
            # or later...
            self.assertEqualBin(ssh_key_sign(privkey, message, 0), sigblob)

        # A cherry-picked example from DJB's test vector data at
        # https://ed25519.cr.yp.to/python/sign.input, which is too
        # large to copy into here in full.
        privkey = unhex(
            'c89955e0f7741d905df0730b3dc2b0ce1a13134e44fef3d40d60c020ef19df77')
        pubkey = unhex(
            'fdb30673402faf1c8033714f3517e47cc0f91fe70cf3836d6c23636e3fd2287c')
        message = unhex(
            '507c94c8820d2a5793cbf3442b3d71936f35fe3afef316')
        signature = unhex(
            '7ef66e5e86f2360848e0014e94880ae2920ad8a3185a46b35d1e07dea8fa8ae4'
            'f6b843ba174d99fa7986654a0891c12a794455669375bf92af4cc2770b579e0c')
        vector(privkey, pubkey, message, signature)

        # You can get this test program to run the full version of
        # DJB's test vectors by modifying the source temporarily to
        # set this variable to a pathname where you downloaded the
        # file.
        ed25519_test_vector_path = None
        if ed25519_test_vector_path is not None:
            with open(ed25519_test_vector_path) as f:
                for line in iter(f.readline, ""):
                    words = line.split(":")
                    # DJB's test vector input format concatenates a
                    # spare copy of the public key to the end of the
                    # private key, and a spare copy of the message to
                    # the end of the signature. Strip those off.
                    privkey = unhex(words[0])[:32]
                    pubkey = unhex(words[1])
                    message = unhex(words[2])
                    signature = unhex(words[3])[:64]
                    vector(privkey, pubkey, message, signature)

    def testEd448(self):
        def vector(privkey, pubkey, message, signature):
            x, y = ecc_edwards_get_affine(eddsa_public(
                mp_from_bytes_le(privkey), 'ed448'))
            self.assertEqual(int(y) | ((int(x) & 1) << 455),
                             int(mp_from_bytes_le(pubkey)))
            pubblob = ssh_string(b"ssh-ed448") + ssh_string(pubkey)
            privblob = ssh_string(privkey)
            sigblob = ssh_string(b"ssh-ed448") + ssh_string(signature)
            pubkey = ssh_key_new_pub('ed448', pubblob)
            self.assertTrue(ssh_key_verify(pubkey, sigblob, message))
            privkey = ssh_key_new_priv('ed448', pubblob, privblob)
            # Deterministic signature check as in Ed25519
            self.assertEqualBin(ssh_key_sign(privkey, message, 0), sigblob)

        # Source: RFC 8032 section 7.4

        privkey = unhex('6c82a562cb808d10d632be89c8513ebf6c929f34ddfa8c9f63c9960ef6e348a3528c8a3fcc2f044e39a3fc5b94492f8f032e7549a20098f95b')
        pubkey = unhex('5fd7449b59b461fd2ce787ec616ad46a1da1342485a70e1f8a0ea75d80e96778edf124769b46c7061bd6783df1e50f6cd1fa1abeafe8256180')
        message = b''
        signature = unhex('533a37f6bbe457251f023c0d88f976ae2dfb504a843e34d2074fd823d41a591f2b233f034f628281f2fd7a22ddd47d7828c59bd0a21bfd3980ff0d2028d4b18a9df63e006c5d1c2d345b925d8dc00b4104852db99ac5c7cdda8530a113a0f4dbb61149f05a7363268c71d95808ff2e652600')
        vector(privkey, pubkey, message, signature)

        privkey = unhex('c4eab05d357007c632f3dbb48489924d552b08fe0c353a0d4a1f00acda2c463afbea67c5e8d2877c5e3bc397a659949ef8021e954e0a12274e')
        pubkey = unhex('43ba28f430cdff456ae531545f7ecd0ac834a55d9358c0372bfa0c6c6798c0866aea01eb00742802b8438ea4cb82169c235160627b4c3a9480')
        message = unhex('03')
        signature = unhex('26b8f91727bd62897af15e41eb43c377efb9c610d48f2335cb0bd0087810f4352541b143c4b981b7e18f62de8ccdf633fc1bf037ab7cd779805e0dbcc0aae1cbcee1afb2e027df36bc04dcecbf154336c19f0af7e0a6472905e799f1953d2a0ff3348ab21aa4adafd1d234441cf807c03a00')
        vector(privkey, pubkey, message, signature)

        privkey = unhex('cd23d24f714274e744343237b93290f511f6425f98e64459ff203e8985083ffdf60500553abc0e05cd02184bdb89c4ccd67e187951267eb328')
        pubkey = unhex('dcea9e78f35a1bf3499a831b10b86c90aac01cd84b67a0109b55a36e9328b1e365fce161d71ce7131a543ea4cb5f7e9f1d8b00696447001400')
        message = unhex('0c3e544074ec63b0265e0c')
        signature = unhex('1f0a8888ce25e8d458a21130879b840a9089d999aaba039eaf3e3afa090a09d389dba82c4ff2ae8ac5cdfb7c55e94d5d961a29fe0109941e00b8dbdeea6d3b051068df7254c0cdc129cbe62db2dc957dbb47b51fd3f213fb8698f064774250a5028961c9bf8ffd973fe5d5c206492b140e00')
        vector(privkey, pubkey, message, signature)

        privkey = unhex('258cdd4ada32ed9c9ff54e63756ae582fb8fab2ac721f2c8e676a72768513d939f63dddb55609133f29adf86ec9929dccb52c1c5fd2ff7e21b')
        pubkey = unhex('3ba16da0c6f2cc1f30187740756f5e798d6bc5fc015d7c63cc9510ee3fd44adc24d8e968b6e46e6f94d19b945361726bd75e149ef09817f580')
        message = unhex('64a65f3cdedcdd66811e2915')
        signature = unhex('7eeeab7c4e50fb799b418ee5e3197ff6bf15d43a14c34389b59dd1a7b1b85b4ae90438aca634bea45e3a2695f1270f07fdcdf7c62b8efeaf00b45c2c96ba457eb1a8bf075a3db28e5c24f6b923ed4ad747c3c9e03c7079efb87cb110d3a99861e72003cbae6d6b8b827e4e6c143064ff3c00')
        vector(privkey, pubkey, message, signature)

        privkey = unhex('d65df341ad13e008567688baedda8e9dcdc17dc024974ea5b4227b6530e339bff21f99e68ca6968f3cca6dfe0fb9f4fab4fa135d5542ea3f01')
        pubkey = unhex('df9705f58edbab802c7f8363cfe5560ab1c6132c20a9f1dd163483a26f8ac53a39d6808bf4a1dfbd261b099bb03b3fb50906cb28bd8a081f00')
        message = unhex('bd0f6a3747cd561bdddf4640a332461a4a30a12a434cd0bf40d766d9c6d458e5512204a30c17d1f50b5079631f64eb3112182da3005835461113718d1a5ef944')
        signature = unhex('554bc2480860b49eab8532d2a533b7d578ef473eeb58c98bb2d0e1ce488a98b18dfde9b9b90775e67f47d4a1c3482058efc9f40d2ca033a0801b63d45b3b722ef552bad3b4ccb667da350192b61c508cf7b6b5adadc2c8d9a446ef003fb05cba5f30e88e36ec2703b349ca229c2670833900')
        vector(privkey, pubkey, message, signature)

        privkey = unhex('2ec5fe3c17045abdb136a5e6a913e32ab75ae68b53d2fc149b77e504132d37569b7e766ba74a19bd6162343a21c8590aa9cebca9014c636df5')
        pubkey = unhex('79756f014dcfe2079f5dd9e718be4171e2ef2486a08f25186f6bff43a9936b9bfe12402b08ae65798a3d81e22e9ec80e7690862ef3d4ed3a00')
        message = unhex('15777532b0bdd0d1389f636c5f6b9ba734c90af572877e2d272dd078aa1e567cfa80e12928bb542330e8409f3174504107ecd5efac61ae7504dabe2a602ede89e5cca6257a7c77e27a702b3ae39fc769fc54f2395ae6a1178cab4738e543072fc1c177fe71e92e25bf03e4ecb72f47b64d0465aaea4c7fad372536c8ba516a6039c3c2a39f0e4d832be432dfa9a706a6e5c7e19f397964ca4258002f7c0541b590316dbc5622b6b2a6fe7a4abffd96105eca76ea7b98816af0748c10df048ce012d901015a51f189f3888145c03650aa23ce894c3bd889e030d565071c59f409a9981b51878fd6fc110624dcbcde0bf7a69ccce38fabdf86f3bef6044819de11')
        signature = unhex('c650ddbb0601c19ca11439e1640dd931f43c518ea5bea70d3dcde5f4191fe53f00cf966546b72bcc7d58be2b9badef28743954e3a44a23f880e8d4f1cfce2d7a61452d26da05896f0a50da66a239a8a188b6d825b3305ad77b73fbac0836ecc60987fd08527c1a8e80d5823e65cafe2a3d00')
        vector(privkey, pubkey, message, signature)

        privkey = unhex('872d093780f5d3730df7c212664b37b8a0f24f56810daa8382cd4fa3f77634ec44dc54f1c2ed9bea86fafb7632d8be199ea165f5ad55dd9ce8')
        pubkey = unhex('a81b2e8a70a5ac94ffdbcc9badfc3feb0801f258578bb114ad44ece1ec0e799da08effb81c5d685c0c56f64eecaef8cdf11cc38737838cf400')
        message = unhex('6ddf802e1aae4986935f7f981ba3f0351d6273c0a0c22c9c0e8339168e675412a3debfaf435ed651558007db4384b650fcc07e3b586a27a4f7a00ac8a6fec2cd86ae4bf1570c41e6a40c931db27b2faa15a8cedd52cff7362c4e6e23daec0fbc3a79b6806e316efcc7b68119bf46bc76a26067a53f296dafdbdc11c77f7777e972660cf4b6a9b369a6665f02e0cc9b6edfad136b4fabe723d2813db3136cfde9b6d044322fee2947952e031b73ab5c603349b307bdc27bc6cb8b8bbd7bd323219b8033a581b59eadebb09b3c4f3d2277d4f0343624acc817804728b25ab797172b4c5c21a22f9c7839d64300232eb66e53f31c723fa37fe387c7d3e50bdf9813a30e5bb12cf4cd930c40cfb4e1fc622592a49588794494d56d24ea4b40c89fc0596cc9ebb961c8cb10adde976a5d602b1c3f85b9b9a001ed3c6a4d3b1437f52096cd1956d042a597d561a596ecd3d1735a8d570ea0ec27225a2c4aaff26306d1526c1af3ca6d9cf5a2c98f47e1c46db9a33234cfd4d81f2c98538a09ebe76998d0d8fd25997c7d255c6d66ece6fa56f11144950f027795e653008f4bd7ca2dee85d8e90f3dc315130ce2a00375a318c7c3d97be2c8ce5b6db41a6254ff264fa6155baee3b0773c0f497c573f19bb4f4240281f0b1f4f7be857a4e59d416c06b4c50fa09e1810ddc6b1467baeac5a3668d11b6ecaa901440016f389f80acc4db977025e7f5924388c7e340a732e554440e76570f8dd71b7d640b3450d1fd5f0410a18f9a3494f707c717b79b4bf75c98400b096b21653b5d217cf3565c9597456f70703497a078763829bc01bb1cbc8fa04eadc9a6e3f6699587a9e75c94e5bab0036e0b2e711392cff0047d0d6b05bd2a588bc109718954259f1d86678a579a3120f19cfb2963f177aeb70f2d4844826262e51b80271272068ef5b3856fa8535aa2a88b2d41f2a0e2fda7624c2850272ac4a2f561f8f2f7a318bfd5caf9696149e4ac824ad3460538fdc25421beec2cc6818162d06bbed0c40a387192349db67a118bada6cd5ab0140ee273204f628aad1c135f770279a651e24d8c14d75a6059d76b96a6fd857def5e0b354b27ab937a5815d16b5fae407ff18222c6d1ed263be68c95f32d908bd895cd76207ae726487567f9a67dad79abec316f683b17f2d02bf07e0ac8b5bc6162cf94697b3c27cd1fea49b27f23ba2901871962506520c392da8b6ad0d99f7013fbc06c2c17a569500c8a7696481c1cd33e9b14e40b82e79a5f5db82571ba97bae3ad3e0479515bb0e2b0f3bfcd1fd33034efc6245eddd7ee2086ddae2600d8ca73e214e8c2b0bdb2b047c6a464a562ed77b73d2d841c4b34973551257713b753632efba348169abc90a68f42611a40126d7cb21b58695568186f7e569d2ff0f9e745d0487dd2eb997cafc5abf9dd102e62ff66cba87')
        signature = unhex('e301345a41a39a4d72fff8df69c98075a0cc082b802fc9b2b6bc503f926b65bddf7f4c8f1cb49f6396afc8a70abe6d8aef0db478d4c6b2970076c6a0484fe76d76b3a97625d79f1ce240e7c576750d295528286f719b413de9ada3e8eb78ed573603ce30d8bb761785dc30dbc320869e1a00')
        vector(privkey, pubkey, message, signature)

    def testMontgomeryKex(self):
        # Unidirectional tests, consisting of an input random number
        # string and peer public value, giving the expected output
        # shared key. Source: RFC 7748 section 5.2.
        rfc7748s5_2 = [
            ('curve25519',
             'a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4',
             'e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c',
             0xc3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552),
            ('curve25519',
             '4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d',
             'e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493',
             0x95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957),
            ('curve448',
             '3d262fddf9ec8e88495266fea19a34d28882acef045104d0d1aae121700a779c984c24f8cdd78fbff44943eba368f54b29259a4f1c600ad3',
             '06fce640fa3487bfda5f6cf2d5263f8aad88334cbd07437f020f08f9814dc031ddbdc38c19c6da2583fa5429db94ada18aa7a7fb4ef8a086',
             0xce3e4ff95a60dc6697da1db1d85e6afbdf79b50a2412d7546d5f239fe14fbaadeb445fc66a01b0779d98223961111e21766282f73dd96b6f),
            ('curve448',
             '203d494428b8399352665ddca42f9de8fef600908e0d461cb021f8c538345dd77c3e4806e25f46d3315c44e0a5b4371282dd2c8d5be3095f',
             '0fbcc2f993cd56d3305b0b7d9e55d4c1a8fb5dbb52f8e9a1e9b6201b165d015894e56c4d3570bee52fe205e28a78b91cdfbde71ce8d157db',
             0x884a02576239ff7a2f2f63b2db6a9ff37047ac13568e1e30fe63c4a7ad1b3ee3a5700df34321d62077e63633c575c1c954514e99da7c179d),
        ]

        for method, priv, pub, expected in rfc7748s5_2:
            with queued_specific_random_data(unhex(priv)):
                ecdh = ecdh_key_new(method, False)
            key = ecdh_key_getkey(ecdh, unhex(pub))
            self.assertEqual(key, ssh2_mpint(expected))

        # Bidirectional tests, consisting of the input random number
        # strings for both parties, and the expected public values and
        # shared key. Source: RFC 7748 section 6.
        rfc7748s6 = [
            ('curve25519', # section 6.1
             '77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a',
             '8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a',
             '5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb',
             'de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f',
             0x4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742),
            ('curve448', # section 6.2
             '9a8f4925d1519f5775cf46b04b5800d4ee9ee8bae8bc5565d498c28dd9c9baf574a9419744897391006382a6f127ab1d9ac2d8c0a598726b',
             '9b08f7cc31b7e3e67d22d5aea121074a273bd2b83de09c63faa73d2c22c5d9bbc836647241d953d40c5b12da88120d53177f80e532c41fa0',
             '1c306a7ac2a0e2e0990b294470cba339e6453772b075811d8fad0d1d6927c120bb5ee8972b0d3e21374c9c921b09d1b0366f10b65173992d',
             '3eb7a829b0cd20f5bcfc0b599b6feccf6da4627107bdb0d4f345b43027d8b972fc3e34fb4232a13ca706dcb57aec3dae07bdc1c67bf33609',
             0x07fff4181ac6cc95ec1c16a94a0f74d12da232ce40a77552281d282bb60c0b56fd2464c335543936521c24403085d59a449a5037514a879d),
        ]

        for method, apriv, apub, bpriv, bpub, expected in rfc7748s6:
            with queued_specific_random_data(unhex(apriv)):
                alice = ecdh_key_new(method, False)
            with queued_specific_random_data(unhex(bpriv)):
                bob = ecdh_key_new(method, False)
            self.assertEqualBin(ecdh_key_getpublic(alice), unhex(apub))
            self.assertEqualBin(ecdh_key_getpublic(bob), unhex(bpub))
            akey = ecdh_key_getkey(alice, unhex(bpub))
            bkey = ecdh_key_getkey(bob, unhex(apub))
            self.assertEqual(akey, ssh2_mpint(expected))
            self.assertEqual(bkey, ssh2_mpint(expected))

    def testCRC32(self):
        self.assertEqual(crc32_rfc1662("123456789"), 0xCBF43926)
        self.assertEqual(crc32_ssh1("123456789"), 0x2DFD2D88)

        # Source:
        # http://reveng.sourceforge.net/crc-catalogue/17plus.htm#crc.cat.crc-32-iso-hdlc
        # which collected these from various sources.
        reveng_tests = [
            '000000001CDF4421',
            'F20183779DAB24',
            '0FAA005587B2C9B6',
            '00FF55111262A032',
            '332255AABBCCDDEEFF3D86AEB0',
            '926B559BA2DE9C',
            'FFFFFFFFFFFFFFFF',
            'C008300028CFE9521D3B08EA449900E808EA449900E8300102007E649416',
            '6173640ACEDE2D15',
        ]
        for vec in map(unhex, reveng_tests):
            # Each of these test vectors can be read two ways. One
            # interpretation is that the last four bytes are the
            # little-endian encoding of the CRC of the rest. (Because
            # that's how the CRC is attached to a string at the
            # sending end.)
            #
            # The other interpretation is that if you CRC the whole
            # string, _including_ the final four bytes, you expect to
            # get the same value for any correct string (because the
            # little-endian encoding matches the way the rest of the
            # string was interpreted as a polynomial in the first
            # place). That's how a receiver is intended to check
            # things.
            #
            # The expected output value is listed in RFC 1662, and in
            # the reveng.sourceforge.net catalogue, as 0xDEBB20E3. But
            # that's because their checking procedure omits the final
            # complement step that the construction procedure
            # includes. Our crc32_rfc1662 function does do the final
            # complement, so we expect the bitwise NOT of that value,
            # namely 0x2144DF1C.
            expected = struct.unpack("<L", vec[-4:])[0]
            self.assertEqual(crc32_rfc1662(vec[:-4]), expected)
            self.assertEqual(crc32_rfc1662(vec), 0x2144DF1C)

    def testHttpDigest(self):
        # RFC 7616 section 3.9.1
        params = ["Mufasa", "Circle of Life", "http-auth@example.org",
                  "GET", "/dir/index.html", "auth",
                  "7ypf/xlj9XXwfDPEoM4URrv/xwf94BcCAzFZH4GiTo0v",
                  "FQhe/qaU925kfnzjCev0ciny7QMkPqMAFRtzCUYo5tdS", 1,
                  "MD5", False]
        cnonce = b64('f2/wE4q74E6zIJEtWaHKaf5wv/H5QzzpXusqGemxURZJ')
        with queued_specific_random_data(cnonce):
            self.assertEqual(http_digest_response(*params),
                             b'username="Mufasa", '
                             b'realm="http-auth@example.org", '
                             b'uri="/dir/index.html", '
                             b'algorithm=MD5, '
                             b'nonce="7ypf/xlj9XXwfDPEoM4URrv/xwf94BcCAzFZH4GiTo0v", '
                             b'nc=00000001, '
                             b'cnonce="f2/wE4q74E6zIJEtWaHKaf5wv/H5QzzpXusqGemxURZJ", '
                             b'qop=auth, '
                             b'response="8ca523f5e9506fed4657c9700eebdbec", '
                             b'opaque="FQhe/qaU925kfnzjCev0ciny7QMkPqMAFRtzCUYo5tdS"')

        # And again with all the same details except the hash
        params[9] = "SHA-256"
        with queued_specific_random_data(cnonce):
            self.assertEqual(http_digest_response(*params),
                             b'username="Mufasa", '
                             b'realm="http-auth@example.org", '
                             b'uri="/dir/index.html", '
                             b'algorithm=SHA-256, '
                             b'nonce="7ypf/xlj9XXwfDPEoM4URrv/xwf94BcCAzFZH4GiTo0v", '
                             b'nc=00000001, '
                             b'cnonce="f2/wE4q74E6zIJEtWaHKaf5wv/H5QzzpXusqGemxURZJ", '
                             b'qop=auth, '
                             b'response="753927fa0e85d155564e2e272a28d1802ca10daf4496794697cf8db5856cb6c1", '
                             b'opaque="FQhe/qaU925kfnzjCev0ciny7QMkPqMAFRtzCUYo5tdS"')

        # RFC 7616 section 3.9.2, using SHA-512-256 (demonstrating
        # that they think it's just a 256-bit truncation of SHA-512,
        # and not the version defined in FIPS 180-4 which also uses
        # a different initial hash state), and username hashing.
        #
        # We don't actually support SHA-512-256 in the top-level proxy
        # client code (see the comment in proxy/cproxy.h). However,
        # this internal http_digest_response function still provides
        # it, simply so that we can run this test case from the RFC,
        # because it's the only provided test case for username
        # hashing, and this confirms that we've got the preimage right
        # for the username hash.
        params = ["J\u00E4s\u00F8n Doe".encode("UTF-8"),
                  "Secret, or not?", "api@example.org",
                  "GET", "/doe.json", "auth",
                  "5TsQWLVdgBdmrQ0XsxbDODV+57QdFR34I9HAbC/RVvkK",
                  "HRPCssKJSGjCrkzDg8OhwpzCiGPChXYjwrI2QmXDnsOS", 1,
                  "SHA-512-256", True]
        cnonce = b64('NTg6RKcb9boFIAS3KrFK9BGeh+iDa/sm6jUMp2wds69v')
        with queued_specific_random_data(cnonce):
            self.assertEqual(http_digest_response(*params),
                             b'username="488869477bf257147b804c45308cd62ac4e25eb717b12b298c79e62dcea254ec", '
                             b'realm="api@example.org", '
                             b'uri="/doe.json", '
                             b'algorithm=SHA-512-256, '
                             b'nonce="5TsQWLVdgBdmrQ0XsxbDODV+57QdFR34I9HAbC/RVvkK", '
                             b'nc=00000001, '
                             b'cnonce="NTg6RKcb9boFIAS3KrFK9BGeh+iDa/sm6jUMp2wds69v", '
                             b'qop=auth, '
                             b'response="ae66e67d6b427bd3f120414a82e4acff38e8ecd9101d6c861229025f607a79dd", '
                             b'opaque="HRPCssKJSGjCrkzDg8OhwpzCiGPChXYjwrI2QmXDnsOS", '
                             b'userhash=true')

    def testAESGCM(self):
        def test(key, iv, plaintext, aad, ciphertext, mac):
            c = ssh_cipher_new('aes{:d}_gcm'.format(8*len(key)))
            m = ssh2_mac_new('aesgcm_{}'.format(impl), c)
            if m is None: return # skip test if HW GCM not available
            c.setkey(key)
            c.setiv(iv + b'\0'*4)
            m.setkey(b'')
            aesgcm_set_prefix_lengths(m, 0, len(aad))

            # Some test cases have plaintext/ciphertext that is not a
            # multiple of the cipher block size. Our MAC
            # implementation supports this, but the cipher
            # implementation expects block-granular input.
            padlen = 15 & -len(plaintext)
            ciphertext_got = c.encrypt(plaintext + b'0' * padlen)[
                :len(plaintext)]

            m.start()
            m.update(aad + ciphertext)
            mac_got = m.genresult()

            self.assertEqualBin(ciphertext_got, ciphertext)
            self.assertEqualBin(mac_got, mac)

            c.setiv(iv + b'\0'*4)

        for impl in get_aesgcm_impls():
            # 'The Galois/Counter Mode of Operation', McGrew and
            # Viega, Appendix B. All the tests except the ones whose
            # IV is the wrong length, because handling that requires
            # an extra evaluation of the polynomial hash, which is
            # never used in an SSH context, so I didn't implement it
            # just for the sake of test vectors.

            # Test Case 1
            test(unhex('00000000000000000000000000000000'),
                 unhex('000000000000000000000000'),
                 unhex(''), unhex(''), unhex(''),
                 unhex('58e2fccefa7e3061367f1d57a4e7455a'))

            # Test Case 2
            test(unhex('00000000000000000000000000000000'),
                 unhex('000000000000000000000000'),
                 unhex('00000000000000000000000000000000'),
                 unhex(''),
                 unhex('0388dace60b6a392f328c2b971b2fe78'),
                 unhex('ab6e47d42cec13bdf53a67b21257bddf'))

            # Test Case 3
            test(unhex('feffe9928665731c6d6a8f9467308308'),
                 unhex('cafebabefacedbaddecaf888'),
                 unhex('d9313225f88406e5a55909c5aff5269a'
                       '86a7a9531534f7da2e4c303d8a318a72'
                       '1c3c0c95956809532fcf0e2449a6b525'
                       'b16aedf5aa0de657ba637b391aafd255'),
                 unhex(''),
                 unhex('42831ec2217774244b7221b784d0d49c'
                       'e3aa212f2c02a4e035c17e2329aca12e'
                       '21d514b25466931c7d8f6a5aac84aa05'
                       '1ba30b396a0aac973d58e091473f5985'),
                 unhex('4d5c2af327cd64a62cf35abd2ba6fab4'))

            # Test Case 4
            test(unhex('feffe9928665731c6d6a8f9467308308'),
                 unhex('cafebabefacedbaddecaf888'),
                 unhex('d9313225f88406e5a55909c5aff5269a'
                       '86a7a9531534f7da2e4c303d8a318a72'
                       '1c3c0c95956809532fcf0e2449a6b525'
                       'b16aedf5aa0de657ba637b39'),
                 unhex('feedfacedeadbeeffeedfacedeadbeef'
                       'abaddad2'),
                 unhex('42831ec2217774244b7221b784d0d49c'
                       'e3aa212f2c02a4e035c17e2329aca12e'
                       '21d514b25466931c7d8f6a5aac84aa05'
                       '1ba30b396a0aac973d58e091'),
                 unhex('5bc94fbc3221a5db94fae95ae7121a47'))

            # Test Case 7
            test(unhex('00000000000000000000000000000000'
                       '0000000000000000'),
                 unhex('000000000000000000000000'),
                 unhex(''), unhex(''), unhex(''),
                 unhex('cd33b28ac773f74ba00ed1f312572435'))

            # Test Case 8
            test(unhex('00000000000000000000000000000000'
                       '0000000000000000'),
                 unhex('000000000000000000000000'),
                 unhex('00000000000000000000000000000000'),
                 unhex(''),
                 unhex('98e7247c07f0fe411c267e4384b0f600'),
                 unhex('2ff58d80033927ab8ef4d4587514f0fb'))

            # Test Case 9
            test(unhex('feffe9928665731c6d6a8f9467308308'
                       'feffe9928665731c'),
                 unhex('cafebabefacedbaddecaf888'),
                 unhex('d9313225f88406e5a55909c5aff5269a'
                       '86a7a9531534f7da2e4c303d8a318a72'
                       '1c3c0c95956809532fcf0e2449a6b525'
                       'b16aedf5aa0de657ba637b391aafd255'),
                 unhex(''),
                 unhex('3980ca0b3c00e841eb06fac4872a2757'
                       '859e1ceaa6efd984628593b40ca1e19c'
                       '7d773d00c144c525ac619d18c84a3f47'
                       '18e2448b2fe324d9ccda2710acade256'),
                 unhex('9924a7c8587336bfb118024db8674a14'))

            # Test Case 10
            test(unhex('feffe9928665731c6d6a8f9467308308'
                       'feffe9928665731c'),
                 unhex('cafebabefacedbaddecaf888'),
                 unhex('d9313225f88406e5a55909c5aff5269a'
                       '86a7a9531534f7da2e4c303d8a318a72'
                       '1c3c0c95956809532fcf0e2449a6b525'
                       'b16aedf5aa0de657ba637b39'),
                 unhex('feedfacedeadbeeffeedfacedeadbeef'
                       'abaddad2'),
                 unhex('3980ca0b3c00e841eb06fac4872a2757'
                       '859e1ceaa6efd984628593b40ca1e19c'
                       '7d773d00c144c525ac619d18c84a3f47'
                       '18e2448b2fe324d9ccda2710'),
                 unhex('2519498e80f1478f37ba55bd6d27618c'))

            # Test Case 13
            test(unhex('00000000000000000000000000000000'
                       '00000000000000000000000000000000'),
                 unhex('000000000000000000000000'),
                 unhex(''), unhex(''), unhex(''),
                 unhex('530f8afbc74536b9a963b4f1c4cb738b'))

            # Test Case 14
            test(unhex('00000000000000000000000000000000'
                       '00000000000000000000000000000000'),
                 unhex('000000000000000000000000'),
                 unhex('00000000000000000000000000000000'),
                 unhex(''),
                 unhex('cea7403d4d606b6e074ec5d3baf39d18'),
                 unhex('d0d1c8a799996bf0265b98b5d48ab919'))

            # Test Case 15
            test(unhex('feffe9928665731c6d6a8f9467308308'
                       'feffe9928665731c6d6a8f9467308308'),
                 unhex('cafebabefacedbaddecaf888'),
                 unhex('d9313225f88406e5a55909c5aff5269a'
                       '86a7a9531534f7da2e4c303d8a318a72'
                       '1c3c0c95956809532fcf0e2449a6b525'
                       'b16aedf5aa0de657ba637b391aafd255'),
                 unhex(''),
                 unhex('522dc1f099567d07f47f37a32a84427d'
                       '643a8cdcbfe5c0c97598a2bd2555d1aa'
                       '8cb08e48590dbb3da7b08b1056828838'
                       'c5f61e6393ba7a0abcc9f662898015ad'),
                 unhex('b094dac5d93471bdec1a502270e3cc6c'))

            # Test Case 16
            test(unhex('feffe9928665731c6d6a8f9467308308'
                       'feffe9928665731c6d6a8f9467308308'),
                 unhex('cafebabefacedbaddecaf888'),
                 unhex('d9313225f88406e5a55909c5aff5269a'
                       '86a7a9531534f7da2e4c303d8a318a72'
                       '1c3c0c95956809532fcf0e2449a6b525'
                       'b16aedf5aa0de657ba637b39'),
                 unhex('feedfacedeadbeeffeedfacedeadbeef'
                       'abaddad2'),
                 unhex('522dc1f099567d07f47f37a32a84427d'
                       '643a8cdcbfe5c0c97598a2bd2555d1aa'
                       '8cb08e48590dbb3da7b08b1056828838'
                       'c5f61e6393ba7a0abcc9f662'),
                 unhex('76fc6ece0f4e1768cddf8853bb2d551b'))

    def testMLKEM(self):
        # As of 2024-12-04, a set of ML-KEM test vectors live in a git
        # repository at https://github.com/usnistgov/ACVP-Server
        #
        # Within that repository, the two useful files (as of commit
        # 3a7333f638a031c6ed35b6ee31064686eb88c1ec) are:
        # gen-val/json-files/ML-KEM-keyGen-FIPS203/internalProjection.json
        # gen-val/json-files/ML-KEM-encapDecap-FIPS203/internalProjection.json
        #
        # The first contains tests of key generation (input randomness
        # and the expected output key). The second contains tests of
        # encapsulation and decapsulation.
        #
        # The full set of test cases is too large to transcribe into
        # here. But you can run them in full by setting the variable
        # names below to local pathnames where those two files can be
        # found.
        keygen_json_path = None
        encapdecap_json_path = None

        def keygen_test(params, d, z, ek_expected, dk_expected):
            ek_got, dk_got = mlkem_keygen_internal(params, d, z)
            self.assertEqualBin(ek_got, ek_expected)
            self.assertEqualBin(dk_got, dk_expected)

        def encaps_test(params, ek, m, c_expected, k_expected):
            success, c_got, k_got = mlkem_encaps_internal(params, ek, m)
            self.assertTrue(success)
            self.assertEqualBin(c_got, c_expected)
            self.assertEqualBin(k_got, k_expected)

        def decaps_test(params, dk, c, k_expected):
            success, k_got = mlkem_decaps(params, dk, c)
            self.assertTrue(success)
            self.assertEqualBin(k_got, k_expected)

        if keygen_json_path is not None:
            with open(keygen_json_path) as fh:
                keygen_json_data = json.load(fh)
            for testgroup in keygen_json_data['testGroups']:
                # Convert "ML-KEM-768" from the JSON to "mlkem768"
                params = testgroup['parameterSet'].lower().replace('-', '')
                for testcase in testgroup['tests']:
                    with self.subTest(testgroup=testgroup['tgId'],
                                      testcase=testcase['tcId']):
                        keygen_test(
                            params,
                            unhex(testcase['d']), unhex(testcase['z']),
                            unhex(testcase['ek']), unhex(testcase['dk']))

        if encapdecap_json_path is not None:
            with open(encapdecap_json_path) as fh:
                encapdecap_json_data = json.load(fh)
            for testgroup in encapdecap_json_data['testGroups']:
                params = testgroup['parameterSet'].lower().replace('-', '')
                for testcase in testgroup['tests']:
                    with self.subTest(testgroup=testgroup['tgId'],
                                      testcase=testcase['tcId']):
                        ek = unhex(testcase['ek'] if 'ek' in testcase
                                   else testgroup['ek'])
                        dk = unhex(testcase['dk'] if 'dk' in testcase
                                   else testgroup['dk'])
                        c = unhex(testcase['c'])
                        k = unhex(testcase['k'])
                        if testgroup["function"] == "encapsulation":
                            # This is a full test that encapsulates a
                            # key, decapsulates it at the other end,
                            # and checks both sides end up with the
                            # same shared secret.
                            m = unhex(testcase['m'])
                            encaps_test(params, ek, m, c, k)

                        # All tests include decapsulation. The ones
                        # that don't also include encapsulation might
                        # provide _bad_ ciphertexts, to test the
                        # implicit rejection system.
                        decaps_test(params, dk, c, k)

        # We replicate a small number of those test cases here, for
        # ongoing checks that nothing has broken.
        # Keygen test group 1, test case 1
        keygen_test('mlkem512',
                    d=unhex('2CB843A02EF02EE109305F39119FABF49AB90A57FFECB3A0E75E179450F52761'),
                    z=unhex('84CC9121AE56FBF39E67ADBD83AD2D3E3BB80843645206BDD9F2F629E3CC49B7'),
                    ek_expected=unhex('A32439F85A3C21D21A71B9B92A9B64EA0AB84312C77023694FD64EAAB907A43539DDB27BA0A853CC9069EAC8508C653E600B2AC018381B4BB4A879ACDAD342F91179CA8249525CB1968BBE52F755B7F5B43D6663D7A3BF0F3357D8A21D15B52DB3818ECE5B402A60C993E7CF436487B8D2AE91E6C5B88275E75824B0007EF3123C0AB51B5CC61B9B22380DE66C5B20B060CBB986F8123D94060049CDF8036873A7BE109444A0A1CD87A48CAE54192484AF844429C1C58C29AC624CD504F1C44F1E1347822B6F221323859A7F6F754BFE710BDA60276240A4FF2A5350703786F5671F449F20C2A95AE7C2903A42CB3B303FF4C427C08B11B4CD31C418C6D18D0861873BFA0332F11271552ED7C035F0E4BC428C43720B39A65166BA9C2D3D770E130360CC2384E83095B1A159495533F116C7B558B650DB04D5A26EAAA08C3EE57DE45A7F88C6A3CEB24DC5397B88C3CEF003319BB0233FD692FDA1524475B351F3C782182DECF590B7723BE400BE14809C44329963FC46959211D6A623339537848C251669941D90B130258ADF55A720A724E8B6A6CAE3C2264B1624CCBE7B456B30C8C7393294CA5180BC837DD2E45DBD59B6E17B24FE93052EB7C43B27AC3DC249CA0CBCA4FB5897C0B744088A8A0779D32233826A01DD6489952A4825E5358A700BE0E179AC197710D83ECC853E52695E9BF87BB1F6CBD05B02D4E679E3B88DD483B0749B11BD37B383DCCA71F9091834A1695502C4B95FC9118C1CFC34C84C2265BBBC563C282666B60AE5C7F3851D25ECBB5021CC38CB73EB6A3411B1C29046CA66540667D136954460C6FCBC4BC7C049BB047FA67A63B3CC1111C1D8AC27E8058BCCA4A15455858A58358F7A61020BC9C4C17F8B95C268CCB404B9AAB4A272A21A70DAF6B6F15121EE01C156A354AA17087E07702EAB38B3241FDB553F657339D5E29DC5D91B7A5A828EE959FEBB90B07229F6E49D23C3A190297042FB43986955B69C28E1016F77A58B431514D21B888899C3608276081B75F568097CDC1748F32307885815F3AEC9651819AA6873D1A4EB83B1953843B93422519483FEF0059D36BB2DB1F3D468FB068C86E8973733C398EAF00E1702C6734AD8EB3B'),
                    dk_expected=unhex('7FE4206F26BEDB64C1ED0009615245DC98483F663ACC617E65898D596A8836C49FBD3B4A849759AA1546BDA835CAF175642C28280892A7878CC318BCC75B834CB29FDF5360D7F982A52C88AE914DBF02B58BEB8BA887AE8FAB5EB78731C6757805471EBCEC2E38DB1F4B8310D288920D8A492795A390A74BCD55CD8557B4DAABA82C28CB3F152C5231196193A66A8CCF34B80E1F6942C32BCFF96A6E3CF3939B7B942498CC5E4CB8E8468E702759852AA229C0257F02982097338607C0F0F45446FAB4267993B8A5908CAB9C46780134804AE18815B1020527A222EC4B39A3194E661737791714122662D8B9769F6C67DE625C0D483C3D420FF1BB889A727E756281513A70047648D29C0C30F9BE52EC0DEB977CF0F34FC2078483456964743410638C57B5539577BF85669078C356B3462E9FA5807D49591AFA41C1969F65E3405CB64DDF163F26734CE348B9CF4567A33A5969EB326CFB5ADC695DCA0C8B2A7B1F4F404CC7A0981E2CC24C1C23D16AA9B4392415E26C22F4A934D794C1FB4E5A67051123CCD153764DEC99D553529053C3DA550BCEA3AC54136A26A676D2BA8421067068C6381C2A62A727C933702EE5804A31CA865A45588FB74DE7E2223D88C0608A16BFEC4FAD6752DB56B48B8872BF26BA2FFA0CEDE5343BE8143689265E065F41A6925B86C892E62EB0772734F5A357C75CA1AC6DF78AB1B8885AD0819615376D33EBB98F8733A6755803D977BF51C12740424B2B49C28382A6917CBFA034C3F126A38C216C03C35770AD481B9084B5588DA65FF118A74F932C7E537ABE5863FB29A10C09701B441F8399C1F8A637825ACEA3E93180574FDEB88076661AB46951716A500184A040557266598CAF76105E1C1870B43969C3BCC1A04927638017498BB62CAFD3A6B082B7BF7A23450E191799619B925112D072025CA888548C791AA42251504D5D1C1CDDB213303B049E7346E8D83AD587836F35284E109727E66BBCC9521FE0B191630047D158F75640FFEB5456072740021AFD15A45469C583829DAAC8A7DEB05B24F0567E4317B3E3B33389B5C5F8B04B099FB4D103A32439F85A3C21D21A71B9B92A9B64EA0AB84312C77023694FD64EAAB907A43539DDB27BA0A853CC9069EAC8508C653E600B2AC018381B4BB4A879ACDAD342F91179CA8249525CB1968BBE52F755B7F5B43D6663D7A3BF0F3357D8A21D15B52DB3818ECE5B402A60C993E7CF436487B8D2AE91E6C5B88275E75824B0007EF3123C0AB51B5CC61B9B22380DE66C5B20B060CBB986F8123D94060049CDF8036873A7BE109444A0A1CD87A48CAE54192484AF844429C1C58C29AC624CD504F1C44F1E1347822B6F221323859A7F6F754BFE710BDA60276240A4FF2A5350703786F5671F449F20C2A95AE7C2903A42CB3B303FF4C427C08B11B4CD31C418C6D18D0861873BFA0332F11271552ED7C035F0E4BC428C43720B39A65166BA9C2D3D770E130360CC2384E83095B1A159495533F116C7B558B650DB04D5A26EAAA08C3EE57DE45A7F88C6A3CEB24DC5397B88C3CEF003319BB0233FD692FDA1524475B351F3C782182DECF590B7723BE400BE14809C44329963FC46959211D6A623339537848C251669941D90B130258ADF55A720A724E8B6A6CAE3C2264B1624CCBE7B456B30C8C7393294CA5180BC837DD2E45DBD59B6E17B24FE93052EB7C43B27AC3DC249CA0CBCA4FB5897C0B744088A8A0779D32233826A01DD6489952A4825E5358A700BE0E179AC197710D83ECC853E52695E9BF87BB1F6CBD05B02D4E679E3B88DD483B0749B11BD37B383DCCA71F9091834A1695502C4B95FC9118C1CFC34C84C2265BBBC563C282666B60AE5C7F3851D25ECBB5021CC38CB73EB6A3411B1C29046CA66540667D136954460C6FCBC4BC7C049BB047FA67A63B3CC1111C1D8AC27E8058BCCA4A15455858A58358F7A61020BC9C4C17F8B95C268CCB404B9AAB4A272A21A70DAF6B6F15121EE01C156A354AA17087E07702EAB38B3241FDB553F657339D5E29DC5D91B7A5A828EE959FEBB90B07229F6E49D23C3A190297042FB43986955B69C28E1016F77A58B431514D21B888899C3608276081B75F568097CDC1748F32307885815F3AEC9651819AA6873D1A4EB83B1953843B93422519483FEF0059D36BB2DB1F3D468FB068C86E8973733C398EAF00E1702C6734AD8EB3B620130D6C2B8C904A3BB9307BE5103F8D814505FB6A60AF7937EA6CAA117315E84CC9121AE56FBF39E67ADBD83AD2D3E3BB80843645206BDD9F2F629E3CC49B7'))
        # Keygen test group 2, test case 26
        keygen_test('mlkem768',
                    d=unhex('E34A701C4C87582F42264EE422D3C684D97611F2523EFE0C998AF05056D693DC'),
                    z=unhex('A85768F3486BD32A01BF9A8F21EA938E648EAE4E5448C34C3EB88820B159EEDD'),
                    ek_expected=unhex('6D14A071F7CC452558D5E71A7B087062ECB1386844588246126402B1FA1637733CD5F60CC84BCB646A7892614D7C51B1C7F1A2799132F13427DC482158DA254470A59E00A4E49686FDC077559367270C2153F11007592C9C4310CF8A12C6A8713BD6BB51F3124F989BA0D54073CC242E0968780B875A869EFB851586B9A868A384B9E6821B201B932C455369A739EC22569C977C212B381871813656AF5B567EF893B584624C863A259000F17B254B98B185097C50EBB68B244342E05D4DE520125B8E1033B1436093ACE7CE8E71B458D525673363045A3B3EEA9455428A398705A42327ADB3774B7057F42B017EC0739A983F19E8214D09195FA24D2D571DB73C19A6F8460E50830D415F627B88E94A7B153791A0C0C7E9484C74D53C714889F0E321B6660A532A5BC0E557FBCA35E29BC611200ED3C633077A4D873C5CC67006B753BF6D6B7AF6CA402AB618236C0AFFBC801F8222FBC36CE0984E2B18C944BBCBEF03B1E1361C1F44B0D734AFB1566CFF8744DA8B9943D6B45A3C09030702CA201FFE20CB7EC5B0D4149EE2C28E8B23374F471B57150D0EC9336261A2D5CB84A3ACACC4289473A4C0ABC617C9ABC178734434C82E1685588A5C2EA2678F6B3C2228733130C466E5B86EF491153E48662247B875D201020B566B81B64D839AB4633BAA8ACE202BAAB4496297F9807ADBBB1E332C6F8022B2A18CFDD4A82530B6D3F007C3353898D966CC2C21CB4244BD00443F209870ACC42BC33068C724EC17223619C1093CCA6AEB29500664D1225036B4B81091906969481F1C723C140B9D6C168F5B64BEA69C5FD6385DF7364B8723BCC85E038C7E464A900D68A2127818994217AEC8BDB39A970A9963DE93688E2AC82ABCC22FB9277BA22009E878381A38163901C7D4C85019538D35CAAE9C41AF8C929EE20BB08CA619E72C2F2262C1C9938572551AC02DC9268FBCC35D79011C3C090AD40A4F111C9BE55C427EB796C1932D8673579AF1B4C638B0944489012A2559A3B02481B01AC30BA8960F80C0C2B3947D36A12C080498BEE448716C973416C8242804A3DA099EE137B0BA90FE4A5C6A89200276A0CFB643EC2C56A2D708D7B4373E44C1502A763A600586E6CDA6273897D44448287DC2E602DC39200BF6166236559FD12A60892AEB153DD651BB469910B4B34669F91DA8654D1EB72EB6E02800B3B0A7D0A48C836854D3A83E65569CB7230BB44F3F143A6DEC5F2C39AB90F274F2088BD3D6A6FCA0070273BEDC84777FB52E3C558B0AE06183D5A48D452F68E15207F861627ACA14279630F82EC3A0CA078633B600AFA79743A600215BE5637458CE2CE8AFF5A08EB5017B2C766577479F8DC6BF9F5CC75089932161B96CEA406620AEDB630407F7687EBBB4814C7981637A48A90DE68031E062A7AF7612B4F5C7A6DA86BD136529E64295A5613EA73BD3D4448CB81F243135C0A660BEB9C17E651DEF469A7D90A15D3481090BCBF227012328941FA46F39C5006AD93D458AA6ADD655862B418C3094F551460DF2153A5810A7DA74F0614C2588BE49DC6F5E88154642BD1D3762563326433507156A57C57694BDD26E7A246FEB723AED67B04887C8E476B48CAB59E5362F26A9EF50C2BC80BA146226216FE62968A60D04E8C170D741C7A2B0E1ABDAC968'),
                    dk_expected=unhex('98A1B2DA4A65CFB5845EA7311E6A06DB731F1590C41EE74BA10782715B35A3102DF637872BE65BAB37A1DE2511D703C70247B35EF27435485024D93FD9E77C43804F371749BA00B20A8C5C588BC9ABE068AEAAA938517EBFE53B6B663282903DCD189736D7296816C733A1C77C6375E5397C0F189BBFE47643A61F58F8A3C6911BE4611A8C7BC050021163D0A404DC14065748FF29BE60D2B9FDCC8FFD98C587F38C67115786464BDB342B17E897D64617CBFB117973A5458977A7D7617A1B4D83BA03C611138A4673B1EB34B078033F97CFFE80C146A26943F842B976327BF1CBC60119525BB9A3C03493349000DD8F51BA21A2E92361762324600E0C13AAA6CB69BFB24276483F6B02421259B7585263C1A028D682C508BBC2801A56E98B8F620B0483D79B5AD8585AC0A475BAC77865194196338791B7985A05D109395CCA8932722A91950D37E12B891420A52B62CBFA815DF6174CE00E68BCA75D4838CA280F713C7E6924AFD95BAA0D01ADA637B158347034C0AB1A7183331A820ACBCB83193A1A94C8F7E384AED0C35ED3CB3397BB638086E7A35A6408A3A4B90CE953707C19BC46C3B2DA3B2EE32319C56B928032B5ED1256D0753D341423E9DB139DE7714FF075CAF58FD9F57D1A54019B5926406830DAE29A875302A81256F4D6CF5E74034EA614BF70C2764B20C9589CDB5C25761A04E58292907C578A94A35836BEE3112DC2C3AE2192C9DEAA304B29C7FEA1BDF47B3B6BCBA2C0E55C9CDB6DE7149E9CB17917718F12C8032DE1ADE0648D405519C70719BECC701845CF9F4B912FE71983CA34F9018C7CA7BB2F6C5D7F8C5B297359EC75209C2543FF11C4244977C5969524EC454D44C323FCCA94ACAC273A0EC49B4A8A585BCE7A5B305C04C3506422580357016A850C3F7EE17205A77B291C7731C9836C02AEE5406F63C6A07A214382AA15336C05D1045588107645EA7DE6870FC0E55E1540974301C42EC14105518680F688ABE4CE453738FE471B87FC31F5C68A39E68AF51B0240B90E0364B04BAC43D6FB68AB65AE028B62BD683B7D28AD38806BEE725B5B2416A8D79C16EC2A99EA4A8D92A2F5052E67F97352289761C5C39FC5C742E9C0A740CA59FC0182F709D01B5187F00063DAAB397596EEA4A31BDBCBD4C1BB0C55BE7C6850FDA9326B353E288C5013226C3C3923A791609E8002E73A5F7B6BB4A877B1FDF53BB2BAB3DD424D31BBB448E609A66B0E343C286E8760312B6D37AA5201D21F53503D88389ADCA21C70FB6C0FC9C69D6616C9EA3780E35565C0C97C15179C95343ECC5E1C2A24DE4699F6875EA2FA2DD3E357BC43914795207E026B850A2237950C108A512FC88C22488112607088185FB0E09C2C4197A83687266BAB2E583E21C40F4CC008FE652804D8223F1520A90B0D5385C7553CC767C58D120CCD3EF5B5D1A6CD7BC00DFF1321B2F2C432B64EFB8A3F5D0064B3F34293026C851C2DED68B9DFF4A28F6A8D225535E0477084430CFFDA0AC0552F9A212785B749913A06FA2274C0D15BAD325458D323EF6BAE13C0010D525C1D5269973AC29BDA7C983746918BA0E002588E30375D78329E6B8BA8C4462A692FB6083842B8C8C92C60F252726D14A071F7CC452558D5E71A7B087062ECB1386844588246126402B1FA1637733CD5F60CC84BCB646A7892614D7C51B1C7F1A2799132F13427DC482158DA254470A59E00A4E49686FDC077559367270C2153F11007592C9C4310CF8A12C6A8713BD6BB51F3124F989BA0D54073CC242E0968780B875A869EFB851586B9A868A384B9E6821B201B932C455369A739EC22569C977C212B381871813656AF5B567EF893B584624C863A259000F17B254B98B185097C50EBB68B244342E05D4DE520125B8E1033B1436093ACE7CE8E71B458D525673363045A3B3EEA9455428A398705A42327ADB3774B7057F42B017EC0739A983F19E8214D09195FA24D2D571DB73C19A6F8460E50830D415F627B88E94A7B153791A0C0C7E9484C74D53C714889F0E321B6660A532A5BC0E557FBCA35E29BC611200ED3C633077A4D873C5CC67006B753BF6D6B7AF6CA402AB618236C0AFFBC801F8222FBC36CE0984E2B18C944BBCBEF03B1E1361C1F44B0D734AFB1566CFF8744DA8B9943D6B45A3C09030702CA201FFE20CB7EC5B0D4149EE2C28E8B23374F471B57150D0EC9336261A2D5CB84A3ACACC4289473A4C0ABC617C9ABC178734434C82E1685588A5C2EA2678F6B3C2228733130C466E5B86EF491153E48662247B875D201020B566B81B64D839AB4633BAA8ACE202BAAB4496297F9807ADBBB1E332C6F8022B2A18CFDD4A82530B6D3F007C3353898D966CC2C21CB4244BD00443F209870ACC42BC33068C724EC17223619C1093CCA6AEB29500664D1225036B4B81091906969481F1C723C140B9D6C168F5B64BEA69C5FD6385DF7364B8723BCC85E038C7E464A900D68A2127818994217AEC8BDB39A970A9963DE93688E2AC82ABCC22FB9277BA22009E878381A38163901C7D4C85019538D35CAAE9C41AF8C929EE20BB08CA619E72C2F2262C1C9938572551AC02DC9268FBCC35D79011C3C090AD40A4F111C9BE55C427EB796C1932D8673579AF1B4C638B0944489012A2559A3B02481B01AC30BA8960F80C0C2B3947D36A12C080498BEE448716C973416C8242804A3DA099EE137B0BA90FE4A5C6A89200276A0CFB643EC2C56A2D708D7B4373E44C1502A763A600586E6CDA6273897D44448287DC2E602DC39200BF6166236559FD12A60892AEB153DD651BB469910B4B34669F91DA8654D1EB72EB6E02800B3B0A7D0A48C836854D3A83E65569CB7230BB44F3F143A6DEC5F2C39AB90F274F2088BD3D6A6FCA0070273BEDC84777FB52E3C558B0AE06183D5A48D452F68E15207F861627ACA14279630F82EC3A0CA078633B600AFA79743A600215BE5637458CE2CE8AFF5A08EB5017B2C766577479F8DC6BF9F5CC75089932161B96CEA406620AEDB630407F7687EBBB4814C7981637A48A90DE68031E062A7AF7612B4F5C7A6DA86BD136529E64295A5613EA73BD3D4448CB81F243135C0A660BEB9C17E651DEF469A7D90A15D3481090BCBF227012328941FA46F39C5006AD93D458AA6ADD655862B418C3094F551460DF2153A5810A7DA74F0614C2588BE49DC6F5E88154642BD1D3762563326433507156A57C57694BDD26E7A246FEB723AED67B04887C8E476B48CAB59E5362F26A9EF50C2BC80BA146226216FE62968A60D04E8C170D741C7A2B0E1ABDAC968E29020839D052FA372585627F8B59EE312AE414C979D825F06A6929A79625718A85768F3486BD32A01BF9A8F21EA938E648EAE4E5448C34C3EB88820B159EEDD'))
        # Keygen test group 3, test case 51
        keygen_test('mlkem1024',
                    d=unhex('49AC8B99BB1E6A8EA818261F8BE68BDEAA52897E7EC6C40B530BC760AB77DCE3'),
                    z=unhex('99E3246884181F8E1DD44E0C7629093330221FD67D9B7D6E1510B2DBAD8762F7'),
                    ek_expected=unhex('A04184D4BC7B532A0F70A54D7757CDE6175A6843B861CB2BC4830C0012554CFC5D2C8A2027AA3CD967130E9B96241B11C4320C7649CC23A71BAFE691AFC08E680BCEF42907000718E4EACE8DA28214197BE1C269DA9CB541E1A3CE97CFADF9C6058780FE6793DBFA8218A2760B802B8DA2AA271A38772523A76736A7A31B9D3037AD21CEBB11A472B8792EB17558B940E70883F264592C689B240BB43D5408BF446432F412F4B9A5F6865CC252A43CF40A320391555591D67561FDD05353AB6B019B3A08A73353D51B6113AB2FA51D975648EE254AF89A230504A236A4658257740BDCBBE1708AB022C3C588A410DB3B9C308A06275BDF5B4859D3A2617A295E1A22F90198BAD0166F4A943417C5B831736CB2C8580ABFDE5714B586ABEEC0A175A08BC710C7A2895DE93AC438061BF7765D0D21CD418167CAF89D1EFC3448BCBB96D69B3E010C82D15CAB6CACC6799D3639669A5B21A633C865F8593B5B7BC800262BB837A924A6C5440E4FC73B41B23092C3912F4C6BEBB4C7B4C62908B03775666C22220DF9C88823E344C7308332345C8B795D34E8C051F21F5A21C214B69841358709B1C305B32CC2C3806AE9CCD3819FFF4507FE520FBFC27199BC23BE6B9B2D2AC1717579AC769279E2A7AAC68A371A47BA3A7DBE016F14E1A727333663C4A5CD1A0F8836CF7B5C49AC51485CA60345C990E06888720003731322C5B8CD5E6907FDA1157F468FD3FC20FA8175EEC95C291A262BA8C5BE990872418930852339D88A19B37FEFA3CFE82175C224407CA414BAEB37923B4D2D83134AE154E490A9B45A0563B06C953C3301450A2176A07C614A74E3478E48509F9A60AE945A8EBC7815121D90A3B0E07091A096CF02C57B25BCA58126AD0C629CE166A7EDB4B33221A0D3F72B85D562EC698B7D0A913D73806F1C5C87B38EC003CB303A3DC51B4B35356A67826D6EDAA8FEB93B98493B2D1C11B676A6AD9506A1AAAE13A824C7C08D1C6C2C4DBA9642C76EA7F6C8264B64A23CCCA9A74635FCBF03E00F1B5722B214376790793B2C4F0A13B5C40760B4218E1D2594DCB30A70D9C1782A5DD30576FA4144BFC8416EDA8118FC6472F56A979586F33BB070FB0F1B0B10BC4897EBE01BCA3893D4E16ADB25093A7417D0708C83A26322E22E6330091E30152BF823597C04CCF4CFC7331578F43A2726CCB428289A90C863259DD180C5FF142BEF41C7717094BE07856DA2B140FA67710967356AA47DFBC8D255B4722AB86D439B7E0A6090251D2D4C1ED5F20BBE6807BF65A90B7CB2EC0102AF02809DC9AC7D0A3ABC69C18365BCFF59185F33996887746185906C0191AED4407E139446459BE29C6822717644353D24AB6339156A9C424909F0A9025BB74720779BE43F16D81C8CC666E99710D8C68BB5CC4E12F314E925A551F09CC59003A1F88103C254BB978D75F394D3540E31E771CDA36E39EC54A62B5832664D821A72F1E6AFBBA27F84295B2694C498498E812BC8E9378FE541CEC5891B25062901CB7212E3CDC46179EC5BCEC10BC0B9311DE05074290687FD6A5392671654284CD9C8CC3EBA80EB3B662EB53EB75116704A1FEB5C2D056338532868DDF24EB8992AB8565D9E490CADF14804360DAA90718EAB616BAB0765D33987B47EFB6599C5563235E61E4BE670E97955AB292D9732CB8930948AC82DF230AC72297A23679D6B94C17F1359483254FEDC2F05819F0D069A443B78E3FC6C3EF4714B05A3FCA81CBBA60242A7060CD885D8F39981BB18092B23DAA59FD9578388688A09BBA079BC809A54843A60385E2310BBCBCC0213CE3DFAAB33B47F9D6305BC95C6107813C585C4B657BF30542833B14949F573C0612AD524BAAE69590C1277B86C286571BF66B3CFF46A3858C09906A794DF4A06E9D4B0A2E43F10F72A6C6C47E5646E2C799B71C33ED2F01EEB45938EB7A4E2E2908C53558A540D350369FA189C616943F7981D7618CF02A5B0A2BCC422E857D1A47871253D08293C1C179BCDC0437069107418205FDB9856623B8CA6B694C96C084B17F13BB6DF12B2CFBBC2B0E0C34B00D0FCD0AECFB27924F6984E747BE2A09D83A8664590A8077331491A4F7D720843F23E652C6FA840308DB4020337AAD37967034A9FB523B67CA70330F02D9EA20C1E84CB8E5757C9E1896B60581441ED618AA5B26DA56C0A5A73C4DCFD755E610B4FC81FF84E21'),
                    dk_expected=unhex('8C8B3722A82E550565521611EBBC63079944C9B1ABB3B0020FF12F631891A9C468D3A67BF6271280DA58D03CB042B3A461441637F929C273469AD15311E910DE18CB9537BA1BE42E98BB59E498A13FD440D0E69EE832B45CD95C382177D67096A18C07F1781663651BDCAC90DEDA3DDD143485864181C91FA2080F6DAB3F86204CEB64A7B4446895C03987A031CB4B6D9E0462FDA829172B6C012C638B29B5CD75A2C930A5596A3181C33A22D574D30261196BC350738D4FD9183A763336243ACED99B3221C71D8866895C4E52C119BF3280DAF80A95E15209A795C4435FBB3570FDB8AA9BF9AEFD43B094B781D5A81136DAB88B8799696556FEC6AE14B0BB8BE4695E9A124C2AB8FF4AB1229B8AAA8C6F41A60C34C7B56182C55C2C685E737C6CA00A23FB8A68C1CD61F30D3993A1653C1675AC5F0901A7160A73966408B8876B715396CFA4903FC69D60491F8146808C97CD5C533E71017909E97B835B86FF847B42A696375435E006061CF7A479463272114A89EB3EAF2246F0F8C104A14986828E0AD20420C9B37EA23F5C514949E77AD9E9AD12290DD1215E11DA274457AC86B1CE6864B122677F3718AA31B02580E64317178D38F25F609BC6C55BC374A1BF78EA8ECC219B30B74CBB3272A599238C93985170048F176775FB19962AC3B135AA59DB104F7114DBC2C2D42949ADECA6A85B323EE2B2B23A77D9DB235979A8E2D67CF7D2136BBBA71F269574B38888E1541340C19284074F9B7C8CF37EB01384E6E3822EC4882DFBBEC4E6098EF2B2FC177A1F0BCB65A57FDAA89315461BEB7885FB68B3CD096EDA596AC0E61DD7A9C507BC6345E0827DFCC8A3AC2DCE51AD731AA0EB932A6D0983992347CBEB3CD0D9C9719797CC21CF0062B0AD94CAD734C63E6B5D859CBE19F0368245351BF464D7505569790D2BB724D8659A9FEB1C7C473DC4D061E29863A2714BAC42ADCD1A8372776556F7928A7A44E94B6A25322D03C0A1622A7FD261522B7358F085BDFB60758762CB901031901B5EECF4920C81020A9B1781BCB9DD19A9DFB66458E7757C52CEC75B4BA740A24099CB56BB60A76B6901AA3E0169C9E83496D73C4C99435A28D613E97A1177F58B6CC595D3B2331E9CA7B57B74DC2C5277D26F2FE19240A55C35D6CFCA26C73E9A2D7C980D97960AE1A04698C16B398A5F20C35A0914145CE1674B71ABC6066A909A3E4B911E69D5A849430361F731B07246A6329B52361904225082D0AAC5B21D6B34862481A890C3C360766F04263603A6B73E802B1F70B2EB00046836B8F493BF10B90B8737C6C548449B294C47253BE26CA72336A632063AD3D0B48C8B0F4A34447EF13B764020DE739EB79ABA20E2BE1951825F293BEDD1089FCB0A91F560C8E17CDF52541DC2B81F972A7375B201F10C08D9B5BC8B95100054A3D0AAFF89BD08D6A0E7F2115A435231290460C9AD435A3B3CF35E52091EDD1890047BCC0AABB1ACEBC75F4A32BC1451ACC4969940788E89412188946C9143C5046BD1B458DF617C5DF533B052CD6038B7754034A23C2F7720134C7B4EACE01FAC0A2853A9285847ABBD06A3343A778AC6062E458BC5E61ECE1C0DE0206E6FE8A84034A7C5F1B005FB0A584051D3229B86C909AC5647B3D75569E05A88279D80E5C30F574DC327512C6BBE8101239EC62861F4BE67B05B9CDA9C545C13E7EB53CFF260AD9870199C21F8C63D64F0458A7141285023FEB829290872389644B0C3B73AC2C8E121A29BB1C43C19A233D56BED82740EB021C97B8EBBA40FF328B541760FCC372B52D3BC4FCBC06F424EAF253804D4CB46F41FF254C0C5BA483B44A87C219654555EC7C163C79B9CB760A2AD9BB722B93E0C28BD4B1685949C496EAB1AFF90919E3761B346838ABB2F01A91E554375AFDAAAF3826E6DB79FE7353A7A578A7C0598CE28B6D9915214236BBFFA6D45B6376A07924A39A7BE818286715C8A3C110CD76C02E0417AF138BDB95C3CCA798AC809ED69CFB672B6FDDC24D89C06A6558814AB0C21C62B2F84C0E3E0803DB337A4E0C7127A6B4C8C08B1D1A76BF07EB6E5B5BB47A16C74BC548375FB29CD789A5CFF91BDBD071859F4846E355BB0D29484E264DFF36C9177A7ACA78908879695CA87F25436BC12630724BB22F0CB64897FE5C41195280DA04184D4BC7B532A0F70A54D7757CDE6175A6843B861CB2BC4830C0012554CFC5D2C8A2027AA3CD967130E9B96241B11C4320C7649CC23A71BAFE691AFC08E680BCEF42907000718E4EACE8DA28214197BE1C269DA9CB541E1A3CE97CFADF9C6058780FE6793DBFA8218A2760B802B8DA2AA271A38772523A76736A7A31B9D3037AD21CEBB11A472B8792EB17558B940E70883F264592C689B240BB43D5408BF446432F412F4B9A5F6865CC252A43CF40A320391555591D67561FDD05353AB6B019B3A08A73353D51B6113AB2FA51D975648EE254AF89A230504A236A4658257740BDCBBE1708AB022C3C588A410DB3B9C308A06275BDF5B4859D3A2617A295E1A22F90198BAD0166F4A943417C5B831736CB2C8580ABFDE5714B586ABEEC0A175A08BC710C7A2895DE93AC438061BF7765D0D21CD418167CAF89D1EFC3448BCBB96D69B3E010C82D15CAB6CACC6799D3639669A5B21A633C865F8593B5B7BC800262BB837A924A6C5440E4FC73B41B23092C3912F4C6BEBB4C7B4C62908B03775666C22220DF9C88823E344C7308332345C8B795D34E8C051F21F5A21C214B69841358709B1C305B32CC2C3806AE9CCD3819FFF4507FE520FBFC27199BC23BE6B9B2D2AC1717579AC769279E2A7AAC68A371A47BA3A7DBE016F14E1A727333663C4A5CD1A0F8836CF7B5C49AC51485CA60345C990E06888720003731322C5B8CD5E6907FDA1157F468FD3FC20FA8175EEC95C291A262BA8C5BE990872418930852339D88A19B37FEFA3CFE82175C224407CA414BAEB37923B4D2D83134AE154E490A9B45A0563B06C953C3301450A2176A07C614A74E3478E48509F9A60AE945A8EBC7815121D90A3B0E07091A096CF02C57B25BCA58126AD0C629CE166A7EDB4B33221A0D3F72B85D562EC698B7D0A913D73806F1C5C87B38EC003CB303A3DC51B4B35356A67826D6EDAA8FEB93B98493B2D1C11B676A6AD9506A1AAAE13A824C7C08D1C6C2C4DBA9642C76EA7F6C8264B64A23CCCA9A74635FCBF03E00F1B5722B214376790793B2C4F0A13B5C40760B4218E1D2594DCB30A70D9C1782A5DD30576FA4144BFC8416EDA8118FC6472F56A979586F33BB070FB0F1B0B10BC4897EBE01BCA3893D4E16ADB25093A7417D0708C83A26322E22E6330091E30152BF823597C04CCF4CFC7331578F43A2726CCB428289A90C863259DD180C5FF142BEF41C7717094BE07856DA2B140FA67710967356AA47DFBC8D255B4722AB86D439B7E0A6090251D2D4C1ED5F20BBE6807BF65A90B7CB2EC0102AF02809DC9AC7D0A3ABC69C18365BCFF59185F33996887746185906C0191AED4407E139446459BE29C6822717644353D24AB6339156A9C424909F0A9025BB74720779BE43F16D81C8CC666E99710D8C68BB5CC4E12F314E925A551F09CC59003A1F88103C254BB978D75F394D3540E31E771CDA36E39EC54A62B5832664D821A72F1E6AFBBA27F84295B2694C498498E812BC8E9378FE541CEC5891B25062901CB7212E3CDC46179EC5BCEC10BC0B9311DE05074290687FD6A5392671654284CD9C8CC3EBA80EB3B662EB53EB75116704A1FEB5C2D056338532868DDF24EB8992AB8565D9E490CADF14804360DAA90718EAB616BAB0765D33987B47EFB6599C5563235E61E4BE670E97955AB292D9732CB8930948AC82DF230AC72297A23679D6B94C17F1359483254FEDC2F05819F0D069A443B78E3FC6C3EF4714B05A3FCA81CBBA60242A7060CD885D8F39981BB18092B23DAA59FD9578388688A09BBA079BC809A54843A60385E2310BBCBCC0213CE3DFAAB33B47F9D6305BC95C6107813C585C4B657BF30542833B14949F573C0612AD524BAAE69590C1277B86C286571BF66B3CFF46A3858C09906A794DF4A06E9D4B0A2E43F10F72A6C6C47E5646E2C799B71C33ED2F01EEB45938EB7A4E2E2908C53558A540D350369FA189C616943F7981D7618CF02A5B0A2BCC422E857D1A47871253D08293C1C179BCDC0437069107418205FDB9856623B8CA6B694C96C084B17F13BB6DF12B2CFBBC2B0E0C34B00D0FCD0AECFB27924F6984E747BE2A09D83A8664590A8077331491A4F7D720843F23E652C6FA840308DB4020337AAD37967034A9FB523B67CA70330F02D9EA20C1E84CB8E5757C9E1896B60581441ED618AA5B26DA56C0A5A73C4DCFD755E610B4FC81FF84E21D2E574DFD8CD0AE893AA7E125B44B924F45223EC09F2AD1141EA93A68050DBF699E3246884181F8E1DD44E0C7629093330221FD67D9B7D6E1510B2DBAD8762F7'))
        # Encaps test from test group 1, test case 1
        encaps_test('mlkem512',
                    ek=unhex('dd1924935aa8e617af18b5a065ac45727767ee897cf4f9442b2ace30c0237b307d3e76bf8eeb78addc4aacd16463d8602fd5487b63c88bb66027f37d0d614d6f9c24603c42947664ac4398c6c52383469b4f9777e5ec7206210f3e5a796bf45c53268e25f39ac261af3bfa2ee755beb8b67ab3ac8df6c629c1176e9e3b965e9369f9b3b92ad7c20955641d99526fe7b9fe8c850820275cd964849250090733ce124ecf316624374bd18b7c358c06e9c136ee1259a9245abc55b964d689f5a08292d28265658ebb40cbfe488a2228275590ab9f32a34109709c1c291d4a23337274c7a5a5991c7a87b81c974ab18ce77859e4995e7c14f0371748b7712fb52c5966cd63063c4f3b81b47c45dde83fb3a2724029b10b3230214c04fa0577fc29ac9086ae18c53b3ed44e507412fca04b4f538a51588ec1f1029d152d9ae7735f76a077aa9484380aed9189e5912487fcc5b7c7012d9223dd967eecdac3008a8931b648243537f548c171698c5b381d846a72e5c92d4226c5a8909884f1c4a3404c1720a5279414d7f27b2b982652b6740219c56d217780d7a5e5ba59836349f726881dea18ef75c0772a8b922766953718cacc14ccbacb5fc412a2d0be521817645ab2bf6a4785e92bc94caf477a967876796c0a5190315ac0885671a4c749564c3b2c7aed9064eba299ef214ba2f40493667c8bd032aec5621711b41a3852c5c2bab4a349ce4b7f085a812bbbc820b81befe63a05b8bcdfe9c2a70a8b1aca9bf9816481907ff4432461111287303f0bd817c05726bfa18a2e24c7724921028032f622bd960a317d83b356b57f4a8004499cbc73c97d1eb7745972631c0561c1a3ab6ef91bd363280a10545da693e6d58aed6845e7cc5f0d08ca7905052c77366d1972ccfcc1a27610cb543665aa798e20940128b9567a7edb7a900407c70d359438435e13961608d552a94c5cda7859220509b483c5c52a210e9c812bc0c2328ca00e789a56b2606b90292e3543dacaa2431841d61a22ca90c1ccf0b5b4e0a6f640536d1a26ab5b8d2151327928ce02904cf1d15e32788a95f62d3c270b6fa1508f97b9155a2726d80a1afa3c5387a276a4d031a08abf4f2e74f1a0bb8a0fd3cb'),
                    m=unhex('6ff02e1dc7fd911beee0c692c8bd100c3e5c48964d31df92994218e80664a6ca'),
                    c_expected=unhex('19c592505907c24c5fa2ebfa932d2cbb48f3e4340a28f7eba5d068fcacabedf77784e2b24d7961775f0bf1a997ae8ba9fc4311be63716779c2b788f812cbb78c74e7517e22e910eff5f38d44469c50de1675ae198fd6a289ae7e6c30a9d4351b3d1f4c36eff9c68da91c40b82dc9b2799a33a26b60a4e70d7101862779469f3a9daec8e3e8f8c6a16bf092fba5866186b8d208fdeb274ac1f829659dc2be4ac4f306cb5584bad1936a92c9b76819234281bb395841c25756086ea564ca3e227e3d9f1052c0766d2eb79a47c150721e0dea7c0069d551b264801b7727ecaf82eecb99a876fda090bf6c3fc6b109f1701485f03ce66274b8435b0a014cfb3e79cced67057b5ae2ad7f5279eb714942e4c1ccff7e85c0db43e5d41289207363b444bb51bb8ab0371e70cbd55f0f3dad403e105176e3e8a225d84ac8bee38c821ee0f547431145dcb3139286abb11794a43a3c1b5229e4bcfe959c78adaee2d5f2497b5d24bc21fa03a9a58c2455373ec89583e7e588d7fe67991ee93783ed4a6f9eeae04e64e2e1e0e699f6dc9c5d39ef9278c985e7fdf2a764ffd1a0b95792ad681e930d76df4efe5d65dbbd0f1438481ed833ad4946ad1c69ad21dd7c86185774426f3fcf53b52ad4b40d228ce124072f592c7daa057f17d790a5bd5b93834d58c08c88dc8f0ef488156425b744654eaca9d64858a4d6ceb478795194bfadb18dc0ea054f9771215ad3cb1fd031d7be4598621926478d375a1845aa91d7c733f8f0e188c83896edf83b8646c99e29c0da2290e71c3d2e970720c97b5b7f950486033c6a2571ddf2bccdabb2dfa5fce4c3a1884606041d181c728794ae0e806ecb49af16756a4ce73c87bd4234e60f05535fa5929fd5a34473266401f63bbd6b90e003472ac0ce88f1b666597279d056a632c8d6b790fd411767848a69e37a8a839bc766a02ca2f695ec63f056a4e2a114cacf9fd90d730c970db387f6de73395f701a1d953b2a89dd7edad439fc205a54a481e889b098d5255670f026b4a2bf02d2bdde87c766b25fc5e0fd453757e756d18c8cd912f9a77f8e6bf0205374b462'),
                    k_expected=unhex('0bf323338d6f0a21d5514b673cd10b714ce6e36f35bcd1bf544196368ee51a13'))
        # Encaps test from test group 2, test case 26
        encaps_test('mlkem768',
                    ek=unhex('89d2cb65f94dcbfc890efc7d0e5a7a38344d1641a3d0b024d50797a5f23c3a18b3101a1269069f43a842bacc098a8821271c673db1beb33034e4d7774d16635c7c2c3c2763453538bc1632e1851591a51642974e5928abb8e55fe55612f9b141aff015545394b2092e590970ec29a7b7e7aa1fb4493bf7cb731906c2a5cb49e6614859064e19b8fa26af51c44b5e7535bfdac072b646d3ea490d277f0d97ced47395fed91e8f2bce0e3ca122c2025f74067ab928a822b35653a74f06757629afb1a1caf237100ea935e793c8f58a71b3d6ae2c8658b10150d4a38f572a0d49d28ae89451d338326fdb3b4350036c1081117740edb86b12081c5c1223dbb5660d5b3cb3787d481849304c68be875466f14ee5495c2bd795ae412d09002d65b8719b90cba3603ac4958ea03cc138c86f7851593125334701b677f82f4952a4c93b5b4c134bb42a857fd15c650864a6aa94eb691c0b691be4684c1f5b7490467fc01b1d1fda4dda35c4ecc231bc73a6fef42c99d34eb82a4d014987b3e386910c62679a118f3c5bd9f467e4162042424357db92ef484a4a1798c1257e870a30cb20aaa0335d83314fe0aa7e63a862648041a72a6321523220b1ace9bb701b21ac1253cb812c15575a9085eabeade73a4ae76e6a7b158a20586d78a5ac620a5c9abcc9c043350a73656b0abe822da5e0ba76045fad75401d7a3b703791b7e99261710f86b72421d240a347638377205a152c794130a4e047742b888303bddc309116764de7424cebea6db65348ac537e01a9cc56ea667d5aa87ac9aaa4317d262c10143050b8d07a728ca633c13e468abcead372c77b8ecf3b986b98c1e55860b2b4216766ad874c35ed7205068739230220b5a2317d102c598356f168acbe80608de4c9a710b8dd07078cd7c671058af1b0b8304a314f7b29be78a933c7b9294424954a1bf8bc745de86198659e0e1225a910726074969c39a97c19240601a46e013dcdcb677a8cbd2c95a40629c256f24a328951df57502ab30772cc7e5b850027c8551781ce4985bdacf6b865c104e8a4bc65c41694d456b7169e45ab3d7acabeafe23ad6a7b94d1979a2f4c1cae7cd77d681d290b5d8e451bfdcccf5310b9d12a88ec29b10255d5e17a192670aa9731c5ca67ec784c502781be8527d6fc003c6701b3632284b40307a527c7620377feb0b73f722c9e3cd4dec64876b93ab5b7cfc4a657f852b659282864384f442b22e8a21109387b8b47585fc680d0ba45c7a8b1d7274bda57845d100d0f42a3b74628773351fd7ac305b2497639be90b3f4f71a6aa3561eecc6a691bb5cb3914d8634ca1e1af543c049a8c6e868c51f0423bd2d5ae09b79e57c27f3fe3ae2b26a441babfc6718ce8c05b4fe793b910b8fbcbbe7f1013242b40e0514d0bdc5c88bac594c794ce5122fbf34896819147b928381587963b0b90034aa07a10be176e01c80ad6a4b71b10af4241400a2a4cbbc05961a15ec1474ed51a3cc6d35800679a462809caa3ab4f7094cd6610b4a700cba939e7eac93e38c99755908727619ed76a34e53c4fa25bfc97008206697dd145e5b9188e5b014e941681e15fe3e132b8a3903474148ba28b987111c9bcb3989bbbc671c581b44a492845f288e62196e471fed3c39c1bbddb0837d0d4706b0922c4'),
                    m=unhex('2ce74ad291133518fe60c7df5d251b9d82add48462ff505c6e547e949e6b6bf7'),
                    c_expected=unhex('56b42d593aab8e8773bd92d76eabddf3b1546f8326f57a7b773764b6c0dd30470f68dff82e0dca92509274ecfe83a954735fde6e14676daaa3680c30d524f4efa79ed6a1f9ed7e1c00560e8683538c3105ab931be0d2b249b38cb9b13af5ceaf7887a59dba16688a7f28de0b14d19f391eb41832a56479416ccf94e997390ed7878eeaff49328a70e0ab5fce6c63c09b35f4e45994de615b88bb722f70e87d2bbd72ae71e1ee9008e459d8e743039a8ddeb874fce5301a2f8c0ee8c2fee7a4ee68b5ed6a6d9ab74f98bb3ba0fe89e82bd5a525c5e8790f818ccc605877d46c8bdb5c337b025bb840ff471896e43bfa99d73dbe31805c27a43e57f0618b3ae522a4644e0d4e4c1c548489431be558f3bfc50e16617e110dd7af9a6fd83e3fbb68c304d15f6cb700d61d7aa915a6751ea3ba80223e654132a20999a43bf408592730b9a9499636c09fa729f9cb1f9d3442f47357a2b9cf15d3103b9bf396c23088f118ede346b5c03891cfa5d517cef8471322e7e31087c4b036abad784bff72a9b11fa198facbcb91f067feaf76fcfe5327c1070b3da6988400756760d2d1f060298f1683d51e3616e98c51c9c03aa42f2e633651a47ad3cc2ab4a852ae0c4b04b4e1c3dd944445a2b12b4f42a6435105c04122fc3587afe409a00b308d63c5dd8163654504eedbb7b5329577c35fbeb3f463872cac28142b3c12a740ec6ea7ce9ad78c6fc8fe1b4df5fc55c1667f31f2312da07799dc870a478608549fedafe021f1cf2984180364e90ad98d845652aa3cdd7a8eb09f5e51423fab42a7b7bb4d514864be8d71297e9c3b17a993f0ae62e8ef52637bd1b885bd9b6ab727854d703d8dc478f96cb81fce4c60383ac01fcf0f971d4c8f352b7a82e218652f2c106ca92ae686bacfcef5d327347a97a9b375d67341552bc2c538778e0f9801823ccdfcd1eaaded55b18c9757e3f212b2889d3857db51f981d16185fd0f900853a75005e3020a8b95b7d8f2f2631c70d78a957c7a62e1b3719070acd1fd480c25b83847da027b6ebbc2eec2df22c87f9b46d5d7baf156b53cee929572b92c4784c4e829f3446a1ffe47f99decd0436029ddebd3ed8e87e5e73d123dbe8a4ddacf2abde87f33ae2b621c0ec5d5cad1259deec2aeff6088f04f27a20338b5762543e5100899a4cbfb7b3ca456b3a19b83a4c432230c23e1c7f107c4cb112152f1c0f30da0bb33f4f11f47eea43872bafa84ae22256d708e0604dade4b2a4dde8cccf11930e13553934ae3ece52f3d7ccc00287377879fe6b8ece7ef79423507c9da339559c20de1c51955999bae47401dc3cdfaa1b256d09c7db9fc8698bfcefa7302d56fbcde1fbaaa1c653454e6fd3d84e4f79a931c681cbb6cb462b10dae112bdfb7f65c7fdf6e5fc594ec3a474a94bd97e6ec81f71c230bf70ca0f13ce3dffbd9ff9804efd8f37a4d3629b43a8f55544ebc5ac0abd9a33d79699068346a0f1a3a96e115a5d80be165b562d082984d5aacc3a2301981a6418f8ba7d7b0d7ca5875c6'),
                    k_expected=unhex('2696d28e9c61c2a01ce9b1608dcb9d292785a0cd58efb7fe13b1de95f0db55b3'))
        # Encaps test from test group 3, test case 51
        encaps_test('mlkem1024',
                    ek=unhex('307a4cea4148219b958ea0b7886659235a4d1980b192610847d86ef32739f94c3b446c4d81d89b8b422a9d079c88b11acaf321b014294e18b296e52f3f744cf9634a4fb01db0d99ef20a633a552e76a0585c6109f018768b763af3678b4780089c1342b96907a29a1c11521c744c2797d0bf2b9ccdca614672b45076773f458a31ef869be1eb2efeb50d0e37495dc5ca55e07528934f6293c4168027d0e53d07facc6630cb08197e53fb193a171135dc8ad9979402a71b6926bcdcdc47b93401910a5fcc1a813b682b09ba7a72d2486d6c799516465c14729b26949b0b7cbc7c640f267fed80b162c51fd8e09227c101d505a8fae8a2d7054e28a78ba8750decf9057c83979f7abb084945648006c5b28804f34e73b238111a65a1f500b1cc606a848f2859070beba7573179f36149cf5801bf89a1c38cc278415528d03bdb943f96280c8cc52042d9b91faa9d6ea7bcbb7ab1897a3266966f78393426c76d8a49578b98b159ebb46ee0a883a270d8057cd0231c86906a91dbbade6b2469581e2bca2fea8389f7c74bcd70961ea5b934fbcf9a6590bf86b8db548854d9a3fb30110433bd7a1b659ca8568085639237b3bdc37b7fa716d482a25b54106b3a8f54d3aa99b5123da96066904592f3a54ee23a7981ab608a2f4413cc658946c6d7780ea765644b3cc06c70034ab4eb351912e7715b56755d09021571bf340ab92598a24e811893195b96a1629f8041f58658431561fc0ab15292b913ec473f04479bc145cd4c563a286235646cd305a9be1014e2c7b130c33eb77cc4a0d9786bd6bc2a954bf3005778f8917ce13789bbb962807858b67731572b6d3c9b4b5206fac9a7c8961698d88324a915186899b29923f08442a3d386bd416bcc9a100164c930ec35eafb6ab35851b6c8ce6377366a175f3d75298c518d44898933f53dee617145093379c4659f68583b2b28122666bec57838991ff16c368dd22c36e780c91a3582e25e19794c6bf2ab42458a8dd7705de2c2aa20c054e84b3ef35032798626c248263253a71a11943571340a978cd0a602e47dee540a8814ba06f31414797cdf6049582361bbaba387a83d89913fe4c0c112b95621a4bda8123a14d1a842fb57b83a4fbaf33a8e552238a596aae7a150d75da648bc44644977ba1f87a4c68a8c4bd245b7d00721f7d64e822b085b901312ec37a8169802160cce1160f010be8cbcace8e7b005d7839234a707868309d03784b4273b1c8a160133ed298184704625f29cfa086d13263ee5899123c596ba788e5c54a8e9ba829b8a9d904bc4bc0bbea76bc53ff811214598472c9c202b73eff035dc09703af7bf1babaac73193cb46117a7c9492a43fc95789a924c5912787b2e2090ebbcfd3796221f06debf9cf70e056b8b9161d6347f47335f3e1776da4bb87c15cc826146ff0249a413b45aa93a805196ea453114b524e310aedaa46e3b99642368782566d049a726d6cca910993aed621d0149ea588a9abd909dbb69aa22829d9b83ada2209a6c2659f2169d668b9314842c6e22a74958b4c25bbdcd293d99cb609d866749a485dfb56024883cf5465dba0363206587f45597f89002fb8607232138e03b2a894525f265370054b48863614472b95d0a2303442e378b0dd1c75acbab971a9a8d1281c79613acec6933c377b3c578c2a61a1ec181b101297a37cc5197b2942f6a0e4704c0ec63540481b9f159dc255b59bb55df496ae54217b7689bd51dba0383a3d72d852ffca76df05b66eeccbd47bc53040817628c71e361d6af889084916b408a466c96e7086c4a60a10fcf7537bb94afbcc7d437590919c28650c4f2368259226a9bfda3a3a0ba1b5087d9d76442fd786c6f81c68c0360d7194d7072c4533aea86c2d1f8c0a27696066f6cfd11003f797270b32389713cffa093d991b63844c385e72277f166f5a3934d6bb89a4788de28321defc7457ab484bd30986dc1dab3008cd7b22f69702fabb9a1045407da4791c3590ff599d81d688cfa7cc12a68c50f51a1009411b44850f9015dc84a93b17c7a207552c661ea9838e31b95ead546248e56be7a5130505268771199880a141771a9e47acfed590cb3aa7cb7c5f74911d8912c29d6233f4d53bc64139e2f55be75507dd77868e384aec581f3f411db1a742972d3ebfd3315c84a5ad63a0e75c8bca3e3041e05d9067aff3b1244f763e7983'),
                    m=unhex('59c5154c04ae43aaff32700f081700389d54bec4c37c088b1c53f66212b12c72'),
                    c_expected=unhex('e2d5fd4c13cea0b52d874fea9012f3a51743a1093710bbf23950f9147a472ee5533928a2f46d592f35da8b4f758c893b0d7b98948be447b17cb2ae58af8a489ddd9232b99b1c0d2de77caa472bc3bbd4a7c60dbfdca92ebf3a1ce1c22dad13e887004e2924fd22656f5e508791de06d85e1a1426808ed9a89f6e2fd3c245d4758b22b02cade33b60fc889a33fc4447edebbfd4530de86596a33789d5dba6e6ec9f89879af4be4909a69017c9bb7a5e31815ea5f132eec4984faa7ccf594dd00d4d8487e45621af8f6e330551439c93ec078a7a3cc1594af91f8417375fd6088ceb5e85c67099091bac11498a0d711455f5e0d95cd7bbe5cdd8fecb319e6853c23c9be2c763df578666c40a40a87486e46ba8716146192904510a6dc59da8025825283d684db91410b4f12c6d8fbd0add75d3098918cb04ac7bc4db0d6bcdf1194dd86292e05b7b8630625b589cc509d215bbd06a2e7c66f424cdf8c40ac6c1e5ae6c964b7d9e92f95fc5c8852281628b81b9afabc7f03be3f62e8047bb88d01c68687b8dd4fe63820062b6788a53729053826ed3b7c7ef8241e19c85117b3c5341881d4f299e50374c8eefd5560bd18319a7963a3d02f0fbe84bc484b5a4018b97d274191c95f702bab9b0d105faf9fdcff97e437236567599faf73b075d406104d403cdf81224da590bec2897e30109e1f2e5ae4610c809a73f638c84210b3447a7c8b6dddb5ae200bf20e2fe4d4ba6c6b12767fb8760f66c5118e7a9935b41c9a471a1d3237688c1e618cc3be936aa3f5e44e086820b810e063211fc21c4044b3ac4d00df1bcc7b24dc07ba48b23b0fc12a3ed3d0a5cf7671415ab9cf21286fe63fb41418570555d4739b88104a8593f293025a4e3ee7c67e4b48e40f6ba8c09860c3fbbe55d45b45fc9ab629b17c276c9c9e2af3a043beafc18fd4f25ee7f83bddcd2d93914b7ed4f7c9af127f3f15c277be16551fef3ae03d7b9143f0c9c019ab97eea076366131f518363711b34e96d3f8a513f3e20b1d452c4b7ae3b975ea94d880dac6693399750d02220403f0d3e3fc1172a4de9dc280eaf0fee2883a6660bf5a3d246ff41d21b36ea521cf7aa689f800d0f86f4fa1057d8a13f9da8fffd0dc1fad3c04bb1cccb7c834db051a7ac2e4c60301996c93071ea416b421759935659cf62ca5f13ae07c3b195c148159d8beb03d440b00f5305765f20c0c46eee59c6d16206402db1c715e888bde59c781f35a7cc7c1c5ecb2155ae3e959c0964cc1ef8d7c69d1458a9a42f95f4c6b5b996345712aa290fbbf7dfd4a6e86463022a3f4725f6511bf7ea5e95c707cd3573609aadeaf540152c495f37fe6ec8bb9fa2aa61d15735934f4737928fde90ba995722465d4a64505a5201f07aa58cfd8ae226e02070b2dbf512b975319a7e8753b4fdae0eb4922869cc8e25c4a5560c2a0685de3ac392a8925ba882004894742e43ccfc277439ec8050a9aeb42932e01c840dfcedcc34d3991289a62c17d1284c839514b93351dbb2dda81f924565d70e7079d5b8126caab7a4a1c731655a53bcc09f5d63ec9086dea650055985edfa8297d9c95410c5d1894d17d5930549adbc2b8733c99fe62e17c4de34a5d89b12d18e42a422d2ce779c2c28eb2d98003d5cd323fcbecf02b5066e0e734810f09ed89013c00f011bd220f2e5d6a362df90599198a093b03c8d8efbfe0b617592faf1e64220c4440b53ffb47164f369c95290ba9f3108d686c57db645c53c012e57af25bd6693e2cc6b57651af1591fe5d8916640ec017c253df0606bb6b3035fae748f3d4034223b1b5efbf5283e778c1094291cf7b19be0f317350e6f8518fde0efb1381fb6e16c241f7f17a5210693a274159e7fac868cd0dc4359c3d9eefea0d9e31e43fa651392c65a543a59b3eee3a639dc9417d056a5ff0f160beee2eac29a7d88c0982cf70b5a46379f21e506aac61a9bb1b8c2b9dab0e44a823b61d0aa11d94f76a4a8e21f9d4280683208f4ea911116f6fd6a97426934ec3426b8c8f703da85e9dcf99336136003728b8ecdd04a389f6a817a78bfa61ba46020bf3c34829508f9d06d1553cd987aac380d86f168843ba3904de5f7058a41b4cd388bc9ce3aba7ee7139b7fc9e5b8cfaaa38990bd4a5db32e2613e7ec4f5f8b1292a38c6f4ff5a40490d76b126652fcf86e245235d636c65cd102b01e22781a72918c'),
                    k_expected=unhex('7264bde5c6cec14849693e2c3c86e48f80958a4f6186fc69333a4148e6e497f3'))
        # Decaps test from test group 1, test case 1 (accept)
        decaps_test('mlkem512',
                    dk=unhex('a5e26e1b2360203944acfc2d7c376780e55b5a5ca38674919437c794f54b8217bb0629c84c692ef7827eed864d0c508990ca4553f16f4720cb75368c1b8ca9dbc175f51bbebaa456f36611a2364775d248c0f4c40b342608f7370a983cf75c915570248e367375b665d9357ce4a8553e659be4a60ca68b58724689c23b74d34c9e78e168e7cb0df84641e41b6e6807be6cf4cf8f338525d57090b08aab5721216395c49147f6e817b117b129987317a7a5ff15a279f86af93c6a4995954000c3d4d8b0a07499a95a5c98d0b8303702dfd801b67c37268904c96abc462750384baea767a5ad30c5d452682b3ac864d1671db38f1cf2ce6e6c901d39c144da3d93b863f95717c3c585ab876d3ef2b10afa0b8142164c3c27fb179a923a3f924b15cebb22ec762907324f1cd4c47573ca1f103ca88844f3b86687280b3b5bb569b1c118b63565055834f39f320cb88c05c199e29684d7802cf45d8da342cc444d91a84d6d9461c873b66f9785488723a167412019077c9a7fcf4c7bd028be3007b3483026a442a095124c9607c950443fd69993615697e9ac1cb9d380437b85eb300ce4d9b5a5bc2132660da3527031a1057a565f2c76775565b0088637707410f2e955355425efe496113149cf52c901bccc48864c8aa4262367213602b63aa1a8bed77826c0c476152ab3464a20c9cd73f17a1d019466f2ae37859e6e5a8bb8862a480c1b12d6797b79663ed2333f188f34e6cf6ec87e43979f88787ce35877ddf0b689547bf5ba9eebb2659d76354ebc39ee83975310aca4f8867ff290793cc08bf29e60a97c28a71ea3084fe27845ab3664e80592412043b03056fdd5744bd74c9584094c2b75c689aca8e4b3d3f91994e4722b9b331399310975275a0065935b6cdf5a6a8216188452394238bc82736488a84a0c96c580a81c69032ad5e96f4c3061df5ab246c258cba0b68a32916bfc6686730b3ff0944a070f535a113fc349cddb0b67b40debfb5215167090f9891365bb3d87639fda05843a079a430fd5892f57ac4510450dec00b7905a3a14442231919f9ed4a76b2b159a6ccc3685b3dd1924935aa8e617af18b5a065ac45727767ee897cf4f9442b2ace30c0237b307d3e76bf8eeb78addc4aacd16463d8602fd5487b63c88bb66027f37d0d614d6f9c24603c42947664ac4398c6c52383469b4f9777e5ec7206210f3e5a796bf45c53268e25f39ac261af3bfa2ee755beb8b67ab3ac8df6c629c1176e9e3b965e9369f9b3b92ad7c20955641d99526fe7b9fe8c850820275cd964849250090733ce124ecf316624374bd18b7c358c06e9c136ee1259a9245abc55b964d689f5a08292d28265658ebb40cbfe488a2228275590ab9f32a34109709c1c291d4a23337274c7a5a5991c7a87b81c974ab18ce77859e4995e7c14f0371748b7712fb52c5966cd63063c4f3b81b47c45dde83fb3a2724029b10b3230214c04fa0577fc29ac9086ae18c53b3ed44e507412fca04b4f538a51588ec1f1029d152d9ae7735f76a077aa9484380aed9189e5912487fcc5b7c7012d9223dd967eecdac3008a8931b648243537f548c171698c5b381d846a72e5c92d4226c5a8909884f1c4a3404c1720a5279414d7f27b2b982652b6740219c56d217780d7a5e5ba59836349f726881dea18ef75c0772a8b922766953718cacc14ccbacb5fc412a2d0be521817645ab2bf6a4785e92bc94caf477a967876796c0a5190315ac0885671a4c749564c3b2c7aed9064eba299ef214ba2f40493667c8bd032aec5621711b41a3852c5c2bab4a349ce4b7f085a812bbbc820b81befe63a05b8bcdfe9c2a70a8b1aca9bf9816481907ff4432461111287303f0bd817c05726bfa18a2e24c7724921028032f622bd960a317d83b356b57f4a8004499cbc73c97d1eb7745972631c0561c1a3ab6ef91bd363280a10545da693e6d58aed6845e7cc5f0d08ca7905052c77366d1972ccfcc1a27610cb543665aa798e20940128b9567a7edb7a900407c70d359438435e13961608d552a94c5cda7859220509b483c5c52a210e9c812bc0c2328ca00e789a56b2606b90292e3543dacaa2431841d61a22ca90c1ccf0b5b4e0a6f640536d1a26ab5b8d2151327928ce02904cf1d15e32788a95f62d3c270b6fa1508f97b9155a2726d80a1afa3c5387a276a4d031a08abf4f2e74f1a0bb8a0fd3cb0ac923a76d541ca65fdec9c788a407326c7db508119f617f43b6e8a6f48a398702e051c20c31de77a1ba6777829f5539c886e3e14ded294d56ae5e88ac06ab09'),
                    c=unhex('19c592505907c24c5fa2ebfa932d2cbb48f3e4340a28f7eba5d068fcacabedf77784e2b24d7961775f0bf1a997ae8ba9fc4311be63716779c2b788f812cbb78c74e7517e22e910eff5f38d44469c50de1675ae198fd6a289ae7e6c30a9d4351b3d1f4c36eff9c68da91c40b82dc9b2799a33a26b60a4e70d7101862779469f3a9daec8e3e8f8c6a16bf092fba5866186b8d208fdeb274ac1f829659dc2be4ac4f306cb5584bad1936a92c9b76819234281bb395841c25756086ea564ca3e227e3d9f1052c0766d2eb79a47c150721e0dea7c0069d551b264801b7727ecaf82eecb99a876fda090bf6c3fc6b109f1701485f03ce66274b8435b0a014cfb3e79cced67057b5ae2ad7f5279eb714942e4c1ccff7e85c0db43e5d41289207363b444bb51bb8ab0371e70cbd55f0f3dad403e105176e3e8a225d84ac8bee38c821ee0f547431145dcb3139286abb11794a43a3c1b5229e4bcfe959c78adaee2d5f2497b5d24bc21fa03a9a58c2455373ec89583e7e588d7fe67991ee93783ed4a6f9eeae04e64e2e1e0e699f6dc9c5d39ef9278c985e7fdf2a764ffd1a0b95792ad681e930d76df4efe5d65dbbd0f1438481ed833ad4946ad1c69ad21dd7c86185774426f3fcf53b52ad4b40d228ce124072f592c7daa057f17d790a5bd5b93834d58c08c88dc8f0ef488156425b744654eaca9d64858a4d6ceb478795194bfadb18dc0ea054f9771215ad3cb1fd031d7be4598621926478d375a1845aa91d7c733f8f0e188c83896edf83b8646c99e29c0da2290e71c3d2e970720c97b5b7f950486033c6a2571ddf2bccdabb2dfa5fce4c3a1884606041d181c728794ae0e806ecb49af16756a4ce73c87bd4234e60f05535fa5929fd5a34473266401f63bbd6b90e003472ac0ce88f1b666597279d056a632c8d6b790fd411767848a69e37a8a839bc766a02ca2f695ec63f056a4e2a114cacf9fd90d730c970db387f6de73395f701a1d953b2a89dd7edad439fc205a54a481e889b098d5255670f026b4a2bf02d2bdde87c766b25fc5e0fd453757e756d18c8cd912f9a77f8e6bf0205374b462'),
                    k_expected=unhex('0bf323338d6f0a21d5514b673cd10b714ce6e36f35bcd1bf544196368ee51a13'))
        # Decaps test from test group 2, test case 26 (accept)
        decaps_test('mlkem768',
                    dk=unhex('b09125afb3cfb5295581373ab6885284d9706318280d223edc987fd14410dbe82e6ac89adfab70e67ca4b1c641ad037fd8c47870f159ec79cdcd52605b9890499bb6dbd8347f342c61436b642c0ddf4617db06198b8285dce4c09d9775a2f41c8cd18af8e75f57d4127df94d901ac83bacbd584cc50c43750f49b357f59350875c9b475480a8aaa168592ddb158614a639813566d205368c6c39f0413ca3230df60d44008282b682ac66b76c3c95f00b2a555035529c86ef3905b4a3968fea7802b6c5eecb08e8f0c42d7ab7cd21a62fb136412a1840b52c99970ccf51892f73497c3775be2189f7fc25e7c74d81fc217683292aa4866ddb04469855323a0810f0893de5c7f94a9c0b5337db83c44891b2e694695b76575032bf51761682958bd4f97be9a355b4a85bb6858b7e5a5ef653ab781056af9187d811c3a8936e5706503db57062410bcc9421f1ab867a657856c411c4e025ecb3c387729ae8e112f330b988e22f47c35c280750d21b107687af7b329ef3cb5289f06fb7d44548391e97ba6dd499b5907c54958413d92aa99d5646cf47a8f48cb70a07ad056b4eefe6c8c46645f7028a32410558638c48e83ac1570160c3833bf64052f5b7df4364d3e0b24e790aa7c98cee0441e6731d9de22d156c61e1c740397672ef54724f01b9d49923aa321f86b98823f21360138392b90c69434635275f9bfbb9b8a99e8e1b7f4ec25f75dbce33c13f750170bd6722efe496e7463e16aaa5867b869a96ad41b22bd2556c924596fd778d79a102f6e46d8eb18fefac8db19993e5414ac816705286892492c8c9e852d6145dff0c10e4a6703a459e7e732a6dfa2766a622b0622bfedb8f41c125f61b2ec264853b9ccc165979f6a263beb148905aac7618a70e829e23f28696f92ef6fa07c102cdbdb1288ba5cff3a81abba15974535fe3106a80068f14e98964572350a7112b1601c196710c096ccf164fbce1aabac9c5b9535070e61ab8068d611ca765fabb6412607dab30c4fc6ad073731fdc4c48b88e267c47b439ad2560c30561815ceb1f52c896489944bbbab52b1b1d1680a1057964dafa600c93a39a447ddbb0adf911afe3e823d8acc7cc04659f625f2c1837bb175282542cd22601f621581ab5a6c0384e087ccd32a5380b522fdd3a4202b5b41c85caff2903b2dc2645703d9bc711fbb404c0c0376187ac588aaf5718522d2273a9408dabcbc9701698d2da172aa6267a4c9693a24011c2265a2b6dc8e96304a98ddc5319a3140c399a08412c20f48537870bb84c32a094457895511ff7ec421de01a64b78534653f78327441b90cd115939dfaafa95b40d0a63d62d12eb5c9096018cc83871e44e6cd0be26d16b7b5a209b8e6471d2954adf9fabd0153707c9caa2bcc38ded841c791a0eb597eeee2c518d926edb28ab53caa5b7746466931b0ac9150688bf37049c1f82bcf648332434cd0a92fd2c958353a26cb65cb499057109b2d688cc43c4b385da7c50868af1b8075e57088f5db12dfa493eacb6dc4ec6e205baa2a89858ec2823c00553714cde47a96e36c7c198b3ec57ccf74d92cddb86aa0a8b8b5ca9d52bb60aba79f4f72b0125532ceb7a9077480d2bb60df51a989d2cb65f94dcbfc890efc7d0e5a7a38344d1641a3d0b024d50797a5f23c3a18b3101a1269069f43a842bacc098a8821271c673db1beb33034e4d7774d16635c7c2c3c2763453538bc1632e1851591a51642974e5928abb8e55fe55612f9b141aff015545394b2092e590970ec29a7b7e7aa1fb4493bf7cb731906c2a5cb49e6614859064e19b8fa26af51c44b5e7535bfdac072b646d3ea490d277f0d97ced47395fed91e8f2bce0e3ca122c2025f74067ab928a822b35653a74f06757629afb1a1caf237100ea935e793c8f58a71b3d6ae2c8658b10150d4a38f572a0d49d28ae89451d338326fdb3b4350036c1081117740edb86b12081c5c1223dbb5660d5b3cb3787d481849304c68be875466f14ee5495c2bd795ae412d09002d65b8719b90cba3603ac4958ea03cc138c86f7851593125334701b677f82f4952a4c93b5b4c134bb42a857fd15c650864a6aa94eb691c0b691be4684c1f5b7490467fc01b1d1fda4dda35c4ecc231bc73a6fef42c99d34eb82a4d014987b3e386910c62679a118f3c5bd9f467e4162042424357db92ef484a4a1798c1257e870a30cb20aaa0335d83314fe0aa7e63a862648041a72a6321523220b1ace9bb701b21ac1253cb812c15575a9085eabeade73a4ae76e6a7b158a20586d78a5ac620a5c9abcc9c043350a73656b0abe822da5e0ba76045fad75401d7a3b703791b7e99261710f86b72421d240a347638377205a152c794130a4e047742b888303bddc309116764de7424cebea6db65348ac537e01a9cc56ea667d5aa87ac9aaa4317d262c10143050b8d07a728ca633c13e468abcead372c77b8ecf3b986b98c1e55860b2b4216766ad874c35ed7205068739230220b5a2317d102c598356f168acbe80608de4c9a710b8dd07078cd7c671058af1b0b8304a314f7b29be78a933c7b9294424954a1bf8bc745de86198659e0e1225a910726074969c39a97c19240601a46e013dcdcb677a8cbd2c95a40629c256f24a328951df57502ab30772cc7e5b850027c8551781ce4985bdacf6b865c104e8a4bc65c41694d456b7169e45ab3d7acabeafe23ad6a7b94d1979a2f4c1cae7cd77d681d290b5d8e451bfdcccf5310b9d12a88ec29b10255d5e17a192670aa9731c5ca67ec784c502781be8527d6fc003c6701b3632284b40307a527c7620377feb0b73f722c9e3cd4dec64876b93ab5b7cfc4a657f852b659282864384f442b22e8a21109387b8b47585fc680d0ba45c7a8b1d7274bda57845d100d0f42a3b74628773351fd7ac305b2497639be90b3f4f71a6aa3561eecc6a691bb5cb3914d8634ca1e1af543c049a8c6e868c51f0423bd2d5ae09b79e57c27f3fe3ae2b26a441babfc6718ce8c05b4fe793b910b8fbcbbe7f1013242b40e0514d0bdc5c88bac594c794ce5122fbf34896819147b928381587963b0b90034aa07a10be176e01c80ad6a4b71b10af4241400a2a4cbbc05961a15ec1474ed51a3cc6d35800679a462809caa3ab4f7094cd6610b4a700cba939e7eac93e38c99755908727619ed76a34e53c4fa25bfc97008206697dd145e5b9188e5b014e941681e15fe3e132b8a3903474148ba28b987111c9bcb3989bbbc671c581b44a492845f288e62196e471fed3c39c1bbddb0837d0d4706b0922c472e31df613da9a1dd33b5d2d8939684b89f7649e1c59b959ffbe972786c477f66177dbf3b059173fd06afcd90e80e862174fc57f97607bbff5b73d6360fb5c37'),
                    c=unhex('56b42d593aab8e8773bd92d76eabddf3b1546f8326f57a7b773764b6c0dd30470f68dff82e0dca92509274ecfe83a954735fde6e14676daaa3680c30d524f4efa79ed6a1f9ed7e1c00560e8683538c3105ab931be0d2b249b38cb9b13af5ceaf7887a59dba16688a7f28de0b14d19f391eb41832a56479416ccf94e997390ed7878eeaff49328a70e0ab5fce6c63c09b35f4e45994de615b88bb722f70e87d2bbd72ae71e1ee9008e459d8e743039a8ddeb874fce5301a2f8c0ee8c2fee7a4ee68b5ed6a6d9ab74f98bb3ba0fe89e82bd5a525c5e8790f818ccc605877d46c8bdb5c337b025bb840ff471896e43bfa99d73dbe31805c27a43e57f0618b3ae522a4644e0d4e4c1c548489431be558f3bfc50e16617e110dd7af9a6fd83e3fbb68c304d15f6cb700d61d7aa915a6751ea3ba80223e654132a20999a43bf408592730b9a9499636c09fa729f9cb1f9d3442f47357a2b9cf15d3103b9bf396c23088f118ede346b5c03891cfa5d517cef8471322e7e31087c4b036abad784bff72a9b11fa198facbcb91f067feaf76fcfe5327c1070b3da6988400756760d2d1f060298f1683d51e3616e98c51c9c03aa42f2e633651a47ad3cc2ab4a852ae0c4b04b4e1c3dd944445a2b12b4f42a6435105c04122fc3587afe409a00b308d63c5dd8163654504eedbb7b5329577c35fbeb3f463872cac28142b3c12a740ec6ea7ce9ad78c6fc8fe1b4df5fc55c1667f31f2312da07799dc870a478608549fedafe021f1cf2984180364e90ad98d845652aa3cdd7a8eb09f5e51423fab42a7b7bb4d514864be8d71297e9c3b17a993f0ae62e8ef52637bd1b885bd9b6ab727854d703d8dc478f96cb81fce4c60383ac01fcf0f971d4c8f352b7a82e218652f2c106ca92ae686bacfcef5d327347a97a9b375d67341552bc2c538778e0f9801823ccdfcd1eaaded55b18c9757e3f212b2889d3857db51f981d16185fd0f900853a75005e3020a8b95b7d8f2f2631c70d78a957c7a62e1b3719070acd1fd480c25b83847da027b6ebbc2eec2df22c87f9b46d5d7baf156b53cee929572b92c4784c4e829f3446a1ffe47f99decd0436029ddebd3ed8e87e5e73d123dbe8a4ddacf2abde87f33ae2b621c0ec5d5cad1259deec2aeff6088f04f27a20338b5762543e5100899a4cbfb7b3ca456b3a19b83a4c432230c23e1c7f107c4cb112152f1c0f30da0bb33f4f11f47eea43872bafa84ae22256d708e0604dade4b2a4dde8cccf11930e13553934ae3ece52f3d7ccc00287377879fe6b8ece7ef79423507c9da339559c20de1c51955999bae47401dc3cdfaa1b256d09c7db9fc8698bfcefa7302d56fbcde1fbaaa1c653454e6fd3d84e4f79a931c681cbb6cb462b10dae112bdfb7f65c7fdf6e5fc594ec3a474a94bd97e6ec81f71c230bf70ca0f13ce3dffbd9ff9804efd8f37a4d3629b43a8f55544ebc5ac0abd9a33d79699068346a0f1a3a96e115a5d80be165b562d082984d5aacc3a2301981a6418f8ba7d7b0d7ca5875c6'),
                    k_expected=unhex('2696d28e9c61c2a01ce9b1608dcb9d292785a0cd58efb7fe13b1de95f0db55b3'))
        # Decaps test from test group 3, test case 51 (accept)
        decaps_test('mlkem1024',
                    dk=unhex('673751cbb596541131c66398662cb4b0eb80796a88b28144a5bbc854f80d4b35be0ab241e4795f8fbba814f50fa80498cbe8bf68a0a583a4c5981b41df0667db614a628c3060697438e62c8d36026ee29c96b673bf1a194ee49481351f4d1748dd01cd023142f01057142b741cba8302e432f88c63d0b4b5767ac3a5a59afa3a321e65b1d1511807a06e16a04b2f1070e465586d4a9b68e2b42d57a356fa7bb3d04e51b193ff4c757cfa0f15924ea6e49afb83b2919c985869ada544338f44ae96a874c425af87bc73f3cb0fd2627b1539b1f19a77e36b7fc817851d39bd8a069a6c2202c17469d421a588e65daf450030b6674ec1c734aa25414b119e61b26efc90df81059d2b9599414f93692bf45a4b1c5cc09edb37b1b1433026aea6b0200722b819c7bc061c53a4304992fca2aee2324a324ab91c3e5d562096b8a141756940f15a2800c274ea4f65817e639c5d2a278c6a294f9db331f84ccb0a10309f530a06eb962573c86005c15bfc7531a143026396721297e25cb655a294964b2fe531905f2802376b8ace35ae3e2814bab7062bc1a840657dbfcb5f41bb55475697849a31e2222e995518ca7640ad4b9cee9820984138be0510ffd6ac225393a5f0cb030528cd2a0610e78a5cf1b073039a6d143068c53dbd15a1d4446da7b310ee795d1fb31b2f97008f83bdf348a593a3bdcbb571907b36d0978162c253e6f50106c463149834abfb0707d8ab4a4babc323598a085b309764b7c32c9db0c9f2d52ef2f00bace7846868c33b82afa430a4c2f67b698a60526a161cd62115dca767c203e3e2cc787031a73b5b7dba1eee5ab04b77bb569b952d9a15d198779804197d23c18e5b055f5c8087d742f64418d6505e70418abfc6b1bf7bb3de286599f4676cf87946d65144998afae1c689449e3f349fd0809afb856dde4a94a2c0258d56432f40c3da812d3fd3b72259a61d2882e0f50b355121e564c6bd33366f32bf4a5996b9998961354925a2bacdf48056118453ac3792a7879b71579adb65f5d83b1ed6c8c49836de379daa027e62b96f683c1688935cb3fccd64329267273e60c6cd59ba1b7fc911e2662527eccb7a474e5ef00ca9f789a3838e889242e7fb2b08f3790613c4eed3c912ec4eb029b971096b384727697b4ddc3b698c9a6da6971fa4c574ecd18eb1c84c0c5790153aa6b9db61d8bac0a680a37ed623582a7e8c0885ebb35af341477764368e0647b14553672316d0b90317c5b53aa747e61b4750db9e63cc3712900005ca24226b523e0a179582c85968c107857bb41521b7342b13dcac462a53be38446f2142519667b48b1c68fcafa4d3c7e3e5aff163c41f2c1b4dbac5456c30776078e7c3a713819f6b9aca55d77d60637183a723035730f94285c42ac3587637f66ac30f2c4039e60420967576e27b96c8c004d9585f33939ac44f0d195b35d472fc219076f12d0984ac844728d5d2266bb5cd8b325dda497b4f397bfe722c9d7684201a921f502271985cb3f31c04884c090b063631253dc454537031f2c82c10a1722de6c556464dc9d64389da37e469480c921065c79a30c83c867c952b30548a6b5bdfeb6ea6247480f163b427b17cf94889220fe934564dab90f5b6a11648870b654495a6691ae21fea86bdc8c49093fa07e926af3aba0e7cec21f613b49986c6c8a139eda70b7ed8211a3215e8c43ef8c151ae61740ef83b48276033614b58e9ceb992233cd21dff70c7a6f7171707a2add37acbf136a4eb4a79517fd0c8aff0b5126435c3100331f208a546c9a4044a8f0503c8ade9506a018b4ca7c6e8d70120017d38b13b52786a85a540d81b8e71c376b796a7215abf065086d3c80ee94b8f09e2a3ba13b82583b825388e87ba010af507173563789a1dcd088907c52bd7fc1c6930605f060f37978211c10fb5717e3fa291d20b5d43fb74cd4711394b0027e41c52b523797470532cbe123c92950720e5e255256577d4e156ebd4c698d813405c61430b978694acde78031e74ba1d8517dae2346f008411231fcce7bff75bc361e691e776049004097b36490d876288701b2d3a1743ab8753d47ac6200e2da7458d3a059681233872794e6720186b20108b1d1033971ce19ed67a2a28e499a360a4ad86ae4194034f202f8fa3626fe75f307a4cea4148219b958ea0b7886659235a4d1980b192610847d86ef32739f94c3b446c4d81d89b8b422a9d079c88b11acaf321b014294e18b296e52f3f744cf9634a4fb01db0d99ef20a633a552e76a0585c6109f018768b763af3678b4780089c1342b96907a29a1c11521c744c2797d0bf2b9ccdca614672b45076773f458a31ef869be1eb2efeb50d0e37495dc5ca55e07528934f6293c4168027d0e53d07facc6630cb08197e53fb193a171135dc8ad9979402a71b6926bcdcdc47b93401910a5fcc1a813b682b09ba7a72d2486d6c799516465c14729b26949b0b7cbc7c640f267fed80b162c51fd8e09227c101d505a8fae8a2d7054e28a78ba8750decf9057c83979f7abb084945648006c5b28804f34e73b238111a65a1f500b1cc606a848f2859070beba7573179f36149cf5801bf89a1c38cc278415528d03bdb943f96280c8cc52042d9b91faa9d6ea7bcbb7ab1897a3266966f78393426c76d8a49578b98b159ebb46ee0a883a270d8057cd0231c86906a91dbbade6b2469581e2bca2fea8389f7c74bcd70961ea5b934fbcf9a6590bf86b8db548854d9a3fb30110433bd7a1b659ca8568085639237b3bdc37b7fa716d482a25b54106b3a8f54d3aa99b5123da96066904592f3a54ee23a7981ab608a2f4413cc658946c6d7780ea765644b3cc06c70034ab4eb351912e7715b56755d09021571bf340ab92598a24e811893195b96a1629f8041f58658431561fc0ab15292b913ec473f04479bc145cd4c563a286235646cd305a9be1014e2c7b130c33eb77cc4a0d9786bd6bc2a954bf3005778f8917ce13789bbb962807858b67731572b6d3c9b4b5206fac9a7c8961698d88324a915186899b29923f08442a3d386bd416bcc9a100164c930ec35eafb6ab35851b6c8ce6377366a175f3d75298c518d44898933f53dee617145093379c4659f68583b2b28122666bec57838991ff16c368dd22c36e780c91a3582e25e19794c6bf2ab42458a8dd7705de2c2aa20c054e84b3ef35032798626c248263253a71a11943571340a978cd0a602e47dee540a8814ba06f31414797cdf6049582361bbaba387a83d89913fe4c0c112b95621a4bda8123a14d1a842fb57b83a4fbaf33a8e552238a596aae7a150d75da648bc44644977ba1f87a4c68a8c4bd245b7d00721f7d64e822b085b901312ec37a8169802160cce1160f010be8cbcace8e7b005d7839234a707868309d03784b4273b1c8a160133ed298184704625f29cfa086d13263ee5899123c596ba788e5c54a8e9ba829b8a9d904bc4bc0bbea76bc53ff811214598472c9c202b73eff035dc09703af7bf1babaac73193cb46117a7c9492a43fc95789a924c5912787b2e2090ebbcfd3796221f06debf9cf70e056b8b9161d6347f47335f3e1776da4bb87c15cc826146ff0249a413b45aa93a805196ea453114b524e310aedaa46e3b99642368782566d049a726d6cca910993aed621d0149ea588a9abd909dbb69aa22829d9b83ada2209a6c2659f2169d668b9314842c6e22a74958b4c25bbdcd293d99cb609d866749a485dfb56024883cf5465dba0363206587f45597f89002fb8607232138e03b2a894525f265370054b48863614472b95d0a2303442e378b0dd1c75acbab971a9a8d1281c79613acec6933c377b3c578c2a61a1ec181b101297a37cc5197b2942f6a0e4704c0ec63540481b9f159dc255b59bb55df496ae54217b7689bd51dba0383a3d72d852ffca76df05b66eeccbd47bc53040817628c71e361d6af889084916b408a466c96e7086c4a60a10fcf7537bb94afbcc7d437590919c28650c4f2368259226a9bfda3a3a0ba1b5087d9d76442fd786c6f81c68c0360d7194d7072c4533aea86c2d1f8c0a27696066f6cfd11003f797270b32389713cffa093d991b63844c385e72277f166f5a3934d6bb89a4788de28321defc7457ab484bd30986dc1dab3008cd7b22f69702fabb9a1045407da4791c3590ff599d81d688cfa7cc12a68c50f51a1009411b44850f9015dc84a93b17c7a207552c661ea9838e31b95ead546248e56be7a5130505268771199880a141771a9e47acfed590cb3aa7cb7c5f74911d8912c29d6233f4d53bc64139e2f55be75507dd77868e384aec581f3f411db1a742972d3ebfd3315c84a5ad63a0e75c8bca3e3041e05d9067aff3b1244f763e7983d48ba34134bab88d635d8cf8ff5d686058fa68b6c2feeaa5fa4de65757086c0125e937bcc0d02faa8988ae7169df07f6a771e6e7fe3ab65e965c63c3e40ed909'),
                    c=unhex('e2d5fd4c13cea0b52d874fea9012f3a51743a1093710bbf23950f9147a472ee5533928a2f46d592f35da8b4f758c893b0d7b98948be447b17cb2ae58af8a489ddd9232b99b1c0d2de77caa472bc3bbd4a7c60dbfdca92ebf3a1ce1c22dad13e887004e2924fd22656f5e508791de06d85e1a1426808ed9a89f6e2fd3c245d4758b22b02cade33b60fc889a33fc4447edebbfd4530de86596a33789d5dba6e6ec9f89879af4be4909a69017c9bb7a5e31815ea5f132eec4984faa7ccf594dd00d4d8487e45621af8f6e330551439c93ec078a7a3cc1594af91f8417375fd6088ceb5e85c67099091bac11498a0d711455f5e0d95cd7bbe5cdd8fecb319e6853c23c9be2c763df578666c40a40a87486e46ba8716146192904510a6dc59da8025825283d684db91410b4f12c6d8fbd0add75d3098918cb04ac7bc4db0d6bcdf1194dd86292e05b7b8630625b589cc509d215bbd06a2e7c66f424cdf8c40ac6c1e5ae6c964b7d9e92f95fc5c8852281628b81b9afabc7f03be3f62e8047bb88d01c68687b8dd4fe63820062b6788a53729053826ed3b7c7ef8241e19c85117b3c5341881d4f299e50374c8eefd5560bd18319a7963a3d02f0fbe84bc484b5a4018b97d274191c95f702bab9b0d105faf9fdcff97e437236567599faf73b075d406104d403cdf81224da590bec2897e30109e1f2e5ae4610c809a73f638c84210b3447a7c8b6dddb5ae200bf20e2fe4d4ba6c6b12767fb8760f66c5118e7a9935b41c9a471a1d3237688c1e618cc3be936aa3f5e44e086820b810e063211fc21c4044b3ac4d00df1bcc7b24dc07ba48b23b0fc12a3ed3d0a5cf7671415ab9cf21286fe63fb41418570555d4739b88104a8593f293025a4e3ee7c67e4b48e40f6ba8c09860c3fbbe55d45b45fc9ab629b17c276c9c9e2af3a043beafc18fd4f25ee7f83bddcd2d93914b7ed4f7c9af127f3f15c277be16551fef3ae03d7b9143f0c9c019ab97eea076366131f518363711b34e96d3f8a513f3e20b1d452c4b7ae3b975ea94d880dac6693399750d02220403f0d3e3fc1172a4de9dc280eaf0fee2883a6660bf5a3d246ff41d21b36ea521cf7aa689f800d0f86f4fa1057d8a13f9da8fffd0dc1fad3c04bb1cccb7c834db051a7ac2e4c60301996c93071ea416b421759935659cf62ca5f13ae07c3b195c148159d8beb03d440b00f5305765f20c0c46eee59c6d16206402db1c715e888bde59c781f35a7cc7c1c5ecb2155ae3e959c0964cc1ef8d7c69d1458a9a42f95f4c6b5b996345712aa290fbbf7dfd4a6e86463022a3f4725f6511bf7ea5e95c707cd3573609aadeaf540152c495f37fe6ec8bb9fa2aa61d15735934f4737928fde90ba995722465d4a64505a5201f07aa58cfd8ae226e02070b2dbf512b975319a7e8753b4fdae0eb4922869cc8e25c4a5560c2a0685de3ac392a8925ba882004894742e43ccfc277439ec8050a9aeb42932e01c840dfcedcc34d3991289a62c17d1284c839514b93351dbb2dda81f924565d70e7079d5b8126caab7a4a1c731655a53bcc09f5d63ec9086dea650055985edfa8297d9c95410c5d1894d17d5930549adbc2b8733c99fe62e17c4de34a5d89b12d18e42a422d2ce779c2c28eb2d98003d5cd323fcbecf02b5066e0e734810f09ed89013c00f011bd220f2e5d6a362df90599198a093b03c8d8efbfe0b617592faf1e64220c4440b53ffb47164f369c95290ba9f3108d686c57db645c53c012e57af25bd6693e2cc6b57651af1591fe5d8916640ec017c253df0606bb6b3035fae748f3d4034223b1b5efbf5283e778c1094291cf7b19be0f317350e6f8518fde0efb1381fb6e16c241f7f17a5210693a274159e7fac868cd0dc4359c3d9eefea0d9e31e43fa651392c65a543a59b3eee3a639dc9417d056a5ff0f160beee2eac29a7d88c0982cf70b5a46379f21e506aac61a9bb1b8c2b9dab0e44a823b61d0aa11d94f76a4a8e21f9d4280683208f4ea911116f6fd6a97426934ec3426b8c8f703da85e9dcf99336136003728b8ecdd04a389f6a817a78bfa61ba46020bf3c34829508f9d06d1553cd987aac380d86f168843ba3904de5f7058a41b4cd388bc9ce3aba7ee7139b7fc9e5b8cfaaa38990bd4a5db32e2613e7ec4f5f8b1292a38c6f4ff5a40490d76b126652fcf86e245235d636c65cd102b01e22781a72918c'),
                    k_expected=unhex('7264bde5c6cec14849693e2c3c86e48f80958a4f6186fc69333a4148e6e497f3'))
        # Decaps test from test group 4, test case 77 (reject)
        decaps_test('mlkem512',
                    dk=unhex('69f9cbfd1237ba161cf6e6c18f488fc6e39ab4a5c9e6c22ea4e3ad8f267a9c442010d32e61f83e6bfa5c58706145376dbb849528f68007c822b33a95b84904dcd2708d0340c8b808bcd3aad0e48b85849583a1b4e5945dd9514a7f6461e057b7ecf61957e97cf62815f9c32294b326e1a1c4e360b9498ba80f8ca91532b171d0aefc4849fa53bc617932e208a677c6044a6600b8d8b83f26a747b18cfb78beafc551ad52b7ca6cb88f3b5d9ce2af6c67956c478cef491f59e0191b3bbe929b94b666c176138b00f49724341ee2e164b94c053c185a51f93e00f36861613a7fd72febd23a8b96a260234239c9628f995dc13807b43a69468167cb1a8f9dd07ee3b33238f63096ebc49d5051c4b65963d74a4766c226f0b94f1862c2124c8c749748c0bc4dc14cb34906b81c5524fb8100798542dc6cc2aa0a708575eabcc11f96a9e61c017a96a7ce93c42091737113ae783c0ae8755e594111edfabfd86c3212c612a7b62afd3c7a5c78b2f07344b789c2b2dbb5f4448be97bba4233c0039c0fe84300f9b03ac99497e6d46b6e95308ff84790f612cf186ec16811e80c179316a63b25703f60b842b61907e62894e736647b3c09da6fec5932782b36e0635085a3949e694d7e17cba3d9064330438c071b5836a770c55f6213cc1425845de5a334d75d3e5058c7809fda4bcd78191da9797325e6236c2650fc604ee43a83ceb34980084403a33259857907799a9d2a713a633b5c904727f61e42520991d655705cb6bc1b74af60713ef8712f14086869be8eb297d228b325a0609fd615eab7081540a61a82abf43b7df98a595be11f416b41e1eb75bb57977c25c64e97437d88ca5fda6159d668f6bab8157555b5d54c0f47cbcd16843b1a0a0f0210ee310313967f3d516499018fdf3114772470a1889cc06cb6b6690ac31abcfaf4bc707684545b000b580ccbfcbce9fa70aaea0bbd9110992a7c6c06cb368527fd229090757e6fe75705fa592a7608f050c6f88703cc28cb000c1d7e77b897b72c62bcc7aea21a57729483d2211832bed612430c983103c69e8c072c0ea7898f2283bec48c5ac81984d4a5a83619735a842bd172c0d1b39f43588af170458ba9ee7492eaaa94ea53a4d38498ecbb98a5f407e7c97b4e166e397192c216033014b878e938075c6c1f10a0065abc3163722f1a2effec8d6e3a0c4f7174fc16b79fb5186a75168f81a56aa48a20a04bddf182c6e179c3f69061555ef7396dd0b7499601a6eb3a96a9a22d04f1168db56355b07600a20370637b645976bbd97b6d6288a0d3036360472e3ac71d566db8fbb1b1d76cb755cd0d68bdbfc048eba2525eea9dd5b144fb3b60fbc34239320cbc069b35ab16b8756536fb33e8a6af1dd42c79f48ad120ae4b159d3d8c319060cce569c3f6035365585d34413795a6a18ec5136ab13c90e3af14c0b8a464c86b9073222b56b3f7328aea798155325911250ef016d72802e3878aa50540cc983956971d6efa352c02554dc760a5a91358ea56370884fd5b3f85b70e83e4697deb1705169e9c60a74528cf15281cb1b1c457d467b5f93a60373d10e0cf6a837aa3c9596a72bec29b2d7e58653d533061d381d51759752217eb46cac7807c4ad38b611644acf0a3f26b6b084ab47a83bf0d696f8a4768fc35bca6bc7903b2a237c27749f5510c863869e6ae56bb2afe4771c9221874f50f5b14baad5993b49238fd0a0c9f79b7b4584e41301f7a885c9f91819bea00d512581730539fb37e59e86a6d19ca25f0a811c9b428ba8614aa4f94807bc031cbcc183f3bf07fe2c1a6eba80d5a706ee0dab27e231458025d84a7a9b0230501116c290a6bb50626d97b939850942828390b0a2001b7853ad1ae9b011b2db36caeea73a2328e3c56485b491c299115a017c907ab54317260a593a0d7ba6d06615d6e2ca84b860eff3ccb597211bfe36bdef8069afa36c5a73392722650e4957dca597acba5605b63c163cfa94b64ddd62301a4332083361972589db0599a694dd4547a5ee9196577c22ed427ac89bb8ba3753eb76c41f2c1129c8a77d6805fa719b1b6ca11b740a78a3d41b5330526ab87d58d5925315a1485edc647c1604eb38138de637ad2c6ca5be44e1008b2c0867b229ccc36619e2758c4c2029eaeb26e7a803fca305a59cd585e117d698ece011cc3fce54d2e114545a21ac5be6771ab8f13122fad295e745a503b142f91aef7bde99998845fda043555c9c1ee535be125e5dce5d266667e723e67b6ba891c16cba174098a3f351778b0888c9590a9090cd404'),
                    c=unhex('5c26d456c6c7b0e8df0b125e5d5428fe393655127a5e05bdd1bcac14c47493783097b6185058fa700555dd8af10f0f979a39a603826ffeb0b44e9487539f3f1a07c673e96640ddf754c8b98cd83473568b49d095f682c1acf0e160ab93eb41a16a57d53b419620d351c837315080d530845cf8d63cfccdb6e9dfbe220a2c14221aa392e6337fa364df0d2e0398f15ac3dc822b5dd7217081107a45c8cb8eaca51e034117962aee7ec0ee212fa67a5d4b07d355a0981e4285116ecf5ca9fab6e3105e4de4aec5e32938a1eb91e65ce7b39c3b9829aa1e72b8092c3622e519ee092fac8106d6597ceb941c763288723cb55044a36d4181052a78b424b0de1b0260f624a8d3b317095371ee9beea9272250d598ac63c2138d23f99087777a902eba2163171a07546b72fce7f86ee3b1dc1b8eac85440b8d241742c3771f91bf981909e4f3e2505c594761259ed3aada6aa09181b99037a395d66e6ee4bbef97de6ba36c53a1808cba50938038c151603105bd6a4199ea44bf4b08961672598cb708f896e03cd9b8f8ad89decfbe6be0ef0006b7bd2f4aa6eb21c0218ede601d46924cf391ae3a44e43d96ebe84a630937c3409ef0710970c27e3add4e64dc64e83942abea9ccf498ef1fe72b254043d2775a37e0b5ddd3f596ea131e0734afa9d0223f4cd9d1ab7304ca979ad37f717bedc3a9526f8fc94433fe4614f82e709456f39bee7bacc84e5a70114af1c2ac8b9b3faa81c8f35f5a5d24189e1a457f58166473f5f1df0170aab5e4ac8fc719f945ccbe6f2fed24b23321d95c4c850b278b8c4ea02e3098d5a599aa3d842cf889b7f284ac5e6e66386d63f2c860b997966b4df2c32288a50045012b7362727b856af4f8258509b563758752ffbb1040f3c2ad8b0ded64fc15c95c1a16de0dae6625a9effce190fc7f3261d844c114913c6b1152a258a37761b81879b59c37a1dfac07c3e934510b45da44c2581a79dafbf00fabb207306269d9b74b93f4367b3ba22ccc51b362de16e49d9fdbf8cff84f6ce6892ca2245d34ceb9c8759e702832b66a572de9f3016a38f7328700f96b2e947'),
                    k_expected=unhex('a4a24e182fea12ff128ab2d4afe6569817513ffc547db70636752c9c66c002b8'))
        # Decaps test from test group 5, test case 86 (reject)
        decaps_test('mlkem768',
                    dk=unhex('1e4ac87b1a692a529fdbbab93374c57d110b10f2b1ddebac0d196b7ba631b8e9293028a8f379888c422dc8d32bbf226010c2c1ec73189080456b0564b258b0f23131bc79c8e8c11cef3938b243c5ce9c0edd37c8f9d29877dbbb615b9b5ac3c948487e467196a9143efbc7cedb64b45d4acda2666cbc2804f2c8662e128f6a9969ec15bc0b9351f6f96346aa7abc743a14fa030e37a2e7597bddfc5a22f9cedaf8614832527210b26f024c7f6c0dcf551e97a4858764c321d1834ad51d75bb246d277237b7bd41dc4362d063f4298292272d01011780b79856b296c4e946658b79603197c9b2a99ec66acb06ce2f69b5a5a61e9bd06ad443ceb0c74ed65345a903b614e81368aac2b3d2a79ca8ccaa1c3b88fb82a36632860b3f7950833fd0212ec96ede4ab6f5a0bda3ec6060a658f9457f6cc87c6b620c1a1451987486e496612a101d0e9c20577c571edb5282608bf4e1ac926c0db1c82a504a799d89885ca6252bd5b1c183af701392a407c05b848c2a3016c40613f02a449b3c7926da067a533116506840097510460bbfd36073dcb0bfa009b36a9123eaa68f835f74a01b00d2097835964df521ce9210789c30b7f06e5844b444c53322396e4799baf6a88af7315860d0192d48c2c0da6b5ba64325543acdf5900e8bc477ab05820072d463affed097e062bd78c99d12b385131a241b708865b4190af69ea0a64db71448a60829369c7555198e438c9abc310bc70101913bb12faa5beef975841617c847cd6b336f877987753822020b92c4cc97055c9b1e0b128bf11f505005b6ab0e627795a20609efa991e598b80f37b1c6a1c3a1e9aee7028f77570ab2139128a00108c50eb305cdb8f9a603a6b078413f6f9b14c6d82b5199ce59d887902a281a027b717495fe12672a127bbf9b256c43720d7c160b281c12757da135b1933352be4ab67e40248afc318e2370c3b8208e695bdf337459b9acbfe5b487f76e9b4b4001d6cf90ca8c699a174d42972dc733f33389fdf59a1daba81d834955027334185ad02c76cf294846ca9294ba0ed66741ddec791cab34196ac5657c5a78321b56c33306b5102397a5c09c3508f76b48282459f81d0c72a43f737bc2f12f45422628b67db51ac1424276a6c08c3f7615665bbb8e928148a270f991bcf365a90f87c30687b68809c91f231813b866bea82e30374d80aa0c02973437498a53b14bf6b6ca1ed76ab8a20d54a083f4a26b7c038d81967640c20bf4431e71dacce8577b21240e494c31f2d877daf4924fd39d82d6167fbcc1f9c5a259f843e30987ccc4bce7493a2404b5e44387f707425781b743fb555685584e2557cc038b1a9b3f4043121f5472eb2b96e5941fec011ceea50791636c6abc26c1377ee3b5146fc7c85cb335b1e795eec2033ee44b9aa90685245ef7b4436c000e66bc8bcbf1cdb803ac1421b1fdb266d5291c8310373a8a3ce9562ab197953871ab99f382cc5aa9c0f273d1dca55d2712853871e1a83cb3b85450f76d3f3c42bab5505f7212fdb6b8b7f6029972a8f3751e4c94c1108b02d6ac79f8d938f05a1b2c229b14b42b31b01a364017e59578c6b033833774cb9b570f9086b722903b375446b495d8a29bf80751877a80fb724a0210c3e1692f397c2f1ddc2e6ba17af81b92acfabef5f7573cb493d184027b718238c89a3549b8905b28a83362867c082d3019d3ca70700731ceb73e8472c1a3a093361c5fea6a7d40955d07a41b64e50081a361b604cc518447c8e25765ab7d68b243275207af8ca6564a4cb1e94199dba1878c59bec809ab48b2f211badc6a1998d9c7227c1303f469d46a9c7e5303f98aba67569ae8227c16ba1fb3244466a25e7f823671810cc26206feb29c7e2a1a91959eeb03a98252a4f7412674eb9a4b277e1f2595fca64033b41b40330812e9735b7c607501cd8183a22afc3392553744f33c4d202526945c6d78a60e201a16987a6fa59d94464b56506556784824a07058f57320e76c825b9347f2936f4a0e5cdaa18cf8833945ae312a36b5f5a3810aac82381fdae4cb9c6831d8eb8abab850416443d739086b1c326fc2a3975704e396a59680c3b5f360f5480d2b62169cd94ca71b37bc5878ba2985e068ba050b2ce50726d4b4451b77aaa8676eae094982210192197b1e92a27f59868b78867887b9a70c32af84630aa908814379e6519150ba16439b5e2b0603d06aa6674557f5b0983e5cb6a97596069b01bb3128c416680657204fd07640392e16b19f337a99a304844e1aa474e9c799062971f672268960f5a82f950070bbe9c2a71950a3785bdf0b8440255ed63928d257845168b1eccc4191325aa76645719b28ebd89302dc6723c786df5217b243099ca78238e57e64692f206b177abc259660395cd7860fb35a16f6b2fe6548c85ab66330c517fa74cdf3cb49d26b1181901af775a1e180813b6a24c456829b5c38104ece43c76a437a6a33b6fc6c5e65c8a89466c1425485b29b9e1854368afca353e143d0a90a6c6c9e7fdb62a606856b5614f12b64b796020c3534c3605cfdc73b86714f411850228a28b8f4b49e663416c84f7e381f6af1071343bf9d39b45439240cc03897295fea080b14bb2d8119a880e164495c61bebc7139c11857c85e1750338d6343913706a507c9566464cd2837cf914d1a3c35e89b235c6ab7ed078bed234757c02ef6993d4a273cb8150528da4d76708177e9425546c83e147039766603b30da6268f4598a53194240a2832a3d67533b5056f9aaac61b4b17b9a2693aa0d58891e6cc56cdd772410900c405af20b903797c64876915c37b8487a1449ce924cd345c29a36e08238f7a157cc7e516ab5ba73c8063f726bb5a0a0319e57127438c7fc601c99ccaae4c1a83726fdcb5045ed1a82a985ea995396d77272c66ce493289f6110910f37c2741ce47026a6f8261999c6482572b1693912ef12eebea7acf9234fb409f2a6090e6b0bfd895469d0b2a921bb723f87a33ea5465ab90f514b67698c0768b6ca498b022c512fa0875f054aa2265867e31c0e522651e024a07d60dd9f633166921f4126bc2b6aa01cc15a09b85bff8218c5aae95bc1ffb26ae5a137670f04910ca9d7241b6660c394c5455917746a26682fb71a432ea9530e839bdeb07433004f45a0ddaa0b24e3a566a540815f281e3fc259ac6cbc0acb8d62268b603bc676ab415c474bb94873e4487ae31a4e3845c79901550890ee8784eef904fee62ba8c5f952c68413052e0a7e3388bb8ff0ad602ae3ea14d9df6dd5e4cc6a381a41da5c137ecc49df587e178eaf47702ec623780691a3233f69f12bd9c9b9637c51378ad71a831055277254cc63c5ad4cb76b4ab82e5fca135e8d26a6b3a89fa5b6f'),
                    c=unhex('74a26c7d27146a22c7eab420134e973799cec1da2df61ae0fa7905a3a47485a063076bfa22d6e4fe5059de0a32e38f11abd63f990e91bd0e3a5bc6e710dfe5dc0f6d4a18147ebc2e2d9b179374d83692c53efbd45f28a2a928c2494f903576c410eb1773895ebeadb119960eebda9c3c710795a6d9b781fc58b30d08107f4e20944a382afb079f31d21724f2c26e6a53412f0a908be7586f2b3d6d7c1dea0270e98aa209244bd88ed68aae01432342ba5f49e015cb476b5b78d15ea77a354cc9e9fd07137d8760be42fd4746c62c02028e7b405ddc95df3d021921cfeddb3d961b957eca302a263dab2dc117beb3e79efacfcf936dfc09fc0d19c358d724fa381ea06ca067c384e944302c3907ab15a1da4b41352692add59b061541f07eff25ec42f46e1a0e370cad06ff3fd997d4d2c5648af762231b382d0593401936cba21551a2ae30d8e8effcf43916b83138bb5e610364429879fa9cdd5b7d3cf2feabaa1dc8d50ce69402e21103e795df7074d1fcf65f8a4e18986d5417780602c63be5a044863384bd3d8ffb685eac567ed8349dcf2ceb702b7375b145729998049d13e2cd466cf2231b9d3a20018ee908f8514a6c6a89df7232f91fcd84b81ebc8bc539e9a37a4324755564be1bf4fa1fb4571e0abbc9b52f9d090c33be599de6c8532c7cb7ec8b4e2d3c07505280e99923865903ffd18bc13b9c8164aa1eae84e38d3f57fdb8801785f105a6a8574bd2fe9bf305848e525330bc2d24f0257e47a4950f433a9233e8cdeba81dbae7d8c1a06d01f70de6ef663207d84952827bab3d451cbea0990007fbdb4240fe899a706f7c1563e05c70be9d575189ef83e0cf76195f6652491cce04f1ce2092170a92e0dd7301246a4c44fc0b4ee6aaa63fc7027840abd2ec25f654589738cd38b9e10b975cfb6c1d2eb4da97736998f84fdddd810d72da3c5ab13507420ddbfaa4f7750c1fae9c7dfb30f40a12aea689fc78da900020e3abb32a364d5c6b3c7544a1b5734a41e95c8314b448cd0b738d829af772a8f81c51adba2d85f326c8f5d6961cf12d44a9bedea00d1df5b48f429b1ce0c15ea5f5bc10b017247ba2c6be922b0563b8e9698677cb6c45ccf2081bf84219d2904c11ff92199f8aefad62d8608e200802c5a07202cc820e9e520e31bf36a83002eca4018b0b3a398801562aa86c77ab0d50a8fbc3768b0a643b97e7f9072168de29b8175999c9aa48d301a3f0303172e9c7d4f16329d5ca9d42397c3982e10c9da42de88bd6c2ab91c1e71e778e58bb8f801f207a88a9b47f9c687afbba34eda6d2899e4fa0008aa2b539711753dc7c07f614e814f683d6c037562ae1fbbe6d7d5fa54b7a6d9451e11b01aaccc3bf2ed64742dd100e0eab2df6cccf937b6d5981eca0e01f3245cf26a72ad1adf066c8f5430d72f509963a657d85e554c14e26e8bec5d5f3ab998c9b29f16b04747d80749b30e51fd2a7f690c22f9986aaf6358d6fab8ded54971b32641de2b258590eeaa6bf1f32324a7c4c983f49466d86'),
                    k_expected=unhex('3d23b10df232a180786f61261e85278251746580bebca6acbad60aef6952be69'))
        # Decaps test from test group 6, test case 97 (reject)
        decaps_test('mlkem1024',
                    dk=unhex('8445c336f3518b298163dcbb6357597983ca2e873dcb49610cf52f14dbcb947c1f3ee9266967276b0c576cf7c30ee6b93dea5118676cbee1b1d4794206fb369aba41167b4393855c84eba8f32373c05bae7631c802744aadb6c2de41250c494315230b52826c34587cb21b183b49b2a5ac04921ac6bfac1b24a4b37a93a4b168cce7591be6111f476260f2762959f5c1640118c2423772e2ad03dc7168a38c6dd39f5f7254264280c8bc10b914168070472fa880acb8601a8a0837f25fe194687cd68b7de2340f036dad891d38d1b0ce9c2633355cf57b50b896036fca260d2669f85bac79714fdafb41ef80b8c30264c31386ae60b05faa542a26b41eb85f67068f088034ff67aa2e815aab8bca6bf71f70ecc3cbcbc45ef701fcd542bd21c7b09568f369c669f396473844fba14957f51974d852b978014603a210c019036287008994f21255b25099ad82aa132438963b2c0a47cdf5f32ba46b76c7a6559f18bfd555b762e487b6ac992fe20e283ca0b3f6164496955995c3b28a57bbc29826f06fb38b253470af631bc46c3a8f9ce824321985dd01c05f69b824f916633b40654c75aaeb9385576ffde2990a6b0a3be829d6d84e34f1780589c79204c63c798f55d23187e461d48c21e5c047e535b19f458bba1345b9e41e0cb4a9c2d8c40b490a3babc553b3026b1672d28cbc8b498a3a99579a832feae74610f0b6250cc333e9493eb1621ed34aa4ab175f2ca231152509acb6ac86b20f6b39108439e5ec12d465a0fef35003e14277a21812146b2544716d6ab82d1b0726c27a98d589ebdacc4c54ba77b2498f217e14e34e66025a2a143a992520a61c0672cc9cced7c9450c683e90a3e4651db623a6db39ac26125b7fc1986d7b0493b8b72de7707dc20bbdd43713156af7d9430ef45399663c2202739168692dd657545b056d9c92385a7f414b34b90c7960d57b35ba7dde7b81fca0119d741b12780926018fe4c8030bf038e18b4fa33743d0d3c846417e9d5915c246315938b1e233614501d026959551258b233230d428b181b132f1d0b026067ba816999bc0cd6b547e548b63c9eaa091bac493dc598dbc2b0e146a2591c2a8c009dd5170aae027c541a1b5e66e45c65612984c46770493ec896ef25aa9305e9f06692cd0b2f06962e205bebe113a34ebb1a4830a9b3749641bb935007b23b24bfe576956254d7a35aa496ac446c67a7fec85a60057e8580617bcb3fad15c76440fed54cc789394fea24452cc6b0585b7eb0a88bba9500d9800e6241afeb523b55a96a535151d1049573206e59c7feb070966823634f77d5f1291755a243119621af8084ab7ac1e22a0568c6201417cbe3655d8a08dd5b513884c98d5a493fd49382ea41860f133ccd601e885966426a2b1f23d42d82e24582d99725192c21777467b1457b1dd429a0c41a5c3d704cea06278c59941b438c62727097809b4530dbe837ea396b6d31077fad3733053989a8442aac4255cb163b8ca2f27501ea967305695abd659aa02c83ee60bb574203e9937ae1c621c8ecb5cc1d21d556960b5b9161ea96fffebac72e1b8a6154fc4d88b56c04741f090cbb156a737c9e6a22ba8ac704bc304f8e17e5ea845fde59fbf788cce0b97c8761f89a242f3052583c6844a632031c964a6c4a85a128a28619ba1bb3d1bea4b49841fc847614a066841f52ed0eb8ae0b8b096e92b8195405815b231266f36b18c1a53333dab95d2a9a374b5478a4a41fb8759957c9ab22cae545ab544ba8dd05b83f3a613a2437adb073a9635cb4bbc965fb454cf27b298a40cd0da3b8f9ca99d8cb4286c5eb476416796070ba535aaa58cdb451cd6db5cbb0ca20f0c71de97c30da97ec7906d06b4b939396028c46ba0e7a865bc8308a3810f1212006339f7bc169b1666fdf475911bbc8aaab41755c9a8aabfa23c0e37f84fe46999e030494b9298ef9934e8a649c0a5cce2b22f31809afed23955d87881d99fc1d352896cac9055bea0d016ccba7805a3a50e221630379bd01135221cad5d9517c8cc42637b9fc0718e9a9bb4945c72d8d11d3d659d83a3c419509af5b470dd89b7f3accf5f35cfc322115fd66a5cd2875651326f9b3168913be5b9c87ae0b025ec7a2f4a072750946ac61170a7826d9704c5a23a1c0a2325146c3bc1858826c6b39279c2da7438a370ed8a0aa5169e3bec29ed88478732758d454143e227f8595883297842e6af133b17e4811b0f5713ac73b7e347423eb92822d2306fa14500a7207a0672672046544acc4ea9c16ed7421a069e0d737a98628519c6a29a424a868b46d9a0cc7c6c9ddd8b8bcbf422c8f48a73143d5abb66bc55499418430802bac544463cc7319d17998f29411365766d04c847f3129d9077b7d8339bfb96a6739c3f6b74a8f05f9138ab2fe37acb57634d1820b50176f5a0b6bc2940f1d5938f1936b5f95828b92eb72973c1590aeb7a552ceca10b00c303b7c75d402071a79e2c810af7c745e3336712492a42043f2903a37c6434cee20b1d159b057699ff9c1d3bd68029839a08f43e6c1c819913532f911dd370c7021488e11cb504cb9c70570fff35b4b4601191dc1ad9e6adc5fa9618798d7cc860c87a939e4ccf8533632268cf1a51aff0cb811c5545cb1656e65269477430699ccdea3800630b78cd5810334ccf02e013f3b80244e70acdb060bbe7a553b063456b2ea807473413165ce57dd563473cfbc90618ade1f0b888aa48e722bb2751858fe19687442a48e7ca0d2a29cd51bfd8f78c17b9660bfb54a470b2ae9a955c6ab8d6e5cc92ac8ed3c185daa8bc29f0578ebb812b97c9e5a848a6384de4e75a31470b53066a8d027ba44b21749c0492465f9072b28376c4e290b30c1863f9e5b79996083422bd8c272c10ecc6eb9a0a8225b31aa0a66e35b9c0b9a79582ba20a3c04cd29914f083a0158288ba4d6eb62d87264b912bca39732fbde536a377ad02b8c835d4a2f4e7b1ce115d0c860beaa7955a49ad689586a89a2b9f9b10d1595d2fc065ad018a7d56c614471f8e946fe8ab49e8226591119fcadb4f9a861631378736b6688b782d58e97e4572753a9664b6b8536812b25911aa76a242375433192738eee762f6b84315bb3436231e0a9b277ed28ae0050728346457e13405062db2804b8da60bb5c793d4cc0e101cba2d9182fd7124ff52bf4ca28292ac26d678088953971dba0b6fec2c9659353291c70c5b9245a0ca253304afd3c95102bea66875c6201680b4bda38687b648c28eb37478e3bc00ca8a3cc27204642b42b68fcbe7b21a366d0668a5029a7deef94cdd6a95d7ea8931673bf7112d4042107b1b8b9700c974f9c4e83a8facd89bfe0ca3cc4c2fce80a03d3576c222a792b72b1f070ab7f6b6f2b5ca2af5054afa70a896990159b45d1003e2a05648675e596016f1b71dd0f7bda7e2097fc73b3a143d12c726020ac34958ad7062b92b9abf3ca6be5ae29f57135e625a367971837e6363d1532094e022a23467cf932e1f89b5b0803c1ec99b585a78b5865096746f32258214ecb38065c97f455e155acc2dd005a9c76bed59cda73837d303504e6c976a606a2be7bbec5948b91a349e8936688cc0279754b743abc58666b19b6c3260051f19206bb962bb6633eb0048e32baacc5b020d02c86ca9770ad469db54a106ac73a35b8057422b3db202c5a5b4e3d535f0fc99326c4b8b7b16f1cb5af96803fa8c195fc0bceddaaf012a51728b76489082373c91e92c87acca795160782e3b0dd643544bb96abc2708d49b759cf057aa223bafd96a330baf39810fe8671b4343c297da1e1969c996216ab5106da668941b160d4477017136cbca5b5a8d44c4a8b1cf3ef79785e5aa25c3a1ad6c24fd140f79207de5a499f8a1534ffa804aa7b3889cbe25c0414704aa57897f17862364eca56258007248813912b836497f0359c2f7238a05d305a0ea152e72b44417a868134e91b3ca7931232fd4c25f8c2a492a339cdc0a138967211451f2562678fa14080a34436c42b07865ac036a81e97a7787a938025caf813450368bed0c94b1857604526405d27a1c1abc81b5b6ec13c71930a97d9232cf7021ef87a4d155328e62b583a83b4af21f9f5750f8575150424f63b899d71cad267c09e4467146e16e9b6c653f008c311375e2e006d4076a546b82f5314222f7c654317e79ec6035b73faf491757e61c828326d53044541c4d4537abd3ea1e67998c3382974ca78ae1b1960e4a9226b0219ab070f0d7aa66d76f9316adb80c54d6499771b471e8168d47bcaa08324ab6ba92c3a70275f24fa4dc10e251633fb98d162bb5537202c6a553ce7841c4d40b873b85ca03a0a1e1cfade6ba5180ab1323ccba9a3e9c53d37575ab1fd9e7316c6feecb0a14df6f2da56c2f56f55a89635cfcfda47927af1f0a47b2d4e4e61634b1b51d37a3a307a972420de1b7a481b83e583b6af16f63cb00c6'),
                    c=unhex('4f90106ff7c3dc4e47417f31ab56b1c5e426c1ecd5878aad2b705e75062da5fa6f4d18b704c941c6c6d941fd21191a69210bc39e24950d9f851b6de8ce30023dc7536439104d42245f3e04e6aa6763f8ac97adbd04cc69547bce0bf290ffb5d12946301174af1b0868c14d4293fa9dcc5b23f809b02cc78defe7f27935b9b681e531fc21ccb2af8ef6144d8498e63e0ee48af8d4cef7ac1f669ac740b06f79ddb58e794f2fc2ca832e05a0374c18a4f2cc78343eea064abc5f468f4dd11e0b6e8fa1d18a221d8241450c05eb9edf90d9d7f666ac82e7fd44af9328e0bc6004d5b114e80e9b980d18e081d771dfcb2acfd40142a2eb33234f75733eab7d8ee8a5a6f796681a4a8af85cce86971b821d4ad8371049e94e280b77b15d111a42aeadfc08d4f804bd78885443e81a393df7c8754c460915846e09a0596587460038f55d06ec21434a1c2df44d0c16706e8d2b83f0e7833976ef05bf1d9f0ddc9a37597e401b817c2bec8e02eb9df7591e239f25f8648e7f2f4f673093bd9cb703da32b353f58514c6ab55748b194e52f153d52f5f33fe95c5f9f65ea97ba721e8ddf333b64d233a867a12701e00c5d8a9b5ae344f3d847c27c079dcc9c3b40ec4604a9f041e7987e8b930c658b9a132de4e422c0e27553a2a0eab8c859eb0e5677e83272725c5c1652e61b9bbf5c9c59bc2357a4d1db9c607f34dc1ba074b84dfc69e4097a7ad2ba9a58000027296ad39fc1ce218a5eec7adfa8aa3b9100b0b603cfc83c152589e12e6bd9ee10c49131a701d315dfec38e018328916f9ffaa7305cfb66781707d2d1020eb782f9f003db4e46b87d693f62e8bde170141ff71f26ddf5310c00c9163655f5217dd2c8b0466ac89db55bd7fb3b0964bc9009e9686185117dcb50d6d0297753cf7f1217e819ee60e3f0faec4a5af0c2ea83ccde15cf045c6961de8ff6235c9d93ba4c89b7a82a7471fcfb0b8ead54d56e8a1de21b3933ac5b4a0689eef3598926e17bbb16aec61ec30a2ccc0e0323ec282887c108c3a4e83e3666493d8653d0e92443808c79d770bff48a49e65ae089fec790bba4c66354ef67a334c1ea5c6c5707b6928ebd1bdb6a940fa242c6ebd7f3e71272421c9082841a6cad2894bb8ac85f105d8bbc9e6f0a3df0d7c46f6e2f4cab904ed157afa85d4a852220a9636e1e8821643a9e4028d87a430432f09354b3973182385cf5abfc8f84982bee0bcbf5d18637399163a09eb45711e07c4458498c76979107cf91b3fc590ea4ad715d656d5e56dc32146580101c952e02ed7017960d54caaccc70607196980adbdaea420a52c0559ed23c9514f8ca7ab7f3baafd2fab58960a64128d5a50e9ad8db7d23a90ce64c1bc349d118d3603358377f84ff5a64457fa1cf41b27094bca72360bd429415b9ef9accb7a5d7b9e5f5fdca8fcfa4592e91d7e5120df7e3c6675af2211bb94d856a5d2285fbbb36984a1345590930b13232565d54812a9345324c232653190323cc67c840e478d09e6ddbcf999f7aa3b556f80332e67aca41ec0661088d7696bb64e9a98a0749faa9854d9b48754023bacaf3c8081a46157c6453bdc89341d3092f3b5337874ce5de559a56a2ffb7f401f6e28eecaf4fde5b60dea73d6b2182ef68e07a8297f3c959e17139b5dedc72c7a0e103aff866e89d1f62a1f6b97b61bc059bde5a2a06087ef783a441f23dd191c692d03c097ff9ee831f7715c6e508bf475e79a8353e84b06a9356045c8fd09fba35879069b9a3f478fbd051143c13d753bc45f3040e85985efd6b149efa9455a18e2894e6ea0be58f451ff1156f93cc7117b5d091e9dd50d41bfccd44f2c4eb7812aefd13c8b68d7f0103bb6ca38d233b6aadd01845b7e44d13c1cb1577d6c4354b063991344787f8c0be667a7440b98917ad64cc2ef2bc82efc3398b3b1b238540756ce9fc5edd26cc20e761d592a1a0530aa8befcfe8dadbac99a417ca0827f4983ff5be656669f2b5f985ff6b16c44bbea131d1fcc70fc53bf31ef225d1f5d41863b51b57ea65c6164f7531ae492efa64161b7daba3ef4586f3459be8a962367dc276597b98e91ff594efe8849bad4cf91b9e5f244cf03ca9615be128e96958533544a56e735994b92e4ef0d5fab54b78ec66641c7463f225d261c144f00a0270741d7a511994833635a8a9b670cbfbef239bf83327e247943b205da68db94e3f3'),
                    k_expected=unhex('7545cc458e0a274a83b13554224f0bd01d57cc4775ad12468d3fee5b08c93a6a'))

if __name__ == "__main__":
    # Run the tests, suppressing automatic sys.exit and collecting the
    # unittest.TestProgram instance returned by unittest.main instead.
    testprogram = unittest.main(exit=False)

    # If any test failed, just exit with failure status.
    if not testprogram.result.wasSuccessful():
        childprocess.wait_for_exit()
        sys.exit(1)

    # But if no tests failed, we have one last check to do: look at
    # the subprocess's return status, so that if Leak Sanitiser
    # detected any memory leaks, the success return status will turn
    # into a failure at the last minute.
    childprocess.check_return_status()
