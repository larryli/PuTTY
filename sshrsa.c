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


int makekey(unsigned char *data, struct RSAKey *result,
	    unsigned char **keystr, int order) {
    unsigned char *p = data;
    int i;

    if (result) {
        result->bits = 0;
        for (i=0; i<4; i++)
            result->bits = (result->bits << 8) + *p++;
    } else
        p += 4;

    /*
     * order=0 means exponent then modulus (the keys sent by the
     * server). order=1 means modulus then exponent (the keys
     * stored in a keyfile).
     */

    if (order == 0)
        p += ssh1_read_bignum(p, result ? &result->exponent : NULL);
    if (result)
        result->bytes = (((p[0] << 8) + p[1]) + 7) / 8;
    if (keystr) *keystr = p+2;
    p += ssh1_read_bignum(p, result ? &result->modulus : NULL);
    if (order == 1)
        p += ssh1_read_bignum(p, result ? &result->exponent : NULL);

    return p - data;
}

int makeprivate(unsigned char *data, struct RSAKey *result) {
    return ssh1_read_bignum(data, &result->private_exponent);
}

void rsaencrypt(unsigned char *data, int length, struct RSAKey *key) {
    Bignum b1, b2;
    int i;
    unsigned char *p;

    memmove(data+key->bytes-length, data, length);
    data[0] = 0;
    data[1] = 2;

    for (i = 2; i < key->bytes-length-1; i++) {
	do {
	    data[i] = random_byte();
	} while (data[i] == 0);
    }
    data[key->bytes-length-1] = 0;

    b1 = bignum_from_bytes(data, key->bytes);

    b2 = modpow(b1, key->exponent, key->modulus);

    p = data;
    for (i=key->bytes; i-- ;) {
        *p++ = bignum_byte(b2, i);
    }

    freebn(b1);
    freebn(b2);
}

Bignum rsadecrypt(Bignum input, struct RSAKey *key) {
    Bignum ret;
    ret = modpow(input, key->private_exponent, key->modulus);
    return ret;
}

int rsastr_len(struct RSAKey *key) {
    Bignum md, ex;
    int mdlen, exlen;

    md = key->modulus;
    ex = key->exponent;
    mdlen = (ssh1_bignum_bitcount(md)+15) / 16;
    exlen = (ssh1_bignum_bitcount(ex)+15) / 16;
    return 4 * (mdlen+exlen) + 20;
}

void rsastr_fmt(char *str, struct RSAKey *key) {
    Bignum md, ex;
    int len = 0, i, nibbles;
    static const char hex[] = "0123456789abcdef";

    md = key->modulus;
    ex = key->exponent;

    len += sprintf(str+len, "0x");

    nibbles = (3 + ssh1_bignum_bitcount(ex))/4; if (nibbles<1) nibbles=1;
    for (i=nibbles; i-- ;)
        str[len++] = hex[(bignum_byte(ex, i/2) >> (4*(i%2))) & 0xF];

    len += sprintf(str+len, ",0x");

    nibbles = (3 + ssh1_bignum_bitcount(md))/4; if (nibbles<1) nibbles=1;
    for (i=nibbles; i-- ;)
        str[len++] = hex[(bignum_byte(md, i/2) >> (4*(i%2))) & 0xF];

    str[len] = '\0';
}

/*
 * Generate a fingerprint string for the key. Compatible with the
 * OpenSSH fingerprint code.
 */
void rsa_fingerprint(char *str, int len, struct RSAKey *key) {
    struct MD5Context md5c;
    unsigned char digest[16];
    char buffer[16*3+40];
    int numlen, slen, i;

    MD5Init(&md5c);
    numlen = ssh1_bignum_length(key->modulus) - 2;
    for (i = numlen; i-- ;) {
        unsigned char c = bignum_byte(key->modulus, i);
        MD5Update(&md5c, &c, 1);
    }
    numlen = ssh1_bignum_length(key->exponent) - 2;
    for (i = numlen; i-- ;) {
        unsigned char c = bignum_byte(key->exponent, i);
        MD5Update(&md5c, &c, 1);
    }
    MD5Final(digest, &md5c);

    sprintf(buffer, "%d ", ssh1_bignum_bitcount(key->modulus));
    for (i = 0; i < 16; i++)
        sprintf(buffer+strlen(buffer), "%s%02x", i?":":"", digest[i]);
    strncpy(str, buffer, len); str[len-1] = '\0';
    slen = strlen(str);
    if (key->comment && slen < len-1) {
        str[slen] = ' ';
        strncpy(str+slen+1, key->comment, len-slen-1);
        str[len-1] = '\0';
    }
}

void freersakey(struct RSAKey *key) {
    if (key->modulus) freebn(key->modulus);
    if (key->exponent) freebn(key->exponent);
    if (key->private_exponent) freebn(key->private_exponent);
    if (key->comment) sfree(key->comment);
}
