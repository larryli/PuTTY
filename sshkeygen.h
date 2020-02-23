/*
 * sshkeygen.h: routines used internally to key generation.
 */

/* ----------------------------------------------------------------------
 * A table of all the primes that fit in a 16-bit integer. Call
 * init_primes_array to make sure it's been initialised.
 */

#define NSMALLPRIMES 6542 /* number of primes < 65536 */
extern const unsigned short *const smallprimes;
void init_smallprimes(void);

/* ----------------------------------------------------------------------
 * A system for making up random candidate integers during prime
 * generation. This unconditionally ensures that the numbers have the
 * right number of bits and are not divisible by any prime in the
 * smallprimes[] array above. It can also impose further constraints,
 * as documented below.
 */
typedef struct PrimeCandidateSource PrimeCandidateSource;

/*
 * pcs_new: you say how many bits you want the prime to have (with the
 * usual semantics that an n-bit number is in the range [2^{n-1},2^n))
 * and also specify what you want its topmost 'nfirst' bits to be.
 *
 * (The 'first' system is used for RSA keys, where you need to arrange
 * that the product of your two primes is in a more tightly
 * constrained range than the factor of 4 you'd get by just generating
 * two (n/2)-bit primes and multiplying them. Any application that
 * doesn't need that can simply specify first = nfirst = 1.)
 */
PrimeCandidateSource *pcs_new(unsigned bits, unsigned first, unsigned nfirst);

/* Insist that generated numbers must be congruent to 'res' mod 'mod' */
void pcs_require_residue(PrimeCandidateSource *s, mp_int *mod, mp_int *res);

/* Convenience wrapper for the common case where res = 1 */
void pcs_require_residue_1(PrimeCandidateSource *s, mp_int *mod);

/* Insist that generated numbers must _not_ be congruent to 'res' mod
 * 'mod'. This is used to avoid being 1 mod the RSA public exponent,
 * which is small, so it only needs ordinary integer parameters. */
void pcs_avoid_residue_small(PrimeCandidateSource *s,
                             unsigned mod, unsigned res);

/* Prepare a PrimeCandidateSource to actually generate numbers. This
 * function does last-minute computation that has to be delayed until
 * all constraints have been input. */
void pcs_ready(PrimeCandidateSource *s);

/* Actually generate a candidate integer. You must free the result, of
 * course. */
mp_int *pcs_generate(PrimeCandidateSource *s);

/* Free a PrimeCandidateSource. */
void pcs_free(PrimeCandidateSource *s);

/* Return some internal fields of the PCS. Used by testcrypt for
 * unit-testing this system. */
void pcs_inspect(PrimeCandidateSource *pcs, mp_int **limit_out,
                 mp_int **factor_out, mp_int **addend_out);
