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

/* Bidi character types */
#define BIDI_CHAR_TYPE_LIST(X) \
    X(L)                       \
    X(LRE)                     \
    X(LRO)                     \
    X(R)                       \
    X(AL)                      \
    X(RLE)                     \
    X(RLO)                     \
    X(PDF)                     \
    X(EN)                      \
    X(ES)                      \
    X(ET)                      \
    X(AN)                      \
    X(CS)                      \
    X(NSM)                     \
    X(BN)                      \
    X(B)                       \
    X(S)                       \
    X(WS)                      \
    X(ON)                      \
    /* end of list */

/* Shaping Types */
#define SHAPING_CHAR_TYPE_LIST(X)                                       \
    X(SL) /* Left-Joining, doesn't exist in U+0600 - U+06FF */          \
    X(SR) /* Right-Joining, ie has Isolated, Final */                   \
    X(SD) /* Dual-Joining, ie has Isolated, Final, Initial, Medial */   \
    X(SU) /* Non-Joining */                                             \
    X(SC) /* Join-Causing, like U+0640 (TATWEEL) */                     \
    /* end of list */

#define ENUM_DECL(name) name,
typedef enum { BIDI_CHAR_TYPE_LIST(ENUM_DECL) N_BIDI_TYPES } BidiType;
typedef enum { SHAPING_CHAR_TYPE_LIST(ENUM_DECL) N_SHAPING_TYPES } ShapingType;
#undef ENUM_DECL

#endif /* PUTTY_BIDI_H */
