#include <string.h>

#define SSH_CIPHER_IDEA		1
#define SSH_CIPHER_DES		2
#define SSH_CIPHER_3DES		3
#define SSH_CIPHER_BLOWFISH	6

struct RSAKey {
    int bits;
    int bytes;
    void *modulus;
    void *exponent;
};

int makekey(unsigned char *data, struct RSAKey *result,
	    unsigned char **keystr);
void rsaencrypt(unsigned char *data, int length, struct RSAKey *key);
int rsastr_len(struct RSAKey *key);
void rsastr_fmt(char *str, struct RSAKey *key);

typedef unsigned int word32;
typedef unsigned int uint32;

unsigned long crc32(const unsigned char *s, unsigned int len);

struct MD5Context {
        uint32 buf[4];
        uint32 bits[2];
        unsigned char in[64];
};

void MD5Init(struct MD5Context *context);
void MD5Update(struct MD5Context *context, unsigned char const *buf,
               unsigned len);
void MD5Final(unsigned char digest[16], struct MD5Context *context);

struct ssh_cipher {
    void (*sesskey)(unsigned char *key);
    void (*encrypt)(unsigned char *blk, int len);
    void (*decrypt)(unsigned char *blk, int len);
};

void SHATransform(word32 *digest, word32 *data);

int random_byte(void);
void random_add_noise(void *noise, int length);
