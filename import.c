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

/*
 * Given a key type, determine whether we know how to import it.
 */
int import_possible(int type)
{
    if (type == SSH_KEYTYPE_OPENSSH)
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
	    while (isbase64(p[0]) && isbase64(p[1]) &&
		   isbase64(p[2]) && isbase64(p[3])) {
		int len;
		unsigned char out[3];

		len = base64_decode_atom(p, out);

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

		p += 4;
	    }

	    if (isbase64(*p)) {
		errmsg = "base64 characters left at end of line";
		goto error;
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

    return ret;

    error:
    if (ret) {
	if (ret->keyblob) sfree(ret->keyblob);
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
    sfree(key->keyblob);
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
    int blobptr, privptr;
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
    blob = smalloc(256+key->keyblob_len);
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
    if (blob) sfree(blob);
    sfree(key->keyblob);
    sfree(key);
    return retkey;

    error:
    if (blob) sfree(blob);
    sfree(key->keyblob);
    sfree(key);
    return retval;
}
