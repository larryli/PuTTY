/*
 * Code for PuTTY to import and export private key files in other
 * SSH clients' formats.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>

#include "ssh.h"
#include "misc.h"

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

int openssh_encrypted(char *filename);
struct ssh2_userkey *openssh_read(char *filename, char *passphrase);

int sshcom_encrypted(char *filename, char **comment);
struct ssh2_userkey *sshcom_read(char *filename, char *passphrase);

/*
 * Given a key type, determine whether we know how to import it.
 */
int import_possible(int type)
{
    if (type == SSH_KEYTYPE_OPENSSH)
	return 1;
    if (type == SSH_KEYTYPE_SSHCOM)
	return 1;
    return 0;
}

/*
 * Given a key type, determine what native key type
 * (SSH_KEYTYPE_SSH1 or SSH_KEYTYPE_SSH2) it will come out as once
 * we've imported it.
 */
int import_target_type(int type)
{
    /*
     * There are no known foreign SSH1 key formats.
     */
    return SSH_KEYTYPE_SSH2;
}

/*
 * Determine whether a foreign key is encrypted.
 */
int import_encrypted(char *filename, int type, char **comment)
{
    if (type == SSH_KEYTYPE_OPENSSH) {
	*comment = filename;	       /* OpenSSH doesn't do key comments */
	return openssh_encrypted(filename);
    }
    if (type == SSH_KEYTYPE_SSHCOM) {
	return sshcom_encrypted(filename, comment);
    }
    return 0;
}

/*
 * Import an SSH1 key.
 */
int import_ssh1(char *filename, int type, struct RSAKey *key, char *passphrase)
{
    return 0;
}

/*
 * Import an SSH2 key.
 */
struct ssh2_userkey *import_ssh2(char *filename, int type, char *passphrase)
{
    if (type == SSH_KEYTYPE_OPENSSH)
	return openssh_read(filename, passphrase);
    if (type == SSH_KEYTYPE_SSHCOM)
	return sshcom_read(filename, passphrase);
    return NULL;
}

/* ----------------------------------------------------------------------
 * Helper routines. (The base64 ones are defined in sshpubk.c.)
 */

#define isbase64(c) (    ((c) >= 'A' && (c) <= 'Z') || \
                         ((c) >= 'a' && (c) <= 'z') || \
                         ((c) >= '0' && (c) <= '9') || \
                         (c) == '+' || (c) == '/' || (c) == '=' \
                         )

extern int base64_decode_atom(char *atom, unsigned char *out);
extern int base64_lines(int datalen);
extern void base64_encode_atom(unsigned char *data, int n, char *out);
extern void base64_encode(FILE * fp, unsigned char *data, int datalen);

/*
 * Read an ASN.1/BER identifier and length pair.
 * 
 * Flags are a combination of the #defines listed below.
 * 
 * Returns -1 if unsuccessful; otherwise returns the number of
 * bytes used out of the source data.
 */

/* ASN.1 tag classes. */
#define ASN1_CLASS_UNIVERSAL        (0 << 6)
#define ASN1_CLASS_APPLICATION      (1 << 6)
#define ASN1_CLASS_CONTEXT_SPECIFIC (2 << 6)
#define ASN1_CLASS_PRIVATE          (3 << 6)
#define ASN1_CLASS_MASK             (3 << 6)

/* Primitive versus constructed bit. */
#define ASN1_CONSTRUCTED            (1 << 5)

int ber_read_id_len(void *source, int sourcelen,
		    int *id, int *length, int *flags)
{
    unsigned char *p = (unsigned char *) source;

    if (sourcelen == 0)
	return -1;

    *flags = (*p & 0xE0);
    if ((*p & 0x1F) == 0x1F) {
	*id = 0;
	while (*p & 0x80) {
	    *id = (*id << 7) | (*p & 0x7F);
	    p++, sourcelen--;
	    if (sourcelen == 0)
		return -1;
	}
	*id = (*id << 7) | (*p & 0x7F);
	p++, sourcelen--;
    } else {
	*id = *p & 0x1F;
	p++, sourcelen--;
    }

    if (sourcelen == 0)
	return -1;

    if (*p & 0x80) {
	int n = *p & 0x7F;
	p++, sourcelen--;
	if (sourcelen < n)
	    return -1;
	*length = 0;
	while (n--)
	    *length = (*length << 8) | (*p++);
	sourcelen -= n;
    } else {
	*length = *p;
	p++, sourcelen--;
    }

    return p - (unsigned char *) source;
}

static int put_string(void *target, void *data, int len)
{
    unsigned char *d = (unsigned char *)target;

    PUT_32BIT(d, len);
    memcpy(d+4, data, len);
    return len+4;
}

static int put_mp(void *target, void *data, int len)
{
    unsigned char *d = (unsigned char *)target;
    unsigned char *i = (unsigned char *)data;

    if (*i & 0x80) {
        PUT_32BIT(d, len+1);
        d[4] = 0;
        memcpy(d+5, data, len);
        return len+5;
    } else {
        PUT_32BIT(d, len);
        memcpy(d+4, data, len);
        return len+4;
    }
}

/* ----------------------------------------------------------------------
 * Code to read OpenSSH private keys.
 */

enum { OSSH_DSA, OSSH_RSA };
struct openssh_key {
    int type;
    int encrypted;
    char iv[32];
    unsigned char *keyblob;
    int keyblob_len, keyblob_size;
};

struct openssh_key *load_openssh_key(char *filename)
{
    struct openssh_key *ret;
    FILE *fp;
    char buffer[256];
    char *errmsg, *p;
    int headers_done;
    char base64_bit[4];
    int base64_chars = 0;

    ret = smalloc(sizeof(*ret));
    ret->keyblob = NULL;
    ret->keyblob_len = ret->keyblob_size = 0;
    ret->encrypted = 0;
    memset(ret->iv, 0, sizeof(ret->iv));

    fp = fopen(filename, "r");
    if (!fp) {
	errmsg = "Unable to open key file";
	goto error;
    }
    if (!fgets(buffer, sizeof(buffer), fp) ||
	0 != strncmp(buffer, "-----BEGIN ", 11) ||
	0 != strcmp(buffer+strlen(buffer)-17, "PRIVATE KEY-----\n")) {
	errmsg = "File does not begin with OpenSSH key header";
	goto error;
    }
    if (!strcmp(buffer, "-----BEGIN RSA PRIVATE KEY-----\n"))
	ret->type = OSSH_RSA;
    else if (!strcmp(buffer, "-----BEGIN DSA PRIVATE KEY-----\n"))
	ret->type = OSSH_DSA;
    else {
	errmsg = "Unrecognised key type";
	goto error;
    }

    headers_done = 0;
    while (1) {
	if (!fgets(buffer, sizeof(buffer), fp)) {
	    errmsg = "Unexpected end of file";
	    goto error;
	}
	if (0 == strncmp(buffer, "-----END ", 9) &&
	    0 == strcmp(buffer+strlen(buffer)-17, "PRIVATE KEY-----\n"))
	    break;		       /* done */
	if ((p = strchr(buffer, ':')) != NULL) {
	    if (headers_done) {
		errmsg = "Header found in body of key data";
		goto error;
	    }
	    *p++ = '\0';
	    while (*p && isspace((unsigned char)*p)) p++;
	    if (!strcmp(buffer, "Proc-Type")) {
		if (p[0] != '4' || p[1] != ',') {
		    errmsg = "Proc-Type is not 4 (only 4 is supported)";
		    goto error;
		}
		p += 2;
		if (!strcmp(p, "ENCRYPTED\n"))
		    ret->encrypted = 1;
	    } else if (!strcmp(buffer, "DEK-Info")) {
		int i, j;

		if (strncmp(p, "DES-EDE3-CBC,", 13)) {
		    errmsg = "Ciphers other than DES-EDE3-CBC not supported";
		    goto error;
		}
		p += 13;
		for (i = 0; i < 8; i++) {
		    if (1 != sscanf(p, "%2x", &j))
			break;
		    ret->iv[i] = j;
		    p += 2;
		}
		if (i < 8) {
		    errmsg = "Expected 16-digit iv in DEK-Info";
		    goto error;
		}
	    }
	} else {
	    headers_done = 1;

	    p = buffer;
	    while (isbase64(*p)) {
                base64_bit[base64_chars++] = *p;
                if (base64_chars == 4) {
                    unsigned char out[3];
                    int len;

                    base64_chars = 0;

                    len = base64_decode_atom(base64_bit, out);

                    if (len <= 0) {
                        errmsg = "Invalid base64 encoding";
                        goto error;
                    }

                    if (ret->keyblob_len + len > ret->keyblob_size) {
                        ret->keyblob_size = ret->keyblob_len + len + 256;
                        ret->keyblob = srealloc(ret->keyblob, ret->keyblob_size);
                    }

                    memcpy(ret->keyblob + ret->keyblob_len, out, len);
                    ret->keyblob_len += len;

                    memset(out, 0, sizeof(out));
                }

		p++;
	    }
	}
    }

    if (ret->keyblob_len == 0 || !ret->keyblob) {
	errmsg = "Key body not present";
	goto error;
    }

    if (ret->encrypted && ret->keyblob_len % 8 != 0) {
	errmsg = "Encrypted key blob is not a multiple of cipher block size";
	goto error;
    }

    memset(buffer, 0, sizeof(buffer));
    memset(base64_bit, 0, sizeof(base64_bit));
    return ret;

    error:
    memset(buffer, 0, sizeof(buffer));
    memset(base64_bit, 0, sizeof(base64_bit));
    if (ret) {
	if (ret->keyblob) {
            memset(ret->keyblob, 0, ret->keyblob_size);
            sfree(ret->keyblob);
        }
        memset(&ret, 0, sizeof(ret));
	sfree(ret);
    }
    return NULL;
}

int openssh_encrypted(char *filename)
{
    struct openssh_key *key = load_openssh_key(filename);
    int ret;

    if (!key)
	return 0;
    ret = key->encrypted;
    memset(key->keyblob, 0, key->keyblob_size);
    sfree(key->keyblob);
    memset(&key, 0, sizeof(key));
    sfree(key);
    return ret;
}

struct ssh2_userkey *openssh_read(char *filename, char *passphrase)
{
    struct openssh_key *key = load_openssh_key(filename);
    struct ssh2_userkey *retkey;
    unsigned char *p;
    int ret, id, len, flags;
    int i, num_integers;
    struct ssh2_userkey *retval = NULL;
    char *errmsg;
    unsigned char *blob;
    int blobsize, blobptr, privptr;
    char *modptr;
    int modlen;

    if (!key)
	return NULL;

    if (key->encrypted) {
	/*
	 * Derive encryption key from passphrase and iv/salt:
	 * 
	 *  - let block A equal MD5(passphrase || iv)
	 *  - let block B equal MD5(A || passphrase || iv)
	 *  - block C would be MD5(B || passphrase || iv) and so on
	 *  - encryption key is the first N bytes of A || B
	 */
	struct MD5Context md5c;
	unsigned char keybuf[32];

	MD5Init(&md5c);
	MD5Update(&md5c, passphrase, strlen(passphrase));
	MD5Update(&md5c, key->iv, 8);
	MD5Final(keybuf, &md5c);

	MD5Init(&md5c);
	MD5Update(&md5c, keybuf, 16);
	MD5Update(&md5c, passphrase, strlen(passphrase));
	MD5Update(&md5c, key->iv, 8);
	MD5Final(keybuf+16, &md5c);

	/*
	 * Now decrypt the key blob.
	 */
	des3_decrypt_pubkey_ossh(keybuf, key->iv,
				 key->keyblob, key->keyblob_len);

        memset(&md5c, 0, sizeof(md5c));
        memset(keybuf, 0, sizeof(keybuf));
    }

    /*
     * Now we have a decrypted key blob, which contains an ASN.1
     * encoded private key. We must now untangle the ASN.1.
     *
     * We expect the whole key blob to be formatted as a SEQUENCE
     * (0x30 followed by a length code indicating that the rest of
     * the blob is part of the sequence). Within that SEQUENCE we
     * expect to see a bunch of INTEGERs. What those integers mean
     * depends on the key type:
     *
     *  - For RSA, we expect the integers to be 0, n, e, d, p, q,
     *    dmp1, dmq1, iqmp in that order. (The last three are d mod
     *    (p-1), d mod (q-1), inverse of q mod p respectively.)
     *
     *  - For DSA, we expect them to be 0, p, q, g, y, x in that
     *    order.
     */
    
    p = key->keyblob;

    /* Expect the SEQUENCE header. Take its absence as a failure to decrypt. */
    ret = ber_read_id_len(p, key->keyblob_len, &id, &len, &flags);
    p += ret;
    if (ret < 0 || id != 16) {
	errmsg = "ASN.1 decoding failure";
	retval = SSH2_WRONG_PASSPHRASE;
	goto error;
    }

    /* Expect a load of INTEGERs. */
    if (key->type == OSSH_RSA)
	num_integers = 9;
    else if (key->type == OSSH_DSA)
	num_integers = 6;

    /*
     * Space to create key blob in.
     */
    blobsize = 256+key->keyblob_len;
    blob = smalloc(blobsize);
    PUT_32BIT(blob, 7);
    if (key->type == OSSH_DSA)
	memcpy(blob+4, "ssh-dss", 7);
    else if (key->type == OSSH_RSA)
	memcpy(blob+4, "ssh-rsa", 7);
    blobptr = 4+7;
    privptr = -1;

    for (i = 0; i < num_integers; i++) {
	ret = ber_read_id_len(p, key->keyblob+key->keyblob_len-p,
			      &id, &len, &flags);
	p += ret;
	if (ret < 0 || id != 2 ||
	    key->keyblob+key->keyblob_len-p < len) {
	    errmsg = "ASN.1 decoding failure";
	    goto error;
	}

	if (i == 0) {
	    /*
	     * The first integer should be zero always (I think
	     * this is some sort of version indication).
	     */
	    if (len != 1 || p[0] != 0) {
		errmsg = "Version number mismatch";
		goto error;
	    }
	} else if (key->type == OSSH_RSA) {
	    /*
	     * Integers 1 and 2 go into the public blob but in the
	     * opposite order; integers 3, 4, 5 and 8 go into the
	     * private blob. The other two (6 and 7) are ignored.
	     */
	    if (i == 1) {
		/* Save the details for after we deal with number 2. */
		modptr = p;
		modlen = len;
	    } else if (i != 6 && i != 7) {
		PUT_32BIT(blob+blobptr, len);
		memcpy(blob+blobptr+4, p, len);
		blobptr += 4+len;
		if (i == 2) {
		    PUT_32BIT(blob+blobptr, modlen);
		    memcpy(blob+blobptr+4, modptr, modlen);
		    blobptr += 4+modlen;
		    privptr = blobptr;
		}
	    }
	} else if (key->type == OSSH_DSA) {
	    /*
	     * Integers 1-4 go into the public blob; integer 5 goes
	     * into the private blob.
	     */
	    PUT_32BIT(blob+blobptr, len);
	    memcpy(blob+blobptr+4, p, len);
	    blobptr += 4+len;
	    if (i == 4)
		privptr = blobptr;
	}

	/* Skip past the number. */
	p += len;
    }

    /*
     * Now put together the actual key. Simplest way to do this is
     * to assemble our own key blobs and feed them to the createkey
     * functions; this is a bit faffy but it does mean we get all
     * the sanity checks for free.
     */
    assert(privptr > 0);	       /* should have bombed by now if not */
    retkey = smalloc(sizeof(struct ssh2_userkey));
    retkey->alg = (key->type == OSSH_RSA ? &ssh_rsa : &ssh_dss);
    retkey->data = retkey->alg->createkey(blob, privptr,
					  blob+privptr, blobptr-privptr);
    if (!retkey->data) {
	sfree(retkey);
	errmsg = "unable to create key data structure";
	goto error;
    }

    retkey->comment = dupstr("imported-openssh-key");
    errmsg = NULL;                     /* no error */
    retval = retkey;

    error:
    if (blob) {
        memset(blob, 0, blobsize);
        sfree(blob);
    }
    memset(key->keyblob, 0, key->keyblob_size);
    sfree(key->keyblob);
    memset(&key, 0, sizeof(key));
    sfree(key);
    return retval;
}

/* ----------------------------------------------------------------------
 * Code to read ssh.com private keys.
 */

/*
 * The format of the base64 blob is largely ssh2-packet-formatted,
 * except that mpints are a bit different: they're more like the
 * old ssh1 mpint. You have a 32-bit bit count N, followed by
 * (N+7)/8 bytes of data.
 * 
 * So. The blob contains:
 * 
 *  - uint32 0x3f6ff9eb       (magic number)
 *  - uint32 size             (total blob size)
 *  - string key-type         (see below)
 *  - string cipher-type      (tells you if key is encrypted)
 *  - string encrypted-blob
 * 
 * (The first size field includes the size field itself and the
 * magic number before it. All other size fields are ordinary ssh2
 * strings, so the size field indicates how much data is to
 * _follow_.)
 * 
 * The encrypted blob, once decrypted, contains a single string
 * which in turn contains the payload. (This allows padding to be
 * added after that string while still making it clear where the
 * real payload ends. Also it probably makes for a reasonable
 * decryption check.)
 * 
 * The payload blob, for an RSA key, contains:
 *  - mpint e
 *  - mpint d
 *  - mpint n  (yes, the public and private stuff is intermixed)
 *  - mpint u  (presumably inverse of p mod q)
 *  - mpint p  (p is the smaller prime)
 *  - mpint q  (q is the larger)
 * 
 * For a DSA key, the payload blob contains:
 *  - uint32 0
 *  - mpint p
 *  - mpint g
 *  - mpint q
 *  - mpint y
 *  - mpint x
 * 
 * Alternatively, if the parameters are `predefined', that
 * (0,p,g,q) sequence can be replaced by a uint32 1 and a string
 * containing some predefined parameter specification. *shudder*,
 * but I doubt we'll encounter this in real life.
 * 
 * The key type strings are ghastly. The RSA key I looked at had a
 * type string of
 * 
 *   `if-modn{sign{rsa-pkcs1-sha1},encrypt{rsa-pkcs1v2-oaep}}'
 * 
 * and the DSA key wasn't much better:
 * 
 *   `dl-modp{sign{dsa-nist-sha1},dh{plain}}'
 * 
 * It isn't clear that these will always be the same. I think it
 * might be wise just to look at the `if-modn{sign{rsa' and
 * `dl-modp{sign{dsa' prefixes.
 * 
 * Finally, the encryption. The cipher-type string appears to be
 * either `none' or `3des-cbc'. Looks as if this is SSH2-style
 * 3des-cbc (i.e. outer cbc rather than inner). The key is created
 * from the passphrase by means of yet another hashing faff:
 * 
 *  - first 16 bytes are MD5(passphrase)
 *  - next 16 bytes are MD5(passphrase || first 16 bytes)
 *  - if there were more, they'd be MD5(passphrase || first 32),
 *    and so on.
 */

struct sshcom_key {
    char comment[256];                 /* allowing any length is overkill */
    unsigned char *keyblob;
    int keyblob_len, keyblob_size;
};

struct sshcom_key *load_sshcom_key(char *filename)
{
    struct sshcom_key *ret;
    FILE *fp;
    char buffer[256];
    int len;
    char *errmsg, *p;
    int headers_done;
    char base64_bit[4];
    int base64_chars = 0;

    ret = smalloc(sizeof(*ret));
    ret->comment[0] = '\0';
    ret->keyblob = NULL;
    ret->keyblob_len = ret->keyblob_size = 0;

    fp = fopen(filename, "r");
    if (!fp) {
	errmsg = "Unable to open key file";
	goto error;
    }
    if (!fgets(buffer, sizeof(buffer), fp) ||
	0 != strcmp(buffer, "---- BEGIN SSH2 ENCRYPTED PRIVATE KEY ----\n")) {
	errmsg = "File does not begin with ssh.com key header";
	goto error;
    }

    headers_done = 0;
    while (1) {
	if (!fgets(buffer, sizeof(buffer), fp)) {
	    errmsg = "Unexpected end of file";
	    goto error;
	}
        if (!strcmp(buffer, "---- END SSH2 ENCRYPTED PRIVATE KEY ----\n"))
            break;                     /* done */
	if ((p = strchr(buffer, ':')) != NULL) {
	    if (headers_done) {
		errmsg = "Header found in body of key data";
		goto error;
	    }
	    *p++ = '\0';
	    while (*p && isspace((unsigned char)*p)) p++;
            /*
             * Header lines can end in a trailing backslash for
             * continuation.
             */
            while ((len = strlen(p)) > sizeof(buffer) - (p-buffer) -1 ||
                   p[len-1] != '\n' || p[len-2] == '\\') {
                if (len > (p-buffer) + sizeof(buffer)-2) {
                    errmsg = "Header line too long to deal with";
                    goto error;
                }
                if (!fgets(p+len-2, sizeof(buffer)-(p-buffer)-(len-2), fp)) {
                    errmsg = "Unexpected end of file";
                    goto error;
                }
            }
            p[strcspn(p, "\n")] = '\0';
            if (!strcmp(buffer, "Comment")) {
                /* Strip quotes in comment if present. */
                if (p[0] == '"' && p[strlen(p)-1] == '"') {
                    p++;
                    p[strlen(p)-1] = '\0';
                }
                strncpy(ret->comment, p, sizeof(ret->comment));
                ret->comment[sizeof(ret->comment)-1] = '\0';
            }
	} else {
	    headers_done = 1;

	    p = buffer;
	    while (isbase64(*p)) {
                base64_bit[base64_chars++] = *p;
                if (base64_chars == 4) {
                    unsigned char out[3];

                    base64_chars = 0;

                    len = base64_decode_atom(base64_bit, out);

                    if (len <= 0) {
                        errmsg = "Invalid base64 encoding";
                        goto error;
                    }

                    if (ret->keyblob_len + len > ret->keyblob_size) {
                        ret->keyblob_size = ret->keyblob_len + len + 256;
                        ret->keyblob = srealloc(ret->keyblob, ret->keyblob_size);
                    }

                    memcpy(ret->keyblob + ret->keyblob_len, out, len);
                    ret->keyblob_len += len;
                }

		p++;
	    }
	}
    }

    if (ret->keyblob_len == 0 || !ret->keyblob) {
	errmsg = "Key body not present";
	goto error;
    }

    return ret;

    error:
    if (ret) {
	if (ret->keyblob) {
            memset(ret->keyblob, 0, ret->keyblob_size);
            sfree(ret->keyblob);
        }
        memset(&ret, 0, sizeof(ret));
	sfree(ret);
    }
    return NULL;
}

int sshcom_encrypted(char *filename, char **comment)
{
    struct sshcom_key *key = load_sshcom_key(filename);
    int pos, len, answer;

    *comment = NULL;
    if (!key)
        return 0;

    /*
     * Check magic number.
     */
    if (GET_32BIT(key->keyblob) != 0x3f6ff9eb)
        return 0;                      /* key is invalid */

    /*
     * Find the cipher-type string.
     */
    answer = 0;
    pos = 8;
    if (key->keyblob_len < pos+4)
        goto done;                     /* key is far too short */
    pos += 4 + GET_32BIT(key->keyblob + pos);   /* skip key type */
    if (key->keyblob_len < pos+4)
        goto done;                     /* key is far too short */
    len = GET_32BIT(key->keyblob + pos);   /* find cipher-type length */
    if (key->keyblob_len < pos+4+len)
        goto done;                     /* cipher type string is incomplete */
    if (len != 4 || 0 != memcmp(key->keyblob + pos + 4, "none", 4))
        answer = 1;

    done:
    *comment = dupstr(key->comment);
    memset(key->keyblob, 0, key->keyblob_size);
    sfree(key->keyblob);
    memset(&key, 0, sizeof(key));
    sfree(key);
    return answer;
}

struct mpint_pos { void *start; int bytes; };

int sshcom_read_mpint(void *data, int len, struct mpint_pos *ret)
{
    int bits;
    int bytes;
    unsigned char *d = (unsigned char *) data;

    if (len < 4)
        goto error;
    bits = GET_32BIT(d);

    bytes = (bits + 7) / 8;
    if (len < 4+bytes)
        goto error;

    ret->start = d + 4;
    ret->bytes = bytes;
    return bytes+4;

    error:
    ret->start = NULL;
    ret->bytes = -1;
    return len;                        /* ensure further calls fail as well */
}

struct ssh2_userkey *sshcom_read(char *filename, char *passphrase)
{
    struct sshcom_key *key = load_sshcom_key(filename);
    char *errmsg;
    int pos, len;
    const char prefix_rsa[] = "if-modn{sign{rsa";
    const char prefix_dsa[] = "dl-modp{sign{dsa";
    enum { RSA, DSA } type;
    int encrypted;
    char *ciphertext;
    int cipherlen;
    struct ssh2_userkey *ret = NULL, *retkey;
    const struct ssh_signkey *alg;
    unsigned char *blob = NULL;
    int blobsize, publen, privlen;

    if (!key)
        return NULL;

    /*
     * Check magic number.
     */
    if (GET_32BIT(key->keyblob) != 0x3f6ff9eb) {
        errmsg = "Key does not begin with magic number";
        goto error;
    }

    /*
     * Determine the key type.
     */
    pos = 8;
    if (key->keyblob_len < pos+4 ||
        (len = GET_32BIT(key->keyblob + pos)) > key->keyblob_len - pos - 4) {
        errmsg = "Key blob does not contain a key type string";
        goto error;
    }
    if (len > sizeof(prefix_rsa) - 1 &&
        !memcmp(key->keyblob+pos+4, prefix_rsa, sizeof(prefix_rsa) - 1)) {
        type = RSA;
    } else if (len > sizeof(prefix_dsa) - 1 &&
        !memcmp(key->keyblob+pos+4, prefix_dsa, sizeof(prefix_dsa) - 1)) {
        type = DSA;
    } else {
        errmsg = "Key is of unknown type";
        goto error;
    }
    pos += 4+len;

    /*
     * Determine the cipher type.
     */
    if (key->keyblob_len < pos+4 ||
        (len = GET_32BIT(key->keyblob + pos)) > key->keyblob_len - pos - 4) {
        errmsg = "Key blob does not contain a cipher type string";
        goto error;
    }
    if (len == 4 && !memcmp(key->keyblob+pos+4, "none", 4))
        encrypted = 0;
    else if (len == 8 && !memcmp(key->keyblob+pos+4, "3des-cbc", 8))
        encrypted = 1;
    else {
        errmsg = "Key encryption is of unknown type";
        goto error;
    }
    pos += 4+len;

    /*
     * Get hold of the encrypted part of the key.
     */
    if (key->keyblob_len < pos+4 ||
        (len = GET_32BIT(key->keyblob + pos)) > key->keyblob_len - pos - 4) {
        errmsg = "Key blob does not contain actual key data";
        goto error;
    }
    ciphertext = key->keyblob + pos + 4;
    cipherlen = len;
    if (cipherlen == 0) {
        errmsg = "Length of key data is zero";
        goto error;
    }

    /*
     * Decrypt it if necessary.
     */
    if (encrypted) {
	/*
	 * Derive encryption key from passphrase and iv/salt:
	 * 
	 *  - let block A equal MD5(passphrase)
	 *  - let block B equal MD5(passphrase || A)
	 *  - block C would be MD5(passphrase || A || B) and so on
	 *  - encryption key is the first N bytes of A || B
	 */
	struct MD5Context md5c;
	unsigned char keybuf[32], iv[8];

        if (cipherlen % 8 != 0) {
            errmsg = "Encrypted part of key is not a multiple of cipher block"
                " size";
            goto error;
        }

	MD5Init(&md5c);
	MD5Update(&md5c, passphrase, strlen(passphrase));
	MD5Final(keybuf, &md5c);

	MD5Init(&md5c);
	MD5Update(&md5c, passphrase, strlen(passphrase));
	MD5Update(&md5c, keybuf, 16);
	MD5Final(keybuf+16, &md5c);

	/*
	 * Now decrypt the key blob.
	 */
        memset(iv, 0, sizeof(iv));
	des3_decrypt_pubkey_ossh(keybuf, iv, ciphertext, cipherlen);

        memset(&md5c, 0, sizeof(md5c));
        memset(keybuf, 0, sizeof(keybuf));

        /*
         * Hereafter we return WRONG_PASSPHRASE for any parsing
         * error. (But only if we've just tried to decrypt it!
         * Returning WRONG_PASSPHRASE for an unencrypted key is
         * automatic doom.)
         */
        if (encrypted)
            ret = SSH2_WRONG_PASSPHRASE;
    }

    /*
     * Strip away the containing string to get to the real meat.
     */
    len = GET_32BIT(ciphertext);
    if (len > cipherlen-4) {
        errmsg = "containing string was ill-formed";
        goto error;
    }
    ciphertext += 4;
    cipherlen = len;

    /*
     * Now we break down into RSA versus DSA. In either case we'll
     * construct public and private blobs in our own format, and
     * end up feeding them to alg->createkey().
     */
    blobsize = cipherlen + 256;
    blob = smalloc(blobsize);
    privlen = 0;
    if (type == RSA) {
        struct mpint_pos n, e, d, u, p, q;
        int pos = 0;
        pos += sshcom_read_mpint(ciphertext+pos, cipherlen-pos, &e);
        pos += sshcom_read_mpint(ciphertext+pos, cipherlen-pos, &d);
        pos += sshcom_read_mpint(ciphertext+pos, cipherlen-pos, &n);
        pos += sshcom_read_mpint(ciphertext+pos, cipherlen-pos, &u);
        pos += sshcom_read_mpint(ciphertext+pos, cipherlen-pos, &p);
        pos += sshcom_read_mpint(ciphertext+pos, cipherlen-pos, &q);
        if (!q.start) {
            errmsg = "key data did not contain six integers";
            goto error;
        }

        alg = &ssh_rsa;
        pos = 0;
        pos += put_string(blob+pos, "ssh-rsa", 7);
        pos += put_mp(blob+pos, e.start, e.bytes);
        pos += put_mp(blob+pos, n.start, n.bytes);
        publen = pos;
        pos += put_string(blob+pos, d.start, d.bytes);
        pos += put_mp(blob+pos, q.start, q.bytes);
        pos += put_mp(blob+pos, p.start, p.bytes);
        pos += put_mp(blob+pos, u.start, u.bytes);
        privlen = pos - publen;
    } else if (type == DSA) {
        struct mpint_pos p, q, g, x, y;
        int pos = 4;
        if (GET_32BIT(ciphertext) != 0) {
            errmsg = "predefined DSA parameters not supported";
            goto error;
        }
        pos += sshcom_read_mpint(ciphertext+pos, cipherlen-pos, &p);
        pos += sshcom_read_mpint(ciphertext+pos, cipherlen-pos, &g);
        pos += sshcom_read_mpint(ciphertext+pos, cipherlen-pos, &q);
        pos += sshcom_read_mpint(ciphertext+pos, cipherlen-pos, &y);
        pos += sshcom_read_mpint(ciphertext+pos, cipherlen-pos, &x);
        if (!x.start) {
            errmsg = "key data did not contain five integers";
            goto error;
        }

        alg = &ssh_dss;
        pos = 0;
        pos += put_string(blob+pos, "ssh-dss", 7);
        pos += put_mp(blob+pos, p.start, p.bytes);
        pos += put_mp(blob+pos, q.start, q.bytes);
        pos += put_mp(blob+pos, g.start, g.bytes);
        pos += put_mp(blob+pos, y.start, y.bytes);
        publen = pos;
        pos += put_mp(blob+pos, x.start, x.bytes);
        privlen = pos - publen;
    }

    assert(privlen > 0);	       /* should have bombed by now if not */

    retkey = smalloc(sizeof(struct ssh2_userkey));
    retkey->alg = alg;
    retkey->data = alg->createkey(blob, publen, blob+publen, privlen);
    if (!retkey->data) {
	sfree(retkey);
	errmsg = "unable to create key data structure";
	goto error;
    }
    retkey->comment = dupstr(key->comment);

    errmsg = NULL; /* no error */
    ret = retkey;

    error:
    if (blob) {
        memset(blob, 0, blobsize);
        sfree(blob);
    }
    memset(key->keyblob, 0, key->keyblob_size);
    sfree(key->keyblob);
    memset(&key, 0, sizeof(key));
    sfree(key);
    return ret;
}
