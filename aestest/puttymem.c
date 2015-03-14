/**
 * puttymem.c
 *
 * Simple implementation of some Putty memory functions
 * for easier test building
 *
 * @author kryukov@frtk.ru
 * @version 1.0
 *
 * For Putty AES NI project
 * http://putty-aes-ni.googlecode.com/
 */

#include <stdlib.h>
#include <limits.h>
#include <string.h>

void *safemalloc(size_t n, size_t size)
{
    void *p;
    if (n > INT_MAX / size) {
        p = NULL;
    }
    else {
        size *= n;
        if (size == 0)
            size = 1;
        p = malloc(size);
    }

    return p;
}

void safefree(void *ptr)
{
    if (ptr)
        free(ptr);
}

void smemclr(void *b, size_t n) {
    volatile char *vp;

    if (b && n > 0) {
        memset(b, 0, n);
        vp = b;
        while (*vp) vp++;
    }
}
