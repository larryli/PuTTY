/*
 * Bignum routines for RSA and DH and stuff.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ssh.h"

static unsigned short Zero[1] = { 0 };

#if defined TESTMODE || defined RSADEBUG
#ifndef DLVL
#define DLVL 10000
#endif
#define debug(x) bndebug(#x,x)
static int level = 0;
static void bndebug(char *name, Bignum b) {
    int i;
    int w = 50-level-strlen(name)-5*b[0];
    if (level >= DLVL)
	return;
    if (w < 0) w = 0;
    dprintf("%*s%s%*s", level, "", name, w, "");
    for (i=b[0]; i>0; i--)
	dprintf(" %04x", b[i]);
    dprintf("\n");
}
#define dmsg(x) do {if(level<DLVL){dprintf("%*s",level,"");printf x;}} while(0)
#define enter(x) do { dmsg(x); level += 4; } while(0)
#define leave(x) do { level -= 4; dmsg(x); } while(0)
#else
#define debug(x)
#define dmsg(x)
#define enter(x)
#define leave(x)
#endif

Bignum newbn(int length) {
    Bignum b = malloc((length+1)*sizeof(unsigned short));
    if (!b)
	abort();		       /* FIXME */
    memset(b, 0, (length+1)*sizeof(*b));
    b[0] = length;
    return b;
}

void freebn(Bignum b) {
    /*
     * Burn the evidence, just in case.
     */
    memset(b, 0, sizeof(b[0]) * (b[0] + 1));
    free(b);
}

/*
 * Compute c = a * b.
 * Input is in the first len words of a and b.
 * Result is returned in the first 2*len words of c.
 */
static void bigmul(unsigned short *a, unsigned short *b, unsigned short *c,
                   int len)
{
    int i, j;
    unsigned long ai, t;

    for (j = len - 1; j >= 0; j--)
	c[j+len] = 0;

    for (i = len - 1; i >= 0; i--) {
	ai = a[i];
	t = 0;
	for (j = len - 1; j >= 0; j--) {
	    t += ai * (unsigned long) b[j];
	    t += (unsigned long) c[i+j+1];
	    c[i+j+1] = (unsigned short)t;
	    t = t >> 16;
	}
	c[i] = (unsigned short)t;
    }
}

/*
 * Compute a = a % m.
 * Input in first 2*len words of a and first len words of m.
 * Output in first 2*len words of a (of which first len words will be zero).
 * The MSW of m MUST have its high bit set.
 */
static void bigmod(unsigned short *a, unsigned short *m, int len)
{
    unsigned short m0, m1;
    unsigned int h;
    int i, k;

    /* Special case for len == 1 */
    if (len == 1) {
	a[1] = (((long) a[0] << 16) + a[1]) % m[0];
	a[0] = 0;
	return;
    }

    m0 = m[0];
    m1 = m[1];

    for (i = 0; i <= len; i++) {
	unsigned long t;
	unsigned int q, r, c;

	if (i == 0) {
	    h = 0;
	} else {
	    h = a[i-1];
	    a[i-1] = 0;
	}

	/* Find q = h:a[i] / m0 */
	t = ((unsigned long) h << 16) + a[i];
	q = t / m0;
	r = t % m0;

	/* Refine our estimate of q by looking at
	 h:a[i]:a[i+1] / m0:m1 */
	t = (long) m1 * (long) q;
	if (t > ((unsigned long) r << 16) + a[i+1]) {
	    q--;
	    t -= m1;
	    r = (r + m0) & 0xffff; /* overflow? */
	    if (r >= (unsigned long)m0 &&
                t > ((unsigned long) r << 16) + a[i+1])
		q--;
	}

	/* Substract q * m from a[i...] */
	c = 0;
	for (k = len - 1; k >= 0; k--) {
	    t = (long) q * (long) m[k];
	    t += c;
	    c = t >> 16;
	    if ((unsigned short) t > a[i+k]) c++;
	    a[i+k] -= (unsigned short) t;
	}

	/* Add back m in case of borrow */
	if (c != h) {
	    t = 0;
	    for (k = len - 1; k >= 0; k--) {
		t += m[k];
		t += a[i+k];
		a[i+k] = (unsigned short)t;
		t = t >> 16;
	    }
	}
    }
}

/*
 * Compute (base ^ exp) % mod.
 * The base MUST be smaller than the modulus.
 * The most significant word of mod MUST be non-zero.
 * We assume that the result array is the same size as the mod array.
 */
void modpow(Bignum base, Bignum exp, Bignum mod, Bignum result)
{
    unsigned short *a, *b, *n, *m;
    int mshift;
    int mlen, i, j;

    /* Allocate m of size mlen, copy mod to m */
    /* We use big endian internally */
    mlen = mod[0];
    m = malloc(mlen * sizeof(unsigned short));
    for (j = 0; j < mlen; j++) m[j] = mod[mod[0] - j];

    /* Shift m left to make msb bit set */
    for (mshift = 0; mshift < 15; mshift++)
	if ((m[0] << mshift) & 0x8000) break;
    if (mshift) {
	for (i = 0; i < mlen - 1; i++)
	    m[i] = (m[i] << mshift) | (m[i+1] >> (16-mshift));
	m[mlen-1] = m[mlen-1] << mshift;
    }

    /* Allocate n of size mlen, copy base to n */
    n = malloc(mlen * sizeof(unsigned short));
    i = mlen - base[0];
    for (j = 0; j < i; j++) n[j] = 0;
    for (j = 0; j < base[0]; j++) n[i+j] = base[base[0] - j];

    /* Allocate a and b of size 2*mlen. Set a = 1 */
    a = malloc(2 * mlen * sizeof(unsigned short));
    b = malloc(2 * mlen * sizeof(unsigned short));
    for (i = 0; i < 2*mlen; i++) a[i] = 0;
    a[2*mlen-1] = 1;

    /* Skip leading zero bits of exp. */
    i = 0; j = 15;
    while (i < exp[0] && (exp[exp[0] - i] & (1 << j)) == 0) {
	j--;
	if (j < 0) { i++; j = 15; }
    }

    /* Main computation */
    while (i < exp[0]) {
	while (j >= 0) {
	    bigmul(a + mlen, a + mlen, b, mlen);
	    bigmod(b, m, mlen);
	    if ((exp[exp[0] - i] & (1 << j)) != 0) {
		bigmul(b + mlen, n, a, mlen);
		bigmod(a, m, mlen);
	    } else {
		unsigned short *t;
		t = a;  a = b;  b = t;
	    }
	    j--;
	}
	i++; j = 15;
    }

    /* Fixup result in case the modulus was shifted */
    if (mshift) {
	for (i = mlen - 1; i < 2*mlen - 1; i++)
	    a[i] = (a[i] << mshift) | (a[i+1] >> (16-mshift));
	a[2*mlen-1] = a[2*mlen-1] << mshift;
	bigmod(a, m, mlen);
	for (i = 2*mlen - 1; i >= mlen; i--)
	    a[i] = (a[i] >> mshift) | (a[i-1] << (16-mshift));
    }

    /* Copy result to buffer */
    for (i = 0; i < mlen; i++)
	result[result[0] - i] = a[i+mlen];

    /* Free temporary arrays */
    for (i = 0; i < 2*mlen; i++) a[i] = 0; free(a);
    for (i = 0; i < 2*mlen; i++) b[i] = 0; free(b);
    for (i = 0; i < mlen; i++) m[i] = 0; free(m);
    for (i = 0; i < mlen; i++) n[i] = 0; free(n);
}
