#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <time.h>
#include "putty.h"
#include "misc.h"

/*
 * Unix Unicode-handling routines.
 * 
 * FIXME: currently trivial stub versions assuming all codepages
 * are ISO8859-1.
 */

void lpage_send(int codepage, char *buf, int len, int interactive)
{
    ldisc_send(buf, len, interactive);
}

void luni_send(wchar_t * widebuf, int len, int interactive)
{
    static char *linebuffer = 0;
    static int linesize = 0;
    int ratio = (in_utf)?6:1;
    int i;
    char *p;

    if (len * ratio > linesize) {
	sfree(linebuffer);
	linebuffer = smalloc(len * ratio * 2 * sizeof(wchar_t));
	linesize = len * ratio * 2;
    }

    if (in_utf) {
	/* UTF is a simple algorithm */
	for (p = linebuffer, i = 0; i < len; i++) {
	    wchar_t ch = widebuf[i];

	    if ((ch&0xF800) == 0xD800) ch = '.';

	    if (ch < 0x80) {
		*p++ = (char) (ch);
	    } else if (ch < 0x800) {
		*p++ = (0xC0 | (ch >> 6));
		*p++ = (0x80 | (ch & 0x3F));
	    } else if (ch < 0x10000) {
		*p++ = (0xE0 | (ch >> 12));
		*p++ = (0x80 | ((ch >> 6) & 0x3F));
		*p++ = (0x80 | (ch & 0x3F));
	    } else if (ch < 0x200000) {
		*p++ = (0xF0 | (ch >> 18));
		*p++ = (0x80 | ((ch >> 12) & 0x3F));
		*p++ = (0x80 | ((ch >> 6) & 0x3F));
		*p++ = (0x80 | (ch & 0x3F));
	    } else if (ch < 0x4000000) {
		*p++ = (0xF8 | (ch >> 24));
		*p++ = (0x80 | ((ch >> 18) & 0x3F));
		*p++ = (0x80 | ((ch >> 12) & 0x3F));
		*p++ = (0x80 | ((ch >> 6) & 0x3F));
		*p++ = (0x80 | (ch & 0x3F));
	    } else {
		*p++ = (0xFC | (ch >> 30));
		*p++ = (0x80 | ((ch >> 24) & 0x3F));
		*p++ = (0x80 | ((ch >> 18) & 0x3F));
		*p++ = (0x80 | ((ch >> 12) & 0x3F));
		*p++ = (0x80 | ((ch >> 6) & 0x3F));
		*p++ = (0x80 | (ch & 0x3F));
	    }
	}
    } else {
	for (p = linebuffer, i = 0; i < len; i++) {
	    wchar_t ch = widebuf[i];
	    if (ch < 0x100)
		*p++ = (char) ch;
	    else
		*p++ = '.';
	}
    }
    if (p > linebuffer)
	ldisc_send(linebuffer, p - linebuffer, interactive);
}

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
	ret++;
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
	unitab_xterm[i] = i & 0x1F;
    }
}