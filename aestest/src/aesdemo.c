/**
 * aesdemo.c
 *
 * Demonstration of AES cryptoalgorithm
 *
 * @author kryukov@frtk.ru
 * @version 2.0
 *
 * For Putty AES NI project
 * http://putty-aes-ni.googlecode.com/
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "coverage.h"

#define BUF_LEN (1 << 28)

void cipher(unsigned char* block)
{
    unsigned char key[32] = "imtheoperatorwithmypocketcalcula";
    unsigned char iv[16] = "initializationve";
    void *handle = aes_make_context();
    aes256_key(handle, key);
    aes_iv(handle, iv);
#ifdef DECODE
    aes_ssh2_decrypt_blk(handle, block, BUF_LEN);
#else
    aes_ssh2_encrypt_blk(handle, block, BUF_LEN);
#endif
    aes_free_context(handle);
}

int ciphCopy(char* src, char* dst)
{
    int result = 0;
    FILE* f_dst;
    FILE* f_src;
    unsigned char* buf;

    /* Source file processing */
    f_src = fopen(src, "rb");
    if (f_src == NULL)
    {
        fprintf(stderr, "Failed to open '%s'\n", src);
        return 1;
    }

    /* Destination file processing */
    f_dst = fopen(dst, "wb");
    if (f_dst == NULL)
    {
        fprintf(stderr, "Failed to create '%s'\n", dst);
        fclose(f_src);
        return 1;
    }

    buf = (unsigned char*)calloc(BUF_LEN, sizeof(unsigned char)); /* Read buffer */
    while (result = fread(buf, sizeof(unsigned char), BUF_LEN, f_src), result > 0)
    {
        if (result == -1)
        {
            fprintf(stderr, "Failed to read from '%s'\n", src);
            fclose(f_src);
            fclose(f_dst);
            return 1;
        }
        cipher(buf);
        result = fwrite(buf, sizeof(unsigned char), result, f_dst);
        fflush(f_dst);
        if (result == -1)
        {
            fprintf(stderr,"Failed to write to '%s'\n", dst);
            fclose(f_src);
            fclose(f_dst);
            return 1;
        }
    }
    free(buf);

    fclose(f_src);
    fclose(f_dst);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        fprintf(stderr,
            "Syntax error! First parameter is source file, second is dest file\n");
        return 1;
    }
    return ciphCopy(argv[1], argv[2]);
}
