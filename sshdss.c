#include <stdio.h>
#include <stdlib.h>

#include "ssh.h"

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

#if 0
/*
 * Condition this section in for debugging of DSS.
 */
static void diagbn(char *prefix, Bignum md) {
    int i, nibbles, morenibbles;
    static const char hex[] = "0123456789ABCDEF";

    printf("%s0x", prefix ? prefix : "");

    nibbles = (3 + ssh1_bignum_bitcount(md))/4; if (nibbles<1) nibbles=1;
    morenibbles = 4*md[0] - nibbles;
    for (i=0; i<morenibbles; i++) putchar('-');
    for (i=nibbles; i-- ;)
        putchar(hex[(bignum_byte(md, i/2) >> (4*(i%2))) & 0xF]);

    if (prefix) putchar('\n');
}
#define DEBUG_DSS
#else
#define diagbn(x,y)
#endif

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
    int i, j, length;
    Bignum b;

    getstring(data, datalen, &p, &length);
    if (!p)
        return NULL;
    if (p[0] & 0x80)
        return NULL;                   /* negative mp */
    b = newbn((length+1)/2);
    for (i = 0; i < length; i++) {
        j = length - 1 - i;
        if (j & 1)
            b[j/2+1] |= ((unsigned char)p[i]) << 8;
        else
            b[j/2+1] |= ((unsigned char)p[i]);
    }
    while (b[0] > 1 && b[b[0]] == 0) b[0]--;
    return b;
}

static Bignum get160(char **data, int *datalen) {
    char *p;
    int i, j, length;
    Bignum b;

    p = *data;
    *data += 20; *datalen -= 20;

    length = 20;
    while (length > 0 && !p[0])
        p++, length--;
    b = newbn((length+1)/2);
    for (i = 0; i < length; i++) {
        j = length - 1 - i;
        if (j & 1)
            b[j/2+1] |= ((unsigned char)p[i]) << 8;
        else
            b[j/2+1] |= ((unsigned char)p[i]);
    }
    return b;
}

static Bignum dss_p, dss_q, dss_g, dss_y;

static void dss_setkey(char *data, int len) {
    char *p;
    int slen;
    getstring(&data, &len, &p, &slen);

#ifdef DEBUG_DSS
    {
        int i;
        printf("key:");
        for (i=0;i<len;i++)
            printf("  %02x", (unsigned char)(data[i]));
        printf("\n");
    }
#endif

    if (!p || memcmp(p, "ssh-dss", 7)) {
        dss_p = NULL;
        return;
    }
    dss_p = getmp(&data, &len);
    dss_q = getmp(&data, &len);
    dss_g = getmp(&data, &len);
    dss_y = getmp(&data, &len);
}

static char *dss_fmtkey(void) {
    char *p;
    int len, i, pos, nibbles;
    static const char hex[] = "0123456789abcdef";
    if (!dss_p)
        return NULL;
    len = 8 + 4 + 1;                   /* 4 x "0x", punctuation, \0 */
    len += 4 * (dss_p[0] + dss_q[0] + dss_g[0] + dss_y[0]);   /* digits */
    p = malloc(len);
    if (!p) return NULL;

    pos = 0;
    pos += sprintf(p+pos, "0x");
    nibbles = (3 + ssh1_bignum_bitcount(dss_p))/4; if (nibbles<1) nibbles=1;
    for (i=nibbles; i-- ;)
        p[pos++] = hex[(bignum_byte(dss_p, i/2) >> (4*(i%2))) & 0xF];
    pos += sprintf(p+pos, ",0x");
    nibbles = (3 + ssh1_bignum_bitcount(dss_q))/4; if (nibbles<1) nibbles=1;
    for (i=nibbles; i-- ;)
        p[pos++] = hex[(bignum_byte(dss_q, i/2) >> (4*(i%2))) & 0xF];
    pos += sprintf(p+pos, ",0x");
    nibbles = (3 + ssh1_bignum_bitcount(dss_g))/4; if (nibbles<1) nibbles=1;
    for (i=nibbles; i-- ;)
        p[pos++] = hex[(bignum_byte(dss_g, i/2) >> (4*(i%2))) & 0xF];
    pos += sprintf(p+pos, ",0x");
    nibbles = (3 + ssh1_bignum_bitcount(dss_y))/4; if (nibbles<1) nibbles=1;
    for (i=nibbles; i-- ;)
        p[pos++] = hex[(bignum_byte(dss_y, i/2) >> (4*(i%2))) & 0xF];
    p[pos] = '\0';
    return p;
}

static char *dss_fingerprint(void) {
    struct MD5Context md5c;
    unsigned char digest[16], lenbuf[4];
    char buffer[16*3+40];
    char *ret;
    int numlen, i;

    MD5Init(&md5c);
    MD5Update(&md5c, "\0\0\0\7ssh-dss", 11);

#define ADD_BIGNUM(bignum) \
    numlen = (ssh1_bignum_bitcount(bignum)+8)/8; \
    PUT_32BIT(lenbuf, numlen); MD5Update(&md5c, lenbuf, 4); \
    for (i = numlen; i-- ;) { \
        unsigned char c = bignum_byte(bignum, i); \
        MD5Update(&md5c, &c, 1); \
    }
    ADD_BIGNUM(dss_p);
    ADD_BIGNUM(dss_q);
    ADD_BIGNUM(dss_g);
    ADD_BIGNUM(dss_y);
#undef ADD_BIGNUM

    MD5Final(digest, &md5c);

    sprintf(buffer, "%d ", ssh1_bignum_bitcount(dss_p));
    for (i = 0; i < 16; i++)
        sprintf(buffer+strlen(buffer), "%s%02x", i?":":"", digest[i]);
    ret = malloc(strlen(buffer)+1);
    if (ret)
        strcpy(ret, buffer);
    return ret;
}

static int dss_verifysig(char *sig, int siglen, char *data, int datalen) {
    char *p;
    int slen;
    char hash[20];
    Bignum r, s, w, i1, i2, i3, u1, u2, sha, v;
    int ret;

    if (!dss_p)
        return 0;

#ifdef DEBUG_DSS
    {
        int i;
        printf("sig:");
        for (i=0;i<siglen;i++)
            printf("  %02xf", (unsigned char)(sig[i]));
        printf("\n");
    }
#endif
    /*
     * Commercial SSH (2.0.13) and OpenSSH disagree over the format
     * of a DSA signature. OpenSSH is in line with the IETF drafts:
     * it uses a string "ssh-dss", followed by a 40-byte string
     * containing two 160-bit integers end-to-end. Commercial SSH
     * can't be bothered with the header bit, and considers a DSA
     * signature blob to be _just_ the 40-byte string containing
     * the two 160-bit integers. We tell them apart by measuring
     * the length: length 40 means the commercial-SSH bug, anything
     * else is assumed to be IETF-compliant.
     */
    if (siglen != 40) {                /* bug not present; read admin fields */
        getstring(&sig, &siglen, &p, &slen);
        if (!p || memcmp(p, "ssh-dss", 7)) {
            return 0;
        }
        sig += 4, siglen -= 4;             /* skip yet another length field */
    }
    diagbn("p=", dss_p);
    diagbn("q=", dss_q);
    diagbn("g=", dss_g);
    diagbn("y=", dss_y);
    r = get160(&sig, &siglen);
    diagbn("r=", r);
    s = get160(&sig, &siglen);
    diagbn("s=", s);
    if (!r || !s)
        return 0;

    /*
     * Step 1. w <- s^-1 mod q.
     */
    w = modinv(s, dss_q);
    diagbn("w=", w);

    /*
     * Step 2. u1 <- SHA(message) * w mod q.
     */
    u1 = newbn(dss_q[0]);
    SHA_Simple(data, datalen, hash);
    p = hash; slen = 20; sha = get160(&p, &slen);
    diagbn("sha=", sha);
    modmul(sha, w, dss_q, u1);
    diagbn("u1=", u1);

    /*
     * Step 3. u2 <- r * w mod q.
     */
    u2 = newbn(dss_q[0]);
    modmul(r, w, dss_q, u2);
    diagbn("u2=", u2);

    /*
     * Step 4. v <- (g^u1 * y^u2 mod p) mod q.
     */
    i1 = newbn(dss_p[0]);
    i2 = newbn(dss_p[0]);
    i3 = newbn(dss_p[0]);
    v = newbn(dss_q[0]);
    modpow(dss_g, u1, dss_p, i1);
    diagbn("gu1p=", i1);
    modpow(dss_y, u2, dss_p, i2);
    diagbn("yu2p=", i2);
    modmul(i1, i2, dss_p, i3);
    diagbn("gu1yu2p=", i3);
    modmul(i3, One, dss_q, v);
    diagbn("gu1yu2q=v=", v);
    diagbn("r=", r);

    /*
     * Step 5. v should now be equal to r.
     */

    ret = !bignum_cmp(v, r);

    freebn(w);
    freebn(sha);
    freebn(i1);
    freebn(i2);
    freebn(i3);
    freebn(v);
    freebn(r);
    freebn(s);

    return ret;
}

struct ssh_hostkey ssh_dss = {
    dss_setkey,
    dss_fmtkey,
    dss_fingerprint,
    dss_verifysig,
    "ssh-dss",
    "dss"
};
