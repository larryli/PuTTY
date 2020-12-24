/*
 * SHA-512 algorithm as described at
 *
 *   http://csrc.nist.gov/cryptval/shs.html
 *
 * Modifications made for SHA-384 also
 */

#include <assert.h>
#include "ssh.h"

static const uint64_t sha512_initial_state[] = {
    0x6a09e667f3bcc908ULL,
    0xbb67ae8584caa73bULL,
    0x3c6ef372fe94f82bULL,
    0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL,
    0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL,
    0x5be0cd19137e2179ULL,
};

static const uint64_t sha384_initial_state[] = {
    0xcbbb9d5dc1059ed8ULL,
    0x629a292a367cd507ULL,
    0x9159015a3070dd17ULL,
    0x152fecd8f70e5939ULL,
    0x67332667ffc00b31ULL,
    0x8eb44a8768581511ULL,
    0xdb0c2e0d64f98fa7ULL,
    0x47b5481dbefa4fa4ULL,
};

static const uint64_t sha512_round_constants[] = {
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

#define SHA512_ROUNDS 80

typedef struct sha512_block sha512_block;
struct sha512_block {
    uint8_t block[128];
    size_t used;
    uint64_t lenhi, lenlo;
};

static inline void sha512_block_setup(sha512_block *blk)
{
    blk->used = 0;
    blk->lenhi = blk->lenlo = 0;
}

static inline bool sha512_block_write(
    sha512_block *blk, const void **vdata, size_t *len)
{
    size_t blkleft = sizeof(blk->block) - blk->used;
    size_t chunk = *len < blkleft ? *len : blkleft;

    const uint8_t *p = *vdata;
    memcpy(blk->block + blk->used, p, chunk);
    *vdata = p + chunk;
    *len -= chunk;
    blk->used += chunk;

    size_t chunkbits = chunk << 3;

    blk->lenlo += chunkbits;
    blk->lenhi += (blk->lenlo < chunkbits);

    if (blk->used == sizeof(blk->block)) {
        blk->used = 0;
        return true;
    }

    return false;
}

static inline void sha512_block_pad(sha512_block *blk, BinarySink *bs)
{
    uint64_t final_lenhi = blk->lenhi;
    uint64_t final_lenlo = blk->lenlo;
    size_t pad = 127 & (111 - blk->used);

    put_byte(bs, 0x80);
    put_padding(bs, pad, 0);
    put_uint64(bs, final_lenhi);
    put_uint64(bs, final_lenlo);

    assert(blk->used == 0 && "Should have exactly hit a block boundary");
}

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

static inline void sha512_sw_round(
    unsigned round_index, const uint64_t *schedule,
    uint64_t *a, uint64_t *b, uint64_t *c, uint64_t *d,
    uint64_t *e, uint64_t *f, uint64_t *g, uint64_t *h)
{
    uint64_t t1 = *h + Sigma_1(*e) + Ch(*e,*f,*g) +
        sha512_round_constants[round_index] + schedule[round_index];

    uint64_t t2 = Sigma_0(*a) + Maj(*a,*b,*c);

    *d += t1;
    *h = t1 + t2;
}

static void sha512_sw_block(uint64_t *core, const uint8_t *block)
{
    uint64_t w[SHA512_ROUNDS];
    uint64_t a,b,c,d,e,f,g,h;

    int t;

    for (t = 0; t < 16; t++)
        w[t] = GET_64BIT_MSB_FIRST(block + 8*t);

    for (t = 16; t < SHA512_ROUNDS; t++)
        w[t] = w[t-16] + w[t-7] + sigma_0(w[t-15]) + sigma_1(w[t-2]);

    a = core[0]; b = core[1]; c = core[2]; d = core[3];
    e = core[4]; f = core[5]; g = core[6]; h = core[7];

    for (t = 0; t < SHA512_ROUNDS; t+=8) {
        sha512_sw_round(t+0, w, &a,&b,&c,&d,&e,&f,&g,&h);
        sha512_sw_round(t+1, w, &h,&a,&b,&c,&d,&e,&f,&g);
        sha512_sw_round(t+2, w, &g,&h,&a,&b,&c,&d,&e,&f);
        sha512_sw_round(t+3, w, &f,&g,&h,&a,&b,&c,&d,&e);
        sha512_sw_round(t+4, w, &e,&f,&g,&h,&a,&b,&c,&d);
        sha512_sw_round(t+5, w, &d,&e,&f,&g,&h,&a,&b,&c);
        sha512_sw_round(t+6, w, &c,&d,&e,&f,&g,&h,&a,&b);
        sha512_sw_round(t+7, w, &b,&c,&d,&e,&f,&g,&h,&a);
    }

    core[0] += a; core[1] += b; core[2] += c; core[3] += d;
    core[4] += e; core[5] += f; core[6] += g; core[7] += h;

    smemclr(w, sizeof(w));
}

typedef struct sha512_sw {
    uint64_t core[8];
    sha512_block blk;
    BinarySink_IMPLEMENTATION;
    ssh_hash hash;
} sha512_sw;

static void sha512_sw_write(BinarySink *bs, const void *vp, size_t len);

static ssh_hash *sha512_sw_new(const ssh_hashalg *alg)
{
    sha512_sw *s = snew(sha512_sw);

    s->hash.vt = alg;
    BinarySink_INIT(s, sha512_sw_write);
    BinarySink_DELEGATE_INIT(&s->hash, s);
    return &s->hash;
}

static void sha512_sw_reset(ssh_hash *hash)
{
    sha512_sw *s = container_of(hash, sha512_sw, hash);

    /* The 'extra' field in the ssh_hashalg indicates which
     * initialisation vector we're using */
    memcpy(s->core, hash->vt->extra, sizeof(s->core));
    sha512_block_setup(&s->blk);
}

static void sha512_sw_copyfrom(ssh_hash *hcopy, ssh_hash *horig)
{
    sha512_sw *copy = container_of(hcopy, sha512_sw, hash);
    sha512_sw *orig = container_of(horig, sha512_sw, hash);

    memcpy(copy, orig, sizeof(*copy));
    BinarySink_COPIED(copy);
    BinarySink_DELEGATE_INIT(&copy->hash, copy);
}

static void sha512_sw_free(ssh_hash *hash)
{
    sha512_sw *s = container_of(hash, sha512_sw, hash);

    smemclr(s, sizeof(*s));
    sfree(s);
}

static void sha512_sw_write(BinarySink *bs, const void *vp, size_t len)
{
    sha512_sw *s = BinarySink_DOWNCAST(bs, sha512_sw);

    while (len > 0)
        if (sha512_block_write(&s->blk, &vp, &len))
            sha512_sw_block(s->core, s->blk.block);
}

static void sha512_sw_digest(ssh_hash *hash, uint8_t *digest)
{
    sha512_sw *s = container_of(hash, sha512_sw, hash);

    sha512_block_pad(&s->blk, BinarySink_UPCAST(s));
    for (size_t i = 0; i < hash->vt->hlen / 8; i++)
        PUT_64BIT_MSB_FIRST(digest + 8*i, s->core[i]);
}

const ssh_hashalg ssh_sha512 = {
    .new = sha512_sw_new,
    .reset = sha512_sw_reset,
    .copyfrom = sha512_sw_copyfrom,
    .digest = sha512_sw_digest,
    .free = sha512_sw_free,
    .hlen = 64,
    .blocklen = 128,
    HASHALG_NAMES_ANNOTATED("SHA-512", "unaccelerated"),
    .extra = sha512_initial_state,
};

const ssh_hashalg ssh_sha384 = {
    .new = sha512_sw_new,
    .reset = sha512_sw_reset,
    .copyfrom = sha512_sw_copyfrom,
    .digest = sha512_sw_digest,
    .free = sha512_sw_free,
    .hlen = 48,
    .blocklen = 128,
    HASHALG_NAMES_ANNOTATED("SHA-384", "unaccelerated"),
    .extra = sha384_initial_state,
};
