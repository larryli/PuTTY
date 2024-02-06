/*
 * millerrabin.c: Miller-Rabin probabilistic primality testing, as
 * declared in sshkeygen.h.
 */

#include <assert.h>
#include "ssh.h"
#include "sshkeygen.h"
#include "mpint.h"
#include "mpunsafe.h"

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

struct MillerRabin {
    MontyContext *mc;

    mp_int *pm1, *m_pm1;
    mp_int *lowbit, *two;
};

MillerRabin *miller_rabin_new(mp_int *p)
{
    MillerRabin *mr = snew(MillerRabin);

    assert(mp_hs_integer(p, 2));
    assert(mp_get_bit(p, 0) == 1);

    mr->pm1 = mp_copy(p);
    mp_sub_integer_into(mr->pm1, mr->pm1, 1);

    /*
     * Standard bit-twiddling trick for isolating the lowest set bit
     * of a number: x & (-x)
     */
    mr->lowbit = mp_new(mp_max_bits(mr->pm1));
    mp_sub_into(mr->lowbit, mr->lowbit, mr->pm1);
    mp_and_into(mr->lowbit, mr->lowbit, mr->pm1);

    mr->two = mp_from_integer(2);

    mr->mc = monty_new(p);
    mr->m_pm1 = monty_import(mr->mc, mr->pm1);

    return mr;
}

void miller_rabin_free(MillerRabin *mr)
{
    mp_free(mr->pm1);
    mp_free(mr->m_pm1);
    mp_free(mr->lowbit);
    mp_free(mr->two);
    monty_free(mr->mc);
    smemclr(mr, sizeof(*mr));
    sfree(mr);
}

/*
 * The main internal function that implements a single M-R test.
 *
 * Expects the witness integer to be in Montgomery representation.
 * (Since in live use witnesses are invented at random, this imposes
 * no extra cost on the callers, and saves effort in here.)
 */
static struct mr_result miller_rabin_test_inner(MillerRabin *mr, mp_int *mw)
{
    mp_int *acc = mp_copy(monty_identity(mr->mc));
    mp_int *spare = mp_new(mp_max_bits(mr->pm1));
    size_t bit = mp_max_bits(mr->pm1);

    /*
     * The obvious approach to Miller-Rabin would be to start by
     * calling monty_pow to raise w to the power q, and then square it
     * k times ourselves. But that introduces a timing leak that gives
     * away the value of k, i.e., how many factors of 2 there are in
     * p-1.
     *
     * Instead, we don't call monty_pow at all. We do a modular
     * exponentiation ourselves to compute w^((p-1)/2), using the
     * technique that works from the top bit of the exponent
     * downwards. That is, in each iteration we compute
     * w^floor(exponent/2^i) for i one less than the previous
     * iteration, by squaring the value we previously had and then
     * optionally multiplying in w if the next exponent bit is 1.
     *
     * At the end of that process, once i <= k, the division
     * (exponent/2^i) yields an integer, so the values we're computing
     * are not just w^(floor of that), but w^(exactly that). In other
     * words, the last k intermediate values of this modexp are
     * precisely the values M-R wants to check against +1 or -1.
     *
     * So we interleave those checks with the modexp loop itself, and
     * to avoid a timing leak, we check _every_ intermediate result
     * against (the Montgomery representations of) both +1 and -1. And
     * then we do bitwise masking to arrange that only the sensible
     * ones of those checks find their way into our final answer.
     */

    unsigned active = 0;

    struct mr_result result;
    result.passed = result.potential_primitive_root = 0;

    while (bit-- > 1) {
        /*
         * In this iteration, we're computing w^(2e) or w^(2e+1),
         * where we have w^e from the previous iteration. So we square
         * the value we had already, and then optionally multiply in
         * another copy of w depending on the next bit of the exponent.
         */
        monty_mul_into(mr->mc, acc, acc, acc);
        monty_mul_into(mr->mc, spare, acc, mw);
        mp_select_into(acc, acc, spare, mp_get_bit(mr->pm1, bit));

        /*
         * mr->lowbit is a number with only one bit set, corresponding
         * to the lowest set bit in p-1. So when that's the bit of the
         * exponent we've just processed, we'll detect it by setting
         * first_iter to true. That's our indication that we're now
         * generating intermediate results useful to M-R, so we also
         * set 'active', which stays set from then on.
         */
        unsigned first_iter = mp_get_bit(mr->lowbit, bit);
        active |= first_iter;

        /*
         * Check the intermediate result against both +1 and -1.
         */
        unsigned is_plus_1 = mp_cmp_eq(acc, monty_identity(mr->mc));
        unsigned is_minus_1 = mp_cmp_eq(acc, mr->m_pm1);

        /*
         * M-R must report success iff either: the first of the useful
         * intermediate results (which is w^q) is 1, or _any_ of them
         * (from w^q all the way up to w^((p-1)/2)) is -1.
         *
         * So we want to pass the test if is_plus_1 is set on the
         * first iteration, or if is_minus_1 is set on any iteration.
         */
        result.passed |= (first_iter & is_plus_1);
        result.passed |= (active & is_minus_1);

        /*
         * In the final iteration, is_minus_1 is also used to set the
         * 'potential primitive root' flag, because we haven't found
         * any exponent smaller than p-1 for which w^(that) == 1.
         */
        if (bit == 1)
            result.potential_primitive_root = is_minus_1;
    }

    mp_free(acc);
    mp_free(spare);

    return result;
}

/*
 * Wrapper on miller_rabin_test_inner for the convenience of
 * testcrypt. Expects the witness integer to be literal, so we
 * monty_import it before running the real test.
 */
struct mr_result miller_rabin_test(MillerRabin *mr, mp_int *w)
{
    mp_int *mw = monty_import(mr->mc, w);
    struct mr_result result = miller_rabin_test_inner(mr, mw);
    mp_free(mw);
    return result;
}

bool miller_rabin_test_random(MillerRabin *mr)
{
    mp_int *mw = mp_random_in_range(mr->two, mr->pm1);
    struct mr_result result = miller_rabin_test_inner(mr, mw);
    mp_free(mw);
    return result.passed;
}

mp_int *miller_rabin_find_potential_primitive_root(MillerRabin *mr)
{
    while (true) {
        mp_int *mw = mp_unsafe_shrink(mp_random_in_range(mr->two, mr->pm1));
        struct mr_result result = miller_rabin_test_inner(mr, mw);

        if (result.passed && result.potential_primitive_root) {
            mp_int *pr = monty_export(mr->mc, mw);
            mp_free(mw);
            return pr;
        }

        mp_free(mw);

        if (!result.passed) {
            return NULL;
        }
    }
}

unsigned miller_rabin_checks_needed(unsigned bits)
{
    /* Table 4.4 from Handbook of Applied Cryptography */
    return (bits >= 1300 ?  2 : bits >= 850 ?  3 : bits >= 650 ?  4 :
            bits >=  550 ?  5 : bits >= 450 ?  6 : bits >= 400 ?  7 :
            bits >=  350 ?  8 : bits >= 300 ?  9 : bits >= 250 ? 12 :
            bits >=  200 ? 15 : bits >= 150 ? 18 : 27);
}

