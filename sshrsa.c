/*
 * RSA implementation just sufficient for ssh client-side
 * initialisation step
 *
 * Rewritten for more speed by Joris van Rantwijk, Jun 1999.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ssh.h"

typedef unsigned short *Bignum;

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

static Bignum newbn(int length) {
    Bignum b = malloc((length+1)*sizeof(unsigned short));
    if (!b)
	abort();		       /* FIXME */
    b[0] = length;
    return b;
}

static void freebn(Bignum b) {
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
static void modpow(Bignum base, Bignum exp, Bignum mod, Bignum result)
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

int makekey(unsigned char *data, struct RSAKey *result,
	    unsigned char **keystr) {
    unsigned char *p = data;
    Bignum bn[2];
    int i, j;
    int w, b;

    result->bits = 0;
    for (i=0; i<4; i++)
	result->bits = (result->bits << 8) + *p++;

    for (j=0; j<2; j++) {

	w = 0;
	for (i=0; i<2; i++)
	    w = (w << 8) + *p++;

	result->bytes = b = (w+7)/8;   /* bits -> bytes */
	w = (w+15)/16;		       /* bits -> words */

	bn[j] = newbn(w);

	if (keystr) *keystr = p;       /* point at key string, second time */

	for (i=1; i<=w; i++)
	    bn[j][i] = 0;
	for (i=b; i-- ;) {
	    unsigned char byte = *p++;
	    if (i & 1)
		bn[j][1+i/2] |= byte<<8;
	    else
		bn[j][1+i/2] |= byte;
	}

	debug(bn[j]);

    }

    result->exponent = bn[0];
    result->modulus = bn[1];

    return p - data;
}

void rsaencrypt(unsigned char *data, int length, struct RSAKey *key) {
    Bignum b1, b2;
    int w, i;
    unsigned char *p;

    debug(key->exponent);

    memmove(data+key->bytes-length, data, length);
    data[0] = 0;
    data[1] = 2;

    for (i = 2; i < key->bytes-length-1; i++) {
	do {
	    data[i] = random_byte();
	} while (data[i] == 0);
    }
    data[key->bytes-length-1] = 0;

    w = (key->bytes+1)/2;

    b1 = newbn(w);
    b2 = newbn(w);

    p = data;
    for (i=1; i<=w; i++)
	b1[i] = 0;
    for (i=key->bytes; i-- ;) {
	unsigned char byte = *p++;
	if (i & 1)
	    b1[1+i/2] |= byte<<8;
	else
	    b1[1+i/2] |= byte;
    }

    debug(b1);

    modpow(b1, key->exponent, key->modulus, b2);

    debug(b2);

    p = data;
    for (i=key->bytes; i-- ;) {
	unsigned char b;
	if (i & 1)
	    b = b2[1+i/2] >> 8;
	else
	    b = b2[1+i/2] & 0xFF;
	*p++ = b;
    }

    freebn(b1);
    freebn(b2);
}

int rsastr_len(struct RSAKey *key) {
    Bignum md, ex;

    md = key->modulus;
    ex = key->exponent;
    return 4 * (ex[0]+md[0]) + 10;
}

void rsastr_fmt(char *str, struct RSAKey *key) {
    Bignum md, ex;
    int len = 0, i;

    md = key->modulus;
    ex = key->exponent;

    for (i=1; i<=ex[0]; i++) {
	sprintf(str+len, "%04x", ex[i]);
	len += strlen(str+len);
    }
    str[len++] = '/';
    for (i=1; i<=md[0]; i++) {
	sprintf(str+len, "%04x", md[i]);
	len += strlen(str+len);
    }
    str[len] = '\0';
}

#ifdef TESTMODE

#ifndef NODDY
#define p1 10007
#define p2 10069
#define p3 10177
#else
#define p1 3
#define p2 7
#define p3 13
#endif

unsigned short P1[2] = { 1, p1 };
unsigned short P2[2] = { 1, p2 };
unsigned short P3[2] = { 1, p3 };
unsigned short bigmod[5] = { 4, 0, 0, 0, 32768U };
unsigned short mod[5] = { 4, 0, 0, 0, 0 };
unsigned short a[5] = { 4, 0, 0, 0, 0 };
unsigned short b[5] = { 4, 0, 0, 0, 0 };
unsigned short c[5] = { 4, 0, 0, 0, 0 };
unsigned short One[2] = { 1, 1 };
unsigned short Two[2] = { 1, 2 };

int main(void) {
    modmult(P1, P2, bigmod, a);   debug(a);
    modmult(a, P3, bigmod, mod);  debug(mod);

    sub(P1, One, a);              debug(a);
    sub(P2, One, b);              debug(b);
    modmult(a, b, bigmod, c);     debug(c);
    sub(P3, One, a);              debug(a);
    modmult(a, c, bigmod, b);     debug(b);

    modpow(Two, b, mod, a);       debug(a);

    return 0;
}

#endif
