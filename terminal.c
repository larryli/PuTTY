#include <windows.h>
#ifndef AUTO_WINSOCK
#ifdef WINSOCK_TWO
#include <winsock2.h>
#else
#include <winsock.h>
#endif
#endif

#include <stdio.h>
#include <stdlib.h>

#include "putty.h"

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

#define TM_ANSIMIN	(CL_ANSIMIN)
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


static unsigned long *text;	       /* buffer of text on terminal screen */
static unsigned long *scrtop;	       /* top of working screen */
static unsigned long *disptop;	       /* top of displayed screen */
static unsigned long *sbtop;	       /* top of scrollback */
static unsigned long *sbbot;	       /* furthest extent of scrollback */
static unsigned long *cpos;	       /* cursor position (convenience) */
static unsigned long *disptext;	       /* buffer of text on real screen */
static unsigned long *wanttext;	       /* buffer of text we want on screen */
static unsigned long *alttext;	       /* buffer of text on alt. screen */

static unsigned char *selspace;	       /* buffer for building selections in */

#define TSIZE (sizeof(*text))
#define fix_cpos  do { cpos = scrtop + curs_y * (cols+1) + curs_x; } while(0)

static unsigned long curr_attr, save_attr;
static unsigned long erase_char = ERASE_CHAR;

static int curs_x, curs_y;	       /* cursor */
static int save_x, save_y;	       /* saved cursor position */
static int marg_t, marg_b;	       /* scroll margins */
static int dec_om;		       /* DEC origin mode flag */
static int wrap, wrapnext;	       /* wrap flags */
static int insert;		       /* insert-mode flag */
static int cset;		       /* 0 or 1: which char set */
static int save_cset, save_csattr;     /* saved with cursor position */
static int rvideo;		       /* global reverse video flag */
static int cursor_on;		       /* cursor enabled flag */
static int reset_132;		       /* Flag ESC c resets to 80 cols */
static int use_bce;		       /* Use Background coloured erase */
static int blinker;		       /* When blinking is the cursor on ? */
static int tblinker;		       /* When the blinking text is on */
static int blink_is_real;	       /* Actually blink blinking text */

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
static unsigned long *selstart, *selend, *selanchor;

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

/*
 * Set up power-on settings for the terminal.
 */
static void power_on(void) {
    curs_x = curs_y = alt_x = alt_y = save_x = save_y = 0;
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
    cursor_on = 1;
    save_attr = curr_attr = ATTR_DEFAULT;
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
    if (text) {
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
	     (seen_disp_event && (!cfg.scroll_on_key)) ) {
	    disptop = scrtop;
	    seen_disp_event = seen_key_event = 0;
	}
	do_paint (ctx, TRUE);
        sys_cursor(curs_x, curs_y + (scrtop - disptop) / (cols+1));
	free_ctx (ctx);
    }
}

/*
 * Same as power_on(), but an external function.
 */
void term_pwron(void) {
    power_on();
    fix_cpos;
    disptop = scrtop;
    deselect();
    term_update();
}

/*
 * Clear the scrollback.
 */
void term_clrsb(void) {
    disptop = sbtop = scrtop;
    update_sbar();
}

/*
 * Initialise the terminal.
 */
void term_init(void) {
    text = sbtop = sbbot = scrtop = disptop = cpos = NULL;
    disptext = wanttext = NULL;
    tabs = NULL;
    selspace = NULL;
    deselect();
    rows = cols = -1;
    power_on();
}

/*
 * Set up the terminal for a given size.
 */
void term_size(int newrows, int newcols, int newsavelines) {
    unsigned long *newtext, *newdisp, *newwant, *newalt;
    int i, j, crows, ccols;

    int save_alt_which = alt_which;

    if (newrows == rows && newcols == cols && newsavelines == savelines)
	return;			       /* nothing to do */

    deselect();
    swap_screen(0);

    alt_t = marg_t = 0;
    alt_b = marg_b = newrows - 1;

    newtext = smalloc ((newrows+newsavelines)*(newcols+1)*TSIZE);
    sbbot = newtext + newsavelines*(newcols+1);
    for (i=0; i<(newrows+newsavelines)*(newcols+1); i++)
	newtext[i] = erase_char;
    if (rows != -1) {
	crows = rows + (scrtop - sbtop) / (cols+1);
	if (crows > newrows+newsavelines)
	    crows = newrows+newsavelines;
	if (newrows>crows)
	    disptop = newtext;
	else
            disptop = newtext + (crows-newrows)*(newcols+1);
	ccols = (cols < newcols ? cols : newcols);
	for (i=0; i<crows; i++) {
	    int oldidx = (rows - crows + i) * (cols+1);
	    int newidx = (newrows - crows + i) * (newcols+1);

	    for (j=0; j<ccols; j++)
		disptop[newidx+j] = scrtop[oldidx+j];
	    disptop[newidx+newcols] =
		(cols == newcols ? scrtop[oldidx+cols] 
		                 : (scrtop[oldidx+cols]&LATTR_MODE));
	}
	sbtop = newtext;
    } else {
	sbtop = disptop = newtext;
    }
    scrtop = disptop;
    sfree (text);
    text = newtext;

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

    newalt = smalloc (newrows*(newcols+1)*TSIZE);
    for (i=0; i<newrows*(newcols+1); i++)
	newalt[i] = erase_char;
    sfree (alttext);
    alttext = newalt;

    sfree (selspace);
    selspace = smalloc ( (newrows+newsavelines) * (newcols+sizeof(sel_nl)) );

    tabs = srealloc (tabs, newcols*sizeof(*tabs));
    {
	int i;
	for (i = (cols > 0 ? cols : 0); i < newcols; i++)
	    tabs[i] = (i % 8 == 0 ? TRUE : FALSE);
    }

    if (rows > 0)
	curs_y += newrows - rows;
    if (curs_y < 0)
	curs_y = 0;
    if (curs_y >= newrows)
	curs_y = newrows-1;
    if (curs_x >= newcols)
	curs_x = newcols-1;
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
    unsigned long tt;

    if (which == alt_which)
	return;

    alt_which = which;

    for (t=0; t<rows*(cols+1); t++) {
	tt = scrtop[t]; scrtop[t] = alttext[t]; alttext[t] = tt;
    }

    t = curs_x; curs_x = alt_x; alt_x = t;
    t = curs_y; curs_y = alt_y; alt_y = t;
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
    int min;

    min = (sbtop - text) / (cols+1);
    set_sbar ((scrtop - text) / (cols+1) + rows - min,
	      (disptop - text) / (cols+1) - min,
	      rows);
}

/*
 * Check whether the region bounded by the two pointers intersects
 * the scroll region, and de-select the on-screen selection if so.
 */
static void check_selection (unsigned long *from, unsigned long *to) {
    if (from < selend && selstart < to)
	deselect();
}

/*
 * Scroll the screen. (`lines' is +ve for scrolling forward, -ve
 * for backward.) `sb' is TRUE if the scrolling is permitted to
 * affect the scrollback buffer.
 */
static void scroll (int topline, int botline, int lines, int sb) {
    unsigned long *scroll_top;
    unsigned long *newscr;
    int scroll_size, size, i;
    int scrtop_is_disptop = (scrtop==disptop);
static int recursive = 0;

    /* Only scroll more than the window if we're doing a 10% scroll */
    if (!recursive && lines > botline - topline + 1)
        lines = botline - topline + 1;

    scroll_top = scrtop + topline*(cols+1);
    size = (lines < 0 ? -lines : lines) * (cols+1);
    scroll_size = (botline - topline + 1) * (cols+1) - size;

    if (lines > 0 && topline == 0 && alt_which == 0 && sb) {
	/*
	 * Since we're going to scroll the top line and we're on the 
	 * scrolling screen let's also effect the scrollback buffer.
	 *
	 * This is normally done by moving the position the screen
	 * painter reads from to reduce the amount of memory copying
	 * required.
	 */
	if (scroll_size >= 0 && !recursive) {
	    newscr = scrtop + lines * (cols+1);

	    if (newscr > sbbot && botline == rows-1) {
		/* We've hit the bottom of memory, so we have to do a 
		 * physical scroll. But instead of just 1 line do it
		 * by 10% of the available memory.
		 *
		 * If the scroll region isn't the whole screen then we can't
		 * do this as it stands. We would need to recover the bottom
		 * of the screen from the scroll buffer after being sure that
		 * it doesn't get deleted.
		 */

		i = (rows+savelines)/10;

		/* Make it simple and ensure safe recursion */
		if ( i<savelines-1) {
		    recursive ++;
		    scroll(topline, botline, i, sb);
		    recursive --;

	            newscr = scrtop - i * (cols+1);
		    if (scrtop_is_disptop) disptop = newscr;
		    scrtop = newscr;
		}

	        newscr = scrtop + lines * (cols+1);
	    }

	    if (newscr <= sbbot) {
		if (scrtop_is_disptop) disptop = newscr;
		scrtop = newscr;

		if (botline == rows-1 )
		    for (i = 0; i < size; i++)
		        scrtop[i+scroll_size] = erase_char;

		update_sbar();
		fix_cpos;

	        if (botline != rows-1) {
		    /* This fastscroll only works for full window scrolls. 
		     * If the caller wanted a partial one we have to reverse
		     * scroll the bottom of the screen.
		     */
		    scroll(botline-lines+1, rows-1, -lines, 0);
		}
		return ;
	    }

	    /* If we can't scroll by memory remapping do it physically.
	     * But rather than expensivly doing the scroll buffer just
	     * scroll the screen. All it means is that sometimes we choose
	     * to not add lines from a scroll region to the scroll buffer.
	     */

	    if (savelines <= 400) {
	        sbtop -= lines * (cols+1);
	        if (sbtop < text)
		    sbtop = text;
	        scroll_size += scroll_top - sbtop;
	        scroll_top = sbtop;
    
	        update_sbar();
	    }
	} else { 
	    /* Ho hum, expensive scroll required. */

	    sbtop -= lines * (cols+1);
	    if (sbtop < text)
		sbtop = text;
	    scroll_size += scroll_top - sbtop;
	    scroll_top = sbtop;

	    update_sbar();
	}
    }

    if (scroll_size < 0) {
	size += scroll_size;
	scroll_size = 0;
    }

    if (lines > 0) {
	if (scroll_size)
	    memmove (scroll_top, scroll_top + size, scroll_size*TSIZE);
	for (i = 0; i < size; i++)
	    scroll_top[i+scroll_size] = erase_char;
	if (selstart > scroll_top &&
	    selstart < scroll_top + size + scroll_size) {
	    selstart -= size;
	    if (selstart < scroll_top)
		selstart = scroll_top;
	}
	if (selend > scroll_top &&
	    selend < scroll_top + size + scroll_size) {
	    selend -= size;
	    if (selend < scroll_top)
		selend = scroll_top;
	}
	if (scrtop_is_disptop)
	    disptop = scrtop;
	else
	    if (disptop > scroll_top &&
		disptop < scroll_top + size + scroll_size) {
		disptop -= size;
		if (disptop < scroll_top)
		    disptop = scroll_top;
	}
    } else {
	if (scroll_size)
	    memmove (scroll_top + size, scroll_top, scroll_size*TSIZE);
	for (i = 0; i < size; i++)
	    scroll_top[i] = erase_char;
	if (selstart > scroll_top &&
	    selstart < scroll_top + size + scroll_size) {
	    selstart += size;
	    if (selstart > scroll_top + size + scroll_size)
		selstart = scroll_top + size + scroll_size;
	}
	if (selend > scroll_top &&
	    selend < scroll_top + size + scroll_size) {
	    selend += size;
	    if (selend > scroll_top + size + scroll_size)
		selend = scroll_top + size + scroll_size;
	}
	if (scrtop_is_disptop)
	    disptop = scrtop;
	else if (disptop > scroll_top &&
		disptop < scroll_top + size + scroll_size) {
		disptop += size;
		if (disptop > scroll_top + size + scroll_size)
		    disptop = scroll_top + size + scroll_size;
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
	if ((curs_y >= marg_t || marg_clip == 2) && y < marg_t)
	    y = marg_t;
	if ((curs_y <= marg_b || marg_clip == 2) && y > marg_b)
	    y = marg_b;
    }
    if (y < 0)
	y = 0;
    if (y >= rows)
	y = rows-1;
    curs_x = x;
    curs_y = y;
    fix_cpos;
    wrapnext = FALSE;
}

/*
 * Save or restore the cursor and SGR mode.
 */
static void save_cursor(int save) {
    if (save) {
	save_x = curs_x;
	save_y = curs_y;
	save_attr = curr_attr;
	save_cset = cset;
	save_csattr = cset_attr[cset];
    } else {
	curs_x = save_x;
	curs_y = save_y;
	/* Make sure the window hasn't shrunk since the save */
	if (curs_x >= cols) curs_x = cols-1;
	if (curs_y >= rows) curs_y = rows-1;

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
    unsigned long *startpos, *endpos;

    if (line_only) {
	startpos = cpos - curs_x;
	endpos = startpos + cols;
	/* I've removed the +1 so that the Wide screen stuff is not
	 * removed when it shouldn't be.
	 */
    } else {
	startpos = scrtop;
	endpos = startpos + rows * (cols+1);
    }
    if (!from_begin)
	startpos = cpos;
    if (!to_end)
	endpos = cpos+1;
    check_selection (startpos, endpos);

    /* Clear screen also forces a full window redraw, just in case. */
    if (startpos == scrtop && endpos == scrtop + rows * (cols+1))
       term_invalidate();

    while (startpos < endpos)
	*startpos++ = erase_char;
}

/*
 * Insert or delete characters within the current line. n is +ve if
 * insertion is desired, and -ve for deletion.
 */
static void insch (int n) {
    int dir = (n < 0 ? -1 : +1);
    int m;

    n = (n < 0 ? -n : n);
    if (n > cols - curs_x)
	n = cols - curs_x;
    m = cols - curs_x - n;
    check_selection (cpos, cpos+n);
    if (dir < 0) {
	memmove (cpos, cpos+n, m*TSIZE);
	while (n--)
	    cpos[m++] = erase_char;
    } else {
	memmove (cpos+n, cpos, m*TSIZE);
	while (n--)
	    cpos[n] = erase_char;
    }
}

/*
 * Toggle terminal mode `mode' to state `state'. (`query' indicates
 * whether the mode is a DEC private one or a normal one.)
 */
static void toggle_mode (int mode, int query, int state) {
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
      case 25:			       /* enable/disable cursor */
	compatibility(VT220);
	cursor_on = state;
	seen_disp_event = TRUE;
	break;
      case 47:			       /* alternate screen */
	compatibility(OTHER);
	deselect();
	swap_screen (state);
	disptop = scrtop;
	break;
    } else switch (mode) {
      case 4:			       /* set insert mode */
	compatibility(VT102);
	insert = state;
	break;
      case 12:			       /* set echo mode */
	/* 
	 * This may be very good in smcup and rmcup (or smkx & rmkx) if you
	 * have a long RTT and the telnet client/daemon doesn't understand
	 * linemode.
	 *
	 * DONT send TS_RECHO/TS_LECHO; the telnet daemon tries to fix the
	 * tty and _really_ confuses some programs.
	 */
	compatibility(VT220);
        ldisc = (state? &ldisc_simple : &ldisc_term);
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

static int beep_overload = 0;
    int beep_count = 0;

    for(inbuf_reap = 0; inbuf_reap < inbuf_head; inbuf_reap++)
    {
        c = inbuf[inbuf_reap];

	/*
         * Optionally log the session traffic to a file. Useful for
         * debugging and possibly also useful for actual logging.
         */
	if (logfile) {
	    static FILE *fp = NULL;
	    if (!fp) fp = fopen(logfile, "wb");
	    if (fp) fputc (c, fp);
	}
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
	        compatibility(OTHER);
		ldisc->send ("PuTTY", 5);
		break;
	      case '\007':
		beep_count++; 
		if(beep_count>6) beep_overload=1;
		disptop = scrtop;
		break;
	      case '\b':
		if (curs_x == 0 && curs_y == 0)
		    ;
		else if (curs_x == 0 && curs_y > 0)
		    curs_x = cols-1, curs_y--;
		else if (wrapnext)
		    wrapnext = FALSE;
		else
		    curs_x--;
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
		curs_x = 0;
		wrapnext = FALSE;
		fix_cpos;
		seen_disp_event = TRUE;
		paste_hold = 0;
		break;
	      case '\013':
	      case '\014':
	        compatibility(VT100);
	      case '\n':
		if (curs_y == marg_b)
		    scroll (marg_t, marg_b, 1, TRUE);
		else if (curs_y < rows-1)
		    curs_y++;
		if (cfg.lfhascr)
		    curs_x = 0;
		fix_cpos;
		wrapnext = FALSE;
		seen_disp_event = 1;
		paste_hold = 0;
		break;
	      case '\t':
		{
		    unsigned long *old_cpos = cpos;
		    unsigned long *p = scrtop + curs_y * (cols+1) + cols;

		    do {
			curs_x++;
		    } while (curs_x < cols-1 && !tabs[curs_x]);

		    if ((*p & LATTR_MODE) != LATTR_NORM)
		    {
			if (curs_x >= cols/2)
			    curs_x = cols/2-1;
		    }
		    else
		    {
			if (curs_x >= cols)
			    curs_x = cols-1;
		    }

		    fix_cpos;
		    check_selection (old_cpos, cpos);
		}
		seen_disp_event = TRUE;
		break;
	    }
	}
	else switch (termstate) {
	  case TOPLEVEL:
	  /* Only graphic characters get this far, ctrls are stripped above */
	    if (wrapnext) {
		cpos[1] |= ATTR_WRAPPED;
		if (curs_y == marg_b)
		    scroll (marg_t, marg_b, 1, TRUE);
		else if (curs_y < rows-1)
		    curs_y++;
		curs_x = 0;
		fix_cpos;
		wrapnext = FALSE;
	    }
	    if (insert)
		insch (1);
	    if (selstate != NO_SELECTION)
	        check_selection (cpos, cpos+1);
	    switch (cset_attr[cset]) {
		/* Linedraw characters are different from 'ESC ( B' only 
		 * for a small range, for ones outside that range make sure 
		 * we use the same font as well as the same encoding.
		 */
	    case ATTR_LINEDRW:
		if (c<0x60 || c>0x7F)
	            *cpos++ = xlat_tty2scr((unsigned char)c) | curr_attr |
		              ATTR_ASCII;
		else
	            *cpos++ = ((unsigned char)c) | curr_attr | ATTR_LINEDRW;
		break;
	    default:
	        *cpos++ = xlat_tty2scr((unsigned char)c) | curr_attr |
		    (c <= 0x7F ? cset_attr[cset] : ATTR_ASCII);
		break;
	    }
	    curs_x++;
	    if (curs_x == cols) {
		cpos--;
		curs_x--;
		wrapnext = wrap;
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
		if (curs_y == marg_b)
		    scroll (marg_t, marg_b, 1, TRUE);
		else if (curs_y < rows-1)
		    curs_y++;
		fix_cpos;
		wrapnext = FALSE;
		seen_disp_event = TRUE;
		break;
	      case 'E':		       /* exactly equivalent to CR-LF */
	        compatibility(VT100);
		curs_x = 0;
		if (curs_y == marg_b)
		    scroll (marg_t, marg_b, 1, TRUE);
		else if (curs_y < rows-1)
		    curs_y++;
		fix_cpos;
		wrapnext = FALSE;
		seen_disp_event = TRUE;
		break;
	      case 'M':		       /* reverse index - backwards LF */
	        compatibility(VT100);
		if (curs_y == marg_t)
		    scroll (marg_t, marg_b, -1, TRUE);
		else if (curs_y > 0)
		    curs_y--;
		fix_cpos;
		wrapnext = FALSE;
		seen_disp_event = TRUE;
		break;
	      case 'Z':		       /* terminal type query */
	        compatibility(VT100);
		ldisc->send (id_string, strlen(id_string));
		break;
	      case 'c':		       /* restore power-on settings */
	        compatibility(VT100);
		power_on();
		if (reset_132) {
	            request_resize (80, rows, 1);
		    reset_132 = 0;
		}
		fix_cpos;
		disptop = scrtop;
		seen_disp_event = TRUE;
		break;
	      case '#':		       /* ESC # 8 fills screen with Es :-) */
	        compatibility(VT100);
		termstate = SEEN_ESCHASH;
		break;
	      case 'H':		       /* set a tab */
	        compatibility(VT100);
		tabs[curs_x] = TRUE;
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
		move (curs_x, curs_y - def(esc_args[0], 1), 1);
		seen_disp_event = TRUE;
		break;
	      case 'e':      /* move down N lines */
	        compatibility(ANSI);
	      case 'B':
		move (curs_x, curs_y + def(esc_args[0], 1), 1);
		seen_disp_event = TRUE;
		break;
	      case 'a':      /* move right N cols */
	        compatibility(ANSI);
	      case 'C':
		move (curs_x + def(esc_args[0], 1), curs_y, 1);
		seen_disp_event = TRUE;
		break;
	      case 'D':		       /* move left N cols */
		move (curs_x - def(esc_args[0], 1), curs_y, 1);
		seen_disp_event = TRUE;
		break;
	      case 'E':		       /* move down N lines and CR */
	        compatibility(ANSI);
		move (0, curs_y + def(esc_args[0], 1), 1);
		seen_disp_event = TRUE;
		break;
	      case 'F':		       /* move up N lines and CR */
	        compatibility(ANSI);
		move (0, curs_y - def(esc_args[0], 1), 1);
		seen_disp_event = TRUE;
		break;
	      case 'G': case '`':      /* set horizontal posn */
	        compatibility(ANSI);
		move (def(esc_args[0], 1) - 1, curs_y, 0);
		seen_disp_event = TRUE;
		break;
	      case 'd':		       /* set vertical posn */
	        compatibility(ANSI);
		move (curs_x, (dec_om ? marg_t : 0) + def(esc_args[0], 1) - 1,
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
		disptop = scrtop;
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
		if (curs_y <= marg_b)
		    scroll (curs_y, marg_b, -def(esc_args[0], 1), FALSE);
		seen_disp_event = TRUE;
		break;
	      case 'M':		       /* delete lines */
	        compatibility(VT102);
		if (curs_y <= marg_b)
		    scroll (curs_y, marg_b, def(esc_args[0], 1), TRUE);
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
		ldisc->send (id_string, strlen(id_string));
		break;
	      case 'n':		       /* cursor position query */
		if (esc_args[0] == 6) {
		    char buf[32];
		    sprintf (buf, "\033[%d;%dR", curs_y + 1, curs_x + 1);
		    ldisc->send (buf, strlen(buf));
		}
		else if (esc_args[0] == 5) {
		    ldisc->send ("\033[0n", 4);
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
			tabs[curs_x] = FALSE;
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
		    if (bot-top > 1) {
			marg_t = top;
			marg_b = bot;
			curs_x = 0;
			/*
			 * I used to think the cursor should be
			 * placed at the top of the newly marginned
			 * area. Apparently not: VMS TPU falls over
			 * if so.
			 *
			 * Well actually it should for Origin mode - RDB
			 */
			curs_y = (dec_om ? marg_t : 0);
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
			    compatibility(VT220);
			    curr_attr &= ~ATTR_BOLD; break;
			  case 24:     /* disable underline */
			    compatibility(VT220);
			    curr_attr &= ~ATTR_UNDER; break;
			  case 25:     /* disable blink */
			    compatibility(VT220);
			    curr_attr &= ~ATTR_BLINK; break;
			  case 27:     /* disable reverse video */
			    compatibility(VT220);
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
		    unsigned long *p = cpos;
		    if (n > cols - curs_x)
			n = cols - curs_x;
		    check_selection (cpos, cpos+n);
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
			ldisc->send (buf, 20);
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
		 * The arg == 60 is a PuTTY extension.
		 * The 2nd arg, 8bit vs 7bit is not obeyed.
		 *
		 * Setting VT102 mode should also change the Fkeys to
		 * generate PF* codes as a real VT102 has no Fkeys.
		 * The VT220 does this, F11..F13 become ESC,BS,LF other Fkeys
		 * send nothing.
		 *
		 * Note ESC c will NOT change this!
		 */

		if (esc_args[0] == 61)      compatibility_level = TM_VT102;
		else if (esc_args[0] == 60) compatibility_level = TM_ANSIMIN;
		else                        compatibility_level = TM_PUTTY;
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
		unsigned long *p;
		unsigned long nlattr;
		int n;

		switch (c) {
		case '8':
		    p = scrtop;
		    n = rows * (cols+1);
		    while (n--)
			*p++ = ATTR_DEFAULT | 'E';
		    disptop = scrtop;
		    seen_disp_event = TRUE;
		    check_selection (scrtop, scrtop + rows * (cols+1));
		    break;

		case '3': nlattr = LATTR_TOP; 	  if(0) {
		case '4': nlattr = LATTR_BOT;	} if(0) {
		case '5': nlattr = LATTR_NORM;	} if(0) {
		case '6': nlattr = LATTR_WIDE;  }

		    p = scrtop + curs_y * (cols+1) + cols;
		    *p &= ~LATTR_MODE;
		    *p |=  nlattr;
		}
	    }
	    termstate = TOPLEVEL;
	    break;
	  case VT52_ESC:
	    termstate = TOPLEVEL;
	    seen_disp_event = TRUE;
	    switch (c) {
	      case 'A':
		move (curs_x, curs_y - 1, 1);
		break;
	      case 'B':
		move (curs_x, curs_y + 1, 1);
		break;
	      case 'C':
		move (curs_x + 1, curs_y, 1);
		break;
	      case 'D':
		move (curs_x - 1, curs_y, 1);
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
		if (curs_y == 0)
		    scroll (0, rows-1, -1, TRUE);
		else if (curs_y > 0)
		    curs_y--;
		fix_cpos;
		wrapnext = FALSE;
		break;
	      case 'J':
		erase_lots(FALSE, FALSE, TRUE);
		disptop = scrtop;
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
		ldisc->send ("\033/Z", 3);
		break;
	      case '=':
	        app_cursor_keys = TRUE;
		break;
	      case '>':
	        app_cursor_keys = FALSE;
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
	    move(curs_x, c-' ', 0);
	    break;
	  case VT52_Y2:
	    termstate = TOPLEVEL;
	    move(c-' ', curs_y, 0);
	    break;
	}
	if (selstate != NO_SELECTION)
	    check_selection (cpos, cpos+1);
    }
    inbuf_head = 0;

    if (beep_overload)
    {
       if(!beep_count) beep_overload=0;
    }
    else if(beep_count && beep_count<5 && cfg.beep)
       beep(beep_count/3);
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
static void do_paint (Context ctx, int may_optimise){
    int i, j, start, our_curs_y;
    unsigned long attr, rv, cursor;
    char ch[1024];

    if (cursor_on) {
        if (has_focus) {
	    if (blinker || !cfg.blink_cur)
                cursor = ATTR_ACTCURS;
            else
                cursor = 0;
        }
        else
            cursor = ATTR_PASCURS;
    }
    else           cursor = 0;
    rv = (rvideo ? ATTR_REVERSE : 0);
    our_curs_y = curs_y + (scrtop - disptop) / (cols+1);

    for (i=0; i<rows; i++) {
	int idx = i*(cols+1);
	int lattr = (disptop[idx+cols] & LATTR_MODE);
	for (j=0; j<=cols; j++,idx++) {
	    unsigned long *d = disptop+idx;
	    wanttext[idx] = lattr | ((*d ^ rv
			      ^ (selstart <= d && d < selend ?
				 ATTR_REVERSE : 0)) |
			     (i==our_curs_y && j==curs_x ? cursor : 0));

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

    /* Make sure the cursor blinks no more than 2Hz */
    if (blink_diff>=0 && blink_diff<450)
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
    int n = where * (cols+1);

    disptop = (rel < 0 ? scrtop :
	       rel > 0 ? sbtop : disptop) + n;
    if (disptop < sbtop)
	disptop = sbtop;
    if (disptop > scrtop)
	disptop = scrtop;
    update_sbar();
    term_update();
}

/*
 * Spread the selection outwards according to the selection mode.
 */
static unsigned long *sel_spread_half (unsigned long *p, int dir) {
    unsigned long *linestart, *lineend;
    int x;
    short wvalue;

    x = (p - text) % (cols+1);
    linestart = p - x;
    lineend = linestart + cols;

    switch (selmode) {
      case SM_CHAR:
	/*
	 * In this mode, every character is a separate unit, except
	 * for runs of spaces at the end of a non-wrapping line.
	 */
	if (!(linestart[cols] & ATTR_WRAPPED)) {
	    unsigned long *q = lineend;
	    while (q > linestart && (q[-1] & CHAR_MASK) == 0x20)
		q--;
	    if (q == lineend)
		q--;
	    if (p >= q)
		p = (dir == -1 ? q : lineend - 1);
	}
	break;
      case SM_WORD:
	/*
	 * In this mode, the units are maximal runs of characters
	 * whose `wordness' has the same value.
	 */
	wvalue = wordness[*p & CHAR_MASK];
	if (dir == +1) {
	    while (p < lineend && wordness[p[1] & CHAR_MASK] == wvalue)
		p++;
	} else {
	    while (p > linestart && wordness[p[-1] & CHAR_MASK] == wvalue)
		p--;
	}
	break;
      case SM_LINE:
	/*
	 * In this mode, every line is a unit.
	 */
	p = (dir == -1 ? linestart : lineend - 1);
	break;
    }
    return p;
}

static void sel_spread (void) {
    selstart = sel_spread_half (selstart, -1);
    selend = sel_spread_half (selend - 1, +1) + 1;
}

void term_mouse (Mouse_Button b, Mouse_Action a, int x, int y) {
    unsigned long *selpoint;
    
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

    selpoint = disptop + y * (cols+1);
    if ((selpoint[cols]&LATTR_MODE) != LATTR_NORM)
	selpoint += x/2;
    else
	selpoint += x;

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
	selend = selstart + 1;
	sel_spread();
    } else if ((b == MB_SELECT && a == MA_DRAG) ||
	       (b == MB_EXTEND && a != MA_RELEASE)) {
	if (selstate == ABOUT_TO && selanchor == selpoint)
	    return;
	if (b == MB_EXTEND && a != MA_DRAG && selstate == SELECTED) {
	    if (selpoint-selstart < (selend-selstart)/2)
		selanchor = selend - 1;
	    else
		selanchor = selstart;
	    selstate = DRAGGING;
	}
	if (selstate != ABOUT_TO && selstate != DRAGGING)
	    selanchor = selpoint;
	selstate = DRAGGING;
	if (selpoint < selanchor) {
	    selstart = selpoint;
	    selend = selanchor + 1;
	} else {
	    selstart = selanchor;
	    selend = selpoint + 1;
	}
	sel_spread();
    } else if ((b == MB_SELECT || b == MB_EXTEND) && a == MA_RELEASE) {
	if (selstate == DRAGGING) {
	    /*
	     * We've completed a selection. We now transfer the
	     * data to the clipboard.
	     */
	    unsigned char *p = selspace;
	    unsigned long *q = selstart;

	    while (q < selend) {
		int nl = FALSE;
		unsigned long *lineend = q - (q-text) % (cols+1) + cols;
		unsigned long *nlpos = lineend;

		if (!(*nlpos & ATTR_WRAPPED)) {
		    while ((nlpos[-1] & CHAR_MASK) == 0x20 && nlpos > q)
			nlpos--;
		    if (nlpos < selend)
			nl = TRUE;
		}
		while (q < nlpos && q < selend)
		    *p++ = (unsigned char) (*q++ & CHAR_MASK);
		if (nl) {
		    int i;
		    for (i=0; i<sizeof(sel_nl); i++)
			*p++ = sel_nl[i];
		}
		q = lineend + 1;       /* start of next line */
	    }
	    write_clip (selspace, p - selspace, FALSE);
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
	        ldisc->send (paste_buffer, paste_len);
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
	char c = paste_buffer[paste_pos++];
	ldisc->send (&c, 1);

	if (c =='\r') {
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
    selstart = selend = scrtop;
}

void term_deselect (void) {
    deselect();
    term_update();
}
