/*
 * SHA core transform algorithm, used here solely as a `stirring'
 * function for the PuTTY random number pool. Implemented directly
 * from the specification by Simon Tatham.
 */

#include "ssh.h"

#define rol(x,y) ( ((x) << (y)) | (((word32)x) >> (32-y)) )

void SHATransform(word32 *digest, word32 *block) {
    word32 w[80];
    word32 a,b,c,d,e;
    int t;

    for (t = 0; t < 16; t++)
        w[t] = block[t];

    for (t = 16; t < 80; t++) {
        word32 tmp = w[t-3] ^ w[t-8] ^ w[t-14] ^ w[t-16];
        w[t] = rol(tmp, 1);
    }

    a = digest[0];
    b = digest[1];
    c = digest[2];
    d = digest[3];
    e = digest[4];

    for (t = 0; t < 20; t++) {
        word32 tmp = rol(a, 5) + ( (b&c) | (d&~b) ) + e + w[t] + 0x5a827999;
        e = d; d = c; c = rol(b, 30); b = a; a = tmp;
    }
    for (t = 20; t < 40; t++) {
        word32 tmp = rol(a, 5) + (b^c^d) + e + w[t] + 0x6ed9eba1;
        e = d; d = c; c = rol(b, 30); b = a; a = tmp;
    }
    for (t = 40; t < 60; t++) {
        word32 tmp = rol(a, 5) + ( (b&c) | (b&d) | (c&d) ) + e + w[t] + 0x8f1bbcdc;
        e = d; d = c; c = rol(b, 30); b = a; a = tmp;
    }
    for (t = 60; t < 80; t++) {
        word32 tmp = rol(a, 5) + (b^c^d) + e + w[t] + 0xca62c1d6;
        e = d; d = c; c = rol(b, 30); b = a; a = tmp;
    }

    digest[0] += a;
    digest[1] += b;
    digest[2] += c;
    digest[3] += d;
    digest[4] += e;
}
