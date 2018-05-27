/*
 * Digital Signature Standard implementation for PuTTY.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "ssh.h"
#include "misc.h"

static void getstring(const char **data, int *datalen,
                      const char **p, int *length)
{
    *p = NULL;
    if (*datalen < 4)
	return;
    *length = toint(GET_32BIT(*data));
    if (*length < 0)
        return;
    *datalen -= 4;
    *data += 4;
    if (*datalen < *length)
	return;
    *p = *data;
    *data += *length;
    *datalen -= *length;
}
static Bignum getmp(const char **data, int *datalen)
{
    const char *p;
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

static Bignum get160(const char **data, int *datalen)
{
    Bignum b;

    if (*datalen < 20)
        return NULL;

    b = bignum_from_bytes(*data, 20);
    *data += 20;
    *datalen -= 20;

    return b;
}

static void dss_freekey(ssh_key *key);    /* forward reference */

static ssh_key *dss_newkey(const ssh_keyalg *self,
                           const void *vdata, int len)
{
    const char *data = (const char *)vdata;
    const char *p;
    int slen;
    struct dss_key *dss;

    dss = snew(struct dss_key);
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

    if (!p || slen != 7 || memcmp(p, "ssh-dss", 7)) {
	sfree(dss);
	return NULL;
    }
    dss->p = getmp(&data, &len);
    dss->q = getmp(&data, &len);
    dss->g = getmp(&data, &len);
    dss->y = getmp(&data, &len);
    dss->x = NULL;

    if (!dss->p || !dss->q || !dss->g || !dss->y ||
        !bignum_cmp(dss->q, Zero) || !bignum_cmp(dss->p, Zero)) {
        /* Invalid key. */
        dss_freekey(&dss->sshk);
        return NULL;
    }

    return &dss->sshk;
}

static void dss_freekey(ssh_key *key)
{
    struct dss_key *dss = FROMFIELD(key, struct dss_key, sshk);
    if (dss->p)
        freebn(dss->p);
    if (dss->q)
        freebn(dss->q);
    if (dss->g)
        freebn(dss->g);
    if (dss->y)
        freebn(dss->y);
    if (dss->x)
        freebn(dss->x);
    sfree(dss);
}

static char *dss_fmtkey(ssh_key *key)
{
    struct dss_key *dss = FROMFIELD(key, struct dss_key, sshk);
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
    p = snewn(len, char);
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

static int dss_verifysig(ssh_key *key, const void *vsig, int siglen,
			 const void *data, int datalen)
{
    struct dss_key *dss = FROMFIELD(key, struct dss_key, sshk);
    const char *sig = (const char *)vsig;
    const char *p;
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
     * of a DSA signature. OpenSSH is in line with RFC 4253:
     * it uses a string "ssh-dss", followed by a 40-byte string
     * containing two 160-bit integers end-to-end. Commercial SSH
     * can't be bothered with the header bit, and considers a DSA
     * signature blob to be _just_ the 40-byte string containing
     * the two 160-bit integers. We tell them apart by measuring
     * the length: length 40 means the commercial-SSH bug, anything
     * else is assumed to be RFC-compliant.
     */
    if (siglen != 40) {		       /* bug not present; read admin fields */
	getstring(&sig, &siglen, &p, &slen);
	if (!p || slen != 7 || memcmp(p, "ssh-dss", 7)) {
	    return 0;
	}
	sig += 4, siglen -= 4;	       /* skip yet another length field */
    }
    r = get160(&sig, &siglen);
    s = get160(&sig, &siglen);
    if (!r || !s) {
        if (r)
            freebn(r);
        if (s)
            freebn(s);
	return 0;
    }

    if (!bignum_cmp(s, Zero)) {
        freebn(r);
        freebn(s);
        return 0;
    }

    /*
     * Step 1. w <- s^-1 mod q.
     */
    w = modinv(s, dss->q);
    if (!w) {
        freebn(r);
        freebn(s);
        return 0;
    }

    /*
     * Step 2. u1 <- SHA(message) * w mod q.
     */
    SHA_Simple(data, datalen, (unsigned char *)hash);
    p = hash;
    slen = 20;
    sha = get160(&p, &slen);
    u1 = modmul(sha, w, dss->q);

    /*
     * Step 3. u2 <- r * w mod q.
     */
    u2 = modmul(r, w, dss->q);

    /*
     * Step 4. v <- (g^u1 * y^u2 mod p) mod q.
     */
    gu1p = modpow(dss->g, u1, dss->p);
    yu2p = modpow(dss->y, u2, dss->p);
    gu1yu2p = modmul(gu1p, yu2p, dss->p);
    v = modmul(gu1yu2p, One, dss->q);

    /*
     * Step 5. v should now be equal to r.
     */

    ret = !bignum_cmp(v, r);

    freebn(w);
    freebn(sha);
    freebn(u1);
    freebn(u2);
    freebn(gu1p);
    freebn(yu2p);
    freebn(gu1yu2p);
    freebn(v);
    freebn(r);
    freebn(s);

    return ret;
}

static void dss_public_blob(ssh_key *key, BinarySink *bs)
{
    struct dss_key *dss = FROMFIELD(key, struct dss_key, sshk);

    put_stringz(bs, "ssh-dss");
    put_mp_ssh2(bs, dss->p);
    put_mp_ssh2(bs, dss->q);
    put_mp_ssh2(bs, dss->g);
    put_mp_ssh2(bs, dss->y);
}

static void dss_private_blob(ssh_key *key, BinarySink *bs)
{
    struct dss_key *dss = FROMFIELD(key, struct dss_key, sshk);

    put_mp_ssh2(bs, dss->x);
}

static ssh_key *dss_createkey(const ssh_keyalg *self,
                              const void *pub_blob, int pub_len,
                              const void *priv_blob, int priv_len)
{
    struct dss_key *dss;
    const char *pb = (const char *) priv_blob;
    const char *hash;
    int hashlen;
    SHA_State s;
    unsigned char digest[20];
    Bignum ytest;

    dss = FROMFIELD(dss_newkey(self, pub_blob, pub_len),
                    struct dss_key, sshk);
    if (!dss)
        return NULL;
    dss->x = getmp(&pb, &priv_len);
    if (!dss->x) {
        dss_freekey(&dss->sshk);
        return NULL;
    }

    /*
     * Check the obsolete hash in the old DSS key format.
     */
    hashlen = -1;
    getstring(&pb, &priv_len, &hash, &hashlen);
    if (hashlen == 20) {
	SHA_Init(&s);
	put_mp_ssh2(&s, dss->p);
	put_mp_ssh2(&s, dss->q);
	put_mp_ssh2(&s, dss->g);
	SHA_Final(&s, digest);
	if (0 != memcmp(hash, digest, 20)) {
	    dss_freekey(&dss->sshk);
	    return NULL;
	}
    }

    /*
     * Now ensure g^x mod p really is y.
     */
    ytest = modpow(dss->g, dss->x, dss->p);
    if (0 != bignum_cmp(ytest, dss->y)) {
	dss_freekey(&dss->sshk);
        freebn(ytest);
	return NULL;
    }
    freebn(ytest);

    return &dss->sshk;
}

static ssh_key *dss_openssh_createkey(const ssh_keyalg *self,
                                      const unsigned char **blob, int *len)
{
    const char **b = (const char **) blob;
    struct dss_key *dss;

    dss = snew(struct dss_key);

    dss->p = getmp(b, len);
    dss->q = getmp(b, len);
    dss->g = getmp(b, len);
    dss->y = getmp(b, len);
    dss->x = getmp(b, len);

    if (!dss->p || !dss->q || !dss->g || !dss->y || !dss->x ||
        !bignum_cmp(dss->q, Zero) || !bignum_cmp(dss->p, Zero)) {
        /* Invalid key. */
        dss_freekey(&dss->sshk);
        return NULL;
    }

    return &dss->sshk;
}

static void dss_openssh_fmtkey(ssh_key *key, BinarySink *bs)
{
    struct dss_key *dss = FROMFIELD(key, struct dss_key, sshk);

    put_mp_ssh2(bs, dss->p);
    put_mp_ssh2(bs, dss->q);
    put_mp_ssh2(bs, dss->g);
    put_mp_ssh2(bs, dss->y);
    put_mp_ssh2(bs, dss->x);
}

static int dss_pubkey_bits(const ssh_keyalg *self,
                           const void *blob, int len)
{
    struct dss_key *dss;
    int ret;

    dss = FROMFIELD(dss_newkey(self, blob, len),
                    struct dss_key, sshk);
    if (!dss)
        return -1;
    ret = bignum_bitcount(dss->p);
    dss_freekey(&dss->sshk);

    return ret;
}

Bignum *dss_gen_k(const char *id_string, Bignum modulus, Bignum private_key,
                  unsigned char *digest, int digest_len)
{
    /*
     * The basic DSS signing algorithm is:
     * 
     *  - invent a random k between 1 and q-1 (exclusive).
     *  - Compute r = (g^k mod p) mod q.
     *  - Compute s = k^-1 * (hash + x*r) mod q.
     * 
     * This has the dangerous properties that:
     * 
     *  - if an attacker in possession of the public key _and_ the
     *    signature (for example, the host you just authenticated
     *    to) can guess your k, he can reverse the computation of s
     *    and work out x = r^-1 * (s*k - hash) mod q. That is, he
     *    can deduce the private half of your key, and masquerade
     *    as you for as long as the key is still valid.
     * 
     *  - since r is a function purely of k and the public key, if
     *    the attacker only has a _range of possibilities_ for k
     *    it's easy for him to work through them all and check each
     *    one against r; he'll never be unsure of whether he's got
     *    the right one.
     * 
     *  - if you ever sign two different hashes with the same k, it
     *    will be immediately obvious because the two signatures
     *    will have the same r, and moreover an attacker in
     *    possession of both signatures (and the public key of
     *    course) can compute k = (hash1-hash2) * (s1-s2)^-1 mod q,
     *    and from there deduce x as before.
     * 
     *  - the Bleichenbacher attack on DSA makes use of methods of
     *    generating k which are significantly non-uniformly
     *    distributed; in particular, generating a 160-bit random
     *    number and reducing it mod q is right out.
     * 
     * For this reason we must be pretty careful about how we
     * generate our k. Since this code runs on Windows, with no
     * particularly good system entropy sources, we can't trust our
     * RNG itself to produce properly unpredictable data. Hence, we
     * use a totally different scheme instead.
     * 
     * What we do is to take a SHA-512 (_big_) hash of the private
     * key x, and then feed this into another SHA-512 hash that
     * also includes the message hash being signed. That is:
     * 
     *   proto_k = SHA512 ( SHA512(x) || SHA160(message) )
     * 
     * This number is 512 bits long, so reducing it mod q won't be
     * noticeably non-uniform. So
     * 
     *   k = proto_k mod q
     * 
     * This has the interesting property that it's _deterministic_:
     * signing the same hash twice with the same key yields the
     * same signature.
     * 
     * Despite this determinism, it's still not predictable to an
     * attacker, because in order to repeat the SHA-512
     * construction that created it, the attacker would have to
     * know the private key value x - and by assumption he doesn't,
     * because if he knew that he wouldn't be attacking k!
     *
     * (This trick doesn't, _per se_, protect against reuse of k.
     * Reuse of k is left to chance; all it does is prevent
     * _excessively high_ chances of reuse of k due to entropy
     * problems.)
     * 
     * Thanks to Colin Plumb for the general idea of using x to
     * ensure k is hard to guess, and to the Cambridge University
     * Computer Security Group for helping to argue out all the
     * fine details.
     */
    SHA512_State ss;
    unsigned char digest512[64];
    Bignum proto_k, k;

    /*
     * Hash some identifying text plus x.
     */
    SHA512_Init(&ss);
    put_asciz(&ss, id_string);
    put_mp_ssh2(&ss, private_key);
    SHA512_Final(&ss, digest512);

    /*
     * Now hash that digest plus the message hash.
     */
    SHA512_Init(&ss);
    put_data(&ss, digest512, sizeof(digest512));
    put_data(&ss, digest, digest_len);

    while (1) {
        SHA512_State ss2 = ss;         /* structure copy */
        SHA512_Final(&ss2, digest512);

        smemclr(&ss2, sizeof(ss2));

        /*
         * Now convert the result into a bignum, and reduce it mod q.
         */
        proto_k = bignum_from_bytes(digest512, 64);
        k = bigmod(proto_k, modulus);
        freebn(proto_k);

        if (bignum_cmp(k, One) != 0 && bignum_cmp(k, Zero) != 0) {
            smemclr(&ss, sizeof(ss));
            smemclr(digest512, sizeof(digest512));
            return k;
        }

        /* Very unlikely we get here, but if so, k was unsuitable. */
        freebn(k);
        /* Perturb the hash to think of a different k. */
        put_byte(&ss, 'x');
        /* Go round and try again. */
    }
}

static void dss_sign(ssh_key *key, const void *data, int datalen,
                     BinarySink *bs)
{
    struct dss_key *dss = FROMFIELD(key, struct dss_key, sshk);
    Bignum k, gkp, hash, kinv, hxr, r, s;
    unsigned char digest[20];
    int i;

    SHA_Simple(data, datalen, digest);

    k = dss_gen_k("DSA deterministic k generator", dss->q, dss->x,
                  digest, sizeof(digest));
    kinv = modinv(k, dss->q);	       /* k^-1 mod q */
    assert(kinv);

    /*
     * Now we have k, so just go ahead and compute the signature.
     */
    gkp = modpow(dss->g, k, dss->p);   /* g^k mod p */
    r = bigmod(gkp, dss->q);	       /* r = (g^k mod p) mod q */
    freebn(gkp);

    hash = bignum_from_bytes(digest, 20);
    hxr = bigmuladd(dss->x, r, hash);  /* hash + x*r */
    s = modmul(kinv, hxr, dss->q);     /* s = k^-1 * (hash + x*r) mod q */
    freebn(hxr);
    freebn(kinv);
    freebn(k);
    freebn(hash);

    put_stringz(bs, "ssh-dss");
    put_uint32(bs, 40);
    for (i = 0; i < 20; i++)
	put_byte(bs, bignum_byte(r, 19 - i));
    for (i = 0; i < 20; i++)
        put_byte(bs, bignum_byte(s, 19 - i));
    freebn(r);
    freebn(s);
}

const ssh_keyalg ssh_dss = {
    dss_newkey,
    dss_freekey,
    dss_fmtkey,
    dss_public_blob,
    dss_private_blob,
    dss_createkey,
    dss_openssh_createkey,
    dss_openssh_fmtkey,
    5 /* p,q,g,y,x */,
    dss_pubkey_bits,
    dss_verifysig,
    dss_sign,
    "ssh-dss",
    "dss",
    NULL,
};
