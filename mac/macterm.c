/* $Id$ */
/*
 * Copyright (c) 1999 Simon Tatham
 * Copyright (c) 1999, 2002 Ben Harris
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
#include <ControlDefinitions.h>
#include <FixMath.h>
#include <Fonts.h>
#include <Gestalt.h>
#include <LowMem.h>
#include <MacMemory.h>
#include <MacWindows.h>
#include <MixedMode.h>
#include <Palettes.h>
#include <Quickdraw.h>
#include <QuickdrawText.h>
#include <Resources.h>
#include <Scrap.h>
#include <Script.h>
#include <Sound.h>
#include <TextCommon.h>
#include <ToolUtils.h>
#include <UnicodeConverter.h>

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "macresid.h"
#include "putty.h"
#include "charset.h"
#include "mac.h"
#include "terminal.h"

#define DEFAULT_FG	256
#define DEFAULT_FG_BOLD	257
#define DEFAULT_BG	258
#define DEFAULT_BG_BOLD	259
#define CURSOR_FG	260
#define CURSOR_BG	261

#define PTOCC(x) ((x) < 0 ? -(-(x - s->font_width - 1) / s->font_width) : \
			    (x) / s->font_width)
#define PTOCR(y) ((y) < 0 ? -(-(y - s->font_height - 1) / s->font_height) : \
			    (y) / s->font_height)

static void mac_initfont(Session *);
static pascal OSStatus uni_to_font_fallback(UniChar *, ByteCount, ByteCount *,
					    TextPtr, ByteCount, ByteCount *,
					    LogicalAddress,
					    ConstUnicodeMappingPtr);
static void mac_initpalette(Session *);
static void mac_adjustwinbg(Session *);
static void mac_adjustsize(Session *, int, int);
static void mac_drawgrowicon(Session *s);
static pascal void mac_growtermdraghook(void);
static pascal void mac_scrolltracker(ControlHandle, short);
static pascal void do_text_for_device(short, short, GDHandle, long);
static void do_text_internal(Context, int, int, wchar_t *, int,
			     unsigned long, int);
static void text_click(Session *, EventRecord *);
static void mac_activateterm(WindowPtr, EventRecord *);
static void mac_adjusttermcursor(WindowPtr, Point, RgnHandle);
static void mac_adjusttermmenus(WindowPtr);
static void mac_updateterm(WindowPtr);
static void mac_clickterm(WindowPtr, EventRecord *);
static void mac_growterm(WindowPtr, EventRecord *);
static void mac_keyterm(WindowPtr, EventRecord *);
static void mac_menuterm(WindowPtr, short, short);
static void mac_closeterm(WindowPtr);

void pre_paint(Session *s);
void post_paint(Session *s);

void mac_startsession(Session *s)
{
    const char *errmsg;
    int i;
    WinInfo *wi;

    init_ucs(s);

    /*
     * Select protocol. This is farmed out into a table in a
     * separate file to enable an ssh-free variant.
     */
    s->back = NULL;
    for (i = 0; backends[i].backend != NULL; i++)
	if (backends[i].protocol == s->cfg.protocol) {
	    s->back = backends[i].backend;
	    break;
	}
    if (s->back == NULL)
	fatalbox("Unsupported protocol number found");

    /* XXX: Own storage management? */
    if (HAVE_COLOR_QD())
	s->window = GetNewCWindow(wTerminal, NULL, (WindowPtr)-1);
    else
	s->window = GetNewWindow(wTerminal, NULL, (WindowPtr)-1);
    wi = snew(WinInfo);
    memset(wi, 0, sizeof(*wi));
    wi->s = s;
    wi->wtype = wTerminal;
    wi->activate = &mac_activateterm;
    wi->adjustcursor = &mac_adjusttermcursor;
    wi->adjustmenus = &mac_adjusttermmenus;
    wi->update = &mac_updateterm;
    wi->click = &mac_clickterm;
    wi->grow = &mac_growterm;
    wi->key = &mac_keyterm;
    wi->menu = &mac_menuterm;
    wi->close = &mac_closeterm;
    SetWRefCon(s->window, (long)wi);
    s->scrollbar = GetNewControl(cVScroll, s->window);
    s->term = term_init(&s->cfg, &s->ucsdata, s);

    mac_initfont(s);
    mac_initpalette(s);
    if (HAVE_COLOR_QD()) {
	/* Set to FALSE to not get palette updates in the background. */
	SetPalette(s->window, s->palette, TRUE); 
	ActivatePalette(s->window);
    }

    s->logctx = log_init(s->term, &s->cfg);
    term_provide_logctx(s->term, s->logctx);

    errmsg = s->back->init(s, &s->backhandle, &s->cfg, s->cfg.host,
			   s->cfg.port, &s->realhost, s->cfg.tcp_nodelay,
			   s->cfg.tcp_keepalives);
    if (errmsg != NULL)
	fatalbox("%s", errmsg);
    s->back->provide_logctx(s->backhandle, s->logctx);
    set_title(s, s->realhost);

    term_provide_resize_fn(s->term, s->back->size, s->backhandle);

    mac_adjustsize(s, s->cfg.height, s->cfg.width);
    term_size(s->term, s->cfg.height, s->cfg.width, s->cfg.savelines);

    s->ldisc = ldisc_create(&s->cfg, s->term, s->back, s->backhandle, s);
    ldisc_send(s->ldisc, NULL, 0, 0);/* cause ldisc to notice changes */

    ShowWindow(s->window);
    s->next = sesslist;
    s->prev = &sesslist;
    if (s->next != NULL)
	s->next->prev = &s->next;
    sesslist = s;
}

/*
 * Try to work out a horizontal scaling factor for the current font
 * that will give a chracter width of wantwidth.  Return it in numer
 * and denom (suitable for passing to StdText()).
 */
static void mac_workoutfontscale(Session *s, int wantwidth,
				 Point *numerp, Point *denomp)
{
    Point numer, denom, tmpnumer, tmpdenom;
    int gotwidth, i;
    const char text = 'W';
    FontInfo fi;
#if TARGET_API_MAC_CARBON
    CQDProcsPtr gp = GetPortGrafProcs(GetWindowPort(s->window));
#else
    QDProcsPtr gp = s->window->grafProcs;
#endif

    numer.v = denom.v = 1; /* always */
    numer.h = denom.h = 1;
    for (i = 0; i < 3; i++) {
	tmpnumer = numer;
	tmpdenom = denom;
	if (gp != NULL)
	    gotwidth = InvokeQDTxMeasUPP(1, &text, &tmpnumer, &tmpdenom, &fi,
					 gp->txMeasProc);
	else
	    gotwidth = StdTxMeas(1, &text, &tmpnumer, &tmpdenom, &fi);
	/* The result of StdTxMeas must be scaled by the factors it returns. */
	gotwidth = FixRound(FixMul(gotwidth << 16,
				   FixRatio(tmpnumer.h, tmpdenom.h)));
	if (gotwidth == wantwidth)
	    break;
	numer.h *= wantwidth;
	denom.h *= gotwidth;
    }
    *numerp = numer;
    *denomp = denom;
}

static UnicodeToTextFallbackUPP uni_to_font_fallback_upp;

static void mac_initfont(Session *s)
{
    FontInfo fi;
    TextEncoding enc;
    OptionBits fbflags;

    SetPort((GrafPtr)GetWindowPort(s->window));
    GetFNum(s->cfg.font.name, &s->fontnum);
    TextFont(s->fontnum);
    TextFace(s->cfg.font.face);
    TextSize(s->cfg.font.size);
    GetFontInfo(&fi);
    s->font_width = CharWidth('W'); /* Well, it's what NCSA uses. */
    s->font_ascent = fi.ascent;
    s->font_leading = fi.leading;
    s->font_height = s->font_ascent + fi.descent + s->font_leading;
    mac_workoutfontscale(s, s->font_width,
			 &s->font_stdnumer, &s->font_stddenom);
    mac_workoutfontscale(s, s->font_width * 2,
			 &s->font_widenumer, &s->font_widedenom);
    TextSize(s->cfg.font.size * 2);
    mac_workoutfontscale(s, s->font_width * 2,
			 &s->font_bignumer, &s->font_bigdenom);
    TextSize(s->cfg.font.size);
    if (!s->cfg.bold_colour) {
	TextFace(bold);
	s->font_boldadjust = s->font_width - CharWidth('W');
    } else
	s->font_boldadjust = 0;

    if (s->uni_to_font != NULL)
	DisposeUnicodeToTextInfo(&s->uni_to_font);
    if (mac_gestalts.encvvers != 0 &&
	UpgradeScriptInfoToTextEncoding(kTextScriptDontCare,
					kTextLanguageDontCare,
					kTextRegionDontCare, s->cfg.font.name,
					&enc) == noErr &&
	CreateUnicodeToTextInfoByEncoding(enc, &s->uni_to_font) == noErr) {
	if (uni_to_font_fallback_upp == NULL)
	    uni_to_font_fallback_upp =
		NewUnicodeToTextFallbackUPP(&uni_to_font_fallback);
	fbflags = kUnicodeFallbackCustomOnly;
	if (mac_gestalts.uncvattr & kTECAddFallbackInterruptMask)
	    fbflags |= kUnicodeFallbackInterruptSafeMask;
	if (SetFallbackUnicodeToText(s->uni_to_font,
	    uni_to_font_fallback_upp, fbflags, NULL) != noErr) {
	    DisposeUnicodeToTextInfo(&s->uni_to_font);
	    goto no_encv;
	}
    } else {
	char cfontname[256];

      no_encv:
	s->uni_to_font = NULL;
	p2cstrcpy(cfontname, s->cfg.font.name);
	s->font_charset =
	    charset_from_macenc(FontToScript(s->fontnum),
				GetScriptManagerVariable(smRegionCode),
				mac_gestalts.sysvers, cfontname);
    }

    mac_adjustsize(s, s->term->rows, s->term->cols);
}

static pascal OSStatus uni_to_font_fallback(UniChar *ucp,
    ByteCount ilen, ByteCount *iusedp, TextPtr obuf, ByteCount olen,
    ByteCount *ousedp, LogicalAddress cookie, ConstUnicodeMappingPtr mapping)
{

    if (olen < 1)
	return kTECOutputBufferFullStatus;
    /*
     * What I'd _like_ to do here is to somehow generate the
     * missing-character glyph that every font is required to have.
     * Unfortunately (and somewhat surprisingly), I can't find any way
     * to actually ask for it explicitly.  Bah.
     */
    *obuf = '.';
    *iusedp = ilen;
    *ousedp = 1;
    return noErr;
}

/*
 * To be called whenever the window size changes.
 * rows and cols should be desired values.
 * It's assumed the terminal emulator will be informed, and will set rows
 * and cols for us.
 */
static void mac_adjustsize(Session *s, int newrows, int newcols) {
    int winwidth, winheight;
    int extraforscroll;

    extraforscroll=s->cfg.scrollbar ? 15 : 0;
    winwidth = newcols * s->font_width + extraforscroll;
    winheight = newrows * s->font_height;
    SizeWindow(s->window, winwidth, winheight, true);
    if (s->cfg.scrollbar) {
        HideControl(s->scrollbar);
        MoveControl(s->scrollbar, winwidth - extraforscroll, -1);
        SizeControl(s->scrollbar, extraforscroll + 1, winheight - 13);
        ShowControl(s->scrollbar);
    }
    mac_drawgrowicon(s);
}

static void mac_initpalette(Session *s)
{

    if (!HAVE_COLOR_QD())
	return;
    /*
     * Most colours should be inhibited on 2bpp displays.
     * Palette manager documentation suggests inhibiting all tolerant colours
     * on greyscale displays.
     */
#define PM_NORMAL 	( pmTolerant | pmInhibitC2 |			\
			  pmInhibitG2 | pmInhibitG4 | pmInhibitG8 )
#define PM_TOLERANCE	0x2000
    s->palette = NewPalette(262, NULL, PM_NORMAL, PM_TOLERANCE);
    if (s->palette == NULL)
	fatalbox("Unable to create palette");
    /* In 2bpp, these are the colours we want most. */
    SetEntryUsage(s->palette, DEFAULT_BG,
		  PM_NORMAL &~ pmInhibitC2, PM_TOLERANCE);
    SetEntryUsage(s->palette, DEFAULT_FG,
		  PM_NORMAL &~ pmInhibitC2, PM_TOLERANCE);
    SetEntryUsage(s->palette, DEFAULT_FG_BOLD,
		  PM_NORMAL &~ pmInhibitC2, PM_TOLERANCE);
    SetEntryUsage(s->palette, CURSOR_BG,
		  PM_NORMAL &~ pmInhibitC2, PM_TOLERANCE);
    palette_reset(s);
}

/*
 * Set the background colour of the window correctly.  Should be
 * called whenever the default background changes.
 */
static void mac_adjustwinbg(Session *s)
{

    if (!HAVE_COLOR_QD())
	return;
#if !TARGET_CPU_68K
    if (mac_gestalts.windattr & gestaltWindowMgrPresent)
	SetWindowContentColor(s->window,
			      &(*s->palette)->pmInfo[DEFAULT_BG].ciRGB);
    else
#endif
    {
#if !TARGET_API_MAC_CARBON
	if (s->wctab == NULL)
	    s->wctab = (WCTabHandle)NewHandle(sizeof(**s->wctab));
	if (s->wctab == NULL)
	    return; /* do without */
	(*s->wctab)->wCSeed = 0;
	(*s->wctab)->wCReserved = 0;
	(*s->wctab)->ctSize = 0;
	(*s->wctab)->ctTable[0].value = wContentColor;
	(*s->wctab)->ctTable[0].rgb = (*s->palette)->pmInfo[DEFAULT_BG].ciRGB;
	SetWinColor(s->window, s->wctab);
#endif
    }
}

/*
 * Set the cursor shape correctly
 */
static void mac_adjusttermcursor(WindowPtr window, Point mouse,
				 RgnHandle cursrgn)
{
    Session *s;
    ControlHandle control;
    short part;
    int x, y;
#if TARGET_API_MAC_CARBON
    Cursor arrow;
    Rect rect;
    RgnHandle visrgn;
#endif

    SetPort((GrafPtr)GetWindowPort(window));
    s = mac_windowsession(window);
    GlobalToLocal(&mouse);
    part = FindControl(mouse, window, &control);
    if (control == s->scrollbar) {
#if TARGET_API_MAC_CARBON
	SetCursor(GetQDGlobalsArrow(&arrow));
	RectRgn(cursrgn, GetControlBounds(s->scrollbar, &rect));
#else
	SetCursor(&qd.arrow);
	RectRgn(cursrgn, &(*s->scrollbar)->contrlRect);
#endif
    } else {
	x = mouse.h / s->font_width;
	y = mouse.v / s->font_height;
	if (s->raw_mouse) {
#if TARGET_API_MAC_CARBON
	    SetCursor(GetQDGlobalsArrow(&arrow));
#else
	    SetCursor(&qd.arrow);
#endif
	} else
	    SetCursor(*GetCursor(iBeamCursor));
	/* Ask for shape changes if we leave this character cell. */
	SetRectRgn(cursrgn, x * s->font_width, y * s->font_height,
		   (x + 1) * s->font_width, (y + 1) * s->font_height);
    }
#if TARGET_API_MAC_CARBON
    visrgn = NewRgn();
    GetPortVisibleRegion(GetWindowPort(window), visrgn);
    SectRgn(cursrgn, visrgn, cursrgn);
    DisposeRgn(visrgn);
#else	
    SectRgn(cursrgn, window->visRgn, cursrgn);
#endif
}

/*
 * Enable/disable menu items based on the active terminal window.
 */
#if TARGET_API_MAC_CARBON
#define DisableItem DisableMenuItem
#define EnableItem EnableMenuItem
#endif
static void mac_adjusttermmenus(WindowPtr window)
{
    Session *s;
    MenuHandle menu;
#if !TARGET_API_MAC_CARBON
    long offset;
#endif

    s = mac_windowsession(window);
    menu = GetMenuHandle(mFile);
    DisableItem(menu, iSave); /* XXX enable if modified */
    EnableItem(menu, iSaveAs);
    EnableItem(menu, iChange);
    EnableItem(menu, iDuplicate);
    menu = GetMenuHandle(mEdit);
    EnableItem(menu, 0);
    DisableItem(menu, iUndo);
    DisableItem(menu, iCut);
    if (1/*s->term->selstate == SELECTED*/)
	EnableItem(menu, iCopy);
    else
	DisableItem(menu, iCopy);
#if TARGET_API_MAC_CARBON
    if (1)
#else
    if (GetScrap(NULL, kScrapFlavorTypeText, &offset) == noTypeErr)
#endif
	DisableItem(menu, iPaste);
    else
	EnableItem(menu, iPaste);
    DisableItem(menu, iClear);
    EnableItem(menu, iSelectAll);
    menu = GetMenuHandle(mWindow);
    EnableItem(menu, 0);
    EnableItem(menu, iShowEventLog);
}

static void mac_menuterm(WindowPtr window, short menu, short item)
{
    Session *s;

    s = mac_windowsession(window);
    switch (menu) {
      case mEdit:
	switch (item) {
	  case iCopy:
	    /* term_copy(s); */
	    break;
	  case iPaste:
	    term_do_paste(s->term);
	    break;
	}
	break;
      case mWindow:
	switch(item) {
	  case iShowEventLog:
	    mac_showeventlog(s);
	    break;
	}
	break;
    }
}
	    
static void mac_clickterm(WindowPtr window, EventRecord *event)
{
    Session *s;
    Point mouse;
    ControlHandle control;
    int part;
    static ControlActionUPP mac_scrolltracker_upp = NULL;

    s = mac_windowsession(window);
    SetPort((GrafPtr)GetWindowPort(window));
    mouse = event->where;
    GlobalToLocal(&mouse);
    part = FindControl(mouse, window, &control);
    if (control == s->scrollbar) {
	switch (part) {
	  case kControlIndicatorPart:
	    if (TrackControl(control, mouse, NULL) == kControlIndicatorPart)
		term_scroll(s->term, +1, GetControlValue(control));
	    break;
	  case kControlUpButtonPart:
	  case kControlDownButtonPart:
	  case kControlPageUpPart:
	  case kControlPageDownPart:
	    if (mac_scrolltracker_upp == NULL)
		mac_scrolltracker_upp =
		    NewControlActionUPP(&mac_scrolltracker);
	    TrackControl(control, mouse, mac_scrolltracker_upp);
	    break;
	}
    } else {
	text_click(s, event);
    }
}

static void text_click(Session *s, EventRecord *event)
{
    Point localwhere;
    int row, col;
    static UInt32 lastwhen = 0;
    static Session *lastsess = NULL;
    static int lastrow = -1, lastcol = -1;
    static Mouse_Action lastact = MA_NOTHING;

    SetPort((GrafPtr)GetWindowPort(s->window));
    localwhere = event->where;
    GlobalToLocal(&localwhere);

    col = PTOCC(localwhere.h);
    row = PTOCR(localwhere.v);
    if (event->when - lastwhen < GetDblTime() &&
	row == lastrow && col == lastcol && s == lastsess)
	lastact = (lastact == MA_CLICK ? MA_2CLK :
		   lastact == MA_2CLK ? MA_3CLK :
		   lastact == MA_3CLK ? MA_CLICK : MA_NOTHING);
    else
	lastact = MA_CLICK;
    term_mouse(s->term, MBT_LEFT,
	       event->modifiers & shiftKey ? MBT_EXTEND : MBT_SELECT,
	       lastact, col, row, event->modifiers & shiftKey,
	       event->modifiers & controlKey, event->modifiers & optionKey);
    lastsess = s;
    lastrow = row;
    lastcol = col;
    while (StillDown()) {
	GetMouse(&localwhere);
	col = PTOCC(localwhere.h);
	row = PTOCR(localwhere.v);
	term_mouse(s->term, MBT_LEFT, 
		   event->modifiers & shiftKey ? MBT_EXTEND : MBT_SELECT,
		   MA_DRAG, col, row, event->modifiers & shiftKey,
		   event->modifiers & controlKey,
		   event->modifiers & optionKey);
	if (row > s->term->rows - 1)
	    term_scroll(s->term, 0, row - (s->term->rows - 1));
	else if (row < 0)
	    term_scroll(s->term, 0, row);
    }
    term_mouse(s->term, MBT_LEFT,
	       event->modifiers & shiftKey ? MBT_EXTEND : MBT_SELECT,
	       MA_RELEASE, col, row, event->modifiers & shiftKey,
	       event->modifiers & controlKey, event->modifiers & optionKey);
    lastwhen = TickCount();
}

void write_clip(void *cookie, wchar_t *data, int len, int must_deselect)
{
#if !TARGET_API_MAC_CARBON
    Session *s = cookie;
    char *mactextbuf;
    ByteCount iread, olen;
    wchar_t *unitextptr;
    StScrpRec *stsc;
    size_t stsz;
    OSErr err;
    int i;

    /*
     * See "Programming with the Text Encoding Conversion Manager"
     * Appendix E for Unicode scrap conventions.
     *
     * XXX Maybe PICT scrap too.
     */
    if (ZeroScrap() != noErr)
	return;
    PutScrap(len * sizeof(*data), kScrapFlavorTypeUnicode, data);

    /* Replace LINE SEPARATORs with CR for TEXT output. */
    for (i = 0; i < len; i++)
	if (data[i] == 0x2028)
	    data[i] = 0x000d;

    mactextbuf = snewn(len, char); /* XXX DBCS */
    if (s->uni_to_font != NULL) {
	err = ConvertFromUnicodeToText(s->uni_to_font, len * sizeof(UniChar),
				       (UniChar *)data,
				       kUnicodeUseFallbacksMask,
				       0, NULL, NULL, NULL,
				       len, &iread, &olen, mactextbuf);
	if (err != noErr && err != kTECUsedFallbacksStatus)
	    return;
    } else  if (s->font_charset != CS_NONE) {
	unitextptr = data;
	olen = charset_from_unicode(&unitextptr, &len, mactextbuf, 1024,
				    s->font_charset, NULL, ".", 1);
    } else
	return;
    PutScrap(olen, kScrapFlavorTypeText, mactextbuf);
    sfree(mactextbuf);

    stsz = offsetof(StScrpRec, scrpStyleTab) + sizeof(ScrpSTElement);
    stsc = smalloc(stsz);
    stsc->scrpNStyles = 1;
    stsc->scrpStyleTab[0].scrpStartChar = 0;
    stsc->scrpStyleTab[0].scrpHeight = s->font_height;
    stsc->scrpStyleTab[0].scrpAscent = s->font_ascent;
    stsc->scrpStyleTab[0].scrpFont = s->fontnum;
    stsc->scrpStyleTab[0].scrpFace = 0;
    stsc->scrpStyleTab[0].scrpSize = s->cfg.font.size;
    stsc->scrpStyleTab[0].scrpColor.red = 0;
    stsc->scrpStyleTab[0].scrpColor.green = 0;
    stsc->scrpStyleTab[0].scrpColor.blue = 0;
    PutScrap(stsz, kScrapFlavorTypeTextStyle, stsc);
    sfree(stsc);
#endif
}

void get_clip(void *frontend, wchar_t **p, int *lenp)
{
#if TARGET_API_MAC_CARBON
    *lenp = 0;
#else
    Session *s = frontend;
    static Handle h = NULL;
    static wchar_t *data = NULL;
    Handle texth;
    long offset;
    int textlen;
    TextEncoding enc;
    TextToUnicodeInfo scrap_to_uni;
    ByteCount iread, olen;
    int charset;
    char *tptr;
    OSErr err;

    if (p == NULL) {
	/* release memory */
	if (h != NULL)
	    DisposeHandle(h);
	h = NULL;
	if (data != NULL)
	    sfree(data);
	data = NULL;
    } else {
	if (GetScrap(NULL, kScrapFlavorTypeUnicode, &offset) > 0) {
	    if (h == NULL)
		h = NewHandle(0);
	    *lenp =
		GetScrap(h, kScrapFlavorTypeUnicode, &offset) / sizeof(**p);
	    HLock(h);
	    *p = (wchar_t *)*h;
	} else if (GetScrap(NULL, kScrapFlavorTypeText, &offset) > 0) {
	    texth = NewHandle(0);
	    textlen = GetScrap(texth, kScrapFlavorTypeText, &offset);
	    HLock(texth);
	    data = snewn(textlen, wchar_t);
	    /* XXX should use 'styl' scrap if it's there. */
	    if (mac_gestalts.encvvers != 0 &&
		UpgradeScriptInfoToTextEncoding(smSystemScript,
						kTextLanguageDontCare,
						kTextRegionDontCare, NULL,
						&enc) == noErr &&
		CreateTextToUnicodeInfoByEncoding(enc, &scrap_to_uni) ==
		noErr) {
		err = ConvertFromTextToUnicode(scrap_to_uni, textlen,
					       *texth, 0, 0, NULL, NULL, NULL,
					       textlen * 2,
					       &iread, &olen, data);
		DisposeTextToUnicodeInfo(&scrap_to_uni);
		if (err == noErr) {
		    *p = data;
		    *lenp = olen / sizeof(**p);
		} else {
		    *p = NULL;
		    *lenp = 0;
		}
	    } else {
		charset =
		    charset_from_macenc(GetScriptManagerVariable(smSysScript),
					GetScriptManagerVariable(smRegionCode),
					mac_gestalts.sysvers, NULL);
		if (charset != CS_NONE) {
		    tptr = *texth;
		    *lenp = charset_to_unicode(&tptr, &textlen, data,
					       textlen * 2, charset, NULL,
					       NULL, 0);
		}
		*p = data;
	    }
	    DisposeHandle(texth);
	} else {
	    *p = NULL;
	    *lenp = 0;
	}
    }
#endif
}

static pascal void mac_scrolltracker(ControlHandle control, short part)
{
    Session *s;

#if TARGET_API_MAC_CARBON
    s = mac_windowsession(GetControlOwner(control));
#else
    s = mac_windowsession((*control)->contrlOwner);
#endif
    switch (part) {
      case kControlUpButtonPart:
	term_scroll(s->term, 0, -1);
	break;
      case kControlDownButtonPart:
	term_scroll(s->term, 0, +1);
	break;
      case kControlPageUpPart:
	term_scroll(s->term, 0, -(s->term->rows - 1));
	break;
      case kControlPageDownPart:
	term_scroll(s->term, 0, +(s->term->rows - 1));
	break;
    }
}

static void mac_keyterm(WindowPtr window, EventRecord *event)
{
    Session *s = mac_windowsession(window);
    Key_Sym keysym = PK_NULL;
    unsigned int mods = 0, flags = PKF_NUMLOCK;
    UniChar utxt[1];
    char txt[1];
    size_t len = 0;
    ScriptCode key_script;

    ObscureCursor();

#if 0
    fprintf(stderr, "Got key event %08x\n", event->message);
#endif

    /* No meta key yet -- that'll be rather fun. */

    /* Keys that we handle locally */
    if (event->modifiers & shiftKey) {
	switch ((event->message & keyCodeMask) >> 8) {
	  case 0x74: /* shift-pageup */
	    term_scroll(s->term, 0, -(s->term->rows - 1));
	    return;
	  case 0x79: /* shift-pagedown */
	    term_scroll(s->term, 0, +(s->term->rows - 1));
	    return;
	}
    }

    if (event->modifiers & shiftKey)
	mods |= PKM_SHIFT;
    if (event->modifiers & controlKey)
	mods |= PKM_CONTROL;
    if (event->what == autoKey)
	flags |= PKF_REPEAT;

    /* Mac key events consist of a virtual key code and a character code. */

    switch ((event->message & keyCodeMask) >> 8) {
      case 0x24: keysym = PK_RETURN; break;
      case 0x30: keysym = PK_TAB; break;
      case 0x33: keysym = PK_BACKSPACE; break;
      case 0x35: keysym = PK_ESCAPE; break;

      case 0x7A: keysym = PK_F1; break;
      case 0x78: keysym = PK_F2; break;
      case 0x63: keysym = PK_F3; break;
      case 0x76: keysym = PK_F4; break;
      case 0x60: keysym = PK_F5; break;
      case 0x61: keysym = PK_F6; break;
      case 0x62: keysym = PK_F7; break;
      case 0x64: keysym = PK_F8; break;
      case 0x65: keysym = PK_F9; break;
      case 0x6D: keysym = PK_F10; break;
      case 0x67: keysym = PK_F11; break;
      case 0x6F: keysym = PK_F12; break;
      case 0x69: keysym = PK_F13; break;
      case 0x6B: keysym = PK_F14; break;
      case 0x71: keysym = PK_F15; break;

      case 0x72: keysym = PK_INSERT; break;
      case 0x73: keysym = PK_HOME; break;
      case 0x74: keysym = PK_PAGEUP; break;
      case 0x75: keysym = PK_DELETE; break;
      case 0x77: keysym = PK_END; break;
      case 0x79: keysym = PK_PAGEDOWN; break;

      case 0x47: keysym = PK_PF1; break;
      case 0x51: keysym = PK_PF2; break;
      case 0x4B: keysym = PK_PF3; break;
      case 0x43: keysym = PK_PF4; break;
      case 0x4E: keysym = PK_KPMINUS; break;
      case 0x45: keysym = PK_KPCOMMA; break;
      case 0x41: keysym = PK_KPDECIMAL; break;
      case 0x4C: keysym = PK_KPENTER; break;
      case 0x52: keysym = PK_KP0; break;
      case 0x53: keysym = PK_KP1; break;
      case 0x54: keysym = PK_KP2; break;
      case 0x55: keysym = PK_KP3; break;
      case 0x56: keysym = PK_KP4; break;
      case 0x57: keysym = PK_KP5; break;
      case 0x58: keysym = PK_KP6; break;
      case 0x59: keysym = PK_KP7; break;
      case 0x5B: keysym = PK_KP8; break;
      case 0x5C: keysym = PK_KP9; break;

      case 0x7B: keysym = PK_LEFT; break;
      case 0x7C: keysym = PK_RIGHT; break;
      case 0x7D: keysym = PK_DOWN; break;
      case 0x7E: keysym = PK_UP; break;
    }

    /* Map from key script to Unicode. */
    txt[0] = event->message & charCodeMask;
    key_script = GetScriptManagerVariable(smKeyScript);

    if (mac_gestalts.encvvers != 0) {
	static TextToUnicodeInfo key_to_uni = NULL;
	static ScriptCode key_to_uni_script;
	TextEncoding enc;
	ByteCount iread, olen;
	OSErr err;

	if (key_to_uni != NULL && key_to_uni_script != key_script)
	    DisposeTextToUnicodeInfo(&key_to_uni);
        if (key_to_uni == NULL || key_to_uni_script != key_script) {
	    if (UpgradeScriptInfoToTextEncoding(key_script,
						kTextLanguageDontCare,
						kTextRegionDontCare, NULL,
						&enc) == noErr &&
		CreateTextToUnicodeInfoByEncoding(enc, &key_to_uni) == noErr)
		key_to_uni_script = key_script;
	    else
		key_to_uni = NULL;
	}
	if (key_to_uni != NULL) {
	    err = ConvertFromTextToUnicode(key_to_uni, 1, txt,
					   (kUnicodeKeepInfoMask |
					    kUnicodeStringUnterminatedMask),
					   0, NULL, NULL, NULL,
					   sizeof(utxt), &iread, &olen, utxt);
	    if (err == noErr)
		len = olen / sizeof(*utxt);
	}
    } else {
	int charset;
	char *tptr = txt;
	int tlen = 1;

	charset = charset_from_macenc(key_script,
				      GetScriptManagerVariable(smRegionCode),
				      mac_gestalts.sysvers, NULL);
	if (charset != CS_NONE) {
	    len = charset_to_unicode(&tptr, &tlen, utxt, sizeof(utxt), charset,
				     NULL, NULL, 0);
	}
    }
    term_key(s->term, keysym, utxt, len, mods, flags);
}

void request_paste(void *frontend)
{
    Session *s = frontend;

    /*
     * In the Mac OS, pasting is synchronous: we can read the
     * clipboard with no difficulty, so request_paste() can just go
     * ahead and paste.
     */
    term_do_paste(s->term);
}

static struct {
    Rect msgrect;
    Point msgorigin;
    Point zeromouse;
    Session *s;
    char oldmsg[20];
} growterm_state;

static void mac_growterm(WindowPtr window, EventRecord *event)
{
    Rect limits;
    long grow_result;
    int newrows, newcols;
    Session *s;
#if !TARGET_API_MAC_CARBON
    DragGrayRgnUPP draghooksave;
    GrafPtr portsave;
    FontInfo fi;
#endif

    s = mac_windowsession(window);

#if !TARGET_API_MAC_CARBON
    draghooksave = LMGetDragHook();
    growterm_state.oldmsg[0] = '\0';
    growterm_state.zeromouse = event->where;
    growterm_state.zeromouse.h -= s->term->cols * s->font_width;
    growterm_state.zeromouse.v -= s->term->rows * s->font_height;
    growterm_state.s = s;
    GetPort(&portsave);
    SetPort(s->window);
    BackColor(whiteColor);
    ForeColor(blackColor);
    TextFont(systemFont);
    TextFace(0);
    TextSize(12);
    GetFontInfo(&fi);
    SetRect(&growterm_state.msgrect, 0, 0,
	    StringWidth("\p99999x99999") + 4, fi.ascent + fi.descent + 4);
    SetPt(&growterm_state.msgorigin, 2, fi.ascent + 2);
    LMSetDragHook(NewDragGrayRgnUPP(mac_growtermdraghook));
#endif

    SetRect(&limits, s->font_width + 15, s->font_height, SHRT_MAX, SHRT_MAX);
    grow_result = GrowWindow(window, event->where, &limits);

#if !TARGET_API_MAC_CARBON
    DisposeDragGrayRgnUPP(LMGetDragHook());
    LMSetDragHook(draghooksave);
    InvalRect(&growterm_state.msgrect);

    SetPort(portsave);
#endif

    if (grow_result != 0) {
	newrows = HiWord(grow_result) / s->font_height;
	newcols = (LoWord(grow_result) - 15) / s->font_width;
	mac_adjustsize(s, newrows, newcols);
	term_size(s->term, newrows, newcols, s->cfg.savelines);
	s->cfg.height=s->term->rows;
	s->cfg.width=s->term->cols;
    }
}

#if !TARGET_API_MAC_CARBON
static pascal void mac_growtermdraghook(void)
{
    Session *s = growterm_state.s;
    GrafPtr portsave;
    Point mouse;
    char buf[20];
    unsigned char pbuf[20];
    int newrows, newcols;
    
    GetMouse(&mouse);
    newrows = (mouse.v - growterm_state.zeromouse.v) / s->font_height;
    if (newrows < 1) newrows = 1;
    newcols = (mouse.h - growterm_state.zeromouse.h) / s->font_width;
    if (newcols < 1) newcols = 1;
    sprintf(buf, "%dx%d", newcols, newrows);
    if (strcmp(buf, growterm_state.oldmsg) == 0)
	return;
    strcpy(growterm_state.oldmsg, buf);
    c2pstrcpy(pbuf, buf);

    GetPort(&portsave);
    SetPort(growterm_state.s->window);
    EraseRect(&growterm_state.msgrect);
    MoveTo(growterm_state.msgorigin.h, growterm_state.msgorigin.v);
    DrawString(pbuf);
    SetPort(portsave);
}
#endif

void mac_closeterm(WindowPtr window)
{
    int alertret;
    Session *s = mac_windowsession(window);

    if (s->cfg.warn_on_close) {
	ParamText("\pAre you sure you want to close this session?",
		  NULL, NULL, NULL);
	alertret=CautionAlert(wQuestion, NULL);
	if (alertret == 2) {
	    /* Cancel */
	    return;
	}
    }

    HideWindow(s->window);
    *s->prev = s->next;
    s->next->prev = s->prev;
    ldisc_free(s->ldisc);
    s->back->free(s->backhandle);
    log_free(s->logctx);
    if (s->uni_to_font != NULL)
	DisposeUnicodeToTextInfo(&s->uni_to_font);
    term_free(s->term);
    mac_freeeventlog(s);
    sfree((WinInfo *)GetWRefCon(s->window));
    DisposeWindow(s->window);
    DisposePalette(s->palette);
    sfree(s);
}

static void mac_activateterm(WindowPtr window, EventRecord *event)
{
    Session *s;
    Boolean active = (event->modifiers & activeFlag) != 0;

    s = mac_windowsession(window);
    term_set_focus(s->term, active);
    term_update(s->term);
    if (active && s->cfg.scrollbar)
	ShowControl(s->scrollbar);
    else {
	if (HAVE_COLOR_QD())
	    PmBackColor(DEFAULT_BG);/* HideControl clears behind the control */
	else
	    BackColor(blackColor);
	HideControl(s->scrollbar);
    }
    mac_drawgrowicon(s);
}

static void mac_updateterm(WindowPtr window)
{
    Session *s;
    Rect bbox;
#if TARGET_API_MAC_CARBON
    RgnHandle visrgn;
#endif

    s = mac_windowsession(window);
    SetPort((GrafPtr)GetWindowPort(window));
    BeginUpdate(window);
    pre_paint(s);
#if TARGET_API_MAC_CARBON
    visrgn = NewRgn();
    GetPortVisibleRegion(GetWindowPort(window), visrgn);
    GetRegionBounds(visrgn, &bbox);
#else
    bbox = (*window->visRgn)->rgnBBox;
#endif
    term_paint(s->term, s, PTOCC(bbox.left), PTOCR(bbox.top),
	       PTOCC(bbox.right), PTOCR(bbox.bottom), 1);
    /* Restore default colours in case the Window Manager uses them */
    if (HAVE_COLOR_QD()) {
	PmForeColor(DEFAULT_FG);
	PmBackColor(DEFAULT_BG);
    } else {
	ForeColor(whiteColor);
	BackColor(blackColor);
    }
    if (FrontWindow() != window)
#if TARGET_API_MAC_CARBON
	EraseRect(GetControlBounds(s->scrollbar, &bbox));
    UpdateControls(window, visrgn);
    DisposeRgn(visrgn);
#else
	EraseRect(&(*s->scrollbar)->contrlRect);
    UpdateControls(window, window->visRgn);
#endif
    mac_drawgrowicon(s);
    post_paint(s);
    EndUpdate(window);
}

static void mac_drawgrowicon(Session *s)
{
    Rect clip;
    RgnHandle savergn;

    SetPort((GrafPtr)GetWindowPort(s->window));
    /*
     * Stop DrawGrowIcon giving us space for a horizontal scrollbar
     * See Tech Note TB575 for details.
     */
#if TARGET_API_MAC_CARBON
    GetPortBounds(GetWindowPort(s->window), &clip);
#else
    clip = s->window->portRect;
#endif
    clip.left = clip.right - 15;
    savergn = NewRgn();
    GetClip(savergn);
    ClipRect(&clip);
    DrawGrowIcon(s->window);
    SetClip(savergn);
    DisposeRgn(savergn);
}    

struct do_text_args {
    Session *s;
    Rect textrect;
    char *text;
    int len;
    unsigned long attr;
    int lattr;
    Point numer, denom;
};

/*
 * Call from the terminal emulator to draw a bit of text
 *
 * x and y are text row and column (zero-based)
 */
static void do_text_internal(Context ctx, int x, int y, wchar_t *text, int len,
		      unsigned long attr, int lattr)
{
    Session *s = ctx;
    int style;
    struct do_text_args a;
    RgnHandle textrgn, saveclip;
#if TARGET_API_MAC_CARBON
    RgnHandle visrgn;
#endif
    char mactextbuf[1024];
    wchar_t *unitextptr;
    int fontwidth;
    ByteCount iread, olen;
    OSStatus err;
    static DeviceLoopDrawingUPP do_text_for_device_upp = NULL;

    assert(len <= 1024);

    SetPort((GrafPtr)GetWindowPort(s->window));

    fontwidth = s->font_width;
    if ((lattr & LATTR_MODE) != LATTR_NORM)
	fontwidth *= 2;

    /* First check this text is relevant */
    a.textrect.top = y * s->font_height;
    a.textrect.bottom = (y + 1) * s->font_height;
    a.textrect.left = x * fontwidth;
    a.textrect.right = (x + len) * fontwidth;
    if (a.textrect.right > s->term->cols * s->font_width)
	a.textrect.right = s->term->cols * s->font_width;
#if TARGET_API_MAC_CARBON
    visrgn = NewRgn();
    GetPortVisibleRegion(GetWindowPort(s->window), visrgn);
    if (!RectInRgn(&a.textrect, visrgn)) {
	DisposeRgn(visrgn);
	return;
    }
    DisposeRgn(visrgn);
#else
    if (!RectInRgn(&a.textrect, s->window->visRgn))
	return;
#endif

    if (s->uni_to_font != NULL) {
	err = ConvertFromUnicodeToText(s->uni_to_font, len * sizeof(UniChar),
				       text, kUnicodeUseFallbacksMask,
				       0, NULL, NULL, NULL,
				       1024, &iread, &olen, mactextbuf);
	if (err != noErr && err != kTECUsedFallbacksStatus)
	    olen = 0;
    } else  if (s->font_charset != CS_NONE) {
	/* XXX this is bogus if wchar_t and UniChar are different sizes. */
	unitextptr = (wchar_t *)text;
	olen = charset_from_unicode(&unitextptr, &len, mactextbuf, 1024,
				    s->font_charset, NULL, ".", 1);
    } else
	olen = 0;

    a.s = s;
    a.text = mactextbuf;
    a.len = olen;
    a.attr = attr;
    a.lattr = lattr;
    switch (lattr & LATTR_MODE) {
      case LATTR_NORM:
	TextSize(s->cfg.font.size);
	a.numer = s->font_stdnumer;
	a.denom = s->font_stddenom;
	break;
      case LATTR_WIDE:
	TextSize(s->cfg.font.size);
	a.numer = s->font_widenumer;
	a.denom = s->font_widedenom;
	break;
      case LATTR_TOP:
      case LATTR_BOT:
	TextSize(s->cfg.font.size * 2);
	a.numer = s->font_bignumer;
	a.denom = s->font_bigdenom;
	break;
    }
    SetPort((GrafPtr)GetWindowPort(s->window));
    TextFont(s->fontnum);
    style = s->cfg.font.face;
    if ((attr & ATTR_BOLD) && !s->cfg.bold_colour)
    	style |= bold;
    if (attr & ATTR_UNDER)
	style |= underline;
    TextFace(style);
    TextMode(srcOr);
    if (HAVE_COLOR_QD())
	if (style & bold) {
	    SpaceExtra(s->font_boldadjust << 16);
	    CharExtra(s->font_boldadjust << 16);
	} else {
	    SpaceExtra(0);
	    CharExtra(0);
	}
    saveclip = NewRgn();
    GetClip(saveclip);
    ClipRect(&a.textrect);
    textrgn = NewRgn();
    RectRgn(textrgn, &a.textrect);
    if (HAVE_COLOR_QD()) {
	if (do_text_for_device_upp == NULL)
	    do_text_for_device_upp =
		NewDeviceLoopDrawingUPP(&do_text_for_device);
	DeviceLoop(textrgn, do_text_for_device_upp, (long)&a, 0);
    } else
	do_text_for_device(1, 0, NULL, (long)&a);
    SetClip(saveclip);
    DisposeRgn(saveclip);
    DisposeRgn(textrgn);
    /* Tell the window manager about it in case this isn't an update */
#if TARGET_API_MAC_CARBON
    ValidWindowRect(s->window, &a.textrect);
#else
    ValidRect(&a.textrect);
#endif
}

/*
 * Wrapper that handles combining characters.
 */
void do_text(Context ctx, int x, int y, wchar_t *text, int len,
	     unsigned long attr, int lattr)
{
    if (attr & TATTR_COMBINING) {
	unsigned long a = 0;
	attr &= ~TATTR_COMBINING;
	while (len--) {
	    do_text_internal(ctx, x, y, text, 1, attr | a, lattr);
	    text++;
	    a = TATTR_COMBINING;
	}
    } else
	do_text_internal(ctx, x, y, text, len, attr, lattr);
}

static pascal void do_text_for_device(short depth, short devflags,
				      GDHandle device, long cookie)
{
    struct do_text_args *a = (struct do_text_args *)cookie;
    int bgcolour, fgcolour, bright, reverse, tmp;
#if TARGET_API_MAC_CARBON
    CQDProcsPtr gp = GetPortGrafProcs(GetWindowPort(a->s->window));
#else
    QDProcsPtr gp = a->s->window->grafProcs;
#endif

    bright = (a->attr & ATTR_BOLD) && a->s->cfg.bold_colour;
    reverse = a->attr & ATTR_REVERSE;

    if (depth == 1 && (a->attr & TATTR_ACTCURS))
	reverse = !reverse;

    if (HAVE_COLOR_QD()) {
	if (depth > 2) {
	    fgcolour = ((a->attr & ATTR_FGMASK) >> ATTR_FGSHIFT);
	    bgcolour = ((a->attr & ATTR_BGMASK) >> ATTR_BGSHIFT);
	} else {
	    /*
	     * NB: bold reverse in 2bpp breaks with the usual PuTTY model and
	     * boldens the background, because that's all we can do.
	     */
	    fgcolour = bright ? DEFAULT_FG_BOLD : DEFAULT_FG;
	    bgcolour = DEFAULT_BG;
	}
	if (reverse) {
	    tmp = fgcolour;
	    fgcolour = bgcolour;
	    bgcolour = tmp;
	}
	if (bright && depth > 2)
	    if (fgcolour < 16) fgcolour |=8;
	    else if (fgcolour >= 256) fgcolour |=1;
	if ((a->attr & TATTR_ACTCURS) && depth > 1) {
	    fgcolour = CURSOR_FG;
	    bgcolour = CURSOR_BG;
	}
	PmForeColor(fgcolour);
	PmBackColor(bgcolour);
    } else { /* No Color Quickdraw */
	/* XXX This should be done with a _little_ more configurability */
	if (reverse) {
	    ForeColor(blackColor);
	    BackColor(whiteColor);
	} else {
	    ForeColor(whiteColor);
	    BackColor(blackColor);
	}
    }

    if (!(a->attr & TATTR_COMBINING))
	EraseRect(&a->textrect);
    switch (a->lattr & LATTR_MODE) {
      case LATTR_NORM:
      case LATTR_WIDE:
	MoveTo(a->textrect.left, a->textrect.top + a->s->font_ascent);
	break;
      case LATTR_TOP:
	MoveTo(a->textrect.left, a->textrect.top + a->s->font_ascent * 2);
	break;
      case LATTR_BOT:
	MoveTo(a->textrect.left,
	       a->textrect.top - a->s->font_height + a->s->font_ascent * 2);
	break;
    }
    /* FIXME: Sort out bold width adjustments on Original QuickDraw. */
    if (gp != NULL)
	InvokeQDTextUPP(a->len, a->text, a->numer, a->denom, gp->textProc);
    else
	StdText(a->len, a->text, a->numer, a->denom);

    if (a->attr & TATTR_PASCURS) {
	PenNormal();
	switch (depth) {
	  case 1:
	    PenMode(patXor);
	    break;
	  default:
	    PmForeColor(CURSOR_BG);
	    break;
	}
	FrameRect(&a->textrect);
    }
}

void do_cursor(Context ctx, int x, int y, wchar_t *text, int len,
	     unsigned long attr, int lattr)
{

    do_text(ctx, x, y, text, len, attr, lattr);
}

/*
 * Call from the terminal emulator to get its graphics context.
 * Should probably be called start_redraw or something.
 */
void pre_paint(Session *s)
{
    GDHandle gdh;
    Rect myrect, tmprect;
#if TARGET_API_MAC_CARBON
    RgnHandle visrgn;
#endif

    if (HAVE_COLOR_QD()) {
	s->term->attr_mask = 0;
	SetPort((GrafPtr)GetWindowPort(s->window));
#if TARGET_API_MAC_CARBON
	visrgn = NewRgn();
	GetPortVisibleRegion(GetWindowPort(s->window), visrgn);
	GetRegionBounds(visrgn, &myrect);
	DisposeRgn(visrgn);
#else
	myrect = (*s->window->visRgn)->rgnBBox;
#endif
	LocalToGlobal((Point *)&myrect.top);
	LocalToGlobal((Point *)&myrect.bottom);
	for (gdh = GetDeviceList();
	     gdh != NULL;
	     gdh = GetNextDevice(gdh)) {
	    if (TestDeviceAttribute(gdh, screenDevice) &&
		TestDeviceAttribute(gdh, screenActive) &&
		SectRect(&(*gdh)->gdRect, &myrect, &tmprect)) {
		switch ((*(*gdh)->gdPMap)->pixelSize) {
		  case 1:
		    if (s->cfg.bold_colour)
			s->term->attr_mask |= ~(ATTR_COLOURS |
			    (s->cfg.bold_colour ? ATTR_BOLD : 0));
		    break;
		  case 2:
		    s->term->attr_mask |= ~ATTR_COLOURS;
		    break;
		  default:
		    s->term->attr_mask = ~0;
		    return; /* No point checking more screens. */
		}
	    }
	}
    } else
	s->term->attr_mask = ~(ATTR_COLOURS |
			        (s->cfg.bold_colour ? ATTR_BOLD : 0));
}

Context get_ctx(void *frontend)
{
    Session *s = frontend;

    pre_paint(s);
    return s;
}

void free_ctx(Context ctx)
{

}

/*
 * Presumably this does something in Windows
 */
void post_paint(Session *s)
{

}

/*
 * Set the scroll bar position
 *
 * total is the line number of the bottom of the working screen
 * start is the line number of the top of the display
 * page is the length of the displayed page
 */
void set_sbar(void *frontend, int total, int start, int page)
{
    Session *s = frontend;

    /* We don't redraw until we've set everything up, to avoid glitches */
    SetControlMinimum(s->scrollbar, 0);
    SetControlMaximum(s->scrollbar, total - page);
    SetControlValue(s->scrollbar, start);
#if !TARGET_CPU_68K
    if (mac_gestalts.cntlattr & gestaltControlMgrPresent)
	SetControlViewSize(s->scrollbar, page);
#endif
}

void sys_cursor(void *frontend, int x, int y)
{
    /*
     * I think his is meaningless under Mac OS.
     */
}

/*
 * This is still called when mode==BELL_VISUAL, even though the
 * visual bell is handled entirely within terminal.c, because we
 * may want to perform additional actions on any kind of bell (for
 * example, taskbar flashing in Windows).
 */
void beep(void *frontend, int mode)
{
    if (mode != BELL_VISUAL)
	SysBeep(30);
    /*
     * XXX We should indicate the relevant window and/or use the
     * Notification Manager
     */
}

int char_width(Context ctx, int uc)
{
    /*
     * Until we support exciting character-set stuff, assume all chars are
     * single-width.
     */
    return 1;
}

/*
 * Set icon string -- a no-op here (Windowshade?)
 */
void set_icon(void *frontend, char *icon) {
    Session *s = frontend;

}

/*
 * Set the window title
 */
void set_title(void *frontend, char *title)
{
    Session *s = frontend;
    Str255 mactitle;

    c2pstrcpy(mactitle, title);
    SetWTitle(s->window, mactitle);
}

/*
 * Used by backend to indicate busy-ness
 */
void set_busy_status(void *frontend, int status)
{
    /* FIXME do something */
}

/*
 * set or clear the "raw mouse message" mode
 */
void set_raw_mouse_mode(void *frontend, int activate)
{
    Session *s = frontend;

    s->raw_mouse = activate;
    /* FIXME: Should call mac_updatetermcursor as appropriate. */
}

/*
 * Resize the window at the emulator's request
 */
void request_resize(void *frontend, int w, int h)
{
    Session *s = frontend;
    RgnHandle grayrgn;
    Rect graybox;
    int wlim, hlim;

    /* Arbitrarily clip to the size of the desktop. */
    grayrgn = GetGrayRgn();
#if TARGET_API_MAC_CARBON
    GetRegionBounds(grayrgn, &graybox);
#else
    graybox = (*grayrgn)->rgnBBox;
#endif
    wlim = (graybox.right - graybox.left) / s->font_width;
    hlim = (graybox.bottom - graybox.top) / s->font_height;
    if (w > wlim) w = wlim;
    if (h > hlim) h = hlim;
    term_size(s->term, h, w, s->cfg.savelines);
    mac_initfont(s);
}

/*
 * Iconify (actually collapse) the window at the emulator's request.
 */
void set_iconic(void *frontend, int iconic)
{
    Session *s = frontend;
    UInt32 features;

    if (mac_gestalts.apprvers >= 0x0100 &&
	GetWindowFeatures(s->window, &features) == noErr &&
	(features & kWindowCanCollapse))
	CollapseWindow(s->window, iconic);
}

/*
 * Move the window in response to a server-side request.
 */
void move_window(void *frontend, int x, int y)
{
    Session *s = frontend;

    MoveWindow(s->window, x, y, FALSE);
}

/*
 * Move the window to the top or bottom of the z-order in response
 * to a server-side request.
 */
void set_zorder(void *frontend, int top)
{
    Session *s = frontend;

    /* 
     * We also change the input focus to point to the topmost window,
     * since that's probably what the Human Interface Guidelines would
     * like us to do.
     */
    if (top)
	SelectWindow(s->window);
    else
	SendBehind(s->window, NULL);
}

/*
 * Refresh the window in response to a server-side request.
 */
void refresh_window(void *frontend)
{
    Session *s = frontend;

    term_invalidate(s->term);
}

/*
 * Maximise or restore the window in response to a server-side
 * request.
 */
void set_zoomed(void *frontend, int zoomed)
{
    Session *s = frontend;

    ZoomWindow(s->window, zoomed ? inZoomOut : inZoomIn, FALSE);
}

/*
 * Report whether the window is iconic, for terminal reports.
 */
int is_iconic(void *frontend)
{
    Session *s = frontend;
    UInt32 features;

    if (mac_gestalts.apprvers >= 0x0100 &&
	GetWindowFeatures(s->window, &features) == noErr &&
	(features & kWindowCanCollapse))
	return IsWindowCollapsed(s->window);
    return FALSE;
}

/*
 * Report the window's position, for terminal reports.
 */
void get_window_pos(void *frontend, int *x, int *y)
{
    Session *s = frontend;
    Rect rect;

#if TARGET_API_MAC_CARBON
    GetPortBounds(GetWindowPort(s->window), &rect);
#else
    rect = s->window->portRect;
#endif
    *x = rect.left;
    *y = rect.top;
}

/*
 * Report the window's pixel size, for terminal reports.
 */
void get_window_pixels(void *frontend, int *x, int *y)
{
    Session *s = frontend;
    Rect rect;

#if TARGET_API_MAC_CARBON
    GetPortBounds(GetWindowPort(s->window), &rect);
#else
    rect = s->window->portRect;
#endif
    *x = rect.right - rect.left;
    *y = rect.bottom - rect.top;
}

/*
 * Return the window or icon title.
 */
char *get_window_title(void *frontend, int icon)
{
    Session *s = frontend;
    Str255 ptitle;
    static char title[256];

    GetWTitle(s->window, ptitle);
    p2cstrcpy(title, ptitle);
    return title;
}

/*
 * real_palette_set(): This does the actual palette-changing work on behalf
 * of palette_set().  Does _not_ call ActivatePalette() in case the caller
 * is doing a batch of updates.
 */
static void real_palette_set(Session *s, int n, int r, int g, int b)
{
    RGBColor col;

    if (!HAVE_COLOR_QD())
	return;
    col.red   = r * 0x0101;
    col.green = g * 0x0101;
    col.blue  = b * 0x0101;
    SetEntryColor(s->palette, n, &col);
}

/*
 * Set the logical palette.  Called by the terminal emulator.
 */
void palette_set(void *frontend, int n, int r, int g, int b)
{
    Session *s = frontend;
    
    if (!HAVE_COLOR_QD())
	return;
    real_palette_set(s, n, r, g, b);
    if (n == DEFAULT_BG)
    	mac_adjustwinbg(s);

    ActivatePalette(s->window);
}

/*
 * Reset to the default palette
 */
void palette_reset(void *frontend)
{
    Session *s = frontend;
    /* This maps colour indices in cfg to those used in our palette. */
    static const int ww[] = {
	256, 257, 258, 259, 260, 261,
	0, 8, 1, 9, 2, 10, 3, 11,
	4, 12, 5, 13, 6, 14, 7, 15
    };

    int i;

    if (!HAVE_COLOR_QD())
	return;

    for (i = 0; i < 22; i++) {
        int w = ww[i];
        real_palette_set(s,w,
			 s->cfg.colours[i][0],
			 s->cfg.colours[i][1],
			 s->cfg.colours[i][2]);
    }
    for (i = 0; i < 240; i++) {
	if (i < 216) {
	    int r = i / 36, g = (i / 6) % 6, b = i % 6;
	    real_palette_set(s,i+16,
			     r * 0x33,
			     g * 0x33,
			     b * 0x33);
	} else {
	    int shade = i - 216;
	    shade = (shade + 1) * 0xFF / (240 - 216 + 1);
	    real_palette_set(s,i+16,shade,shade,shade);
	}
    }


    mac_adjustwinbg(s);
    ActivatePalette(s->window);
    /* Palette Manager will generate update events as required. */
}

/*
 * Scroll the screen. (`lines' is +ve for scrolling forward, -ve
 * for backward.)
 */
void do_scroll(Context ctx, int topline, int botline, int lines)
{
    Session *s = ctx;
    Rect r;
    RgnHandle scrollrgn = NewRgn();
    RgnHandle movedupdate = NewRgn();
    RgnHandle update = NewRgn();
    Point g2l = { 0, 0 };

    SetPort((GrafPtr)GetWindowPort(s->window));

    /*
     * Work out the part of the update region that will scrolled by
     * this operation.
     */
    if (lines > 0)
	SetRectRgn(scrollrgn, 0, (topline + lines) * s->font_height,
		   s->term->cols * s->font_width,
		   (botline + 1) * s->font_height);
    else
	SetRectRgn(scrollrgn, 0, topline * s->font_height,
		   s->term->cols * s->font_width,
		   (botline - lines + 1) * s->font_height);
#if TARGET_API_MAC_CARBON
    GetWindowRegion(s->window, kWindowUpdateRgn, movedupdate);
#else
    GetWindowUpdateRgn(s->window, movedupdate);
#endif
    GlobalToLocal(&g2l);
    OffsetRgn(movedupdate, g2l.h, g2l.v); /* Convert to local co-ords. */
    SectRgn(scrollrgn, movedupdate, movedupdate); /* Clip scrolled section. */
#if TARGET_API_MAC_CARBON
    ValidWindowRgn(s->window, movedupdate);
#else
    ValidRgn(movedupdate);
#endif
    OffsetRgn(movedupdate, 0, -lines * s->font_height); /* Scroll it. */

    PenNormal();
    if (HAVE_COLOR_QD())
	PmBackColor(DEFAULT_BG);
    else
	BackColor(blackColor); /* XXX make configurable */
    SetRect(&r, 0, topline * s->font_height,
	    s->term->cols * s->font_width, (botline + 1) * s->font_height);
    ScrollRect(&r, 0, - lines * s->font_height, update);

#if TARGET_API_MAC_CARBON
    InvalWindowRgn(s->window, update);
    InvalWindowRgn(s->window, movedupdate);
#else
    InvalRgn(update);
    InvalRgn(movedupdate);
#endif

    DisposeRgn(scrollrgn);
    DisposeRgn(movedupdate);
    DisposeRgn(update);
}

/* Dummy routine, only required in plink. */
void ldisc_update(void *frontend, int echo, int edit)
{
}

/*
 * Mac PuTTY doesn't support printing yet.
 */
printer_job *printer_start_job(char *printer)
{

    return NULL;
}

void printer_job_data(printer_job *pj, void *data, int len)
{
}

void printer_finish_job(printer_job *pj)
{
}

void frontend_keypress(void *handle)
{
    /*
     * Keypress termination in non-Close-On-Exit mode is not
     * currently supported in PuTTY proper, because the window
     * always has a perfectly good Close button anyway. So we do
     * nothing here.
     */
    return;
}

/*
 * Ask whether to wipe a session log file before writing to it.
 * Returns 2 for wipe, 1 for append, 0 for cancel (don't log).
 */
int askappend(void *frontend, Filename filename,
	      void (*callback)(void *ctx, int result), void *ctx)
{

    /* FIXME: not implemented yet. */
    return 2;
}

int from_backend(void *frontend, int is_stderr, const char *data, int len)
{
    Session *s = frontend;

    return term_data(s->term, is_stderr, data, len);
}

/*
 * Emacs magic:
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */

