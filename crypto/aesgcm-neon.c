/*
 * Implementation of the GCM polynomial hash using Arm NEON vector
 * intrinsics, in particular the multiplication operation for
 * polynomials over GF(2).
 *
 * Follows the reference implementation in aesgcm-ref-poly.c; see
 * there for comments on the underlying technique. Here the comments
 * just discuss the NEON-specific details.
 */

#include "ssh.h"
#include "aesgcm.h"

#if USE_ARM64_NEON_H
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif

typedef struct aesgcm_neon {
    AESGCM_COMMON_FIELDS;
    poly128_t var, acc, mask;
} aesgcm_neon;

static bool aesgcm_neon_available(void)
{
    return platform_pmull_neon_available();
}

/*
 * The NEON types involved are:
 *
 * 'poly128_t' is a type that lives in a 128-bit vector register and
 * represents a 128-bit polynomial over GF(2)
 *
 * 'poly64x2_t' is a type that lives in a 128-bit vector register and
 * represents a vector of two 64-bit polynomials. These appear as
 * intermediate results in some of the helper functions below, but we
 * never need to actually have a variable of that type.
 *
 * 'poly64x1_t' is a type that lives in a 128-bit vector register and
 * represents a vector of one 64-bit polynomial.
 *
 * That is distinct from 'poly64_t', which is a type that lives in
 * ordinary scalar registers and is a typedef for an integer type.
 *
 * Generally here we try to work in terms of poly128_t and 64-bit
 * integer types, and let everything else be handled as internal
 * details of these helper functions.
 */

/* Make a poly128_t from two halves */
static inline poly128_t create_p128(poly64_t hi, poly64_t lo)
{
    return vreinterpretq_p128_p64(
        vcombine_p64(vcreate_p64(lo), vcreate_p64(hi)));
}

/* Retrieve the high and low halves of a poly128_t */
static inline poly64_t hi_half(poly128_t v)
{
    return vgetq_lane_p64(vreinterpretq_p64_p128(v), 1);
}
static inline poly64_t lo_half(poly128_t v)
{
    return vgetq_lane_p64(vreinterpretq_p64_p128(v), 0);
}

/* 64x64 -> 128 bit polynomial multiplication, the largest we can do
 * in one CPU operation */
static inline poly128_t pmul(poly64_t v, poly64_t w)
{
    return vmull_p64(v, w);
}

/* Load and store a poly128_t in the form of big-endian bytes. This
 * involves separately swapping the halves of the register and
 * reversing the bytes within each half. */
static inline poly128_t load_p128_be(const void *p)
{
    poly128_t swapped = vreinterpretq_p128_u8(vrev64q_u8(vld1q_u8(p)));
    return create_p128(lo_half(swapped), hi_half(swapped));
}
static inline void store_p128_be(void *p, poly128_t v)
{
    poly128_t swapped = create_p128(lo_half(v), hi_half(v));
    vst1q_u8(p, vrev64q_u8(vreinterpretq_u8_p128(swapped)));
}

#if !HAVE_NEON_VADDQ_P128
static inline poly128_t vaddq_p128(poly128_t a, poly128_t b)
{
    return vreinterpretq_p128_u32(veorq_u32(
        vreinterpretq_u32_p128(a), vreinterpretq_u32_p128(b)));
}
#endif

/*
 * Key setup is just like in aesgcm-ref-poly.c. There's no point using
 * vector registers to accelerate this, because it happens rarely.
 */
static void aesgcm_neon_setkey_impl(aesgcm_neon *ctx, const unsigned char *var)
{
    uint64_t hi = GET_64BIT_MSB_FIRST(var);
    uint64_t lo = GET_64BIT_MSB_FIRST(var + 8);

    uint64_t bit = 1 & (hi >> 63);
    hi = (hi << 1) ^ (lo >> 63);
    lo = (lo << 1) ^ bit;
    hi ^= 0xC200000000000000 & -bit;

    ctx->var = create_p128(hi, lo);
}

static inline void aesgcm_neon_setup(aesgcm_neon *ctx,
                                     const unsigned char *mask)
{
    ctx->mask = load_p128_be(mask);
    ctx->acc = create_p128(0, 0);
}

/*
 * Folding a coefficient into the accumulator is done by exactly the
 * algorithm in aesgcm-ref-poly.c, translated line by line.
 *
 * It's possible that this could be improved by some clever manoeuvres
 * that avoid having to break vectors in half and put them together
 * again. Patches welcome if anyone has better ideas.
 */
static inline void aesgcm_neon_coeff(aesgcm_neon *ctx,
                                     const unsigned char *coeff)
{
    ctx->acc = vaddq_p128(ctx->acc, load_p128_be(coeff));

    poly64_t ah = hi_half(ctx->acc), al = lo_half(ctx->acc);
    poly64_t bh = hi_half(ctx->var), bl = lo_half(ctx->var);
    poly128_t md = pmul(ah ^ al, bh ^ bl);
    poly128_t lo = pmul(al, bl);
    poly128_t hi = pmul(ah, bh);
    md = vaddq_p128(md, vaddq_p128(hi, lo));
    hi = create_p128(hi_half(hi), lo_half(hi) ^ hi_half(md));
    lo = create_p128(hi_half(lo) ^ lo_half(md), lo_half(lo));

    poly128_t r1 = pmul((poly64_t)0xC200000000000000, lo_half(lo));
    hi = create_p128(hi_half(hi), lo_half(hi) ^ lo_half(lo) ^ hi_half(r1));
    lo = create_p128(hi_half(lo) ^ lo_half(r1), lo_half(lo));

    poly128_t r2 = pmul((poly64_t)0xC200000000000000, hi_half(lo));
    hi = vaddq_p128(hi, r2);
    hi = create_p128(hi_half(hi) ^ hi_half(lo), lo_half(hi));

    ctx->acc = hi;
}

static inline void aesgcm_neon_output(aesgcm_neon *ctx, unsigned char *output)
{
    store_p128_be(output, vaddq_p128(ctx->acc, ctx->mask));
    ctx->acc = create_p128(0, 0);
    ctx->mask = create_p128(0, 0);
}

#define AESGCM_FLAVOUR neon
#define AESGCM_NAME "NEON accelerated"
#include "aesgcm-footer.h"
