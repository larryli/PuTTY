#include <string.h>

#include "puttymem.h"

/*
 * Useful thing.
 */
#ifndef lenof
#define lenof(x) ( (sizeof((x))) / (sizeof(*(x))))
#endif

#define SSH_CIPHER_IDEA		1
#define SSH_CIPHER_DES		2
#define SSH_CIPHER_3DES		3
#define SSH_CIPHER_BLOWFISH	6

#ifdef MSCRYPTOAPI
#define APIEXTRA 8
#else
#define APIEXTRA 0
#endif

#ifndef BIGNUM_INTERNAL
typedef void *Bignum;
#endif

struct RSAKey {
    int bits;
    int bytes;
#ifdef MSCRYPTOAPI
    unsigned long exponent;
    unsigned char *modulus;
#else
    Bignum modulus;
    Bignum exponent;
    Bignum private_exponent;
#endif
    char *comment;
};

struct RSAAux {
    Bignum p;
    Bignum q;
    Bignum iqmp;
};

int makekey(unsigned char *data, struct RSAKey *result,
	    unsigned char **keystr, int order);
int makeprivate(unsigned char *data, struct RSAKey *result);
void rsaencrypt(unsigned char *data, int length, struct RSAKey *key);
Bignum rsadecrypt(Bignum input, struct RSAKey *key);
void rsasign(unsigned char *data, int length, struct RSAKey *key);
void rsasanitise(struct RSAKey *key);
int rsastr_len(struct RSAKey *key);
void rsastr_fmt(char *str, struct RSAKey *key);
void rsa_fingerprint(char *str, int len, struct RSAKey *key);
void freersakey(struct RSAKey *key);

typedef unsigned int word32;
typedef unsigned int uint32;

unsigned long crc32(const void *s, size_t len);

typedef struct {
    uint32 h[4];
} MD5_Core_State;

struct MD5Context {
#ifdef MSCRYPTOAPI
    unsigned long hHash;
#else
    MD5_Core_State core;
    unsigned char block[64];
    int blkused;
    uint32 lenhi, lenlo;
#endif
};

void MD5Init(struct MD5Context *context);
void MD5Update(struct MD5Context *context, unsigned char const *buf,
               unsigned len);
void MD5Final(unsigned char digest[16], struct MD5Context *context);

typedef struct {
    uint32 h[5];
    unsigned char block[64];
    int blkused;
    uint32 lenhi, lenlo;
} SHA_State;

void SHA_Init(SHA_State *s);
void SHA_Bytes(SHA_State *s, void *p, int len);
void SHA_Final(SHA_State *s, unsigned char *output);
void SHA_Simple(void *p, int len, unsigned char *output);

struct ssh_cipher {
    void (*sesskey)(unsigned char *key);   /* for ssh 1 */
    void (*setcsiv)(unsigned char *key);   /* for ssh 2 */
    void (*setcskey)(unsigned char *key);   /* for ssh 2 */
    void (*setsciv)(unsigned char *key);   /* for ssh 2 */
    void (*setsckey)(unsigned char *key);   /* for ssh 2 */
    void (*encrypt)(unsigned char *blk, int len);
    void (*decrypt)(unsigned char *blk, int len);
    char *name;
    int blksize;
    int keylen;
};

struct ssh_mac {
    void (*setcskey)(unsigned char *key);
    void (*setsckey)(unsigned char *key);
    void (*generate)(unsigned char *blk, int len, unsigned long seq);
    int (*verify)(unsigned char *blk, int len, unsigned long seq);
    char *name;
    int len;
};

struct ssh_kex {
    /*
     * Plugging in another KEX algorithm requires structural chaos,
     * so it's hard to abstract them into nice little structures
     * like this. Hence, for the moment, this is just a
     * placeholder. I claim justification in the fact that OpenSSH
     * does this too :-)
     */
    char *name;
};

struct ssh_signkey {
    void *(*newkey)(char *data, int len);
    void (*freekey)(void *key);
    char *(*fmtkey)(void *key);
    char *(*fingerprint)(void *key);
    int (*verifysig)(void *key, char *sig, int siglen,
		     char *data, int datalen);
    int (*sign)(void *key, char *sig, int siglen,
		char *data, int datalen);
    char *name;
    char *keytype;                     /* for host key cache */
};

struct ssh_compress {
    char *name;
    void (*compress_init)(void);
    int (*compress)(unsigned char *block, int len,
		    unsigned char **outblock, int *outlen);
    void (*decompress_init)(void);
    int (*decompress)(unsigned char *block, int len,
		      unsigned char **outblock, int *outlen);
};

#ifndef MSCRYPTOAPI
void SHATransform(word32 *digest, word32 *data);
#endif

int random_byte(void);
void random_add_noise(void *noise, int length);
void random_add_heavynoise(void *noise, int length);

void logevent (char *);

Bignum copybn(Bignum b);
Bignum bn_power_2(int n);
void bn_restore_invariant(Bignum b);
Bignum bignum_from_short(unsigned short n);
void freebn(Bignum b);
Bignum modpow(Bignum base, Bignum exp, Bignum mod);
Bignum modmul(Bignum a, Bignum b, Bignum mod);
void decbn(Bignum n);
extern Bignum Zero, One;
Bignum bignum_from_bytes(unsigned char *data, int nbytes);
int ssh1_read_bignum(unsigned char *data, Bignum *result);
int ssh1_bignum_bitcount(Bignum bn);
int ssh1_bignum_length(Bignum bn);
int bignum_byte(Bignum bn, int i);
int bignum_bit(Bignum bn, int i);
void bignum_set_bit(Bignum bn, int i, int value);
int ssh1_write_bignum(void *data, Bignum bn);
Bignum biggcd(Bignum a, Bignum b);
unsigned short bignum_mod_short(Bignum number, unsigned short modulus);
Bignum bignum_add_long(Bignum number, unsigned long addend);
Bignum bigmul(Bignum a, Bignum b);
Bignum modinv(Bignum number, Bignum modulus);
Bignum bignum_bitmask(Bignum number);
Bignum bignum_rshift(Bignum number, int shift);
int bignum_cmp(Bignum a, Bignum b);
char *bignum_decimal(Bignum x);

void dh_setup_group1(void);
void dh_setup_group(Bignum pval, Bignum gval);
void dh_cleanup(void);
Bignum dh_create_e(void);
Bignum dh_find_K(Bignum f);

int loadrsakey(char *filename, struct RSAKey *key, struct RSAAux *aux,
               char *passphrase);
int rsakey_encrypted(char *filename, char **comment);

int saversakey(char *filename, struct RSAKey *key, struct RSAAux *aux,
               char *passphrase);

void des3_decrypt_pubkey(unsigned char *key,
                         unsigned char *blk, int len);
void des3_encrypt_pubkey(unsigned char *key,
                         unsigned char *blk, int len);

/*
 * For progress updates in the key generation utility.
 */
typedef void (*progfn_t)(void *param, int phase, int progress);

int rsa_generate(struct RSAKey *key, struct RSAAux *aux, int bits,
                 progfn_t pfn, void *pfnparam);
Bignum primegen(int bits, int modulus, int residue,
                int phase, progfn_t pfn, void *pfnparam);

/*
 * zlib compression.
 */
void zlib_compress_init(void);
void zlib_decompress_init(void);
int zlib_compress_block(unsigned char *block, int len,
			unsigned char **outblock, int *outlen);
int zlib_decompress_block(unsigned char *block, int len,
			  unsigned char **outblock, int *outlen);
