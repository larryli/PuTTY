/* $Id: macterm.c,v 1.1.2.8 1999/02/28 17:05:11 ben Exp $ */
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

#include <limits.h>
#include <stdlib.h>

#include "macresid.h"
#include "putty.h"
#include "mac.h"

struct mac_session {
    short		fontnum;
    int			font_ascent;
    WindowPtr		window;
    PaletteHandle	palette;
    ControlHandle	scrollbar;
};

static void mac_initfont(struct mac_session *);
static void mac_initpalette(struct mac_session *);
static void mac_adjustsize(struct mac_session *);

/* Temporary hack till I get the terminal emulator supporting multiple sessions */

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
    term_size(24, 80, 100);
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
    mac_adjustsize(s);
}

/*
 * To be called whenever the window size changes.
 * rows and cols should be desired values.
 * It's assumed the terminal emulator will be or has been informed.
 */
static void mac_adjustsize(struct mac_session *s) {
    int winwidth, winheight;

    winwidth = cols * font_width + 15;
    winheight = rows * font_height;
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

void mac_activateterm(WindowPtr window, Boolean active) {
    struct mac_session *s;

    s = (struct mac_session *)GetWRefCon(window);
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
    clip.left = window->portRect.right - 15;
    clip.right = SHRT_MAX;
    clip.top = SHRT_MIN;
    clip.bottom = SHRT_MAX;
    ClipRect(&clip);
    DrawGrowIcon(window);
    clip.left = SHRT_MIN;
    ClipRect(&clip);
    EndUpdate(window);
}

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
    Rect textrect;

    SetPort(s->window);
    
    /* First check this text is relevant */
    textrect.top = y * font_height;
    textrect.bottom = (y + 1) * font_height;
    textrect.left = x * font_width;
    textrect.right = (x + len) * font_width;
    if (!RectInRgn(&textrect, s->window->visRgn))
	return;
	
    TextFont(s->fontnum);
    if (cfg.fontisbold || (attr & ATTR_BOLD) && !cfg.bold_colour)
    	style |= bold;
    if (attr & ATTR_UNDER)
	style |= underline;
    TextFace(style);
    TextSize(cfg.fontheight);
    TextMode(srcCopy);
    if (attr & ATTR_REVERSE) {
	bgcolour = ((attr & ATTR_FGMASK) >> ATTR_FGSHIFT) * 2;
	fgcolour = ((attr & ATTR_BGMASK) >> ATTR_BGSHIFT) * 2;
    } else {
	fgcolour = ((attr & ATTR_FGMASK) >> ATTR_FGSHIFT) * 2;
	bgcolour = ((attr & ATTR_BGMASK) >> ATTR_BGSHIFT) * 2;
    }
    if ((attr & ATTR_BOLD) && cfg.bold_colour)
    	fgcolour++;
    /* RGBForeColor(&s->palette[fgcolour]); */ /* XXX Non-Color-QD version */
    /* RGBBackColor(&s->palette[bgcolour]); */
    PmForeColor(fgcolour);
    PmBackColor(bgcolour);
    SetFractEnable(FALSE); /* We want characters on pixel boundaries */
    MoveTo(textrect.left, textrect.top + s->font_ascent);
    DrawText(text, 0, len);
    
    /* Tell the window manager about it in case this isn't an update */
    ValidRect(&textrect);
}

/*
 * Call from the terminal emulator to get its graphics context.
 * I feel this should disappear entirely (and do_text should take
 * a Session as an argument).  Simon may disagree.
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

    SetControlMinimum(s->scrollbar, 0);
    SetControlMaximum(s->scrollbar, total - page);
    SetControlValue(s->scrollbar, start);
#if 0
    /* XXX: This doesn't compile for me - bjh */
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
