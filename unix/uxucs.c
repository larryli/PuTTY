#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <time.h>
#include "putty.h"
#include "terminal.h"
#include "misc.h"

/*
 * Unix Unicode-handling routines.
 * 
 * FIXME: currently trivial stub versions assuming all codepages
 * are ISO8859-1.
 */

int is_dbcs_leadbyte(int codepage, char byte)
{
    return 0;			       /* we don't do DBCS */
}

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

int wc_to_mb(int codepage, int flags, wchar_t *wcstr, int wclen,
	     char *mbstr, int mblen, char *defchr, int *defused)
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

void init_ucs(void)
{
    int i;
    /* Find the line control characters. FIXME: this is not right. */
    for (i = 0; i < 256; i++)
	if (i < ' ' || (i >= 0x7F && i < 0xA0))
	    unitab_ctrl[i] = i;
	else
	    unitab_ctrl[i] = 0xFF;

    for (i = 0; i < 256; i++) {
	unitab_line[i] = unitab_scoacs[i] = i;
	unitab_xterm[i] = (i >= 0x5F && i < 0x7F) ? ((i+1) & 0x1F) : i;
    }
}
