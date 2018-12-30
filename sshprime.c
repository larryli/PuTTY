/*
 * Prime generation.
 */

#include <assert.h>
#include "ssh.h"

/*
 * This prime generation algorithm is pretty much cribbed from
 * OpenSSL. The algorithm is:
 * 
 *  - invent a B-bit random number and ensure the top and bottom
 *    bits are set (so it's definitely B-bit, and it's definitely
 *    odd)
 * 
 *  - see if it's coprime to all primes below 2^16; increment it by
 *    two until it is (this shouldn't take long in general)
 * 
 *  - perform the Miller-Rabin primality test enough times to
 *    ensure the probability of it being composite is 2^-80 or
 *    less
 * 
 *  - go back to square one if any M-R test fails.
 */

/*
 * The Miller-Rabin primality test is an extension to the Fermat
 * test. The Fermat test just checks that a^(p-1) == 1 mod p; this
 * is vulnerable to Carmichael numbers. Miller-Rabin considers how
 * that 1 is derived as well.
 * 
 * Lemma: if a^2 == 1 (mod p), and p is prime, then either a == 1
 * or a == -1 (mod p).
 * 
 *   Proof: p divides a^2-1, i.e. p divides (a+1)(a-1). Hence,
 *   since p is prime, either p divides (a+1) or p divides (a-1).
 *   But this is the same as saying that either a is congruent to
 *   -1 mod p or a is congruent to +1 mod p. []
 * 
 *   Comment: This fails when p is not prime. Consider p=mn, so
 *   that mn divides (a+1)(a-1). Now we could have m dividing (a+1)
 *   and n dividing (a-1), without the whole of mn dividing either.
 *   For example, consider a=10 and p=99. 99 = 9 * 11; 9 divides
 *   10-1 and 11 divides 10+1, so a^2 is congruent to 1 mod p
 *   without a having to be congruent to either 1 or -1.
 * 
 * So the Miller-Rabin test, as well as considering a^(p-1),
 * considers a^((p-1)/2), a^((p-1)/4), and so on as far as it can
 * go. In other words. we write p-1 as q * 2^k, with k as large as
 * possible (i.e. q must be odd), and we consider the powers
 * 
 *       a^(q*2^0)      a^(q*2^1)          ...  a^(q*2^(k-1))  a^(q*2^k)
 * i.e.  a^((n-1)/2^k)  a^((n-1)/2^(k-1))  ...  a^((n-1)/2)    a^(n-1)
 * 
 * If p is to be prime, the last of these must be 1. Therefore, by
 * the above lemma, the one before it must be either 1 or -1. And
 * _if_ it's 1, then the one before that must be either 1 or -1,
 * and so on ... In other words, we expect to see a trailing chain
 * of 1s preceded by a -1. (If we're unlucky, our trailing chain of
 * 1s will be as long as the list so we'll never get to see what
 * lies before it. This doesn't count as a test failure because it
 * hasn't _proved_ that p is not prime.)
 * 
 * For example, consider a=2 and p=1729. 1729 is a Carmichael
 * number: although it's not prime, it satisfies a^(p-1) == 1 mod p
 * for any a coprime to it. So the Fermat test wouldn't have a
 * problem with it at all, unless we happened to stumble on an a
 * which had a common factor.
 * 
 * So. 1729 - 1 equals 27 * 2^6. So we look at
 * 
 *     2^27 mod 1729 == 645
 *    2^108 mod 1729 == 1065
 *    2^216 mod 1729 == 1
 *    2^432 mod 1729 == 1
 *    2^864 mod 1729 == 1
 *   2^1728 mod 1729 == 1
 * 
 * We do have a trailing string of 1s, so the Fermat test would
 * have been happy. But this trailing string of 1s is preceded by
 * 1065; whereas if 1729 were prime, we'd expect to see it preceded
 * by -1 (i.e. 1728.). Guards! Seize this impostor.
 * 
 * (If we were unlucky, we might have tried a=16 instead of a=2;
 * now 16^27 mod 1729 == 1, so we would have seen a long string of
 * 1s and wouldn't have seen the thing _before_ the 1s. So, just
 * like the Fermat test, for a given p there may well exist values
 * of a which fail to show up its compositeness. So we try several,
 * just like the Fermat test. The difference is that Miller-Rabin
 * is not _in general_ fooled by Carmichael numbers.)
 * 
 * Put simply, then, the Miller-Rabin test requires us to:
 * 
 *  1. write p-1 as q * 2^k, with q odd
 *  2. compute z = (a^q) mod p.
 *  3. report success if z == 1 or z == -1.
 *  4. square z at most k-1 times, and report success if it becomes
 *     -1 at any point.
 *  5. report failure otherwise.
 * 
 * (We expect z to become -1 after at most k-1 squarings, because
 * if it became -1 after k squarings then a^(p-1) would fail to be
 * 1. And we don't need to investigate what happens after we see a
 * -1, because we _know_ that -1 squared is 1 modulo anything at
 * all, so after we've seen a -1 we can be sure of seeing nothing
 * but 1s.)
 */

static unsigned short primes[6542]; /* # primes < 65536 */
#define NPRIMES (lenof(primes))

static void init_primes_array(void)
{
    if (primes[0])
        return;                        /* already done */

    bool A[65536];

    for (size_t i = 2; i < lenof(A); i++)
        A[i] = true;

    for (size_t i = 2; i < lenof(A); i++) {
        if (!A[i])
            continue;
        for (size_t j = 2*i; j < lenof(A); j += i)
            A[j] = false;
    }

    size_t pos = 0;
    for (size_t i = 2; i < lenof(A); i++)
        if (A[i])
            primes[pos++] = i;

    assert(pos == NPRIMES);
}

/*
 * Generate a prime. We can deal with various extra properties of
 * the prime:
 * 
 *  - to speed up use in RSA, we can arrange to select a prime with
 *    the property (prime % modulus) != residue.
 * 
 *  - for use in DSA, we can arrange to select a prime which is one
 *    more than a multiple of a dirty great bignum. In this case
 *    `bits' gives the size of the factor by which we _multiply_
 *    that bignum, rather than the size of the whole number.
 *
 *  - for the basically cosmetic purposes of generating keys of the
 *    length actually specified rather than off by one bit, we permit
 *    the caller to provide an unsigned integer 'firstbits' which will
 *    match the top few bits of the returned prime. (That is, there
 *    will exist some n such that (returnvalue >> n) == firstbits.) If
 *    'firstbits' is not needed, specifying it to either 0 or 1 is
 *    an adequate no-op.
 */
Bignum primegen(int bits, int modulus, int residue, Bignum factor,
		int phase, progfn_t pfn, void *pfnparam, unsigned firstbits)
{
    int i, k, v, byte, bitsleft, check, checks, fbsize;
    unsigned long delta;
    unsigned long moduli[NPRIMES + 1];
    unsigned long residues[NPRIMES + 1];
    unsigned long multipliers[NPRIMES + 1];
    Bignum p, pm1, q, wqp, wqp2;
    int progress = 0;

    init_primes_array();

    byte = 0;
    bitsleft = 0;

    fbsize = 0;
    while (firstbits >> fbsize)        /* work out how to align this */
        fbsize++;

  STARTOVER:

    pfn(pfnparam, PROGFN_PROGRESS, phase, ++progress);

    /*
     * Generate a k-bit random number with top and bottom bits set.
     * Alternatively, if `factor' is nonzero, generate a k-bit
     * random number with the top bit set and the bottom bit clear,
     * multiply it by `factor', and add one.
     */
    p = bn_power_2(bits - 1);
    for (i = 0; i < bits; i++) {
	if (i == 0 || i == bits - 1) {
	    v = (i != 0 || !factor) ? 1 : 0;
        } else if (i >= bits - fbsize) {
            v = (firstbits >> (i - (bits - fbsize))) & 1;
        } else {
	    if (bitsleft <= 0)
		bitsleft = 8, byte = random_byte();
	    v = byte & 1;
	    byte >>= 1;
	    bitsleft--;
	}
	bignum_set_bit(p, i, v);
    }
    if (factor) {
	Bignum tmp = p;
	p = bigmul(tmp, factor);
	freebn(tmp);
	assert(bignum_bit(p, 0) == 0);
	bignum_set_bit(p, 0, 1);
    }

    /*
     * Ensure this random number is coprime to the first few
     * primes, by repeatedly adding either 2 or 2*factor to it
     * until it is.
     */
    for (i = 0; i < NPRIMES; i++) {
	moduli[i] = primes[i];
	residues[i] = bignum_mod_short(p, primes[i]);
	if (factor)
	    multipliers[i] = bignum_mod_short(factor, primes[i]);
	else
	    multipliers[i] = 1;
    }
    moduli[NPRIMES] = modulus;
    residues[NPRIMES] = (bignum_mod_short(p, (unsigned short) modulus)
			 + modulus - residue);
    if (factor)
	multipliers[NPRIMES] = bignum_mod_short(factor, modulus);
    else
	multipliers[NPRIMES] = 1;
    delta = 0;
    while (1) {
	for (i = 0; i < (sizeof(moduli) / sizeof(*moduli)); i++)
	    if (!((residues[i] + delta * multipliers[i]) % moduli[i]))
		break;
	if (i < (sizeof(moduli) / sizeof(*moduli))) {	/* we broke */
	    delta += 2;
	    if (delta > 65536) {
		freebn(p);
		goto STARTOVER;
	    }
	    continue;
	}
	break;
    }
    q = p;
    if (factor) {
	Bignum tmp;
	tmp = bignum_from_long(delta);
	p = bigmuladd(tmp, factor, q);
	freebn(tmp);
    } else {
	p = bignum_add_long(q, delta);
    }
    freebn(q);

    /*
     * Now apply the Miller-Rabin primality test a few times. First
     * work out how many checks are needed.
     */
    checks = 27;
    if (bits >= 150)
	checks = 18;
    if (bits >= 200)
	checks = 15;
    if (bits >= 250)
	checks = 12;
    if (bits >= 300)
	checks = 9;
    if (bits >= 350)
	checks = 8;
    if (bits >= 400)
	checks = 7;
    if (bits >= 450)
	checks = 6;
    if (bits >= 550)
	checks = 5;
    if (bits >= 650)
	checks = 4;
    if (bits >= 850)
	checks = 3;
    if (bits >= 1300)
	checks = 2;

    /*
     * Next, write p-1 as q*2^k.
     */
    for (k = 0; bignum_bit(p, k) == !k; k++)
	continue;	/* find first 1 bit in p-1 */
    q = bignum_rshift(p, k);
    /* And store p-1 itself, which we'll need. */
    pm1 = copybn(p);
    decbn(pm1);

    /*
     * Now, for each check ...
     */
    for (check = 0; check < checks; check++) {
	Bignum w;

	/*
	 * Invent a random number between 1 and p-1 inclusive.
	 */
	while (1) {
	    w = bn_power_2(bits - 1);
	    for (i = 0; i < bits; i++) {
		if (bitsleft <= 0)
		    bitsleft = 8, byte = random_byte();
		v = byte & 1;
		byte >>= 1;
		bitsleft--;
		bignum_set_bit(w, i, v);
	    }
	    bn_restore_invariant(w);
	    if (bignum_cmp(w, p) >= 0 || bignum_cmp(w, Zero) == 0) {
		freebn(w);
		continue;
	    }
	    break;
	}

	pfn(pfnparam, PROGFN_PROGRESS, phase, ++progress);

	/*
	 * Compute w^q mod p.
	 */
	wqp = modpow(w, q, p);
	freebn(w);

	/*
	 * See if this is 1, or if it is -1, or if it becomes -1
	 * when squared at most k-1 times.
	 */
	if (bignum_cmp(wqp, One) == 0 || bignum_cmp(wqp, pm1) == 0) {
	    freebn(wqp);
	    continue;
	}
	for (i = 0; i < k - 1; i++) {
	    wqp2 = modmul(wqp, wqp, p);
	    freebn(wqp);
	    wqp = wqp2;
	    if (bignum_cmp(wqp, pm1) == 0)
		break;
	}
	if (i < k - 1) {
	    freebn(wqp);
	    continue;
	}

	/*
	 * It didn't. Therefore, w is a witness for the
	 * compositeness of p.
	 */
	freebn(wqp);
	freebn(p);
	freebn(pm1);
	freebn(q);
	goto STARTOVER;
    }

    /*
     * We have a prime!
     */
    freebn(q);
    freebn(pm1);
    return p;
}

/*
 * Invent a pair of values suitable for use as 'firstbits' in the
 * above function, such that their product is at least 2.
 *
 * This is used for generating both RSA and DSA keys which have
 * exactly the specified number of bits rather than one fewer - if you
 * generate an a-bit and a b-bit number completely at random and
 * multiply them together, you could end up with either an (ab-1)-bit
 * number or an (ab)-bit number. The former happens log(2)*2-1 of the
 * time (about 39%) and, though actually harmless, every time it
 * occurs it has a non-zero probability of sparking a user email along
 * the lines of 'Hey, I asked PuTTYgen for a 2048-bit key and I only
 * got 2047 bits! Bug!'
 */
void invent_firstbits(unsigned *one, unsigned *two)
{
    /*
     * Our criterion is that any number in the range [one,one+1)
     * multiplied by any number in the range [two,two+1) should have
     * the highest bit set. It should be clear that we can trivially
     * test this by multiplying the smallest values in each interval,
     * i.e. the ones we actually invented.
     */
    do {
        *one = 0x100 | random_byte();
        *two = 0x100 | random_byte();
    } while (*one * *two < 0x20000);
}
