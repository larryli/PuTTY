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

/* ----------------------------------------------------------------------
 * Implementation of the ssh-rsa signing key type. 
 */

#define GET_32BIT(cp) \
    (((unsigned long)(unsigned char)(cp)[0] << 24) | \
    ((unsigned long)(unsigned char)(cp)[1] << 16) | \
    ((unsigned long)(unsigned char)(cp)[2] << 8) | \
    ((unsigned long)(unsigned char)(cp)[3]))

#define PUT_32BIT(cp, value) { \
    (cp)[0] = (unsigned char)((value) >> 24); \
    (cp)[1] = (unsigned char)((value) >> 16); \
    (cp)[2] = (unsigned char)((value) >> 8); \
    (cp)[3] = (unsigned char)(value); }

static void getstring(char **data, int *datalen, char **p, int *length) {
    *p = NULL;
    if (*datalen < 4)
        return;
    *length = GET_32BIT(*data);
    *datalen -= 4; *data += 4;
    if (*datalen < *length)
        return;
    *p = *data;
    *data += *length; *datalen -= *length;
}
static Bignum getmp(char **data, int *datalen) {
    char *p;
    int length;
    Bignum b;

    getstring(data, datalen, &p, &length);
    if (!p)
        return NULL;
    b = bignum_from_bytes(p, length);
    return b;
}

static void *rsa2_newkey(char *data, int len) {
    char *p;
    int slen;
    struct RSAKey *rsa;

    rsa = smalloc(sizeof(struct RSAKey));
    if (!rsa) return NULL;
    getstring(&data, &len, &p, &slen);

    if (!p || memcmp(p, "ssh-rsa", 7)) {
	sfree(rsa);
	return NULL;
    }
    rsa->exponent = getmp(&data, &len);
    rsa->modulus = getmp(&data, &len);
    rsa->private_exponent = NULL;
    rsa->comment = NULL;

    return rsa;
}

static void rsa2_freekey(void *key) {
    struct RSAKey *rsa = (struct RSAKey *)key;
    freersakey(rsa);
    sfree(rsa);
}

static char *rsa2_fmtkey(void *key) {
    struct RSAKey *rsa = (struct RSAKey *)key;
    char *p;
    int len;
    
    len = rsastr_len(rsa);
    p = smalloc(len);
    rsastr_fmt(p, rsa);
    return p;
}

static char *rsa2_fingerprint(void *key) {
    struct RSAKey *rsa = (struct RSAKey *)key;
    struct MD5Context md5c;
    unsigned char digest[16], lenbuf[4];
    char buffer[16*3+40];
    char *ret;
    int numlen, i;

    MD5Init(&md5c);
    MD5Update(&md5c, "\0\0\0\7ssh-rsa", 11);

#define ADD_BIGNUM(bignum) \
    numlen = (ssh1_bignum_bitcount(bignum)+8)/8; \
    PUT_32BIT(lenbuf, numlen); MD5Update(&md5c, lenbuf, 4); \
    for (i = numlen; i-- ;) { \
        unsigned char c = bignum_byte(bignum, i); \
        MD5Update(&md5c, &c, 1); \
    }
    ADD_BIGNUM(rsa->exponent);
    ADD_BIGNUM(rsa->modulus);
#undef ADD_BIGNUM

    MD5Final(digest, &md5c);

    sprintf(buffer, "ssh-rsa %d ", ssh1_bignum_bitcount(rsa->modulus));
    for (i = 0; i < 16; i++)
        sprintf(buffer+strlen(buffer), "%s%02x", i?":":"", digest[i]);
    ret = smalloc(strlen(buffer)+1);
    if (ret)
        strcpy(ret, buffer);
    return ret;
}

/*
 * This is the magic ASN.1/DER prefix that goes in the decoded
 * signature, between the string of FFs and the actual SHA hash
 * value. As closely as I can tell, the meaning of it is:
 * 
 * 00 -- this marks the end of the FFs; not part of the ASN.1 bit itself
 * 
 * 30 21 -- a constructed SEQUENCE of length 0x21
 *    30 09 -- a constructed sub-SEQUENCE of length 9
 *       06 05 -- an object identifier, length 5
 *          2B 0E 03 02 1A -- 
 *       05 00 -- NULL
 *    04 14 -- a primitive OCTET STRING of length 0x14
 *       [0x14 bytes of hash data follows]
 */
static unsigned char asn1_weird_stuff[] = {
    0x00,0x30,0x21,0x30,0x09,0x06,0x05,0x2B,
    0x0E,0x03,0x02,0x1A,0x05,0x00,0x04,0x14,
};

static int rsa2_verifysig(void *key, char *sig, int siglen,
			 char *data, int datalen) {
    struct RSAKey *rsa = (struct RSAKey *)key;
    Bignum in, out;
    char *p;
    int slen;
    int bytes, i, j, ret;
    unsigned char hash[20];

    getstring(&sig, &siglen, &p, &slen);
    if (!p || slen != 7 || memcmp(p, "ssh-rsa", 7)) {
        return 0;
    }
    in = getmp(&sig, &siglen);
    out = modpow(in, rsa->exponent, rsa->modulus);
    freebn(in);

    ret = 1;

    bytes = ssh1_bignum_bitcount(rsa->modulus) / 8;
    /* Top (partial) byte should be zero. */
    if (bignum_byte(out, bytes-1) != 0)
        ret = 0;
    /* First whole byte should be 1. */
    if (bignum_byte(out, bytes-2) != 1)
        ret = 0;
    /* Most of the rest should be FF. */
    for (i = bytes-3; i >= 20 + sizeof(asn1_weird_stuff); i--) {
        if (bignum_byte(out, i) != 0xFF)
            ret = 0;
    }
    /* Then we expect to see the asn1_weird_stuff. */
    for (i = 20 + sizeof(asn1_weird_stuff) - 1, j=0; i >= 20; i--,j++) {
        if (bignum_byte(out, i) != asn1_weird_stuff[j])
            ret = 0;
    }
    /* Finally, we expect to see the SHA-1 hash of the signed data. */
    SHA_Simple(data, datalen, hash);
    for (i = 19, j=0; i >= 0; i--,j++) {
        if (bignum_byte(out, i) != hash[j])
            ret = 0;
    }

    return ret;
}

int rsa2_sign(void *key, char *sig, int siglen,
	     char *data, int datalen) {
    return 0;			       /* FIXME */
}

struct ssh_signkey ssh_rsa = {
    rsa2_newkey,
    rsa2_freekey,
    rsa2_fmtkey,
    rsa2_fingerprint,
    rsa2_verifysig,
    rsa2_sign,
    "ssh-rsa",
    "rsa2"
};
