#include <windows.h>
#include <stdio.h>

#include "putty.h"

/*
 * ldisc.c: PuTTY line disciplines
 */

static void c_write (char *buf, int len) {
    while (len--) {
	int new_head = (inbuf_head + 1) & INBUF_MASK;
	int c = (unsigned char) *buf;
	if (new_head != inbuf_reap) {
	    inbuf[inbuf_head] = *buf++;
	    inbuf_head = new_head;
	}
    }
}

static char *term_buf = NULL;
static int term_buflen = 0, term_bufsiz = 0, term_quotenext = 0;

static int plen(unsigned char c) {
    if ((c >= 32 && c <= 126) ||
        (c >= 160))
        return 1;
    else if (c < 128)
        return 2;                      /* ^x for some x */
    else
        return 4;                      /* <XY> for hex XY */
}

static void pwrite(unsigned char c) {
    if ((c >= 32 && c <= 126) ||
        (c >= 160)) {
        char cc = (char)c;
        c_write(&cc, 1);
    } else if (c < 128) {
        char cc[2];
        cc[1] = (c == 127 ? '?' : c + 0x40);
        cc[0] = '^';
        c_write(cc, 2);
    } else {
        char cc[5];
        sprintf(cc, "<%02X>", c);
        c_write(cc, 4);
    }
}

static void bsb(int n) {
    while (n--)
	c_write("\010 \010", 3);
}

static void term_send(char *buf, int len) {
    while (len--) {
	char c;
        c = *buf++;
	switch (term_quotenext ? ' ' : c) {
	    /*
	     * ^h/^?: delete one char and output one BSB
	     * ^w: delete, and output BSBs, to return to last space/nonspace
	     * boundary
	     * ^u: delete, and output BSBs, to return to BOL
	     * ^r: echo "^R\n" and redraw line
	     * ^v: quote next char
	     * ^d: if at BOL, end of file and close connection, else send line
	     * and reset to BOL
	     * ^m/^j: send line-plus-\r\n and reset to BOL
	     */
	  case 8: case 127:	       /* backspace/delete */
	    if (term_buflen > 0) {
		bsb(plen(term_buf[term_buflen-1]));
		term_buflen--;
	    }
	    break;
	  case 23:		       /* ^W delete word */
	    while (term_buflen > 0) {
		bsb(plen(term_buf[term_buflen-1]));
		term_buflen--;
		if (term_buflen > 0 &&
		    isspace(term_buf[term_buflen-1]) &&
		    !isspace(term_buf[term_buflen]))
		    break;
	    }
	    break;
	  case 21:		       /* ^U delete line */
	    while (term_buflen > 0) {
		bsb(plen(term_buf[term_buflen-1]));
		term_buflen--;
	    }
            break;
	  case 18:		       /* ^R redraw line */
	    c_write("^R\r\n", 4);
	    {
		int i;
		for (i = 0; i < term_buflen; i++)
		    pwrite(term_buf[i]);
	    }
	    break;
	  case 22:		       /* ^V quote next char */
	    term_quotenext = TRUE;
	    break;
	  case 4:		       /* ^D logout or send */
	    if (term_buflen == 0) {
		/* FIXME: eof */;
	    } else {
		back->send(term_buf, term_buflen);
		term_buflen = 0;
	    }
	    break;
	  case 13: case 10:	       /* ^M/^J send with newline */
	    back->send(term_buf, term_buflen);
	    back->send("\r\n", 2);
	    c_write("\r\n", 2);
	    term_buflen = 0;
	    break;
	  default:                     /* get to this label from ^V handler */
	    if (term_buflen >= term_bufsiz) {
		term_bufsiz = term_buflen + 256;
		term_buf = saferealloc(term_buf, term_bufsiz);
	    }
	    term_buf[term_buflen++] = c;
	    pwrite(c);
            term_quotenext = FALSE;
	    break;
	}
    }
}

static void simple_send(char *buf, int len) {
    back->send(buf, len);
}

Ldisc ldisc_term = { term_send };
Ldisc ldisc_simple = { simple_send };
