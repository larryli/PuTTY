#!/usr/bin/env python

import unittest
import struct
import itertools
import contextlib
import hashlib
try:
    from math import gcd
except ImportError:
    from fractions import gcd

from eccref import *
from testcrypt import *

def nbits(n):
    # Mimic mp_get_nbits for ordinary Python integers.
    assert 0 <= n
    smax = next(s for s in itertools.count() if (n >> (1 << s)) == 0)
    toret = 0
    for shift in reversed([1 << s for s in range(smax)]):
        if n >> shift != 0:
            n >>= shift
            toret += shift
    assert n <= 1
    if n == 1:
        toret += 1
    return toret

def unhex(s):
    return s.replace(" ", "").replace("\n", "").decode("hex")

def ssh_uint32(n):
    return struct.pack(">L", n)
def ssh_string(s):
    return ssh_uint32(len(s)) + s
def ssh1_mpint(x):
    bits = nbits(x)
    bytevals = [0xFF & (x >> (8*n)) for n in range((bits-1)//8, -1, -1)]
    return struct.pack(">H" + "B" * len(bytevals), bits, *bytevals)
def ssh2_mpint(x):
    bytevals = [0xFF & (x >> (8*n)) for n in range(nbits(x)//8, -1, -1)]
    return struct.pack(">L" + "B" * len(bytevals), len(bytevals), *bytevals)

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
        self.assertEqual(x.encode('hex'), y.encode('hex'))

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
        # For hex, test both upper and lower case digits
        hexstr = 'ea7cb89f409ae845215822e37D32D0C63EC43E1381C2FF8094'
        self.assertEqual(int(mp_from_hex_pl(hexstr)), int(hexstr, 16))
        self.assertEqual(int(mp_from_hex(hexstr)), int(hexstr, 16))
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

                # mp_min{,_into} is a reasonable thing to test here as well
                self.assertEqual(int(mp_min(am, bm)), min(ai, bi))
                am2 = mp_copy(am)
                mp_min_into(am2, am, bm)
                self.assertEqual(int(am2), min(ai, bi))

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

                for bits in range(0, 512, 64):
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
                    mq = mp_new(nbits(q))
                    mr = mp_new(nbits(r))
                    mp_divmod_into(n, d, mq, mr)
                    self.assertEqual(int(mq), q)
                    self.assertEqual(int(mr), r)
                    self.assertEqual(int(mp_div(n, d)), q)
                    self.assertEqual(int(mp_mod(n, d)), r)

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
            c = ssh2_cipher_new(cipher)
            ssh2_cipher_setkey(c, key)
            ssh2_cipher_setiv(c, iv)
            self.assertEqualBin(ssh2_cipher_encrypt(c, plaintext), ciphertext)
            ssh2_cipher_setiv(c, iv)
            self.assertEqualBin(ssh2_cipher_decrypt(c, ciphertext), plaintext)

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

        vector('aes128', key[:16], iv, plaintext, unhex('''
        547ee90514cb6406d5bb00855c8092892c58299646edda0b4e7c044247795c8d
        3c3eb3d91332e401215d4d528b94a691969d27b7890d1ae42fe3421b91c989d5
        113fefa908921a573526259c6b4f8e4d90ea888e1d8b7747457ba3a43b5b79b9
        34873ebf21102d14b51836709ee85ed590b7ca618a1e884f5c57c8ea73fe3d0d
        6bf8c082dd602732bde28131159ed0b6e9cf67c353ffdd010a5a634815aaa963'''))

        vector('aes192', key[:24], iv, plaintext, unhex('''
        e3dee5122edd3fec5fab95e7db8c784c0cb617103e2a406fba4ae3b4508dd608
        4ff5723a670316cc91ed86e413c11b35557c56a6f5a7a2c660fc6ee603d73814
        73a287645be0f297cdda97aef6c51faeb2392fec9d33adb65138d60f954babd9
        8ee0daab0d1decaa8d1e07007c4a3c7b726948025f9fb72dd7de41f74f2f36b4
        23ac6a5b4b6b39682ec74f57d9d300e547f3c3e467b77f5e4009923b2f94c903'''))

        vector('aes256', key[:32], iv, plaintext, unhex('''
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

        def increment(keylen, iv):
            key = b'\xab' * (keylen//8)
            sdctr = ssh2_cipher_new("aes{}_ctr".format(keylen))
            ssh2_cipher_setkey(sdctr, key)
            cbc = ssh2_cipher_new("aes{}".format(keylen))
            ssh2_cipher_setkey(cbc, key)

            ssh2_cipher_setiv(sdctr, iv)
            ec0 = ssh2_cipher_encrypt(sdctr, b'\x00' * 16)
            ec1 = ssh2_cipher_encrypt(sdctr, b'\x00' * 16)
            ssh2_cipher_setiv(cbc, b'\x00' * 16)
            dc0 = ssh2_cipher_decrypt(cbc, ec0)
            ssh2_cipher_setiv(cbc, b'\x00' * 16)
            dc1 = ssh2_cipher_decrypt(cbc, ec1)
            self.assertEqualBin(iv, dc0)
            return dc1

        def test(keylen, ivInteger):
            mask = (1 << 128) - 1
            ivInteger &= mask
            ivBinary = unhex("{:032x}".format(ivInteger))
            ivIntegerInc = (ivInteger + 1) & mask
            ivBinaryInc = unhex("{:032x}".format((ivIntegerInc)))
            actualResult = increment(keylen, ivBinary)
            self.assertEqualBin(actualResult, ivBinaryInc)

        # Check every input IV you can make by gluing together 32-bit
        # pieces of the form 0, 1 or -1. This should test all the
        # places where carry propagation within the 128-bit integer
        # can go wrong.
        #
        # We also test this at all three AES key lengths, in case the
        # core cipher routines are written separately for each one.

        for keylen in [128, 192, 256]:
            hexTestValues = ["00000000", "00000001", "ffffffff"]
            for ivHexBytes in itertools.product(*([hexTestValues] * 4)):
                ivInteger = int("".join(ivHexBytes), 16)
                test(keylen, ivInteger)

class standard_test_vectors(MyTestBase):
    def testAES(self):
        def vector(cipher, key, plaintext, ciphertext):
            c = ssh2_cipher_new(cipher)
            ssh2_cipher_setkey(c, key)

            # The AES test vectors are implicitly in ECB mode, because
            # they're testing the cipher primitive rather than any
            # mode layered on top of it. We fake this by using PuTTY's
            # CBC setting, and clearing the IV to all zeroes before
            # each operation.

            ssh2_cipher_setiv(c, b'\x00' * 16)
            self.assertEqualBin(ssh2_cipher_encrypt(c, plaintext), ciphertext)

            ssh2_cipher_setiv(c, b'\x00' * 16)
            self.assertEqualBin(ssh2_cipher_decrypt(c, ciphertext), plaintext)

        # The test vectors from FIPS 197 appendix C: the key bytes go
        # 00 01 02 03 ... for as long as needed, and the plaintext
        # bytes go 00 11 22 33 ... FF.
        fullkey = struct.pack("B"*32, *range(32))
        plaintext = struct.pack("B"*16, *[0x11*i for i in range(16)])
        vector('aes128', fullkey[:16], plaintext,
               unhex('69c4e0d86a7b0430d8cdb78070b4c55a'))
        vector('aes192', fullkey[:24], plaintext,
               unhex('dda97ca4864cdfe06eaf70a0ec0d7191'))
        vector('aes256', fullkey[:32], plaintext,
               unhex('8ea2b7ca516745bfeafc49904b496089'))

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
        # Test cases from RFC 6234 section 8.5, omitting the ones
        # whose input is not a multiple of 8 bits
        self.assertEqualBin(hash_str('sha1', "abc"), unhex(
            "a9993e364706816aba3e25717850c26c9cd0d89d"))
        self.assertEqualBin(hash_str('sha1',
            "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"), unhex(
            "84983e441c3bd26ebaae4aa1f95129e5e54670f1"))
        self.assertEqualBin(hash_str_iter('sha1',
            ("a" * 1000 for _ in range(1000))), unhex(
            "34aa973cd4c4daa4f61eeb2bdbad27316534016f"))
        self.assertEqualBin(hash_str('sha1',
            "01234567012345670123456701234567" * 20), unhex(
            "dea356a2cddd90c7a7ecedc5ebb563934f460452"))
        self.assertEqualBin(hash_str('sha1', b"\x5e"), unhex(
            "5e6f80a34a9798cafc6a5db96cc57ba4c4db59c2"))
        self.assertEqualBin(hash_str('sha1',
            unhex("9a7dfdf1ecead06ed646aa55fe757146")), unhex(
            "82abff6605dbe1c17def12a394fa22a82b544a35"))
        self.assertEqualBin(hash_str('sha1', unhex(
            "f78f92141bcd170ae89b4fba15a1d59f3fd84d223c9251bdacbbae61d05ed115"
            "a06a7ce117b7beead24421ded9c32592bd57edeae39c39fa1fe8946a84d0cf1f"
            "7beead1713e2e0959897347f67c80b0400c209815d6b10a683836fd5562a56ca"
            "b1a28e81b6576654631cf16566b86e3b33a108b05307c00aff14a768ed735060"
            "6a0f85e6a91d396f5b5cbe577f9b38807c7d523d6d792f6ebc24a4ecf2b3a427"
            "cdbbfb")), unhex(
            "cb0082c8f197d260991ba6a460e76e202bad27b3"))

    def testSHA256(self):
        # Test cases from RFC 6234 section 8.5, omitting the ones
        # whose input is not a multiple of 8 bits
        self.assertEqualBin(hash_str('sha256', "abc"), unhex(
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))
        self.assertEqualBin(hash_str('sha256',
            "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"), unhex(
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"))
        self.assertEqualBin(hash_str_iter('sha256',
            ("a" * 1000 for _ in range(1000))), unhex(
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"))
        self.assertEqualBin(hash_str('sha256',
            "01234567012345670123456701234567" * 20), unhex(
            "594847328451bdfa85056225462cc1d867d877fb388df0ce35f25ab5562bfbb5"))
        self.assertEqualBin(hash_str('sha256', b"\x19"), unhex(
            "68aa2e2ee5dff96e3355e6c7ee373e3d6a4e17f75f9518d843709c0c9bc3e3d4"))
        self.assertEqualBin(hash_str('sha256',
            unhex("e3d72570dcdd787ce3887ab2cd684652")), unhex(
            "175ee69b02ba9b58e2b0a5fd13819cea573f3940a94f825128cf4209beabb4e8"))
        self.assertEqualBin(hash_str('sha256', unhex(
            "8326754e2277372f4fc12b20527afef04d8a056971b11ad57123a7c137760000"
            "d7bef6f3c1f7a9083aa39d810db310777dab8b1e7f02b84a26c773325f8b2374"
            "de7a4b5a58cb5c5cf35bcee6fb946e5bd694fa593a8beb3f9d6592ecedaa66ca"
            "82a29d0c51bcf9336230e5d784e4c0a43f8d79a30a165cbabe452b774b9c7109"
            "a97d138f129228966f6c0adc106aad5a9fdd30825769b2c671af6759df28eb39"
            "3d54d6")), unhex(
            "97dbca7df46d62c8a422c941dd7e835b8ad3361763f7e9b2d95f4f0da6e1ccbc"))

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
        # Test cases from RFC 6234 section 8.5, omitting the ones
        # which have a long enough key to require hashing it first.
        # (Our implementation doesn't support that, because it knows
        # it only has to deal with a fixed key length.)
        def vector(key, message, s1, s256):
            self.assertEqualBin(
                mac_str('hmac_sha1', key, message), unhex(s1))
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

if __name__ == "__main__":
    try:
        unittest.main()
    finally:
        # On exit, make sure we check the subprocess's return status,
        # so that if Leak Sanitiser detected any memory leaks, the
        # test will turn into a failure at the last minute.
        childprocess.check_return_status()
