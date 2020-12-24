/*
 * SHA-512 algorithm as described at
 *
 *   http://csrc.nist.gov/cryptval/shs.html
 *
 * Modifications made for SHA-384 also
 */

#include <assert.h>
#include "ssh.h"

#define BLKSIZE 128

typedef struct {
    uint64_t h[8];
    unsigned char block[BLKSIZE];
    int blkused;
    uint64_t lenhi, lenlo;
    BinarySink_IMPLEMENTATION;
} SHA512_State;

/* ----------------------------------------------------------------------
 * Core SHA512 algorithm: processes 16-doubleword blocks into a
 * message digest.
 */

static inline uint64_t ror(uint64_t x, unsigned y)
{
    return (x << (63 & -y)) | (x >> (63 & y));
}

static inline uint64_t Ch(uint64_t ctrl, uint64_t if1, uint64_t if0)
{
    return if0 ^ (ctrl & (if1 ^ if0));
}

static inline uint64_t Maj(uint64_t x, uint64_t y, uint64_t z)
{
    return (x & y) | (z & (x | y));
}

static inline uint64_t Sigma_0(uint64_t x)
{
    return ror(x,28) ^ ror(x,34) ^ ror(x,39);
}

static inline uint64_t Sigma_1(uint64_t x)
{
    return ror(x,14) ^ ror(x,18) ^ ror(x,41);
}

static inline uint64_t sigma_0(uint64_t x)
{
    return ror(x,1) ^ ror(x,8) ^ (x >> 7);
}

static inline uint64_t sigma_1(uint64_t x)
{
    return ror(x,19) ^ ror(x,61) ^ (x >> 6);
}

static inline void SHA512_Round(
    unsigned round_index, const uint64_t *round_constants,
    const uint64_t *schedule,
    uint64_t *a, uint64_t *b, uint64_t *c, uint64_t *d,
    uint64_t *e, uint64_t *f, uint64_t *g, uint64_t *h)
{
    uint64_t t1 = *h + Sigma_1(*e) + Ch(*e,*f,*g) +
        round_constants[round_index] + schedule[round_index];

    uint64_t t2 = Sigma_0(*a) + Maj(*a,*b,*c);

    *d += t1;
    *h = t1 + t2;
}

static void SHA512_Core_Init(SHA512_State *s) {
    static const uint64_t iv[] = {
        0x6a09e667f3bcc908ULL,
        0xbb67ae8584caa73bULL,
        0x3c6ef372fe94f82bULL,
        0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL,
        0x9b05688c2b3e6c1fULL,
        0x1f83d9abfb41bd6bULL,
        0x5be0cd19137e2179ULL,
    };
    int i;
    for (i = 0; i < 8; i++)
        s->h[i] = iv[i];
}

static void SHA384_Core_Init(SHA512_State *s) {
    static const uint64_t iv[] = {
        0xcbbb9d5dc1059ed8ULL,
        0x629a292a367cd507ULL,
        0x9159015a3070dd17ULL,
        0x152fecd8f70e5939ULL,
        0x67332667ffc00b31ULL,
        0x8eb44a8768581511ULL,
        0xdb0c2e0d64f98fa7ULL,
        0x47b5481dbefa4fa4ULL,
    };
    int i;
    for (i = 0; i < 8; i++)
        s->h[i] = iv[i];
}

static void SHA512_Block(SHA512_State *s, uint64_t *block) {
    uint64_t w[80];
    uint64_t a,b,c,d,e,f,g,h;
    static const uint64_t k[] = {
        0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL,
        0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
        0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
        0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
        0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
        0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
        0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL,
        0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
        0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
        0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
        0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL,
        0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
        0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL,
        0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
        0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
        0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
        0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL,
        0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
        0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL,
        0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
        0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
        0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
        0xd192e819d6ef5218ULL, 0xd69906245565a910ULL,
        0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
        0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
        0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
        0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
        0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
        0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL,
        0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL,
        0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
        0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
        0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
        0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
        0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
        0x28db77f523047d84ULL, 0x32caab7b40c72493ULL,
        0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
        0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
        0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
    };

    int t;

    for (t = 0; t < 16; t++)
        w[t] = block[t];

    for (t = 16; t < 80; t++)
        w[t] = w[t-16] + w[t-7] + sigma_0(w[t-15]) + sigma_1(w[t-2]);

    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3];
    e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];

    for (t = 0; t < 80; t+=8) {
        SHA512_Round(t+0, k,w, &a,&b,&c,&d,&e,&f,&g,&h);
        SHA512_Round(t+1, k,w, &h,&a,&b,&c,&d,&e,&f,&g);
        SHA512_Round(t+2, k,w, &g,&h,&a,&b,&c,&d,&e,&f);
        SHA512_Round(t+3, k,w, &f,&g,&h,&a,&b,&c,&d,&e);
        SHA512_Round(t+4, k,w, &e,&f,&g,&h,&a,&b,&c,&d);
        SHA512_Round(t+5, k,w, &d,&e,&f,&g,&h,&a,&b,&c);
        SHA512_Round(t+6, k,w, &c,&d,&e,&f,&g,&h,&a,&b);
        SHA512_Round(t+7, k,w, &b,&c,&d,&e,&f,&g,&h,&a);
    }

    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

/* ----------------------------------------------------------------------
 * Outer SHA512 algorithm: take an arbitrary length byte string,
 * convert it into 16-doubleword blocks with the prescribed padding
 * at the end, and pass those blocks to the core SHA512 algorithm.
 */

static void SHA512_BinarySink_write(BinarySink *bs,
                                    const void *p, size_t len);

static void SHA512_Init(SHA512_State *s) {
    SHA512_Core_Init(s);
    s->blkused = 0;
    s->lenhi = s->lenlo = 0;
    BinarySink_INIT(s, SHA512_BinarySink_write);
}

static void SHA384_Init(SHA512_State *s) {
    SHA384_Core_Init(s);
    s->blkused = 0;
    s->lenhi = s->lenlo = 0;
    BinarySink_INIT(s, SHA512_BinarySink_write);
}

static void SHA512_BinarySink_write(BinarySink *bs,
                                    const void *p, size_t len)
{
    SHA512_State *s = BinarySink_DOWNCAST(bs, SHA512_State);
    unsigned char *q = (unsigned char *)p;
    uint64_t wordblock[16];
    int i;

    /*
     * Update the length field.
     */
    s->lenlo += len;
    s->lenhi += (s->lenlo < len);

    if (s->blkused && s->blkused+len < BLKSIZE) {
        /*
         * Trivial case: just add to the block.
         */
        memcpy(s->block + s->blkused, q, len);
        s->blkused += len;
    } else {
        /*
         * We must complete and process at least one block.
         */
        while (s->blkused + len >= BLKSIZE) {
            memcpy(s->block + s->blkused, q, BLKSIZE - s->blkused);
            q += BLKSIZE - s->blkused;
            len -= BLKSIZE - s->blkused;
            /* Now process the block. Gather bytes big-endian into words */
            for (i = 0; i < 16; i++)
                wordblock[i] = GET_64BIT_MSB_FIRST(s->block + i*8);
            SHA512_Block(s, wordblock);
            s->blkused = 0;
        }
        memcpy(s->block, q, len);
        s->blkused = len;
    }
}

static void SHA512_Final(SHA512_State *s, unsigned char *digest) {
    int i;
    int pad;
    unsigned char c[BLKSIZE];
    uint64_t lenhi, lenlo;

    if (s->blkused >= BLKSIZE-16)
        pad = (BLKSIZE-16) + BLKSIZE - s->blkused;
    else
        pad = (BLKSIZE-16) - s->blkused;

    lenhi = (s->lenhi << 3) | (s->lenlo >> (32-3));
    lenlo = (s->lenlo << 3);

    memset(c, 0, pad);
    c[0] = 0x80;
    put_data(s, &c, pad);

    put_uint64(s, lenhi);
    put_uint64(s, lenlo);

    for (i = 0; i < 8; i++)
        PUT_64BIT_MSB_FIRST(digest + i*8, s->h[i]);
}

static void SHA384_Final(SHA512_State *s, unsigned char *digest) {
    unsigned char biggerDigest[512 / 8];
    SHA512_Final(s, biggerDigest);
    memcpy(digest, biggerDigest, 384 / 8);
}

/*
 * Thin abstraction for things where hashes are pluggable.
 */

struct sha512_hash {
    SHA512_State state;
    ssh_hash hash;
};

static ssh_hash *sha512_new(const ssh_hashalg *alg)
{
    struct sha512_hash *h = snew(struct sha512_hash);
    h->hash.vt = alg;
    BinarySink_DELEGATE_INIT(&h->hash, &h->state);
    return ssh_hash_reset(&h->hash);
}

static void sha512_reset(ssh_hash *hash)
{
    struct sha512_hash *h = container_of(hash, struct sha512_hash, hash);
    SHA512_Init(&h->state);
}

static void sha512_copyfrom(ssh_hash *hashnew, ssh_hash *hashold)
{
    struct sha512_hash *hold = container_of(hashold, struct sha512_hash, hash);
    struct sha512_hash *hnew = container_of(hashnew, struct sha512_hash, hash);

    hnew->state = hold->state;
    BinarySink_COPIED(&hnew->state);
}

static void sha512_free(ssh_hash *hash)
{
    struct sha512_hash *h = container_of(hash, struct sha512_hash, hash);

    smemclr(h, sizeof(*h));
    sfree(h);
}

static void sha512_digest(ssh_hash *hash, unsigned char *output)
{
    struct sha512_hash *h = container_of(hash, struct sha512_hash, hash);
    SHA512_Final(&h->state, output);
}

const ssh_hashalg ssh_sha512 = {
    .new = sha512_new,
    .reset = sha512_reset,
    .copyfrom = sha512_copyfrom,
    .digest = sha512_digest,
    .free = sha512_free,
    .hlen = 64,
    .blocklen = BLKSIZE,
    HASHALG_NAMES_BARE("SHA-512"),
};

static void sha384_reset(ssh_hash *hash)
{
    struct sha512_hash *h = container_of(hash, struct sha512_hash, hash);
    SHA384_Init(&h->state);
}

static void sha384_digest(ssh_hash *hash, unsigned char *output)
{
    struct sha512_hash *h = container_of(hash, struct sha512_hash, hash);
    SHA384_Final(&h->state, output);
}

const ssh_hashalg ssh_sha384 = {
    .new = sha512_new,
    .reset = sha384_reset,
    .copyfrom = sha512_copyfrom,
    .digest = sha384_digest,
    .free = sha512_free,
    .hlen = 48,
    .blocklen = BLKSIZE,
    HASHALG_NAMES_BARE("SHA-384"),
};
