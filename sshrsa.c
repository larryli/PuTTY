/*
 * RSA implementation just sufficient for ssh client-side
 * initialisation step
 */

/*#include <windows.h>
#define RSADEBUG
#define DLVL 2
#include "stel.h"*/

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

static int msb(Bignum r) {
    int i;
    int j;
    unsigned short n;

    for (i=r[0]; i>0; i--)
	if (r[i])
	    break;

    j = (i-1)*16;
    n = r[i];
    if (n & 0xFF00) j += 8, n >>= 8;
    if (n & 0x00F0) j += 4, n >>= 4;
    if (n & 0x000C) j += 2, n >>= 2;
    if (n & 0x0002) j += 1, n >>= 1;

    return j;
}

static void add(Bignum r1, Bignum r2, Bignum result) {
    int i;
    long stuff = 0;

    enter((">add\n"));
    debug(r1);
    debug(r2);

    for (i = 1 ;; i++) {
	if (i <= r1[0])
	    stuff += r1[i];
	if (i <= r2[0])
	    stuff += r2[i];
	if (i <= result[0])
	    result[i] = stuff & 0xFFFFU;
	if (i > r1[0] && i > r2[0] && i >= result[0])
	    break;
	stuff >>= 16;
    }

    debug(result);
    leave(("<add\n"));
}

static void sub(Bignum r1, Bignum r2, Bignum result) {
    int i;
    long stuff = 0;

    enter((">sub\n"));
    debug(r1);
    debug(r2);

    for (i = 1 ;; i++) {
	if (i <= r1[0])
	    stuff += r1[i];
	if (i <= r2[0])
	    stuff -= r2[i];
	if (i <= result[0])
	    result[i] = stuff & 0xFFFFU;
	if (i > r1[0] && i > r2[0] && i >= result[0])
	    break;
	stuff = stuff<0 ? -1 : 0;
    }

    debug(result);
    leave(("<sub\n"));
}

static int ge(Bignum r1, Bignum r2) {
    int i;

    enter((">ge\n"));
    debug(r1);
    debug(r2);

    if (r1[0] < r2[0])
	i = r2[0];
    else
	i = r1[0];

    while (i > 0) {
	unsigned short n1 = (i > r1[0] ? 0 : r1[i]);
	unsigned short n2 = (i > r2[0] ? 0 : r2[i]);

	if (n1 > n2) {
	    dmsg(("greater\n"));
	    leave(("<ge\n"));
	    return 1;		       /* r1 > r2 */
	} else if (n1 < n2) {
	    dmsg(("less\n"));
	    leave(("<ge\n"));
	    return 0;		       /* r1 < r2 */
	}

	i--;
    }

    dmsg(("equal\n"));
    leave(("<ge\n"));
    return 1;			       /* r1 = r2 */
}

static void modmult(Bignum r1, Bignum r2, Bignum modulus, Bignum result) {
    Bignum temp = newbn(modulus[0]+1);
    Bignum tmp2 = newbn(modulus[0]+1);
    int i;
    int bit, bits, digit, smallbit;

    enter((">modmult\n"));
    debug(r1);
    debug(r2);
    debug(modulus);

    for (i=1; i<=result[0]; i++)
	result[i] = 0;		       /* result := 0 */
    for (i=1; i<=temp[0]; i++)
	temp[i] = (i > r2[0] ? 0 : r2[i]);   /* temp := r2 */

    bits = 1+msb(r1);

    for (bit = 0; bit < bits; bit++) {
	digit = 1 + bit / 16;
	smallbit = bit % 16;

	debug(temp);
	if (digit <= r1[0] && (r1[digit] & (1<<smallbit))) {
	    dmsg(("bit %d\n", bit));
	    add(temp, result, tmp2);
	    if (ge(tmp2, modulus))
		sub(tmp2, modulus, result);
	    else
		add(tmp2, Zero, result);
	    debug(result);
	}

	add(temp, temp, tmp2);
	if (ge(tmp2, modulus))
	    sub(tmp2, modulus, temp);
	else
	    add(tmp2, Zero, temp);
    }

    freebn(temp);
    freebn(tmp2);

    debug(result);
    leave(("<modmult\n"));
}

static void modpow(Bignum r1, Bignum r2, Bignum modulus, Bignum result) {
    Bignum temp = newbn(modulus[0]+1);
    Bignum tmp2 = newbn(modulus[0]+1);
    int i;
    int bit, bits, digit, smallbit;

    enter((">modpow\n"));
    debug(r1);
    debug(r2);
    debug(modulus);

    for (i=1; i<=result[0]; i++)
	result[i] = (i==1);	       /* result := 1 */
    for (i=1; i<=temp[0]; i++)
	temp[i] = (i > r1[0] ? 0 : r1[i]);   /* temp := r1 */

    bits = 1+msb(r2);

    for (bit = 0; bit < bits; bit++) {
	digit = 1 + bit / 16;
	smallbit = bit % 16;

	debug(temp);
	if (digit <= r2[0] && (r2[digit] & (1<<smallbit))) {
	    dmsg(("bit %d\n", bit));
	    modmult(temp, result, modulus, tmp2);
	    add(tmp2, Zero, result);
	    debug(result);
	}

	modmult(temp, temp, modulus, tmp2);
	add(tmp2, Zero, temp);
    }

    freebn(temp);
    freebn(tmp2);

    debug(result);
    leave(("<modpow\n"));
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
	for (i=0; i<b; i++) {
	    unsigned char byte = *p++;
	    if ((b-i) & 1)
		bn[j][w-i/2] |= byte;
	    else
		bn[j][w-i/2] |= byte<<8;
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
    for (i=0; i<key->bytes; i++) {
	unsigned char byte = *p++;
	if ((key->bytes-i) & 1)
	    b1[w-i/2] |= byte;
	else
	    b1[w-i/2] |= byte<<8;
    }

    debug(b1);

    modpow(b1, key->exponent, key->modulus, b2);

    debug(b2);

    p = data;
    for (i=0; i<key->bytes; i++) {
	unsigned char b;
	if (i & 1)
	    b = b2[w-i/2] & 0xFF;
	else
	    b = b2[w-i/2] >> 8;
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
