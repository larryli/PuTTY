/*
 * Generic SSH public-key handling operations. In particular,
 * reading of SSH public-key files, and also the generic `sign'
 * operation for ssh2 (which checks the type of the key and
 * dispatches to the appropriate key-type specific function).
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

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
    comment = smalloc(j+1);
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
    i += ssh1_read_bignum(buf+i, &key->iqmp);
    if (len-i < 0) goto end;
    i += ssh1_read_bignum(buf+i, &key->q);
    if (len-i < 0) goto end;
    i += ssh1_read_bignum(buf+i, &key->p);
    if (len-i < 0) goto end;

    if (!rsa_verify(key)) {
	freersakey(key);
	ret = 0;
    } else
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
    fclose(fp);
    return 0;                          /* wasn't the right kind of file */
}

/*
 * Save an RSA key file. Return nonzero on success.
 */
int saversakey(char *filename, struct RSAKey *key, char *passphrase) {
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
    PUT_32BIT(p, bignum_bitcount(key->modulus)); p += 4;
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
    p += ssh1_write_bignum(p, key->iqmp);
    p += ssh1_write_bignum(p, key->q);
    p += ssh1_write_bignum(p, key->p);

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

/* ----------------------------------------------------------------------
 * SSH2 private key load/store functions.
 */

/*
 * PuTTY's own format for SSH2 keys is as follows:
 * 
 * The file is text. Lines are terminated by CRLF, although CR-only
 * and LF-only are tolerated on input.
 * 
 * The first line says "PuTTY-User-Key-File-1: " plus the name of the
 * algorithm ("ssh-dss", "ssh-rsa" etc. Although, of course, this
 * being PuTTY, "ssh-dss" is not supported.)
 * 
 * The next line says "Encryption: " plus an encryption type.
 * Currently the only supported encryption types are "aes256-cbc"
 * and "none".
 * 
 * The next line says "Comment: " plus the comment string.
 * 
 * Next there is a line saying "Public-Lines: " plus a number N.
 * The following N lines contain a base64 encoding of the public
 * part of the key. This is encoded as the standard SSH2 public key
 * blob (with no initial length): so for RSA, for example, it will
 * read
 * 
 *    string "ssh-rsa"
 *    mpint  exponent
 *    mpint  modulus
 * 
 * Next, there is a line saying "Private-Lines: " plus a number N,
 * and then N lines containing the (potentially encrypted) private
 * part of the key. For the key type "ssh-rsa", this will be
 * composed of
 * 
 *    mpint  private_exponent
 *    mpint  p                  (the larger of the two primes)
 *    mpint  q                  (the smaller prime)
 *    mpint  iqmp               (the inverse of q modulo p)
 *    data   padding            (to reach a multiple of the cipher block size)
 * 
 * Finally, there is a line saying "Private-Hash: " plus a hex
 * representation of a SHA-1 hash of the plaintext version of the
 * private part, including the final padding.
 * 
 * If the key is encrypted, the encryption key is derived from the
 * passphrase by means of a succession of SHA-1 hashes. Each hash
 * is the hash of:
 * 
 *    uint32  sequence-number
 *    string  passphrase
 * 
 * where the sequence-number increases from zero. As many of these
 * hashes are used as necessary.
 * 
 * NOTE! It is important that all _public_ data can be verified
 * with reference to the _private_ data. There exist attacks based
 * on modifying the public key but leaving the private section
 * intact.
 * 
 * With RSA, this is easy: verify that n = p*q, and also verify
 * that e*d == 1 modulo (p-1)(q-1). With DSA (if we were ever to
 * support it), we would need to store extra data in the private
 * section other than just x.
 */

static int read_header(FILE *fp, char *header) {
    int len = 39;
    int c;

    while (len > 0) {
	c = fgetc(fp);
	if (c == '\n' || c == '\r' || c == EOF)
	    return 0;		       /* failure */
	if (c == ':') {
	    c = fgetc(fp);
	    if (c != ' ')
		return 0;
	    *header = '\0';
	    return 1;		       /* success! */
	}
	if (len == 0)
	    return 0;		       /* failure */
	*header++ = c;
	len--;
    }
    return 0;			       /* failure */
}

static char *read_body(FILE *fp) {
    char *text;
    int len;
    int size;
    int c;

    size = 128;
    text = smalloc(size);
    len = 0;
    text[len] = '\0';

    while (1) {
	c = fgetc(fp);
	if (c == '\r' || c == '\n') {
	    c = fgetc(fp);
	    if (c != '\r' && c != '\n' && c != EOF)
		ungetc(c, fp);
	    return text;
	}
	if (c == EOF) {
	    sfree(text);
	    return NULL;
	}
	if (len + 1 > size) {
	    size += 128;
	    text = srealloc(text, size);
	}
	text[len++] = c;
	text[len] = '\0';
    }
}

int base64_decode_atom(char *atom, unsigned char *out) {
    int vals[4];
    int i, v, len;
    unsigned word;
    char c;
    
    for (i = 0; i < 4; i++) {
	c = atom[i];
	if (c >= 'A' && c <= 'Z')
	    v = c - 'A';
	else if (c >= 'a' && c <= 'z')
	    v = c - 'a' + 26;
	else if (c >= '0' && c <= '9')
	    v = c - '0' + 52;
	else if (c == '+')
	    v = 62;
	else if (c == '/')
	    v = 63;
	else if (c == '=')
	    v = -1;
	else
	    return 0;		       /* invalid atom */
	vals[i] = v;
    }

    if (vals[0] == -1 || vals[1] == -1)
	return 0;
    if (vals[2] == -1 && vals[3] != -1)
	return 0;

    if (vals[3] != -1)
	len = 3;
    else if (vals[2] != -1)
	len = 2;
    else
	len = 1;

    word = ((vals[0] << 18) |
	    (vals[1] << 12) |
	    ((vals[2] & 0x3F) << 6) |
	    (vals[3] & 0x3F));
    out[0] = (word >> 16) & 0xFF;
    if (len > 1)
	out[1] = (word >> 8) & 0xFF;
    if (len > 2)
	out[2] = word & 0xFF;
    return len;
}

static char *read_blob(FILE *fp, int nlines, int *bloblen) {
    unsigned char *blob;
    char *line;
    int linelen, len;
    int i, j, k;

    /* We expect at most 64 base64 characters, ie 48 real bytes, per line. */
    blob = smalloc(48 * nlines);
    len = 0;
    for (i = 0; i < nlines; i++) {
	line = read_body(fp);
	if (!line) {
	    sfree(blob);
	    return NULL;
	}
	linelen = strlen(line);
	if (linelen % 4 != 0 || linelen > 64) {
	    sfree(blob);
	    sfree(line);
	    return NULL;
	}
	for (j = 0; j < linelen; j += 4) {
	    k = base64_decode_atom(line+j, blob+len);
	    if (!k) {
		sfree(line);
		sfree(blob);
		return NULL;
	    }
	    len += k;
	}
	sfree(line);
    }
    *bloblen = len;
    return blob;
}

/*
 * Magic error return value for when the passphrase is wrong.
 */
struct ssh2_userkey ssh2_wrong_passphrase = {
    NULL, NULL, NULL
};

struct ssh2_userkey *ssh2_load_userkey(char *filename, char *passphrase) {
    FILE *fp;
    char header[40], *b, *comment, *hash;
    const struct ssh_signkey *alg;
    struct ssh2_userkey *ret;
    int cipher, cipherblk;
    unsigned char *public_blob, *private_blob;
    int public_blob_len, private_blob_len;
    int i;

    ret = NULL;			       /* return NULL for most errors */
    comment = hash = NULL;
    public_blob = private_blob = NULL;

    fp = fopen(filename, "rb");
    if (!fp)
	goto error;

    /* Read the first header line which contains the key type. */
    if (!read_header(fp, header) || 0!=strcmp(header, "PuTTY-User-Key-File-1"))
	goto error;
    if ((b = read_body(fp)) == NULL)
	goto error;
    /* Select key algorithm structure. Currently only ssh-rsa. */
    if (!strcmp(b, "ssh-rsa"))
	alg = &ssh_rsa;
    else {
	sfree(b);
	goto error;
    }
    sfree(b);
    
    /* Read the Encryption header line. */
    if (!read_header(fp, header) || 0!=strcmp(header, "Encryption"))
	goto error;
    if ((b = read_body(fp)) == NULL)
	goto error;
    if (!strcmp(b, "aes256-cbc")) {
	cipher = 1; cipherblk = 16;
    } else if (!strcmp(b, "none")) {
	cipher = 0; cipherblk = 1;
    } else {
	sfree(b);
	goto error;
    }
    sfree(b);

    /* Read the Comment header line. */
    if (!read_header(fp, header) || 0!=strcmp(header, "Comment"))
	goto error;
    if ((comment = read_body(fp)) == NULL)
	goto error;

    /* Read the Public-Lines header line and the public blob. */
    if (!read_header(fp, header) || 0!=strcmp(header, "Public-Lines"))
	goto error;
    if ((b = read_body(fp)) == NULL)
	goto error;
    i = atoi(b);
    sfree(b);
    if ((public_blob = read_blob(fp, i, &public_blob_len)) == NULL)
	goto error;

    /* Read the Private-Lines header line and the Private blob. */
    if (!read_header(fp, header) || 0!=strcmp(header, "Private-Lines"))
	goto error;
    if ((b = read_body(fp)) == NULL)
	goto error;
    i = atoi(b);
    sfree(b);
    if ((private_blob = read_blob(fp, i, &private_blob_len)) == NULL)
	goto error;

    /* Read the Private-Hash header line. */
    if (!read_header(fp, header) || 0!=strcmp(header, "Private-Hash"))
	goto error;
    if ((hash = read_body(fp)) == NULL)
	goto error;

    fclose(fp);
    fp = NULL;

    /*
     * Decrypt the private blob.
     */
    if (cipher) {
	unsigned char key[40];
	SHA_State s;
	int passlen;

	if (!passphrase)
	    goto error;
	if (private_blob_len % cipherblk)
	    goto error;

	passlen = strlen(passphrase);

	SHA_Init(&s);
	SHA_Bytes(&s, "\0\0\0\0", 4);
	SHA_Bytes(&s, passphrase, passlen);
	SHA_Final(&s, key+0);
	SHA_Init(&s);
	SHA_Bytes(&s, "\0\0\0\1", 4);
	SHA_Bytes(&s, passphrase, passlen);
	SHA_Final(&s, key+20);
	aes256_decrypt_pubkey(key, private_blob, private_blob_len);
    }

    /*
     * Verify the private hash.
     */
    {
	char realhash[41];
	unsigned char binary[20];

	SHA_Simple(private_blob, private_blob_len, binary);
	for (i = 0; i < 20; i++)
	    sprintf(realhash+2*i, "%02x", binary[i]);

	if (strcmp(hash, realhash)) {
	    /* An incorrect hash is an unconditional Error if the key is
	     * unencrypted. Otherwise, it means Wrong Passphrase. */
	    ret = cipher ? SSH2_WRONG_PASSPHRASE : NULL;
	    goto error;
	}
    }
    sfree(hash);

    /*
     * Create and return the key.
     */
    ret = smalloc(sizeof(struct ssh2_userkey));
    ret->alg = alg;
    ret->comment = comment;
    ret->data = alg->createkey(public_blob, public_blob_len,
			       private_blob, private_blob_len);
    if (!ret->data) {
	sfree(ret->comment);
	sfree(ret);
	ret = NULL;
    }
    sfree(public_blob);
    sfree(private_blob);
    return ret;

    /*
     * Error processing.
     */
    error:
    if (fp) fclose(fp);
    if (comment) sfree(comment);
    if (hash) sfree(hash);
    if (public_blob) sfree(public_blob);
    if (private_blob) sfree(private_blob);
    return ret;
}

char *ssh2_userkey_loadpub(char *filename, char **algorithm, int *pub_blob_len) {
    FILE *fp;
    char header[40], *b;
    const struct ssh_signkey *alg;
    unsigned char *public_blob;
    int public_blob_len;
    int i;

    public_blob = NULL;

    fp = fopen(filename, "rb");
    if (!fp)
	goto error;

    /* Read the first header line which contains the key type. */
    if (!read_header(fp, header) || 0!=strcmp(header, "PuTTY-User-Key-File-1"))
	goto error;
    if ((b = read_body(fp)) == NULL)
	goto error;
    /* Select key algorithm structure. Currently only ssh-rsa. */
    if (!strcmp(b, "ssh-rsa"))
	alg = &ssh_rsa;
    else {
	sfree(b);
	goto error;
    }
    sfree(b);
    
    /* Read the Encryption header line. */
    if (!read_header(fp, header) || 0!=strcmp(header, "Encryption"))
	goto error;
    if ((b = read_body(fp)) == NULL)
	goto error;
    sfree(b);			       /* we don't care */

    /* Read the Comment header line. */
    if (!read_header(fp, header) || 0!=strcmp(header, "Comment"))
	goto error;
    if ((b = read_body(fp)) == NULL)
	goto error;
    sfree(b);			       /* we don't care */

    /* Read the Public-Lines header line and the public blob. */
    if (!read_header(fp, header) || 0!=strcmp(header, "Public-Lines"))
	goto error;
    if ((b = read_body(fp)) == NULL)
	goto error;
    i = atoi(b);
    sfree(b);
    if ((public_blob = read_blob(fp, i, &public_blob_len)) == NULL)
	goto error;

    fclose(fp);
    *pub_blob_len = public_blob_len;
    *algorithm = alg->name;
    return public_blob;

    /*
     * Error processing.
     */
    error:
    if (fp) fclose(fp);
    if (public_blob) sfree(public_blob);
    return NULL;
}

int ssh2_userkey_encrypted(char *filename, char **commentptr) {
    FILE *fp;
    char header[40], *b, *comment;
    int ret;

    if (commentptr) *commentptr = NULL;

    fp = fopen(filename, "rb");
    if (!fp)
	return 0;
    if (!read_header(fp, header) || 0!=strcmp(header, "PuTTY-User-Key-File-1")) {
	fclose(fp); return 0;
    }
    if ((b = read_body(fp)) == NULL) {
	fclose(fp); return 0;
    }
    sfree(b);			       /* we don't care about key type here */
    /* Read the Encryption header line. */
    if (!read_header(fp, header) || 0!=strcmp(header, "Encryption")) {
	fclose(fp); return 0;
    }
    if ((b = read_body(fp)) == NULL) {
	fclose(fp); return 0;
    }

    /* Read the Comment header line. */
    if (!read_header(fp, header) || 0!=strcmp(header, "Comment")) {
	fclose(fp); sfree(b); return 1;
    }
    if ((comment = read_body(fp)) == NULL) {
	fclose(fp); sfree(b); return 1;
    }

    if (commentptr) *commentptr = comment;

    fclose(fp);
    if (!strcmp(b, "aes256-cbc"))
	ret = 1;
    else
	ret = 0;
    sfree(b);
    return ret;
}

int base64_lines(int datalen) {
    /* When encoding, we use 64 chars/line, which equals 48 real chars. */
    return (datalen+47) / 48;
}

void base64_encode_atom(unsigned char *data, int n, char *out) {
    static const char base64_chars[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    unsigned word;

    word = data[0] << 16;
    if (n > 1)
	word |= data[1] << 8;
    if (n > 2)
	word |= data[2];
    out[0] = base64_chars[(word >> 18) & 0x3F];
    out[1] = base64_chars[(word >> 12) & 0x3F];
    if (n > 1)
	out[2] = base64_chars[(word >> 6) & 0x3F];
    else
	out[2] = '=';
    if (n > 2)
	out[3] = base64_chars[word & 0x3F];
    else
	out[3] = '=';
}

void base64_encode(FILE *fp, unsigned char *data, int datalen) {
    int linelen = 0;
    char out[4];
    int n;

    while (datalen > 0) {
	if (linelen >= 64) {
	    linelen = 0;
	    fputc('\n', fp);
	}
	n = (datalen < 3 ? datalen : 3);
	base64_encode_atom(data, n, out);
	data += n;
	datalen -= n;
	fwrite(out, 1, 4, fp);
	linelen += 4;
    }
    fputc('\n', fp);
}

int ssh2_save_userkey(char *filename, struct ssh2_userkey *key, char *passphrase) {
    FILE *fp;
    unsigned char *pub_blob, *priv_blob, *priv_blob_encrypted;
    int pub_blob_len, priv_blob_len, priv_encrypted_len;
    int passlen;
    int cipherblk;
    int i;
    char *cipherstr;
    unsigned char priv_hash[20];

    /*
     * Fetch the key component blobs.
     */
    pub_blob = key->alg->public_blob(key->data, &pub_blob_len);
    priv_blob = key->alg->private_blob(key->data, &priv_blob_len);
    if (!pub_blob || !priv_blob) {
	sfree(pub_blob);
	sfree(priv_blob);
	return 0;
    }

    /*
     * Determine encryption details, and encrypt the private blob.
     */
    if (passphrase) {
	cipherstr = "aes256-cbc";
	cipherblk = 16;
    } else {
	cipherstr = "none";
	cipherblk = 1;
    }
    priv_encrypted_len = priv_blob_len + cipherblk - 1;
    priv_encrypted_len -= priv_encrypted_len % cipherblk;
    priv_blob_encrypted = smalloc(priv_encrypted_len);
    memset(priv_blob_encrypted, 0, priv_encrypted_len);
    memcpy(priv_blob_encrypted, priv_blob, priv_blob_len);
    /* Create padding based on the SHA hash of the unpadded blob. This prevents
     * too easy a known-plaintext attack on the last block. */
    SHA_Simple(priv_blob, priv_blob_len, priv_hash);
    assert(priv_encrypted_len - priv_blob_len < 20);
    memcpy(priv_blob_encrypted + priv_blob_len, priv_hash,
	   priv_encrypted_len - priv_blob_len);

    /* Now create the _real_ private hash. */
    SHA_Simple(priv_blob_encrypted, priv_encrypted_len, priv_hash);

    if (passphrase) {
	char key[40];
	SHA_State s;

	passlen = strlen(passphrase);

	SHA_Init(&s);
	SHA_Bytes(&s, "\0\0\0\0", 4);
	SHA_Bytes(&s, passphrase, passlen);
	SHA_Final(&s, key+0);
	SHA_Init(&s);
	SHA_Bytes(&s, "\0\0\0\1", 4);
	SHA_Bytes(&s, passphrase, passlen);
	SHA_Final(&s, key+20);
	aes256_encrypt_pubkey(key, priv_blob_encrypted, priv_encrypted_len);
    }

    fp = fopen(filename, "w");
    if (!fp)
	return 0;
    fprintf(fp, "PuTTY-User-Key-File-1: %s\n", key->alg->name);
    fprintf(fp, "Encryption: %s\n", cipherstr);
    fprintf(fp, "Comment: %s\n", key->comment);
    fprintf(fp, "Public-Lines: %d\n", base64_lines(pub_blob_len));
    base64_encode(fp, pub_blob, pub_blob_len);
    fprintf(fp, "Private-Lines: %d\n", base64_lines(priv_encrypted_len));
    base64_encode(fp, priv_blob_encrypted, priv_encrypted_len);
    fprintf(fp, "Private-Hash: ");
    for (i = 0; i < 20; i++)
	fprintf(fp, "%02x", priv_hash[i]);
    fprintf(fp, "\n");
    fclose(fp);
    return 1;
}

/* ----------------------------------------------------------------------
 * A function to determine which version of SSH to try on a private
 * key file. Returns 0 on failure, 1 or 2 on success.
 */
int keyfile_version(char *filename) {
    FILE *fp;
    int i;

    fp = fopen(filename, "r");
    if (!fp)
	return 0;
    i = fgetc(fp);
    fclose(fp);
    if (i == 'S')
	return 1;		       /* "SSH PRIVATE KEY FORMAT" etc */
    if (i == 'P')		       /* "PuTTY-User-Key-File" etc */
	return 2;
    return 0;			       /* unrecognised or EOF */
}
