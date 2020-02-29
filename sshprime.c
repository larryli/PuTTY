/*
 * Prime generation.
 */

#include <assert.h>
#include <math.h>

#include "ssh.h"
#include "mpint.h"
#include "mpunsafe.h"
#include "sshkeygen.h"

/* ----------------------------------------------------------------------
 * Standard probabilistic prime-generation algorithm:
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

static PrimeGenerationContext *probprime_new_context(
    const PrimeGenerationPolicy *policy)
{
    PrimeGenerationContext *ctx = snew(PrimeGenerationContext);
    ctx->vt = policy;
    return ctx;
}

static void probprime_free_context(PrimeGenerationContext *ctx)
{
    sfree(ctx);
}

static ProgressPhase probprime_add_progress_phase(
    const PrimeGenerationPolicy *policy,
    ProgressReceiver *prog, unsigned bits)
{
    /*
     * The density of primes near x is 1/(log x). When x is about 2^b,
     * that's 1/(b log 2).
     *
     * But we're only doing the expensive part of the process (the M-R
     * checks) for a number that passes the initial winnowing test of
     * having no factor less than 2^16 (at least, unless the prime is
     * so small that PrimeCandidateSource gives up on that winnowing).
     * The density of _those_ numbers is about 1/19.76. So the odds of
     * hitting a prime per expensive attempt are boosted by a factor
     * of 19.76.
     */
    const double log_2 = 0.693147180559945309417232121458;
    double winnow_factor = (bits < 32 ? 1.0 : 19.76);
    double prob = winnow_factor / (bits * log_2);

    /*
     * Estimate the cost of prime generation as the cost of the M-R
     * modexps.
     */
    double cost = (miller_rabin_checks_needed(bits) *
                   estimate_modexp_cost(bits));
    return progress_add_probabilistic(prog, cost, prob);
}

static mp_int *probprime_generate(
    PrimeGenerationContext *ctx,
    PrimeCandidateSource *pcs, ProgressReceiver *prog)
{
    pcs_ready(pcs);

    while (true) {
        progress_report_attempt(prog);

        mp_int *p = pcs_generate(pcs);

        MillerRabin *mr = miller_rabin_new(p);
        bool known_bad = false;
        unsigned nchecks = miller_rabin_checks_needed(mp_get_nbits(p));
        for (unsigned check = 0; check < nchecks; check++) {
            if (!miller_rabin_test_random(mr)) {
                known_bad = true;
                break;
            }
        }
        miller_rabin_free(mr);

        if (!known_bad) {
            /*
             * We have a prime!
             */
            pcs_free(pcs);
            return p;
        }

        mp_free(p);
    }
}

const PrimeGenerationPolicy primegen_probabilistic = {
    probprime_add_progress_phase,
    probprime_new_context,
    probprime_free_context,
    probprime_generate,
};

/* ----------------------------------------------------------------------
 * Reusable null implementation of the progress-reporting API.
 */

ProgressPhase null_progress_add_probabilistic(
    ProgressReceiver *prog, double c, double p) {
    ProgressPhase ph = { .n = 0 };
    return ph;
}
void null_progress_ready(ProgressReceiver *prog) {}
void null_progress_start_phase(ProgressReceiver *prog, ProgressPhase phase) {}
void null_progress_report_attempt(ProgressReceiver *prog) {}
void null_progress_report_phase_complete(ProgressReceiver *prog) {}
const ProgressReceiverVtable null_progress_vt = {
    null_progress_add_probabilistic,
    null_progress_ready,
    null_progress_start_phase,
    null_progress_report_attempt,
    null_progress_report_phase_complete,
};

/* ----------------------------------------------------------------------
 * Helper function for progress estimation.
 */

double estimate_modexp_cost(unsigned bits)
{
    /*
     * A modexp of n bits goes roughly like O(n^2.58), on the grounds
     * that our modmul is O(n^1.58) (Karatsuba) and you need O(n) of
     * them in a modexp.
     */
    return pow(bits, 2.58);
}
