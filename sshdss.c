#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

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
#define DEBUG_DSS
#else
#define diagbn(x,y)
#endif

static void getstring(char **data, int *datalen, char **p, int *length)
{
    *p = NULL;
    if (*datalen < 4)
	return;
    *length = GET_32BIT(*data);
    *datalen -= 4;
    *data += 4;
    if (*datalen < *length)
	return;
    *p = *data;
    *data += *length;
    *datalen -= *length;
}
static Bignum getmp(char **data, int *datalen)
{
    char *p;
    int length;
    Bignum b;

    getstring(data, datalen, &p, &length);
    if (!p)
	return NULL;
    if (p[0] & 0x80)
	return NULL;		       /* negative mp */
    b = bignum_from_bytes(p, length);
    return b;
}

static Bignum get160(char **data, int *datalen)
{
    Bignum b;

    b = bignum_from_bytes(*data, 20);
    *data += 20;
    *datalen -= 20;

    return b;
}

struct dss_key {
    Bignum p, q, g, y;
};

static void *dss_newkey(char *data, int len)
{
    char *p;
    int slen;
    struct dss_key *dss;

    dss = smalloc(sizeof(struct dss_key));
    if (!dss)
	return NULL;
    getstring(&data, &len, &p, &slen);

#ifdef DEBUG_DSS
    {
	int i;
	printf("key:");
	for (i = 0; i < len; i++)
	    printf("  %02x", (unsigned char) (data[i]));
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

static void dss_freekey(void *key)
{
    struct dss_key *dss = (struct dss_key *) key;
    freebn(dss->p);
    freebn(dss->q);
    freebn(dss->g);
    freebn(dss->y);
    sfree(dss);
}

static char *dss_fmtkey(void *key)
{
    struct dss_key *dss = (struct dss_key *) key;
    char *p;
    int len, i, pos, nibbles;
    static const char hex[] = "0123456789abcdef";
    if (!dss->p)
	return NULL;
    len = 8 + 4 + 1;		       /* 4 x "0x", punctuation, \0 */
    len += 4 * (bignum_bitcount(dss->p) + 15) / 16;
    len += 4 * (bignum_bitcount(dss->q) + 15) / 16;
    len += 4 * (bignum_bitcount(dss->g) + 15) / 16;
    len += 4 * (bignum_bitcount(dss->y) + 15) / 16;
    p = smalloc(len);
    if (!p)
	return NULL;

    pos = 0;
    pos += sprintf(p + pos, "0x");
    nibbles = (3 + bignum_bitcount(dss->p)) / 4;
    if (nibbles < 1)
	nibbles = 1;
    for (i = nibbles; i--;)
	p[pos++] =
	    hex[(bignum_byte(dss->p, i / 2) >> (4 * (i % 2))) & 0xF];
    pos += sprintf(p + pos, ",0x");
    nibbles = (3 + bignum_bitcount(dss->q)) / 4;
    if (nibbles < 1)
	nibbles = 1;
    for (i = nibbles; i--;)
	p[pos++] =
	    hex[(bignum_byte(dss->q, i / 2) >> (4 * (i % 2))) & 0xF];
    pos += sprintf(p + pos, ",0x");
    nibbles = (3 + bignum_bitcount(dss->g)) / 4;
    if (nibbles < 1)
	nibbles = 1;
    for (i = nibbles; i--;)
	p[pos++] =
	    hex[(bignum_byte(dss->g, i / 2) >> (4 * (i % 2))) & 0xF];
    pos += sprintf(p + pos, ",0x");
    nibbles = (3 + bignum_bitcount(dss->y)) / 4;
    if (nibbles < 1)
	nibbles = 1;
    for (i = nibbles; i--;)
	p[pos++] =
	    hex[(bignum_byte(dss->y, i / 2) >> (4 * (i % 2))) & 0xF];
    p[pos] = '\0';
    return p;
}

static char *dss_fingerprint(void *key)
{
    struct dss_key *dss = (struct dss_key *) key;
    struct MD5Context md5c;
    unsigned char digest[16], lenbuf[4];
    char buffer[16 * 3 + 40];
    char *ret;
    int numlen, i;

    MD5Init(&md5c);
    MD5Update(&md5c, "\0\0\0\7ssh-dss", 11);

#define ADD_BIGNUM(bignum) \
    numlen = (bignum_bitcount(bignum)+8)/8; \
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

    sprintf(buffer, "ssh-dss %d ", bignum_bitcount(dss->p));
    for (i = 0; i < 16; i++)
	sprintf(buffer + strlen(buffer), "%s%02x", i ? ":" : "",
		digest[i]);
    ret = smalloc(strlen(buffer) + 1);
    if (ret)
	strcpy(ret, buffer);
    return ret;
}

static int dss_verifysig(void *key, char *sig, int siglen,
			 char *data, int datalen)
{
    struct dss_key *dss = (struct dss_key *) key;
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
	for (i = 0; i < siglen; i++)
	    printf("  %02x", (unsigned char) (sig[i]));
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
    if (siglen != 40) {		       /* bug not present; read admin fields */
	getstring(&sig, &siglen, &p, &slen);
	if (!p || slen != 7 || memcmp(p, "ssh-dss", 7)) {
	    return 0;
	}
	sig += 4, siglen -= 4;	       /* skip yet another length field */
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
    p = hash;
    slen = 20;
    sha = get160(&p, &slen);
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

static unsigned char *dss_public_blob(void *key, int *len)
{
    struct dss_key *dss = (struct dss_key *) key;
    int plen, qlen, glen, ylen, bloblen;
    int i;
    unsigned char *blob, *p;

    plen = (bignum_bitcount(dss->p) + 8) / 8;
    qlen = (bignum_bitcount(dss->q) + 8) / 8;
    glen = (bignum_bitcount(dss->g) + 8) / 8;
    ylen = (bignum_bitcount(dss->y) + 8) / 8;

    /*
     * string "ssh-dss", mpint p, mpint q, mpint g, mpint y. Total
     * 27 + sum of lengths. (five length fields, 20+7=27).
     */
    bloblen = 27 + plen + qlen + glen + ylen;
    blob = smalloc(bloblen);
    p = blob;
    PUT_32BIT(p, 7);
    p += 4;
    memcpy(p, "ssh-dss", 7);
    p += 7;
    PUT_32BIT(p, plen);
    p += 4;
    for (i = plen; i--;)
	*p++ = bignum_byte(dss->p, i);
    PUT_32BIT(p, qlen);
    p += 4;
    for (i = qlen; i--;)
	*p++ = bignum_byte(dss->q, i);
    PUT_32BIT(p, glen);
    p += 4;
    for (i = glen; i--;)
	*p++ = bignum_byte(dss->g, i);
    PUT_32BIT(p, ylen);
    p += 4;
    for (i = ylen; i--;)
	*p++ = bignum_byte(dss->y, i);
    assert(p == blob + bloblen);
    *len = bloblen;
    return blob;
}

static unsigned char *dss_private_blob(void *key, int *len)
{
    return NULL;		       /* can't handle DSS private keys */
}

static void *dss_createkey(unsigned char *pub_blob, int pub_len,
			   unsigned char *priv_blob, int priv_len)
{
    return NULL;		       /* can't handle DSS private keys */
}

static void *dss_openssh_createkey(unsigned char **blob, int *len)
{
    return NULL;		       /* can't handle DSS private keys */
}

static int dss_openssh_fmtkey(void *key, unsigned char *blob, int len)
{
    return -1;			       /* can't handle DSS private keys */
}

unsigned char *dss_sign(void *key, char *data, int datalen, int *siglen)
{
    return NULL;		       /* can't handle DSS private keys */
}

const struct ssh_signkey ssh_dss = {
    dss_newkey,
    dss_freekey,
    dss_fmtkey,
    dss_public_blob,
    dss_private_blob,
    dss_createkey,
    dss_openssh_createkey,
    dss_openssh_fmtkey,
    dss_fingerprint,
    dss_verifysig,
    dss_sign,
    "ssh-dss",
    "dss"
};
