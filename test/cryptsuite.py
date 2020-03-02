#!/usr/bin/env python3

import unittest
import struct
import itertools
import functools
import contextlib
import hashlib
import binascii
import base64
try:
    from math import gcd
except ImportError:
    from fractions import gcd

from eccref import *
from testcrypt import *
from ssh import *

try:
    base64decode = base64.decodebytes
except AttributeError:
    base64decode = base64.decodestring

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

@contextlib.contextmanager
def queued_random_data(nbytes, seed):
    hashsize = 512 // 8
    data = b''.join(
        hashlib.sha512(unicode_to_bytes("preimage:{:d}:{}".format(i, seed)))
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
                             unicode_to_bytes("{:x}".format(i)))
            self.assertEqual(mp_get_hex_uppercase(n),
                             unicode_to_bytes("{:X}".format(i)))
        checkHex("0")
        checkHex("f")
        checkHex("00000000000000000000000000000000000000000000000000")
        checkHex("d5aa1acd5a9a1f6b126ed416015390b8dc5fceee4c86afc8c2")
        checkHex("ffffffffffffffffffffffffffffffffffffffffffffffffff")

        def checkDec(hexstr):
            n = mp_from_hex(hexstr)
            i = int(hexstr, 16)
            self.assertEqual(mp_get_decimal(n),
                             unicode_to_bytes("{:d}".format(i)))
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
        mp_add_integer_into(mp10, mp10, 10)
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

        # mp_{add,sub}_integer_into should be able to cope with any
        # uintmax_t. Test a number that requires more than 32 bits.
        mp_add_integer_into(x, initial, 123123123123123)
        self.assertEqual(int(x), 4444444444567567567567567)
        mp_sub_integer_into(x, initial, 123123123123123)
        self.assertEqual(int(x), 4444444444321321321321321)

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
            mp_rshift_fixed_into(mp, mp, i)
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
        wc = ecc_weierstrass_curve(p256.p, int(p256.a), int(p256.b), None)
        wG = ecc_weierstrass_point_new(wc, int(p256.G.x), int(p256.G.y))
        self.assertTrue(ecc_weierstrass_point_valid(wG))

        ints = set(i % p256.p for i in fibonacci_scattered(10))
        ints.remove(0) # the zero multiple isn't expected to work
        for i in sorted(ints):
            wGi = ecc_weierstrass_multiply(wG, i)
            x, y = ecc_weierstrass_get_affine(wGi)
            rGi = p256.G * i
            self.assertEqual(int(x), int(rGi.x))
            self.assertEqual(int(y), int(rGi.y))

    def testMontgomeryMultiply(self):
        mc = ecc_montgomery_curve(
            curve25519.p, int(curve25519.a), int(curve25519.b))
        mG = ecc_montgomery_point_new(mc, int(curve25519.G.x))

        ints = set(i % p256.p for i in fibonacci_scattered(10))
        ints.remove(0) # the zero multiple isn't expected to work
        for i in sorted(ints):
            mGi = ecc_montgomery_multiply(mG, i)
            x = ecc_montgomery_get_affine(mGi)
            rGi = curve25519.G * i
            self.assertEqual(int(x), int(rGi.x))

    def testEdwardsMultiply(self):
        ec = ecc_edwards_curve(ed25519.p, int(ed25519.d), int(ed25519.a), None)
        eG = ecc_edwards_point_new(ec, int(ed25519.G.x), int(ed25519.G.y))

        ints = set(i % ed25519.p for i in fibonacci_scattered(10))
        ints.remove(0) # the zero multiple isn't expected to work
        for i in sorted(ints):
            eGi = ecc_edwards_multiply(eG, i)
            x, y = ecc_edwards_get_affine(eGi)
            rGi = ed25519.G * i
            self.assertEqual(int(x), int(rGi.x))
            self.assertEqual(int(y), int(rGi.y))

class crypt(MyTestBase):
    def testSSH1Fingerprint(self):
        # Example key and reference fingerprint value generated by
        # OpenSSH 6.7 ssh-keygen
        rsa = rsa_bare(65537, 984185866443261798625575612408956568591522723900235822424492423996716524817102482330189709310179009158443944785704183009867662230534501187034891091310377917105259938712348098594526746211645472854839799025154390701673823298369051411)
        fp = rsa_ssh1_fingerprint(rsa)
        self.assertEqual(
            fp, b"768 96:12:c8:bc:e6:03:75:86:e8:c7:b9:af:d8:0c:15:75")

    def testAES(self):
        # My own test cases, generated by a mostly independent
        # reference implementation of AES in Python. ('Mostly'
        # independent in that it was written by me.)

        def vector(cipher, key, iv, plaintext, ciphertext):
            for suffix in "hw", "sw":
                c = ssh_cipher_new("{}_{}".format(cipher, suffix))
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

        for suffix in "hw", "sw":
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

            for suffix in "hw", "sw":
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
        c = unhex('7b112d00c0fc95bc13fcdacfd43281bf'
                  'de9389db1bbcfde79d59a303d41fd2eb'
                  '0955c9477ae4ee3a4d6c1fbe474c0ef6')
        self.assertEqualBin(aes256_encrypt_pubkey(k, p), c)
        self.assertEqualBin(aes256_decrypt_pubkey(k, c), p)

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
            ("aes256_ctr_hw", 32,   16, False, unhex('b87b35e819f60f0f398a37b05d7bcf0b04ad4ebe570bd08e8bfa8606bafb0db2cfcd82baf2ccceae5de1a3c1ae08a8b8fdd884fdc5092031ea8ce53333e62976')),
            ("aes256_ctr_sw", 32,   16, False, unhex('b87b35e819f60f0f398a37b05d7bcf0b04ad4ebe570bd08e8bfa8606bafb0db2cfcd82baf2ccceae5de1a3c1ae08a8b8fdd884fdc5092031ea8ce53333e62976')),
            ("aes256_cbc",    32,   16, True,  unhex('381cbb2fbcc48118d0094540242bd990dd6af5b9a9890edd013d5cad2d904f34b9261c623a452f32ea60e5402919a77165df12862742f1059f8c4a862f0827c5')),
            ("aes256_cbc_hw", 32,   16, True,  unhex('381cbb2fbcc48118d0094540242bd990dd6af5b9a9890edd013d5cad2d904f34b9261c623a452f32ea60e5402919a77165df12862742f1059f8c4a862f0827c5')),
            ("aes256_cbc_sw", 32,   16, True,  unhex('381cbb2fbcc48118d0094540242bd990dd6af5b9a9890edd013d5cad2d904f34b9261c623a452f32ea60e5402919a77165df12862742f1059f8c4a862f0827c5')),
            ("aes192_ctr",    24,   16, False, unhex('06bcfa7ccf075d723e12b724695a571a0fad67c56287ea609c410ac12749c51bb96e27fa7e1c7ea3b14792bbbb8856efb0617ebec24a8e4a87340d820cf347b8')),
            ("aes192_ctr_hw", 24,   16, False, unhex('06bcfa7ccf075d723e12b724695a571a0fad67c56287ea609c410ac12749c51bb96e27fa7e1c7ea3b14792bbbb8856efb0617ebec24a8e4a87340d820cf347b8')),
            ("aes192_ctr_sw", 24,   16, False, unhex('06bcfa7ccf075d723e12b724695a571a0fad67c56287ea609c410ac12749c51bb96e27fa7e1c7ea3b14792bbbb8856efb0617ebec24a8e4a87340d820cf347b8')),
            ("aes192_cbc",    24,   16, True,  unhex('ac97f8698170f9c05341214bd7624d5d2efef8311596163dc597d9fe6c868971bd7557389974612cbf49ea4e7cc6cc302d4cc90519478dd88a4f09b530c141f3')),
            ("aes192_cbc_hw", 24,   16, True,  unhex('ac97f8698170f9c05341214bd7624d5d2efef8311596163dc597d9fe6c868971bd7557389974612cbf49ea4e7cc6cc302d4cc90519478dd88a4f09b530c141f3')),
            ("aes192_cbc_sw", 24,   16, True,  unhex('ac97f8698170f9c05341214bd7624d5d2efef8311596163dc597d9fe6c868971bd7557389974612cbf49ea4e7cc6cc302d4cc90519478dd88a4f09b530c141f3')),
            ("aes128_ctr",    16,   16, False, unhex('0ad4ddfd2360ec59d77dcb9a981f92109437c68c5e7f02f92017d9f424f89ab7850473ac0e19274125e740f252c84ad1f6ad138b6020a03bdaba2f3a7378ce1e')),
            ("aes128_ctr_hw", 16,   16, False, unhex('0ad4ddfd2360ec59d77dcb9a981f92109437c68c5e7f02f92017d9f424f89ab7850473ac0e19274125e740f252c84ad1f6ad138b6020a03bdaba2f3a7378ce1e')),
            ("aes128_ctr_sw", 16,   16, False, unhex('0ad4ddfd2360ec59d77dcb9a981f92109437c68c5e7f02f92017d9f424f89ab7850473ac0e19274125e740f252c84ad1f6ad138b6020a03bdaba2f3a7378ce1e')),
            ("aes128_cbc",    16,   16, True,  unhex('36de36917fb7955a711c8b0bf149b29120a77524f393ae3490f4ce5b1d5ca2a0d7064ce3c38e267807438d12c0e40cd0d84134647f9f4a5b11804a0cc5070e62')),
            ("aes128_cbc_hw", 16,   16, True,  unhex('36de36917fb7955a711c8b0bf149b29120a77524f393ae3490f4ce5b1d5ca2a0d7064ce3c38e267807438d12c0e40cd0d84134647f9f4a5b11804a0cc5070e62')),
            ("aes128_cbc_sw", 16,   16, True,  unhex('36de36917fb7955a711c8b0bf149b29120a77524f393ae3490f4ce5b1d5ca2a0d7064ce3c38e267807438d12c0e40cd0d84134647f9f4a5b11804a0cc5070e62')),
            ("blowfish_ctr",  32,    8, False, unhex('079daf0f859363ccf72e975764d709232ec48adc74f88ccd1f342683f0bfa89ca0e8dbfccc8d4d99005d6b61e9cc4e6eaa2fd2a8163271b94bf08ef212129f01')),
            ("blowfish_ssh2", 16,    8, True,  unhex('e986b7b01f17dfe80ee34cac81fa029b771ec0f859ae21ae3ec3df1674bc4ceb54a184c6c56c17dd2863c3e9c068e76fd9aef5673465995f0d648b0bb848017f')),
            ("blowfish_ssh1", 32,    8, True,  unhex('d44092a9035d895acf564ba0365d19570fbb4f125d5a4fd2a1812ee6c8a1911a51bb181fbf7d1a261253cab71ee19346eb477b3e7ecf1d95dd941e635c1a4fbf')),
            ("arcfour256",    32, None, False, unhex('db68db4cd9bbc1d302cce5919ff3181659272f5d38753e464b3122fc69518793fe15dd0fbdd9cd742bd86c5e8a3ae126c17ecc420bd2d5204f1a24874d00fda3')),
            ("arcfour128",    16, None, False, unhex('fd4af54c5642cb29629e50a15d22e4944e21ffba77d0543b27590eafffe3886686d1aefae0484afc9e67edc0e67eb176bbb5340af1919ea39adfe866d066dd05')),
        ]

        for alg, keylen, ivlen, simple_cbc, c in ciphers:
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

    def testPRNG(self):
        hashalg = 'sha256'
        seed = b"hello, world"
        entropy = b'1234567890' * 100
        rev = lambda s: valbytes(reversed(bytevals(s)))

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

        key1 = hash_str(hashalg, b'R' + seed)
        expected_data1 = b''.join(
            rev(hash_str(hashalg, key1 + b'G' + ssh2_mpint(counter)))
            for counter in range(4))
        # After prng_read finishes, we expect the PRNG to have
        # automatically reseeded itself, so that if its internal state
        # is revealed then the previous output can't be reconstructed.
        key2 = hash_str(hashalg, key1 + b'R')
        expected_data2 = b''.join(
            rev(hash_str(hashalg, key2 + b'G' + ssh2_mpint(counter)))
            for counter in range(4,8))
        # There will have been another reseed after the second
        # prng_read, and then another due to the entropy.
        key3 = hash_str(hashalg, key2 + b'R')
        key4 = hash_str(hashalg, key3 + b'R' + hash_str(hashalg, entropy))
        expected_data3 = b''.join(
            rev(hash_str(hashalg, key4 + b'G' + ssh2_mpint(counter)))
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
            ('p256', 'AAAAE2VjZHNhLXNoYTItbmlzdHAyNTYAAAAIbmlzdHAyNTYAAABBBHkYQ0sQoq5LbJI1VMWhw3bV43TSYi3WVpqIgKcBKK91TcFFlAMZgceOHQ0xAFYcSczIttLvFu+xkcLXrRd4N7Q=', 'AAAAIQCV/1VqiCsHZm/n+bq7lHEHlyy7KFgZBEbzqYaWtbx48Q==', 256, b'nistp256,0x7918434b10a2ae4b6c923554c5a1c376d5e374d2622dd6569a8880a70128af75,0x4dc14594031981c78e1d0d3100561c49ccc8b6d2ef16efb191c2d7ad177837b4', [(0, 'AAAAE2VjZHNhLXNoYTItbmlzdHAyNTYAAABIAAAAIAryzHDGi/TcCnbdxZkIYR5EGR6SNYXr/HlQRF8le+/IAAAAIERfzn6eHuBbqWIop2qL8S7DWRB3lenN1iyL10xYQPKw')]),
            ('p384', 'AAAAE2VjZHNhLXNoYTItbmlzdHAzODQAAAAIbmlzdHAzODQAAABhBMYK8PUtfAlJwKaBTIGEuCzH0vqOMa4UbcjrBbTbkGVSUnfo+nuC80NCdj9JJMs1jvfF8GzKLc5z8H3nZyM741/BUFjV7rEHsQFDek4KyWvKkEgKiTlZid19VukNo1q2Hg==', 'AAAAMGsfTmdB4zHdbiQ2euTSdzM6UKEOnrVjMAWwHEYvmG5qUOcBnn62fJDRJy67L+QGdg==', 384, b'nistp384,0xc60af0f52d7c0949c0a6814c8184b82cc7d2fa8e31ae146dc8eb05b4db9065525277e8fa7b82f34342763f4924cb358e,0xf7c5f06cca2dce73f07de767233be35fc15058d5eeb107b101437a4e0ac96bca90480a89395989dd7d56e90da35ab61e', [(0, 'AAAAE2VjZHNhLXNoYTItbmlzdHAzODQAAABpAAAAMDmHrtXCADzLvkkWG/duBAHlf6B1mVvdt6F0uzXfsf8Yub8WXNUNVnYq6ovrWPzLggAAADEA9izzwoUuFcXYRJeKcRLZEGMmSDDPzUZb7oZR0UgD1jsMQXs8UfpO31Qur/FDSCRK')]),
            ('p521', 'AAAAE2VjZHNhLXNoYTItbmlzdHA1MjEAAAAIbmlzdHA1MjEAAACFBAFrGthlKM152vu2Ghk+R7iO9/M6e+hTehNZ6+FBwof4HPkPB2/HHXj5+w5ynWyUrWiX5TI2riuJEIrJErcRH5LglADnJDX2w4yrKZ+wDHSz9lwh9p2F+B5R952es6gX3RJRkGA+qhKpKup8gKx78RMbleX8wgRtIu+4YMUnKb1edREiRg==', 'AAAAQgFh7VNJFUljWhhyAEiL0z+UPs/QggcMTd3Vv2aKDeBdCRl5di8r+BMm39L7bRzxRMEtW5NSKlDtE8MFEGdIE9khsw==', 521, b'nistp521,0x16b1ad86528cd79dafbb61a193e47b88ef7f33a7be8537a1359ebe141c287f81cf90f076fc71d78f9fb0e729d6c94ad6897e53236ae2b89108ac912b7111f92e094,0xe72435f6c38cab299fb00c74b3f65c21f69d85f81e51f79d9eb3a817dd125190603eaa12a92aea7c80ac7bf1131b95e5fcc2046d22efb860c52729bd5e75112246', [(0, 'AAAAE2VjZHNhLXNoYTItbmlzdHA1MjEAAACMAAAAQgCLgvftvwM3CUaigrW0yzmCHoYjC6GLtO+6S91itqpgMEtWPNlaTZH6QQqkgscijWdXx98dDkQao/gcAKVmOZKPXgAAAEIB1PIrsDF1y6poJ/czqujB7NSUWt31v+c2t6UA8m2gTA1ARuVJ9XBGLMdceOTB00Hi9psC2RYFLpaWREOGCeDa6ow=')]),
            ('dsa', 'AAAAB3NzaC1kc3MAAABhAJyWZzjVddGdyc5JPu/WPrC07vKRAmlqO6TUi49ah96iRcM7/D1aRMVAdYBepQ2mf1fsQTmvoC9KgQa79nN3kHhz0voQBKOuKI1ZAodfVOgpP4xmcXgjaA73Vjz22n4newAAABUA6l7/vIveaiA33YYv+SKcKLQaA8cAAABgbErc8QLw/WDz7mhVRZrU+9x3Tfs68j3eW+B/d7Rz1ZCqMYDk7r/F8dlBdQlYhpQvhuSBgzoFa0+qPvSSxPmutgb94wNqhHlVIUb9ZOJNloNr2lXiPP//Wu51TxXAEvAAAAAAYQCcQ9mufXtZa5RyfwT4NuLivdsidP4HRoLXdlnppfFAbNdbhxE0Us8WZt+a/443bwKnYxgif8dgxv5UROnWTngWu0jbJHpaDcTc9lRyTeSUiZZK312s/Sl7qDk3/Du7RUI=', 'AAAAFGx3ft7G8AQzFsjhle7PWardUXh3', 768, b'0x9c966738d575d19dc9ce493eefd63eb0b4eef29102696a3ba4d48b8f5a87dea245c33bfc3d5a44c54075805ea50da67f57ec4139afa02f4a8106bbf67377907873d2fa1004a3ae288d5902875f54e8293f8c66717823680ef7563cf6da7e277b,0xea5effbc8bde6a2037dd862ff9229c28b41a03c7,0x6c4adcf102f0fd60f3ee6855459ad4fbdc774dfb3af23dde5be07f77b473d590aa3180e4eebfc5f1d94175095886942f86e481833a056b4faa3ef492c4f9aeb606fde3036a8479552146fd64e24d96836bda55e23cffff5aee754f15c012f000,0x9c43d9ae7d7b596b94727f04f836e2e2bddb2274fe074682d77659e9a5f1406cd75b87113452cf1666df9aff8e376f02a76318227fc760c6fe5444e9d64e7816bb48db247a5a0dc4dcf654724de49489964adf5dacfd297ba83937fc3bbb4542', [(0, 'AAAAB3NzaC1kc3MAAAAo0T2t6dr8Qr5DK2B0ETwUa3BhxMLPjLY0ZtlOACmP/kUt3JgByLv+3g==')]),
            ('rsa', 'AAAAB3NzaC1yc2EAAAABJQAAAGEA2ChX9+mQD/NULFkBrxLDI8d1PHgrInC2u11U4Grqu4oVzKvnFROo6DZeCu6sKhFJE5CnIL7evAthQ9hkXVHDhQ7xGVauzqyHGdIU4/pHRScAYWBv/PZOlNMrSoP/PP91', 'AAAAYCMNdgyGvWpez2EjMLSbQj0nQ3GW8jzvru3zdYwtA3hblNUU9QpWNxDmOMOApkwCzUgsdIPsBxctIeWT2h+v8sVOH+d66LCaNmNR0lp+dQ+iXM67hcGNuxJwRdMupD9ZbQAAADEA7XMrMAb4WuHaFafoTfGrf6Jhdy9Ozjqi1fStuld7Nj9JkoZluiL2dCwIrxqOjwU5AAAAMQDpC1gYiGVSPeDRILr2oxREtXWOsW+/ZZTfZNX7lvoufnp+qvwZPqvZnXQFHyZ8qB0AAAAwQE0wx8TPgcvRVEVv8Wt+o1NFlkJZayWD5hqpe/8AqUMZbqfg/aiso5mvecDLFgfV', 768, b'0x25,0xd82857f7e9900ff3542c5901af12c323c7753c782b2270b6bb5d54e06aeabb8a15ccabe71513a8e8365e0aeeac2a11491390a720bedebc0b6143d8645d51c3850ef11956aeceac8719d214e3fa4745270061606ffcf64e94d32b4a83ff3cff75', [(0, 'AAAAB3NzaC1yc2EAAABgrLSC4635RCsH1b3en58NqLsrH7PKRZyb3YmRasOyr8xIZMSlKZyxNg+kkn9OgBzbH9vChafzarfHyVwtJE2IMt3uwxTIWjwgwH19tc16k8YmNfDzujmB6OFOArmzKJgJ'), (2, 'AAAADHJzYS1zaGEyLTI1NgAAAGAJszr04BZlVBEdRLGOv1rTJwPiid/0I6/MycSH+noahvUH2wjrRhqDuv51F4nKYF5J9vBsEotTSrSF/cnLsliCdvVkEfmvhdcn/jx2LWF2OfjqETiYSc69Dde9UFmAPds='), (4, 'AAAADHJzYS1zaGEyLTUxMgAAAGBxfZ2m+WjvZ5YV5RFm0+w84CgHQ95EPndoAha0PCMc93AUHBmoHnezsJvEGuLovUm35w/0POmUNHI7HzM9PECwXrV0rO6N/HL/oFxJuDYmeqCpjMVmN8QXka+yxs2GEtA=')]),
        ]

        for alg, pubb64, privb64, bits, cachestr, siglist in test_keys:
            # Decode the blobs in the above test data.
            pubblob = base64decode(pubb64.encode('ASCII'))
            privblob = base64decode(privb64.encode('ASCII'))

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
                sigblob = base64decode(sigb64.encode('ASCII'))

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
                    sigbytes = list(bytevals(sigblob))
                    bit = 8 * len(sigbytes) * n // d
                    sigbytes[bit // 8] ^= 1 << (bit % 8)
                    badsig = valbytes(sigbytes)
                    for key in [pubkey, privkey, privkey2]:
                        self.assertFalse(ssh_key_verify(
                            key, badsig, test_message))

class standard_test_vectors(MyTestBase):
    def testAES(self):
        def vector(cipher, key, plaintext, ciphertext):
            for suffix in "hw", "sw":
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
        for hashname in ['sha1_sw', 'sha1_hw']:
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
        for hashname in ['sha256_sw', 'sha256_hw']:
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
        # Test cases from RFC 6234 section 8.5, omitting the ones
        # whose input is not a multiple of 8 bits
        self.assertEqualBin(hash_str('sha384', "abc"), unhex(
            'cb00753f45a35e8bb5a03d699ac65007272c32ab0eded163'
            '1a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7'))
        self.assertEqualBin(hash_str('sha384',
            "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
            "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"), unhex(
            '09330c33f71147e83d192fc782cd1b4753111b173b3b05d2'
            '2fa08086e3b0f712fcc7c71a557e2db966c3e9fa91746039'))
        self.assertEqualBin(hash_str_iter('sha384',
            ("a" * 1000 for _ in range(1000))), unhex(
            '9d0e1809716474cb086e834e310a4a1ced149e9c00f24852'
            '7972cec5704c2a5b07b8b3dc38ecc4ebae97ddd87f3d8985'))
        self.assertEqualBin(hash_str('sha384',
            "01234567012345670123456701234567" * 20), unhex(
            '2fc64a4f500ddb6828f6a3430b8dd72a368eb7f3a8322a70'
            'bc84275b9c0b3ab00d27a5cc3c2d224aa6b61a0d79fb4596'))
        self.assertEqualBin(hash_str('sha384', b"\xB9"), unhex(
            'bc8089a19007c0b14195f4ecc74094fec64f01f90929282c'
            '2fb392881578208ad466828b1c6c283d2722cf0ad1ab6938'))
        self.assertEqualBin(hash_str('sha384',
            unhex("a41c497779c0375ff10a7f4e08591739")), unhex(
            'c9a68443a005812256b8ec76b00516f0dbb74fab26d66591'
            '3f194b6ffb0e91ea9967566b58109cbc675cc208e4c823f7'))
        self.assertEqualBin(hash_str('sha384', unhex(
            "399669e28f6b9c6dbcbb6912ec10ffcf74790349b7dc8fbe4a8e7b3b5621db0f"
            "3e7dc87f823264bbe40d1811c9ea2061e1c84ad10a23fac1727e7202fc3f5042"
            "e6bf58cba8a2746e1f64f9b9ea352c711507053cf4e5339d52865f25cc22b5e8"
            "7784a12fc961d66cb6e89573199a2ce6565cbdf13dca403832cfcb0e8b7211e8"
            "3af32a11ac17929ff1c073a51cc027aaedeff85aad7c2b7c5a803e2404d96d2a"
            "77357bda1a6daeed17151cb9bc5125a422e941de0ca0fc5011c23ecffefdd096"
            "76711cf3db0a3440720e1615c1f22fbc3c721de521e1b99ba1bd557740864214"
            "7ed096")), unhex(
            '4f440db1e6edd2899fa335f09515aa025ee177a79f4b4aaf'
            '38e42b5c4de660f5de8fb2a5b2fbd2a3cbffd20cff1288c0'))

    def testSHA512(self):
        # Test cases from RFC 6234 section 8.5, omitting the ones
        # whose input is not a multiple of 8 bits
        self.assertEqualBin(hash_str('sha512', "abc"), unhex(
            'ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a'
            '2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f'))
        self.assertEqualBin(hash_str('sha512',
            "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
            "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"), unhex(
            '8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018'
            '501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909'))
        self.assertEqualBin(hash_str_iter('sha512',
            ("a" * 1000 for _ in range(1000))), unhex(
            'e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973eb'
            'de0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b'))
        self.assertEqualBin(hash_str('sha512',
            "01234567012345670123456701234567" * 20), unhex(
            '89d05ba632c699c31231ded4ffc127d5a894dad412c0e024db872d1abd2ba814'
            '1a0f85072a9be1e2aa04cf33c765cb510813a39cd5a84c4acaa64d3f3fb7bae9'))
        self.assertEqualBin(hash_str('sha512', b"\xD0"), unhex(
            '9992202938e882e73e20f6b69e68a0a7149090423d93c81bab3f21678d4aceee'
            'e50e4e8cafada4c85a54ea8306826c4ad6e74cece9631bfa8a549b4ab3fbba15'))
        self.assertEqualBin(hash_str('sha512',
            unhex("8d4e3c0e3889191491816e9d98bff0a0")), unhex(
            'cb0b67a4b8712cd73c9aabc0b199e9269b20844afb75acbdd1c153c9828924c3'
            'ddedaafe669c5fdd0bc66f630f6773988213eb1b16f517ad0de4b2f0c95c90f8'))
        self.assertEqualBin(hash_str('sha512', unhex(
            "a55f20c411aad132807a502d65824e31a2305432aa3d06d3e282a8d84e0de1de"
            "6974bf495469fc7f338f8054d58c26c49360c3e87af56523acf6d89d03e56ff2"
            "f868002bc3e431edc44df2f0223d4bb3b243586e1a7d924936694fcbbaf88d95"
            "19e4eb50a644f8e4f95eb0ea95bc4465c8821aacd2fe15ab4981164bbb6dc32f"
            "969087a145b0d9cc9c67c22b763299419cc4128be9a077b3ace634064e6d9928"
            "3513dc06e7515d0d73132e9a0dc6d3b1f8b246f1a98a3fc72941b1e3bb2098e8"
            "bf16f268d64f0b0f4707fe1ea1a1791ba2f3c0c758e5f551863a96c949ad47d7"
            "fb40d2")), unhex(
            'c665befb36da189d78822d10528cbf3b12b3eef726039909c1a16a270d487193'
            '77966b957a878e720584779a62825c18da26415e49a7176a894e7510fd1451f5'))

    def testHmacSHA(self):
        # Test cases from RFC 6234 section 8.5.
        def vector(key, message, s1=None, s256=None):
            if s1 is not None:
                self.assertEqualBin(
                    mac_str('hmac_sha1', key, message), unhex(s1))
            if s256 is not None:
                self.assertEqualBin(
                    mac_str('hmac_sha256', key, message), unhex(s256))
        vector(
            unhex("0b"*20), "Hi There",
            "b617318655057264e28bc0b6fb378c8ef146be00",
            "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7")
        vector(
            "Jefe", "what do ya want for nothing?",
            "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79",
            "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843")
        vector(
            unhex("aa"*20), unhex('dd'*50),
            "125d7342b9ac11cd91a39af48aa17b4f63f175d3",
            "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565FE")
        vector(
            unhex("0102030405060708090a0b0c0d0e0f10111213141516171819"),
            unhex("cd"*50),
            "4c9007f4026250c6bc8414f9bf50c86c2d7235da",
            "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b")
        vector(
            unhex("aa"*80),
            "Test Using Larger Than Block-Size Key - Hash Key First",
            s1="aa4ae5e15272d00e95705637ce8a3b55ed402112")
        vector(
            unhex("aa"*131),
            "Test Using Larger Than Block-Size Key - Hash Key First",
            s256="60e431591ee0b67f0d8a26aacbf5b77f"
            "8e0bc6213728c5140546040f0ee37f54")
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
            s256="9B09FFA71B942FCB27635FBCD5B0E944BFDC63644F0713938A7F51535C3A35E2")

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

    def testMontgomeryKex(self):
        # Unidirectional tests, consisting of an input random number
        # string and peer public value, giving the expected output
        # shared key. Source: RFC 7748 section 5.2.
        rfc7748s5_2 = [
            ('a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4',
             'e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c',
             0xc3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552),
            ('4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d',
             'e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493',
             0x95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957),
        ]

        for priv, pub, expected in rfc7748s5_2:
            with queued_specific_random_data(unhex(priv)):
                ecdh = ssh_ecdhkex_newkey('curve25519')
            key = ssh_ecdhkex_getkey(ecdh, unhex(pub))
            self.assertEqual(int(key), expected)

        # Bidirectional tests, consisting of the input random number
        # strings for both parties, and the expected public values and
        # shared key. Source: RFC 7748 section 6.1.
        rfc7748s6_1 = [
            ('77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a',
             '8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a',
             '5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb',
             'de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f',
             0x4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742),
        ]

        for apriv, apub, bpriv, bpub, expected in rfc7748s6_1:
            with queued_specific_random_data(unhex(apriv)):
                alice = ssh_ecdhkex_newkey('curve25519')
            with queued_specific_random_data(unhex(bpriv)):
                bob = ssh_ecdhkex_newkey('curve25519')
            self.assertEqualBin(ssh_ecdhkex_getpublic(alice), unhex(apub))
            self.assertEqualBin(ssh_ecdhkex_getpublic(bob), unhex(bpub))
            akey = ssh_ecdhkex_getkey(alice, unhex(bpub))
            bkey = ssh_ecdhkex_getkey(bob, unhex(apub))
            self.assertEqual(int(akey), expected)
            self.assertEqual(int(bkey), expected)

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
