/*
 * ldisc.c: PuTTY line discipline. Sits between the input coming
 * from keypresses in the window, and the output channel leading to
 * the back end. Implements echo and/or local line editing,
 * depending on what's currently configured.
 */

#include <windows.h>
#include <stdio.h>
#include <ctype.h>

#include "putty.h"

#define ECHOING (cfg.localecho == LD_YES || \
                 (cfg.localecho == LD_BACKEND && \
                      (back->ldisc(LD_ECHO) || term_ldisc(LD_ECHO))))
#define EDITING (cfg.localedit == LD_YES || \
                 (cfg.localedit == LD_BACKEND && \
                      (back->ldisc(LD_EDIT) || term_ldisc(LD_EDIT))))

static void c_write(char *buf, int len)
{
    from_backend(0, buf, len);
}

static char *term_buf = NULL;
static int term_buflen = 0, term_bufsiz = 0, term_quotenext = 0;

static int plen(unsigned char c)
{
    if ((c >= 32 && c <= 126) || (c >= 160))
	return 1;
    else if (c < 128)
	return 2;		       /* ^x for some x */
    else
	return 4;		       /* <XY> for hex XY */
}

static void pwrite(unsigned char c)
{
    if ((c >= 32 && c <= 126) || (c >= 160)) {
	c_write(&c, 1);
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

static void bsb(int n)
{
    while (n--)
	c_write("\010 \010", 3);
}

#define CTRL(x) (x^'@')

void ldisc_send(char *buf, int len)
{
    /*
     * Called with len=0 when the options change. We must inform
     * the front end in case it needs to know.
     */
    if (len == 0) {
	void ldisc_update(int echo, int edit);
	ldisc_update(ECHOING, EDITING);
    }
    /*
     * Either perform local editing, or just send characters.
     */
    if (EDITING) {
	while (len--) {
	    char c;
	    c = *buf++;
	    switch (term_quotenext ? ' ' : c) {
		/*
		 * ^h/^?: delete one char and output one BSB
		 * ^w: delete, and output BSBs, to return to last
		 * space/nonspace boundary
		 * ^u: delete, and output BSBs, to return to BOL
		 * ^c: Do a ^u then send a telnet IP
		 * ^z: Do a ^u then send a telnet SUSP
		 * ^\: Do a ^u then send a telnet ABORT
		 * ^r: echo "^R\n" and redraw line
		 * ^v: quote next char
		 * ^d: if at BOL, end of file and close connection,
		 * else send line and reset to BOL
		 * ^m: send line-plus-\r\n and reset to BOL
		 */
	      case CTRL('H'):
	      case CTRL('?'):	       /* backspace/delete */
		if (term_buflen > 0) {
		    if (ECHOING)
			bsb(plen(term_buf[term_buflen - 1]));
		    term_buflen--;
		}
		break;
	      case CTRL('W'):	       /* delete word */
		while (term_buflen > 0) {
		    if (ECHOING)
			bsb(plen(term_buf[term_buflen - 1]));
		    term_buflen--;
		    if (term_buflen > 0 &&
			isspace(term_buf[term_buflen - 1]) &&
			!isspace(term_buf[term_buflen]))
			break;
		}
		break;
	      case CTRL('U'):	       /* delete line */
	      case CTRL('C'):	       /* Send IP */
	      case CTRL('\\'):	       /* Quit */
	      case CTRL('Z'):	       /* Suspend */
		while (term_buflen > 0) {
		    if (ECHOING)
			bsb(plen(term_buf[term_buflen - 1]));
		    term_buflen--;
		}
		back->special(TS_EL);
		if (c == CTRL('C'))
		    back->special(TS_IP);
		if (c == CTRL('Z'))
		    back->special(TS_SUSP);
		if (c == CTRL('\\'))
		    back->special(TS_ABORT);
		break;
	      case CTRL('R'):	       /* redraw line */
		if (ECHOING) {
		    int i;
		    c_write("^R\r\n", 4);
		    for (i = 0; i < term_buflen; i++)
			pwrite(term_buf[i]);
		}
		break;
	      case CTRL('V'):	       /* quote next char */
		term_quotenext = TRUE;
		break;
	      case CTRL('D'):	       /* logout or send */
		if (term_buflen == 0) {
		    back->special(TS_EOF);
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
		if (ECHOING)
		    c_write("\r\n", 2);
		term_buflen = 0;
		break;
	      default:		       /* get to this label from ^V handler */
		if (term_buflen >= term_bufsiz) {
		    term_bufsiz = term_buflen + 256;
		    term_buf = saferealloc(term_buf, term_bufsiz);
		}
		term_buf[term_buflen++] = c;
		if (ECHOING)
		    pwrite(c);
		term_quotenext = FALSE;
		break;
	    }
	}
    } else {
	if (term_buflen != 0) {
	    back->send(term_buf, term_buflen);
	    while (term_buflen > 0) {
		bsb(plen(term_buf[term_buflen - 1]));
		term_buflen--;
	    }
	}
	if (len > 0) {
	    if (ECHOING)
		c_write(buf, len);
	    back->send(buf, len);
	}
    }
}
