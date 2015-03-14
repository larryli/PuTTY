/**
 * coverage.h
 *
 * Declarations of PuTTY AES functions 
 *
 * @author kryukov@frtk.ru
 * @version 1.0
 *
 * For Putty AES NI project
 * http://putty-aes-ni.googlecode.com/
 */
 
#ifndef AESTEST_COVERAGE_H
#define AESTEST_COVERAGE_H

void *aes_make_context(void);
void aes_free_context(void *handle);
void aes128_key(void *handle, unsigned char *key);
void aes192_key(void *handle, unsigned char *key);
void aes256_key(void *handle, unsigned char *key);
void aes_iv(void *handle, unsigned char *iv);
void aes_ssh2_encrypt_blk(void *handle, unsigned char *blk, int len);
void aes_ssh2_decrypt_blk(void *handle, unsigned char *blk, int len);

#endif /* AESTEST_COVERAGE_H */
