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

int main(int argc, char **argv)
{
    static const struct { int type; char *name; } typetoname[] = {
#define TYPETONAME(X) { X , #X }
        TYPETONAME(L),
        TYPETONAME(LRE),
        TYPETONAME(LRO),
        TYPETONAME(R),
        TYPETONAME(AL),
        TYPETONAME(RLE),
        TYPETONAME(RLO),
        TYPETONAME(PDF),
        TYPETONAME(EN),
        TYPETONAME(ES),
        TYPETONAME(ET),
        TYPETONAME(AN),
        TYPETONAME(CS),
        TYPETONAME(NSM),
        TYPETONAME(BN),
        TYPETONAME(B),
        TYPETONAME(S),
        TYPETONAME(WS),
        TYPETONAME(ON),
#undef TYPETONAME
    };
    int i;

    for (i = 1; i < argc; i++) {
        unsigned long chr = strtoul(argv[i], NULL, 0);
        int type = bidi_getType(chr);
        assert(typetoname[type].type == type);
        printf("U+%04x: %s\n", (unsigned)chr, typetoname[type].name);
    }

    return 0;
}
