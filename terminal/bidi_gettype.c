/*
 * Standalone test program that exposes the minibidi getType function.
 */

#include <stdio.h>
#include <assert.h>

#include "putty.h"
#include "misc.h"
#include "bidi.h"

void out_of_memory(void)
{
    fprintf(stderr, "out of memory!\n");
    exit(2);
}

#define TYPETONAME(X) #X,
static const char *const typenames[] = { BIDI_CHAR_TYPE_LIST(TYPETONAME) };
#undef TYPETONAME

int main(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        unsigned long chr = strtoul(argv[i], NULL, 0);
        int type = bidi_getType(chr);
        printf("U+%04x: %s\n", (unsigned)chr, typenames[type]);
    }

    return 0;
}
