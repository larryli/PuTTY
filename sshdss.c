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

struct dss_key {
    Bignum p, q, g, y;
};

static void *dss_newkey(char *data, int len) {
    char *p;
    int slen;
    struct dss_key *dss;

    dss = smalloc(sizeof(struct dss_key));
    if (!dss) return NULL;
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
	sfree(dss);
	return NULL;
    }
    dss->p = getmp(&data, &len);
    dss->q = getmp(&data, &len);
    dss->g = getmp(&data, &len);
    dss->y = getmp(&data, &len);

    return dss;
}

static void dss_freekey(void *key) {
    struct dss_key *dss = (struct dss_key *)key;
    freebn(dss->p);
    freebn(dss->q);
    freebn(dss->g);
    freebn(dss->y);
    sfree(dss);
}

static char *dss_fmtkey(void *key) {
    struct dss_key *dss = (struct dss_key *)key;
    char *p;
    int len, i, pos, nibbles;
    static const char hex[] = "0123456789abcdef";
    if (!dss->p)
        return NULL;
    len = 8 + 4 + 1;                   /* 4 x "0x", punctuation, \0 */
    len += 4 * (dss->p[0] + dss->q[0] + dss->g[0] + dss->y[0]);   /* digits */
    p = smalloc(len);
    if (!p) return NULL;

    pos = 0;
    pos += sprintf(p+pos, "0x");
    nibbles = (3 + ssh1_bignum_bitcount(dss->p))/4; if (nibbles<1) nibbles=1;
    for (i=nibbles; i-- ;)
        p[pos++] = hex[(bignum_byte(dss->p, i/2) >> (4*(i%2))) & 0xF];
    pos += sprintf(p+pos, ",0x");
    nibbles = (3 + ssh1_bignum_bitcount(dss->q))/4; if (nibbles<1) nibbles=1;
    for (i=nibbles; i-- ;)
        p[pos++] = hex[(bignum_byte(dss->q, i/2) >> (4*(i%2))) & 0xF];
    pos += sprintf(p+pos, ",0x");
    nibbles = (3 + ssh1_bignum_bitcount(dss->g))/4; if (nibbles<1) nibbles=1;
    for (i=nibbles; i-- ;)
        p[pos++] = hex[(bignum_byte(dss->g, i/2) >> (4*(i%2))) & 0xF];
    pos += sprintf(p+pos, ",0x");
    nibbles = (3 + ssh1_bignum_bitcount(dss->y))/4; if (nibbles<1) nibbles=1;
    for (i=nibbles; i-- ;)
        p[pos++] = hex[(bignum_byte(dss->y, i/2) >> (4*(i%2))) & 0xF];
    p[pos] = '\0';
    return p;
}

static char *dss_fingerprint(void *key) {
    struct dss_key *dss = (struct dss_key *)key;
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
    ADD_BIGNUM(dss->p);
    ADD_BIGNUM(dss->q);
    ADD_BIGNUM(dss->g);
    ADD_BIGNUM(dss->y);
#undef ADD_BIGNUM

    MD5Final(digest, &md5c);

    sprintf(buffer, "%d ", ssh1_bignum_bitcount(dss->p));
    for (i = 0; i < 16; i++)
        sprintf(buffer+strlen(buffer), "%s%02x", i?":":"", digest[i]);
    ret = smalloc(strlen(buffer)+1);
    if (ret)
        strcpy(ret, buffer);
    return ret;
}

static int dss_verifysig(void *key, char *sig, int siglen,
			 char *data, int datalen) {
    struct dss_key *dss = (struct dss_key *)key;
    char *p;
    int slen;
    char hash[20];
    Bignum r, s, w, gu1p, yu2p, gu1yu2p, u1, u2, sha, v;
    int ret;

    if (!dss->p)
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
    diagbn("p=", dss->p);
    diagbn("q=", dss->q);
    diagbn("g=", dss->g);
    diagbn("y=", dss->y);
    r = get160(&sig, &siglen);
    diagbn("r=", r);
    s = get160(&sig, &siglen);
    diagbn("s=", s);
    if (!r || !s)
        return 0;

    /*
     * Step 1. w <- s^-1 mod q.
     */
    w = modinv(s, dss->q);
    diagbn("w=", w);

    /*
     * Step 2. u1 <- SHA(message) * w mod q.
     */
    SHA_Simple(data, datalen, hash);
    p = hash; slen = 20; sha = get160(&p, &slen);
    diagbn("sha=", sha);
    u1 = modmul(sha, w, dss->q);
    diagbn("u1=", u1);

    /*
     * Step 3. u2 <- r * w mod q.
     */
    u2 = modmul(r, w, dss->q);
    diagbn("u2=", u2);

    /*
     * Step 4. v <- (g^u1 * y^u2 mod p) mod q.
     */
    gu1p = modpow(dss->g, u1, dss->p);
    diagbn("gu1p=", gu1p);
    yu2p = modpow(dss->y, u2, dss->p);
    diagbn("yu2p=", yu2p);
    gu1yu2p = modmul(gu1p, yu2p, dss->p);
    diagbn("gu1yu2p=", gu1yu2p);
    v = modmul(gu1yu2p, One, dss->q);
    diagbn("gu1yu2q=v=", v);
    diagbn("r=", r);

    /*
     * Step 5. v should now be equal to r.
     */

    ret = !bignum_cmp(v, r);

    freebn(w);
    freebn(sha);
    freebn(gu1p);
    freebn(yu2p);
    freebn(gu1yu2p);
    freebn(v);
    freebn(r);
    freebn(s);

    return ret;
}

int dss_sign(void *key, char *sig, int siglen,
	     char *data, int datalen) {
    return 0;			       /* do nothing */
}

struct ssh_signkey ssh_dss = {
    dss_newkey,
    dss_freekey,
    dss_fmtkey,
    dss_fingerprint,
    dss_verifysig,
    dss_sign,
    "ssh-dss",
    "dss"
};
