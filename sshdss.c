#include <stdio.h>
#include <stdlib.h>

#include "ssh.h"

#define GET_32BIT(cp) \
    (((unsigned long)(unsigned char)(cp)[0] << 24) | \
    ((unsigned long)(unsigned char)(cp)[1] << 16) | \
    ((unsigned long)(unsigned char)(cp)[2] << 8) | \
    ((unsigned long)(unsigned char)(cp)[3]))

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
    int len;
    int i;
    if (!dss_p)
        return NULL;
    len = 7 + 4 + 1;                   /* "ssh-dss", punctuation, \0 */
    len += 4 * (dss_p[0] + dss_q[0] + dss_g[0] + dss_y[0]);   /* digits */
    p = malloc(len);
    if (!p) return NULL;
    strcpy(p, "ssh-dss:");
    for (i = dss_p[0]; i > 0; i--) sprintf(p+strlen(p), "%04X", dss_p[i]);
    strcat(p, "/");
    for (i = dss_q[0]; i > 0; i--) sprintf(p+strlen(p), "%04X", dss_q[i]);
    strcat(p, "/");
    for (i = dss_g[0]; i > 0; i--) sprintf(p+strlen(p), "%04X", dss_g[i]);
    strcat(p, "/");
    for (i = dss_y[0]; i > 0; i--) sprintf(p+strlen(p), "%04X", dss_y[i]);
    return p;
}

static int dss_verifysig(char *sig, int siglen, char *data, int datalen) {
    char *p;
    int i, slen;
    char hash[20];
    Bignum qm2, r, s, w, i1, i2, i3, u1, u2, sha, v;
    int ret;

    if (!dss_p)
        return 0;

    getstring(&sig, &siglen, &p, &slen);
    if (!p || memcmp(p, "ssh-dss", 7)) {
        return 0;
    }
    sig += 4, siglen -= 4;             /* skip yet another length field */
    r = get160(&sig, &siglen);
    s = get160(&sig, &siglen);
    if (!r || !s)
        return 0;

    /*
     * Step 1. w <- s^-1 mod q.
     */
    w = newbn(dss_q[0]);
    qm2 = copybn(dss_q);
    decbn(qm2); decbn(qm2);
    /* Now qm2 is q-2, and by Fermat's Little Theorem, s^qm2 == s^-1 (mod q).
     * This is a silly way to do it; may fix it later. */
    modpow(s, qm2, dss_q, w);

    /*
     * Step 2. u1 <- SHA(message) * w mod q.
     */
    u1 = newbn(dss_q[0]);
    SHA_Simple(data, datalen, hash);
    p = hash; slen = 20; sha = get160(&p, &slen);
    modmul(sha, w, dss_q, u1);

    /*
     * Step 3. u2 <- r * w mod q.
     */
    u2 = newbn(dss_q[0]);
    modmul(r, w, dss_q, u2);

    /*
     * Step 4. v <- (g^u1 * y^u2 mod p) mod q.
     */
    i1 = newbn(dss_p[0]);
    i2 = newbn(dss_p[0]);
    i3 = newbn(dss_p[0]);
    v = newbn(dss_q[0]);
    modpow(dss_g, u1, dss_p, i1);
    modpow(dss_y, u2, dss_p, i2);
    modmul(i1, i2, dss_p, i3);
    modmul(i3, One, dss_q, v);

    /*
     * Step 5. v should now be equal to r.
     */

    ret = 1;
    for (i = 1; i <= v[0] || i <= r[0]; i++) {
        if ((i > v[0] && r[i] != 0) ||
            (i > r[0] && v[i] != 0) ||
            (i <= v[0] && i <= r[0] && r[i] != v[i]))
            ret = 0;
    }

    freebn(w);
    freebn(qm2);
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
    dss_verifysig,
    "ssh-dss"
};
