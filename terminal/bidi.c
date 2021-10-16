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
 * revision 44:
 *
 *   https://www.unicode.org/reports/tr9/tr9-44.html
 *
 * and passes the full conformance test suite in Unicode 14.0.0.
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
 *
 * The data table in this function is constructed from the Unicode
 * Character Database version 14.0.0, downloadable from unicode.org at
 * the URL
 *
 *     https://www.unicode.org/Public/14.0.0/ucd/
 *
 * by the following fragment of Perl:

perl -ne '@_=split ";"; $num = hex $_[0]; $type = $_[4];' \
      -e '$fl = ($_[1] =~ /First/ ? 1 : $_[1] =~ /Last/ ? 2 : 0);' \
      -e 'if ($type eq $runtype and ($runend == $num-1 or ' \
      -e '    ($fl==2 and $pfl==1))) {$runend = $num;} else { &reset; }' \
      -e '$pfl=$fl; END { &reset }; sub reset {' \
      -e 'printf"        {0x%04x, 0x%04x, %s},\n",$runstart,$runend,$runtype' \
      -e '  if defined $runstart and $runtype ne "ON";' \
      -e '$runstart=$runend=$num; $runtype=$type;}' \
    UnicodeData.txt

 */
unsigned char bidi_getType(int ch)
{
    static const struct {
        int first, last, type;
    } lookup[] = {
        {0x0000, 0x0008, BN},
        {0x0009, 0x0009, S},
        {0x000a, 0x000a, B},
        {0x000b, 0x000b, S},
        {0x000c, 0x000c, WS},
        {0x000d, 0x000d, B},
        {0x000e, 0x001b, BN},
        {0x001c, 0x001e, B},
        {0x001f, 0x001f, S},
        {0x0020, 0x0020, WS},
        {0x0023, 0x0025, ET},
        {0x002b, 0x002b, ES},
        {0x002c, 0x002c, CS},
        {0x002d, 0x002d, ES},
        {0x002e, 0x002f, CS},
        {0x0030, 0x0039, EN},
        {0x003a, 0x003a, CS},
        {0x0041, 0x005a, L},
        {0x0061, 0x007a, L},
        {0x007f, 0x0084, BN},
        {0x0085, 0x0085, B},
        {0x0086, 0x009f, BN},
        {0x00a0, 0x00a0, CS},
        {0x00a2, 0x00a5, ET},
        {0x00aa, 0x00aa, L},
        {0x00ad, 0x00ad, BN},
        {0x00b0, 0x00b1, ET},
        {0x00b2, 0x00b3, EN},
        {0x00b5, 0x00b5, L},
        {0x00b9, 0x00b9, EN},
        {0x00ba, 0x00ba, L},
        {0x00c0, 0x00d6, L},
        {0x00d8, 0x00f6, L},
        {0x00f8, 0x02b8, L},
        {0x02bb, 0x02c1, L},
        {0x02d0, 0x02d1, L},
        {0x02e0, 0x02e4, L},
        {0x02ee, 0x02ee, L},
        {0x0300, 0x036f, NSM},
        {0x0370, 0x0373, L},
        {0x0376, 0x0377, L},
        {0x037a, 0x037d, L},
        {0x037f, 0x037f, L},
        {0x0386, 0x0386, L},
        {0x0388, 0x038a, L},
        {0x038c, 0x038c, L},
        {0x038e, 0x03a1, L},
        {0x03a3, 0x03f5, L},
        {0x03f7, 0x0482, L},
        {0x0483, 0x0489, NSM},
        {0x048a, 0x052f, L},
        {0x0531, 0x0556, L},
        {0x0559, 0x0589, L},
        {0x058f, 0x058f, ET},
        {0x0591, 0x05bd, NSM},
        {0x05be, 0x05be, R},
        {0x05bf, 0x05bf, NSM},
        {0x05c0, 0x05c0, R},
        {0x05c1, 0x05c2, NSM},
        {0x05c3, 0x05c3, R},
        {0x05c4, 0x05c5, NSM},
        {0x05c6, 0x05c6, R},
        {0x05c7, 0x05c7, NSM},
        {0x05d0, 0x05ea, R},
        {0x05ef, 0x05f4, R},
        {0x0600, 0x0605, AN},
        {0x0608, 0x0608, AL},
        {0x0609, 0x060a, ET},
        {0x060b, 0x060b, AL},
        {0x060c, 0x060c, CS},
        {0x060d, 0x060d, AL},
        {0x0610, 0x061a, NSM},
        {0x061b, 0x064a, AL},
        {0x064b, 0x065f, NSM},
        {0x0660, 0x0669, AN},
        {0x066a, 0x066a, ET},
        {0x066b, 0x066c, AN},
        {0x066d, 0x066f, AL},
        {0x0670, 0x0670, NSM},
        {0x0671, 0x06d5, AL},
        {0x06d6, 0x06dc, NSM},
        {0x06dd, 0x06dd, AN},
        {0x06df, 0x06e4, NSM},
        {0x06e5, 0x06e6, AL},
        {0x06e7, 0x06e8, NSM},
        {0x06ea, 0x06ed, NSM},
        {0x06ee, 0x06ef, AL},
        {0x06f0, 0x06f9, EN},
        {0x06fa, 0x070d, AL},
        {0x070f, 0x0710, AL},
        {0x0711, 0x0711, NSM},
        {0x0712, 0x072f, AL},
        {0x0730, 0x074a, NSM},
        {0x074d, 0x07a5, AL},
        {0x07a6, 0x07b0, NSM},
        {0x07b1, 0x07b1, AL},
        {0x07c0, 0x07ea, R},
        {0x07eb, 0x07f3, NSM},
        {0x07f4, 0x07f5, R},
        {0x07fa, 0x07fa, R},
        {0x07fd, 0x07fd, NSM},
        {0x07fe, 0x0815, R},
        {0x0816, 0x0819, NSM},
        {0x081a, 0x081a, R},
        {0x081b, 0x0823, NSM},
        {0x0824, 0x0824, R},
        {0x0825, 0x0827, NSM},
        {0x0828, 0x0828, R},
        {0x0829, 0x082d, NSM},
        {0x0830, 0x083e, R},
        {0x0840, 0x0858, R},
        {0x0859, 0x085b, NSM},
        {0x085e, 0x085e, R},
        {0x0860, 0x086a, AL},
        {0x0870, 0x088e, AL},
        {0x0890, 0x0891, AN},
        {0x0898, 0x089f, NSM},
        {0x08a0, 0x08c9, AL},
        {0x08ca, 0x08e1, NSM},
        {0x08e2, 0x08e2, AN},
        {0x08e3, 0x0902, NSM},
        {0x0903, 0x0939, L},
        {0x093a, 0x093a, NSM},
        {0x093b, 0x093b, L},
        {0x093c, 0x093c, NSM},
        {0x093d, 0x0940, L},
        {0x0941, 0x0948, NSM},
        {0x0949, 0x094c, L},
        {0x094d, 0x094d, NSM},
        {0x094e, 0x0950, L},
        {0x0951, 0x0957, NSM},
        {0x0958, 0x0961, L},
        {0x0962, 0x0963, NSM},
        {0x0964, 0x0980, L},
        {0x0981, 0x0981, NSM},
        {0x0982, 0x0983, L},
        {0x0985, 0x098c, L},
        {0x098f, 0x0990, L},
        {0x0993, 0x09a8, L},
        {0x09aa, 0x09b0, L},
        {0x09b2, 0x09b2, L},
        {0x09b6, 0x09b9, L},
        {0x09bc, 0x09bc, NSM},
        {0x09bd, 0x09c0, L},
        {0x09c1, 0x09c4, NSM},
        {0x09c7, 0x09c8, L},
        {0x09cb, 0x09cc, L},
        {0x09cd, 0x09cd, NSM},
        {0x09ce, 0x09ce, L},
        {0x09d7, 0x09d7, L},
        {0x09dc, 0x09dd, L},
        {0x09df, 0x09e1, L},
        {0x09e2, 0x09e3, NSM},
        {0x09e6, 0x09f1, L},
        {0x09f2, 0x09f3, ET},
        {0x09f4, 0x09fa, L},
        {0x09fb, 0x09fb, ET},
        {0x09fc, 0x09fd, L},
        {0x09fe, 0x09fe, NSM},
        {0x0a01, 0x0a02, NSM},
        {0x0a03, 0x0a03, L},
        {0x0a05, 0x0a0a, L},
        {0x0a0f, 0x0a10, L},
        {0x0a13, 0x0a28, L},
        {0x0a2a, 0x0a30, L},
        {0x0a32, 0x0a33, L},
        {0x0a35, 0x0a36, L},
        {0x0a38, 0x0a39, L},
        {0x0a3c, 0x0a3c, NSM},
        {0x0a3e, 0x0a40, L},
        {0x0a41, 0x0a42, NSM},
        {0x0a47, 0x0a48, NSM},
        {0x0a4b, 0x0a4d, NSM},
        {0x0a51, 0x0a51, NSM},
        {0x0a59, 0x0a5c, L},
        {0x0a5e, 0x0a5e, L},
        {0x0a66, 0x0a6f, L},
        {0x0a70, 0x0a71, NSM},
        {0x0a72, 0x0a74, L},
        {0x0a75, 0x0a75, NSM},
        {0x0a76, 0x0a76, L},
        {0x0a81, 0x0a82, NSM},
        {0x0a83, 0x0a83, L},
        {0x0a85, 0x0a8d, L},
        {0x0a8f, 0x0a91, L},
        {0x0a93, 0x0aa8, L},
        {0x0aaa, 0x0ab0, L},
        {0x0ab2, 0x0ab3, L},
        {0x0ab5, 0x0ab9, L},
        {0x0abc, 0x0abc, NSM},
        {0x0abd, 0x0ac0, L},
        {0x0ac1, 0x0ac5, NSM},
        {0x0ac7, 0x0ac8, NSM},
        {0x0ac9, 0x0ac9, L},
        {0x0acb, 0x0acc, L},
        {0x0acd, 0x0acd, NSM},
        {0x0ad0, 0x0ad0, L},
        {0x0ae0, 0x0ae1, L},
        {0x0ae2, 0x0ae3, NSM},
        {0x0ae6, 0x0af0, L},
        {0x0af1, 0x0af1, ET},
        {0x0af9, 0x0af9, L},
        {0x0afa, 0x0aff, NSM},
        {0x0b01, 0x0b01, NSM},
        {0x0b02, 0x0b03, L},
        {0x0b05, 0x0b0c, L},
        {0x0b0f, 0x0b10, L},
        {0x0b13, 0x0b28, L},
        {0x0b2a, 0x0b30, L},
        {0x0b32, 0x0b33, L},
        {0x0b35, 0x0b39, L},
        {0x0b3c, 0x0b3c, NSM},
        {0x0b3d, 0x0b3e, L},
        {0x0b3f, 0x0b3f, NSM},
        {0x0b40, 0x0b40, L},
        {0x0b41, 0x0b44, NSM},
        {0x0b47, 0x0b48, L},
        {0x0b4b, 0x0b4c, L},
        {0x0b4d, 0x0b4d, NSM},
        {0x0b55, 0x0b56, NSM},
        {0x0b57, 0x0b57, L},
        {0x0b5c, 0x0b5d, L},
        {0x0b5f, 0x0b61, L},
        {0x0b62, 0x0b63, NSM},
        {0x0b66, 0x0b77, L},
        {0x0b82, 0x0b82, NSM},
        {0x0b83, 0x0b83, L},
        {0x0b85, 0x0b8a, L},
        {0x0b8e, 0x0b90, L},
        {0x0b92, 0x0b95, L},
        {0x0b99, 0x0b9a, L},
        {0x0b9c, 0x0b9c, L},
        {0x0b9e, 0x0b9f, L},
        {0x0ba3, 0x0ba4, L},
        {0x0ba8, 0x0baa, L},
        {0x0bae, 0x0bb9, L},
        {0x0bbe, 0x0bbf, L},
        {0x0bc0, 0x0bc0, NSM},
        {0x0bc1, 0x0bc2, L},
        {0x0bc6, 0x0bc8, L},
        {0x0bca, 0x0bcc, L},
        {0x0bcd, 0x0bcd, NSM},
        {0x0bd0, 0x0bd0, L},
        {0x0bd7, 0x0bd7, L},
        {0x0be6, 0x0bf2, L},
        {0x0bf9, 0x0bf9, ET},
        {0x0c00, 0x0c00, NSM},
        {0x0c01, 0x0c03, L},
        {0x0c04, 0x0c04, NSM},
        {0x0c05, 0x0c0c, L},
        {0x0c0e, 0x0c10, L},
        {0x0c12, 0x0c28, L},
        {0x0c2a, 0x0c39, L},
        {0x0c3c, 0x0c3c, NSM},
        {0x0c3d, 0x0c3d, L},
        {0x0c3e, 0x0c40, NSM},
        {0x0c41, 0x0c44, L},
        {0x0c46, 0x0c48, NSM},
        {0x0c4a, 0x0c4d, NSM},
        {0x0c55, 0x0c56, NSM},
        {0x0c58, 0x0c5a, L},
        {0x0c5d, 0x0c5d, L},
        {0x0c60, 0x0c61, L},
        {0x0c62, 0x0c63, NSM},
        {0x0c66, 0x0c6f, L},
        {0x0c77, 0x0c77, L},
        {0x0c7f, 0x0c80, L},
        {0x0c81, 0x0c81, NSM},
        {0x0c82, 0x0c8c, L},
        {0x0c8e, 0x0c90, L},
        {0x0c92, 0x0ca8, L},
        {0x0caa, 0x0cb3, L},
        {0x0cb5, 0x0cb9, L},
        {0x0cbc, 0x0cbc, NSM},
        {0x0cbd, 0x0cc4, L},
        {0x0cc6, 0x0cc8, L},
        {0x0cca, 0x0ccb, L},
        {0x0ccc, 0x0ccd, NSM},
        {0x0cd5, 0x0cd6, L},
        {0x0cdd, 0x0cde, L},
        {0x0ce0, 0x0ce1, L},
        {0x0ce2, 0x0ce3, NSM},
        {0x0ce6, 0x0cef, L},
        {0x0cf1, 0x0cf2, L},
        {0x0d00, 0x0d01, NSM},
        {0x0d02, 0x0d0c, L},
        {0x0d0e, 0x0d10, L},
        {0x0d12, 0x0d3a, L},
        {0x0d3b, 0x0d3c, NSM},
        {0x0d3d, 0x0d40, L},
        {0x0d41, 0x0d44, NSM},
        {0x0d46, 0x0d48, L},
        {0x0d4a, 0x0d4c, L},
        {0x0d4d, 0x0d4d, NSM},
        {0x0d4e, 0x0d4f, L},
        {0x0d54, 0x0d61, L},
        {0x0d62, 0x0d63, NSM},
        {0x0d66, 0x0d7f, L},
        {0x0d81, 0x0d81, NSM},
        {0x0d82, 0x0d83, L},
        {0x0d85, 0x0d96, L},
        {0x0d9a, 0x0db1, L},
        {0x0db3, 0x0dbb, L},
        {0x0dbd, 0x0dbd, L},
        {0x0dc0, 0x0dc6, L},
        {0x0dca, 0x0dca, NSM},
        {0x0dcf, 0x0dd1, L},
        {0x0dd2, 0x0dd4, NSM},
        {0x0dd6, 0x0dd6, NSM},
        {0x0dd8, 0x0ddf, L},
        {0x0de6, 0x0def, L},
        {0x0df2, 0x0df4, L},
        {0x0e01, 0x0e30, L},
        {0x0e31, 0x0e31, NSM},
        {0x0e32, 0x0e33, L},
        {0x0e34, 0x0e3a, NSM},
        {0x0e3f, 0x0e3f, ET},
        {0x0e40, 0x0e46, L},
        {0x0e47, 0x0e4e, NSM},
        {0x0e4f, 0x0e5b, L},
        {0x0e81, 0x0e82, L},
        {0x0e84, 0x0e84, L},
        {0x0e86, 0x0e8a, L},
        {0x0e8c, 0x0ea3, L},
        {0x0ea5, 0x0ea5, L},
        {0x0ea7, 0x0eb0, L},
        {0x0eb1, 0x0eb1, NSM},
        {0x0eb2, 0x0eb3, L},
        {0x0eb4, 0x0ebc, NSM},
        {0x0ebd, 0x0ebd, L},
        {0x0ec0, 0x0ec4, L},
        {0x0ec6, 0x0ec6, L},
        {0x0ec8, 0x0ecd, NSM},
        {0x0ed0, 0x0ed9, L},
        {0x0edc, 0x0edf, L},
        {0x0f00, 0x0f17, L},
        {0x0f18, 0x0f19, NSM},
        {0x0f1a, 0x0f34, L},
        {0x0f35, 0x0f35, NSM},
        {0x0f36, 0x0f36, L},
        {0x0f37, 0x0f37, NSM},
        {0x0f38, 0x0f38, L},
        {0x0f39, 0x0f39, NSM},
        {0x0f3e, 0x0f47, L},
        {0x0f49, 0x0f6c, L},
        {0x0f71, 0x0f7e, NSM},
        {0x0f7f, 0x0f7f, L},
        {0x0f80, 0x0f84, NSM},
        {0x0f85, 0x0f85, L},
        {0x0f86, 0x0f87, NSM},
        {0x0f88, 0x0f8c, L},
        {0x0f8d, 0x0f97, NSM},
        {0x0f99, 0x0fbc, NSM},
        {0x0fbe, 0x0fc5, L},
        {0x0fc6, 0x0fc6, NSM},
        {0x0fc7, 0x0fcc, L},
        {0x0fce, 0x0fda, L},
        {0x1000, 0x102c, L},
        {0x102d, 0x1030, NSM},
        {0x1031, 0x1031, L},
        {0x1032, 0x1037, NSM},
        {0x1038, 0x1038, L},
        {0x1039, 0x103a, NSM},
        {0x103b, 0x103c, L},
        {0x103d, 0x103e, NSM},
        {0x103f, 0x1057, L},
        {0x1058, 0x1059, NSM},
        {0x105a, 0x105d, L},
        {0x105e, 0x1060, NSM},
        {0x1061, 0x1070, L},
        {0x1071, 0x1074, NSM},
        {0x1075, 0x1081, L},
        {0x1082, 0x1082, NSM},
        {0x1083, 0x1084, L},
        {0x1085, 0x1086, NSM},
        {0x1087, 0x108c, L},
        {0x108d, 0x108d, NSM},
        {0x108e, 0x109c, L},
        {0x109d, 0x109d, NSM},
        {0x109e, 0x10c5, L},
        {0x10c7, 0x10c7, L},
        {0x10cd, 0x10cd, L},
        {0x10d0, 0x1248, L},
        {0x124a, 0x124d, L},
        {0x1250, 0x1256, L},
        {0x1258, 0x1258, L},
        {0x125a, 0x125d, L},
        {0x1260, 0x1288, L},
        {0x128a, 0x128d, L},
        {0x1290, 0x12b0, L},
        {0x12b2, 0x12b5, L},
        {0x12b8, 0x12be, L},
        {0x12c0, 0x12c0, L},
        {0x12c2, 0x12c5, L},
        {0x12c8, 0x12d6, L},
        {0x12d8, 0x1310, L},
        {0x1312, 0x1315, L},
        {0x1318, 0x135a, L},
        {0x135d, 0x135f, NSM},
        {0x1360, 0x137c, L},
        {0x1380, 0x138f, L},
        {0x13a0, 0x13f5, L},
        {0x13f8, 0x13fd, L},
        {0x1401, 0x167f, L},
        {0x1680, 0x1680, WS},
        {0x1681, 0x169a, L},
        {0x16a0, 0x16f8, L},
        {0x1700, 0x1711, L},
        {0x1712, 0x1714, NSM},
        {0x1715, 0x1715, L},
        {0x171f, 0x1731, L},
        {0x1732, 0x1733, NSM},
        {0x1734, 0x1736, L},
        {0x1740, 0x1751, L},
        {0x1752, 0x1753, NSM},
        {0x1760, 0x176c, L},
        {0x176e, 0x1770, L},
        {0x1772, 0x1773, NSM},
        {0x1780, 0x17b3, L},
        {0x17b4, 0x17b5, NSM},
        {0x17b6, 0x17b6, L},
        {0x17b7, 0x17bd, NSM},
        {0x17be, 0x17c5, L},
        {0x17c6, 0x17c6, NSM},
        {0x17c7, 0x17c8, L},
        {0x17c9, 0x17d3, NSM},
        {0x17d4, 0x17da, L},
        {0x17db, 0x17db, ET},
        {0x17dc, 0x17dc, L},
        {0x17dd, 0x17dd, NSM},
        {0x17e0, 0x17e9, L},
        {0x180b, 0x180d, NSM},
        {0x180e, 0x180e, BN},
        {0x180f, 0x180f, NSM},
        {0x1810, 0x1819, L},
        {0x1820, 0x1878, L},
        {0x1880, 0x1884, L},
        {0x1885, 0x1886, NSM},
        {0x1887, 0x18a8, L},
        {0x18a9, 0x18a9, NSM},
        {0x18aa, 0x18aa, L},
        {0x18b0, 0x18f5, L},
        {0x1900, 0x191e, L},
        {0x1920, 0x1922, NSM},
        {0x1923, 0x1926, L},
        {0x1927, 0x1928, NSM},
        {0x1929, 0x192b, L},
        {0x1930, 0x1931, L},
        {0x1932, 0x1932, NSM},
        {0x1933, 0x1938, L},
        {0x1939, 0x193b, NSM},
        {0x1946, 0x196d, L},
        {0x1970, 0x1974, L},
        {0x1980, 0x19ab, L},
        {0x19b0, 0x19c9, L},
        {0x19d0, 0x19da, L},
        {0x1a00, 0x1a16, L},
        {0x1a17, 0x1a18, NSM},
        {0x1a19, 0x1a1a, L},
        {0x1a1b, 0x1a1b, NSM},
        {0x1a1e, 0x1a55, L},
        {0x1a56, 0x1a56, NSM},
        {0x1a57, 0x1a57, L},
        {0x1a58, 0x1a5e, NSM},
        {0x1a60, 0x1a60, NSM},
        {0x1a61, 0x1a61, L},
        {0x1a62, 0x1a62, NSM},
        {0x1a63, 0x1a64, L},
        {0x1a65, 0x1a6c, NSM},
        {0x1a6d, 0x1a72, L},
        {0x1a73, 0x1a7c, NSM},
        {0x1a7f, 0x1a7f, NSM},
        {0x1a80, 0x1a89, L},
        {0x1a90, 0x1a99, L},
        {0x1aa0, 0x1aad, L},
        {0x1ab0, 0x1ace, NSM},
        {0x1b00, 0x1b03, NSM},
        {0x1b04, 0x1b33, L},
        {0x1b34, 0x1b34, NSM},
        {0x1b35, 0x1b35, L},
        {0x1b36, 0x1b3a, NSM},
        {0x1b3b, 0x1b3b, L},
        {0x1b3c, 0x1b3c, NSM},
        {0x1b3d, 0x1b41, L},
        {0x1b42, 0x1b42, NSM},
        {0x1b43, 0x1b4c, L},
        {0x1b50, 0x1b6a, L},
        {0x1b6b, 0x1b73, NSM},
        {0x1b74, 0x1b7e, L},
        {0x1b80, 0x1b81, NSM},
        {0x1b82, 0x1ba1, L},
        {0x1ba2, 0x1ba5, NSM},
        {0x1ba6, 0x1ba7, L},
        {0x1ba8, 0x1ba9, NSM},
        {0x1baa, 0x1baa, L},
        {0x1bab, 0x1bad, NSM},
        {0x1bae, 0x1be5, L},
        {0x1be6, 0x1be6, NSM},
        {0x1be7, 0x1be7, L},
        {0x1be8, 0x1be9, NSM},
        {0x1bea, 0x1bec, L},
        {0x1bed, 0x1bed, NSM},
        {0x1bee, 0x1bee, L},
        {0x1bef, 0x1bf1, NSM},
        {0x1bf2, 0x1bf3, L},
        {0x1bfc, 0x1c2b, L},
        {0x1c2c, 0x1c33, NSM},
        {0x1c34, 0x1c35, L},
        {0x1c36, 0x1c37, NSM},
        {0x1c3b, 0x1c49, L},
        {0x1c4d, 0x1c88, L},
        {0x1c90, 0x1cba, L},
        {0x1cbd, 0x1cc7, L},
        {0x1cd0, 0x1cd2, NSM},
        {0x1cd3, 0x1cd3, L},
        {0x1cd4, 0x1ce0, NSM},
        {0x1ce1, 0x1ce1, L},
        {0x1ce2, 0x1ce8, NSM},
        {0x1ce9, 0x1cec, L},
        {0x1ced, 0x1ced, NSM},
        {0x1cee, 0x1cf3, L},
        {0x1cf4, 0x1cf4, NSM},
        {0x1cf5, 0x1cf7, L},
        {0x1cf8, 0x1cf9, NSM},
        {0x1cfa, 0x1cfa, L},
        {0x1d00, 0x1dbf, L},
        {0x1dc0, 0x1dff, NSM},
        {0x1e00, 0x1f15, L},
        {0x1f18, 0x1f1d, L},
        {0x1f20, 0x1f45, L},
        {0x1f48, 0x1f4d, L},
        {0x1f50, 0x1f57, L},
        {0x1f59, 0x1f59, L},
        {0x1f5b, 0x1f5b, L},
        {0x1f5d, 0x1f5d, L},
        {0x1f5f, 0x1f7d, L},
        {0x1f80, 0x1fb4, L},
        {0x1fb6, 0x1fbc, L},
        {0x1fbe, 0x1fbe, L},
        {0x1fc2, 0x1fc4, L},
        {0x1fc6, 0x1fcc, L},
        {0x1fd0, 0x1fd3, L},
        {0x1fd6, 0x1fdb, L},
        {0x1fe0, 0x1fec, L},
        {0x1ff2, 0x1ff4, L},
        {0x1ff6, 0x1ffc, L},
        {0x2000, 0x200a, WS},
        {0x200b, 0x200d, BN},
        {0x200e, 0x200e, L},
        {0x200f, 0x200f, R},
        {0x2028, 0x2028, WS},
        {0x2029, 0x2029, B},
        {0x202a, 0x202a, LRE},
        {0x202b, 0x202b, RLE},
        {0x202c, 0x202c, PDF},
        {0x202d, 0x202d, LRO},
        {0x202e, 0x202e, RLO},
        {0x202f, 0x202f, CS},
        {0x2030, 0x2034, ET},
        {0x2044, 0x2044, CS},
        {0x205f, 0x205f, WS},
        {0x2060, 0x2064, BN},
        {0x2066, 0x2066, LRI},
        {0x2067, 0x2067, RLI},
        {0x2068, 0x2068, FSI},
        {0x2069, 0x2069, PDI},
        {0x206a, 0x206f, BN},
        {0x2070, 0x2070, EN},
        {0x2071, 0x2071, L},
        {0x2074, 0x2079, EN},
        {0x207a, 0x207b, ES},
        {0x207f, 0x207f, L},
        {0x2080, 0x2089, EN},
        {0x208a, 0x208b, ES},
        {0x2090, 0x209c, L},
        {0x20a0, 0x20c0, ET},
        {0x20d0, 0x20f0, NSM},
        {0x2102, 0x2102, L},
        {0x2107, 0x2107, L},
        {0x210a, 0x2113, L},
        {0x2115, 0x2115, L},
        {0x2119, 0x211d, L},
        {0x2124, 0x2124, L},
        {0x2126, 0x2126, L},
        {0x2128, 0x2128, L},
        {0x212a, 0x212d, L},
        {0x212e, 0x212e, ET},
        {0x212f, 0x2139, L},
        {0x213c, 0x213f, L},
        {0x2145, 0x2149, L},
        {0x214e, 0x214f, L},
        {0x2160, 0x2188, L},
        {0x2212, 0x2212, ES},
        {0x2213, 0x2213, ET},
        {0x2336, 0x237a, L},
        {0x2395, 0x2395, L},
        {0x2488, 0x249b, EN},
        {0x249c, 0x24e9, L},
        {0x26ac, 0x26ac, L},
        {0x2800, 0x28ff, L},
        {0x2c00, 0x2ce4, L},
        {0x2ceb, 0x2cee, L},
        {0x2cef, 0x2cf1, NSM},
        {0x2cf2, 0x2cf3, L},
        {0x2d00, 0x2d25, L},
        {0x2d27, 0x2d27, L},
        {0x2d2d, 0x2d2d, L},
        {0x2d30, 0x2d67, L},
        {0x2d6f, 0x2d70, L},
        {0x2d7f, 0x2d7f, NSM},
        {0x2d80, 0x2d96, L},
        {0x2da0, 0x2da6, L},
        {0x2da8, 0x2dae, L},
        {0x2db0, 0x2db6, L},
        {0x2db8, 0x2dbe, L},
        {0x2dc0, 0x2dc6, L},
        {0x2dc8, 0x2dce, L},
        {0x2dd0, 0x2dd6, L},
        {0x2dd8, 0x2dde, L},
        {0x2de0, 0x2dff, NSM},
        {0x3000, 0x3000, WS},
        {0x3005, 0x3007, L},
        {0x3021, 0x3029, L},
        {0x302a, 0x302d, NSM},
        {0x302e, 0x302f, L},
        {0x3031, 0x3035, L},
        {0x3038, 0x303c, L},
        {0x3041, 0x3096, L},
        {0x3099, 0x309a, NSM},
        {0x309d, 0x309f, L},
        {0x30a1, 0x30fa, L},
        {0x30fc, 0x30ff, L},
        {0x3105, 0x312f, L},
        {0x3131, 0x318e, L},
        {0x3190, 0x31bf, L},
        {0x31f0, 0x321c, L},
        {0x3220, 0x324f, L},
        {0x3260, 0x327b, L},
        {0x327f, 0x32b0, L},
        {0x32c0, 0x32cb, L},
        {0x32d0, 0x3376, L},
        {0x337b, 0x33dd, L},
        {0x33e0, 0x33fe, L},
        {0x3400, 0x4dbf, L},
        {0x4e00, 0xa48c, L},
        {0xa4d0, 0xa60c, L},
        {0xa610, 0xa62b, L},
        {0xa640, 0xa66e, L},
        {0xa66f, 0xa672, NSM},
        {0xa674, 0xa67d, NSM},
        {0xa680, 0xa69d, L},
        {0xa69e, 0xa69f, NSM},
        {0xa6a0, 0xa6ef, L},
        {0xa6f0, 0xa6f1, NSM},
        {0xa6f2, 0xa6f7, L},
        {0xa722, 0xa787, L},
        {0xa789, 0xa7ca, L},
        {0xa7d0, 0xa7d1, L},
        {0xa7d3, 0xa7d3, L},
        {0xa7d5, 0xa7d9, L},
        {0xa7f2, 0xa801, L},
        {0xa802, 0xa802, NSM},
        {0xa803, 0xa805, L},
        {0xa806, 0xa806, NSM},
        {0xa807, 0xa80a, L},
        {0xa80b, 0xa80b, NSM},
        {0xa80c, 0xa824, L},
        {0xa825, 0xa826, NSM},
        {0xa827, 0xa827, L},
        {0xa82c, 0xa82c, NSM},
        {0xa830, 0xa837, L},
        {0xa838, 0xa839, ET},
        {0xa840, 0xa873, L},
        {0xa880, 0xa8c3, L},
        {0xa8c4, 0xa8c5, NSM},
        {0xa8ce, 0xa8d9, L},
        {0xa8e0, 0xa8f1, NSM},
        {0xa8f2, 0xa8fe, L},
        {0xa8ff, 0xa8ff, NSM},
        {0xa900, 0xa925, L},
        {0xa926, 0xa92d, NSM},
        {0xa92e, 0xa946, L},
        {0xa947, 0xa951, NSM},
        {0xa952, 0xa953, L},
        {0xa95f, 0xa97c, L},
        {0xa980, 0xa982, NSM},
        {0xa983, 0xa9b2, L},
        {0xa9b3, 0xa9b3, NSM},
        {0xa9b4, 0xa9b5, L},
        {0xa9b6, 0xa9b9, NSM},
        {0xa9ba, 0xa9bb, L},
        {0xa9bc, 0xa9bd, NSM},
        {0xa9be, 0xa9cd, L},
        {0xa9cf, 0xa9d9, L},
        {0xa9de, 0xa9e4, L},
        {0xa9e5, 0xa9e5, NSM},
        {0xa9e6, 0xa9fe, L},
        {0xaa00, 0xaa28, L},
        {0xaa29, 0xaa2e, NSM},
        {0xaa2f, 0xaa30, L},
        {0xaa31, 0xaa32, NSM},
        {0xaa33, 0xaa34, L},
        {0xaa35, 0xaa36, NSM},
        {0xaa40, 0xaa42, L},
        {0xaa43, 0xaa43, NSM},
        {0xaa44, 0xaa4b, L},
        {0xaa4c, 0xaa4c, NSM},
        {0xaa4d, 0xaa4d, L},
        {0xaa50, 0xaa59, L},
        {0xaa5c, 0xaa7b, L},
        {0xaa7c, 0xaa7c, NSM},
        {0xaa7d, 0xaaaf, L},
        {0xaab0, 0xaab0, NSM},
        {0xaab1, 0xaab1, L},
        {0xaab2, 0xaab4, NSM},
        {0xaab5, 0xaab6, L},
        {0xaab7, 0xaab8, NSM},
        {0xaab9, 0xaabd, L},
        {0xaabe, 0xaabf, NSM},
        {0xaac0, 0xaac0, L},
        {0xaac1, 0xaac1, NSM},
        {0xaac2, 0xaac2, L},
        {0xaadb, 0xaaeb, L},
        {0xaaec, 0xaaed, NSM},
        {0xaaee, 0xaaf5, L},
        {0xaaf6, 0xaaf6, NSM},
        {0xab01, 0xab06, L},
        {0xab09, 0xab0e, L},
        {0xab11, 0xab16, L},
        {0xab20, 0xab26, L},
        {0xab28, 0xab2e, L},
        {0xab30, 0xab69, L},
        {0xab70, 0xabe4, L},
        {0xabe5, 0xabe5, NSM},
        {0xabe6, 0xabe7, L},
        {0xabe8, 0xabe8, NSM},
        {0xabe9, 0xabec, L},
        {0xabed, 0xabed, NSM},
        {0xabf0, 0xabf9, L},
        {0xac00, 0xd7a3, L},
        {0xd7b0, 0xd7c6, L},
        {0xd7cb, 0xd7fb, L},
        {0xd800, 0xfa6d, L},
        {0xfa70, 0xfad9, L},
        {0xfb00, 0xfb06, L},
        {0xfb13, 0xfb17, L},
        {0xfb1d, 0xfb1d, R},
        {0xfb1e, 0xfb1e, NSM},
        {0xfb1f, 0xfb28, R},
        {0xfb29, 0xfb29, ES},
        {0xfb2a, 0xfb36, R},
        {0xfb38, 0xfb3c, R},
        {0xfb3e, 0xfb3e, R},
        {0xfb40, 0xfb41, R},
        {0xfb43, 0xfb44, R},
        {0xfb46, 0xfb4f, R},
        {0xfb50, 0xfbc2, AL},
        {0xfbd3, 0xfd3d, AL},
        {0xfd50, 0xfd8f, AL},
        {0xfd92, 0xfdc7, AL},
        {0xfdf0, 0xfdfc, AL},
        {0xfe00, 0xfe0f, NSM},
        {0xfe20, 0xfe2f, NSM},
        {0xfe50, 0xfe50, CS},
        {0xfe52, 0xfe52, CS},
        {0xfe55, 0xfe55, CS},
        {0xfe5f, 0xfe5f, ET},
        {0xfe62, 0xfe63, ES},
        {0xfe69, 0xfe6a, ET},
        {0xfe70, 0xfe74, AL},
        {0xfe76, 0xfefc, AL},
        {0xfeff, 0xfeff, BN},
        {0xff03, 0xff05, ET},
        {0xff0b, 0xff0b, ES},
        {0xff0c, 0xff0c, CS},
        {0xff0d, 0xff0d, ES},
        {0xff0e, 0xff0f, CS},
        {0xff10, 0xff19, EN},
        {0xff1a, 0xff1a, CS},
        {0xff21, 0xff3a, L},
        {0xff41, 0xff5a, L},
        {0xff66, 0xffbe, L},
        {0xffc2, 0xffc7, L},
        {0xffca, 0xffcf, L},
        {0xffd2, 0xffd7, L},
        {0xffda, 0xffdc, L},
        {0xffe0, 0xffe1, ET},
        {0xffe5, 0xffe6, ET},
        {0x10000, 0x1000b, L},
        {0x1000d, 0x10026, L},
        {0x10028, 0x1003a, L},
        {0x1003c, 0x1003d, L},
        {0x1003f, 0x1004d, L},
        {0x10050, 0x1005d, L},
        {0x10080, 0x100fa, L},
        {0x10100, 0x10100, L},
        {0x10102, 0x10102, L},
        {0x10107, 0x10133, L},
        {0x10137, 0x1013f, L},
        {0x1018d, 0x1018e, L},
        {0x101d0, 0x101fc, L},
        {0x101fd, 0x101fd, NSM},
        {0x10280, 0x1029c, L},
        {0x102a0, 0x102d0, L},
        {0x102e0, 0x102e0, NSM},
        {0x102e1, 0x102fb, EN},
        {0x10300, 0x10323, L},
        {0x1032d, 0x1034a, L},
        {0x10350, 0x10375, L},
        {0x10376, 0x1037a, NSM},
        {0x10380, 0x1039d, L},
        {0x1039f, 0x103c3, L},
        {0x103c8, 0x103d5, L},
        {0x10400, 0x1049d, L},
        {0x104a0, 0x104a9, L},
        {0x104b0, 0x104d3, L},
        {0x104d8, 0x104fb, L},
        {0x10500, 0x10527, L},
        {0x10530, 0x10563, L},
        {0x1056f, 0x1057a, L},
        {0x1057c, 0x1058a, L},
        {0x1058c, 0x10592, L},
        {0x10594, 0x10595, L},
        {0x10597, 0x105a1, L},
        {0x105a3, 0x105b1, L},
        {0x105b3, 0x105b9, L},
        {0x105bb, 0x105bc, L},
        {0x10600, 0x10736, L},
        {0x10740, 0x10755, L},
        {0x10760, 0x10767, L},
        {0x10780, 0x10785, L},
        {0x10787, 0x107b0, L},
        {0x107b2, 0x107ba, L},
        {0x10800, 0x10805, R},
        {0x10808, 0x10808, R},
        {0x1080a, 0x10835, R},
        {0x10837, 0x10838, R},
        {0x1083c, 0x1083c, R},
        {0x1083f, 0x10855, R},
        {0x10857, 0x1089e, R},
        {0x108a7, 0x108af, R},
        {0x108e0, 0x108f2, R},
        {0x108f4, 0x108f5, R},
        {0x108fb, 0x1091b, R},
        {0x10920, 0x10939, R},
        {0x1093f, 0x1093f, R},
        {0x10980, 0x109b7, R},
        {0x109bc, 0x109cf, R},
        {0x109d2, 0x10a00, R},
        {0x10a01, 0x10a03, NSM},
        {0x10a05, 0x10a06, NSM},
        {0x10a0c, 0x10a0f, NSM},
        {0x10a10, 0x10a13, R},
        {0x10a15, 0x10a17, R},
        {0x10a19, 0x10a35, R},
        {0x10a38, 0x10a3a, NSM},
        {0x10a3f, 0x10a3f, NSM},
        {0x10a40, 0x10a48, R},
        {0x10a50, 0x10a58, R},
        {0x10a60, 0x10a9f, R},
        {0x10ac0, 0x10ae4, R},
        {0x10ae5, 0x10ae6, NSM},
        {0x10aeb, 0x10af6, R},
        {0x10b00, 0x10b35, R},
        {0x10b40, 0x10b55, R},
        {0x10b58, 0x10b72, R},
        {0x10b78, 0x10b91, R},
        {0x10b99, 0x10b9c, R},
        {0x10ba9, 0x10baf, R},
        {0x10c00, 0x10c48, R},
        {0x10c80, 0x10cb2, R},
        {0x10cc0, 0x10cf2, R},
        {0x10cfa, 0x10cff, R},
        {0x10d00, 0x10d23, AL},
        {0x10d24, 0x10d27, NSM},
        {0x10d30, 0x10d39, AN},
        {0x10e60, 0x10e7e, AN},
        {0x10e80, 0x10ea9, R},
        {0x10eab, 0x10eac, NSM},
        {0x10ead, 0x10ead, R},
        {0x10eb0, 0x10eb1, R},
        {0x10f00, 0x10f27, R},
        {0x10f30, 0x10f45, AL},
        {0x10f46, 0x10f50, NSM},
        {0x10f51, 0x10f59, AL},
        {0x10f70, 0x10f81, R},
        {0x10f82, 0x10f85, NSM},
        {0x10f86, 0x10f89, R},
        {0x10fb0, 0x10fcb, R},
        {0x10fe0, 0x10ff6, R},
        {0x11000, 0x11000, L},
        {0x11001, 0x11001, NSM},
        {0x11002, 0x11037, L},
        {0x11038, 0x11046, NSM},
        {0x11047, 0x1104d, L},
        {0x11066, 0x1106f, L},
        {0x11070, 0x11070, NSM},
        {0x11071, 0x11072, L},
        {0x11073, 0x11074, NSM},
        {0x11075, 0x11075, L},
        {0x1107f, 0x11081, NSM},
        {0x11082, 0x110b2, L},
        {0x110b3, 0x110b6, NSM},
        {0x110b7, 0x110b8, L},
        {0x110b9, 0x110ba, NSM},
        {0x110bb, 0x110c1, L},
        {0x110c2, 0x110c2, NSM},
        {0x110cd, 0x110cd, L},
        {0x110d0, 0x110e8, L},
        {0x110f0, 0x110f9, L},
        {0x11100, 0x11102, NSM},
        {0x11103, 0x11126, L},
        {0x11127, 0x1112b, NSM},
        {0x1112c, 0x1112c, L},
        {0x1112d, 0x11134, NSM},
        {0x11136, 0x11147, L},
        {0x11150, 0x11172, L},
        {0x11173, 0x11173, NSM},
        {0x11174, 0x11176, L},
        {0x11180, 0x11181, NSM},
        {0x11182, 0x111b5, L},
        {0x111b6, 0x111be, NSM},
        {0x111bf, 0x111c8, L},
        {0x111c9, 0x111cc, NSM},
        {0x111cd, 0x111ce, L},
        {0x111cf, 0x111cf, NSM},
        {0x111d0, 0x111df, L},
        {0x111e1, 0x111f4, L},
        {0x11200, 0x11211, L},
        {0x11213, 0x1122e, L},
        {0x1122f, 0x11231, NSM},
        {0x11232, 0x11233, L},
        {0x11234, 0x11234, NSM},
        {0x11235, 0x11235, L},
        {0x11236, 0x11237, NSM},
        {0x11238, 0x1123d, L},
        {0x1123e, 0x1123e, NSM},
        {0x11280, 0x11286, L},
        {0x11288, 0x11288, L},
        {0x1128a, 0x1128d, L},
        {0x1128f, 0x1129d, L},
        {0x1129f, 0x112a9, L},
        {0x112b0, 0x112de, L},
        {0x112df, 0x112df, NSM},
        {0x112e0, 0x112e2, L},
        {0x112e3, 0x112ea, NSM},
        {0x112f0, 0x112f9, L},
        {0x11300, 0x11301, NSM},
        {0x11302, 0x11303, L},
        {0x11305, 0x1130c, L},
        {0x1130f, 0x11310, L},
        {0x11313, 0x11328, L},
        {0x1132a, 0x11330, L},
        {0x11332, 0x11333, L},
        {0x11335, 0x11339, L},
        {0x1133b, 0x1133c, NSM},
        {0x1133d, 0x1133f, L},
        {0x11340, 0x11340, NSM},
        {0x11341, 0x11344, L},
        {0x11347, 0x11348, L},
        {0x1134b, 0x1134d, L},
        {0x11350, 0x11350, L},
        {0x11357, 0x11357, L},
        {0x1135d, 0x11363, L},
        {0x11366, 0x1136c, NSM},
        {0x11370, 0x11374, NSM},
        {0x11400, 0x11437, L},
        {0x11438, 0x1143f, NSM},
        {0x11440, 0x11441, L},
        {0x11442, 0x11444, NSM},
        {0x11445, 0x11445, L},
        {0x11446, 0x11446, NSM},
        {0x11447, 0x1145b, L},
        {0x1145d, 0x1145d, L},
        {0x1145e, 0x1145e, NSM},
        {0x1145f, 0x11461, L},
        {0x11480, 0x114b2, L},
        {0x114b3, 0x114b8, NSM},
        {0x114b9, 0x114b9, L},
        {0x114ba, 0x114ba, NSM},
        {0x114bb, 0x114be, L},
        {0x114bf, 0x114c0, NSM},
        {0x114c1, 0x114c1, L},
        {0x114c2, 0x114c3, NSM},
        {0x114c4, 0x114c7, L},
        {0x114d0, 0x114d9, L},
        {0x11580, 0x115b1, L},
        {0x115b2, 0x115b5, NSM},
        {0x115b8, 0x115bb, L},
        {0x115bc, 0x115bd, NSM},
        {0x115be, 0x115be, L},
        {0x115bf, 0x115c0, NSM},
        {0x115c1, 0x115db, L},
        {0x115dc, 0x115dd, NSM},
        {0x11600, 0x11632, L},
        {0x11633, 0x1163a, NSM},
        {0x1163b, 0x1163c, L},
        {0x1163d, 0x1163d, NSM},
        {0x1163e, 0x1163e, L},
        {0x1163f, 0x11640, NSM},
        {0x11641, 0x11644, L},
        {0x11650, 0x11659, L},
        {0x11680, 0x116aa, L},
        {0x116ab, 0x116ab, NSM},
        {0x116ac, 0x116ac, L},
        {0x116ad, 0x116ad, NSM},
        {0x116ae, 0x116af, L},
        {0x116b0, 0x116b5, NSM},
        {0x116b6, 0x116b6, L},
        {0x116b7, 0x116b7, NSM},
        {0x116b8, 0x116b9, L},
        {0x116c0, 0x116c9, L},
        {0x11700, 0x1171a, L},
        {0x1171d, 0x1171f, NSM},
        {0x11720, 0x11721, L},
        {0x11722, 0x11725, NSM},
        {0x11726, 0x11726, L},
        {0x11727, 0x1172b, NSM},
        {0x11730, 0x11746, L},
        {0x11800, 0x1182e, L},
        {0x1182f, 0x11837, NSM},
        {0x11838, 0x11838, L},
        {0x11839, 0x1183a, NSM},
        {0x1183b, 0x1183b, L},
        {0x118a0, 0x118f2, L},
        {0x118ff, 0x11906, L},
        {0x11909, 0x11909, L},
        {0x1190c, 0x11913, L},
        {0x11915, 0x11916, L},
        {0x11918, 0x11935, L},
        {0x11937, 0x11938, L},
        {0x1193b, 0x1193c, NSM},
        {0x1193d, 0x1193d, L},
        {0x1193e, 0x1193e, NSM},
        {0x1193f, 0x11942, L},
        {0x11943, 0x11943, NSM},
        {0x11944, 0x11946, L},
        {0x11950, 0x11959, L},
        {0x119a0, 0x119a7, L},
        {0x119aa, 0x119d3, L},
        {0x119d4, 0x119d7, NSM},
        {0x119da, 0x119db, NSM},
        {0x119dc, 0x119df, L},
        {0x119e0, 0x119e0, NSM},
        {0x119e1, 0x119e4, L},
        {0x11a00, 0x11a00, L},
        {0x11a01, 0x11a06, NSM},
        {0x11a07, 0x11a08, L},
        {0x11a09, 0x11a0a, NSM},
        {0x11a0b, 0x11a32, L},
        {0x11a33, 0x11a38, NSM},
        {0x11a39, 0x11a3a, L},
        {0x11a3b, 0x11a3e, NSM},
        {0x11a3f, 0x11a46, L},
        {0x11a47, 0x11a47, NSM},
        {0x11a50, 0x11a50, L},
        {0x11a51, 0x11a56, NSM},
        {0x11a57, 0x11a58, L},
        {0x11a59, 0x11a5b, NSM},
        {0x11a5c, 0x11a89, L},
        {0x11a8a, 0x11a96, NSM},
        {0x11a97, 0x11a97, L},
        {0x11a98, 0x11a99, NSM},
        {0x11a9a, 0x11aa2, L},
        {0x11ab0, 0x11af8, L},
        {0x11c00, 0x11c08, L},
        {0x11c0a, 0x11c2f, L},
        {0x11c30, 0x11c36, NSM},
        {0x11c38, 0x11c3d, NSM},
        {0x11c3e, 0x11c45, L},
        {0x11c50, 0x11c6c, L},
        {0x11c70, 0x11c8f, L},
        {0x11c92, 0x11ca7, NSM},
        {0x11ca9, 0x11ca9, L},
        {0x11caa, 0x11cb0, NSM},
        {0x11cb1, 0x11cb1, L},
        {0x11cb2, 0x11cb3, NSM},
        {0x11cb4, 0x11cb4, L},
        {0x11cb5, 0x11cb6, NSM},
        {0x11d00, 0x11d06, L},
        {0x11d08, 0x11d09, L},
        {0x11d0b, 0x11d30, L},
        {0x11d31, 0x11d36, NSM},
        {0x11d3a, 0x11d3a, NSM},
        {0x11d3c, 0x11d3d, NSM},
        {0x11d3f, 0x11d45, NSM},
        {0x11d46, 0x11d46, L},
        {0x11d47, 0x11d47, NSM},
        {0x11d50, 0x11d59, L},
        {0x11d60, 0x11d65, L},
        {0x11d67, 0x11d68, L},
        {0x11d6a, 0x11d8e, L},
        {0x11d90, 0x11d91, NSM},
        {0x11d93, 0x11d94, L},
        {0x11d95, 0x11d95, NSM},
        {0x11d96, 0x11d96, L},
        {0x11d97, 0x11d97, NSM},
        {0x11d98, 0x11d98, L},
        {0x11da0, 0x11da9, L},
        {0x11ee0, 0x11ef2, L},
        {0x11ef3, 0x11ef4, NSM},
        {0x11ef5, 0x11ef8, L},
        {0x11fb0, 0x11fb0, L},
        {0x11fc0, 0x11fd4, L},
        {0x11fdd, 0x11fe0, ET},
        {0x11fff, 0x12399, L},
        {0x12400, 0x1246e, L},
        {0x12470, 0x12474, L},
        {0x12480, 0x12543, L},
        {0x12f90, 0x12ff2, L},
        {0x13000, 0x1342e, L},
        {0x13430, 0x13438, L},
        {0x14400, 0x14646, L},
        {0x16800, 0x16a38, L},
        {0x16a40, 0x16a5e, L},
        {0x16a60, 0x16a69, L},
        {0x16a6e, 0x16abe, L},
        {0x16ac0, 0x16ac9, L},
        {0x16ad0, 0x16aed, L},
        {0x16af0, 0x16af4, NSM},
        {0x16af5, 0x16af5, L},
        {0x16b00, 0x16b2f, L},
        {0x16b30, 0x16b36, NSM},
        {0x16b37, 0x16b45, L},
        {0x16b50, 0x16b59, L},
        {0x16b5b, 0x16b61, L},
        {0x16b63, 0x16b77, L},
        {0x16b7d, 0x16b8f, L},
        {0x16e40, 0x16e9a, L},
        {0x16f00, 0x16f4a, L},
        {0x16f4f, 0x16f4f, NSM},
        {0x16f50, 0x16f87, L},
        {0x16f8f, 0x16f92, NSM},
        {0x16f93, 0x16f9f, L},
        {0x16fe0, 0x16fe1, L},
        {0x16fe3, 0x16fe3, L},
        {0x16fe4, 0x16fe4, NSM},
        {0x16ff0, 0x16ff1, L},
        {0x17000, 0x187f7, L},
        {0x18800, 0x18cd5, L},
        {0x18d00, 0x18d08, L},
        {0x1aff0, 0x1aff3, L},
        {0x1aff5, 0x1affb, L},
        {0x1affd, 0x1affe, L},
        {0x1b000, 0x1b122, L},
        {0x1b150, 0x1b152, L},
        {0x1b164, 0x1b167, L},
        {0x1b170, 0x1b2fb, L},
        {0x1bc00, 0x1bc6a, L},
        {0x1bc70, 0x1bc7c, L},
        {0x1bc80, 0x1bc88, L},
        {0x1bc90, 0x1bc99, L},
        {0x1bc9c, 0x1bc9c, L},
        {0x1bc9d, 0x1bc9e, NSM},
        {0x1bc9f, 0x1bc9f, L},
        {0x1bca0, 0x1bca3, BN},
        {0x1cf00, 0x1cf2d, NSM},
        {0x1cf30, 0x1cf46, NSM},
        {0x1cf50, 0x1cfc3, L},
        {0x1d000, 0x1d0f5, L},
        {0x1d100, 0x1d126, L},
        {0x1d129, 0x1d166, L},
        {0x1d167, 0x1d169, NSM},
        {0x1d16a, 0x1d172, L},
        {0x1d173, 0x1d17a, BN},
        {0x1d17b, 0x1d182, NSM},
        {0x1d183, 0x1d184, L},
        {0x1d185, 0x1d18b, NSM},
        {0x1d18c, 0x1d1a9, L},
        {0x1d1aa, 0x1d1ad, NSM},
        {0x1d1ae, 0x1d1e8, L},
        {0x1d242, 0x1d244, NSM},
        {0x1d2e0, 0x1d2f3, L},
        {0x1d360, 0x1d378, L},
        {0x1d400, 0x1d454, L},
        {0x1d456, 0x1d49c, L},
        {0x1d49e, 0x1d49f, L},
        {0x1d4a2, 0x1d4a2, L},
        {0x1d4a5, 0x1d4a6, L},
        {0x1d4a9, 0x1d4ac, L},
        {0x1d4ae, 0x1d4b9, L},
        {0x1d4bb, 0x1d4bb, L},
        {0x1d4bd, 0x1d4c3, L},
        {0x1d4c5, 0x1d505, L},
        {0x1d507, 0x1d50a, L},
        {0x1d50d, 0x1d514, L},
        {0x1d516, 0x1d51c, L},
        {0x1d51e, 0x1d539, L},
        {0x1d53b, 0x1d53e, L},
        {0x1d540, 0x1d544, L},
        {0x1d546, 0x1d546, L},
        {0x1d54a, 0x1d550, L},
        {0x1d552, 0x1d6a5, L},
        {0x1d6a8, 0x1d6da, L},
        {0x1d6dc, 0x1d714, L},
        {0x1d716, 0x1d74e, L},
        {0x1d750, 0x1d788, L},
        {0x1d78a, 0x1d7c2, L},
        {0x1d7c4, 0x1d7cb, L},
        {0x1d7ce, 0x1d7ff, EN},
        {0x1d800, 0x1d9ff, L},
        {0x1da00, 0x1da36, NSM},
        {0x1da37, 0x1da3a, L},
        {0x1da3b, 0x1da6c, NSM},
        {0x1da6d, 0x1da74, L},
        {0x1da75, 0x1da75, NSM},
        {0x1da76, 0x1da83, L},
        {0x1da84, 0x1da84, NSM},
        {0x1da85, 0x1da8b, L},
        {0x1da9b, 0x1da9f, NSM},
        {0x1daa1, 0x1daaf, NSM},
        {0x1df00, 0x1df1e, L},
        {0x1e000, 0x1e006, NSM},
        {0x1e008, 0x1e018, NSM},
        {0x1e01b, 0x1e021, NSM},
        {0x1e023, 0x1e024, NSM},
        {0x1e026, 0x1e02a, NSM},
        {0x1e100, 0x1e12c, L},
        {0x1e130, 0x1e136, NSM},
        {0x1e137, 0x1e13d, L},
        {0x1e140, 0x1e149, L},
        {0x1e14e, 0x1e14f, L},
        {0x1e290, 0x1e2ad, L},
        {0x1e2ae, 0x1e2ae, NSM},
        {0x1e2c0, 0x1e2eb, L},
        {0x1e2ec, 0x1e2ef, NSM},
        {0x1e2f0, 0x1e2f9, L},
        {0x1e2ff, 0x1e2ff, ET},
        {0x1e7e0, 0x1e7e6, L},
        {0x1e7e8, 0x1e7eb, L},
        {0x1e7ed, 0x1e7ee, L},
        {0x1e7f0, 0x1e7fe, L},
        {0x1e800, 0x1e8c4, R},
        {0x1e8c7, 0x1e8cf, R},
        {0x1e8d0, 0x1e8d6, NSM},
        {0x1e900, 0x1e943, R},
        {0x1e944, 0x1e94a, NSM},
        {0x1e94b, 0x1e94b, R},
        {0x1e950, 0x1e959, R},
        {0x1e95e, 0x1e95f, R},
        {0x1ec71, 0x1ecb4, AL},
        {0x1ed01, 0x1ed3d, AL},
        {0x1ee00, 0x1ee03, AL},
        {0x1ee05, 0x1ee1f, AL},
        {0x1ee21, 0x1ee22, AL},
        {0x1ee24, 0x1ee24, AL},
        {0x1ee27, 0x1ee27, AL},
        {0x1ee29, 0x1ee32, AL},
        {0x1ee34, 0x1ee37, AL},
        {0x1ee39, 0x1ee39, AL},
        {0x1ee3b, 0x1ee3b, AL},
        {0x1ee42, 0x1ee42, AL},
        {0x1ee47, 0x1ee47, AL},
        {0x1ee49, 0x1ee49, AL},
        {0x1ee4b, 0x1ee4b, AL},
        {0x1ee4d, 0x1ee4f, AL},
        {0x1ee51, 0x1ee52, AL},
        {0x1ee54, 0x1ee54, AL},
        {0x1ee57, 0x1ee57, AL},
        {0x1ee59, 0x1ee59, AL},
        {0x1ee5b, 0x1ee5b, AL},
        {0x1ee5d, 0x1ee5d, AL},
        {0x1ee5f, 0x1ee5f, AL},
        {0x1ee61, 0x1ee62, AL},
        {0x1ee64, 0x1ee64, AL},
        {0x1ee67, 0x1ee6a, AL},
        {0x1ee6c, 0x1ee72, AL},
        {0x1ee74, 0x1ee77, AL},
        {0x1ee79, 0x1ee7c, AL},
        {0x1ee7e, 0x1ee7e, AL},
        {0x1ee80, 0x1ee89, AL},
        {0x1ee8b, 0x1ee9b, AL},
        {0x1eea1, 0x1eea3, AL},
        {0x1eea5, 0x1eea9, AL},
        {0x1eeab, 0x1eebb, AL},
        {0x1f100, 0x1f10a, EN},
        {0x1f110, 0x1f12e, L},
        {0x1f130, 0x1f169, L},
        {0x1f170, 0x1f1ac, L},
        {0x1f1e6, 0x1f202, L},
        {0x1f210, 0x1f23b, L},
        {0x1f240, 0x1f248, L},
        {0x1f250, 0x1f251, L},
        {0x1fbf0, 0x1fbf9, EN},
        {0x20000, 0x2a6df, L},
        {0x2a700, 0x2b738, L},
        {0x2b740, 0x2b81d, L},
        {0x2b820, 0x2cea1, L},
        {0x2ceb0, 0x2ebe0, L},
        {0x2f800, 0x2fa1d, L},
        {0x30000, 0x3134a, L},
        {0xe0001, 0xe0001, BN},
        {0xe0020, 0xe007f, BN},
        {0xe0100, 0xe01ef, NSM},
        {0xf0000, 0xffffd, L},
        {0x100000, 0x10fffd, L},
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

 * The data table in this function is constructed from the Unicode
 * Character Database version 14.0.0, downloadable from unicode.org at
 * the URL
 *
 *     https://www.unicode.org/Public/14.0.0/ucd/
 *
 * by the following fragment of Perl:

perl -e '
  while (<<>>) {
    chomp; s{\s}{}g; s{#.*$}{}; next unless /./;
    @_ = split /;/, $_;
    $src = hex $_[0]; $dst = hex $_[1];
    $m{$src}=$dst; $m{$dst}=$src;
  }
  for $src (sort {$a <=> $b} keys %m) {
    printf "        {0x%04x, 0x%04x},\n", $src, $m{$src};
  }
' BidiMirroring.txt

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
        {0x0028, 0x0029},
        {0x0029, 0x0028},
        {0x003c, 0x003e},
        {0x003e, 0x003c},
        {0x005b, 0x005d},
        {0x005d, 0x005b},
        {0x007b, 0x007d},
        {0x007d, 0x007b},
        {0x00ab, 0x00bb},
        {0x00bb, 0x00ab},
        {0x0f3a, 0x0f3b},
        {0x0f3b, 0x0f3a},
        {0x0f3c, 0x0f3d},
        {0x0f3d, 0x0f3c},
        {0x169b, 0x169c},
        {0x169c, 0x169b},
        {0x2039, 0x203a},
        {0x203a, 0x2039},
        {0x2045, 0x2046},
        {0x2046, 0x2045},
        {0x207d, 0x207e},
        {0x207e, 0x207d},
        {0x208d, 0x208e},
        {0x208e, 0x208d},
        {0x2208, 0x220b},
        {0x2209, 0x220c},
        {0x220a, 0x220d},
        {0x220b, 0x2208},
        {0x220c, 0x2209},
        {0x220d, 0x220a},
        {0x2215, 0x29f5},
        {0x221f, 0x2bfe},
        {0x2220, 0x29a3},
        {0x2221, 0x299b},
        {0x2222, 0x29a0},
        {0x2224, 0x2aee},
        {0x223c, 0x223d},
        {0x223d, 0x223c},
        {0x2243, 0x22cd},
        {0x2245, 0x224c},
        {0x224c, 0x2245},
        {0x2252, 0x2253},
        {0x2253, 0x2252},
        {0x2254, 0x2255},
        {0x2255, 0x2254},
        {0x2264, 0x2265},
        {0x2265, 0x2264},
        {0x2266, 0x2267},
        {0x2267, 0x2266},
        {0x2268, 0x2269},
        {0x2269, 0x2268},
        {0x226a, 0x226b},
        {0x226b, 0x226a},
        {0x226e, 0x226f},
        {0x226f, 0x226e},
        {0x2270, 0x2271},
        {0x2271, 0x2270},
        {0x2272, 0x2273},
        {0x2273, 0x2272},
        {0x2274, 0x2275},
        {0x2275, 0x2274},
        {0x2276, 0x2277},
        {0x2277, 0x2276},
        {0x2278, 0x2279},
        {0x2279, 0x2278},
        {0x227a, 0x227b},
        {0x227b, 0x227a},
        {0x227c, 0x227d},
        {0x227d, 0x227c},
        {0x227e, 0x227f},
        {0x227f, 0x227e},
        {0x2280, 0x2281},
        {0x2281, 0x2280},
        {0x2282, 0x2283},
        {0x2283, 0x2282},
        {0x2284, 0x2285},
        {0x2285, 0x2284},
        {0x2286, 0x2287},
        {0x2287, 0x2286},
        {0x2288, 0x2289},
        {0x2289, 0x2288},
        {0x228a, 0x228b},
        {0x228b, 0x228a},
        {0x228f, 0x2290},
        {0x2290, 0x228f},
        {0x2291, 0x2292},
        {0x2292, 0x2291},
        {0x2298, 0x29b8},
        {0x22a2, 0x22a3},
        {0x22a3, 0x22a2},
        {0x22a6, 0x2ade},
        {0x22a8, 0x2ae4},
        {0x22a9, 0x2ae3},
        {0x22ab, 0x2ae5},
        {0x22b0, 0x22b1},
        {0x22b1, 0x22b0},
        {0x22b2, 0x22b3},
        {0x22b3, 0x22b2},
        {0x22b4, 0x22b5},
        {0x22b5, 0x22b4},
        {0x22b6, 0x22b7},
        {0x22b7, 0x22b6},
        {0x22b8, 0x27dc},
        {0x22c9, 0x22ca},
        {0x22ca, 0x22c9},
        {0x22cb, 0x22cc},
        {0x22cc, 0x22cb},
        {0x22cd, 0x2243},
        {0x22d0, 0x22d1},
        {0x22d1, 0x22d0},
        {0x22d6, 0x22d7},
        {0x22d7, 0x22d6},
        {0x22d8, 0x22d9},
        {0x22d9, 0x22d8},
        {0x22da, 0x22db},
        {0x22db, 0x22da},
        {0x22dc, 0x22dd},
        {0x22dd, 0x22dc},
        {0x22de, 0x22df},
        {0x22df, 0x22de},
        {0x22e0, 0x22e1},
        {0x22e1, 0x22e0},
        {0x22e2, 0x22e3},
        {0x22e3, 0x22e2},
        {0x22e4, 0x22e5},
        {0x22e5, 0x22e4},
        {0x22e6, 0x22e7},
        {0x22e7, 0x22e6},
        {0x22e8, 0x22e9},
        {0x22e9, 0x22e8},
        {0x22ea, 0x22eb},
        {0x22eb, 0x22ea},
        {0x22ec, 0x22ed},
        {0x22ed, 0x22ec},
        {0x22f0, 0x22f1},
        {0x22f1, 0x22f0},
        {0x22f2, 0x22fa},
        {0x22f3, 0x22fb},
        {0x22f4, 0x22fc},
        {0x22f6, 0x22fd},
        {0x22f7, 0x22fe},
        {0x22fa, 0x22f2},
        {0x22fb, 0x22f3},
        {0x22fc, 0x22f4},
        {0x22fd, 0x22f6},
        {0x22fe, 0x22f7},
        {0x2308, 0x2309},
        {0x2309, 0x2308},
        {0x230a, 0x230b},
        {0x230b, 0x230a},
        {0x2329, 0x232a},
        {0x232a, 0x2329},
        {0x2768, 0x2769},
        {0x2769, 0x2768},
        {0x276a, 0x276b},
        {0x276b, 0x276a},
        {0x276c, 0x276d},
        {0x276d, 0x276c},
        {0x276e, 0x276f},
        {0x276f, 0x276e},
        {0x2770, 0x2771},
        {0x2771, 0x2770},
        {0x2772, 0x2773},
        {0x2773, 0x2772},
        {0x2774, 0x2775},
        {0x2775, 0x2774},
        {0x27c3, 0x27c4},
        {0x27c4, 0x27c3},
        {0x27c5, 0x27c6},
        {0x27c6, 0x27c5},
        {0x27c8, 0x27c9},
        {0x27c9, 0x27c8},
        {0x27cb, 0x27cd},
        {0x27cd, 0x27cb},
        {0x27d5, 0x27d6},
        {0x27d6, 0x27d5},
        {0x27dc, 0x22b8},
        {0x27dd, 0x27de},
        {0x27de, 0x27dd},
        {0x27e2, 0x27e3},
        {0x27e3, 0x27e2},
        {0x27e4, 0x27e5},
        {0x27e5, 0x27e4},
        {0x27e6, 0x27e7},
        {0x27e7, 0x27e6},
        {0x27e8, 0x27e9},
        {0x27e9, 0x27e8},
        {0x27ea, 0x27eb},
        {0x27eb, 0x27ea},
        {0x27ec, 0x27ed},
        {0x27ed, 0x27ec},
        {0x27ee, 0x27ef},
        {0x27ef, 0x27ee},
        {0x2983, 0x2984},
        {0x2984, 0x2983},
        {0x2985, 0x2986},
        {0x2986, 0x2985},
        {0x2987, 0x2988},
        {0x2988, 0x2987},
        {0x2989, 0x298a},
        {0x298a, 0x2989},
        {0x298b, 0x298c},
        {0x298c, 0x298b},
        {0x298d, 0x2990},
        {0x298e, 0x298f},
        {0x298f, 0x298e},
        {0x2990, 0x298d},
        {0x2991, 0x2992},
        {0x2992, 0x2991},
        {0x2993, 0x2994},
        {0x2994, 0x2993},
        {0x2995, 0x2996},
        {0x2996, 0x2995},
        {0x2997, 0x2998},
        {0x2998, 0x2997},
        {0x299b, 0x2221},
        {0x29a0, 0x2222},
        {0x29a3, 0x2220},
        {0x29a4, 0x29a5},
        {0x29a5, 0x29a4},
        {0x29a8, 0x29a9},
        {0x29a9, 0x29a8},
        {0x29aa, 0x29ab},
        {0x29ab, 0x29aa},
        {0x29ac, 0x29ad},
        {0x29ad, 0x29ac},
        {0x29ae, 0x29af},
        {0x29af, 0x29ae},
        {0x29b8, 0x2298},
        {0x29c0, 0x29c1},
        {0x29c1, 0x29c0},
        {0x29c4, 0x29c5},
        {0x29c5, 0x29c4},
        {0x29cf, 0x29d0},
        {0x29d0, 0x29cf},
        {0x29d1, 0x29d2},
        {0x29d2, 0x29d1},
        {0x29d4, 0x29d5},
        {0x29d5, 0x29d4},
        {0x29d8, 0x29d9},
        {0x29d9, 0x29d8},
        {0x29da, 0x29db},
        {0x29db, 0x29da},
        {0x29e8, 0x29e9},
        {0x29e9, 0x29e8},
        {0x29f5, 0x2215},
        {0x29f8, 0x29f9},
        {0x29f9, 0x29f8},
        {0x29fc, 0x29fd},
        {0x29fd, 0x29fc},
        {0x2a2b, 0x2a2c},
        {0x2a2c, 0x2a2b},
        {0x2a2d, 0x2a2e},
        {0x2a2e, 0x2a2d},
        {0x2a34, 0x2a35},
        {0x2a35, 0x2a34},
        {0x2a3c, 0x2a3d},
        {0x2a3d, 0x2a3c},
        {0x2a64, 0x2a65},
        {0x2a65, 0x2a64},
        {0x2a79, 0x2a7a},
        {0x2a7a, 0x2a79},
        {0x2a7b, 0x2a7c},
        {0x2a7c, 0x2a7b},
        {0x2a7d, 0x2a7e},
        {0x2a7e, 0x2a7d},
        {0x2a7f, 0x2a80},
        {0x2a80, 0x2a7f},
        {0x2a81, 0x2a82},
        {0x2a82, 0x2a81},
        {0x2a83, 0x2a84},
        {0x2a84, 0x2a83},
        {0x2a85, 0x2a86},
        {0x2a86, 0x2a85},
        {0x2a87, 0x2a88},
        {0x2a88, 0x2a87},
        {0x2a89, 0x2a8a},
        {0x2a8a, 0x2a89},
        {0x2a8b, 0x2a8c},
        {0x2a8c, 0x2a8b},
        {0x2a8d, 0x2a8e},
        {0x2a8e, 0x2a8d},
        {0x2a8f, 0x2a90},
        {0x2a90, 0x2a8f},
        {0x2a91, 0x2a92},
        {0x2a92, 0x2a91},
        {0x2a93, 0x2a94},
        {0x2a94, 0x2a93},
        {0x2a95, 0x2a96},
        {0x2a96, 0x2a95},
        {0x2a97, 0x2a98},
        {0x2a98, 0x2a97},
        {0x2a99, 0x2a9a},
        {0x2a9a, 0x2a99},
        {0x2a9b, 0x2a9c},
        {0x2a9c, 0x2a9b},
        {0x2a9d, 0x2a9e},
        {0x2a9e, 0x2a9d},
        {0x2a9f, 0x2aa0},
        {0x2aa0, 0x2a9f},
        {0x2aa1, 0x2aa2},
        {0x2aa2, 0x2aa1},
        {0x2aa6, 0x2aa7},
        {0x2aa7, 0x2aa6},
        {0x2aa8, 0x2aa9},
        {0x2aa9, 0x2aa8},
        {0x2aaa, 0x2aab},
        {0x2aab, 0x2aaa},
        {0x2aac, 0x2aad},
        {0x2aad, 0x2aac},
        {0x2aaf, 0x2ab0},
        {0x2ab0, 0x2aaf},
        {0x2ab1, 0x2ab2},
        {0x2ab2, 0x2ab1},
        {0x2ab3, 0x2ab4},
        {0x2ab4, 0x2ab3},
        {0x2ab5, 0x2ab6},
        {0x2ab6, 0x2ab5},
        {0x2ab7, 0x2ab8},
        {0x2ab8, 0x2ab7},
        {0x2ab9, 0x2aba},
        {0x2aba, 0x2ab9},
        {0x2abb, 0x2abc},
        {0x2abc, 0x2abb},
        {0x2abd, 0x2abe},
        {0x2abe, 0x2abd},
        {0x2abf, 0x2ac0},
        {0x2ac0, 0x2abf},
        {0x2ac1, 0x2ac2},
        {0x2ac2, 0x2ac1},
        {0x2ac3, 0x2ac4},
        {0x2ac4, 0x2ac3},
        {0x2ac5, 0x2ac6},
        {0x2ac6, 0x2ac5},
        {0x2ac7, 0x2ac8},
        {0x2ac8, 0x2ac7},
        {0x2ac9, 0x2aca},
        {0x2aca, 0x2ac9},
        {0x2acb, 0x2acc},
        {0x2acc, 0x2acb},
        {0x2acd, 0x2ace},
        {0x2ace, 0x2acd},
        {0x2acf, 0x2ad0},
        {0x2ad0, 0x2acf},
        {0x2ad1, 0x2ad2},
        {0x2ad2, 0x2ad1},
        {0x2ad3, 0x2ad4},
        {0x2ad4, 0x2ad3},
        {0x2ad5, 0x2ad6},
        {0x2ad6, 0x2ad5},
        {0x2ade, 0x22a6},
        {0x2ae3, 0x22a9},
        {0x2ae4, 0x22a8},
        {0x2ae5, 0x22ab},
        {0x2aec, 0x2aed},
        {0x2aed, 0x2aec},
        {0x2aee, 0x2224},
        {0x2af7, 0x2af8},
        {0x2af8, 0x2af7},
        {0x2af9, 0x2afa},
        {0x2afa, 0x2af9},
        {0x2bfe, 0x221f},
        {0x2e02, 0x2e03},
        {0x2e03, 0x2e02},
        {0x2e04, 0x2e05},
        {0x2e05, 0x2e04},
        {0x2e09, 0x2e0a},
        {0x2e0a, 0x2e09},
        {0x2e0c, 0x2e0d},
        {0x2e0d, 0x2e0c},
        {0x2e1c, 0x2e1d},
        {0x2e1d, 0x2e1c},
        {0x2e20, 0x2e21},
        {0x2e21, 0x2e20},
        {0x2e22, 0x2e23},
        {0x2e23, 0x2e22},
        {0x2e24, 0x2e25},
        {0x2e25, 0x2e24},
        {0x2e26, 0x2e27},
        {0x2e27, 0x2e26},
        {0x2e28, 0x2e29},
        {0x2e29, 0x2e28},
        {0x2e55, 0x2e56},
        {0x2e56, 0x2e55},
        {0x2e57, 0x2e58},
        {0x2e58, 0x2e57},
        {0x2e59, 0x2e5a},
        {0x2e5a, 0x2e59},
        {0x2e5b, 0x2e5c},
        {0x2e5c, 0x2e5b},
        {0x3008, 0x3009},
        {0x3009, 0x3008},
        {0x300a, 0x300b},
        {0x300b, 0x300a},
        {0x300c, 0x300d},
        {0x300d, 0x300c},
        {0x300e, 0x300f},
        {0x300f, 0x300e},
        {0x3010, 0x3011},
        {0x3011, 0x3010},
        {0x3014, 0x3015},
        {0x3015, 0x3014},
        {0x3016, 0x3017},
        {0x3017, 0x3016},
        {0x3018, 0x3019},
        {0x3019, 0x3018},
        {0x301a, 0x301b},
        {0x301b, 0x301a},
        {0xfe59, 0xfe5a},
        {0xfe5a, 0xfe59},
        {0xfe5b, 0xfe5c},
        {0xfe5c, 0xfe5b},
        {0xfe5d, 0xfe5e},
        {0xfe5e, 0xfe5d},
        {0xfe64, 0xfe65},
        {0xfe65, 0xfe64},
        {0xff08, 0xff09},
        {0xff09, 0xff08},
        {0xff1c, 0xff1e},
        {0xff1e, 0xff1c},
        {0xff3b, 0xff3d},
        {0xff3d, 0xff3b},
        {0xff5b, 0xff5d},
        {0xff5d, 0xff5b},
        {0xff5f, 0xff60},
        {0xff60, 0xff5f},
        {0xff62, 0xff63},
        {0xff63, 0xff62},
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
 *
 * The data table in this function is constructed from the Unicode
 * Character Database version 14.0.0, downloadable from unicode.org at
 * the URL
 *
 *     https://www.unicode.org/Public/14.0.0/ucd/
 *
 * by the following fragment of Perl:

perl -e '
  open BIDIBRACKETS, "<", $ARGV[0] or die;
  while (<BIDIBRACKETS>) {
    chomp; s{\s}{}g; s{#.*$}{}; next unless /./;
    @_ = split /;/, $_;
    $src = hex $_[0]; $dst = hex $_[1]; $kind = $_[2];
    $m{$src}=[$kind, $dst];
  }
  open UNICODEDATA, "<", $ARGV[1] or die;
  while (<UNICODEDATA>) {
    chomp; @_ = split /;/, $_;
    $src = hex $_[0]; next unless defined $m{$src};
    if ($_[5] =~ /^[0-9a-f]+$/i) {
      $equiv = hex $_[5];
      $e{$src} = $equiv;
      $e{$equiv} = $src;
    }
  }
  for $src (sort {$a <=> $b} keys %m) {
    ($kind, $dst) = @{$m{$src}};
    $equiv = 0 + $e{$dst};
    printf "        {0x%04x, {0x%04x, 0x%04x, %s}},\n", $src, $dst, $equiv,
       $kind eq "c" ? "BT_CLOSE" : "BT_OPEN";
  }
' BidiBrackets.txt UnicodeData.txt

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
        {0x0028, {0x0029, 0x0000, BT_OPEN}},
        {0x0029, {0x0028, 0x0000, BT_CLOSE}},
        {0x005b, {0x005d, 0x0000, BT_OPEN}},
        {0x005d, {0x005b, 0x0000, BT_CLOSE}},
        {0x007b, {0x007d, 0x0000, BT_OPEN}},
        {0x007d, {0x007b, 0x0000, BT_CLOSE}},
        {0x0f3a, {0x0f3b, 0x0000, BT_OPEN}},
        {0x0f3b, {0x0f3a, 0x0000, BT_CLOSE}},
        {0x0f3c, {0x0f3d, 0x0000, BT_OPEN}},
        {0x0f3d, {0x0f3c, 0x0000, BT_CLOSE}},
        {0x169b, {0x169c, 0x0000, BT_OPEN}},
        {0x169c, {0x169b, 0x0000, BT_CLOSE}},
        {0x2045, {0x2046, 0x0000, BT_OPEN}},
        {0x2046, {0x2045, 0x0000, BT_CLOSE}},
        {0x207d, {0x207e, 0x0000, BT_OPEN}},
        {0x207e, {0x207d, 0x0000, BT_CLOSE}},
        {0x208d, {0x208e, 0x0000, BT_OPEN}},
        {0x208e, {0x208d, 0x0000, BT_CLOSE}},
        {0x2308, {0x2309, 0x0000, BT_OPEN}},
        {0x2309, {0x2308, 0x0000, BT_CLOSE}},
        {0x230a, {0x230b, 0x0000, BT_OPEN}},
        {0x230b, {0x230a, 0x0000, BT_CLOSE}},
        {0x2329, {0x232a, 0x3009, BT_OPEN}},
        {0x232a, {0x2329, 0x3008, BT_CLOSE}},
        {0x2768, {0x2769, 0x0000, BT_OPEN}},
        {0x2769, {0x2768, 0x0000, BT_CLOSE}},
        {0x276a, {0x276b, 0x0000, BT_OPEN}},
        {0x276b, {0x276a, 0x0000, BT_CLOSE}},
        {0x276c, {0x276d, 0x0000, BT_OPEN}},
        {0x276d, {0x276c, 0x0000, BT_CLOSE}},
        {0x276e, {0x276f, 0x0000, BT_OPEN}},
        {0x276f, {0x276e, 0x0000, BT_CLOSE}},
        {0x2770, {0x2771, 0x0000, BT_OPEN}},
        {0x2771, {0x2770, 0x0000, BT_CLOSE}},
        {0x2772, {0x2773, 0x0000, BT_OPEN}},
        {0x2773, {0x2772, 0x0000, BT_CLOSE}},
        {0x2774, {0x2775, 0x0000, BT_OPEN}},
        {0x2775, {0x2774, 0x0000, BT_CLOSE}},
        {0x27c5, {0x27c6, 0x0000, BT_OPEN}},
        {0x27c6, {0x27c5, 0x0000, BT_CLOSE}},
        {0x27e6, {0x27e7, 0x0000, BT_OPEN}},
        {0x27e7, {0x27e6, 0x0000, BT_CLOSE}},
        {0x27e8, {0x27e9, 0x0000, BT_OPEN}},
        {0x27e9, {0x27e8, 0x0000, BT_CLOSE}},
        {0x27ea, {0x27eb, 0x0000, BT_OPEN}},
        {0x27eb, {0x27ea, 0x0000, BT_CLOSE}},
        {0x27ec, {0x27ed, 0x0000, BT_OPEN}},
        {0x27ed, {0x27ec, 0x0000, BT_CLOSE}},
        {0x27ee, {0x27ef, 0x0000, BT_OPEN}},
        {0x27ef, {0x27ee, 0x0000, BT_CLOSE}},
        {0x2983, {0x2984, 0x0000, BT_OPEN}},
        {0x2984, {0x2983, 0x0000, BT_CLOSE}},
        {0x2985, {0x2986, 0x0000, BT_OPEN}},
        {0x2986, {0x2985, 0x0000, BT_CLOSE}},
        {0x2987, {0x2988, 0x0000, BT_OPEN}},
        {0x2988, {0x2987, 0x0000, BT_CLOSE}},
        {0x2989, {0x298a, 0x0000, BT_OPEN}},
        {0x298a, {0x2989, 0x0000, BT_CLOSE}},
        {0x298b, {0x298c, 0x0000, BT_OPEN}},
        {0x298c, {0x298b, 0x0000, BT_CLOSE}},
        {0x298d, {0x2990, 0x0000, BT_OPEN}},
        {0x298e, {0x298f, 0x0000, BT_CLOSE}},
        {0x298f, {0x298e, 0x0000, BT_OPEN}},
        {0x2990, {0x298d, 0x0000, BT_CLOSE}},
        {0x2991, {0x2992, 0x0000, BT_OPEN}},
        {0x2992, {0x2991, 0x0000, BT_CLOSE}},
        {0x2993, {0x2994, 0x0000, BT_OPEN}},
        {0x2994, {0x2993, 0x0000, BT_CLOSE}},
        {0x2995, {0x2996, 0x0000, BT_OPEN}},
        {0x2996, {0x2995, 0x0000, BT_CLOSE}},
        {0x2997, {0x2998, 0x0000, BT_OPEN}},
        {0x2998, {0x2997, 0x0000, BT_CLOSE}},
        {0x29d8, {0x29d9, 0x0000, BT_OPEN}},
        {0x29d9, {0x29d8, 0x0000, BT_CLOSE}},
        {0x29da, {0x29db, 0x0000, BT_OPEN}},
        {0x29db, {0x29da, 0x0000, BT_CLOSE}},
        {0x29fc, {0x29fd, 0x0000, BT_OPEN}},
        {0x29fd, {0x29fc, 0x0000, BT_CLOSE}},
        {0x2e22, {0x2e23, 0x0000, BT_OPEN}},
        {0x2e23, {0x2e22, 0x0000, BT_CLOSE}},
        {0x2e24, {0x2e25, 0x0000, BT_OPEN}},
        {0x2e25, {0x2e24, 0x0000, BT_CLOSE}},
        {0x2e26, {0x2e27, 0x0000, BT_OPEN}},
        {0x2e27, {0x2e26, 0x0000, BT_CLOSE}},
        {0x2e28, {0x2e29, 0x0000, BT_OPEN}},
        {0x2e29, {0x2e28, 0x0000, BT_CLOSE}},
        {0x2e55, {0x2e56, 0x0000, BT_OPEN}},
        {0x2e56, {0x2e55, 0x0000, BT_CLOSE}},
        {0x2e57, {0x2e58, 0x0000, BT_OPEN}},
        {0x2e58, {0x2e57, 0x0000, BT_CLOSE}},
        {0x2e59, {0x2e5a, 0x0000, BT_OPEN}},
        {0x2e5a, {0x2e59, 0x0000, BT_CLOSE}},
        {0x2e5b, {0x2e5c, 0x0000, BT_OPEN}},
        {0x2e5c, {0x2e5b, 0x0000, BT_CLOSE}},
        {0x3008, {0x3009, 0x232a, BT_OPEN}},
        {0x3009, {0x3008, 0x2329, BT_CLOSE}},
        {0x300a, {0x300b, 0x0000, BT_OPEN}},
        {0x300b, {0x300a, 0x0000, BT_CLOSE}},
        {0x300c, {0x300d, 0x0000, BT_OPEN}},
        {0x300d, {0x300c, 0x0000, BT_CLOSE}},
        {0x300e, {0x300f, 0x0000, BT_OPEN}},
        {0x300f, {0x300e, 0x0000, BT_CLOSE}},
        {0x3010, {0x3011, 0x0000, BT_OPEN}},
        {0x3011, {0x3010, 0x0000, BT_CLOSE}},
        {0x3014, {0x3015, 0x0000, BT_OPEN}},
        {0x3015, {0x3014, 0x0000, BT_CLOSE}},
        {0x3016, {0x3017, 0x0000, BT_OPEN}},
        {0x3017, {0x3016, 0x0000, BT_CLOSE}},
        {0x3018, {0x3019, 0x0000, BT_OPEN}},
        {0x3019, {0x3018, 0x0000, BT_CLOSE}},
        {0x301a, {0x301b, 0x0000, BT_OPEN}},
        {0x301b, {0x301a, 0x0000, BT_CLOSE}},
        {0xfe59, {0xfe5a, 0x0000, BT_OPEN}},
        {0xfe5a, {0xfe59, 0x0000, BT_CLOSE}},
        {0xfe5b, {0xfe5c, 0x0000, BT_OPEN}},
        {0xfe5c, {0xfe5b, 0x0000, BT_CLOSE}},
        {0xfe5d, {0xfe5e, 0x0000, BT_OPEN}},
        {0xfe5e, {0xfe5d, 0x0000, BT_CLOSE}},
        {0xff08, {0xff09, 0x0000, BT_OPEN}},
        {0xff09, {0xff08, 0x0000, BT_CLOSE}},
        {0xff3b, {0xff3d, 0x0000, BT_OPEN}},
        {0xff3d, {0xff3b, 0x0000, BT_CLOSE}},
        {0xff5b, {0xff5d, 0x0000, BT_OPEN}},
        {0xff5d, {0xff5b, 0x0000, BT_CLOSE}},
        {0xff5f, {0xff60, 0x0000, BT_OPEN}},
        {0xff60, {0xff5f, 0x0000, BT_CLOSE}},
        {0xff62, {0xff63, 0x0000, BT_OPEN}},
        {0xff63, {0xff62, 0x0000, BT_CLOSE}},
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
 * The Main Bidi Function, and the only function that should be used
 * by the outside world.
 *
 * text: a buffer of size textlen containing text to apply the
 * Bidirectional algorithm to.
 */
void do_bidi_new(BidiContext *ctx, bidi_char *text, size_t textlen)
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
    assert(textlen >= 0);
    do_bidi_new(ctx, text, textlen);
#endif
}
