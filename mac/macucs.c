/* $Id: macucs.c,v 1.5 2003/01/14 19:57:36 ben Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <time.h>
#include "putty.h"
#include "terminal.h"
#include "misc.h"
#include "mac.h"

/*
 * Mac Unicode-handling routines.
 * 
 * FIXME: currently trivial stub versions assuming all codepages
 * are ISO8859-1.
 *
 * What we _should_ do is to use the Text Encoding Conversion Manager
 * when it's available, and have our own routines for converting to
 * standard Mac OS scripts when it's not.  Support for ATSUI might be
 * nice, too.
 */

/*
 * Determine whether a byte is the first byte of a double-byte
 * character in a system character set.  Only MI use is by clipme()
 * when copying direct-to-font text to the clipboard.
 */
int is_dbcs_leadbyte(int codepage, char byte)
{
    return 0;			       /* we don't do DBCS */
}

/*
 * Convert from Unicode to a system character set.  MI uses are:
 * (1) by lpage_send(), whose only MI use is to convert the answerback
 * string to Unicode, and
 * (2) by clipme() when copying direct-to-font text to the clipboard.
 */
int mb_to_wc(int codepage, int flags, char *mbstr, int mblen,
	     wchar_t *wcstr, int wclen)
{
    int ret = 0;
    while (mblen > 0 && wclen > 0) {
	*wcstr++ = (unsigned char) *mbstr++;
	mblen--, wclen--, ret++;
    }
    return ret;			       /* FIXME: check error codes! */
}

/*
 * Convert from a system character set to Unicode.  Used by luni_send
 * to convert Unicode into the line character set.
 */
int wc_to_mb(int codepage, int flags, wchar_t *wcstr, int wclen,
	     char *mbstr, int mblen, char *defchr, int *defused,
	     struct unicode_data *ucsdata)
{
    int ret = 0;
    if (defused)
	*defused = 0;
    while (mblen > 0 && wclen > 0) {
	if (*wcstr >= 0x100) {
	    if (defchr)
		*mbstr++ = *defchr;
	    else
		*mbstr++ = '.';
	    if (defused)
		*defused = 1;
	} else
	    *mbstr++ = (unsigned char) *wcstr;
	wcstr++;
	mblen--, wclen--, ret++;
    }
    return ret;			       /* FIXME: check error codes! */
}

/* Character conversion array,
 * the xterm one has the four scanlines that have no unicode 2.0
 * equivalents mapped to their unicode 3.0 locations.
 */
static const wchar_t unitab_xterm_std[32] = {
    0x2666, 0x2592, 0x2409, 0x240c, 0x240d, 0x240a, 0x00b0, 0x00b1,
    0x2424, 0x240b, 0x2518, 0x2510, 0x250c, 0x2514, 0x253c, 0x23ba,
    0x23bb, 0x2500, 0x23bc, 0x23bd, 0x251c, 0x2524, 0x2534, 0x252c,
    0x2502, 0x2264, 0x2265, 0x03c0, 0x2260, 0x00a3, 0x00b7, 0x0020
};

void init_ucs(Session *s)
{
    int i;

    /* Find the line control characters. FIXME: this is not right. */
    for (i = 0; i < 256; i++)
	if (i < ' ' || (i >= 0x7F && i < 0xA0))
	    s->ucsdata.unitab_ctrl[i] = i;
	else
	    s->ucsdata.unitab_ctrl[i] = 0xFF;

    for (i = 0; i < 256; i++)
	s->ucsdata.unitab_line[i] = s->ucsdata.unitab_scoacs[i] = i;

    /* VT100 graphics - NB: Broken for non-ascii CP's */
    memcpy(s->ucsdata.unitab_xterm, s->ucsdata.unitab_line,
	   sizeof(s->ucsdata.unitab_xterm));
    memcpy(s->ucsdata.unitab_xterm + '`', unitab_xterm_std,
	   sizeof(unitab_xterm_std));
    s->ucsdata.unitab_xterm['_'] = ' ';

}
