#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <time.h>
#include <assert.h>
#include "putty.h"
#include "tree234.h"

#define CL_ANSIMIN	0x0001	/* Codes in all ANSI like terminals. */
#define CL_VT100	0x0002	/* VT100 */
#define CL_VT100AVO	0x0004	/* VT100 +AVO; 132x24 (not 132x14) & attrs */
#define CL_VT102	0x0008	/* VT102 */
#define CL_VT220	0x0010	/* VT220 */
#define CL_VT320	0x0020	/* VT320 */
#define CL_VT420	0x0040	/* VT420 */
#define CL_VT510	0x0080	/* VT510, NB VT510 includes ANSI */
#define CL_VT340TEXT	0x0100	/* VT340 extensions that appear in the VT420 */
#define CL_SCOANSI	0x1000	/* SCOANSI not in ANSIMIN. */
#define CL_ANSI		0x2000	/* ANSI ECMA-48 not in the VT100..VT420 */
#define CL_OTHER	0x4000	/* Others, Xterm, linux, putty, dunno, etc */

#define TM_VT100	(CL_ANSIMIN|CL_VT100)
#define TM_VT100AVO	(TM_VT100|CL_VT100AVO)
#define TM_VT102	(TM_VT100AVO|CL_VT102)
#define TM_VT220	(TM_VT102|CL_VT220)
#define TM_VTXXX	(TM_VT220|CL_VT340TEXT|CL_VT510|CL_VT420|CL_VT320)
#define TM_SCOANSI	(CL_ANSIMIN|CL_SCOANSI)

#define TM_PUTTY	(0xFFFF)

#define compatibility(x) \
    if ( ((CL_##x)&compatibility_level) == 0 ) { 	\
       termstate=TOPLEVEL;				\
       break;						\
    }
#define compatibility2(x,y) \
    if ( ((CL_##x|CL_##y)&compatibility_level) == 0 ) { \
       termstate=TOPLEVEL;				\
       break;						\
    }

#define has_compat(x) ( ((CL_##x)&compatibility_level) != 0 )

static int compatibility_level = TM_PUTTY;

static tree234 *scrollback;	       /* lines scrolled off top of screen */
static tree234 *screen;		       /* lines on primary screen */
static tree234 *alt_screen;	       /* lines on alternate screen */
static int disptop;		       /* distance scrolled back (0 or -ve) */

static unsigned long *cpos;	       /* cursor position (convenience) */

static unsigned long *disptext;	       /* buffer of text on real screen */
static unsigned long *wanttext;	       /* buffer of text we want on screen */

#define VBELL_TIMEOUT 100	       /* millisecond len of visual bell */

struct beeptime {
    struct beeptime *next;
    long ticks;
};
static struct beeptime *beephead, *beeptail;
int nbeeps;
int beep_overloaded;
long lastbeep;

static unsigned char *selspace;	       /* buffer for building selections in */

#define TSIZE (sizeof(unsigned long))
#define fix_cpos do { cpos = lineptr(curs.y) + curs.x; } while(0)

static unsigned long curr_attr, save_attr;
static unsigned long erase_char = ERASE_CHAR;

typedef struct {
    int y, x;
} pos;
#define poslt(p1,p2) ( (p1).y < (p2).y || ( (p1).y == (p2).y && (p1).x < (p2).x ) )
#define posle(p1,p2) ( (p1).y < (p2).y || ( (p1).y == (p2).y && (p1).x <= (p2).x ) )
#define poseq(p1,p2) ( (p1).y == (p2).y && (p1).x == (p2).x )
#define posdiff(p1,p2) ( ((p2).y - (p1).y) * (cols+1) + (p2).x - (p1).x )
#define incpos(p) ( (p).x == cols ? ((p).x = 0, (p).y++, 1) : ((p).x++, 0) )
#define decpos(p) ( (p).x == 0 ? ((p).x = cols, (p).y--, 1) : ((p).x--, 0) )

static pos curs;		       /* cursor */
static pos savecurs;		       /* saved cursor position */
static int marg_t, marg_b;	       /* scroll margins */
static int dec_om;		       /* DEC origin mode flag */
static int wrap, wrapnext;	       /* wrap flags */
static int insert;		       /* insert-mode flag */
static int cset;		       /* 0 or 1: which char set */
static int save_cset, save_csattr;     /* saved with cursor position */
static int rvideo;		       /* global reverse video flag */
static int rvbell_timeout;	       /* for ESC[?5hESC[?5l vbell */
static int cursor_on;		       /* cursor enabled flag */
static int reset_132;		       /* Flag ESC c resets to 80 cols */
static int use_bce;		       /* Use Background coloured erase */
static int blinker;		       /* When blinking is the cursor on ? */
static int tblinker;		       /* When the blinking text is on */
static int blink_is_real;	       /* Actually blink blinking text */
static int term_echoing;               /* Does terminal want local echo? */
static int term_editing;               /* Does terminal want local edit? */

static unsigned long cset_attr[2];

/*
 * Saved settings on the alternate screen.
 */
static int alt_x, alt_y, alt_om, alt_wrap, alt_wnext, alt_ins, alt_cset;
static int alt_t, alt_b;
static int alt_which;

#define ARGS_MAX 32		       /* max # of esc sequence arguments */
#define ARG_DEFAULT 0		       /* if an arg isn't specified */
#define def(a,d) ( (a) == ARG_DEFAULT ? (d) : (a) )
static int esc_args[ARGS_MAX];
static int esc_nargs;
static int esc_query;
#define ANSI(x,y)	((x)+((y)<<8))
#define ANSI_QUE(x)	ANSI(x,TRUE)

#define OSC_STR_MAX 2048
static int osc_strlen;
static char osc_string[OSC_STR_MAX+1];
static int osc_w;

static char id_string[1024] = "\033[?6c";

static unsigned char *tabs;

static enum {
    TOPLEVEL,
    SEEN_ESC,
    SEEN_CSI,
    SEEN_OSC,
    SEEN_OSC_W,

    DO_CTRLS,

    IGNORE_NEXT,
    SET_GL, SET_GR,
    SEEN_OSC_P,
    OSC_STRING, OSC_MAYBE_ST,
    SEEN_ESCHASH,
    VT52_ESC,
    VT52_Y1,
    VT52_Y2
} termstate;

static enum {
    NO_SELECTION, ABOUT_TO, DRAGGING, SELECTED
} selstate;
static enum {
    SM_CHAR, SM_WORD, SM_LINE
} selmode;
static pos selstart, selend, selanchor;

static short wordness[256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 01 */
    0,1,2,1,1,1,1,1,1,1,1,1,1,2,2,2, 2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,1, /* 23 */
    1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,2, /* 45 */
    1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1, /* 67 */
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* 89 */
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, /* AB */
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2, /* CD */
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2, /* EF */
};

static unsigned char sel_nl[] = SEL_NL;
static char * paste_buffer = 0;
static int paste_len, paste_pos, paste_hold;

/*
 * Internal prototypes.
 */
static void do_paint (Context, int);
static void erase_lots (int, int, int);
static void swap_screen (int);
static void update_sbar (void);
static void deselect (void);
/* log session to file stuff ... */
static FILE *lgfp = NULL;
static void logtraffic(unsigned char c, int logmode);

/*
 * Retrieve a line of the screen or of the scrollback, according to
 * whether the y coordinate is non-negative or negative
 * (respectively).
 */
unsigned long *lineptr(int y, int lineno) {
    unsigned long *line, lineattrs;
    tree234 *whichtree;
    int i, treeindex, oldlen;

    if (y >= 0) {
	whichtree = screen;
	treeindex = y;
    } else {
	whichtree = scrollback;
	treeindex = y + count234(scrollback);
    }
    line = index234(whichtree, treeindex);

    /* We assume that we don't screw up and retrieve something out of range. */
    assert(line != NULL);

    if (line[0] != cols) {
	/*
	 * This line is the wrong length, which probably means it
	 * hasn't been accessed since a resize. Resize it now.
	 */
	oldlen = line[0];
	lineattrs = line[oldlen+1];
	delpos234(whichtree, treeindex);
	line = srealloc(line, TSIZE * (2+cols));
	line[0] = cols;
	for (i = oldlen; i < cols; i++)
	    line[i+1] = ERASE_CHAR;
	line[cols+1] = lineattrs & LATTR_MODE;
	addpos234(whichtree, line, treeindex);
    }

    return line+1;
}
#define lineptr(x) lineptr(x,__LINE__)
/*
 * Set up power-on settings for the terminal.
 */
static void power_on(void) {
    curs.x = curs.y = alt_x = alt_y = savecurs.x = savecurs.y = 0;
    alt_t = marg_t = 0;
    if (rows != -1)
	alt_b = marg_b = rows - 1;
    else
	alt_b = marg_b = 0;
    if (cols != -1) {
	int i;
	for (i = 0; i < cols; i++)
	    tabs[i] = (i % 8 == 0 ? TRUE : FALSE);
    }
    alt_om = dec_om = cfg.dec_om;
    alt_wnext = wrapnext = alt_ins = insert = FALSE;
    alt_wrap = wrap = cfg.wrap_mode;
    alt_cset = cset = 0;
    cset_attr[0] = cset_attr[1] = ATTR_ASCII;
    rvideo = 0;
    in_vbell = FALSE;
    cursor_on = 1;
    save_attr = curr_attr = ATTR_DEFAULT;
    term_editing = term_echoing = FALSE;
    ldisc_send(NULL, 0);               /* cause ldisc to notice changes */
    app_cursor_keys = cfg.app_cursor;
    app_keypad_keys = cfg.app_keypad;
    use_bce = cfg.bce;
    blink_is_real = cfg.blinktext;
    erase_char = ERASE_CHAR;
    alt_which = 0;
    {
	int i;
	for (i = 0; i < 256; i++)
	    wordness[i] = cfg.wordness[i];
    }
    if (screen) {
	swap_screen (1);
	erase_lots (FALSE, TRUE, TRUE);
	swap_screen (0);
	erase_lots (FALSE, TRUE, TRUE);
    }
}

/*
 * Force a screen update.
 */
void term_update(void) {
    Context ctx;
    ctx = get_ctx();
    if (ctx) {
        if ( (seen_key_event && (cfg.scroll_on_key)) ||
	     (seen_disp_event && (cfg.scroll_on_disp)) ) {
	    disptop = 0;	       /* return to main screen */
	    seen_disp_event = seen_key_event = 0;
	    update_sbar();
	}
	do_paint (ctx, TRUE);
        sys_cursor(curs.x, curs.y - disptop);
	free_ctx (ctx);
    }
}

/*
 * Same as power_on(), but an external function.
 */
void term_pwron(void) {
    power_on();
    fix_cpos;
    disptop = 0;
    deselect();
    term_update();
}

/*
 * Clear the scrollback.
 */
void term_clrsb(void) {
    unsigned long *line;
    disptop = 0;
    while ((line = delpos234(scrollback, 0)) != NULL) {
	sfree(line);
    }
    update_sbar();
}

/*
 * Initialise the terminal.
 */
void term_init(void) {
    screen = alt_screen = scrollback = NULL;
    disptop = 0;
    disptext = wanttext = NULL;
    tabs = NULL;
    selspace = NULL;
    deselect();
    rows = cols = -1;
    power_on();
    beephead = beeptail = NULL;
    nbeeps = 0;
    lastbeep = FALSE;
    beep_overloaded = FALSE;
}

/*
 * Set up the terminal for a given size.
 */
void term_size(int newrows, int newcols, int newsavelines) {
    tree234 *newsb, *newscreen, *newalt;
    unsigned long *newdisp, *newwant, *oldline, *line;
    int i, j, ccols;
    int sblen;
    int save_alt_which = alt_which;

    if (newrows == rows && newcols == cols && newsavelines == savelines)
	return;			       /* nothing to do */

    deselect();
    swap_screen(0);

    alt_t = marg_t = 0;
    alt_b = marg_b = newrows - 1;

    if (rows == -1) {
	scrollback = newtree234(NULL);
	screen = newtree234(NULL);
	rows = 0;
    }

    /*
     * Resize the screen and scrollback. We only need to shift
     * lines around within our data structures, because lineptr()
     * will take care of resizing each individual line if
     * necessary. So:
     * 
     *  - If the new screen and the old screen differ in length, we
     *    must shunt some lines in from the scrollback or out to
     *    the scrollback.
     * 
     *  - If doing that fails to provide us with enough material to
     *    fill the new screen (i.e. the number of rows needed in
     *    the new screen exceeds the total number in the previous
     *    screen+scrollback), we must invent some blank lines to
     *    cover the gap.
     * 
     *  - Then, if the new scrollback length is less than the
     *    amount of scrollback we actually have, we must throw some
     *    away.
     */
    sblen = count234(scrollback);
    if (newrows > rows) {
	for (i = rows; i < newrows; i++) {
	    if (sblen > 0) {
		line = delpos234(scrollback, --sblen);
	    } else {
		line = smalloc(TSIZE * (newcols+2));
		line[0] = newcols;
		for (j = 0; j <= newcols; j++)
		    line[j+1] = ERASE_CHAR;
	    }
	    addpos234(screen, line, 0);
	}
    } else if (newrows < rows) {
	for (i = newrows; i < rows; i++) {
	    line = delpos234(screen, 0);
	    addpos234(scrollback, line, sblen++);
	}
    }
    assert(count234(screen) == newrows);
    while (sblen > newsavelines) {
	line = delpos234(scrollback, 0);
	sfree(line);
	sblen--;
    }
    assert(count234(scrollback) <= newsavelines);
    disptop = 0;

    newdisp = smalloc (newrows*(newcols+1)*TSIZE);
    for (i=0; i<newrows*(newcols+1); i++)
	newdisp[i] = ATTR_INVALID;
    sfree (disptext);
    disptext = newdisp;

    newwant = smalloc (newrows*(newcols+1)*TSIZE);
    for (i=0; i<newrows*(newcols+1); i++)
	newwant[i] = ATTR_INVALID;
    sfree (wanttext);
    wanttext = newwant;

    newalt = newtree234(NULL);
    for (i=0; i<newrows; i++) {
	line = smalloc(TSIZE * (newcols+2));
	line[0] = newcols;
	for (j = 0; j <= newcols; j++)
	    line[j+1] = erase_char;
	addpos234(newalt, line, i);
    }
    if (alt_screen) {
	while (NULL != (line = delpos234(alt_screen, 0)))
	    sfree(line);
	freetree234(alt_screen);
    }
    alt_screen = newalt;

    sfree (selspace);
    selspace = smalloc ( (newrows+newsavelines) * (newcols+sizeof(sel_nl)) );

    tabs = srealloc (tabs, newcols*sizeof(*tabs));
    {
	int i;
	for (i = (cols > 0 ? cols : 0); i < newcols; i++)
	    tabs[i] = (i % 8 == 0 ? TRUE : FALSE);
    }

    if (rows > 0)
	curs.y += newrows - rows;
    if (curs.y < 0)
	curs.y = 0;
    if (curs.y >= newrows)
	curs.y = newrows-1;
    if (curs.x >= newcols)
	curs.x = newcols-1;
    alt_x = alt_y = 0;
    wrapnext = alt_wnext = FALSE;

    rows = newrows;
    cols = newcols;
    savelines = newsavelines;
    fix_cpos;

    swap_screen(save_alt_which);

    update_sbar();
    term_update();
}

/*
 * Swap screens.
 */
static void swap_screen (int which) {
    int t;
    tree234 *ttr;

    if (which == alt_which)
	return;

    alt_which = which;

    ttr = alt_screen; alt_screen = screen; screen = ttr;
    t = curs.x; curs.x = alt_x; alt_x = t;
    t = curs.y; curs.y = alt_y; alt_y = t;
    t = marg_t; marg_t = alt_t; alt_t = t;
    t = marg_b; marg_b = alt_b; alt_b = t;
    t = dec_om; dec_om = alt_om; alt_om = t;
    t = wrap; wrap = alt_wrap; alt_wrap = t;
    t = wrapnext; wrapnext = alt_wnext; alt_wnext = t;
    t = insert; insert = alt_ins; alt_ins = t;
    t = cset; cset = alt_cset; alt_cset = t;

    fix_cpos;
}

/*
 * Update the scroll bar.
 */
static void update_sbar(void) {
    int nscroll;

    nscroll = count234(scrollback);

    set_sbar (nscroll + rows, nscroll + disptop, rows);
}

/*
 * Check whether the region bounded by the two pointers intersects
 * the scroll region, and de-select the on-screen selection if so.
 */
static void check_selection (pos from, pos to) {
    if (poslt(from, selend) && poslt(selstart, to))
	deselect();
}

/*
 * Scroll the screen. (`lines' is +ve for scrolling forward, -ve
 * for backward.) `sb' is TRUE if the scrolling is permitted to
 * affect the scrollback buffer.
 * 
 * NB this function invalidates all pointers into lines of the
 * screen data structures. In particular, you MUST call fix_cpos
 * after calling scroll() and before doing anything else that
 * uses the cpos shortcut pointer.
 */
static void scroll (int topline, int botline, int lines, int sb) {
    unsigned long *line, *line2;
    int i;

    if (topline != 0 || alt_which != 0)
	sb = FALSE;

    if (lines < 0) {
	while (lines < 0) {
	    line = delpos234(screen, botline);
	    for (i = 0; i < cols; i++)
		line[i+1] = erase_char;
	    line[cols+1] = 0;
	    addpos234(screen, line, topline);

	    if (selstart.y >= topline && selstart.y <= botline) {
		selstart.y++;
		if (selstart.y > botline) {
		    selstart.y = botline;
		    selstart.x = 0;
		}
	    }
	    if (selend.y >= topline && selend.y <= botline) {
		selend.y++;
		if (selend.y > botline) {
		    selend.y = botline;
		    selend.x = 0;
		}
	    }

	    lines++;
	}
    } else {
	while (lines > 0) {
	    line = delpos234(screen, topline);
	    if (sb && savelines > 0) {
		int sblen = count234(scrollback);
		/*
		 * We must add this line to the scrollback. We'll
		 * remove a line from the top of the scrollback to
		 * replace it, or allocate a new one if the
		 * scrollback isn't full.
		 */
		if (sblen == savelines) {
		    sblen--, line2 = delpos234(scrollback, 0);
		} else {
		    line2 = smalloc(TSIZE * (cols+2));
		    line2[0] = cols;
		}
		addpos234(scrollback, line, sblen);
		line = line2;
	    }
	    for (i = 0; i < cols; i++)
		line[i+1] = erase_char;
	    line[cols+1] = 0;
	    addpos234(screen, line, botline);

	    if (selstart.y >= topline && selstart.y <= botline) {
		selstart.y--;
		if (selstart.y < topline) {
		    selstart.y = topline;
		    selstart.x = 0;
		}
	    }
	    if (selend.y >= topline && selend.y <= botline) {
		selend.y--;
		if (selend.y < topline) {
		    selend.y = topline;
		    selend.x = 0;
		}
	    }

	    lines--;
	}
    }
}

/*
 * Move the cursor to a given position, clipping at boundaries. We
 * may or may not want to clip at the scroll margin: marg_clip is 0
 * not to, 1 to disallow _passing_ the margins, and 2 to disallow
 * even _being_ outside the margins.
 */
static void move (int x, int y, int marg_clip) {
    if (x < 0)
	x = 0;
    if (x >= cols)
	x = cols-1;
    if (marg_clip) {
	if ((curs.y >= marg_t || marg_clip == 2) && y < marg_t)
	    y = marg_t;
	if ((curs.y <= marg_b || marg_clip == 2) && y > marg_b)
	    y = marg_b;
    }
    if (y < 0)
	y = 0;
    if (y >= rows)
	y = rows-1;
    curs.x = x;
    curs.y = y;
    fix_cpos;
    wrapnext = FALSE;
}

/*
 * Save or restore the cursor and SGR mode.
 */
static void save_cursor(int save) {
    if (save) {
	savecurs = curs;
	save_attr = curr_attr;
	save_cset = cset;
	save_csattr = cset_attr[cset];
    } else {
	curs = savecurs;
	/* Make sure the window hasn't shrunk since the save */
	if (curs.x >= cols) curs.x = cols-1;
	if (curs.y >= rows) curs.y = rows-1;

	curr_attr = save_attr;
	cset = save_cset;
	cset_attr[cset] = save_csattr;
	fix_cpos;
        if (use_bce) erase_char = (' ' |(curr_attr&(ATTR_FGMASK|ATTR_BGMASK)));
    }
}

/*
 * Erase a large portion of the screen: the whole screen, or the
 * whole line, or parts thereof.
 */
static void erase_lots (int line_only, int from_begin, int to_end) {
    pos start, end;
    int erase_lattr;
    unsigned long *ldata;

    if (line_only) {
	start.y = curs.y;
	start.x = 0;
	end.y = curs.y + 1;
	end.x = 0;
	erase_lattr = FALSE;
    } else {
	start.y = 0;
	start.x = 0;
	end.y = rows;
	end.x = 0;
	erase_lattr = TRUE;
    }
    if (!from_begin) {
	start = curs;
    }
    if (!to_end) {
	end = curs;
    }
    check_selection (start, end);

    /* Clear screen also forces a full window redraw, just in case. */
    if (start.y == 0 && start.x == 0 && end.y == rows)
       term_invalidate();

    ldata = lineptr(start.y);
    while (poslt(start, end)) {
	if (start.y == cols && !erase_lattr)
	    ldata[start.x] &= ~ATTR_WRAPPED;
	else
	    ldata[start.x] = erase_char;
	if (incpos(start) && start.y < rows)
	    ldata = lineptr(start.y);
    }
}

/*
 * Insert or delete characters within the current line. n is +ve if
 * insertion is desired, and -ve for deletion.
 */
static void insch (int n) {
    int dir = (n < 0 ? -1 : +1);
    int m;
    pos cursplus;
    unsigned long *ldata;

    n = (n < 0 ? -n : n);
    if (n > cols - curs.x)
	n = cols - curs.x;
    m = cols - curs.x - n;
    cursplus.y = curs.y;
    cursplus.x = curs.x + n;
    check_selection (curs, cursplus);
    ldata = lineptr(curs.y);
    if (dir < 0) {
	memmove (ldata + curs.x, ldata + curs.x + n, m*TSIZE);
	while (n--)
	    ldata[curs.x + m++] = erase_char;
    } else {
	memmove (ldata + curs.x + n, ldata + curs.x, m*TSIZE);
	while (n--)
	    ldata[curs.x + n] = erase_char;
    }
}

/*
 * Toggle terminal mode `mode' to state `state'. (`query' indicates
 * whether the mode is a DEC private one or a normal one.)
 */
static void toggle_mode (int mode, int query, int state) {
    long ticks;
    if (query) switch (mode) {
      case 1:			       /* application cursor keys */
	app_cursor_keys = state;
	break;
      case 2:			       /* VT52 mode */
	vt52_mode = !state;
	break;
      case 3:			       /* 80/132 columns */
	deselect();
	request_resize (state ? 132 : 80, rows, 1);
	reset_132 = state;
	break;
      case 5:			       /* reverse video */
	/*
	 * Toggle reverse video. If we receive an OFF within the
	 * visual bell timeout period after an ON, we trigger an
	 * effective visual bell, so that ESC[?5hESC[?5l will
	 * always be an actually _visible_ visual bell.
	 */
	ticks = GetTickCount();
	if (rvideo && !state &&	       /* we're turning it off */
	    ticks < rvbell_timeout) {  /* and it's not long since it was turned on */
	    in_vbell = TRUE;	       /* we may clear rvideo but we set in_vbell */
	    if (vbell_timeout < rvbell_timeout)   /* don't move vbell end forward */
		vbell_timeout = rvbell_timeout;   /* vbell end is at least then */
	} else if (!rvideo && state) {
	    /* This is an ON, so we notice the time and save it. */
	    rvbell_timeout = ticks + VBELL_TIMEOUT;
	}
	rvideo = state;
	seen_disp_event = TRUE;
	if (state) term_update();
	break;
      case 6:			       /* DEC origin mode */
	dec_om = state;
	break;
      case 7:			       /* auto wrap */
	wrap = state;
	break;
      case 8:			       /* auto key repeat */
	repeat_off = !state;
	break;
      case 10:			       /* set local edit mode */
        term_editing = state;
        ldisc_send(NULL, 0);           /* cause ldisc to notice changes */
	break;
      case 25:			       /* enable/disable cursor */
	compatibility2(OTHER,VT220);
	cursor_on = state;
	seen_disp_event = TRUE;
	break;
      case 47:			       /* alternate screen */
	compatibility(OTHER);
	deselect();
	swap_screen (state);
	disptop = 0;
	break;
    } else switch (mode) {
      case 4:			       /* set insert mode */
	compatibility(VT102);
	insert = state;
	break;
      case 12:			       /* set echo mode */
        term_echoing = !state;
        ldisc_send(NULL, 0);           /* cause ldisc to notice changes */
	break;
      case 20:			       /* Return sends ... */
	cr_lf_return = state;
	break;
    }
}

/*
 * Process an OSC sequence: set window title or icon name.
 */
static void do_osc(void) {
    if (osc_w) {
	while (osc_strlen--)
	    wordness[(unsigned char)osc_string[osc_strlen]] = esc_args[0];
    } else {
	osc_string[osc_strlen] = '\0';
	switch (esc_args[0]) {
	  case 0:
	  case 1:
	    set_icon (osc_string);
	    if (esc_args[0] == 1)
		break;
	    /* fall through: parameter 0 means set both */
	  case 2:
	  case 21:
	    set_title (osc_string);
	    break;
	}
    }
}

/*
 * Remove everything currently in `inbuf' and stick it up on the
 * in-memory display. There's a big state machine in here to
 * process escape sequences...
 */
void term_out(void) {
    int c, inbuf_reap;

    for(inbuf_reap = 0; inbuf_reap < inbuf_head; inbuf_reap++)
    {
        c = inbuf[inbuf_reap];

	/*
         * Optionally log the session traffic to a file. Useful for
         * debugging and possibly also useful for actual logging.
         */
	logtraffic((unsigned char)c, LGTYP_DEBUG);

	/* Note only VT220+ are 8-bit VT102 is seven bit, it shouldn't even
	 * be able to display 8-bit characters, but I'll let that go 'cause
	 * of i18n.
	 */
	if( ( (c&0x60) == 0 || c == '\177') && 
	     termstate < DO_CTRLS &&
	    ( (c&0x80) == 0 || has_compat(VT220))) {
	    switch (c) {
	      case '\005':	       /* terminal type query */
		/* Strictly speaking this is VT100 but a VT100 defaults to
		 * no response. Other terminals respond at their option.
		 *
		 * Don't put a CR in the default string as this tends to
		 * upset some weird software.
		 *
		 * An xterm returns "xterm" (5 characters)
		 */
	        compatibility(ANSIMIN);
		{
		    char abuf[256], *s, *d;
		    int state=0;
		    for(s=cfg.answerback, d=abuf; *s; s++) {
                        if (state)
                        {
                            if (*s >= 'a' && *s <= 'z')
                                *d++ = (*s - ('a'-1));
                            else if ((*s >='@' && *s<='_') ||
                                     *s == '?' || (*s&0x80))
                                *d++ = ('@'^*s);
                            else if (*s == '~')
                                *d++ = '^';
                            state = 0;
                        }
                        else if (*s == '^') {
                            state = 1;
                        }
                        else
                            *d++ = xlat_kbd2tty((unsigned char)*s);
		    }
		    ldisc_send (abuf, d-abuf);
		}
		break;
	      case '\007':
		{
		    struct beeptime *newbeep;
		    long ticks;

		    ticks = GetTickCount();

		    if (!beep_overloaded) {
			newbeep = smalloc(sizeof(struct beeptime));
			newbeep->ticks = ticks;
			newbeep->next = NULL;
			if (!beephead)
			    beephead = newbeep;
			else
			    beeptail->next = newbeep;
			beeptail = newbeep;
			nbeeps++;
		    }

		    /*
		     * Throw out any beeps that happened more than
		     * t seconds ago.
		     */
		    while (beephead &&
			   beephead->ticks < ticks - cfg.bellovl_t) {
			struct beeptime *tmp = beephead;
			beephead = tmp->next;
			sfree(tmp);
			if (!beephead)
			    beeptail = NULL;
			nbeeps--;
		    }

		    if (cfg.bellovl && beep_overloaded &&
			ticks-lastbeep >= cfg.bellovl_s) {
			/*
			 * If we're currently overloaded and the
			 * last beep was more than s seconds ago,
			 * leave overload mode.
			 */
			beep_overloaded = FALSE;
		    } else if (cfg.bellovl && !beep_overloaded &&
			       nbeeps >= cfg.bellovl_n) {
			/*
			 * Now, if we have n or more beeps
			 * remaining in the queue, go into overload
			 * mode.
			 */
			beep_overloaded = TRUE;
		    }
		    lastbeep = ticks;

		    /*
		     * Perform an actual beep if we're not overloaded.
		     */
		    if ((!cfg.bellovl || !beep_overloaded) && cfg.beep != 0) {
			if (cfg.beep != 2)
			    beep(cfg.beep);
			else if(cfg.beep == 2) {
			    in_vbell = TRUE;
			    vbell_timeout = ticks + VBELL_TIMEOUT;
			    term_update();
			}
		    }
		    disptop = 0;
		}
		break;
	      case '\b':
		if (curs.x == 0 && curs.y == 0)
		    ;
		else if (curs.x == 0 && curs.y > 0)
		    curs.x = cols-1, curs.y--;
		else if (wrapnext)
		    wrapnext = FALSE;
		else
		    curs.x--;
		fix_cpos;
		seen_disp_event = TRUE;
		break;
	      case '\016':
	        compatibility(VT100);
		cset = 1;
		break;
	      case '\017':
	        compatibility(VT100);
		cset = 0;
		break;
	      case '\033':
		if (vt52_mode) 
		   termstate = VT52_ESC;
		else {
		    compatibility(ANSIMIN);
		    termstate = SEEN_ESC;
		}
		break;
	      case 0233:
	        compatibility(VT220);
		termstate = SEEN_CSI;
		esc_nargs = 1;
		esc_args[0] = ARG_DEFAULT;
		esc_query = FALSE;
		break;
	      case 0235:
	        compatibility(VT220);
		termstate = SEEN_OSC;
		esc_args[0] = 0;
		break;
	      case '\r':
		curs.x = 0;
		wrapnext = FALSE;
		fix_cpos;
		seen_disp_event = TRUE;
		paste_hold = 0;
		logtraffic((unsigned char)c,LGTYP_ASCII);
		break;
	      case '\013':
	      case '\014':
	        compatibility(VT100);
	      case '\n':
		if (curs.y == marg_b)
		    scroll (marg_t, marg_b, 1, TRUE);
		else if (curs.y < rows-1)
		    curs.y++;
		if (cfg.lfhascr)
		    curs.x = 0;
		fix_cpos;
		wrapnext = FALSE;
		seen_disp_event = 1;
		paste_hold = 0;
		logtraffic((unsigned char)c,LGTYP_ASCII);
		break;
	      case '\t':
		{
		    pos old_curs = curs;
		    unsigned long *ldata = lineptr(curs.y);

		    do {
			curs.x++;
		    } while (curs.x < cols-1 && !tabs[curs.x]);

		    if ((ldata[cols] & LATTR_MODE) != LATTR_NORM)
		    {
			if (curs.x >= cols/2)
			    curs.x = cols/2-1;
		    }
		    else
		    {
			if (curs.x >= cols)
			    curs.x = cols-1;
		    }

		    fix_cpos;
		    check_selection (old_curs, curs);
		}
		seen_disp_event = TRUE;
		break;
	      case '\177': /* Destructive backspace
			      This does nothing on a real VT100 */
	        compatibility(OTHER);
		if (curs.x && !wrapnext) curs.x--;
		wrapnext = FALSE;
		fix_cpos;
		*cpos = (' ' | curr_attr | ATTR_ASCII);
		break;
	    }
	}
	else switch (termstate) {
	  case TOPLEVEL:
	    /* Only graphic characters get this far, ctrls are stripped above */
	    if (wrapnext && wrap) {
		cpos[1] |= ATTR_WRAPPED;
		if (curs.y == marg_b)
		    scroll (marg_t, marg_b, 1, TRUE);
		else if (curs.y < rows-1)
		    curs.y++;
		curs.x = 0;
		fix_cpos;
		wrapnext = FALSE;
	    }
	    if (insert)
		insch (1);
	    if (selstate != NO_SELECTION) {
		pos cursplus = curs;
		incpos(cursplus);
	        check_selection (curs, cursplus);
	    }
	    switch (cset_attr[cset]) {
		/*
		 * Linedraw characters are different from 'ESC ( B'
		 * only for a small range. For ones outside that
		 * range, make sure we use the same font as well as
		 * the same encoding.
		 */
	    case ATTR_LINEDRW:
		if (c<0x5f || c>0x7F)
	            *cpos++ = xlat_tty2scr((unsigned char)c) | curr_attr |
		              ATTR_ASCII;
		else if (c==0x5F)
		    *cpos++ = ' ' | curr_attr | ATTR_ASCII;
		else
	            *cpos++ = ((unsigned char)c) | curr_attr | ATTR_LINEDRW;
		break;
	    case ATTR_GBCHR:
		/* If UK-ASCII, make the '#' a LineDraw Pound */
		if (c == '#') {
		    *cpos++ = '}' | curr_attr | ATTR_LINEDRW;
		    break;
		}
		/*FALLTHROUGH*/
	    default:
		*cpos = xlat_tty2scr((unsigned char)c) | curr_attr |
		    (c <= 0x7F ? cset_attr[cset] : ATTR_ASCII);
		logtraffic((unsigned char)c, LGTYP_ASCII);
		cpos++;
		break;
	    }
	    curs.x++;
	    if (curs.x == cols) {
		cpos--;
		curs.x--;
		wrapnext = TRUE;
	    }
	    seen_disp_event = 1;
	    break;

	  case IGNORE_NEXT:
	    termstate = TOPLEVEL;
	    break;
	  case OSC_MAYBE_ST:
	    /*
	     * This state is virtually identical to SEEN_ESC, with the
	     * exception that we have an OSC sequence in the pipeline,
	     * and _if_ we see a backslash, we process it.
	     */
	    if (c == '\\') {
		do_osc();
		termstate = TOPLEVEL;
		break;
	    }
	    /* else fall through */
	  case SEEN_ESC:
	    termstate = TOPLEVEL;
	    switch (c) {
	      case ' ':		       /* some weird sequence? */
	        compatibility(VT220);
		termstate = IGNORE_NEXT;
		break;
	      case '[':		       /* enter CSI mode */
		termstate = SEEN_CSI;
		esc_nargs = 1;
		esc_args[0] = ARG_DEFAULT;
		esc_query = FALSE;
		break;
	      case ']':		       /* xterm escape sequences */
		/* Compatibility is nasty here, xterm, linux, decterm yuk! */
	        compatibility(OTHER);
		termstate = SEEN_OSC;
		esc_args[0] = 0;
		break;
	      case '(':		       /* should set GL */
	        compatibility(VT100);
		termstate = SET_GL;
		break;
	      case ')':		       /* should set GR */
	        compatibility(VT100);
		termstate = SET_GR;
		break;
	      case '7':		       /* save cursor */
	        compatibility(VT100);
		save_cursor (TRUE);
		break;
	      case '8':		       /* restore cursor */
	        compatibility(VT100);
		save_cursor (FALSE);
		seen_disp_event = TRUE;
		break;
	      case '=':
	        compatibility(VT100);
		app_keypad_keys = TRUE;
		break;
	      case '>':
	        compatibility(VT100);
		app_keypad_keys = FALSE;
		break;
	      case 'D':		       /* exactly equivalent to LF */
	        compatibility(VT100);
		if (curs.y == marg_b)
		    scroll (marg_t, marg_b, 1, TRUE);
		else if (curs.y < rows-1)
		    curs.y++;
		fix_cpos;
		wrapnext = FALSE;
		seen_disp_event = TRUE;
		break;
	      case 'E':		       /* exactly equivalent to CR-LF */
	        compatibility(VT100);
		curs.x = 0;
		if (curs.y == marg_b)
		    scroll (marg_t, marg_b, 1, TRUE);
		else if (curs.y < rows-1)
		    curs.y++;
		fix_cpos;
		wrapnext = FALSE;
		seen_disp_event = TRUE;
		break;
	      case 'M':		       /* reverse index - backwards LF */
	        compatibility(VT100);
		if (curs.y == marg_t)
		    scroll (marg_t, marg_b, -1, TRUE);
		else if (curs.y > 0)
		    curs.y--;
		fix_cpos;
		wrapnext = FALSE;
		seen_disp_event = TRUE;
		break;
	      case 'Z':		       /* terminal type query */
	        compatibility(VT100);
		ldisc_send (id_string, strlen(id_string));
		break;
	      case 'c':		       /* restore power-on settings */
	        compatibility(VT100);
		power_on();
		if (reset_132) {
	            request_resize (80, rows, 1);
		    reset_132 = 0;
		}
		fix_cpos;
		disptop = 0;
		seen_disp_event = TRUE;
		break;
	      case '#':		       /* ESC # 8 fills screen with Es :-) */
	        compatibility(VT100);
		termstate = SEEN_ESCHASH;
		break;
	      case 'H':		       /* set a tab */
	        compatibility(VT100);
		tabs[curs.x] = TRUE;
		break;
	    }
	    break;
	  case SEEN_CSI:
	    termstate = TOPLEVEL;      /* default */
	    if( isdigit(c) )
	    {
		if (esc_nargs <= ARGS_MAX) {
		    if (esc_args[esc_nargs-1] == ARG_DEFAULT)
			esc_args[esc_nargs-1] = 0;
		    esc_args[esc_nargs-1] =
			10 * esc_args[esc_nargs-1] + c - '0';
		}
		termstate = SEEN_CSI;
	    }
	    else if( c == ';' )
	    {
		if (++esc_nargs <= ARGS_MAX)
		    esc_args[esc_nargs-1] = ARG_DEFAULT;
		termstate = SEEN_CSI;
	    }
	    else if( c < '@' )
	    {
		if( esc_query )     esc_query = -1;
		else if( c == '?' ) esc_query = TRUE;
		else                esc_query = c;
		termstate = SEEN_CSI;
	    }
	    else switch (ANSI(c,esc_query)) {
	      case 'A':		       /* move up N lines */
		move (curs.x, curs.y - def(esc_args[0], 1), 1);
		seen_disp_event = TRUE;
		break;
	      case 'e':      /* move down N lines */
	        compatibility(ANSI);
	      case 'B':
		move (curs.x, curs.y + def(esc_args[0], 1), 1);
		seen_disp_event = TRUE;
		break;
	      case 'a':      /* move right N cols */
	        compatibility(ANSI);
	      case 'C':
		move (curs.x + def(esc_args[0], 1), curs.y, 1);
		seen_disp_event = TRUE;
		break;
	      case 'D':		       /* move left N cols */
		move (curs.x - def(esc_args[0], 1), curs.y, 1);
		seen_disp_event = TRUE;
		break;
	      case 'E':		       /* move down N lines and CR */
	        compatibility(ANSI);
		move (0, curs.y + def(esc_args[0], 1), 1);
		seen_disp_event = TRUE;
		break;
	      case 'F':		       /* move up N lines and CR */
	        compatibility(ANSI);
		move (0, curs.y - def(esc_args[0], 1), 1);
		seen_disp_event = TRUE;
		break;
	      case 'G': case '`':      /* set horizontal posn */
	        compatibility(ANSI);
		move (def(esc_args[0], 1) - 1, curs.y, 0);
		seen_disp_event = TRUE;
		break;
	      case 'd':		       /* set vertical posn */
	        compatibility(ANSI);
		move (curs.x, (dec_om ? marg_t : 0) + def(esc_args[0], 1) - 1,
		      (dec_om ? 2 : 0));
		seen_disp_event = TRUE;
		break;
	      case 'H': case 'f':      /* set horz and vert posns at once */
		if (esc_nargs < 2)
		    esc_args[1] = ARG_DEFAULT;
		move (def(esc_args[1], 1) - 1,
		      (dec_om ? marg_t : 0) + def(esc_args[0], 1) - 1,
		      (dec_om ? 2 : 0));
		seen_disp_event = TRUE;
		break;
	      case 'J':		       /* erase screen or parts of it */
		{
		    unsigned int i = def(esc_args[0], 0) + 1;
		    if (i > 3)
			i = 0;
		    erase_lots(FALSE, !!(i & 2), !!(i & 1));
		}
		disptop = 0;
		seen_disp_event = TRUE;
		break;
	      case 'K':		       /* erase line or parts of it */
		{
		    unsigned int i = def(esc_args[0], 0) + 1;
		    if (i > 3)
			i = 0;
		    erase_lots(TRUE, !!(i & 2), !!(i & 1));
		}
		seen_disp_event = TRUE;
		break;
	      case 'L':		       /* insert lines */
	        compatibility(VT102);
		if (curs.y <= marg_b)
		    scroll (curs.y, marg_b, -def(esc_args[0], 1), FALSE);
                fix_cpos;
		seen_disp_event = TRUE;
		break;
	      case 'M':		       /* delete lines */
	        compatibility(VT102);
		if (curs.y <= marg_b)
		    scroll (curs.y, marg_b, def(esc_args[0], 1), TRUE);
                fix_cpos;
		seen_disp_event = TRUE;
		break;
	      case '@':		       /* insert chars */
		/* XXX VTTEST says this is vt220, vt510 manual says vt102 */
	        compatibility(VT102);	
		insch (def(esc_args[0], 1));
		seen_disp_event = TRUE;
		break;
	      case 'P':		       /* delete chars */
	        compatibility(VT102);	
		insch (-def(esc_args[0], 1));
		seen_disp_event = TRUE;
		break;
	      case 'c':		       /* terminal type query */
	        compatibility(VT100);
		/* This is the response for a VT102 */
		ldisc_send (id_string, strlen(id_string));
		break;
	      case 'n':		       /* cursor position query */
		if (esc_args[0] == 6) {
		    char buf[32];
		    sprintf (buf, "\033[%d;%dR", curs.y + 1, curs.x + 1);
		    ldisc_send (buf, strlen(buf));
		}
		else if (esc_args[0] == 5) {
		    ldisc_send ("\033[0n", 4);
		}
		break;
	      case 'h':		       /* toggle modes to high */
	      case ANSI_QUE('h'):
	        compatibility(VT100);
		{
		    int i;
		    for (i=0; i<esc_nargs; i++)
		        toggle_mode (esc_args[i], esc_query, TRUE);
		}
		break;
	      case 'l':		       /* toggle modes to low */
	      case ANSI_QUE('l'):
	        compatibility(VT100);
		{
		    int i;
		    for (i=0; i<esc_nargs; i++)
		        toggle_mode (esc_args[i], esc_query, FALSE);
		}
		break;
	      case 'g':		       /* clear tabs */
		compatibility(VT100);
		if (esc_nargs == 1) {
		    if (esc_args[0] == 0) {
			tabs[curs.x] = FALSE;
		    } else if (esc_args[0] == 3) {
			int i;
			for (i = 0; i < cols; i++)
			    tabs[i] = FALSE;
		    }
		}
		break;
	      case 'r':		       /* set scroll margins */
		compatibility(VT100);
		if (esc_nargs <= 2) {
		    int top, bot;
		    top = def(esc_args[0], 1) - 1;
		    bot = (esc_nargs <= 1 || esc_args[1] == 0 ? rows :
			   def(esc_args[1], rows)) - 1;
		    if (bot >= rows)
			bot = rows-1;
		    /* VTTEST Bug 9 - if region is less than 2 lines
		     * don't change region.
		     */
		    if (bot-top > 0) {
			marg_t = top;
			marg_b = bot;
			curs.x = 0;
			/*
			 * I used to think the cursor should be
			 * placed at the top of the newly marginned
			 * area. Apparently not: VMS TPU falls over
			 * if so.
			 *
			 * Well actually it should for Origin mode - RDB
			 */
			curs.y = (dec_om ? marg_t : 0);
			fix_cpos;
			seen_disp_event = TRUE;
		    }
		}
		break;
	      case 'm':		       /* set graphics rendition */
		{
		    /* 
		     * A VT100 without the AVO only had one attribute, either
		     * underline or reverse video depending on the cursor type,
		     * this was selected by CSI 7m.
		     *
		     * case 2:
		     *  This is DIM on the VT100-AVO and VT102
		     * case 5:
		     *  This is BLINK on the VT100-AVO and VT102+
		     * case 8:
		     *  This is INVIS on the VT100-AVO and VT102
		     * case 21:
		     *  This like 22 disables BOLD, DIM and INVIS
		     *
		     * The ANSI colours appear on any terminal that has colour
		     * (obviously) but the interaction between sgr0 and the
		     * colours varies but is usually related to the background
		     * colour erase item.
		     * The interaction between colour attributes and the mono
		     * ones is also very implementation dependent.
		     *
		     * The 39 and 49 attributes are likely to be unimplemented.
		     */
		    int i;
		    for (i=0; i<esc_nargs; i++) {
			switch (def(esc_args[i], 0)) {
			  case 0:      /* restore defaults */
			    curr_attr = ATTR_DEFAULT; break;
			  case 1:      /* enable bold */
			    compatibility(VT100AVO);
			    curr_attr |= ATTR_BOLD; break;
			  case 21:     /* (enable double underline) */
			    compatibility(OTHER);
			  case 4:      /* enable underline */
			    compatibility(VT100AVO);
			    curr_attr |= ATTR_UNDER; break;
			  case 5:      /* enable blink */
			    compatibility(VT100AVO);
			    curr_attr |= ATTR_BLINK; break;
			  case 7:      /* enable reverse video */
			    curr_attr |= ATTR_REVERSE; break;
			  case 22:     /* disable bold */
			    compatibility2(OTHER,VT220);
			    curr_attr &= ~ATTR_BOLD; break;
			  case 24:     /* disable underline */
			    compatibility2(OTHER,VT220);
			    curr_attr &= ~ATTR_UNDER; break;
			  case 25:     /* disable blink */
			    compatibility2(OTHER,VT220);
			    curr_attr &= ~ATTR_BLINK; break;
			  case 27:     /* disable reverse video */
			    compatibility2(OTHER,VT220);
			    curr_attr &= ~ATTR_REVERSE; break;
			  case 30: case 31: case 32: case 33:
			  case 34: case 35: case 36: case 37:
			    /* foreground */
			    curr_attr &= ~ATTR_FGMASK;
			    curr_attr |= (esc_args[i] - 30) << ATTR_FGSHIFT;
			    break;
			  case 39:     /* default-foreground */
			    curr_attr &= ~ATTR_FGMASK;
			    curr_attr |= ATTR_DEFFG;
			    break;
			  case 40: case 41: case 42: case 43:
			  case 44: case 45: case 46: case 47:
			    /* background */
			    curr_attr &= ~ATTR_BGMASK;
			    curr_attr |= (esc_args[i] - 40) << ATTR_BGSHIFT;
			    break;
			  case 49:     /* default-background */
			    curr_attr &= ~ATTR_BGMASK;
			    curr_attr |= ATTR_DEFBG;
			    break;
			}
		    }
		    if (use_bce) 
		       erase_char = 
			    (' '|
			      (curr_attr&(ATTR_FGMASK|ATTR_BGMASK|ATTR_BLINK))
			    );
		}
		break;
	      case 's':		       /* save cursor */
		save_cursor (TRUE);
		break;
	      case 'u':		       /* restore cursor */
		save_cursor (FALSE);
		seen_disp_event = TRUE;
		break;
	      case 't':		       /* set page size - ie window height */
		/*
		 * VT340/VT420 sequence DECSLPP, DEC only allows values
		 *  24/25/36/48/72/144 other emulators (eg dtterm) use
		 * illegal values (eg first arg 1..9) for window changing 
		 * and reports.
		 */
		compatibility(VT340TEXT);
		if (esc_nargs<=1 && (esc_args[0]<1 || esc_args[0]>=24)) {
		    request_resize (cols, def(esc_args[0], 24), 0);
		    deselect();
		}
		break;
	      case ANSI('|', '*'):
		/* VT420 sequence DECSNLS
		 * Set number of lines on screen
		 * VT420 uses VGA like hardware and can support any size in
		 * reasonable range (24..49 AIUI) with no default specified.
		 */
		compatibility(VT420);
		if (esc_nargs==1 && esc_args[0]>0) {
		    request_resize (cols, def(esc_args[0], cfg.height), 0);
		    deselect();
		}
		break;
	      case ANSI('|', '$'):
		/* VT340/VT420 sequence DECSCPP
		 * Set number of columns per page
		 * Docs imply range is only 80 or 132, but I'll allow any.
		 */
		compatibility(VT340TEXT);
		if (esc_nargs<=1) {
		    request_resize (def(esc_args[0], cfg.width), rows, 0);
		    deselect();
		}
		break;
	      case 'X':		       /* write N spaces w/o moving cursor */
		/* XXX VTTEST says this is vt220, vt510 manual says vt100 */
		compatibility(ANSIMIN);
		{
		    int n = def(esc_args[0], 1);
		    pos cursplus;
		    unsigned long *p = cpos;
		    if (n > cols - curs.x)
			n = cols - curs.x;
		    cursplus = curs;
		    cursplus.x += n;
		    check_selection (curs, cursplus);
		    while (n--)
			*p++ = erase_char;
		    seen_disp_event = TRUE;
		}
		break;
	      case 'x':		       /* report terminal characteristics */
		compatibility(VT100);
		{
		    char buf[32];
		    int i = def(esc_args[0], 0);
		    if (i == 0 || i == 1) {
			strcpy (buf, "\033[2;1;1;112;112;1;0x");
			buf[2] += i;
			ldisc_send (buf, 20);
		    }
		}
		break;
	      case ANSI('L','='):
		compatibility(OTHER);
		use_bce = (esc_args[0]<=0);
		erase_char = ERASE_CHAR;
		if (use_bce)
		    erase_char = (' '|(curr_attr&(ATTR_FGMASK|ATTR_BGMASK)));
		break;
	      case ANSI('E','='):
		compatibility(OTHER);
		blink_is_real = (esc_args[0]>=1);
		break;
	      case ANSI('p','"'):
		/* Allow the host to make this emulator a 'perfect' VT102.
		 * This first appeared in the VT220, but we do need to get 
		 * back to PuTTY mode so I won't check it.
		 *
		 * The arg in 40..42 are a PuTTY extension.
		 * The 2nd arg, 8bit vs 7bit is not checked.
		 *
		 * Setting VT102 mode should also change the Fkeys to
		 * generate PF* codes as a real VT102 has no Fkeys.
		 * The VT220 does this, F11..F13 become ESC,BS,LF other Fkeys
		 * send nothing.
		 *
		 * Note ESC c will NOT change this!
		 */

		switch (esc_args[0]) {
		case 61: compatibility_level &= ~TM_VTXXX;
		         compatibility_level |=  TM_VT102;	break;
		case 62: compatibility_level &= ~TM_VTXXX;
		         compatibility_level |=  TM_VT220;	break;

		default: if( esc_args[0] > 60 && esc_args[0] < 70 )
		            compatibility_level |= TM_VTXXX; 	
			 break;

		case 40: compatibility_level &=  TM_VTXXX;	break;
		case 41: compatibility_level  =  TM_PUTTY;	break;
		case 42: compatibility_level  =  TM_SCOANSI;	break;

		case ARG_DEFAULT: 
			 compatibility_level  =  TM_PUTTY;	break;
		case 50: break;
		}

		/* Change the response to CSI c */
		if (esc_args[0] == 50) {
		   int i;
		   char lbuf[64];
		   strcpy(id_string, "\033[?");
		   for (i=1; i<esc_nargs; i++) {
		      if (i!=1) strcat(id_string, ";");
		      sprintf(lbuf, "%d", esc_args[i]);
		      strcat(id_string, lbuf);
		   }
		   strcat(id_string, "c");
		}

#if 0
		/* Is this a good idea ? 
		 * Well we should do a soft reset at this point ...
		 */
		if (!has_compat(VT420) && has_compat(VT100)) {
		    if (reset_132) request_resize (132, 24, 1);
		    else           request_resize ( 80, 24, 1);
		}
#endif
		break;
	    }
	    break;
	  case SET_GL:
	  case SET_GR:
	    /* VT100 only here, checked above */
	    switch (c) {
	      case 'A':
		cset_attr[termstate == SET_GL ? 0 : 1] = ATTR_GBCHR;
		break;
	      case '0':
		cset_attr[termstate == SET_GL ? 0 : 1] = ATTR_LINEDRW;
		break;
	      case 'B':
	      default:		       /* specifically, 'B' */
		cset_attr[termstate == SET_GL ? 0 : 1] = ATTR_ASCII;
		break;
	    }
	    if( !has_compat(VT220) || c != '%' )
	        termstate = TOPLEVEL;
	    break;
	  case SEEN_OSC:
	    osc_w = FALSE;
	    switch (c) {
	      case 'P':		       /* Linux palette sequence */
		termstate = SEEN_OSC_P;
		osc_strlen = 0;
		break;
	      case 'R':		       /* Linux palette reset */
		palette_reset();
		term_invalidate();
		termstate = TOPLEVEL;
		break;
	      case 'W':		       /* word-set */
		termstate = SEEN_OSC_W;
		osc_w = TRUE;
		break;
	      case '0': case '1': case '2': case '3': case '4':
	      case '5': case '6': case '7': case '8': case '9':
		esc_args[0] = 10 * esc_args[0] + c - '0';
		break;
	      case 'L':
		/*
		 * Grotty hack to support xterm and DECterm title
		 * sequences concurrently.
		 */
		if (esc_args[0] == 2) {
		    esc_args[0] = 1;
		    break;
		}
		/* else fall through */
	      default:
		termstate = OSC_STRING;
		osc_strlen = 0;
	    }
	    break;
	  case OSC_STRING:
	    /*
	     * This OSC stuff is EVIL. It takes just one character to get into
	     * sysline mode and it's not initially obvious how to get out.
	     * So I've added CR and LF as string aborts.
	     * This shouldn't effect compatibility as I believe embedded 
	     * control characters are supposed to be interpreted (maybe?) 
	     * and they don't display anything useful anyway.
	     *
	     * -- RDB
	     */
	    if (c == '\n' || c == '\r') {
		termstate = TOPLEVEL;
	    } else if (c == 0234 || c == '\007' ) {
		/*
		 * These characters terminate the string; ST and BEL
		 * terminate the sequence and trigger instant
		 * processing of it, whereas ESC goes back to SEEN_ESC
		 * mode unless it is followed by \, in which case it is
		 * synonymous with ST in the first place.
		 */
		do_osc();
		termstate = TOPLEVEL;
	    } else if (c == '\033')
		    termstate = OSC_MAYBE_ST;
	    else if (osc_strlen < OSC_STR_MAX)
		osc_string[osc_strlen++] = c;
	    break;
	  case SEEN_OSC_P:
	    {
		int max = (osc_strlen == 0 ? 21 : 16);
		int val;
		if (c >= '0' && c <= '9')
		    val = c - '0';
		else if (c >= 'A' && c <= 'A'+max-10)
		    val = c - 'A' + 10;
		else if (c >= 'a' && c <= 'a'+max-10)
		    val = c - 'a' + 10;
		else
		    termstate = TOPLEVEL;
		osc_string[osc_strlen++] = val;
		if (osc_strlen >= 7) {
		    palette_set (osc_string[0],
				 osc_string[1] * 16 + osc_string[2],
				 osc_string[3] * 16 + osc_string[4],
				 osc_string[5] * 16 + osc_string[6]);
		    term_invalidate();
		    termstate = TOPLEVEL;
		}
	    }
	    break;
	  case SEEN_OSC_W:
	    switch (c) {
	      case '0': case '1': case '2': case '3': case '4':
	      case '5': case '6': case '7': case '8': case '9':
		esc_args[0] = 10 * esc_args[0] + c - '0';
		break;
	      default:
		termstate = OSC_STRING;
		osc_strlen = 0;
	    }
	    break;
	  case SEEN_ESCHASH:
	    {
		unsigned long nlattr;
		unsigned long *ldata;
		int i, j;
		pos scrtop, scrbot;

		switch (c) {
		case '8':
		    for (i = 0; i < rows; i++) {
			ldata = lineptr(i);
			for (j = 0; j < cols; j++)
			    ldata[j] = ATTR_DEFAULT | 'E';
			ldata[cols] = 0;
		    }
		    disptop = 0;
		    seen_disp_event = TRUE;
		    scrtop.x = scrtop.y = 0;
		    scrbot.x = 0; scrbot.y = rows;
		    check_selection (scrtop, scrbot);
		    break;

		case '3': nlattr = LATTR_TOP; goto lattr_common;
		case '4': nlattr = LATTR_BOT; goto lattr_common;
		case '5': nlattr = LATTR_NORM; goto lattr_common;
		case '6': nlattr = LATTR_WIDE;
		    lattr_common:

		    ldata = lineptr(curs.y);
		    ldata[cols] &= ~LATTR_MODE;
		    ldata[cols] |=  nlattr;
		}
	    }
	    termstate = TOPLEVEL;
	    break;
	  case VT52_ESC:
	    termstate = TOPLEVEL;
	    seen_disp_event = TRUE;
	    switch (c) {
	      case 'A':
		move (curs.x, curs.y - 1, 1);
		break;
	      case 'B':
		move (curs.x, curs.y + 1, 1);
		break;
	      case 'C':
		move (curs.x + 1, curs.y, 1);
		break;
	      case 'D':
		move (curs.x - 1, curs.y, 1);
		break;
	      case 'F':
		cset_attr[cset=0] = ATTR_LINEDRW;
		break;
	      case 'G':
		cset_attr[cset=0] = ATTR_ASCII;
		break;
	      case 'H':
		move (0, 0, 0);
		break;
	      case 'I':
		if (curs.y == 0)
		    scroll (0, rows-1, -1, TRUE);
		else if (curs.y > 0)
		    curs.y--;
		fix_cpos;
		wrapnext = FALSE;
		break;
	      case 'J':
		erase_lots(FALSE, FALSE, TRUE);
		disptop = 0;
		break;
	      case 'K':
		erase_lots(TRUE, FALSE, TRUE);
		break;
	      case 'V':
		/* XXX Print cursor line */
		break;
	      case 'W':
		/* XXX Start controller mode */
		break;
	      case 'X':
		/* XXX Stop controller mode */
		break;
	      case 'Y':
	        termstate = VT52_Y1;
		break;
	      case 'Z':
		ldisc_send ("\033/Z", 3);
		break;
	      case '=':
	        app_keypad_keys = TRUE;
		break;
	      case '>':
	        app_keypad_keys = FALSE;
		break;
	      case '<':
		/* XXX This should switch to VT100 mode not current or default
		 *     VT mode. But this will only have effect in a VT220+
		 *     emulation.
		 */
	        vt52_mode = FALSE;
		break;
	      case '^':
		/* XXX Enter auto print mode */
		break;
	      case '_':
		/* XXX Exit auto print mode */
		break;
	      case ']':
		/* XXX Print screen */
		break;
	    }
	    break;
	  case VT52_Y1:
	    termstate = VT52_Y2;
	    move(curs.x, c-' ', 0);
	    break;
	  case VT52_Y2:
	    termstate = TOPLEVEL;
	    move(c-' ', curs.y, 0);
	    break;
	}
	if (selstate != NO_SELECTION) {
	    pos cursplus = curs;
	    incpos(cursplus);
	    check_selection (curs, cursplus);
	}
    }
    inbuf_head = 0;
}

/*
 * Compare two lines to determine whether they are sufficiently
 * alike to scroll-optimise one to the other. Return the degree of
 * similarity.
 */
static int linecmp (unsigned long *a, unsigned long *b) {
    int i, n;

    for (i=n=0; i < cols; i++)
	n += (*a++ == *b++);
    return n;
}

/*
 * Given a context, update the window. Out of paranoia, we don't
 * allow WM_PAINT responses to do scrolling optimisations.
 */
static void do_paint (Context ctx, int may_optimise) {
    int i, j, start, our_curs_y;
    unsigned long attr, rv, cursor;
    pos scrpos;
    char ch[1024];
    long ticks;

    /*
     * Check the visual bell state.
     */
    if (in_vbell) {
	ticks = GetTickCount();
	if (ticks - vbell_timeout >= 0)
	    in_vbell = FALSE;
    }

    /* Depends on:
     * screen array, disptop, scrtop,
     * selection, rv, 
     * cfg.blinkpc, blink_is_real, tblinker, 
     * curs.y, curs.x, blinker, cfg.blink_cur, cursor_on, has_focus
     */
    if (cursor_on) {
        if (has_focus) {
	    if (blinker || !cfg.blink_cur)
                cursor = ATTR_ACTCURS;
            else
                cursor = 0;
        }
        else
            cursor = ATTR_PASCURS;
	if (wrapnext)
	    cursor |= ATTR_RIGHTCURS;
    }
    else
	cursor = 0;
    rv = (!rvideo ^ !in_vbell ? ATTR_REVERSE : 0);
    our_curs_y = curs.y - disptop;

    for (i=0; i<rows; i++) {
	unsigned long *ldata;
	int lattr;
	scrpos.y = i + disptop;
	ldata = lineptr(scrpos.y);
	lattr = (ldata[cols] & LATTR_MODE);
	for (j=0; j<=cols; j++) {
	    unsigned long d = ldata[j];
	    int idx = i*(cols+1)+j;
	    scrpos.x = j;
	    
	    wanttext[idx] = lattr | (((d &~ ATTR_WRAPPED) ^ rv
				      ^ (posle(selstart, scrpos) &&
					 poslt(scrpos, selend) ?
					 ATTR_REVERSE : 0)) |
				     (i==our_curs_y && j==curs.x ? cursor : 0));
	    if (blink_is_real) {
		if (has_focus && tblinker && (wanttext[idx]&ATTR_BLINK) )
		{
		    wanttext[idx] &= ATTR_MASK;
		    wanttext[idx] += ' ';
		}
		wanttext[idx] &= ~ATTR_BLINK;
	    }
	}
    }

    /*
     * We would perform scrolling optimisations in here, if they
     * didn't have a nasty tendency to cause the whole sodding
     * program to hang for a second at speed-critical moments.
     * We'll leave it well alone...
     */

    for (i=0; i<rows; i++) {
	int idx = i*(cols+1);
	int lattr = (wanttext[idx+cols] & LATTR_MODE);
	start = -1;
	for (j=0; j<=cols; j++,idx++) {
	    unsigned long t = wanttext[idx];
	    int needs_update = (j < cols && t != disptext[idx]);
	    int keep_going = (start != -1 && needs_update &&
			      (t & ATTR_MASK) == attr &&
			      j-start < sizeof(ch));
	    if (start != -1 && !keep_going) {
		do_text (ctx, start, i, ch, j-start, attr, lattr);
		start = -1;
	    }
	    if (needs_update) {
		if (start == -1) {
		    start = j;
		    attr = t & ATTR_MASK;
		}
		ch[j-start] = (char) (t & CHAR_MASK);
	    }
	    disptext[idx] = t;
	}
    }
}

/*
 * Flick the switch that says if blinking things should be shown or hidden.
 */

void term_blink(int flg) {
    static long last_blink = 0;
    static long last_tblink = 0;
    long now, blink_diff;

    now = GetTickCount();
    blink_diff = now-last_tblink;

    /* Make sure the text blinks no more than 2Hz */
    if (blink_diff<0 || blink_diff>450)
    {
        last_tblink = now;
	tblinker = !tblinker;
    }

    if (flg) {
        blinker = 1;
        last_blink = now;
	return;
    } 

    blink_diff = now-last_blink;

    /* Make sure the cursor blinks no faster than GetCaretBlinkTime() */
    if (blink_diff>=0 && blink_diff<(long)GetCaretBlinkTime())
       return;
 
    last_blink = now;
    blinker = !blinker;
}

/*
 * Invalidate the whole screen so it will be repainted in full.
 */
void term_invalidate(void) {
    int i;

    for (i=0; i<rows*(cols+1); i++)
	disptext[i] = ATTR_INVALID;
}

/*
 * Paint the window in response to a WM_PAINT message.
 */
void term_paint (Context ctx, int l, int t, int r, int b) {
    int i, j, left, top, right, bottom;

    left = l / font_width;
    right = (r - 1) / font_width;
    top = t / font_height;
    bottom = (b - 1) / font_height;
    for (i = top; i <= bottom && i < rows ; i++)
    {
	if ( (disptext[i*(cols+1)+cols]&LATTR_MODE) == LATTR_NORM)
	    for (j = left; j <= right && j < cols ; j++)
		disptext[i*(cols+1)+j] = ATTR_INVALID;
	else
	    for (j = left/2; j <= right/2+1 && j < cols ; j++)
		disptext[i*(cols+1)+j] = ATTR_INVALID;
    }

    /* This should happen soon enough, also for some reason it sometimes 
     * fails to actually do anything when re-sizing ... painting the wrong
     * window perhaps ?
    do_paint (ctx, FALSE);
    */
}

/*
 * Attempt to scroll the scrollback. The second parameter gives the
 * position we want to scroll to; the first is +1 to denote that
 * this position is relative to the beginning of the scrollback, -1
 * to denote it is relative to the end, and 0 to denote that it is
 * relative to the current position.
 */
void term_scroll (int rel, int where) {
    int sbtop = -count234(scrollback);

    disptop = (rel < 0 ? 0 :
	       rel > 0 ? sbtop : disptop) + where;
    if (disptop < sbtop)
	disptop = sbtop;
    if (disptop > 0)
	disptop = 0;
    update_sbar();
    term_update();
}

static void clipme(pos top, pos bottom, char *workbuf) {
    char *wbptr;		/* where next char goes within workbuf */
    int wblen = 0;		/* workbuf len */
    int buflen;			/* amount of memory allocated to workbuf */

    if ( workbuf != NULL ) {	/* user supplied buffer? */
	buflen = -1;		/* assume buffer passed in is big enough */
	wbptr = workbuf;	/* start filling here */
    }
    else
	buflen = 0;		/* No data is available yet */

    while (poslt(top, bottom)) {
	int nl = FALSE;
	unsigned long *ldata = lineptr(top.y);
	pos nlpos;

	nlpos.y = top.y;
	nlpos.x = cols;

	if (!(ldata[cols] & ATTR_WRAPPED)) {
	    while ((ldata[nlpos.x-1] & CHAR_MASK) == 0x20 && poslt(top, nlpos))
		decpos(nlpos);
	    if (poslt(nlpos, bottom))
		nl = TRUE;
	}
	while (poslt(top, bottom) && poslt(top, nlpos)) {
	    int ch = (ldata[top.x] & CHAR_MASK);
	    int set = (ldata[top.x] & CSET_MASK);

	    /* VT Specials -> ISO8859-1 for Cut&Paste */
	    static const unsigned char poorman2[] =
"* # HTFFCRLF\xB0 \xB1 NLVT+ + + + + - - - - - + + + + | <=>=PI!=\xA3 \xB7 ";

	    if (set && !cfg.rawcnp) {
	        if (set == ATTR_LINEDRW && ch >= 0x60 && ch < 0x7F) {
		    int x;
		    if ((x = poorman2[2*(ch-0x60)+1]) == ' ')
		        x = 0;
		    ch = (x<<8) + poorman2[2*(ch-0x60)];
	        }
	    }

	    while(ch != 0) {
		if (cfg.rawcnp || !!(ch&0xE0)) {
		    if ( wblen == buflen )
		    {
		        workbuf = srealloc(workbuf, buflen += 100);
		        wbptr = workbuf + wblen;
		    }
		    wblen++;
		    *wbptr++ = (unsigned char) ch;
		}
		ch>>=8;
	    }
	    top.x++;
	}
	if (nl) {
	    int i;
	    for (i=0; i<sizeof(sel_nl); i++)
	    {
		if ( wblen == buflen )
		{
		    workbuf = srealloc(workbuf, buflen += 100);
		    wbptr = workbuf + wblen;
		}
	        wblen++;
		*wbptr++ = sel_nl[i];
	    }
	}
	top.y++;
	top.x = 0;
    }
    write_clip (workbuf, wblen, FALSE);	/* transfer to clipboard */
    if ( buflen > 0 )	/* indicates we allocated this buffer */
	sfree(workbuf);

}
void term_copyall (void) {
    pos top;
    top.y = -count234(scrollback);
    top.x = 0;
    clipme(top, curs, NULL /* dynamic allocation */);
}

/*
 * Spread the selection outwards according to the selection mode.
 */
static pos sel_spread_half (pos p, int dir) {
    unsigned long *ldata;
    short wvalue;

    ldata = lineptr(p.y);

    switch (selmode) {
      case SM_CHAR:
	/*
	 * In this mode, every character is a separate unit, except
	 * for runs of spaces at the end of a non-wrapping line.
	 */
	if (!(ldata[cols] & ATTR_WRAPPED)) {
	    unsigned long *q = ldata+cols;
	    while (q > ldata && (q[-1] & CHAR_MASK) == 0x20)
		q--;
	    if (q == ldata+cols)
		q--;
	    if (p.x >= q-ldata)
		p.x = (dir == -1 ? q-ldata : cols - 1);
	}
	break;
      case SM_WORD:
	/*
	 * In this mode, the units are maximal runs of characters
	 * whose `wordness' has the same value.
	 */
	wvalue = wordness[ldata[p.x] & CHAR_MASK];
	if (dir == +1) {
	    while (p.x < cols && wordness[ldata[p.x+1] & CHAR_MASK] == wvalue)
		p.x++;
	} else {
	    while (p.x > 0 && wordness[ldata[p.x-1] & CHAR_MASK] == wvalue)
		p.x--;
	}
	break;
      case SM_LINE:
	/*
	 * In this mode, every line is a unit.
	 */
	p.x = (dir == -1 ? 0 : cols - 1);
	break;
    }
    return p;
}

static void sel_spread (void) {
    selstart = sel_spread_half (selstart, -1);
    decpos(selend);
    selend = sel_spread_half (selend, +1);
    incpos(selend);
}

void term_mouse (Mouse_Button b, Mouse_Action a, int x, int y) {
    pos selpoint;
    unsigned long *ldata;
    
    if (y<0) y = 0;
    if (y>=rows) y = rows-1;
    if (x<0) {
        if (y > 0) {
            x = cols-1;
            y--;
        } else
            x = 0;
    }
    if (x>=cols) x = cols-1;

    selpoint.y = y + disptop;
    selpoint.x = x;
    ldata = lineptr(selpoint.y);
    if ((ldata[cols]&LATTR_MODE) != LATTR_NORM)
	selpoint.x /= 2;

    if (b == MB_SELECT && a == MA_CLICK) {
	deselect();
	selstate = ABOUT_TO;
	selanchor = selpoint;
	selmode = SM_CHAR;
    } else if (b == MB_SELECT && (a == MA_2CLK || a == MA_3CLK)) {
	deselect();
	selmode = (a == MA_2CLK ? SM_WORD : SM_LINE);
	selstate = DRAGGING;
	selstart = selanchor = selpoint;
	selend = selstart;
	incpos(selend);
	sel_spread();
    } else if ((b == MB_SELECT && a == MA_DRAG) ||
	       (b == MB_EXTEND && a != MA_RELEASE)) {
	if (selstate == ABOUT_TO && poseq(selanchor, selpoint))
	    return;
	if (b == MB_EXTEND && a != MA_DRAG && selstate == SELECTED) {
	    if (posdiff(selpoint,selstart) < posdiff(selend,selstart)/2) {
		selanchor = selend;
		decpos(selanchor);
	    } else {
		selanchor = selstart;
	    }
	    selstate = DRAGGING;
	}
	if (selstate != ABOUT_TO && selstate != DRAGGING)
	    selanchor = selpoint;
	selstate = DRAGGING;
	if (poslt(selpoint, selanchor)) {
	    selstart = selpoint;
	    selend = selanchor;
	    incpos(selend);
	} else {
	    selstart = selanchor;
	    selend = selpoint;
	    incpos(selend);
	}
	sel_spread();
    } else if ((b == MB_SELECT || b == MB_EXTEND) && a == MA_RELEASE) {
	if (selstate == DRAGGING) {
	    /*
	     * We've completed a selection. We now transfer the
	     * data to the clipboard.
	     */
	    clipme(selstart, selend, selspace);
	    selstate = SELECTED;
	} else
	    selstate = NO_SELECTION;
    } else if (b == MB_PASTE && (a==MA_CLICK || a==MA_2CLK || a==MA_3CLK)) {
	char *data;
	int len;

	get_clip((void **) &data, &len);
	if (data) {
	    char *p, *q;

	    if (paste_buffer) sfree(paste_buffer);
	    paste_pos = paste_hold = paste_len = 0;
	    paste_buffer = smalloc(len);

	    p = q = data;
	    while (p < data+len) {
		while (p < data+len &&
		       !(p <= data+len-sizeof(sel_nl) &&
			 !memcmp(p, sel_nl, sizeof(sel_nl))))
		    p++;

		{
		    int i;
		    unsigned char c;
		    for(i=0;i<p-q;i++)
		    {
			c=xlat_kbd2tty(q[i]);
			paste_buffer[paste_len++] = c;
		    }
		}

		if (p <= data+len-sizeof(sel_nl) &&
		    !memcmp(p, sel_nl, sizeof(sel_nl))) {
		    paste_buffer[paste_len++] = '\r';
		    p += sizeof(sel_nl);
		}
		q = p;
	    }

	    /* Assume a small paste will be OK in one go. */
	    if (paste_len<256) {
	        ldisc_send (paste_buffer, paste_len);
	        if (paste_buffer) sfree(paste_buffer);
		paste_buffer = 0;
	        paste_pos = paste_hold = paste_len = 0;
	    }
	}
	get_clip(NULL, NULL);
    }

    term_update();
}

void term_nopaste() {
    if(paste_len == 0) return;
    sfree(paste_buffer);
    paste_buffer = 0;
    paste_len = 0;
}

void term_paste() {
    static long last_paste = 0;
    long now, paste_diff;

    if(paste_len == 0) return;

    /* Don't wait forever to paste */
    if(paste_hold) {
    	now = GetTickCount();
    	paste_diff = now-last_paste;
	if (paste_diff>=0 && paste_diff<450)
	    return;
    }
    paste_hold = 0;

    while(paste_pos<paste_len)
    {
	int n = 0;
	while (n + paste_pos < paste_len) {
	    if (paste_buffer[paste_pos + n++] == '\r')
		break;
	}
	ldisc_send (paste_buffer+paste_pos, n);
	paste_pos += n;

	if (paste_pos < paste_len) {
	    paste_hold = 1;
	    return;
	}
    }
    sfree(paste_buffer);
    paste_buffer = 0;
    paste_len = 0;
}

static void deselect (void) {
    selstate = NO_SELECTION;
    selstart.x = selstart.y = selend.x = selend.y = 0;
}

void term_deselect (void) {
    deselect();
    term_update();
}

int term_ldisc(int option) {
    if (option == LD_ECHO) return term_echoing;
    if (option == LD_EDIT) return term_editing;
    return FALSE;
}

/*
 * from_backend(), to get data from the backend for the terminal.
 */
void from_backend(int is_stderr, char *data, int len) {
    while (len--) {
	if (inbuf_head >= INBUF_SIZE)
	    term_out();
	inbuf[inbuf_head++] = *data++;
    }
}

/*
 * Log session traffic.
 */
void logtraffic(unsigned char c, int logmode) {
    if (cfg.logtype > 0) {
	if (cfg.logtype == logmode) {
	    /* deferred open file from pgm start? */
	    if (!lgfp) logfopen();
	    if (lgfp) fputc (c, lgfp);
    	}
    }
}

/* open log file append/overwrite mode */
void logfopen(void) {
    char buf[256];
    time_t t;
    struct tm *tm;
    char writemod[4];

    if (!cfg.logtype)
	return;
    sprintf (writemod, "wb");	       /* default to rewrite */
    lgfp = fopen(cfg.logfilename, "r");  /* file already present? */
    if (lgfp) {
	int i;
	fclose(lgfp);
	i = askappend(cfg.logfilename);
	if (i == 1)
	    writemod[0] = 'a';	       /* set append mode */
	else if (i == 0) {	       /* cancelled */
	    lgfp = NULL;
            cfg.logtype = 0;           /* disable logging */
	    return;
	}
    }

    lgfp = fopen(cfg.logfilename, writemod);
    if (lgfp) { /* enter into event log */
	sprintf(buf, "%s session log (%s mode) to file : ",
		(writemod[0] == 'a') ? "Appending" : "Writing new",
		(cfg.logtype == LGTYP_ASCII ? "ASCII" :
		 cfg.logtype == LGTYP_DEBUG ? "raw" : "<ukwn>")  );
	/* Make sure we do not exceed the output buffer size */
	strncat (buf, cfg.logfilename, 128);
	buf[strlen(buf)] = '\0';
	logevent(buf);

        /* --- write header line iinto log file */
	fputs ("=~=~=~=~=~=~=~=~=~=~=~= PuTTY log ", lgfp);
	time(&t);
	tm = localtime(&t);
	strftime(buf, 24, "%Y.%m.%d %H:%M:%S", tm);
	fputs (buf, lgfp);
	fputs (" =~=~=~=~=~=~=~=~=~=~=~=\r\n", lgfp);
    }
}

void logfclose (void) {
    if (lgfp) { fclose(lgfp); lgfp = NULL; }
}
