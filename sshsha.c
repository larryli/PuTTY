/*
 * The following code was taken directly from drivers/char/random.c
 * in the Linux kernel.
 */

#include "ssh.h"

/*
 * SHA transform algorithm, taken from code written by Peter Gutman,
 * and apparently in the public domain.
 */

/* The SHA f()-functions.  */

#define f1(x,y,z)   ( z ^ ( x & ( y ^ z ) ) )           /* Rounds  0-19 */
#define f2(x,y,z)   ( x ^ y ^ z )                       /* Rounds 20-39 */
#define f3(x,y,z)   ( ( x & y ) | ( z & ( x | y ) ) )   /* Rounds 40-59 */
#define f4(x,y,z)   ( x ^ y ^ z )                       /* Rounds 60-79 */

/* The SHA Mysterious Constants */

#define K1  0x5A827999L                                 /* Rounds  0-19 */
#define K2  0x6ED9EBA1L                                 /* Rounds 20-39 */
#define K3  0x8F1BBCDCL                                 /* Rounds 40-59 */
#define K4  0xCA62C1D6L                                 /* Rounds 60-79 */

#define ROTL(n,X)  ( ( ( X ) << n ) | ( ( X ) >> ( 32 - n ) ) )

#define expand(W,i) ( W[ i & 15 ] = \
		     ROTL( 1, ( W[ i & 15 ] ^ W[ (i - 14) & 15 ] ^ \
			        W[ (i - 8) & 15 ] ^ W[ (i - 3) & 15 ] ) ) )

#define subRound(a, b, c, d, e, f, k, data) \
    ( e += ROTL( 5, a ) + f( b, c, d ) + k + data, b = ROTL( 30, b ) )


void SHATransform(word32 *digest, word32 *data)
{
    word32 A, B, C, D, E;     /* Local vars */
    word32 eData[ 16 ];       /* Expanded data */

    /* Set up first buffer and local data buffer */
    A = digest[ 0 ];
    B = digest[ 1 ];
    C = digest[ 2 ];
    D = digest[ 3 ];
    E = digest[ 4 ];
    memcpy( eData, data, 16*sizeof(word32));

    /* Heavy mangling, in 4 sub-rounds of 20 iterations each. */
    subRound( A, B, C, D, E, f1, K1, eData[  0 ] );
    subRound( E, A, B, C, D, f1, K1, eData[  1 ] );
    subRound( D, E, A, B, C, f1, K1, eData[  2 ] );
    subRound( C, D, E, A, B, f1, K1, eData[  3 ] );
    subRound( B, C, D, E, A, f1, K1, eData[  4 ] );
    subRound( A, B, C, D, E, f1, K1, eData[  5 ] );
    subRound( E, A, B, C, D, f1, K1, eData[  6 ] );
    subRound( D, E, A, B, C, f1, K1, eData[  7 ] );
    subRound( C, D, E, A, B, f1, K1, eData[  8 ] );
    subRound( B, C, D, E, A, f1, K1, eData[  9 ] );
    subRound( A, B, C, D, E, f1, K1, eData[ 10 ] );
    subRound( E, A, B, C, D, f1, K1, eData[ 11 ] );
    subRound( D, E, A, B, C, f1, K1, eData[ 12 ] );
    subRound( C, D, E, A, B, f1, K1, eData[ 13 ] );
    subRound( B, C, D, E, A, f1, K1, eData[ 14 ] );
    subRound( A, B, C, D, E, f1, K1, eData[ 15 ] );
    subRound( E, A, B, C, D, f1, K1, expand( eData, 16 ) );
    subRound( D, E, A, B, C, f1, K1, expand( eData, 17 ) );
    subRound( C, D, E, A, B, f1, K1, expand( eData, 18 ) );
    subRound( B, C, D, E, A, f1, K1, expand( eData, 19 ) );

    subRound( A, B, C, D, E, f2, K2, expand( eData, 20 ) );
    subRound( E, A, B, C, D, f2, K2, expand( eData, 21 ) );
    subRound( D, E, A, B, C, f2, K2, expand( eData, 22 ) );
    subRound( C, D, E, A, B, f2, K2, expand( eData, 23 ) );
    subRound( B, C, D, E, A, f2, K2, expand( eData, 24 ) );
    subRound( A, B, C, D, E, f2, K2, expand( eData, 25 ) );
    subRound( E, A, B, C, D, f2, K2, expand( eData, 26 ) );
    subRound( D, E, A, B, C, f2, K2, expand( eData, 27 ) );
    subRound( C, D, E, A, B, f2, K2, expand( eData, 28 ) );
    subRound( B, C, D, E, A, f2, K2, expand( eData, 29 ) );
    subRound( A, B, C, D, E, f2, K2, expand( eData, 30 ) );
    subRound( E, A, B, C, D, f2, K2, expand( eData, 31 ) );
    subRound( D, E, A, B, C, f2, K2, expand( eData, 32 ) );
    subRound( C, D, E, A, B, f2, K2, expand( eData, 33 ) );
    subRound( B, C, D, E, A, f2, K2, expand( eData, 34 ) );
    subRound( A, B, C, D, E, f2, K2, expand( eData, 35 ) );
    subRound( E, A, B, C, D, f2, K2, expand( eData, 36 ) );
    subRound( D, E, A, B, C, f2, K2, expand( eData, 37 ) );
    subRound( C, D, E, A, B, f2, K2, expand( eData, 38 ) );
    subRound( B, C, D, E, A, f2, K2, expand( eData, 39 ) );

    subRound( A, B, C, D, E, f3, K3, expand( eData, 40 ) );
    subRound( E, A, B, C, D, f3, K3, expand( eData, 41 ) );
    subRound( D, E, A, B, C, f3, K3, expand( eData, 42 ) );
    subRound( C, D, E, A, B, f3, K3, expand( eData, 43 ) );
    subRound( B, C, D, E, A, f3, K3, expand( eData, 44 ) );
    subRound( A, B, C, D, E, f3, K3, expand( eData, 45 ) );
    subRound( E, A, B, C, D, f3, K3, expand( eData, 46 ) );
    subRound( D, E, A, B, C, f3, K3, expand( eData, 47 ) );
    subRound( C, D, E, A, B, f3, K3, expand( eData, 48 ) );
    subRound( B, C, D, E, A, f3, K3, expand( eData, 49 ) );
    subRound( A, B, C, D, E, f3, K3, expand( eData, 50 ) );
    subRound( E, A, B, C, D, f3, K3, expand( eData, 51 ) );
    subRound( D, E, A, B, C, f3, K3, expand( eData, 52 ) );
    subRound( C, D, E, A, B, f3, K3, expand( eData, 53 ) );
    subRound( B, C, D, E, A, f3, K3, expand( eData, 54 ) );
    subRound( A, B, C, D, E, f3, K3, expand( eData, 55 ) );
    subRound( E, A, B, C, D, f3, K3, expand( eData, 56 ) );
    subRound( D, E, A, B, C, f3, K3, expand( eData, 57 ) );
    subRound( C, D, E, A, B, f3, K3, expand( eData, 58 ) );
    subRound( B, C, D, E, A, f3, K3, expand( eData, 59 ) );

    subRound( A, B, C, D, E, f4, K4, expand( eData, 60 ) );
    subRound( E, A, B, C, D, f4, K4, expand( eData, 61 ) );
    subRound( D, E, A, B, C, f4, K4, expand( eData, 62 ) );
    subRound( C, D, E, A, B, f4, K4, expand( eData, 63 ) );
    subRound( B, C, D, E, A, f4, K4, expand( eData, 64 ) );
    subRound( A, B, C, D, E, f4, K4, expand( eData, 65 ) );
    subRound( E, A, B, C, D, f4, K4, expand( eData, 66 ) );
    subRound( D, E, A, B, C, f4, K4, expand( eData, 67 ) );
    subRound( C, D, E, A, B, f4, K4, expand( eData, 68 ) );
    subRound( B, C, D, E, A, f4, K4, expand( eData, 69 ) );
    subRound( A, B, C, D, E, f4, K4, expand( eData, 70 ) );
    subRound( E, A, B, C, D, f4, K4, expand( eData, 71 ) );
    subRound( D, E, A, B, C, f4, K4, expand( eData, 72 ) );
    subRound( C, D, E, A, B, f4, K4, expand( eData, 73 ) );
    subRound( B, C, D, E, A, f4, K4, expand( eData, 74 ) );
    subRound( A, B, C, D, E, f4, K4, expand( eData, 75 ) );
    subRound( E, A, B, C, D, f4, K4, expand( eData, 76 ) );
    subRound( D, E, A, B, C, f4, K4, expand( eData, 77 ) );
    subRound( C, D, E, A, B, f4, K4, expand( eData, 78 ) );
    subRound( B, C, D, E, A, f4, K4, expand( eData, 79 ) );

    /* Build message digest */
    digest[ 0 ] += A;
    digest[ 1 ] += B;
    digest[ 2 ] += C;
    digest[ 3 ] += D;
    digest[ 4 ] += E;
}
