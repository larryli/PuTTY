#define _WIN32_WINNT 0x0400
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wincrypt.h>
#include "ssh.h"

void fatalbox(char *fmt, ...);

static HCRYPTKEY create_des_key(unsigned char *key);


HCRYPTPROV hCryptProv;
HCRYPTKEY hDESKey[2][3] = { {0, 0, 0}, {0, 0, 0} };	/* global for now */


/* use Microsoft Enhanced Cryptographic Service Provider */
#define CSP MS_ENHANCED_PROV


static BYTE PrivateKeyWithExponentOfOne[] = {
    0x07, 0x02, 0x00, 0x00, 0x00, 0xA4, 0x00, 0x00,
    0x52, 0x53, 0x41, 0x32, 0x00, 0x02, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0xAB, 0xEF, 0xFA, 0xC6,
    0x7D, 0xE8, 0xDE, 0xFB, 0x68, 0x38, 0x09, 0x92,
    0xD9, 0x42, 0x7E, 0x6B, 0x89, 0x9E, 0x21, 0xD7,
    0x52, 0x1C, 0x99, 0x3C, 0x17, 0x48, 0x4E, 0x3A,
    0x44, 0x02, 0xF2, 0xFA, 0x74, 0x57, 0xDA, 0xE4,
    0xD3, 0xC0, 0x35, 0x67, 0xFA, 0x6E, 0xDF, 0x78,
    0x4C, 0x75, 0x35, 0x1C, 0xA0, 0x74, 0x49, 0xE3,
    0x20, 0x13, 0x71, 0x35, 0x65, 0xDF, 0x12, 0x20,
    0xF5, 0xF5, 0xF5, 0xC1, 0xED, 0x5C, 0x91, 0x36,
    0x75, 0xB0, 0xA9, 0x9C, 0x04, 0xDB, 0x0C, 0x8C,
    0xBF, 0x99, 0x75, 0x13, 0x7E, 0x87, 0x80, 0x4B,
    0x71, 0x94, 0xB8, 0x00, 0xA0, 0x7D, 0xB7, 0x53,
    0xDD, 0x20, 0x63, 0xEE, 0xF7, 0x83, 0x41, 0xFE,
    0x16, 0xA7, 0x6E, 0xDF, 0x21, 0x7D, 0x76, 0xC0,
    0x85, 0xD5, 0x65, 0x7F, 0x00, 0x23, 0x57, 0x45,
    0x52, 0x02, 0x9D, 0xEA, 0x69, 0xAC, 0x1F, 0xFD,
    0x3F, 0x8C, 0x4A, 0xD0,

    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    0x64, 0xD5, 0xAA, 0xB1,
    0xA6, 0x03, 0x18, 0x92, 0x03, 0xAA, 0x31, 0x2E,
    0x48, 0x4B, 0x65, 0x20, 0x99, 0xCD, 0xC6, 0x0C,
    0x15, 0x0C, 0xBF, 0x3E, 0xFF, 0x78, 0x95, 0x67,
    0xB1, 0x74, 0x5B, 0x60,

    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};



/* ---------------------------------------------------------*
 * Utility functions                                        *
 * ---------------------------------------------------------*/


int crypto_startup()
{
    if (CryptAcquireContext(&hCryptProv, "Putty", CSP, PROV_RSA_FULL,
			    CRYPT_NEWKEYSET) == 0) {
	if (GetLastError() == NTE_EXISTS) {
	    if (CryptAcquireContext(&hCryptProv, "Putty", CSP,
				    PROV_RSA_FULL, 0) == 0) {
		return FALSE;	       /* failed to acquire context - probably
				        * don't have high encryption installed! */
	    }
	} else
	    return FALSE;	       /* failed to acquire context - probably
				        * don't have high encryption installed! */
    }
    return TRUE;
}


void crypto_wrapup()
{
    int i, j;
    for (i = 0; i < 2; i++) {
	for (j = 0; j < 3; j++) {
	    if (hDESKey[i][j])
		CryptDestroyKey(hDESKey[i][j]);
	    hDESKey[i][j] = 0;
	}
    }
    if (hCryptProv)
	CryptReleaseContext(hCryptProv, 0);
    hCryptProv = 0;
}


/* ---------------------------------------------------------*
 * Random number functions                                  *
 * ---------------------------------------------------------*/

int random_byte(void)
{
    unsigned char b;
    if (!CryptGenRandom(hCryptProv, 1, &b))
	fatalbox("random number generator failure!");
    return b;
}

void random_add_noise(void *noise, int length)
{
    /* do nothing */
}
void random_init(void)
{
    /* do nothing */
}
void random_get_savedata(void **data, int *len)
{
    /* do nothing */
}
void noise_get_heavy(void (*func) (void *, int))
{
    /* do nothing */
}
void noise_get_light(void (*func) (void *, int))
{
    /* do nothing */
}
void noise_ultralight(DWORD data)
{
    /* do nothing */
}
void random_save_seed(void)
{
    /* do nothing */
}


/* ---------------------------------------------------------*
 * MD5 hash functions                                       *
 * ---------------------------------------------------------*/


void MD5Init(struct MD5Context *ctx)
{
    if (!CryptCreateHash(hCryptProv, CALG_MD5, 0, 0, &ctx->hHash))
	fatalbox("Error during CryptBeginHash!\n");
}


void MD5Update(struct MD5Context *ctx,
	       unsigned char const *buf, unsigned len)
{
    if (CryptHashData(ctx->hHash, buf, len, 0) == 0)
	fatalbox("Error during CryptHashSessionKey!\n");
}


void MD5Final(unsigned char digest[16], struct MD5Context *ctx)
{
    DWORD cb = 16;
    if (CryptGetHashParam(ctx->hHash, HP_HASHVAL, digest, &cb, 0) == 0)
	fatalbox("Error during CryptGetHashParam!\n");
    if (ctx->hHash)
	CryptDestroyHash(ctx->hHash);
    ctx->hHash = 0;
}


/* ---------------------------------------------------------*
 * RSA public key functions                                 *
 * ---------------------------------------------------------*/

int makekey(unsigned char *data, struct RSAKey *result,
	    unsigned char **keystr)
{

    unsigned char *p = data;
    int i;
    int w, b;

    /* get size (bits) of modulus */
    result->bits = 0;
    for (i = 0; i < 4; i++)
	result->bits = (result->bits << 8) + *p++;

    /* get size (bits) of public exponent */
    w = 0;
    for (i = 0; i < 2; i++)
	w = (w << 8) + *p++;
    b = (w + 7) / 8;		       /* bits -> bytes */

    /* convert exponent to DWORD */
    result->exponent = 0;
    for (i = 0; i < b; i++)
	result->exponent = (result->exponent << 8) + *p++;

    /* get size (bits) of modulus */
    w = 0;
    for (i = 0; i < 2; i++)
	w = (w << 8) + *p++;
    result->bytes = b = (w + 7) / 8;   /* bits -> bytes */

    /* allocate buffer for modulus & copy it */
    result->modulus = malloc(b);
    memcpy(result->modulus, p, b);

    /* update callers pointer */
    if (keystr)
	*keystr = p;		       /* point at key string, second time */

    return (p - data) + b;
}


void rsaencrypt(unsigned char *data, int length, struct RSAKey *rsakey)
{

    int i;
    unsigned char *pKeybuf, *pKeyin;
    HCRYPTKEY hRsaKey;
    PUBLICKEYSTRUC *pBlob;
    RSAPUBKEY *pRPK;
    unsigned char *buf;
    DWORD dlen;
    DWORD bufsize;

    /* allocate buffer for public key blob */
    if ((pBlob = malloc(sizeof(PUBLICKEYSTRUC) + sizeof(RSAPUBKEY) +
			rsakey->bytes)) == NULL)
	fatalbox("Out of memory");

    /* allocate buffer for message encryption block */
    bufsize = (length + rsakey->bytes) << 1;
    if ((buf = malloc(bufsize)) == NULL)
	fatalbox("Out of memory");

    /* construct public key blob from host public key */
    pKeybuf = ((unsigned char *) pBlob) + sizeof(PUBLICKEYSTRUC) +
	sizeof(RSAPUBKEY);
    pKeyin = ((unsigned char *) rsakey->modulus);
    /* change big endian to little endian */
    for (i = 0; i < rsakey->bytes; i++)
	pKeybuf[i] = pKeyin[rsakey->bytes - i - 1];
    pBlob->bType = PUBLICKEYBLOB;
    pBlob->bVersion = 0x02;
    pBlob->reserved = 0;
    pBlob->aiKeyAlg = CALG_RSA_KEYX;
    pRPK =
	(RSAPUBKEY *) (((unsigned char *) pBlob) + sizeof(PUBLICKEYSTRUC));
    pRPK->magic = 0x31415352;	       /* "RSA1" */
    pRPK->bitlen = rsakey->bits;
    pRPK->pubexp = rsakey->exponent;

    /* import public key blob into key container */
    if (CryptImportKey(hCryptProv, (void *) pBlob,
		       sizeof(PUBLICKEYSTRUC) + sizeof(RSAPUBKEY) +
		       rsakey->bytes, 0, 0, &hRsaKey) == 0)
	fatalbox("Error importing RSA key!");

    /* copy message into buffer */
    memcpy(buf, data, length);
    dlen = length;

    /* using host public key, encrypt the message */
    if (CryptEncrypt(hRsaKey, 0, TRUE, 0, buf, &dlen, bufsize) == 0)
	fatalbox("Error encrypting using RSA key!");

    /*
     * For some strange reason, Microsoft CryptEncrypt using public
     * key, returns the cyphertext in backwards (little endian)
     * order, so reverse it!
     */
    for (i = 0; i < (int) dlen; i++)
	data[i] = buf[dlen - i - 1];   /* make it big endian */

    CryptDestroyKey(hRsaKey);
    free(buf);
    free(pBlob);

}


int rsastr_len(struct RSAKey *key)
{
    return 2 * (sizeof(DWORD) + key->bytes) + 10;
}


void rsastr_fmt(char *str, struct RSAKey *key)
{

    int len = 0, i;

    sprintf(str + len, "%04x", key->exponent);
    len += strlen(str + len);

    str[len++] = '/';
    for (i = 1; i < key->bytes; i++) {
	sprintf(str + len, "%02x", key->modulus[i]);
	len += strlen(str + len);
    }
    str[len] = '\0';
}



/* ---------------------------------------------------------*
 * DES encryption / decryption functions                    *
 * ---------------------------------------------------------*/


void des3_sesskey(unsigned char *key)
{
    int i, j;
    for (i = 0; i < 2; i++) {
	for (j = 0; j < 3; j++) {
	    hDESKey[i][j] = create_des_key(key + (j * 8));
	}
    }
}


void des3_encrypt_blk(unsigned char *blk, int len)
{

    DWORD dlen;
    dlen = len;

    if (CryptEncrypt(hDESKey[0][0], 0, FALSE, 0, blk, &dlen, len + 8) == 0)
	fatalbox("Error encrypting block!\n");
    if (CryptDecrypt(hDESKey[0][1], 0, FALSE, 0, blk, &dlen) == 0)
	fatalbox("Error encrypting block!\n");
    if (CryptEncrypt(hDESKey[0][2], 0, FALSE, 0, blk, &dlen, len + 8) == 0)
	fatalbox("Error encrypting block!\n");
}


void des3_decrypt_blk(unsigned char *blk, int len)
{
    DWORD dlen;
    dlen = len;

    if (CryptDecrypt(hDESKey[1][2], 0, FALSE, 0, blk, &dlen) == 0)
	fatalbox("Error decrypting block!\n");
    if (CryptEncrypt(hDESKey[1][1], 0, FALSE, 0, blk, &dlen, len + 8) == 0)
	fatalbox("Error decrypting block!\n");
    if (CryptDecrypt(hDESKey[1][0], 0, FALSE, 0, blk, &dlen) == 0)
	fatalbox("Error decrypting block!\n");
}


struct ssh_cipher ssh_3des = {
    des3_sesskey,
    des3_encrypt_blk,
    des3_decrypt_blk
};


void des_sesskey(unsigned char *key)
{
    int i;
    for (i = 0; i < 2; i++) {
	hDESKey[i][0] = create_des_key(key);
    }
}


void des_encrypt_blk(unsigned char *blk, int len)
{
    DWORD dlen;
    dlen = len;
    if (CryptEncrypt(hDESKey[0][0], 0, FALSE, 0, blk, &dlen, len + 8) == 0)
	fatalbox("Error encrypting block!\n");
}


void des_decrypt_blk(unsigned char *blk, int len)
{
    DWORD dlen;
    dlen = len;
    if (CryptDecrypt(hDESKey[1][0], 0, FALSE, 0, blk, &dlen) == 0)
	fatalbox("Error decrypting block!\n");
}

struct ssh_cipher ssh_des = {
    des_sesskey,
    des_encrypt_blk,
    des_decrypt_blk
};


static HCRYPTKEY create_des_key(unsigned char *key)
{

    HCRYPTKEY hSessionKey, hPrivateKey;
    DWORD dlen = 8;
    BLOBHEADER *pbh;
    char buf[sizeof(BLOBHEADER) + sizeof(ALG_ID) + 256];

    /*
     * Need special private key to encrypt session key so we can
     * import session key, since only encrypted session keys can be
     * imported
     */
    if (CryptImportKey(hCryptProv, PrivateKeyWithExponentOfOne,
		       sizeof(PrivateKeyWithExponentOfOne),
		       0, 0, &hPrivateKey) == 0)
	return 0;

    /* now encrypt session key using special private key */
    memcpy(buf + sizeof(BLOBHEADER) + sizeof(ALG_ID), key, 8);
    if (CryptEncrypt(hPrivateKey, 0, TRUE, 0,
		     buf + sizeof(BLOBHEADER) + sizeof(ALG_ID),
		     &dlen, 256) == 0)
	return 0;

    /* build session key blob */
    pbh = (BLOBHEADER *) buf;
    pbh->bType = SIMPLEBLOB;
    pbh->bVersion = 0x02;
    pbh->reserved = 0;
    pbh->aiKeyAlg = CALG_DES;
    *((ALG_ID *) (buf + sizeof(BLOBHEADER))) = CALG_RSA_KEYX;

    /* import session key into key container */
    if (CryptImportKey(hCryptProv, buf,
		       dlen + sizeof(BLOBHEADER) + sizeof(ALG_ID),
		       hPrivateKey, 0, &hSessionKey) == 0)
	return 0;

    if (hPrivateKey)
	CryptDestroyKey(hPrivateKey);

    return hSessionKey;

}
