/*
 * Implementation of the Unicode bidirectional and Arabic shaping
 * algorithms for PuTTY.
 *
 * Original version written and kindly contributed to this code base
 * by Ahmad Khalifa of Arabeyes. The bidi part was almost completely
 * rewritten in 2021 by Simon Tatham to bring it up to date, but the
 * shaping part is still the one by the original authors.
 *
 * Implementation notes:
 *
 * Algorithm version
 * -----------------
 *
 * This algorithm is up to date with Unicode Standard Annex #9
 * revision 46:
 *
 *   https://www.unicode.org/reports/tr9/tr9-46.html
 *
 * and passes the full conformance test suite in Unicode 15.0.0.
 *
 * Paragraph and line handling
 * ---------------------------
 *
 * The full Unicode bidi algorithm expects to receive text containing
 * multiple paragraphs, together with a decision about how those
 * paragraphs are broken up into lines. It calculates embedding levels
 * a whole paragraph at a time without considering the line breaks,
 * but then the final reordering of the text for display is done to
 * each _line_ independently based on the levels computed for the text
 * in that line.
 *
 * This algorithm omits all of that, because it's intended for use as
 * a display-time transformation of a text terminal, which doesn't
 * preserve enough semantic information to decide what's a paragraph
 * break and what is not. So a piece of input text provided to this
 * algorithm is always expected to consist of exactly one paragraph
 * *and* exactly one line.
 *
 * Embeddings, overrides and isolates
 * ----------------------------------
 *
 * This implementation has full support for all the Unicode special
 * control characters that modify bidi behaviour, such as
 *
 *   U+202A LEFT-TO-RIGHT EMBEDDING
 *   U+202B RIGHT-TO-LEFT EMBEDDING
 *   U+202D LEFT-TO-RIGHT OVERRIDE
 *   U+202E RIGHT-TO-LEFT OVERRIDE
 *   U+202C POP DIRECTIONAL FORMATTING
 *   U+2068 FIRST STRONG ISOLATE
 *   U+2066 LEFT-TO-RIGHT ISOLATE
 *   U+2067 RIGHT-TO-LEFT ISOLATE
 *   U+2069 POP DIRECTIONAL ISOLATE
 *
 * However, at present, the terminal emulator that is a client of this
 * code has no way to pass those in (because they're dropped during
 * escape sequence processing and don't get stored in the terminal
 * state). Nonetheless, the code is all here, so if the terminal
 * emulator becomes able to record those characters at some later
 * point, we'll be all set to take account of them during bidi.
 *
 * But the _main_ purpose of supporting the full bidi algorithm is
 * simply that that's the easiest way to be sure it's correct, because
 * if you support the whole thing, you can run the full conformance
 * test suite. (And I don't 100% believe that restricting to the
 * subset of _tests_ valid with a reduced character set will test the
 * full set of _functionality_ relevant to the reduced set.)
 *
 * Retained formatting characters
 * ------------------------------
 *
 * The standard bidi algorithm, in step X9, deletes assorted
 * formatting characters from the text: all the embedding and override
 * section initiator characters, the Pop Directional Formatting
 * character that closes one of those sections again, and any
 * character labelled as Boundary Neutral. So the characters it
 * returns are not a _full_ reordering of the input; some input
 * characters vanish completely.
 *
 * This would be fine, if it were not for the fact that - as far as I
 * can see - _exactly one_ Unicode code point in the discarded
 * category has a wcwidth() of more than 0, namely U+00AD SOFT HYPHEN
 * which is a printing character for terminal purposes but has a bidi
 * class of BN.
 *
 * Therefore, we must implement a modified version of the algorithm,
 * as described in section 5.2 of TR9, which retains those formatting
 * characters so that a client can find out where they ended up in the
 * reordering.
 *
 * Section 5.2 describes a set of modifications to the algorithm that
 * are _intended_ to achieve this without changing the rest of the
 * behaviour: that is, if you take the output of the modified
 * algorithm and delete all the characters that the standard algorithm
 * would have removed, you should end up with the remaining characters
 * in the same order that the standard algorithm would have delivered.
 * However, section 5.2 admits the possibility of error, and says "in
 * case of any deviation the explicit algorithm is the normative
 * statement for conformance". And indeed, in one or two places I
 * found I had to make my own tweaks to the section 5.2 description in
 * order to get the whole test suite to pass, because I think the 5.2
 * modifications if taken literally don't quite achieve that. My
 * justification is that sentence of 5.2: in case of doubt, the right
 * thing is to make the code behave the same as the official
 * algorithm.
 *
 * It's possible that there might still be some undiscovered
 * discrepancies between the behaviour of the standard and modified
 * algorithms. So, just in case, I've kept in this code the ability to
 * implement the _standard_ algorithm too! If you compile with
 * -DREMOVE_FORMATTING_CHARS, this code should go back to implementing
 * the literal UAX#9 bidi algorithm - so you can run your suspect
 * input through both versions, making it much easier to figure out
 * why they differ, and in which of the many stages of the algorithm
 * the difference was introduced.
 *
 * However, beware that when compiling in this mode, the do_bidi
 * interface to the terminal will stop working, and just abort() when
 * called! The only useful thing you can do with this mode is to run
 * the companion program bidi_test.c.
 */

#include <stdlib.h>     /* definition of wchar_t */

#include "putty.h"
#include "misc.h"
#include "bidi.h"

typedef struct {
    char type;
    wchar_t form_b;
} shape_node;

/* Kept near the actual table, for verification. */
#define SHAPE_FIRST 0x621
#define SHAPE_LAST (SHAPE_FIRST + lenof(shapetypes) - 1)

static const shape_node shapetypes[] = {
    /* index, Typ, Iso, Ligature Index*/
    /* 621 */ {SU, 0xFE80},
    /* 622 */ {SR, 0xFE81},
    /* 623 */ {SR, 0xFE83},
    /* 624 */ {SR, 0xFE85},
    /* 625 */ {SR, 0xFE87},
    /* 626 */ {SD, 0xFE89},
    /* 627 */ {SR, 0xFE8D},
    /* 628 */ {SD, 0xFE8F},
    /* 629 */ {SR, 0xFE93},
    /* 62A */ {SD, 0xFE95},
    /* 62B */ {SD, 0xFE99},
    /* 62C */ {SD, 0xFE9D},
    /* 62D */ {SD, 0xFEA1},
    /* 62E */ {SD, 0xFEA5},
    /* 62F */ {SR, 0xFEA9},
    /* 630 */ {SR, 0xFEAB},
    /* 631 */ {SR, 0xFEAD},
    /* 632 */ {SR, 0xFEAF},
    /* 633 */ {SD, 0xFEB1},
    /* 634 */ {SD, 0xFEB5},
    /* 635 */ {SD, 0xFEB9},
    /* 636 */ {SD, 0xFEBD},
    /* 637 */ {SD, 0xFEC1},
    /* 638 */ {SD, 0xFEC5},
    /* 639 */ {SD, 0xFEC9},
    /* 63A */ {SD, 0xFECD},
    /* 63B */ {SU, 0x0},
    /* 63C */ {SU, 0x0},
    /* 63D */ {SU, 0x0},
    /* 63E */ {SU, 0x0},
    /* 63F */ {SU, 0x0},
    /* 640 */ {SC, 0x0},
    /* 641 */ {SD, 0xFED1},
    /* 642 */ {SD, 0xFED5},
    /* 643 */ {SD, 0xFED9},
    /* 644 */ {SD, 0xFEDD},
    /* 645 */ {SD, 0xFEE1},
    /* 646 */ {SD, 0xFEE5},
    /* 647 */ {SD, 0xFEE9},
    /* 648 */ {SR, 0xFEED},
    /* 649 */ {SR, 0xFEEF}, /* SD */
    /* 64A */ {SD, 0xFEF1},
    /* 64B */ {SU, 0x0},
    /* 64C */ {SU, 0x0},
    /* 64D */ {SU, 0x0},
    /* 64E */ {SU, 0x0},
    /* 64F */ {SU, 0x0},
    /* 650 */ {SU, 0x0},
    /* 651 */ {SU, 0x0},
    /* 652 */ {SU, 0x0},
    /* 653 */ {SU, 0x0},
    /* 654 */ {SU, 0x0},
    /* 655 */ {SU, 0x0},
    /* 656 */ {SU, 0x0},
    /* 657 */ {SU, 0x0},
    /* 658 */ {SU, 0x0},
    /* 659 */ {SU, 0x0},
    /* 65A */ {SU, 0x0},
    /* 65B */ {SU, 0x0},
    /* 65C */ {SU, 0x0},
    /* 65D */ {SU, 0x0},
    /* 65E */ {SU, 0x0},
    /* 65F */ {SU, 0x0},
    /* 660 */ {SU, 0x0},
    /* 661 */ {SU, 0x0},
    /* 662 */ {SU, 0x0},
    /* 663 */ {SU, 0x0},
    /* 664 */ {SU, 0x0},
    /* 665 */ {SU, 0x0},
    /* 666 */ {SU, 0x0},
    /* 667 */ {SU, 0x0},
    /* 668 */ {SU, 0x0},
    /* 669 */ {SU, 0x0},
    /* 66A */ {SU, 0x0},
    /* 66B */ {SU, 0x0},
    /* 66C */ {SU, 0x0},
    /* 66D */ {SU, 0x0},
    /* 66E */ {SU, 0x0},
    /* 66F */ {SU, 0x0},
    /* 670 */ {SU, 0x0},
    /* 671 */ {SR, 0xFB50},
    /* 672 */ {SU, 0x0},
    /* 673 */ {SU, 0x0},
    /* 674 */ {SU, 0x0},
    /* 675 */ {SU, 0x0},
    /* 676 */ {SU, 0x0},
    /* 677 */ {SU, 0x0},
    /* 678 */ {SU, 0x0},
    /* 679 */ {SD, 0xFB66},
    /* 67A */ {SD, 0xFB5E},
    /* 67B */ {SD, 0xFB52},
    /* 67C */ {SU, 0x0},
    /* 67D */ {SU, 0x0},
    /* 67E */ {SD, 0xFB56},
    /* 67F */ {SD, 0xFB62},
    /* 680 */ {SD, 0xFB5A},
    /* 681 */ {SU, 0x0},
    /* 682 */ {SU, 0x0},
    /* 683 */ {SD, 0xFB76},
    /* 684 */ {SD, 0xFB72},
    /* 685 */ {SU, 0x0},
    /* 686 */ {SD, 0xFB7A},
    /* 687 */ {SD, 0xFB7E},
    /* 688 */ {SR, 0xFB88},
    /* 689 */ {SU, 0x0},
    /* 68A */ {SU, 0x0},
    /* 68B */ {SU, 0x0},
    /* 68C */ {SR, 0xFB84},
    /* 68D */ {SR, 0xFB82},
    /* 68E */ {SR, 0xFB86},
    /* 68F */ {SU, 0x0},
    /* 690 */ {SU, 0x0},
    /* 691 */ {SR, 0xFB8C},
    /* 692 */ {SU, 0x0},
    /* 693 */ {SU, 0x0},
    /* 694 */ {SU, 0x0},
    /* 695 */ {SU, 0x0},
    /* 696 */ {SU, 0x0},
    /* 697 */ {SU, 0x0},
    /* 698 */ {SR, 0xFB8A},
    /* 699 */ {SU, 0x0},
    /* 69A */ {SU, 0x0},
    /* 69B */ {SU, 0x0},
    /* 69C */ {SU, 0x0},
    /* 69D */ {SU, 0x0},
    /* 69E */ {SU, 0x0},
    /* 69F */ {SU, 0x0},
    /* 6A0 */ {SU, 0x0},
    /* 6A1 */ {SU, 0x0},
    /* 6A2 */ {SU, 0x0},
    /* 6A3 */ {SU, 0x0},
    /* 6A4 */ {SD, 0xFB6A},
    /* 6A5 */ {SU, 0x0},
    /* 6A6 */ {SD, 0xFB6E},
    /* 6A7 */ {SU, 0x0},
    /* 6A8 */ {SU, 0x0},
    /* 6A9 */ {SD, 0xFB8E},
    /* 6AA */ {SU, 0x0},
    /* 6AB */ {SU, 0x0},
    /* 6AC */ {SU, 0x0},
    /* 6AD */ {SD, 0xFBD3},
    /* 6AE */ {SU, 0x0},
    /* 6AF */ {SD, 0xFB92},
    /* 6B0 */ {SU, 0x0},
    /* 6B1 */ {SD, 0xFB9A},
    /* 6B2 */ {SU, 0x0},
    /* 6B3 */ {SD, 0xFB96},
    /* 6B4 */ {SU, 0x0},
    /* 6B5 */ {SU, 0x0},
    /* 6B6 */ {SU, 0x0},
    /* 6B7 */ {SU, 0x0},
    /* 6B8 */ {SU, 0x0},
    /* 6B9 */ {SU, 0x0},
    /* 6BA */ {SR, 0xFB9E},
    /* 6BB */ {SD, 0xFBA0},
    /* 6BC */ {SU, 0x0},
    /* 6BD */ {SU, 0x0},
    /* 6BE */ {SD, 0xFBAA},
    /* 6BF */ {SU, 0x0},
    /* 6C0 */ {SR, 0xFBA4},
    /* 6C1 */ {SD, 0xFBA6},
    /* 6C2 */ {SU, 0x0},
    /* 6C3 */ {SU, 0x0},
    /* 6C4 */ {SU, 0x0},
    /* 6C5 */ {SR, 0xFBE0},
    /* 6C6 */ {SR, 0xFBD9},
    /* 6C7 */ {SR, 0xFBD7},
    /* 6C8 */ {SR, 0xFBDB},
    /* 6C9 */ {SR, 0xFBE2},
    /* 6CA */ {SU, 0x0},
    /* 6CB */ {SR, 0xFBDE},
    /* 6CC */ {SD, 0xFBFC},
    /* 6CD */ {SU, 0x0},
    /* 6CE */ {SU, 0x0},
    /* 6CF */ {SU, 0x0},
    /* 6D0 */ {SU, 0x0},
    /* 6D1 */ {SU, 0x0},
    /* 6D2 */ {SR, 0xFBAE},
};

/*
 * Returns the bidi character type of ch.
 */
unsigned char bidi_getType(int ch)
{
    static const struct {
        int first, last, type;
    } lookup[] = {
        #include "unicode/bidi_type.h"
    };

    int i, j, k;

    i = -1;
    j = lenof(lookup);

    while (j - i > 1) {
        k = (i + j) / 2;
        if (ch < lookup[k].first)
            j = k;
        else if (ch > lookup[k].last)
            i = k;
        else
            return lookup[k].type;
    }

    /*
     * If we reach here, the character was not in any of the
     * intervals listed in the lookup table. This means we return
     * ON (`Other Neutrals'). This is the appropriate code for any
     * character genuinely not listed in the Unicode table, and
     * also the table above has deliberately left out any
     * characters _explicitly_ listed as ON (to save space!).
     */
    return ON;
}

/*
 * Return the mirrored version of a glyph.
 *
 * FIXME: there are also glyphs which the text rendering engine is
 * supposed to display left-right reflected, since no mirrored glyph
 * exists in Unicode itself to indicate the reflected form. Those are
 * listed in comments in BidiMirroring.txt. Many of them are
 * mathematical, e.g. the square root sign, or set difference
 * operator, or integral sign. No API currently exists here to
 * communicate the need for that reflected display back to the client.
 */
static unsigned mirror_glyph(unsigned int ch)
{
    static const struct {
        unsigned src, dst;
    } mirror_pairs[] = {
        #include "unicode/bidi_mirror.h"
    };

    int i, j, k;

    i = -1;
    j = lenof(mirror_pairs);

    while (j - i > 1) {
        k = (i + j) / 2;
        if (ch < mirror_pairs[k].src)
            j = k;
        else if (ch > mirror_pairs[k].src)
            i = k;
        else
            return mirror_pairs[k].dst;
    }

    return ch;
}

/*
 * Identify the bracket characters treated specially by bidi rule
 * BD19, and return their paired character(s).
 */
typedef enum { BT_NONE, BT_OPEN, BT_CLOSE } BracketType;
typedef struct BracketTypeData {
    unsigned partner, equiv_partner;
    BracketType type;
} BracketTypeData;
static BracketTypeData bracket_type(unsigned int ch)
{
    static const struct {
        unsigned src;
        BracketTypeData payload;
    } bracket_pairs[] = {
        #include "unicode/bidi_brackets.h"
    };

    int i, j, k;

    i = -1;
    j = lenof(bracket_pairs);

    while (j - i > 1) {
        k = (i + j) / 2;
        if (ch < bracket_pairs[k].src) {
            j = k;
        } else if (ch > bracket_pairs[k].src) {
            i = k;
        } else {
            return bracket_pairs[k].payload;
        }
    }

    static const BracketTypeData null = { 0, 0, BT_NONE };
    return null;
}

/*
 * Function exported to front ends to allow them to identify
 * bidi-active characters (in case, for example, the platform's
 * text display function can't conveniently be prevented from doing
 * its own bidi and so special treatment is required for characters
 * that would cause the bidi algorithm to activate).
 *
 * This function is passed a single Unicode code point, and returns
 * nonzero if the presence of this code point can possibly cause
 * the bidi algorithm to do any reordering. Thus, any string
 * composed entirely of characters for which is_rtl() returns zero
 * should be safe to pass to a bidi-active platform display
 * function without fear.
 *
 * (is_rtl() must therefore also return true for any character
 * which would be affected by Arabic shaping, but this isn't
 * important because all such characters are right-to-left so it
 * would have flagged them anyway.)
 */
bool is_rtl(int c)
{
    return typeIsBidiActive(bidi_getType(c));
}

/* The Main shaping function, and the only one to be used
 * by the outside world.
 *
 * line: buffer to apply shaping to. this must be passed by doBidi() first
 * to: output buffer for the shaped data
 * count: number of characters in line
 */
int do_shape(bidi_char *line, bidi_char *to, int count)
{
    int i, tempShape;
    bool ligFlag = false;

    for (i=0; i<count; i++) {
        to[i] = line[i];
        tempShape = STYPE(line[i].wc);
        switch (tempShape) {
          case SC:
            break;

          case SU:
            break;

          case SR:
            tempShape = (i+1 < count ? STYPE(line[i+1].wc) : SU);
            if ((tempShape == SL) || (tempShape == SD) || (tempShape == SC))
                to[i].wc = SFINAL((SISOLATED(line[i].wc)));
            else
                to[i].wc = SISOLATED(line[i].wc);
            break;


          case SD:
            /* Make Ligatures */
            tempShape = (i+1 < count ? STYPE(line[i+1].wc) : SU);
            if (line[i].wc == 0x644) {
                if (i > 0) switch (line[i-1].wc) {
                  case 0x622:
                    ligFlag = true;
                    if ((tempShape == SL) || (tempShape == SD) || (tempShape == SC))
                        to[i].wc = 0xFEF6;
                    else
                        to[i].wc = 0xFEF5;
                    break;
                  case 0x623:
                    ligFlag = true;
                    if ((tempShape == SL) || (tempShape == SD) || (tempShape == SC))
                        to[i].wc = 0xFEF8;
                    else
                        to[i].wc = 0xFEF7;
                    break;
                  case 0x625:
                    ligFlag = true;
                    if ((tempShape == SL) || (tempShape == SD) || (tempShape == SC))
                        to[i].wc = 0xFEFA;
                    else
                        to[i].wc = 0xFEF9;
                    break;
                  case 0x627:
                    ligFlag = true;
                    if ((tempShape == SL) || (tempShape == SD) || (tempShape == SC))
                        to[i].wc = 0xFEFC;
                    else
                        to[i].wc = 0xFEFB;
                    break;
                }
                if (ligFlag) {
                    to[i-1].wc = 0x20;
                    ligFlag = false;
                    break;
                }
            }

            if ((tempShape == SL) || (tempShape == SD) || (tempShape == SC)) {
                tempShape = (i > 0 ? STYPE(line[i-1].wc) : SU);
                if ((tempShape == SR) || (tempShape == SD) || (tempShape == SC))
                    to[i].wc = SMEDIAL((SISOLATED(line[i].wc)));
                else
                    to[i].wc = SFINAL((SISOLATED(line[i].wc)));
                break;
            }

            tempShape = (i > 0 ? STYPE(line[i-1].wc) : SU);
            if ((tempShape == SR) || (tempShape == SD) || (tempShape == SC))
                to[i].wc = SINITIAL((SISOLATED(line[i].wc)));
            else
                to[i].wc = SISOLATED(line[i].wc);
            break;


        }
    }
    return 1;
}

typedef enum { DO_NEUTRAL, DO_LTR, DO_RTL } DirectionalOverride;

typedef struct DSStackEntry {
    /*
     * An entry in the directional status stack (rule section X).
     */
    unsigned char level;
    bool isolate;
    DirectionalOverride override;
} DSStackEntry;

typedef struct BracketStackEntry {
    /*
     * An entry in the bracket-pair-tracking stack (rule BD16).
     */
    unsigned ch;
    size_t c;
} BracketStackEntry;

typedef struct IsolatingRunSequence {
    size_t start, end;
    BidiType sos, eos, embeddingDirection;
} IsolatingRunSequence;

#define MAX_DEPTH 125                  /* specified in the standard */

struct BidiContext {
    /*
     * Storage space preserved between runs, all allocated to the same
     * length (internal_array_sizes).
     */
    size_t internal_array_sizes;
    BidiType *types, *origTypes;
    unsigned char *levels;
    size_t *irsindices, *bracketpos;
    bool *irsdone;

    /*
     * Separately allocated with its own size field
     */
    IsolatingRunSequence *irslist;
    size_t irslistsize;

    /*
     * Rewritten to point to the input to the currently active run of
     * the bidi algorithm
     */
    bidi_char *text;
    size_t textlen;

    /*
     * State within a run of the algorithm
     */
    BidiType paragraphOverride;
    DSStackEntry dsstack[MAX_DEPTH + 2];
    size_t ds_sp;
    size_t overflowIsolateCount, overflowEmbeddingCount, validIsolateCount;
    unsigned char paragraphLevel;
    size_t *irs;
    size_t irslen;
    BidiType sos, eos, embeddingDirection;
    BracketStackEntry bstack[63]; /* constant size specified in rule BD16 */
};

BidiContext *bidi_new_context(void)
{
    BidiContext *ctx = snew(BidiContext);
    memset(ctx, 0, sizeof(BidiContext));
    return ctx;
}

void bidi_free_context(BidiContext *ctx)
{
    sfree(ctx->types);
    sfree(ctx->origTypes);
    sfree(ctx->levels);
    sfree(ctx->irsindices);
    sfree(ctx->irsdone);
    sfree(ctx->bracketpos);
    sfree(ctx->irslist);
    sfree(ctx);
}

static void ensure_arrays(BidiContext *ctx, size_t textlen)
{
    if (textlen <= ctx->internal_array_sizes)
        return;
    ctx->internal_array_sizes = textlen;
    ctx->types = sresize(ctx->types, ctx->internal_array_sizes, BidiType);
    ctx->origTypes = sresize(ctx->origTypes, ctx->internal_array_sizes,
                             BidiType);
    ctx->levels = sresize(ctx->levels, ctx->internal_array_sizes,
                          unsigned char);
    ctx->irsindices = sresize(ctx->irsindices, ctx->internal_array_sizes,
                              size_t);
    ctx->irsdone = sresize(ctx->irsdone, ctx->internal_array_sizes, bool);
    ctx->bracketpos = sresize(ctx->bracketpos, ctx->internal_array_sizes,
                              size_t);
}

static void setup_types(BidiContext *ctx)
{
    for (size_t i = 0; i < ctx->textlen; i++)
        ctx->types[i] = ctx->origTypes[i] = bidi_getType(ctx->text[i].wc);
}

static bool text_needs_bidi(BidiContext *ctx)
{
    /*
     * Initial optimisation: check for any bidi-active character at
     * all in an input line. If there aren't any, we can skip the
     * whole algorithm.
     *
     * Also include the paragraph override in this check!
     */
    for (size_t i = 0; i < ctx->textlen; i++)
        if (typeIsBidiActive(ctx->types[i]))
            return true;
    return typeIsBidiActive(ctx->paragraphOverride);
}

static size_t find_matching_pdi(const BidiType *types, size_t i, size_t size)
{
    /* Assuming that types[i] is an isolate initiator, find its
     * matching PDI by rule BD9. */
    unsigned counter = 1;
    i++;
    for (; i < size; i++) {
        BidiType t = types[i];
        if (typeIsIsolateInitiator(t)) {
            counter++;
        } else if (t == PDI) {
            counter--;
            if (counter == 0)
                return i;
        }
    }

    /* If no PDI was found, return the length of the array. */
    return size;
}

static unsigned char rule_p2_p3(const BidiType *types, size_t size)
{
    /*
     * Rule P2. Find the first strong type (L, R or AL), ignoring
     * anything inside an isolated segment.
     *
     * Rule P3. If that type is R or AL, choose a paragraph embeddding
     * level of 1, otherwise 0.
     */
    for (size_t i = 0; i < size; i++) {
        BidiType t = types[i];
        if (typeIsIsolateInitiator(t))
            i = find_matching_pdi(types, i, size);
        else if (typeIsStrong(t))
            return (t == L ? 0 : 1);
    }

    return 0; /* default if no strong type found */
}

static void set_paragraph_level(BidiContext *ctx)
{
    if (ctx->paragraphOverride == L)
        ctx->paragraphLevel = 0;
    else if (ctx->paragraphOverride == R)
        ctx->paragraphLevel = 1;
    else
        ctx->paragraphLevel = rule_p2_p3(ctx->types, ctx->textlen);
}

static inline unsigned char nextOddLevel(unsigned char x)  { return (x+1)|1; }
static inline unsigned char nextEvenLevel(unsigned char x) { return (x|1)+1; }

static inline void push(BidiContext *ctx, unsigned char level,
                        DirectionalOverride override, bool isolate)
{
    ctx->ds_sp++;
    assert(ctx->ds_sp < lenof(ctx->dsstack));
    ctx->dsstack[ctx->ds_sp].level = level;
    ctx->dsstack[ctx->ds_sp].override = override;
    ctx->dsstack[ctx->ds_sp].isolate = isolate;
}

static inline void pop(BidiContext *ctx)
{
    assert(ctx->ds_sp > 0);
    ctx->ds_sp--;
}

static void process_explicit_embeddings(BidiContext *ctx)
{
    /*
     * Rule X1 initialisation.
     */
    ctx->ds_sp = (size_t)-1;
    push(ctx, ctx->paragraphLevel, DO_NEUTRAL, false);
    ctx->overflowIsolateCount = 0;
    ctx->overflowEmbeddingCount = 0;
    ctx->validIsolateCount = 0;

    #define stk (&ctx->dsstack[ctx->ds_sp])

    for (size_t i = 0; i < ctx->textlen; i++) {
        BidiType t = ctx->types[i];
        switch (t) {
          case RLE: case LRE: case RLO: case LRO: {
            /* Rules X2-X5 */
            unsigned char newLevel;
            DirectionalOverride override;

#ifndef REMOVE_FORMATTING_CHARS
            ctx->levels[i] = stk->level;
#endif

            switch (t) {
              case RLE: /* rule X2 */
                newLevel = nextOddLevel(stk->level);
                override = DO_NEUTRAL;
                break;
              case LRE: /* rule X3 */
                newLevel = nextEvenLevel(stk->level);
                override = DO_NEUTRAL;
                break;
              case RLO: /* rule X4 */
                newLevel = nextOddLevel(stk->level);
                override = DO_RTL;
                break;
              case LRO: /* rule X5 */
                newLevel = nextEvenLevel(stk->level);
                override = DO_LTR;
                break;
              default:
                unreachable("how did this get past the outer switch?");
            }

            if (newLevel <= MAX_DEPTH &&
                ctx->overflowIsolateCount == 0 &&
                ctx->overflowEmbeddingCount == 0) {
                /* Embedding code is valid. Push a stack entry. */
                push(ctx, newLevel, override, false);
            } else {
                /* Embedding code is an overflow one. */
                if (ctx->overflowIsolateCount == 0)
                    ctx->overflowEmbeddingCount++;
            }
            break;
          }

          case RLI: case LRI: case FSI: {
            /* Rules X5a, X5b, X5c */

            if (t == FSI) {
                /* Rule X5c: decide whether this should be treated
                 * like RLI or LRI */
                size_t pdi = find_matching_pdi(ctx->types, i, ctx->textlen);
                unsigned char level = rule_p2_p3(ctx->types + (i + 1),
                                                 pdi - (i + 1));
                t = (level == 1 ? RLI : LRI);
            }

            ctx->levels[i] = stk->level;
            if (stk->override != DO_NEUTRAL)
                ctx->types[i] = (stk->override == DO_LTR ? L :
                                 stk->override == DO_RTL ? R : t);

            unsigned char newLevel = (t == RLI ? nextOddLevel(stk->level) :
                                      nextEvenLevel(stk->level));

            if (newLevel <= MAX_DEPTH &&
                ctx->overflowIsolateCount == 0 &&
                ctx->overflowEmbeddingCount == 0) {
                /* Isolate code is valid. Push a stack entry. */
                push(ctx, newLevel, DO_NEUTRAL, true);
                ctx->validIsolateCount++;
            } else {
                /* Isolate code is an overflow one. */
                ctx->overflowIsolateCount++;
            }
            break;
          }

          case PDI: {
            /* Rule X6a */
            if (ctx->overflowIsolateCount > 0) {
                ctx->overflowIsolateCount--;
            } else if (ctx->validIsolateCount == 0) {
                /* Do nothing: spurious isolate-pop */
            } else {
                /* Valid isolate-pop. We expect that the stack must
                 * therefore contain at least one isolate==true entry,
                 * so pop everything up to and including it. */
                ctx->overflowEmbeddingCount = 0;
                while (!stk->isolate)
                    pop(ctx);
                pop(ctx);
                ctx->validIsolateCount--;
            }
            ctx->levels[i] = stk->level;
            if (stk->override != DO_NEUTRAL)
                ctx->types[i] = (stk->override == DO_LTR ? L : R);
            break;
          }

          case PDF: {
            /* Rule X7 */
            if (ctx->overflowIsolateCount > 0) {
                /* Do nothing if we've overflowed on isolates */
            } else if (ctx->overflowEmbeddingCount > 0) {
                ctx->overflowEmbeddingCount--;
            } else if (ctx->ds_sp > 0 && !stk->isolate) {
                pop(ctx);
            } else {
                /* Do nothing: spurious embedding-pop */
            }

#ifndef REMOVE_FORMATTING_CHARS
            ctx->levels[i] = stk->level;
#endif
            break;
          }

          case B: {
            /* Rule X8: if an explicit paragraph separator appears in
             * this text at all then it does not participate in any of
             * the above, and just gets assigned the paragraph level.
             *
             * PS, it had better be right at the end of the text,
             * because we have not implemented rule P1 in this code. */
            assert(i == ctx->textlen - 1);
            ctx->levels[i] = ctx->paragraphLevel;
            break;
          }

          case BN: {
            /*
             * The section 5.2 adjustment to rule X6 says that we
             * apply it to BN just like any other class. But I think
             * this can't possibly give the same results as the
             * unmodified algorithm.
             *
             * Proof: adding RLO BN or LRO BN at the end of a
             * paragraph should not change the output of the standard
             * algorithm, because the override doesn't affect the BN
             * in rule X6, and then rule X9 removes both. But with the
             * modified rule X6, the BN is changed into R or L, and
             * then rule X9 doesn't remove it, and then you've added a
             * strong type that will set eos for the level run just
             * before the override. And whatever the standard
             * algorithm set eos to, _one_ of these override sequences
             * will disagree with it.
             *
             * So I think we just set the BN's level, and don't change
             * its type.
             */
            ctx->levels[i] = stk->level;
            break;
          }

          default: {
            /* Rule X6. */
            ctx->levels[i] = stk->level;
            if (stk->override != DO_NEUTRAL)
                ctx->types[i] = (stk->override == DO_LTR ? L : R);
            break;
          }
        }
    }

    #undef stk
}

static void remove_embedding_characters(BidiContext *ctx)
{
#ifndef REMOVE_FORMATTING_CHARS
    /*
     * Rule X9, as modified by section 5.2: turn embedding (but not
     * isolate) characters into BN.
     */
    for (size_t i = 0; i < ctx->textlen; i++) {
        BidiType t = ctx->types[i];
        if (typeIsRemovedDuringProcessing(t)) {
            ctx->types[i] = BN;

            /*
             * My own adjustment to the section 5.2 mods: a sequence
             * of contiguous BN generated by this setup should never
             * be at different levels from each other.
             *
             * An example where this goes wrong is if you open two
             * LREs in sequence, then close them again:
             *
             *   ... LRE LRE PDF PDF ...
             *
             * The initial level assignment gives level 0 to the outer
             * LRE/PDF pair, and level 2 to the inner one. The
             * standard algorithm would remove all four, so this
             * doesn't matter, and you end up with no break in the
             * surrounding level run. But if you just rewrite the
             * types of all those characters to BN and leave the
             * levels in that state, then the modified algorithm will
             * leave the middle two BN at level 2, dividing what
             * should have been a long level run at level 0 into two
             * separate ones.
             */
            if (i > 0 && ctx->types[i-1] == BN)
                ctx->levels[i] = ctx->levels[i-1];
        }
    }
#else
    /*
     * Rule X9, original version: completely remove embedding
     * start/end characters and also boundary neutrals.
     */
    size_t outpos = 0;
    for (size_t i = 0; i < ctx->textlen; i++) {
        BidiType t = ctx->types[i];
        if (!typeIsRemovedDuringProcessing(t)) {
            ctx->text[outpos] = ctx->text[i];
            ctx->levels[outpos] = ctx->levels[i];
            ctx->types[outpos] = ctx->types[i];
            ctx->origTypes[outpos] = ctx->origTypes[i];
            outpos++;
        }
    }
    ctx->textlen = outpos;
#endif
}

typedef void (*irs_fn_t)(BidiContext *ctx);

static void find_isolating_run_sequences(BidiContext *ctx, irs_fn_t process)
{
    /*
     * Rule X10 / BD13. Now that we've assigned an embedding level to
     * each character in the text, we have to divide the text into
     * subsequences on which to do the next stage of processing.
     *
     * In earlier issues of the bidi algorithm, these subsequences
     * were contiguous in the original text, and each one was a 'level
     * run': a maximal contiguous subsequence of characters all at the
     * same embedding level.
     *
     * But now we have isolates, and the point of an (isolate
     * initiator ... PDI) sequence is that the whole sequence should
     * be treated like a single BN for the purposes of formatting
     * everything outside it. As a result, we now have to recombine
     * our level runs into longer sequences, on the principle that if
     * a level run ends with an isolate initiator, then we bring it
     * together with whatever later level run starts with the matching
     * PDI.
     *
     * These subsequences are no longer contiguous (the whole point is
     * that between the isolate initiator and the PDI is some other
     * text that we've skipped over). They're called 'isolating run
     * sequences'.
     */

    memset(ctx->irsdone, 0, ctx->textlen);
    size_t i = 0;
    size_t n_irs = 0;
    size_t indexpos = 0;
    while (i < ctx->textlen) {
        if (ctx->irsdone[i]) {
            i++;
            continue;
        }

        /*
         * Found a character not already processed. Start a new
         * sequence here.
         */
        sgrowarray(ctx->irslist, ctx->irslistsize, n_irs);
        IsolatingRunSequence *irs = &ctx->irslist[n_irs++];
        irs->start = indexpos;
        size_t j = i;
        size_t irslevel = ctx->levels[i];
        while (j < ctx->textlen) {
            /*
             * We expect that all level runs in this sequence will be
             * at the same level as each other, by construction of how
             * we set up the levels from the isolates in the first
             * place.
             */
            assert(ctx->levels[j] == irslevel);

            do {
                ctx->irsdone[j] = true;
                ctx->irsindices[indexpos++] = j++;
            } while (j < ctx->textlen && ctx->levels[j] == irslevel);
            if (!typeIsIsolateInitiator(ctx->types[j-1]))
                break;                 /* this IRS is ended */
            j = find_matching_pdi(ctx->types, j-1, ctx->textlen);
        }
        irs->end = indexpos;

        /*
         * Determine the start-of-sequence and end-of-sequence types
         * for this sequence.
         *
         * These depend on the embedding levels of surrounding text.
         * But processing each run can change those levels. That's why
         * we have to use a two-pass strategy here, first identifying
         * all the isolating run sequences using the input level data,
         * and not processing any of them until we know where they all
         * are.
         */
        size_t p;
        unsigned char level_inside, level_outside, level_max;

        p = i;
        level_inside = ctx->levels[p];
        level_outside = ctx->paragraphLevel;
        while (p > 0) {
            p--;
            if (ctx->types[p] != BN) {
                level_outside = ctx->levels[p];
                break;
            }
        }
        level_max = max(level_inside, level_outside);
        irs->sos = (level_max % 2 ? R : L);

        p = ctx->irsindices[irs->end - 1];
        level_inside = ctx->levels[p];
        level_outside = ctx->paragraphLevel;
        if (typeIsIsolateInitiator(ctx->types[p])) {
            /* Special case: if an isolating run sequence ends in an
             * unmatched isolate initiator, then level_outside is
             * taken to be the paragraph embedding level and the
             * loop below is skipped. */
        } else {
            while (p+1 < ctx->textlen) {
                p++;
                if (ctx->types[p] != BN) {
                    level_outside = ctx->levels[p];
                    break;
                }
            }
        }
        level_max = max(level_inside, level_outside);
        irs->eos = (level_max % 2 ? R : L);

        irs->embeddingDirection = (irslevel % 2 ? R : L);

        /*
         * Now we've listed in ctx->irsindices[] the index of every
         * character that's part of this isolating run sequence, and
         * recorded an entry in irslist containing the interval of
         * indices relevant to this IRS, plus its assorted metadata.
         * We've also marked those locations in the input text as done
         * in ctx->irsdone, so that we'll skip over them when the
         * outer iteration reaches them later.
         */
    }

    for (size_t k = 0; k < n_irs; k++) {
        IsolatingRunSequence *irs = &ctx->irslist[k];
        ctx->irs = ctx->irsindices + irs->start;
        ctx->irslen = irs->end - irs->start;
        ctx->sos = irs->sos;
        ctx->eos = irs->eos;
        ctx->embeddingDirection = irs->embeddingDirection;
        process(ctx);
    }

    /* Reset irslen to 0 when we've finished. This means any other
     * functions that absentmindedly try to use irslen at all will end
     * up doing nothing at all, which should be easier to detect and
     * debug than if they run on subtly the wrong subset of the
     * text. */
    ctx->irslen = 0;
}

static void remove_nsm(BidiContext *ctx)
{
    /* Rule W1: NSM gains the type of the previous character, or sos
     * at the start of the run, with the exception that isolation
     * boundaries turn into ON. */
    BidiType prevType = ctx->sos;
    for (size_t c = 0; c < ctx->irslen; c++) {
        size_t i = ctx->irs[c];
        BidiType t = ctx->types[i];
        if (t == NSM) {
            ctx->types[i] = prevType;
        } else if (typeIsIsolateInitiatorOrPDI(t)) {
            prevType = ON;
#ifndef REMOVE_FORMATTING_CHARS
        } else if (t == BN) {
            /* section 5.2 adjustment: these don't affect prevType */
#endif
        } else {
            prevType = t;
        }
    }
}

static void change_en_to_an(BidiContext *ctx)
{
    /* Rule W2: EN becomes AN if the previous strong type is AL. (The
     * spec says that the 'previous strong type' is counted as sos at
     * the start of the run, although it hardly matters, since sos
     * can't be AL.) */
    BidiType prevStrongType = ctx->sos;
    for (size_t c = 0; c < ctx->irslen; c++) {
        size_t i = ctx->irs[c];
        BidiType t = ctx->types[i];
        if (t == EN && prevStrongType == AL) {
            ctx->types[i] = AN;
        } else if (typeIsStrong(t)) {
            prevStrongType = t;
        }
    }
}

static void change_al_to_r(BidiContext *ctx)
{
    /* Rule W3: AL becomes R unconditionally. (The only difference
     * between the two types was their effect on nearby numbers, which
     * was dealt with in rule W2, so now we're done with the
     * distinction.) */
    for (size_t c = 0; c < ctx->irslen; c++) {
        size_t i = ctx->irs[c];
        if (ctx->types[i] == AL)
            ctx->types[i] = R;
    }
}

static void eliminate_separators_between_numbers(BidiContext *ctx)
{
    /* Rule W4: a single numeric separator between two numbers of the
     * same type compatible with that separator takes the type of the
     * number. ES is a separator type compatible only with EN; CS is a
     * separator type compatible with either EN or AN.
     *
     * Section 5.2 adjustment: intervening BNs do not break this, so
     * instead of simply looking at types[irs[c-1]] and types[irs[c+1]],
     * we must track the last three indices we saw that were not BN. */
    size_t i1 = 0, i2 = 0;
    BidiType t0 = ON, t1 = ON, t2 = ON;
    for (size_t c = 0; c < ctx->irslen; c++) {
        size_t i = ctx->irs[c];
        BidiType t = ctx->types[i];

#ifndef REMOVE_FORMATTING_CHARS
        if (t == BN)
            continue;
#endif

        i1 = i2; i2 = i;
        t0 = t1; t1 = t2; t2 = t;
        if (t0 == t2 && ((t1 == ES && t0 == EN) ||
                         (t1 == CS && (t0 == EN || t0 == AN)))) {
            ctx->types[i1] = t0;
        }
    }
}

static void eliminate_et_next_to_en(BidiContext *ctx)
{
    /* Rule W5: a sequence of ET adjacent to an EN take the type EN.
     * This is easiest to implement with one loop in each direction.
     *
     * Section 5.2 adjustment: include BN with ET. (We don't need to
     * #ifdef that out, because in the standard algorithm, we won't
     * have any BN left in any case.) */

    bool modifying = false;

    for (size_t c = 0; c < ctx->irslen; c++) {
        size_t i = ctx->irs[c];
        BidiType t = ctx->types[i];
        if (t == EN) {
            modifying = true;
        } else if (modifying && typeIsETOrBN(t)) {
            ctx->types[i] = EN;
        } else {
            modifying = false;
        }
    }

    for (size_t c = ctx->irslen; c-- > 0 ;) {
        size_t i = ctx->irs[c];
        BidiType t = ctx->types[i];
        if (t == EN) {
            modifying = true;
        } else if (modifying && typeIsETOrBN(t)) {
            ctx->types[i] = EN;
        } else {
            modifying = false;
        }
    }
}

static void eliminate_separators_and_terminators(BidiContext *ctx)
{
    /* Rule W6: all separators and terminators change to ON.
     *
     * (The spec is not quite clear on which bidi types are included
     * in this; one assumes ES, ET and CS, but what about S? I _think_
     * the answer is that this is a rule in the W section, so it's
     * implicitly supposed to only apply to types designated as weakly
     * directional, so not S.) */

#ifndef REMOVE_FORMATTING_CHARS
    /*
     * Section 5.2 adjustment: this also applies to any BN adjacent on
     * either side to one of these types, which is easiest to
     * implement with a separate double-loop converting those to an
     * arbitrary one of the affected types, say CS.
     *
     * This double loop can be completely skipped in the standard
     * algorithm.
     */
    bool modifying = false;

    for (size_t c = 0; c < ctx->irslen; c++) {
        size_t i = ctx->irs[c];
        BidiType t = ctx->types[i];
        if (typeIsWeakSeparatorOrTerminator(t)) {
            modifying = true;
        } else if (modifying && t == BN) {
            ctx->types[i] = CS;
        } else {
            modifying = false;
        }
    }

    for (size_t c = ctx->irslen; c-- > 0 ;) {
        size_t i = ctx->irs[c];
        BidiType t = ctx->types[i];
        if (typeIsWeakSeparatorOrTerminator(t)) {
            modifying = true;
        } else if (modifying && t == BN) {
            ctx->types[i] = CS;
        } else {
            modifying = false;
        }
    }
#endif

    /* Now the main part of rule W6 */
    for (size_t c = 0; c < ctx->irslen; c++) {
        size_t i = ctx->irs[c];
        BidiType t = ctx->types[i];
        if (typeIsWeakSeparatorOrTerminator(t))
            ctx->types[i] = ON;
    }
}

static void change_en_to_l(BidiContext *ctx)
{
    /* Rule W7: EN becomes L if the previous strong type (or sos) is L. */
    BidiType prevStrongType = ctx->sos;
    for (size_t c = 0; c < ctx->irslen; c++) {
        size_t i = ctx->irs[c];
        BidiType t = ctx->types[i];
        if (t == EN && prevStrongType == L) {
            ctx->types[i] = L;
        } else if (typeIsStrong(t)) {
            prevStrongType = t;
        }
    }
}

typedef void (*bracket_pair_fn)(BidiContext *ctx, size_t copen, size_t cclose);

static void find_bracket_pairs(BidiContext *ctx, bracket_pair_fn process)
{
    const size_t NO_BRACKET = ~(size_t)0;

    /*
     * Rule BD16.
     */
    size_t sp = 0;
    for (size_t c = 0; c < ctx->irslen; c++)
        ctx->bracketpos[c] = NO_BRACKET;

    for (size_t c = 0; c < ctx->irslen; c++) {
        size_t i = ctx->irs[c];
        unsigned wc = ctx->text[i].wc;
        BracketTypeData bt = bracket_type(wc);
        if (bt.type == BT_OPEN) {
            if (sp >= lenof(ctx->bstack)) {
                /*
                 * Stack overflow. The spec says we simply give up at
                 * this point.
                 */
                goto found_all_pairs;
            }

            ctx->bstack[sp].ch = wc;
            ctx->bstack[sp].c = c;
            sp++;
        } else if (bt.type == BT_CLOSE) {
            size_t new_sp = sp;

            /*
             * Search up the stack for an entry containing a matching
             * open bracket. If we find it, pop that entry and
             * everything deeper, and record a matching pair. If we
             * reach the bottom of the stack without finding anything,
             * leave sp where it started.
             */
            while (new_sp-- > 0) {
                if (ctx->bstack[new_sp].ch == bt.partner ||
                    ctx->bstack[new_sp].ch == bt.equiv_partner) {
                    /* Found a stack element matching this one */
                    size_t cstart = ctx->bstack[new_sp].c;
                    ctx->bracketpos[cstart] = c;
                    sp = new_sp;
                    break;
                }
            }
        }
    }

  found_all_pairs:
    for (size_t c = 0; c < ctx->irslen; c++) {
        if (ctx->bracketpos[c] != NO_BRACKET) {
            process(ctx, c, ctx->bracketpos[c]);
        }
    }
}

static BidiType get_bracket_type(BidiContext *ctx, size_t copen, size_t cclose)
{
    /*
     * Rule N0: a pair of matched brackets containing at least one
     * strong type takes on the current embedding direction, unless
     * all of these are true at once:
     *
     *  (a) there are no strong types inside the brackets matching the
     *      current embedding direction
     *  (b) there _is_ at least one strong type inside the brackets
     *      that is _opposite_ to the current embedding direction
     *  (c) the strong type preceding the open bracket is also
     *      opposite to the current embedding direction
     *
     * in which case they take on the opposite direction.
     *
     * For these purposes, number types (EN and AN) count as R.
     */

    bool foundOppositeTypeInside = false;
    for (size_t c = copen + 1; c < cclose; c++) {
        size_t i = ctx->irs[c];
        BidiType t = ctx->types[i];
        if (typeIsStrongOrNumber(t)) {
            t = t == L ? L : R;        /* numbers count as R */
            if (t == ctx->embeddingDirection) {
                /* Found something inside the brackets matching the
                 * current level, so (a) is violated. */
                return ctx->embeddingDirection;
            } else {
                foundOppositeTypeInside = true;
            }
        }
    }

    if (!foundOppositeTypeInside) {
        /* No strong types at all inside the brackets, so return ON to
         * indicate that we're not messing with their type at all. */
        return ON;
    }

    /* There was an opposite strong type in the brackets. Look
     * backwards to the preceding strong type, and go with that,
     * whichever it is. */
    for (size_t c = copen; c-- > 0 ;) {
        size_t i = ctx->irs[c];
        BidiType t = ctx->types[i];
        if (typeIsStrongOrNumber(t)) {
            t = t == L ? L : R;        /* numbers count as R */
            return t;
        }
    }

    /* Fallback: if the preceding strong type was not found, go with
     * sos. */
    return ctx->sos;
}

static void reset_bracket_type(BidiContext *ctx, size_t c, BidiType t)
{
    /* Final bullet point of rule N0: when we change the type of a
     * bracket, the same change applies to any contiguous sequence of
     * characters after it whose _original_ bidi type was NSM. */
    do {
        ctx->types[ctx->irs[c++]] = t;

#ifndef REMOVE_FORMATTING_CHARS
        while (c < ctx->irslen && ctx->origTypes[ctx->irs[c]] == BN) {
            /* Section 5.2 adjustment: skip past BN in the process. */
            c++;
        }
#endif
    } while (c < ctx->irslen && ctx->origTypes[ctx->irs[c]] == NSM);
}

static void resolve_brackets(BidiContext *ctx, size_t copen, size_t cclose)
{
    if (typeIsNeutral(ctx->types[ctx->irs[copen]]) &&
        typeIsNeutral(ctx->types[ctx->irs[cclose]])) {
        BidiType t = get_bracket_type(ctx, copen, cclose);
        if (t != ON) {
            reset_bracket_type(ctx, copen, t);
            reset_bracket_type(ctx, cclose, t);
        }
    }
}

static void remove_ni(BidiContext *ctx)
{
    /*
     * Rules N1 and N2 together: neutral or isolate characters take
     * the direction of the surrounding strong text if the nearest
     * strong characters on each side match, and otherwise, they take
     * the embedding direction.
     */
    const size_t NO_INDEX = ~(size_t)0;
    BidiType prevStrongType = ctx->sos;
    size_t c_ni_start = NO_INDEX;
    for (size_t c = 0; c <= ctx->irslen; c++) {
        BidiType t;

        if (c < ctx->irslen) {
            size_t i = ctx->irs[c];
            t = ctx->types[i];
        } else {
            /* One extra loop iteration, using eos to resolve the
             * final sequence of NI if any */
            t = ctx->eos;
        }

        if (typeIsStrongOrNumber(t)) {
            t = t == L ? L : R;        /* numbers count as R */
            if (c_ni_start != NO_INDEX) {
                /* There are some NI we have to fix up */
                BidiType ni_type = (t == prevStrongType ? t :
                                    ctx->embeddingDirection);
                for (size_t c2 = c_ni_start; c2 < c; c2++) {
                    size_t i2 = ctx->irs[c2];
                    BidiType t2 = ctx->types[i2];
                    if (typeIsNeutralOrIsolate(t2))
                        ctx->types[i2] = ni_type;
                }
            }
            prevStrongType = t;
            c_ni_start = NO_INDEX;
        } else if (typeIsNeutralOrIsolate(t) && c_ni_start == NO_INDEX) {
            c_ni_start = c;
        }
    }
}

static void resolve_implicit_levels(BidiContext *ctx)
{
    /* Rules I1 and I2 */
    for (size_t c = 0; c < ctx->irslen; c++) {
        size_t i = ctx->irs[c];
        unsigned char level = ctx->levels[i];
        BidiType t = ctx->types[i];
        if (level % 2 == 0) {
            /* Rule I1 */
            if (t == R)
                ctx->levels[i] += 1;
            else if (t == AN || t == EN)
                ctx->levels[i] += 2;
        } else {
            /* Rule I2 */
            if (t == L || t == AN || t == EN)
                ctx->levels[i] += 1;
        }
    }
}

static void process_isolating_run_sequence(BidiContext *ctx)
{
    /* Section W: resolve weak types  */
    remove_nsm(ctx);
    change_en_to_an(ctx);
    change_al_to_r(ctx);
    eliminate_separators_between_numbers(ctx);
    eliminate_et_next_to_en(ctx);
    eliminate_separators_and_terminators(ctx);
    change_en_to_l(ctx);

    /* Section N: resolve neutral types (and isolates) */
    find_bracket_pairs(ctx, resolve_brackets);
    remove_ni(ctx);

    /* Section I: resolve implicit levels */
    resolve_implicit_levels(ctx);
}

static void reset_whitespace_and_separators(BidiContext *ctx)
{
    /*
     * Rule L1: segment and paragraph separators, plus whitespace
     * preceding them, all reset to the paragraph embedding level.
     * This also applies to whitespace at the very end.
     *
     * This is done using the original types, not the versions that
     * the rest of this algorithm has been merrily mutating.
     */
    bool modifying = true;
    for (size_t i = ctx->textlen; i-- > 0 ;) {
        BidiType t = ctx->origTypes[i];
        if (typeIsSegmentOrParaSeparator(t)) {
            ctx->levels[i] = ctx->paragraphLevel;
            modifying = true;
        } else if (modifying) {
            if (typeIsWhitespaceOrIsolate(t)) {
                ctx->levels[i] = ctx->paragraphLevel;
            } else if (!typeIsRemovedDuringProcessing(t)) {
                modifying = false;
            }
        }
    }

#ifndef REMOVE_FORMATTING_CHARS
    /*
     * Section 5.2 adjustment: types removed by rule X9 take the level
     * of the character to their left.
     */
    for (size_t i = 0; i < ctx->textlen; i++) {
        BidiType t = ctx->origTypes[i];
        if (typeIsRemovedDuringProcessing(t)) {
            /* Section 5.2 adjustment */
            ctx->levels[i] = (i > 0 ? ctx->levels[i-1] : ctx->paragraphLevel);
        }
    }
#endif /* ! REMOVE_FORMATTING_CHARS */
}

static void reverse(BidiContext *ctx, size_t start, size_t end)
{
    for (size_t i = start, j = end; i < j; i++, j--) {
        bidi_char tmp = ctx->text[i];
        ctx->text[i] = ctx->text[j];
        ctx->text[j] = tmp;
    }
}

static void mirror_glyphs(BidiContext *ctx)
{
    /*
     * Rule L3: any character with a mirror-image pair at an odd
     * embedding level is replaced by its mirror image.
     *
     * This is specified in the standard as happening _after_ rule L2
     * (the actual reordering of the text). But it's much easier to
     * implement it before, while our levels[] array still matches up
     * to the text order.
     */
    for (size_t i = 0; i < ctx->textlen; i++) {
        if (ctx->levels[i] % 2)
            ctx->text[i].wc = mirror_glyph(ctx->text[i].wc);
    }
}

static void reverse_sequences(BidiContext *ctx)
{
    /*
     * Rule L2: every maximal contiguous sequence of characters at a
     * given level or higher is reversed.
     */
    unsigned level = 0;
    for (size_t i = 0; i < ctx->textlen; i++)
        level = max(level, ctx->levels[i]);

    for (; level >= 1; level--) {
        for (size_t i = 0; i < ctx->textlen; i++) {
            if (ctx->levels[i] >= level) {
                size_t start = i;
                while (i+1 < ctx->textlen && ctx->levels[i+1] >= level)
                    i++;
                reverse(ctx, start, i);
            }
        }
    }
}

/*
 * The Main Bidi Function. The two wrappers below it present different
 * external APIs for different purposes, but everything comes through
 * here.
 *
 * text: a buffer of size textlen containing text to apply the
 * Bidirectional algorithm to.
 */
static void do_bidi_new(BidiContext *ctx, bidi_char *text, size_t textlen)
{
    ensure_arrays(ctx, textlen);
    ctx->text = text;
    ctx->textlen = textlen;
    setup_types(ctx);

    /* Quick initial test: see if we need to bother with any work at all */
    if (!text_needs_bidi(ctx))
        return;

    set_paragraph_level(ctx);
    process_explicit_embeddings(ctx);
    remove_embedding_characters(ctx);
    find_isolating_run_sequences(ctx, process_isolating_run_sequence);

    /* If this implementation distinguished paragraphs from lines,
     * then this would be the point where we repeat the remainder of
     * the algorithm once for each line in the paragraph. */

    reset_whitespace_and_separators(ctx);
    mirror_glyphs(ctx);
    reverse_sequences(ctx);
}

size_t do_bidi_test(BidiContext *ctx, bidi_char *text, size_t textlen,
                    int override)
{
    ctx->paragraphOverride = (override > 0 ? L : override < 0 ? R : ON);
    do_bidi_new(ctx, text, textlen);
    return ctx->textlen;
}

void do_bidi(BidiContext *ctx, bidi_char *text, size_t textlen)
{
#ifdef REMOVE_FORMATTING_CHARACTERS
    abort(); /* can't use the standard algorithm in a live terminal */
#else
    ctx->paragraphOverride = ON;
    do_bidi_new(ctx, text, textlen);
#endif
}
