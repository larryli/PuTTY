#include <windows.h>
#include <stdio.h>
#include <ctype.h>

#include "putty.h"

/*
 * ldisc.c: PuTTY line disciplines
 */

static void c_write (char *buf, int len) {
    while (len--) 
        c_write1(*buf++);
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
        c_write1(c);
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

#define CTRL(x) (x^'@')

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
	     * ^c: Do a ^u then send a telnet IP
	     * ^z: Do a ^u then send a telnet SUSP
	     * ^\: Do a ^u then send a telnet ABORT
	     * ^r: echo "^R\n" and redraw line
	     * ^v: quote next char
	     * ^d: if at BOL, end of file and close connection, else send line
	     * and reset to BOL
	     * ^m: send line-plus-\r\n and reset to BOL
	     */
	  case CTRL('H'): case CTRL('?'):      /* backspace/delete */
	    if (term_buflen > 0) {
		bsb(plen(term_buf[term_buflen-1]));
		term_buflen--;
	    }
	    break;
	  case CTRL('W'):		       /* delete word */
	    while (term_buflen > 0) {
		bsb(plen(term_buf[term_buflen-1]));
		term_buflen--;
		if (term_buflen > 0 &&
		    isspace(term_buf[term_buflen-1]) &&
		    !isspace(term_buf[term_buflen]))
		    break;
	    }
	    break;
	  case CTRL('U'):	       /* delete line */
	  case CTRL('C'):	       /* Send IP */
	  case CTRL('\\'):	       /* Quit */
	  case CTRL('Z'):	       /* Suspend */
	    while (term_buflen > 0) {
		bsb(plen(term_buf[term_buflen-1]));
		term_buflen--;
	    }
	    back->special (TS_EL);
	    if( c == CTRL('C') )  back->special (TS_IP);
	    if( c == CTRL('Z') )  back->special (TS_SUSP);
	    if( c == CTRL('\\') ) back->special (TS_ABORT);
            break;
	  case CTRL('R'):	       /* redraw line */
	    c_write("^R\r\n", 4);
	    {
		int i;
		for (i = 0; i < term_buflen; i++)
		    pwrite(term_buf[i]);
	    }
	    break;
	  case CTRL('V'):	       /* quote next char */
	    term_quotenext = TRUE;
	    break;
	  case CTRL('D'):	       /* logout or send */
	    if (term_buflen == 0) {
		back->special (TS_EOF);
	    } else {
		back->send(term_buf, term_buflen);
		term_buflen = 0;
	    }
	    break;
	  case CTRL('M'):	       /* send with newline */
	    if (term_buflen > 0)
                back->send(term_buf, term_buflen);
	    if (cfg.protocol == PROT_RAW)
	        back->send("\r\n", 2);
	    else
	        back->send("\r", 1);
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
    if( term_buflen != 0 )
    {
	back->send(term_buf, term_buflen);
	while (term_buflen > 0) {
	    bsb(plen(term_buf[term_buflen-1]));
	    term_buflen--;
	}
    }
    if (len > 0)
        back->send(buf, len);
}

Ldisc ldisc_term = { term_send };
Ldisc ldisc_simple = { simple_send };
