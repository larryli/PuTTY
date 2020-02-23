/*
 * primecandidate.c: implementation of the PrimeCandidateSource
 * abstraction declared in sshkeygen.h.
 */

#include <assert.h>
#include "ssh.h"
#include "mpint.h"
#include "mpunsafe.h"
#include "sshkeygen.h"

struct PrimeCandidateSource {
    unsigned bits;
    bool ready;

    /* We'll start by making up a random number strictly less than this ... */
    mp_int *limit;

    /* ... then we'll multiply by 'factor', and add 'addend'. */
    mp_int *factor, *addend;

    /* Then we'll try to add a small multiple of 'factor' to it to
     * avoid it being a multiple of any small prime. Also, for RSA, we
     * may need to avoid it being _this_ multiple of _this_: */
    unsigned avoid_residue, avoid_modulus;
};

PrimeCandidateSource *pcs_new(unsigned bits, unsigned first, unsigned nfirst)
{
    PrimeCandidateSource *s = snew(PrimeCandidateSource);

    assert(first >> (nfirst-1) == 1);

    s->bits = bits;
    s->ready = false;

    /* Make the number that's the lower limit of our range */
    mp_int *firstmp = mp_from_integer(first);
    mp_int *base = mp_lshift_fixed(firstmp, bits - nfirst);
    mp_free(firstmp);

    /* Set the low bit of that, because all (nontrivial) primes are odd */
    mp_set_bit(base, 0, 1);

    /* That's our addend. Now initialise factor to 2, to ensure we
     * only generate odd numbers */
    s->factor = mp_from_integer(2);
    s->addend = base;

    /* And that means the limit of our random numbers must be one
     * factor of two _less_ than the position of the low bit of
     * 'first', because we'll be multiplying the random number by
     * 2 immediately afterwards. */
    s->limit = mp_power_2(bits - nfirst - 1);

    /* avoid_modulus == 0 signals that there's no extra residue to avoid */
    s->avoid_residue = 1;
    s->avoid_modulus = 0;

    return s;
}

void pcs_free(PrimeCandidateSource *s)
{
    mp_free(s->limit);
    mp_free(s->factor);
    mp_free(s->addend);
    sfree(s);
}

static void pcs_require_residue_inner(PrimeCandidateSource *s,
                                      mp_int *mod, mp_int *res)
{
    /*
     * We already have a factor and addend. Ensure this one doesn't
     * contradict it.
     */
    mp_int *gcd = mp_gcd(mod, s->factor);
    mp_int *test1 = mp_mod(s->addend, gcd);
    mp_int *test2 = mp_mod(res, gcd);
    assert(mp_cmp_eq(test1, test2));
    mp_free(test1);
    mp_free(test2);

    /*
     * Reduce our input factor and addend, which are constraints on
     * the ultimate output number, so that they're constraints on the
     * initial cofactor we're going to make up.
     *
     * If we're generating x and we want to ensure ax+b == r (mod m),
     * how does that work? We've already checked that b == r modulo g
     * = gcd(a,m), i.e. r-b is a multiple of g, and so are a and m. So
     * let's write a=gA, m=gM, (r-b)=gR, and then we can start by
     * dividing that off:
     *
     *      ax == r-b (mod m )
     * =>  gAx == gR  (mod gM)
     * =>   Ax ==  R  (mod  M)
     *
     * Now the moduli A,M are coprime, which makes things easier.
     *
     * We're going to need to generate the x in this equation by
     * generating a new smaller value y, multiplying it by M, and
     * adding some constant K. So we have x = My + K, and we need to
     * work out what K will satisfy the above equation. In other
     * words, we need A(My+K) == R (mod M), and the AMy term vanishes,
     * so we just need AK == R (mod M). So our congruence is solved by
     * setting K to be R * A^{-1} mod M.
     */
    mp_int *A = mp_div(s->factor, gcd);
    mp_int *M = mp_div(mod, gcd);
    mp_int *Rpre = mp_modsub(res, s->addend, mod);
    mp_int *R = mp_div(Rpre, gcd);
    mp_int *Ainv = mp_invert(A, M);
    mp_int *K = mp_modmul(R, Ainv, M);

    mp_free(gcd);
    mp_free(Rpre);
    mp_free(Ainv);
    mp_free(A);
    mp_free(R);

    /*
     * So we know we have to transform our existing (factor, addend)
     * pair into (factor * M, addend * factor * K). Now we just need
     * to work out what the limit should be on the random value we're
     * generating.
     *
     * If we need My+K < old_limit, then y < (old_limit-K)/M. But the
     * RHS is a fraction, so in integers, we need y < ceil of it.
     */
    assert(!mp_cmp_hs(K, s->limit));
    mp_int *dividend = mp_add(s->limit, M);
    mp_sub_integer_into(dividend, dividend, 1);
    mp_sub_into(dividend, dividend, K);
    mp_free(s->limit);
    s->limit = mp_div(dividend, M);
    mp_free(dividend);

    /*
     * Now just update the real factor and addend, and we're done.
     */

    mp_int *addend_old = s->addend;
    mp_int *tmp = mp_mul(s->factor, K); /* use the _old_ value of factor */
    s->addend = mp_add(s->addend, tmp);
    mp_free(tmp);
    mp_free(addend_old);

    mp_int *factor_old = s->factor;
    s->factor = mp_mul(s->factor, M);
    mp_free(factor_old);

    mp_free(M);
    mp_free(K);
    s->factor = mp_unsafe_shrink(s->factor);
    s->addend = mp_unsafe_shrink(s->addend);
    s->limit = mp_unsafe_shrink(s->limit);
}

void pcs_require_residue(PrimeCandidateSource *s,
                         mp_int *mod, mp_int *res_orig)
{
    /*
     * Reduce the input residue to its least non-negative value, in
     * case it was given as a larger equivalent value.
     */
    mp_int *res_reduced = mp_mod(res_orig, mod);
    pcs_require_residue_inner(s, mod, res_reduced);
    mp_free(res_reduced);
}

void pcs_require_residue_1(PrimeCandidateSource *s, mp_int *mod)
{
    mp_int *res = mp_from_integer(1);
    pcs_require_residue(s, mod, res);
    mp_free(res);
}

void pcs_avoid_residue_small(PrimeCandidateSource *s,
                             unsigned mod, unsigned res)
{
    assert(!s->avoid_modulus);         /* can't cope with more than one */
    s->avoid_modulus = mod;
    s->avoid_residue = res;
}

void pcs_ready(PrimeCandidateSource *s)
{
    /*
     * Reduce the upper limit of the range we're searching, to account
     * for the fact that in the generation loop we may add up to 2^16
     * product to the random number we pick from that range.
     *
     * We can't do this until we've finished dividing limit by things,
     * of course.
     */

    assert(mp_hs_integer(s->limit, 0x10001));
    mp_sub_integer_into(s->limit, s->limit, 0x10000);

    s->ready = true;
}

mp_int *pcs_generate(PrimeCandidateSource *s)
{
    assert(s->ready);

    /* List the (modulus, residue) pairs we want to avoid. Mostly this
     * will be 'don't be 0 mod any small prime', but we may have one
     * to add from our parameters. */
    init_smallprimes();
    uint64_t avoidmod[NSMALLPRIMES + 1], avoidres[NSMALLPRIMES + 1];
    size_t navoid = 0;
    for (size_t i = 0; i < NSMALLPRIMES; i++) {
        avoidmod[navoid] = smallprimes[i];
        avoidres[navoid] = 0;
        navoid++;
    }
    if (s->avoid_modulus) {
        avoidmod[navoid] = s->avoid_modulus;
        avoidres[navoid] = s->avoid_residue % s->avoid_modulus;
        navoid++;
    }

    while (true) {
        mp_int *x = mp_random_upto(s->limit);

        uint64_t xres[NSMALLPRIMES + 1], xmul[NSMALLPRIMES + 1];
        for (size_t i = 0; i < navoid; i++) {
            uint64_t mod = avoidmod[i], res = avoidres[i];

            uint64_t factor_m = mp_unsafe_mod_integer(s->factor, mod);
            uint64_t addend_m = mp_unsafe_mod_integer(s->addend, mod);
            uint64_t x_m = mp_unsafe_mod_integer(x, mod);

            xmul[i] = factor_m;
            xres[i] = (addend_m + x_m * factor_m - res + mod) % mod;
        }

        /*
         * Try to find a value delta such that x + delta * factor
         * avoids all the residues we want to avoid. We select
         * candidates at random to avoid a directional bias, and if we
         * don't find one quickly enough, give up and try a fresh
         * random x.
         */
        unsigned delta;
        for (unsigned delta_attempts = 0; delta_attempts < 1024 ;) {
            unsigned char randbuf[64];
            random_read(randbuf, sizeof(randbuf));

            for (size_t pos = 0; pos+2 <= sizeof(randbuf);
                 pos += 2, delta_attempts++) {

                delta = GET_16BIT_MSB_FIRST(randbuf + pos);

                bool ok = true;
                for (size_t i = 0; i < navoid; i++)
                    if (!((xres[i] + delta * xmul[i]) % avoidmod[i])) {
                    ok = false;
                    break;
                }

                if (ok)
                    goto found;
            }

            smemclr(randbuf, sizeof(randbuf));
        }

        mp_free(x);
        continue; /* try a new x */

      found:;
        /*
         * We've found a viable delta. Make the final output value.
         */
        mp_int *mpdelta = mp_from_integer(delta);
        mp_int *xplus = mp_add(x, mpdelta);
        mp_int *toret = mp_new(s->bits);
        mp_mul_into(toret, xplus, s->factor);
        mp_add_into(toret, toret, s->addend);
        mp_free(mpdelta);
        mp_free(xplus);
        mp_free(x);
        return toret;
    }
}

void pcs_inspect(PrimeCandidateSource *pcs, mp_int **limit_out,
                 mp_int **factor_out, mp_int **addend_out)
{
    *limit_out = mp_copy(pcs->limit);
    *factor_out = mp_copy(pcs->factor);
    *addend_out = mp_copy(pcs->addend);
}
