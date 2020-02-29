/*
 * RSA key generation.
 */

#include <assert.h>

#include "ssh.h"
#include "sshkeygen.h"
#include "mpint.h"

#define RSA_EXPONENT 65537

#define NFIRSTBITS 13
static void invent_firstbits(unsigned *one, unsigned *two,
                             unsigned min_separation);

int rsa_generate(RSAKey *key, int bits, PrimeGenerationContext *pgc,
                 ProgressReceiver *prog)
{
    key->sshk.vt = &ssh_rsa;

    /*
     * We don't generate e; we just use a standard one always.
     */
    mp_int *exponent = mp_from_integer(RSA_EXPONENT);

    /*
     * Generate p and q: primes with combined length `bits', not
     * congruent to 1 modulo e. (Strictly speaking, we wanted (p-1)
     * and e to be coprime, and (q-1) and e to be coprime, but in
     * general that's slightly more fiddly to arrange. By choosing
     * a prime e, we can simplify the criterion.)
     *
     * We give a min_separation of 2 to invent_firstbits(), ensuring
     * that the two primes won't be very close to each other. (The
     * chance of them being _dangerously_ close is negligible - even
     * more so than an attacker guessing a whole 256-bit session key -
     * but it doesn't cost much to make sure.)
     */
    int qbits = bits / 2;
    int pbits = bits - qbits;
    assert(pbits >= qbits);

    ProgressPhase phase_p = primegen_add_progress_phase(pgc, prog, pbits);
    ProgressPhase phase_q = primegen_add_progress_phase(pgc, prog, qbits);
    progress_ready(prog);

    unsigned pfirst, qfirst;
    invent_firstbits(&pfirst, &qfirst, 2);

    PrimeCandidateSource *pcs;

    progress_start_phase(prog, phase_p);
    pcs = pcs_new_with_firstbits(pbits, pfirst, NFIRSTBITS);
    pcs_avoid_residue_small(pcs, RSA_EXPONENT, 1);
    mp_int *p = primegen_generate(pgc, pcs, prog);
    progress_report_phase_complete(prog);

    progress_start_phase(prog, phase_q);
    pcs = pcs_new_with_firstbits(qbits, qfirst, NFIRSTBITS);
    pcs_avoid_residue_small(pcs, RSA_EXPONENT, 1);
    mp_int *q = primegen_generate(pgc, pcs, prog);
    progress_report_phase_complete(prog);

    /*
     * Ensure p > q, by swapping them if not.
     *
     * We only need to do this if the two primes were generated with
     * the same number of bits (i.e. if the requested key size is
     * even) - otherwise it's already guaranteed!
     */
    if (pbits == qbits) {
        mp_cond_swap(p, q, mp_cmp_hs(q, p));
    } else {
        assert(mp_cmp_hs(p, q));
    }

    /*
     * Now we have p, q and e. All we need to do now is work out
     * the other helpful quantities: n=pq, d=e^-1 mod (p-1)(q-1),
     * and (q^-1 mod p).
     */
    mp_int *modulus = mp_mul(p, q);
    mp_int *pm1 = mp_copy(p);
    mp_sub_integer_into(pm1, pm1, 1);
    mp_int *qm1 = mp_copy(q);
    mp_sub_integer_into(qm1, qm1, 1);
    mp_int *phi_n = mp_mul(pm1, qm1);
    mp_free(pm1);
    mp_free(qm1);
    mp_int *private_exponent = mp_invert(exponent, phi_n);
    mp_free(phi_n);
    mp_int *iqmp = mp_invert(q, p);

    /*
     * Populate the returned structure.
     */
    key->modulus = modulus;
    key->exponent = exponent;
    key->private_exponent = private_exponent;
    key->p = p;
    key->q = q;
    key->iqmp = iqmp;

    key->bits = mp_get_nbits(modulus);
    key->bytes = (key->bits + 7) / 8;

    return 1;
}

/*
 * Invent a pair of values suitable for use as the 'firstbits' values
 * for the two RSA primes, such that their product is at least 2, and
 * such that their difference is also at least min_separation.
 *
 * This is used for generating RSA keys which have exactly the
 * specified number of bits rather than one fewer - if you generate an
 * a-bit and a b-bit number completely at random and multiply them
 * together, you could end up with either an (ab-1)-bit number or an
 * (ab)-bit number. The former happens log(2)*2-1 of the time (about
 * 39%) and, though actually harmless, every time it occurs it has a
 * non-zero probability of sparking a user email along the lines of
 * 'Hey, I asked PuTTYgen for a 2048-bit key and I only got 2047 bits!
 * Bug!'
 */
static inline unsigned firstbits_b_min(
    unsigned a, unsigned lo, unsigned hi, unsigned min_separation)
{
    /* To get a large enough product, b must be at least this much */
    unsigned b_min = (2*lo*lo + a - 1) / a;
    /* Now enforce a<b, optionally with minimum separation */
    if (b_min < a + min_separation)
        b_min = a + min_separation;
    /* And cap at the upper limit */
    if (b_min > hi)
        b_min = hi;
    return b_min;
}

static void invent_firstbits(unsigned *one, unsigned *two,
                             unsigned min_separation)
{
    /*
     * We'll pick 12 initial bits (number selected at random) for each
     * prime, not counting the leading 1. So we want to return two
     * values in the range [2^12,2^13) whose product is at least 2^25.
     *
     * Strategy: count up all the viable pairs, then select a random
     * number in that range and use it to pick a pair.
     *
     * To keep things simple, we'll ensure a < b, and randomly swap
     * them at the end.
     */
    const unsigned lo = 1<<12, hi = 1<<13, minproduct = 2*lo*lo;
    unsigned a, b;

    /*
     * Count up the number of prefixes of b that would be valid for
     * each prefix of a.
     */
    mp_int *total = mp_new(32);
    for (a = lo; a < hi; a++) {
        unsigned b_min = firstbits_b_min(a, lo, hi, min_separation);
        mp_add_integer_into(total, total, hi - b_min);
    }

    /*
     * Make up a random number in the range [0,2*total).
     */
    mp_int *mlo = mp_from_integer(0), *mhi = mp_new(32);
    mp_lshift_fixed_into(mhi, total, 1);
    mp_int *randval = mp_random_in_range(mlo, mhi);
    mp_free(mlo);
    mp_free(mhi);

    /*
     * Use the low bit of randval as our swap indicator, leaving the
     * rest of it in the range [0,total).
     */
    unsigned swap = mp_get_bit(randval, 0);
    mp_rshift_fixed_into(randval, randval, 1);

    /*
     * Now do the same counting loop again to make the actual choice.
     */
    a = b = 0;
    for (unsigned a_candidate = lo; a_candidate < hi; a_candidate++) {
        unsigned b_min = firstbits_b_min(a_candidate, lo, hi, min_separation);
        unsigned limit = hi - b_min;

        unsigned b_candidate = b_min + mp_get_integer(randval);
        unsigned use_it = 1 ^ mp_hs_integer(randval, limit);
        a ^= (a ^ a_candidate) & -use_it;
        b ^= (b ^ b_candidate) & -use_it;

        mp_sub_integer_into(randval, randval, limit);
    }

    mp_free(randval);
    mp_free(total);

    /*
     * Check everything came out right.
     */
    assert(lo <= a);
    assert(a < hi);
    assert(lo <= b);
    assert(b < hi);
    assert(a * b >= minproduct);
    assert(b >= a + min_separation);

    /*
     * Last-minute optional swap of a and b.
     */
    unsigned diff = (a ^ b) & (-swap);
    a ^= diff;
    b ^= diff;

    *one = a;
    *two = b;
}
