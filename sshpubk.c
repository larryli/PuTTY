/*
 * Generic SSH public-key handling operations. In particular,
 * reading of SSH public-key files, and also the generic `sign'
 * operation for ssh2 (which checks the type of the key and
 * dispatches to the appropriate key-type specific function).
 */

#include <stdio.h>
#include <stdlib.h>

#include <stdarg.h> /* FIXME */
#include <windows.h> /* FIXME */

#include "putty.h"
#include "ssh.h"

#define GET_32BIT(cp) \
    (((unsigned long)(unsigned char)(cp)[0] << 24) | \
    ((unsigned long)(unsigned char)(cp)[1] << 16) | \
    ((unsigned long)(unsigned char)(cp)[2] << 8) | \
    ((unsigned long)(unsigned char)(cp)[3]))

#define rsa_signature "SSH PRIVATE KEY FILE FORMAT 1.1\n"
#define dss_signature "-----BEGIN DSA PRIVATE KEY-----\n"

#define BASE64_TOINT(x) ( (x)-'A'<26 ? (x)-'A'+0 :\
                          (x)-'a'<26 ? (x)-'a'+26 :\
                          (x)-'0'<10 ? (x)-'0'+52 :\
                          (x)=='+' ? 62 : \
                          (x)=='/' ? 63 : 0 )

static int loadrsakey_main(FILE *fp, struct RSAKey *key,
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
     * decryption modulus, and then we're done.
     */
    i += makeprivate(buf+i, key);
    if (len-i < 0) goto end;

    ret = 1;
    end:
    memset(buf, 0, sizeof(buf));       /* burn the evidence */
    return ret;
}

int loadrsakey(char *filename, struct RSAKey *key, char *passphrase) {
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
        return loadrsakey_main(fp, key, NULL, passphrase);
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
        return loadrsakey_main(fp, NULL, comment, NULL);
    }
    return 0;                          /* wasn't the right kind of file */
}
