/*
 * sshbn.h: the assorted conditional definitions of BignumInt and
 * multiply macros used throughout the bignum code to treat numbers as
 * arrays of the most conveniently sized word for the target machine.
 * Exported so that other code (e.g. poly1305) can use it too.
 */

#if defined __SIZEOF_INT128__
/* gcc and clang both provide a __uint128_t type on 64-bit targets
 * (and, when they do, indicate its presence by the above macro),
 * using the same 'two machine registers' kind of code generation that
 * 32-bit targets use for 64-bit ints. If we have one of these, we can
 * use a 64-bit BignumInt and a 128-bit BignumDblInt. */
typedef unsigned long long BignumInt;
typedef __uint128_t BignumDblInt;
#define BIGNUM_INT_MASK  0xFFFFFFFFFFFFFFFFULL
#define BIGNUM_TOP_BIT   0x8000000000000000ULL
#define BIGNUM_INT_BITS  64
#define MUL_WORD(w1, w2) ((BignumDblInt)w1 * w2)
#elif defined __GNUC__ && defined __i386__
typedef unsigned long BignumInt;
typedef unsigned long long BignumDblInt;
#define BIGNUM_INT_MASK  0xFFFFFFFFUL
#define BIGNUM_TOP_BIT   0x80000000UL
#define BIGNUM_INT_BITS  32
#define MUL_WORD(w1, w2) ((BignumDblInt)w1 * w2)
#elif defined _MSC_VER && defined _M_IX86
typedef unsigned __int32 BignumInt;
typedef unsigned __int64 BignumDblInt;
#define BIGNUM_INT_MASK  0xFFFFFFFFUL
#define BIGNUM_TOP_BIT   0x80000000UL
#define BIGNUM_INT_BITS  32
#define MUL_WORD(w1, w2) ((BignumDblInt)w1 * w2)
#elif defined _LP64
/* 64-bit architectures can do 32x32->64 chunks at a time */
typedef unsigned int BignumInt;
typedef unsigned long BignumDblInt;
#define BIGNUM_INT_MASK  0xFFFFFFFFU
#define BIGNUM_TOP_BIT   0x80000000U
#define BIGNUM_INT_BITS  32
#define MUL_WORD(w1, w2) ((BignumDblInt)w1 * w2)
#elif defined _LLP64
/* 64-bit architectures in which unsigned long is 32 bits, not 64 */
typedef unsigned long BignumInt;
typedef unsigned long long BignumDblInt;
#define BIGNUM_INT_MASK  0xFFFFFFFFUL
#define BIGNUM_TOP_BIT   0x80000000UL
#define BIGNUM_INT_BITS  32
#define MUL_WORD(w1, w2) ((BignumDblInt)w1 * w2)
#else
/* Fallback for all other cases */
typedef unsigned short BignumInt;
typedef unsigned long BignumDblInt;
#define BIGNUM_INT_MASK  0xFFFFU
#define BIGNUM_TOP_BIT   0x8000U
#define BIGNUM_INT_BITS  16
#define MUL_WORD(w1, w2) ((BignumDblInt)w1 * w2)
#endif

#define BIGNUM_INT_BYTES (BIGNUM_INT_BITS / 8)
