/* $Id: macterm.c,v 1.1.2.15 1999/03/07 23:22:23 ben Exp $ */
/*
 * Copyright (c) 1999 Ben Harris
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * macterm.c -- Macintosh terminal front-end
 */

#include <MacTypes.h>
#include <Controls.h>
#include <Fonts.h>
#include <Gestalt.h>
#include <MacWindows.h>
#include <Palettes.h>
#include <Quickdraw.h>
#include <QuickdrawText.h>
#include <Resources.h>
#include <Sound.h>
#include <ToolUtils.h>

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>

#include "macresid.h"
#include "putty.h"
#include "mac.h"

#define DEFAULT_FG	16
#define DEFAULT_FG_BOLD	17
#define DEFAULT_BG	18
#define DEFAULT_BG_BOLD	19
#define CURSOR_FG	22
#define CURSOR_FG_BOLD	23

struct mac_session {
    short		fontnum;
    int			font_ascent;
    WindowPtr		window;
    PaletteHandle	palette;
    ControlHandle	scrollbar;
};

static void mac_initfont(struct mac_session *);
static void mac_initpalette(struct mac_session *);
static void mac_adjustsize(struct mac_session *, int, int);
static pascal void mac_scrolltracker(ControlHandle, short);
static pascal void do_text_for_device(short, short, GDHandle, long);
static int mac_keytrans(struct mac_session *, EventRecord *, unsigned char *);

/*
 * Temporary hack till I get the terminal emulator supporting multiple
 * sessions
 */

static struct mac_session *onlysession;

static void inbuf_putc(int c) {
    inbuf[inbuf_head] = c;
    inbuf_head = (inbuf_head+1) & INBUF_MASK;
}

static void inbuf_putstr(const char *c) {
    while (*c)
	inbuf_putc(*c++);
}

static void display_resource(unsigned long type, short id) {
    Handle h;
    int len, i;
    char *t;

    h = GetResource(type, id);
    if (h == NULL)
	fatalbox("Can't get test resource");
    SetResAttrs(h, GetResAttrs(h) | resLocked);
    t = *h;
    len = GetResourceSizeOnDisk(h);
    for (i = 0; i < len; i++) {
	inbuf_putc(t[i]);
	term_out();
    }
    SetResAttrs(h, GetResAttrs(h) & ~resLocked);
    ReleaseResource(h);
}
	

void mac_newsession(void) {
    struct mac_session *s;
    int i;

    /* This should obviously be initialised by other means */
    mac_loadconfig(&cfg);
/*    back = &loop_backend; */
    s = smalloc(sizeof(*s));
    onlysession = s;
	
    /* XXX: Own storage management? */
    if (mac_gestalts.qdvers == gestaltOriginalQD)
	s->window = GetNewWindow(wTerminal, NULL, (WindowPtr)-1);
    else
	s->window = GetNewCWindow(wTerminal, NULL, (WindowPtr)-1);
    SetWRefCon(s->window, (long)s);
    s->scrollbar = GetNewControl(cVScroll, s->window);
    term_init();
    term_size(cfg.height, cfg.width, cfg.savelines);
    mac_initfont(s);
    mac_initpalette(s);
    /* Set to FALSE to not get palette updates in the background. */
    SetPalette(s->window, s->palette, TRUE); 
    ActivatePalette(s->window);
    ShowWindow(s->window);
    display_resource('pTST', 128);
}

static void mac_initfont(struct mac_session *s) {
    Str255 macfont;
    FontInfo fi;
 
    SetPort(s->window);
    macfont[0] = sprintf((char *)&macfont[1], "%s", cfg.font);
    GetFNum(macfont, &s->fontnum);
    TextFont(s->fontnum);
    TextFace(cfg.fontisbold ? bold : 0);
    TextSize(cfg.fontheight);
    GetFontInfo(&fi);
    font_width = fi.widMax;
    font_height = fi.ascent + fi.descent + fi.leading;
    s->font_ascent = fi.ascent;
    mac_adjustsize(s, rows, cols);
}

/*
 * To be called whenever the window size changes.
 * rows and cols should be desired values.
 * It's assumed the terminal emulator will be informed, and will set rows
 * and cols for us.
 */
static void mac_adjustsize(struct mac_session *s, int newrows, int newcols) {
    int winwidth, winheight;

    winwidth = newcols * font_width + 15;
    winheight = newrows * font_height;
    SizeWindow(s->window, winwidth, winheight, true);
    HideControl(s->scrollbar);
    MoveControl(s->scrollbar, winwidth - 15, -1);
    SizeControl(s->scrollbar, 16, winheight - 13);
    ShowControl(s->scrollbar);
}

static void mac_initpalette(struct mac_session *s) {
    WinCTab ct;
  
    if (mac_gestalts.qdvers == gestaltOriginalQD)
	return;
    s->palette = NewPalette((*cfg.colours)->pmEntries, NULL, pmCourteous, 0);
    if (s->palette == NULL)
	fatalbox("Unable to create palette");
    CopyPalette(cfg.colours, s->palette, 0, 0, (*cfg.colours)->pmEntries);
}

/*
 * I don't think this is (a) safe or (b) a good way to do this.
 */
static void mac_updatewinbg(struct mac_session *s) {
    WinCTab ct;
    WCTabPtr ctp = &ct;
    WCTabHandle cth = &ctp;

    ct.wCSeed = 0;
    ct.wCReserved = 0;
    ct.ctSize = 1;
    ct.ctTable[0].value = wContentColor;
    ct.ctTable[0].rgb = (*s->palette)->pmInfo[16].ciRGB;
    SetWinColor(s->window, cth);
}

void mac_clickterm(WindowPtr window, EventRecord *event) {
    struct mac_session *s;
    Point mouse;
    ControlHandle control;
    int part;

    s = (struct mac_session *)GetWRefCon(window);
    SetPort(window);
    mouse = event->where;
    GlobalToLocal(&mouse);
    part = FindControl(mouse, window, &control);
    if (control == s->scrollbar) {
	switch (part) {
	  case kControlIndicatorPart:
	    if (TrackControl(control, mouse, NULL) == kControlIndicatorPart)
		term_scroll(+1, GetControlValue(control));
	    break;
	  case kControlUpButtonPart:
	  case kControlDownButtonPart:
	  case kControlPageUpPart:
	  case kControlPageDownPart:
	    TrackControl(control, mouse, mac_scrolltracker);
	    break;
	}
    }
}

static pascal void mac_scrolltracker(ControlHandle control, short part) {
    struct mac_session *s;

    s = (struct mac_session *)GetWRefCon((*control)->contrlOwner);
    switch (part) {
      case kControlUpButtonPart:
	term_scroll(0, -1);
	break;
      case kControlDownButtonPart:
	term_scroll(0, +1);
	break;
      case kControlPageUpPart:
	term_scroll(0, -(rows - 1));
	break;
      case kControlPageDownPart:
	term_scroll(0, +(rows - 1));
	break;
    }
}

#define K_SPACE	0x3100
#define K_BS	0x3300
#define K_F1	0x7a00
#define K_F2	0x7800
#define K_F3	0x6300
#define K_F4	0x7600
#define K_F5	0x6000
#define K_F6	0x6100
#define K_F7	0x6200
#define K_F8	0x6400
#define K_F9	0x6500
#define K_F10	0x6d00
#define K_F11	0x6700
#define K_F12	0x6f00
#define K_INSERT 0x7200
#define K_HOME	0x7300
#define K_PRIOR	0x7400
#define K_DELETE 0x7500
#define K_END	0x7700
#define K_NEXT	0x7900
#define K_LEFT	0x7b00
#define K_RIGHT	0x7c00
#define K_DOWN	0x7d00
#define K_UP	0x7e00
#define KP_0	0x5200
#define KP_1	0x5300
#define KP_2	0x5400
#define KP_3	0x5500
#define KP_4	0x5600
#define KP_5	0x5700
#define KP_6	0x5800
#define KP_7	0x5900
#define KP_8	0x5b00
#define KP_9	0x5c00
#define KP_CLEAR 0x4700
#define KP_EQUAL 0x5100
#define KP_SLASH 0x4b00
#define KP_STAR	0x4300
#define KP_PLUS	0x4500
#define KP_MINUS 0x4e00
#define KP_DOT	0x4100
#define KP_ENTER 0x4c00

void mac_keyterm(WindowPtr window, EventRecord *event) {
    unsigned char buf[20];
    int len;
    struct mac_session *s;
    int i;

    s = (struct mac_session *)GetWRefCon(window);
    len = mac_keytrans(s, event, buf);
    /* XXX: I can't get the loopback backend to link, so we'll do this: */
/*    back->send((char *)buf, len); */
    for (i = 0; i < len; i++)
	inbuf_putc(buf[i]);
    term_out();
    term_update();
}

static int mac_keytrans(struct mac_session *s, EventRecord *event,
			unsigned char *output) {
    unsigned char *p = output;
    int code;

    /* No meta key yet -- that'll be rather fun. */

    /* Keys that we handle locally */
    if (event->modifiers & shiftKey) {
	switch (event->message & keyCodeMask) {
	  case K_PRIOR: /* shift-pageup */
	    term_scroll(0, -(rows - 1));
	    return 0;
	  case K_NEXT:  /* shift-pagedown */
	    term_scroll(0, +(rows - 1));
	    return 0;
	}
    }

    /*
     * Control-2 should return ^@ (0x00), Control-6 should return
     * ^^ (0x1E), and Control-Minus should return ^_ (0x1F). Since
     * the DOS keyboard handling did it, and we have nothing better
     * to do with the key combo in question, we'll also map
     * Control-Backquote to ^\ (0x1C).
     */

    if (event->modifiers & controlKey) {
	switch (event->message & charCodeMask) {
	  case ' ': case '2':
	    *p++ = 0x00;
	    return p - output;
	  case '`':
	    *p++ = 0x1c;
	    return p - output;
	  case '6':
	    *p++ = 0x1e;
	    return p - output;
	  case '/':
	    *p++ = 0x1f;
	    return p - output;
	}
    }

    /*
     * First, all the keys that do tilde codes. (ESC '[' nn '~',
     * for integer decimal nn.)
     *
     * We also deal with the weird ones here. Linux VCs replace F1
     * to F5 by ESC [ [ A to ESC [ [ E. rxvt doesn't do _that_, but
     * does replace Home and End (1~ and 4~) by ESC [ H and ESC O w
     * respectively.
     */
    code = 0;
    switch (event->message & keyCodeMask) {
      case K_F1: code = (event->modifiers & shiftKey ? 23 : 11); break;
      case K_F2: code = (event->modifiers & shiftKey ? 24 : 12); break;
      case K_F3: code = (event->modifiers & shiftKey ? 25 : 13); break;
      case K_F4: code = (event->modifiers & shiftKey ? 26 : 14); break;
      case K_F5: code = (event->modifiers & shiftKey ? 28 : 15); break;
      case K_F6: code = (event->modifiers & shiftKey ? 29 : 17); break;
      case K_F7: code = (event->modifiers & shiftKey ? 31 : 18); break;
      case K_F8: code = (event->modifiers & shiftKey ? 32 : 19); break;
      case K_F9: code = (event->modifiers & shiftKey ? 33 : 20); break;
      case K_F10: code = (event->modifiers & shiftKey ? 34 : 21); break;
      case K_F11: code = 23; break;
      case K_F12: code = 24; break;
      case K_HOME: code = 1; break;
      case K_INSERT: code = 2; break;
      case K_DELETE: code = 3; break;
      case K_END: code = 4; break;
      case K_PRIOR: code = 5; break;
      case K_NEXT: code = 6; break;
    }
    if (cfg.linux_funkeys && code >= 11 && code <= 15) {
	p += sprintf((char *)p, "\x1B[[%c", code + 'A' - 11);
	return p - output;
    }
    if (cfg.rxvt_homeend && (code == 1 || code == 4)) {
	p += sprintf((char *)p, code == 1 ? "\x1B[H" : "\x1BOw");
	return p - output;
    }
    if (code) {
	p += sprintf((char *)p, "\x1B[%d~", code);
	return p - output;
    }

    if (app_keypad_keys) {
	switch (event->message & keyCodeMask) {
	  case KP_ENTER: p += sprintf((char *)p, "\x1BOM"); return p - output;
	  case KP_CLEAR: p += sprintf((char *)p, "\x1BOP"); return p - output;
	  case KP_EQUAL: p += sprintf((char *)p, "\x1BOQ"); return p - output;
	  case KP_SLASH: p += sprintf((char *)p, "\x1BOR"); return p - output;
	  case KP_STAR:  p += sprintf((char *)p, "\x1BOS"); return p - output;
	  case KP_PLUS:  p += sprintf((char *)p, "\x1BOl"); return p - output;
	  case KP_MINUS: p += sprintf((char *)p, "\x1BOm"); return p - output;
	  case KP_DOT:   p += sprintf((char *)p, "\x1BOn"); return p - output;
	  case KP_0:     p += sprintf((char *)p, "\x1BOp"); return p - output;
	  case KP_1:     p += sprintf((char *)p, "\x1BOq"); return p - output;
	  case KP_2:     p += sprintf((char *)p, "\x1BOr"); return p - output;
	  case KP_3:     p += sprintf((char *)p, "\x1BOs"); return p - output;
	  case KP_4:     p += sprintf((char *)p, "\x1BOt"); return p - output;
	  case KP_5:     p += sprintf((char *)p, "\x1BOu"); return p - output;
	  case KP_6:     p += sprintf((char *)p, "\x1BOv"); return p - output;
	  case KP_7:     p += sprintf((char *)p, "\x1BOw"); return p - output;
	  case KP_8:     p += sprintf((char *)p, "\x1BOx"); return p - output;
	  case KP_9:     p += sprintf((char *)p, "\x1BOy"); return p - output;
	}
    }

    switch (event->message & keyCodeMask) {
      case K_UP:
	p += sprintf((char *)p, app_cursor_keys ? "\x1BOA" : "\x1B[A");
	return p - output;
      case K_DOWN:
	p += sprintf((char *)p, app_cursor_keys ? "\x1BOB" : "\x1B[B");
	return p - output;
      case K_RIGHT:
	p += sprintf((char *)p, app_cursor_keys ? "\x1BOC" : "\x1B[C");
	return p - output;
      case K_LEFT:
	p += sprintf((char *)p, app_cursor_keys ? "\x1BOD" : "\x1B[D");
	return p - output;
      case K_BS:
	*p++ = (cfg.bksp_is_delete ? 0x7f : 0x08);
	return p - output;
      default:
	*p++ = event->message & charCodeMask;
	return p - output;
    }
}

void mac_growterm(WindowPtr window, EventRecord *event) {
    Rect limits;
    long grow_result;
    int newrows, newcols;
    struct mac_session *s;

    s = (struct mac_session *)GetWRefCon(window);
    SetRect(&limits, font_width + 15, font_height, SHRT_MAX, SHRT_MAX);
    grow_result = GrowWindow(window, event->where, &limits);
    if (grow_result != 0) {
	newrows = HiWord(grow_result) / font_height;
	newcols = (LoWord(grow_result) - 15) / font_width;
	mac_adjustsize(s, newrows, newcols);
	term_size(newrows, newcols, cfg.savelines);
    }
}

void mac_activateterm(WindowPtr window, Boolean active) {
    struct mac_session *s;

    s = (struct mac_session *)GetWRefCon(window);
    has_focus = active;
    term_update();
    if (active)
	ShowControl(s->scrollbar);
    else
	HideControl(s->scrollbar);
}

void mac_updateterm(WindowPtr window) {
    struct mac_session *s;
    Rect clip;

    s = (struct mac_session *)GetWRefCon(window);
    BeginUpdate(window);
    term_paint(s,
	       (*window->visRgn)->rgnBBox.left,
	       (*window->visRgn)->rgnBBox.top,
	       (*window->visRgn)->rgnBBox.right,
	       (*window->visRgn)->rgnBBox.bottom);
    /* Restore default colours in case the Window Manager uses them */
    PmForeColor(16);
    PmBackColor(18);
    if (FrontWindow() != window)
	EraseRect(&(*s->scrollbar)->contrlRect);
    UpdateControls(window, window->visRgn);
    /* Stop DrawGrowIcon giving us space for a horizontal scrollbar */
    SetRect(&clip, window->portRect.right - 15, SHRT_MIN, SHRT_MAX, SHRT_MAX);
    ClipRect(&clip);
    DrawGrowIcon(window);
    clip.left = SHRT_MIN;
    ClipRect(&clip);
    EndUpdate(window);
}

struct do_text_args {
    struct mac_session *s;
    Rect textrect;
    char *text;
    int len;
    unsigned long attr;
};

/*
 * Call from the terminal emulator to draw a bit of text
 *
 * x and y are text row and column (zero-based)
 */
void do_text(struct mac_session *s, int x, int y, char *text, int len,
	     unsigned long attr) {
    int style = 0;
    int bgcolour, fgcolour;
    RGBColor rgbfore, rgbback;
    struct do_text_args a;
    RgnHandle textrgn;

    SetPort(s->window);
    
    /* First check this text is relevant */
    a.textrect.top = y * font_height;
    a.textrect.bottom = (y + 1) * font_height;
    a.textrect.left = x * font_width;
    a.textrect.right = (x + len) * font_width;
    if (!RectInRgn(&a.textrect, s->window->visRgn))
	return;

    a.s = s;
    a.text = text;
    a.len = len;
    a.attr = attr;
    SetPort(s->window);
    TextFont(s->fontnum);
    if (cfg.fontisbold || (attr & ATTR_BOLD) && !cfg.bold_colour)
    	style |= bold;
    if (attr & ATTR_UNDER)
	style |= underline;
    TextFace(style);
    TextSize(cfg.fontheight);
    if (attr & ATTR_REVERSE)
	TextMode(notSrcCopy);
    else
	TextMode(srcCopy);
    SetFractEnable(FALSE); /* We want characters on pixel boundaries */
    textrgn = NewRgn();
    RectRgn(textrgn, &a.textrect);
    DeviceLoop(textrgn, do_text_for_device, (long)&a, 0);
    /* Tell the window manager about it in case this isn't an update */
    DisposeRgn(textrgn);
    ValidRect(&a.textrect);
}

static pascal void do_text_for_device(short depth, short devflags,
				      GDHandle device, long cookie) {
    struct do_text_args *a;
    int bgcolour, fgcolour;

    a = (struct do_text_args *)cookie;

    switch (depth) {
      case 1:
	/* XXX This should be done with a _little_ more configurability */
	ForeColor(whiteColor);
	BackColor(blackColor);
	break;
      case 2:
	if ((a->attr & ATTR_BOLD) && cfg.bold_colour)
	    PmForeColor(DEFAULT_FG_BOLD);
	else
	    PmForeColor(DEFAULT_FG);
	if (a->attr & ATTR_ACTCURS)
	    PmBackColor(CURSOR_FG);
	else
	    PmBackColor(DEFAULT_BG);
	break;
      default:
	fgcolour = ((a->attr & ATTR_FGMASK) >> ATTR_FGSHIFT) * 2;
	bgcolour = ((a->attr & ATTR_BGMASK) >> ATTR_BGSHIFT) * 2;
	if ((a->attr & ATTR_BOLD) && cfg.bold_colour)
	    if (a->attr & ATTR_REVERSE)
		bgcolour++;
	    else
		fgcolour++;
	if (a->attr & ATTR_ACTCURS)
	    bgcolour = CURSOR_FG;
	PmForeColor(fgcolour);
	PmBackColor(bgcolour);
	break;
    }
    MoveTo(a->textrect.left, a->textrect.top + a->s->font_ascent);
    DrawText(a->text, 0, a->len);
}

/*
 * Call from the terminal emulator to get its graphics context.
 */
struct mac_session *get_ctx(void) {

    return onlysession;
}

/*
 * Presumably this does something in Windows
 */
void free_ctx(struct mac_session *ctx) {

}

/*
 * Set the scroll bar position
 *
 * total is the line number of the bottom of the working screen
 * start is the line number of the top of the display
 * page is the length of the displayed page
 */
void set_sbar(int total, int start, int page) {
    struct mac_session *s = onlysession;

    /* We don't redraw until we've set everything up, to avoid glitches */
    (*s->scrollbar)->contrlMin = 0;
    (*s->scrollbar)->contrlMax = total - page;
    SetControlValue(s->scrollbar, start);
#if 0
    /* XXX: This doesn't link for me. */
    if (mac_gestalts.cntlattr & gestaltControlMgrPresent)
	SetControlViewSize(s->scrollbar, page);
#endif
}

/*
 * Beep
 */
void beep(void) {

    SysBeep(30);
    /*
     * XXX We should indicate the relevant window and/or use the
     * Notification Manager
     */
}

/*
 * Set icon string -- a no-op here (Windowshade?)
 */
void set_icon(char *icon) {

}

/*
 * Set the window title
 */
void set_title(char *title) {
    Str255 mactitle;
    struct mac_session *s = onlysession;

    mactitle[0] = sprintf((char *)&mactitle[1], "%s", title);
    SetWTitle(s->window, mactitle);
}

/*
 * Resize the window at the emulator's request
 */
void request_resize(int w, int h) {

    cols = w;
    rows = h;
    mac_initfont(onlysession);
}

/*
 * Set the logical palette
 */
void palette_set(int n, int r, int g, int b) {
    RGBColor col;
    struct mac_session *s = onlysession;
    static const int first[21] = {
	0, 2, 4, 6, 8, 10, 12, 14,
	1, 3, 5, 7, 9, 11, 13, 15,
	16, 17, 18, 20, 22
    };
    
    if (mac_gestalts.qdvers == gestaltOriginalQD)
      return;
    col.red   = r * 0x0101;
    col.green = g * 0x0101;
    col.blue  = b * 0x0101;
    SetEntryColor(s->palette, first[n], &col);
    if (first[n] >= 18)
	SetEntryColor(s->palette, first[n]+1, &col);
    ActivatePalette(s->window);
}

/*
 * Reset to the default palette
 */
void palette_reset(void) {
    struct mac_session *s = onlysession;

    if (mac_gestalts.qdvers == gestaltOriginalQD)
	return;
    CopyPalette(cfg.colours, s->palette, 0, 0, (*cfg.colours)->pmEntries);
    ActivatePalette(s->window);
    /* Palette Manager will generate update events as required. */
}

/*
 * Scroll the screen. (`lines' is +ve for scrolling forward, -ve
 * for backward.)
 */
void do_scroll(int topline, int botline, int lines) {
    struct mac_session *s = onlysession;
    Rect r;
    RgnHandle update;

    SetPort(s->window);
    PmBackColor(DEFAULT_BG);
    update = NewRgn();
    SetRect(&r, 0, topline * font_height,
	    cols * font_width, (botline + 1) * font_height);
    ScrollRect(&r, 0, - lines * font_height, update);
    /* XXX: move update region? */
    InvalRgn(update);
    DisposeRgn(update);
}
