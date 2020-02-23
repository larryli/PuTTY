/*
 * Prime generation.
 */

#include <assert.h>
#include "ssh.h"
#include "mpint.h"
#include "mpunsafe.h"
#include "sshkeygen.h"

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
mp_int *primegen(
    int bits, int modulus, int residue, mp_int *factor,
    int phase, progfn_t pfn, void *pfnparam, unsigned firstbits)
{
    int progress = 0;

    size_t fbsize = 0;
    while (firstbits >> fbsize)        /* work out how to align this */
        fbsize++;

    PrimeCandidateSource *pcs = pcs_new(bits, firstbits, fbsize);
    if (factor)
        pcs_require_residue_1(pcs, factor);
    if (modulus)
        pcs_avoid_residue_small(pcs, modulus, residue);
    pcs_ready(pcs);

  STARTOVER:

    pfn(pfnparam, PROGFN_PROGRESS, phase, ++progress);

    mp_int *p = pcs_generate(pcs);

    /*
     * Now apply the Miller-Rabin primality test a few times. First
     * work out how many checks are needed.
     */
    unsigned checks =
        bits >= 1300 ?  2 : bits >= 850 ?  3 : bits >= 650 ?  4 :
        bits >=  550 ?  5 : bits >= 450 ?  6 : bits >= 400 ?  7 :
        bits >=  350 ?  8 : bits >= 300 ?  9 : bits >= 250 ? 12 :
        bits >=  200 ? 15 : bits >= 150 ? 18 : 27;

    /*
     * Next, write p-1 as q*2^k.
     */
    size_t k;
    for (k = 0; mp_get_bit(p, k) == !k; k++)
        continue;       /* find first 1 bit in p-1 */
    mp_int *q = mp_rshift_safe(p, k);

    /*
     * Set up stuff for the Miller-Rabin checks.
     */
    mp_int *two = mp_from_integer(2);
    mp_int *pm1 = mp_copy(p);
    mp_sub_integer_into(pm1, pm1, 1);
    MontyContext *mc = monty_new(p);
    mp_int *m_pm1 = monty_import(mc, pm1);

    bool known_bad = false;

    /*
     * Now, for each check ...
     */
    for (unsigned check = 0; check < checks && !known_bad; check++) {
        /*
         * Invent a random number between 1 and p-1.
         */
        mp_int *w = mp_random_in_range(two, pm1);
        monty_import_into(mc, w, w);

        pfn(pfnparam, PROGFN_PROGRESS, phase, ++progress);

        /*
         * Compute w^q mod p.
         */
        mp_int *wqp = monty_pow(mc, w, q);
        mp_free(w);

        /*
         * See if this is 1, or if it is -1, or if it becomes -1
         * when squared at most k-1 times.
         */
        bool passed = false;

        if (mp_cmp_eq(wqp, monty_identity(mc)) || mp_cmp_eq(wqp, m_pm1)) {
            passed = true;
        } else {
            for (size_t i = 0; i < k - 1; i++) {
                monty_mul_into(mc, wqp, wqp, wqp);
                if (mp_cmp_eq(wqp, m_pm1)) {
                    passed = true;
                    break;
                }
            }
        }

        if (!passed)
            known_bad = true;

        mp_free(wqp);
    }

    mp_free(q);
    mp_free(two);
    mp_free(pm1);
    monty_free(mc);
    mp_free(m_pm1);

    if (known_bad) {
        mp_free(p);
        goto STARTOVER;
    }

    /*
     * We have a prime!
     */
    return p;
}
