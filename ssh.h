#include <string.h>

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

struct RSAKey {
    int bits;
    int bytes;
#ifdef MSCRYPTOAPI
    unsigned long exponent;
    unsigned char *modulus;
#else
    void *modulus;
    void *exponent;
#endif
};

int makekey(unsigned char *data, struct RSAKey *result,
	    unsigned char **keystr);
void rsaencrypt(unsigned char *data, int length, struct RSAKey *key);
int rsastr_len(struct RSAKey *key);
void rsastr_fmt(char *str, struct RSAKey *key);

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

struct ssh_cipher {
    void (*sesskey)(unsigned char *key);
    void (*encrypt)(unsigned char *blk, int len);
    void (*decrypt)(unsigned char *blk, int len);
    char *name;
    int blksize;
};

struct ssh_mac {
    void (*sesskey)(unsigned char *key, int len);
    void (*generate)(unsigned char *blk, int len, unsigned long seq);
    int (*verify)(unsigned char *blk, int len, unsigned long seq);
    char *name;
    int len;
};

struct ssh_kex {
    char *name;
};

struct ssh_hostkey {
    char *name;
};

struct ssh_compress {
    char *name;
};

#ifndef MSCRYPTOAPI
void SHATransform(word32 *digest, word32 *data);
#endif

int random_byte(void);
void random_add_noise(void *noise, int length);

void logevent (char *);

/*
 * A Bignum is stored as a sequence of `unsigned short' words. The
 * first tells how many remain; the remaining ones are digits, LS
 * first.
 */
typedef unsigned short *Bignum;

Bignum newbn(int length);
void freebn(Bignum b);
void modpow(Bignum base, Bignum exp, Bignum mod, Bignum result);

Bignum dh_create_e(void);
Bignum dh_find_K(Bignum f);
