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

/*
 * A Bignum is stored as a sequence of `unsigned short' words. The
 * first tells how many remain; the remaining ones are digits, LS
 * first.
 */
typedef unsigned short *Bignum;

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

struct ssh_hostkey {
    void (*setkey)(char *data, int len);
    char *(*fmtkey)(void);
    char *(*fingerprint)(void);
    int (*verifysig)(char *sig, int siglen, char *data, int datalen);
    char *name;
    char *keytype;                     /* for host key cache */
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

Bignum newbn(int length);
Bignum copybn(Bignum b);
void freebn(Bignum b);
void modpow(Bignum base, Bignum exp, Bignum mod, Bignum result);
void modmul(Bignum a, Bignum b, Bignum mod, Bignum result);
void decbn(Bignum n);
extern Bignum Zero, One;
int ssh1_read_bignum(unsigned char *data, Bignum *result);
int ssh1_bignum_bitcount(Bignum bn);
int ssh1_bignum_length(Bignum bn);
int bignum_byte(Bignum bn, int i);
int ssh1_write_bignum(void *data, Bignum bn);

Bignum dh_create_e(void);
Bignum dh_find_K(Bignum f);

int loadrsakey(char *filename, struct RSAKey *key, char *passphrase);
int rsakey_encrypted(char *filename, char **comment);

void des3_decrypt_pubkey(unsigned char *key,
                         unsigned char *blk, int len);
