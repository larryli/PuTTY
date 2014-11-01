/*
 * EC key generation.
 */

#include "ssh.h"

/* Forward reference from sshecc.c */
struct ec_point *ecp_mul(const struct ec_point *a, const Bignum b);

int ec_generate(struct ec_key *key, int bits, progfn_t pfn,
                void *pfnparam)
{
    struct ec_point *publicKey;

    if (bits == 256) {
        key->publicKey.curve = ec_p256();
    } else if (bits == 384) {
        key->publicKey.curve = ec_p384();
    } else if (bits == 521) {
        key->publicKey.curve = ec_p521();
    } else {
        return 0;
    }

    key->privateKey = bignum_random_in_range(One, key->publicKey.curve->n);
    if (!key->privateKey) return 0;

    publicKey = ecp_mul(&key->publicKey.curve->G, key->privateKey);
    if (!publicKey) {
        freebn(key->privateKey);
        key->privateKey = NULL;
        return 0;
    }

    key->publicKey.x = publicKey->x;
    key->publicKey.y = publicKey->y;
    key->publicKey.z = NULL;
    sfree(publicKey);

    return 1;
}
