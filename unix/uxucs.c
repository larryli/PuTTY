#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <locale.h>
#include <limits.h>
#include <wchar.h>

#include <time.h>

#include "putty.h"
#include "terminal.h"
#include "misc.h"

/*
 * Unix Unicode-handling routines.
 */

int is_dbcs_leadbyte(int codepage, char byte)
{
    return 0;			       /* we don't do DBCS */
}

int mb_to_wc(int codepage, int flags, char *mbstr, int mblen,
	     wchar_t *wcstr, int wclen)
{
    if (codepage == DEFAULT_CODEPAGE) {
	int n = 0;
	mbstate_t state = { 0 };

	setlocale(LC_CTYPE, "");

	while (mblen > 0) {
	    size_t i = mbrtowc(wcstr+n, mbstr, (size_t)mblen, &state);
	    if (i == (size_t)-1 || i == (size_t)-2)
		break;
	    n++;
	    mbstr += i;
	    mblen -= i;
	}

	setlocale(LC_CTYPE, "C");

	return n;
    } else
	return charset_to_unicode(&mbstr, &mblen, wcstr, wclen, codepage,
				  NULL, NULL, 0);
}

int wc_to_mb(int codepage, int flags, wchar_t *wcstr, int wclen,
	     char *mbstr, int mblen, char *defchr, int *defused)
{
    /* FIXME: we should remove the defused param completely... */
    if (defused)
	*defused = 0;

    if (codepage == DEFAULT_CODEPAGE) {
	char output[MB_LEN_MAX];
	mbstate_t state = { 0 };
	int n = 0;

	setlocale(LC_CTYPE, "");

	while (wclen > 0) {
	    int i = wcrtomb(output, wcstr[0], &state);
	    if (i == (size_t)-1 || i > n - mblen)
		break;
	    memcpy(mbstr+n, output, i);
	    n += i;
	    wcstr++;
	    wclen--;
	}

	setlocale(LC_CTYPE, "C");

	return n;
    } else
	return charset_from_unicode(&wcstr, &wclen, mbstr, mblen, codepage,
				    NULL, NULL, 0);
}

void init_ucs(void)
{
    int i;

    /*
     * In the platform-independent parts of the code, font_codepage
     * is used only for system DBCS support - which we don't
     * support at all. So we set this to something which will never
     * be used.
     */
    font_codepage = -1;

    /*
     * line_codepage should be decoded from the specification in
     * cfg.
     */
    line_codepage = charset_from_mimeenc(cfg.line_codepage);
    if (line_codepage == CS_NONE)
	line_codepage = charset_from_xenc(cfg.line_codepage);
    /* If it's still CS_NONE, we should assume direct-to-font. */

    /* FIXME: this is a hack. Currently fonts with incomprehensible
     * encodings are dealt with by pretending they're 8859-1. It's
     * ugly, but it's good enough to stop things crashing. Should do
     * something better here. */
    if (line_codepage == CS_NONE)
	line_codepage = CS_ISO8859_1;

    /*
     * Set up unitab_line, by translating each individual character
     * in the line codepage into Unicode.
     */
    for (i = 0; i < 256; i++) {
	char c[1], *p;
	wchar_t wc[1];
	int len;
	c[0] = i;
	p = c;
	len = 1;
	if (1 == charset_to_unicode(&p,&len,wc,1,line_codepage,NULL,L"",0))
	    unitab_line[i] = wc[0];
	else
	    unitab_line[i] = 0xFFFD;
    }

    /*
     * Set up unitab_xterm. This is the same as unitab_line except
     * in the line-drawing regions, where it follows the Unicode
     * encoding.
     * 
     * (Note that the strange X encoding of line-drawing characters
     * in the bottom 32 glyphs of ISO8859-1 fonts is taken care of
     * by the font encoding, which will spot such a font and act as
     * if it were in a variant encoding of ISO8859-1.)
     */
    for (i = 0; i < 256; i++) {
	static const wchar_t unitab_xterm_std[32] = {
	    0x2666, 0x2592, 0x2409, 0x240c, 0x240d, 0x240a, 0x00b0, 0x00b1,
	    0x2424, 0x240b, 0x2518, 0x2510, 0x250c, 0x2514, 0x253c, 0x23ba,
	    0x23bb, 0x2500, 0x23bc, 0x23bd, 0x251c, 0x2524, 0x2534, 0x252c,
	    0x2502, 0x2264, 0x2265, 0x03c0, 0x2260, 0x00a3, 0x00b7, 0x0020
	};
	if (i >= 0x5F && i < 0x7F)
	    unitab_xterm[i] = unitab_xterm_std[i & 0x1F];
	else
	    unitab_xterm[i] = unitab_line[i];
    }

    /*
     * Set up unitab_scoacs. The SCO Alternate Character Set is
     * simply CP437.
     */
    for (i = 0; i < 256; i++) {
	char c[1], *p;
	wchar_t wc[1];
	int len;
	c[0] = i;
	p = c;
	len = 1;
	if (1 == charset_to_unicode(&p,&len,wc,1,CS_CP437,NULL,L"",0))
	    unitab_scoacs[i] = wc[0];
	else
	    unitab_scoacs[i] = 0xFFFD;
    }

    /* Find the line control characters. */
    for (i = 0; i < 256; i++)
	if (unitab_line[i] < ' '
	    || (unitab_line[i] >= 0x7F && unitab_line[i] < 0xA0))
	    unitab_ctrl[i] = i;
	else
	    unitab_ctrl[i] = 0xFF;
}
