/************************************************************************
 * $Id$
 *
 * ------------
 * Description:
 * ------------
 * This is an implemention of Unicode's Bidirectional Algorithm
 * (known as UAX #9).
 *
 *   http://www.unicode.org/reports/tr9/
 * 
 * Author: Ahmad Khalifa
 *
 * -----------------
 * Revision Details:    (Updated by Revision Control System)
 * -----------------
 *  $Date$
 *  $Author$
 *  $Revision$
 *
 * (www.arabeyes.org - under MIT license)
 *
 ************************************************************************/

/*
 * TODO:
 * =====
 * - work almost finished
 * - Shaping Table to be expanded to include the whole range.
 * - Ligature handling
 */

#include <stdlib.h>	/* definition of wchar_t*/

#define LMASK	0x3F	/* Embedding Level mask */
#define OMASK	0xC0	/* Override mask */
#define OISL	0x80	/* Override is L */
#define OISR	0x40	/* Override is R */

/* Shaping Helpers */
#define STYPE(xh) (((xh >= SHAPE_FIRST) && (xh <= SHAPE_LAST)) ? \
shapetypes[xh-SHAPE_FIRST].type : SU) /*))*/
#define SISOLATED(xh) (shapetypes[xh-SHAPE_FIRST].form_b)
#define SFINAL(xh) xh+1
#define SINITIAL(xh) xh+2
#define SMEDIAL(ch) ch+3

typedef struct bidi_char {
    wchar_t origwc, wc;
    unsigned short index;
} bidi_char;

/* function declarations */
void flipThisRun(bidi_char *from, unsigned char* level, int max, int count);
int findIndexOfRun(unsigned char* level , int start, int count, int tlevel);
unsigned char getType(wchar_t ch);
unsigned char setOverrideBits(unsigned char level, unsigned char override);
int getPreviousLevel(unsigned char* level, int from);
unsigned char leastGreaterOdd(unsigned char x);
unsigned char leastGreaterEven(unsigned char x);
unsigned char getRLE(wchar_t ch);
int do_shape(bidi_char *line, bidi_char *to, int count);
int do_bidi(bidi_char *line, int count);
void doMirror(wchar_t* ch);

/* character types */
enum
{
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
   ON,
};

/* Shaping Types */
enum
{
	SL, /* Left-Joining, doesnt exist in U+0600 - U+06FF */
	SR, /* Right-Joining, ie has Isolated, Final */
	SD, /* Dual-Joining, ie has Isolated, Final, Initial, Medial */
	SU, /* Non-Joining */
	SC  /* Join-Causing, like U+0640 (TATWEEL) */
};

typedef struct{
	char type;
	wchar_t form_b;
} shape_node;

/* Kept near the actual table, for verification. */
#define SHAPE_FIRST 0x621
#define SHAPE_LAST 0x64A

const shape_node shapetypes[] = {
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
};

/*
 * This describes the data byte and its frequency  
 */
typedef struct
{
   unsigned char d;
   unsigned char f;
}RLENode;


/* This is an array of RLENodes, which is the
 * Compressed unicode types table
 */
const RLENode RLE_table[] =
{
    { BN,   9}, {  S,   1}, {  B,   1}, {  S,   1}, { WS,   1},
    {  B,   1}, { BN,  14}, {  B,   3}, {  S,   1}, { WS,   1},
    { ON,   2}, { ET,   3}, { ON,   5}, { ET,   1}, { CS,   1},
    { ET,   1}, { CS,   1}, { ES,   1}, { EN,  10}, { CS,   1},
    { ON,   6}, {  L,  26}, { ON,   6}, {  L,  26}, { ON,   4},
    { BN,   6}, {  B,   1}, { BN,  26}, { CS,   1}, { ON,   1},
    { ET,   4}, { ON,   4}, {  L,   1}, { ON,   5}, { ET,   2},
    { EN,   2}, { ON,   1}, {  L,   1}, { ON,   3}, { EN,   1},
    {  L,   1}, { ON,   5}, {  L,  23}, { ON,   1}, {  L,  31},
    { ON,   1}, {  L, 255}, {  L,  42}, { ON,   1}, {  L,  18},
    { ON,  28}, {  L,  94}, { ON,   2}, {  L,   9}, { ON,   2},
    {  L,   7}, { ON,  14}, {  L,   2}, { ON,  14}, {  L,   5},
    { ON,   9}, {  L,   1}, { ON,  17}, {NSM,  80}, { ON,  16},
    {NSM,  16}, { ON,  10}, {  L,   1}, { ON,  11}, {  L,   1},
    { ON,   1}, {  L,   3}, { ON,   1}, {  L,   1}, { ON,   1},
    {  L,  20}, { ON,   1}, {  L,  44}, { ON,   1}, {  L,  38},
    { ON,  10}, {  L, 131}, {NSM,   4}, { ON,   1}, {NSM,   2},
    {  L,  69}, { ON,   1}, {  L,  38}, { ON,   2}, {  L,   2},
    { ON,   6}, {  L,  16}, { ON,  33}, {  L,  38}, { ON,   2},
    {  L,   7}, { ON,   1}, {  L,  39}, { ON,   1}, {  L,   1},
    { ON,   7}, {NSM,  17}, { ON,   1}, {NSM,  23}, { ON,   1},
    {NSM,   3}, {  R,   1}, {NSM,   1}, {  R,   1}, {NSM,   2},
    {  R,   1}, {NSM,   1}, { ON,  11}, {  R,  27}, { ON,   5},
    {  R,   5}, { ON,  23}, { CS,   1}, { ON,  14}, { AL,   1},
    { ON,   3}, { AL,   1}, { ON,   1}, { AL,  26}, { ON,   5},
    { AL,  11}, {NSM,  11}, { ON,  10}, { AN,  10}, { ET,   1},
    { AN,   2}, { AL,   3}, {NSM,   1}, { AL, 101}, {NSM,   7},
    { AL,   1}, {NSM,   7}, { AL,   2}, {NSM,   2}, { ON,   1},
    {NSM,   4}, { ON,   2}, { EN,  10}, { AL,   5}, { ON,   1},
    { AL,  14}, { ON,   1}, { BN,   1}, { AL,   1}, {NSM,   1},
    { AL,  27}, { ON,   3}, {NSM,  27}, { ON,  53}, { AL,  38},
    {NSM,  11}, { AL,   1}, { ON, 255}, { ON,  80}, {NSM,   2},
    {  L,   1}, { ON,   1}, {  L,  53}, { ON,   2}, {NSM,   1},
    {  L,   4}, {NSM,   8}, {  L,   4}, {NSM,   1}, { ON,   2},
    {  L,   1}, {NSM,   4}, { ON,   3}, {  L,  10}, {NSM,   2},
    {  L,  13}, { ON,  16}, {NSM,   1}, {  L,   2}, { ON,   1},
    {  L,   8}, { ON,   2}, {  L,   2}, { ON,   2}, {  L,  22},
    { ON,   1}, {  L,   7}, { ON,   1}, {  L,   1}, { ON,   3},
    {  L,   4}, { ON,   2}, {NSM,   1}, { ON,   1}, {  L,   3},
    {NSM,   4}, { ON,   2}, {  L,   2}, { ON,   2}, {  L,   2},
    {NSM,   1}, { ON,   9}, {  L,   1}, { ON,   4}, {  L,   2},
    { ON,   1}, {  L,   3}, {NSM,   2}, { ON,   2}, {  L,  12},
    { ET,   2}, {  L,   7}, { ON,   7}, {NSM,   1}, { ON,   2},
    {  L,   6}, { ON,   4}, {  L,   2}, { ON,   2}, {  L,  22},
    { ON,   1}, {  L,   7}, { ON,   1}, {  L,   2}, { ON,   1},
    {  L,   2}, { ON,   1}, {  L,   2}, { ON,   2}, {NSM,   1},
    { ON,   1}, {  L,   3}, {NSM,   2}, { ON,   4}, {NSM,   2},
    { ON,   2}, {NSM,   3}, { ON,  11}, {  L,   4}, { ON,   1},
    {  L,   1}, { ON,   7}, {  L,  10}, {NSM,   2}, {  L,   3},
    { ON,  12}, {NSM,   2}, {  L,   1}, { ON,   1}, {  L,   7},
    { ON,   1}, {  L,   1}, { ON,   1}, {  L,   3}, { ON,   1},
    {  L,  22}, { ON,   1}, {  L,   7}, { ON,   1}, {  L,   2},
    { ON,   1}, {  L,   5}, { ON,   2}, {NSM,   1}, {  L,   4},
    {NSM,   5}, { ON,   1}, {NSM,   2}, {  L,   1}, { ON,   1},
    {  L,   2}, {NSM,   1}, { ON,   2}, {  L,   1}, { ON,  15},
    {  L,   1}, { ON,   5}, {  L,  10}, { ON,  17}, {NSM,   1},
    {  L,   2}, { ON,   1}, {  L,   8}, { ON,   2}, {  L,   2},
    { ON,   2}, {  L,  22}, { ON,   1}, {  L,   7}, { ON,   1},
    {  L,   2}, { ON,   2}, {  L,   4}, { ON,   2}, {NSM,   1},
    {  L,   2}, {NSM,   1}, {  L,   1}, {NSM,   3}, { ON,   3},
    {  L,   2}, { ON,   2}, {  L,   2}, {NSM,   1}, { ON,   8},
    {NSM,   1}, {  L,   1}, { ON,   4}, {  L,   2}, { ON,   1},
    {  L,   3}, { ON,   4}, {  L,  11}, { ON,  17}, {NSM,   1},
    {  L,   1}, { ON,   1}, {  L,   6}, { ON,   3}, {  L,   3},
    { ON,   1}, {  L,   4}, { ON,   3}, {  L,   2}, { ON,   1},
    {  L,   1}, { ON,   1}, {  L,   2}, { ON,   3}, {  L,   2},
    { ON,   3}, {  L,   3}, { ON,   3}, {  L,   8}, { ON,   1},
    {  L,   3}, { ON,   4}, {  L,   2}, {NSM,   1}, {  L,   2},
    { ON,   3}, {  L,   3}, { ON,   1}, {  L,   3}, {NSM,   1},
    { ON,   9}, {  L,   1}, { ON,  15}, {  L,  12}, { ON,  14},
    {  L,   3}, { ON,   1}, {  L,   8}, { ON,   1}, {  L,   3},
    { ON,   1}, {  L,  23}, { ON,   1}, {  L,  10}, { ON,   1},
    {  L,   5}, { ON,   4}, {NSM,   3}, {  L,   4}, { ON,   1},
    {NSM,   3}, { ON,   1}, {NSM,   4}, { ON,   7}, {NSM,   2},
    { ON,   9}, {  L,   2}, { ON,   4}, {  L,  10}, { ON,  18},
    {  L,   2}, { ON,   1}, {  L,   8}, { ON,   1}, {  L,   3},
    { ON,   1}, {  L,  23}, { ON,   1}, {  L,  10}, { ON,   1},
    {  L,   5}, { ON,   4}, {  L,   1}, {NSM,   1}, {  L,   5},
    { ON,   1}, {NSM,   1}, {  L,   2}, { ON,   1}, {  L,   2},
    {NSM,   2}, { ON,   7}, {  L,   2}, { ON,   7}, {  L,   1},
    { ON,   1}, {  L,   2}, { ON,   4}, {  L,  10}, { ON,  18},
    {  L,   2}, { ON,   1}, {  L,   8}, { ON,   1}, {  L,   3},
    { ON,   1}, {  L,  23}, { ON,   1}, {  L,  16}, { ON,   4},
    {  L,   3}, {NSM,   3}, { ON,   2}, {  L,   3}, { ON,   1},
    {  L,   3}, {NSM,   1}, { ON,   9}, {  L,   1}, { ON,   8},
    {  L,   2}, { ON,   4}, {  L,  10}, { ON,  18}, {  L,   2},
    { ON,   1}, {  L,  18}, { ON,   3}, {  L,  24}, { ON,   1},
    {  L,   9}, { ON,   1}, {  L,   1}, { ON,   2}, {  L,   7},
    { ON,   3}, {NSM,   1}, { ON,   4}, {  L,   3}, {NSM,   3},
    { ON,   1}, {NSM,   1}, { ON,   1}, {  L,   8}, { ON,  18},
    {  L,   3}, { ON,  12}, {  L,  48}, {NSM,   1}, {  L,   2},
    {NSM,   7}, { ON,   4}, { ET,   1}, {  L,   7}, {NSM,   8},
    {  L,  13}, { ON,  37}, {  L,   2}, { ON,   1}, {  L,   1},
    { ON,   2}, {  L,   2}, { ON,   1}, {  L,   1}, { ON,   2},
    {  L,   1}, { ON,   6}, {  L,   4}, { ON,   1}, {  L,   7},
    { ON,   1}, {  L,   3}, { ON,   1}, {  L,   1}, { ON,   1},
    {  L,   1}, { ON,   2}, {  L,   2}, { ON,   1}, {  L,   4},
    {NSM,   1}, {  L,   2}, {NSM,   6}, { ON,   1}, {NSM,   2},
    {  L,   1}, { ON,   2}, {  L,   5}, { ON,   1}, {  L,   1},
    { ON,   1}, {NSM,   6}, { ON,   2}, {  L,  10}, { ON,   2},
    {  L,   2}, { ON,  34}, {  L,  24}, {NSM,   2}, {  L,  27},
    {NSM,   1}, {  L,   1}, {NSM,   1}, {  L,   1}, {NSM,   1},
    { ON,   4}, {  L,  10}, { ON,   1}, {  L,  34}, { ON,   6},
    {NSM,  14}, {  L,   1}, {NSM,   5}, {  L,   1}, {NSM,   2},
    {  L,   4}, { ON,   4}, {NSM,   8}, { ON,   1}, {NSM,  36},
    { ON,   1}, {  L,   8}, {NSM,   1}, {  L,   6}, { ON,   2},
    {  L,   1}, { ON,  48}, {  L,  34}, { ON,   1}, {  L,   5},
    { ON,   1}, {  L,   2}, { ON,   1}, {  L,   1}, {NSM,   4},
    {  L,   1}, {NSM,   1}, { ON,   3}, {NSM,   2}, {  L,   1},
    {NSM,   1}, { ON,   6}, {  L,  24}, {NSM,   2}, { ON,  70},
    {  L,  38}, { ON,  10}, {  L,  41}, { ON,   2}, {  L,   1},
    { ON,   4}, {  L,  90}, { ON,   5}, {  L,  68}, { ON,   5},
    {  L,  82}, { ON,   6}, {  L,   7}, { ON,   1}, {  L,  63},
    { ON,   1}, {  L,   1}, { ON,   1}, {  L,   4}, { ON,   2},
    {  L,   7}, { ON,   1}, {  L,   1}, { ON,   1}, {  L,   4},
    { ON,   2}, {  L,  39}, { ON,   1}, {  L,   1}, { ON,   1},
    {  L,   4}, { ON,   2}, {  L,  31}, { ON,   1}, {  L,   1},
    { ON,   1}, {  L,   4}, { ON,   2}, {  L,   7}, { ON,   1},
    {  L,   1}, { ON,   1}, {  L,   4}, { ON,   2}, {  L,   7},
    { ON,   1}, {  L,   7}, { ON,   1}, {  L,  23}, { ON,   1},
    {  L,  31}, { ON,   1}, {  L,   1}, { ON,   1}, {  L,   4},
    { ON,   2}, {  L,   7}, { ON,   1}, {  L,  39}, { ON,   1},
    {  L,  19}, { ON,   6}, {  L,  28}, { ON,  35}, {  L,  85},
    { ON,  12}, {  L, 255}, {  L, 255}, {  L, 120}, { ON,   9},
    { WS,   1}, {  L,  26}, { ON,   5}, {  L,  81}, { ON,  15},
    {  L,  13}, { ON,   1}, {  L,   4}, {NSM,   3}, { ON,  11},
    {  L,  18}, {NSM,   3}, {  L,   2}, { ON,   9}, {  L,  18},
    {NSM,   2}, { ON,  12}, {  L,  13}, { ON,   1}, {  L,   3},
    { ON,   1}, {NSM,   2}, { ON,  12}, {  L,  55}, {NSM,   7},
    {  L,   8}, {NSM,   1}, {  L,   2}, {NSM,  11}, {  L,   7},
    { ET,   1}, {  L,   1}, { ON,   3}, {  L,  10}, { ON,  33},
    {NSM,   3}, { BN,   1}, { ON,   1}, {  L,  10}, { ON,   6},
    {  L,  88}, { ON,   8}, {  L,  41}, {NSM,   1}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON,  91},
    {  L, 156}, { ON,   4}, {  L,  90}, { ON,   6}, {  L,  22},
    { ON,   2}, {  L,   6}, { ON,   2}, {  L,  38}, { ON,   2},
    {  L,   6}, { ON,   2}, {  L,   8}, { ON,   1}, {  L,   1},
    { ON,   1}, {  L,   1}, { ON,   1}, {  L,   1}, { ON,   1},
    {  L,  31}, { ON,   2}, {  L,  53}, { ON,   1}, {  L,   7},
    { ON,   1}, {  L,   1}, { ON,   3}, {  L,   3}, { ON,   1},
    {  L,   7}, { ON,   3}, {  L,   4}, { ON,   2}, {  L,   6},
    { ON,   4}, {  L,  13}, { ON,   5}, {  L,   3}, { ON,   1},
    {  L,   7}, { ON,   3}, { WS,  11}, { BN,   3}, {  L,   1},
    {  R,   1}, { ON,  24}, { WS,   1}, {  B,   1}, {LRE,   1},
    {RLE,   1}, {PDF,   1}, {LRO,   1}, {RLO,   1}, { WS,   1},
    { ET,   5}, { ON,  42}, { WS,   1}, { BN,   4}, { ON,   6},
    { BN,   6}, { EN,   1}, {  L,   1}, { ON,   2}, { EN,   6},
    { ET,   2}, { ON,   3}, {  L,   1}, { EN,  10}, { ET,   2},
    { ON,  20}, { ET,  18}, { ON,  30}, {NSM,  27}, { ON,  23},
    {  L,   1}, { ON,   4}, {  L,   1}, { ON,   2}, {  L,  10},
    { ON,   1}, {  L,   1}, { ON,   3}, {  L,   5}, { ON,   6},
    {  L,   1}, { ON,   1}, {  L,   1}, { ON,   1}, {  L,   1},
    { ON,   1}, {  L,   4}, { ET,   1}, {  L,   3}, { ON,   1},
    {  L,   7}, { ON,   3}, {  L,   3}, { ON,   5}, {  L,   5},
    { ON,  22}, {  L,  36}, { ON, 142}, { ET,   2}, { ON, 255},
    { ON,  35}, {  L,  69}, { ON,  26}, {  L,   1}, { ON, 202},
    { EN,  60}, {  L,  78}, { EN,   1}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON,  32},
    { WS,   1}, { ON,   4}, {  L,   3}, { ON,  25}, {  L,   9},
    {NSM,   6}, { ON,   1}, {  L,   5}, { ON,   2}, {  L,   5},
    { ON,   4}, {  L,  86}, { ON,   2}, {NSM,   2}, { ON,   2},
    {  L,   3}, { ON,   1}, {  L,  90}, { ON,   1}, {  L,   4},
    { ON,   5}, {  L,  40}, { ON,   4}, {  L,  94}, { ON,   1},
    {  L,  40}, { ON,  56}, {  L,  45}, { ON,   3}, {  L,  36},
    { ON,  28}, {  L,  28}, { ON,   3}, {  L,  50}, { ON,  15},
    {  L,  12}, { ON,   4}, {  L,  47}, { ON,   1}, {  L, 119},
    { ON,   4}, {  L,  99}, { ON,   2}, {  L,  31}, { ON,   1},
    {  L,   1}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 205}, {  L,   1}, { ON,  74}, {  L,   1},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 245}, {  L,   1}, { ON,  90}, {  L, 255},
    {  L, 255}, {  L, 255}, {  L, 255}, {  L, 145}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 122}, {  L,   1}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 205}, {  L,   1}, { ON,  92}, {  L,   1},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 129}, {  L,   2},
    { ON, 126}, {  L,   2}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON,   2}, {  L,   2}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255}, { ON, 255},
    { ON, 255}, { ON, 255}, { ON, 255}, { ON,  23}, {  L, 255},
    {  L,  48}, { ON,   2}, {  L,  59}, { ON, 149}, {  L,   7},
    { ON,  12}, {  L,   5}, { ON,   5}, {  R,   1}, {NSM,   1},
    {  R,  10}, { ET,   1}, {  R,  13}, { ON,   1}, {  R,   5},
    { ON,   1}, {  R,   1}, { ON,   1}, {  R,   2}, { ON,   1},
    {  R,   2}, { ON,   1}, {  R,  10}, { AL,  98}, { ON,  33},
    { AL, 255}, { AL, 108}, { ON,  18}, { AL,  64}, { ON,   2},
    { AL,  54}, { ON,  40}, { AL,  13}, { ON,   3}, {NSM,  16},
    { ON,  16}, {NSM,   4}, { ON,  44}, { CS,   1}, { ON,   1},
    { CS,   1}, { ON,   2}, { CS,   1}, { ON,   9}, { ET,   1},
    { ON,   2}, { ET,   2}, { ON,   5}, { ET,   2}, { ON,   5},
    { AL,   5}, { ON,   1}, { AL, 135}, { ON,   2}, { BN,   1},
    { ON,   3}, { ET,   3}, { ON,   5}, { ET,   1}, { CS,   1},
    { ET,   1}, { CS,   1}, { ES,   1}, { EN,  10}, { CS,   1},
    { ON,   6}, {  L,  26}, { ON,   6}, {  L,  26}, { ON,  11},
    {  L,  89}, { ON,   3}, {  L,   6}, { ON,   2}, {  L,   6},
    { ON,   2}, {  L,   6}, { ON,   2}, {  L,   3}, { ON,   3},
    { ET,   2}, { ON,   3}, { ET,   2}, { ON,   9}, {  L,  14},
};
