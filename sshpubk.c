/*
 * Read SSH public keys from files.
 *
 * First implementation: only supports unencrypted SSH 1.1 format
 * RSA keys. Encryption, and SSH 2 DSS keys, to be supported later.
 */

#include <stdio.h>

#include <stdio.h> /* FIXME */
#include <stdarg.h> /* FIXME */
#include <windows.h> /* FIXME */
#include "putty.h" /* FIXME */

#include "ssh.h"

#define GET_32BIT(cp) \
    (((unsigned long)(unsigned char)(cp)[0] << 24) | \
    ((unsigned long)(unsigned char)(cp)[1] << 16) | \
    ((unsigned long)(unsigned char)(cp)[2] << 8) | \
    ((unsigned long)(unsigned char)(cp)[3]))

#define rsa_signature "SSH PRIVATE KEY FILE FORMAT 1.1\n"

int loadrsakey(char *filename, struct RSAKey *key, char *passphrase) {
    FILE *fp;
    unsigned char buf[16384];
    unsigned char keybuf[16];
    int len;
    int i, j, ciphertype;
    int ret = 0;
    struct MD5Context md5c;

    fp = fopen(filename, "rb");
    if (!fp)
        goto end;

    /* Slurp the whole file into a buffer. */
    len = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);
    if (len < 0 || len == sizeof(buf))
        goto end;                      /* file too big or not read */

    if (len < sizeof(rsa_signature) ||
        memcmp(buf, rsa_signature, sizeof(rsa_signature)) != 0)
        goto end;                      /* failure to have sig at front */

    i = sizeof(rsa_signature);

    /* Next, one byte giving encryption type, and one reserved uint32. */
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
    if (len-i < 4+j) goto end; i += 4+j;
    /*
     * FIXME: might need to use this string.
     */

    /*
     * Decrypt remainder of buffer.
     */
    if (ciphertype) {
        MD5Init(&md5c);
        MD5Update(&md5c, passphrase, strlen(passphrase));
        MD5Final(keybuf, &md5c);
        des3_decrypt_pubkey(keybuf, buf+i, (len-i+7)&~7);
        memset(keybuf, 0, sizeof(buf));    /* burn the evidence */
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
     * decryption modulus, and then we're done.
     */
    i += makeprivate(buf+i, key);
    if (len-i < 0) goto end;

    ret = 1;
    end:
    memset(buf, 0, sizeof(buf));       /* burn the evidence */
    return ret;
}

/*
 * See whether an RSA key is encrypted.
 */
int rsakey_encrypted(char *filename) {
    FILE *fp;
    unsigned char buf[1+sizeof(rsa_signature)];
    int len;

    fp = fopen(filename, "rb");
    if (!fp)
        return 0;                      /* doesn't even exist */

    /* Slurp the whole file into a buffer. */
    len = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);
    if (len < sizeof(buf))
        return 0;                      /* not even valid */
    if (buf[sizeof(buf)-1])
        return 1;                      /* encrypted */
    return 0;
}
