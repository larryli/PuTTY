/*
 * cryptographic random number generator for PuTTY's ssh client
 */

#include "ssh.h"

void noise_get_heavy(void (*func) (void *, int));
void noise_get_light(void (*func) (void *, int));

/*
 * `pool' itself is a pool of random data which we actually use: we
 * return bytes from `pool', at position `poolpos', until `poolpos'
 * reaches the end of the pool. At this point we generate more
 * random data, by adding noise, stirring well, and resetting
 * `poolpos' to point to just past the beginning of the pool (not
 * _the_ beginning, since otherwise we'd give away the whole
 * contents of our pool, and attackers would just have to guess the
 * next lot of noise).
 *
 * `incomingb' buffers acquired noise data, until it gets full, at
 * which point the acquired noise is SHA'ed into `incoming' and
 * `incomingb' is cleared. The noise in `incoming' is used as part
 * of the noise for each stirring of the pool, in addition to local
 * time, process listings, and other such stuff.
 */

#define HASHINPUT 64		       /* 64 bytes SHA input */
#define HASHSIZE 20		       /* 160 bits SHA output */
#define POOLSIZE 1200		       /* size of random pool */

struct RandPool {
    unsigned char pool[POOLSIZE];
    int poolpos;

    unsigned char incoming[HASHSIZE];

    unsigned char incomingb[HASHINPUT];
    int incomingpos;
};

static struct RandPool pool;

void random_add_noise(void *noise, int length) {
    unsigned char *p = noise;

    while (length >= (HASHINPUT - pool.incomingpos)) {
	memcpy(pool.incomingb + pool.incomingpos, p,
	       HASHINPUT - pool.incomingpos);
	p += HASHINPUT - pool.incomingpos;
	length -= HASHINPUT - pool.incomingpos;
	SHATransform((word32 *)pool.incoming, (word32 *)pool.incomingb);
	pool.incomingpos = 0;
    }

    memcpy(pool.incomingb, p, length);
    pool.incomingpos = length;
}

void random_stir(void) {
    word32 block[HASHINPUT/sizeof(word32)];
    word32 digest[HASHSIZE/sizeof(word32)];
    int i, j, k;

    noise_get_light(random_add_noise);

    SHATransform((word32 *)pool.incoming, (word32 *)pool.incomingb);
    pool.incomingpos = 0;

    /*
     * Chunks of this code are blatantly endianness-dependent, but
     * as it's all random bits anyway, WHO CARES?
     */
    memcpy(digest, pool.incoming, sizeof(digest));

    /*
     * Make two passes over the pool.
     */
    for (i = 0; i < 2; i++) {

	/*
	 * We operate SHA in CFB mode, repeatedly adding the same
	 * block of data to the digest. But we're also fiddling
	 * with the digest-so-far, so this shouldn't be Bad or
	 * anything.
	 */
	memcpy(block, pool.pool, sizeof(block));

	/*
	 * Each pass processes the pool backwards in blocks of
	 * HASHSIZE, just so that in general we get the output of
	 * SHA before the corresponding input, in the hope that
	 * things will be that much less predictable that way
	 * round, when we subsequently return bytes ...
	 */
	for (j = POOLSIZE; (j -= HASHSIZE) >= 0 ;) {
	    /*
	     * XOR the bit of the pool we're processing into the
	     * digest.
	     */

	    for (k = 0; k < sizeof(digest)/sizeof(*digest); k++)
		digest[k] ^= ((word32 *)(pool.pool+j))[k];

	    /*
	     * Munge our unrevealed first block of the pool into
	     * it.
	     */
	    SHATransform(digest, block);

	    /*
	     * Stick the result back into the pool.
	     */

	    for (k = 0; k < sizeof(digest)/sizeof(*digest); k++)
		((word32 *)(pool.pool+j))[k] = digest[k];
	}
    }

    /*
     * Might as well save this value back into `incoming', just so
     * there'll be some extra bizarreness there.
     */
    SHATransform(digest, block);
    memcpy(digest, pool.incoming, sizeof(digest));

    pool.poolpos = sizeof(pool.incoming);
}

static void random_add_heavynoise(void *noise, int length) {
    unsigned char *p = noise;

    while (length >= (POOLSIZE - pool.poolpos)) {
	memcpy(pool.pool + pool.poolpos, p, POOLSIZE - pool.poolpos);
	p += POOLSIZE - pool.poolpos;
	length -= POOLSIZE - pool.poolpos;
	random_stir();
	pool.poolpos = 0;
    }

    memcpy(pool.pool, p, length);
    pool.poolpos = length;
}

void random_init(void) {
    memset(&pool, 0, sizeof(pool));    /* just to start with */

    /*
     * For noise_get_heavy, we temporarily use `poolpos' as the
     * pointer for addition of noise, rather than extraction of
     * random numbers.
     */
    pool.poolpos = 0;
    noise_get_heavy(random_add_heavynoise);

    random_stir();
}

int random_byte(void) {
    if (pool.poolpos >= POOLSIZE)
	random_stir();

    return pool.pool[pool.poolpos++];
}

void random_get_savedata(void **data, int *len) {
    random_stir();
    *data = pool.pool+pool.poolpos;
    *len = POOLSIZE/2;
}
