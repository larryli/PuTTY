/*
 * sshkeygen.h: routines used internally to key generation.
 */

/*
 * A table of all the primes that fit in a 16-bit integer. Call
 * init_primes_array to make sure it's been initialised.
 */

#define NSMALLPRIMES 6542 /* number of primes < 65536 */
extern const unsigned short *const smallprimes;
void init_smallprimes(void);
