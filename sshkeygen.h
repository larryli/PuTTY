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
 * and also optionally specify what you want its topmost 'nfirst' bits
 * to be.
 *
 * (The 'first' system is used for RSA keys, where you need to arrange
 * that the product of your two primes is in a more tightly
 * constrained range than the factor of 4 you'd get by just generating
 * two (n/2)-bit primes and multiplying them.)
 */
PrimeCandidateSource *pcs_new(unsigned bits);
PrimeCandidateSource *pcs_new_with_firstbits(unsigned bits,
                                             unsigned first, unsigned nfirst);

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

/* Query functions for primegen to use */
unsigned pcs_get_bits(PrimeCandidateSource *pcs);

/* ----------------------------------------------------------------------
 * A system for doing Miller-Rabin probabilistic primality tests.
 * These benefit from having set up some context beforehand, if you're
 * going to do more than one of them on the same candidate prime, so
 * we declare an object type here to store that context.
 */

typedef struct MillerRabin MillerRabin;

/* Make and free a Miller-Rabin context. */
MillerRabin *miller_rabin_new(mp_int *p);
void miller_rabin_free(MillerRabin *mr);

/* Perform a single Miller-Rabin test, using a random witness value. */
bool miller_rabin_test_random(MillerRabin *mr);

/* Suggest how many tests are needed to make it sufficiently unlikely
 * that a composite number will pass them all */
unsigned miller_rabin_checks_needed(unsigned bits);

/* An extension to the M-R test, which iterates until it either finds
 * a witness value that is potentially a primitive root, or one
 * that proves the number to be composite. */
mp_int *miller_rabin_find_potential_primitive_root(MillerRabin *mr);

/* ----------------------------------------------------------------------
 * Callback API that allows key generation to report progress to its
 * caller.
 */

typedef struct ProgressReceiverVtable ProgressReceiverVtable;
typedef struct ProgressReceiver ProgressReceiver;
typedef union ProgressPhase ProgressPhase;

union ProgressPhase {
    int n;
    void *p;
};

struct ProgressReceiver {
    const ProgressReceiverVtable *vt;
};

struct ProgressReceiverVtable {
    ProgressPhase (*add_probabilistic)(ProgressReceiver *prog,
                                       double cost_per_attempt,
                                       double attempt_probability);
    void (*ready)(ProgressReceiver *prog);
    void (*start_phase)(ProgressReceiver *prog, ProgressPhase phase);
    void (*report_attempt)(ProgressReceiver *prog);
    void (*report_phase_complete)(ProgressReceiver *prog);
};

static inline ProgressPhase progress_add_probabilistic(ProgressReceiver *prog,
                                                       double c, double p)
{ return prog->vt->add_probabilistic(prog, c, p); }
static inline void progress_ready(ProgressReceiver *prog)
{ prog->vt->ready(prog); }
static inline void progress_start_phase(
    ProgressReceiver *prog, ProgressPhase phase)
{ prog->vt->start_phase(prog, phase); }
static inline void progress_report_attempt(ProgressReceiver *prog)
{ prog->vt->report_attempt(prog); }
static inline void progress_report_phase_complete(ProgressReceiver *prog)
{ prog->vt->report_phase_complete(prog); }

ProgressPhase null_progress_add_probabilistic(
    ProgressReceiver *prog, double c, double p);
void null_progress_ready(ProgressReceiver *prog);
void null_progress_start_phase(ProgressReceiver *prog, ProgressPhase phase);
void null_progress_report_attempt(ProgressReceiver *prog);
void null_progress_report_phase_complete(ProgressReceiver *prog);
extern const ProgressReceiverVtable null_progress_vt;

/* A helper function for dreaming up progress cost estimates. */
double estimate_modexp_cost(unsigned bits);

/* ----------------------------------------------------------------------
 * The top-level API for generating primes.
 */

/* This function consumes and frees the PrimeCandidateSource you give it */
mp_int *primegen(PrimeCandidateSource *pcs, ProgressReceiver *prog);

/* Estimate how long it will take, and add a phase to a ProgressReceiver */
ProgressPhase primegen_add_progress_phase(ProgressReceiver *prog,
                                          unsigned bits);

/* ----------------------------------------------------------------------
 * The overall top-level API for generating entire key pairs.
 */

int rsa_generate(RSAKey *key, int bits, ProgressReceiver *prog);
int dsa_generate(struct dss_key *key, int bits, ProgressReceiver *prog);
int ecdsa_generate(struct ecdsa_key *key, int bits);
int eddsa_generate(struct eddsa_key *key, int bits);
