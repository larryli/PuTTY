/*
 * SSH agent client code.
 */

#include <stdio.h>
#include <stdlib.h>

#include "misc.h"
#include "puttymem.h"

#define GET_32BIT(cp) \
    (((unsigned long)(unsigned char)(cp)[0] << 24) | \
    ((unsigned long)(unsigned char)(cp)[1] << 16) | \
    ((unsigned long)(unsigned char)(cp)[2] << 8) | \
    ((unsigned long)(unsigned char)(cp)[3]))

int agent_exists(void)
{
    return FALSE;		       /* FIXME */
}

void agent_query(void *in, int inlen, void **out, int *outlen)
{
    /* FIXME */
}
