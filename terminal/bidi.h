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
    X(LRI)                     \
    X(R)                       \
    X(AL)                      \
    X(RLE)                     \
    X(RLO)                     \
    X(RLI)                     \
    X(PDF)                     \
    X(PDI)                     \
    X(FSI)                     \
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

static inline bool typeIsStrong(BidiType t)
{
    return ((1<<L) | (1<<R) | (1<<AL)) & (1 << t);
}
static inline bool typeIsWeak(BidiType t)
{
    return ((1<<EN) | (1<<ES) | (1<<ET) | (1<<AN) |
            (1<<CS) | (1<<NSM) | (1<<BN)) & (1 << t);
}
static inline bool typeIsNeutral(BidiType t)
{
    return ((1<<B) | (1<<S) | (1<<WS) | (1<<ON)) & (1 << t);
}
static inline bool typeIsBidiActive(BidiType t)
{
    return ((1<<R) | (1<<AL) | (1<<AN) | (1<<RLE) | (1<<LRE) | (1<<RLO) |
            (1<<LRO) | (1<<PDF) | (1<<RLI)) & (1 << t);
}
static inline bool typeIsIsolateInitiator(BidiType t)
{
    return ((1<<LRI) | (1<<RLI) | (1<<FSI)) & (1 << t);
}
static inline bool typeIsIsolateInitiatorOrPDI(BidiType t)
{
    return ((1<<LRI) | (1<<RLI) | (1<<FSI) | (1<<PDI)) & (1 << t);
}
static inline bool typeIsEmbeddingInitiator(BidiType t)
{
    return ((1<<LRE) | (1<<RLE) | (1<<LRO) | (1<<RLO)) & (1 << t);
}
static inline bool typeIsEmbeddingInitiatorOrPDF(BidiType t)
{
    return ((1<<LRE) | (1<<RLE) | (1<<LRO) | (1<<RLO) | (1<<PDF)) & (1 << t);
}
static inline bool typeIsWeakSeparatorOrTerminator(BidiType t)
{
    return ((1<<ES) | (1<<ET) | (1<<CS)) & (1 << t);
}
static inline bool typeIsNeutralOrIsolate(BidiType t)
{
    return ((1<<S) | (1<<WS) | (1<<ON) | (1<<FSI) | (1<<LRI) | (1<<RLI) |
            (1<<PDI)) & (1 << t);
}
static inline bool typeIsSegmentOrParaSeparator(BidiType t)
{
    return ((1<<S) | (1<<B)) & (1 << t);
}
static inline bool typeIsWhitespaceOrIsolate(BidiType t)
{
    return ((1<<WS) | (1<<FSI) | (1<<LRI) | (1<<RLI) | (1<<PDI)) & (1 << t);
}
static inline bool typeIsRemovedDuringProcessing(BidiType t)
{
    return ((1<<RLE) | (1<<LRE) | (1<<RLO) | (1<<LRO) | (1<<PDF) |
            (1<<BN)) & (1 << t);
}
static inline bool typeIsStrongOrNumber(BidiType t)
{
    return ((1<<L) | (1<<R) | (1<<AL) | (1<<EN) | (1<<AN)) & (1 << t);
}
static inline bool typeIsETOrBN(BidiType t)
{
    return ((1<<ET) | (1<<BN)) & (1 << t);
}

/*
 * More featureful interface to the bidi code, for use in bidi_test.c.
 * It returns a potentially different value of textlen (in case we're
 * compiling in REMOVE_FORMATTING_CHARACTERS mode), and also permits
 * you to pass in an override to the paragraph direction (because many
 * of the UCD conformance tests use one).
 *
 * 'override' is 0 for no override, +1 for left-to-right, -1 for
 * right-to-left.
 */
size_t do_bidi_test(BidiContext *ctx, bidi_char *text, size_t textlen,
                    int override);

#endif /* PUTTY_BIDI_H */
