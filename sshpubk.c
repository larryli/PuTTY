/*
 * Generic SSH public-key handling operations. In particular,
 * reading of SSH public-key files, and also the generic `sign'
 * operation for ssh2 (which checks the type of the key and
 * dispatches to the appropriate key-type specific function).
 */

#include <stdio.h>
#include <stdlib.h>

#include "ssh.h"

#define PUT_32BIT(cp, value) do { \
  (cp)[3] = (value); \
  (cp)[2] = (value) >> 8; \
  (cp)[1] = (value) >> 16; \
  (cp)[0] = (value) >> 24; } while (0)

#define GET_32BIT(cp) \
    (((unsigned long)(unsigned char)(cp)[0] << 24) | \
    ((unsigned long)(unsigned char)(cp)[1] << 16) | \
    ((unsigned long)(unsigned char)(cp)[2] << 8) | \
    ((unsigned long)(unsigned char)(cp)[3]))

#define rsa_signature "SSH PRIVATE KEY FILE FORMAT 1.1\n"

#define BASE64_TOINT(x) ( (x)-'A'<26 ? (x)-'A'+0 :\
                          (x)-'a'<26 ? (x)-'a'+26 :\
                          (x)-'0'<10 ? (x)-'0'+52 :\
                          (x)=='+' ? 62 : \
                          (x)=='/' ? 63 : 0 )

static int loadrsakey_main(FILE *fp, struct RSAKey *key, struct RSAAux *aux,
                           char **commentptr, char *passphrase) {
    unsigned char buf[16384];
    unsigned char keybuf[16];
    int len;
    int i, j, ciphertype;
    int ret = 0;
    struct MD5Context md5c;
    char *comment;

    /* Slurp the whole file (minus the header) into a buffer. */
    len = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);
    if (len < 0 || len == sizeof(buf))
        goto end;                      /* file too big or not read */

    i = 0;

    /*
     * A zero byte. (The signature includes a terminating NUL.)
     */
    if (len-i < 1 || buf[i] != 0)
        goto end;
    i++;

    /* One byte giving encryption type, and one reserved uint32. */
    if (len-i < 1)
        goto end;
    ciphertype = buf[i];
    if (ciphertype != 0 && ciphertype != SSH_CIPHER_3DES)
        goto end;
    i++;
    if (len-i < 4)
        goto end;                      /* reserved field not present */
    if (buf[i] != 0 || buf[i+1] != 0 || buf[i+2] != 0 || buf[i+3] != 0)
        goto end;                      /* reserved field nonzero, panic! */
    i += 4;

    /* Now the serious stuff. An ordinary SSH 1 public key. */
    i += makekey(buf+i, key, NULL, 1);
    if (len-i < 0)
        goto end;                      /* overran */

    /* Next, the comment field. */
    j = GET_32BIT(buf+i);
    i += 4;
    if (len-i < j) goto end;
    comment = malloc(j+1);
    if (comment) {
        memcpy(comment, buf+i, j);
        comment[j] = '\0';
    }
    i += j;
    if (commentptr)
        *commentptr = comment;
    if (key)
        key->comment = comment;
    if (!key) {
        return ciphertype != 0;
    }

    /*
     * Decrypt remainder of buffer.
     */
    if (ciphertype) {
        MD5Init(&md5c);
        MD5Update(&md5c, passphrase, strlen(passphrase));
        MD5Final(keybuf, &md5c);
        des3_decrypt_pubkey(keybuf, buf+i, (len-i+7)&~7);
        memset(keybuf, 0, sizeof(keybuf));    /* burn the evidence */
    }

    /*
     * We are now in the secret part of the key. The first four
     * bytes should be of the form a, b, a, b.
     */
    if (len-i < 4) goto end;
    if (buf[i] != buf[i+2] || buf[i+1] != buf[i+3]) { ret = -1; goto end; }
    i += 4;

    /*
     * After that, we have one further bignum which is our
     * decryption exponent, and then the three auxiliary values
     * (iqmp, q, p).
     */
    i += makeprivate(buf+i, key);
    if (len-i < 0) goto end;
    if (aux) {
        i += ssh1_read_bignum(buf+i, &aux->iqmp);
        if (len-i < 0) goto end;
        i += ssh1_read_bignum(buf+i, &aux->q);
        if (len-i < 0) goto end;
        i += ssh1_read_bignum(buf+i, &aux->p);
        if (len-i < 0) goto end;
    }

    ret = 1;
    end:
    memset(buf, 0, sizeof(buf));       /* burn the evidence */
    return ret;
}

int loadrsakey(char *filename, struct RSAKey *key, struct RSAAux *aux,
               char *passphrase) {
    FILE *fp;
    unsigned char buf[64];

    fp = fopen(filename, "rb");
    if (!fp)
        return 0;                      /* doesn't even exist */

    /*
     * Read the first line of the file and see if it's a v1 private
     * key file.
     */
    if (fgets(buf, sizeof(buf), fp) &&
        !strcmp(buf, rsa_signature)) {
        return loadrsakey_main(fp, key, aux, NULL, passphrase);
    }

    /*
     * Otherwise, we have nothing. Return empty-handed.
     */
    fclose(fp);
    return 0;
}

/*
 * See whether an RSA key is encrypted. Return its comment field as
 * well.
 */
int rsakey_encrypted(char *filename, char **comment) {
    FILE *fp;
    unsigned char buf[64];

    fp = fopen(filename, "rb");
    if (!fp)
        return 0;                      /* doesn't even exist */

    /*
     * Read the first line of the file and see if it's a v1 private
     * key file.
     */
    if (fgets(buf, sizeof(buf), fp) &&
        !strcmp(buf, rsa_signature)) {
        return loadrsakey_main(fp, NULL, NULL, comment, NULL);
    }
    fclose(fp);
    return 0;                          /* wasn't the right kind of file */
}

/*
 * Save an RSA key file. Return nonzero on success.
 */
int saversakey(char *filename, struct RSAKey *key, struct RSAAux *aux,
               char *passphrase) {
    unsigned char buf[16384];
    unsigned char keybuf[16];
    struct MD5Context md5c;
    unsigned char *p, *estart;
    FILE *fp;

    /*
     * Write the initial signature.
     */
    p = buf;
    memcpy(p, rsa_signature, sizeof(rsa_signature));
    p += sizeof(rsa_signature);

    /*
     * One byte giving encryption type, and one reserved (zero)
     * uint32.
     */
    *p++ = (passphrase ? SSH_CIPHER_3DES : 0);
    PUT_32BIT(p, 0); p += 4;

    /*
     * An ordinary SSH 1 public key consists of: a uint32
     * containing the bit count, then two bignums containing the
     * modulus and exponent respectively.
     */
    PUT_32BIT(p, ssh1_bignum_bitcount(key->modulus)); p += 4;
    p += ssh1_write_bignum(p, key->modulus);
    p += ssh1_write_bignum(p, key->exponent);

    /*
     * A string containing the comment field.
     */
    if (key->comment) {
        PUT_32BIT(p, strlen(key->comment)); p += 4;
        memcpy(p, key->comment, strlen(key->comment));
        p += strlen(key->comment);
    } else {
        PUT_32BIT(p, 0); p += 4;
    }

    /*
     * The encrypted portion starts here.
     */
    estart = p;

    /*
     * Two bytes, then the same two bytes repeated.
     */
    *p++ = random_byte();
    *p++ = random_byte();
    p[0] = p[-2]; p[1] = p[-1]; p += 2;

    /*
     * Four more bignums: the decryption exponent, then iqmp, then
     * q, then p.
     */
    p += ssh1_write_bignum(p, key->private_exponent);
    p += ssh1_write_bignum(p, aux->iqmp);
    p += ssh1_write_bignum(p, aux->q);
    p += ssh1_write_bignum(p, aux->p);

    /*
     * Now write zeros until the encrypted portion is a multiple of
     * 8 bytes.
     */
    while ((p-estart) % 8)
        *p++ = '\0';

    /*
     * Now encrypt the encrypted portion.
     */
    if (passphrase) {
        MD5Init(&md5c);
        MD5Update(&md5c, passphrase, strlen(passphrase));
        MD5Final(keybuf, &md5c);
        des3_encrypt_pubkey(keybuf, estart, p-estart);
        memset(keybuf, 0, sizeof(keybuf));    /* burn the evidence */
    }

    /*
     * Done. Write the result to the file.
     */
    fp = fopen(filename, "wb");
    if (fp) {
        int ret = (fwrite(buf, 1, p-buf, fp) == (size_t)(p-buf));
        ret = ret && (fclose(fp) == 0);
        return ret;
    } else
        return 0;
}
