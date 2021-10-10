/*
 * Header file shared between bidi.c and its tests. Not used by
 * anything outside the bidi subsystem.
 */

#ifndef PUTTY_BIDI_H
#define PUTTY_BIDI_H

#define LMASK   0x3F    /* Embedding Level mask */
#define OMASK   0xC0    /* Override mask */
#define OISL    0x80    /* Override is L */
#define OISR    0x40    /* Override is R */

/* Shaping Helpers */
#define STYPE(xh) ((((xh) >= SHAPE_FIRST) && ((xh) <= SHAPE_LAST)) ? \
shapetypes[(xh)-SHAPE_FIRST].type : SU) /*))*/
#define SISOLATED(xh) (shapetypes[(xh)-SHAPE_FIRST].form_b)
#define SFINAL(xh) ((xh)+1)
#define SINITIAL(xh) ((xh)+2)
#define SMEDIAL(ch) ((ch)+3)

#define leastGreaterOdd(x) ( ((x)+1) | 1 )
#define leastGreaterEven(x) ( ((x)+2) &~ 1 )

/* Function declarations used outside bidi.c */
unsigned char bidi_getType(int ch);

/* character types */
enum {
    L,
    LRE,
    LRO,
    R,
    AL,
    RLE,
    RLO,
    PDF,
    EN,
    ES,
    ET,
    AN,
    CS,
    NSM,
    BN,
    B,
    S,
    WS,
    ON
};

/* Shaping Types */
enum {
    SL, /* Left-Joining, doesn't exist in U+0600 - U+06FF */
    SR, /* Right-Joining, ie has Isolated, Final */
    SD, /* Dual-Joining, ie has Isolated, Final, Initial, Medial */
    SU, /* Non-Joining */
    SC  /* Join-Causing, like U+0640 (TATWEEL) */
};

#endif /* PUTTY_BIDI_H */
