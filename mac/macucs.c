/* $Id: macucs.c,v 1.4 2003/01/14 19:42:00 ben Exp $ */

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

void init_ucs(Session *s)
{
    int i;

    /* Find the line control characters. FIXME: this is not right. */
    for (i = 0; i < 256; i++)
	if (i < ' ' || (i >= 0x7F && i < 0xA0))
	    s->ucsdata.unitab_ctrl[i] = i;
	else
	    s->ucsdata.unitab_ctrl[i] = 0xFF;

    for (i = 0; i < 256; i++) {
	s->ucsdata.unitab_line[i] = s->ucsdata.unitab_scoacs[i] = i;
	s->ucsdata.unitab_xterm[i] =
	    (i >= 0x5F && i < 0x7F) ? ((i+1) & 0x1F) : i;
    }
}
