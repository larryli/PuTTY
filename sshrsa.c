/*
 * RSA implementation just sufficient for ssh client-side
 * initialisation step
 *
 * Rewritten for more speed by Joris van Rantwijk, Jun 1999.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#include "ssh.h"

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
