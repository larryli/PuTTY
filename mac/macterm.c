/* $Id: macterm.c,v 1.44 2003/01/12 16:11:27 ben Exp $ */
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
#include <StandardFile.h>
#include <TextCommon.h>
#include <Threads.h>
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
#include "storage.h"
#include "terminal.h"

#define NCOLOURS (lenof(((Config *)0)->colours))

#define DEFAULT_FG	16
#define DEFAULT_FG_BOLD	17
#define DEFAULT_BG	18
#define DEFAULT_BG_BOLD	19
#define CURSOR_FG	20
#define CURSOR_BG	21

#define PTOCC(x) ((x) < 0 ? -(-(x - s->font_width - 1) / s->font_width) : \
			    (x) / s->font_width)
#define PTOCR(y) ((y) < 0 ? -(-(y - s->font_height - 1) / s->font_height) : \
			    (y) / s->font_height)

static void mac_initfont(Session *);
static pascal OSStatus uni_to_font_fallback(UniChar *, ByteCount, ByteCount *,
					    TextPtr, ByteCount, ByteCount *,
					    LogicalAddress *,
					    ConstUnicodeMappingPtr);
static void mac_initpalette(Session *);
static void mac_adjustwinbg(Session *);
static void mac_adjustsize(Session *, int, int);
static void mac_drawgrowicon(Session *s);
static pascal void mac_growtermdraghook(void);
static pascal void mac_scrolltracker(ControlHandle, short);
static pascal void do_text_for_device(short, short, GDHandle, long);
static int mac_keytrans(Session *, EventRecord *, unsigned char *);
static void text_click(Session *, EventRecord *);

void pre_paint(Session *s);
void post_paint(Session *s);

#if TARGET_RT_MAC_CFM
static RoutineDescriptor mac_scrolltracker_upp =
    BUILD_ROUTINE_DESCRIPTOR(uppControlActionProcInfo,
			     (ProcPtr)mac_scrolltracker);
static RoutineDescriptor do_text_for_device_upp =
    BUILD_ROUTINE_DESCRIPTOR(uppDeviceLoopDrawingProcInfo,
			     (ProcPtr)do_text_for_device);
#else /* not TARGET_RT_MAC_CFM */
#define mac_scrolltracker_upp	mac_scrolltracker
#define do_text_for_device_upp	do_text_for_device
#endif /* not TARGET_RT_MAC_CFM */

void mac_opensession(void) {
    Session *s;
    StandardFileReply sfr;
    static const OSType sftypes[] = { 'Sess', 0, 0, 0 };
    void *sesshandle;
    int i;

    s = smalloc(sizeof(*s));
    memset(s, 0, sizeof(*s));

    StandardGetFile(NULL, 1, sftypes, &sfr);
    if (!sfr.sfGood) goto fail;

    sesshandle = open_settings_r_fsp(&sfr.sfFile);
    if (sesshandle == NULL) goto fail;
    load_open_settings(sesshandle, TRUE, &s->cfg);
    close_settings_r(sesshandle);

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
    if (s->back == NULL) {
	fatalbox("Unsupported protocol number found");
    }
    mac_startsession(s);
    return;

  fail:
    sfree(s);
    return;
}

void mac_startsession(Session *s)
{
    char *errmsg;

    /* XXX: Own storage management? */
    if (HAVE_COLOR_QD())
	s->window = GetNewCWindow(wTerminal, NULL, (WindowPtr)-1);
    else
	s->window = GetNewWindow(wTerminal, NULL, (WindowPtr)-1);
    SetWRefCon(s->window, (long)s);
    s->scrollbar = GetNewControl(cVScroll, s->window);
    s->term = term_init(&s->cfg, s);

    mac_initfont(s);
    mac_initpalette(s);
    if (HAVE_COLOR_QD()) {
	/* Set to FALSE to not get palette updates in the background. */
	SetPalette(s->window, s->palette, TRUE); 
	ActivatePalette(s->window);
    }

    s->logctx = log_init(s, &s->cfg);
    term_provide_logctx(s->term, s->logctx);

    errmsg = s->back->init(s->term, &s->backhandle, &s->cfg, s->cfg.host,
			   s->cfg.port, &s->realhost, s->cfg.tcp_nodelay);
    if (errmsg != NULL)
	fatalbox("%s", errmsg);
    s->back->provide_logctx(s->backhandle, s->logctx);

    term_provide_resize_fn(s->term, s->back->size, s->backhandle);

    mac_adjustsize(s, s->cfg.height, s->cfg.width);
    term_size(s->term, s->cfg.height, s->cfg.width, s->cfg.savelines);

    s->ldisc = ldisc_create(&s->cfg, s->term, s->back, s->backhandle, s);
    ldisc_send(s->ldisc, NULL, 0, 0);/* cause ldisc to notice changes */

    ShowWindow(s->window);
    s->next = sesslist;
    s->prev = s->next->prev;
    if (s->next != NULL)
	s->next->prev = &s->next;
    sesslist = s;
}

static UnicodeToTextFallbackUPP uni_to_font_fallback_upp;

static void mac_initfont(Session *s) {
    Str255 macfont;
    FontInfo fi;
    TextEncoding enc;
    OptionBits fbflags;

    SetPort(s->window);
    c2pstrcpy(macfont, s->cfg.font);
    GetFNum(macfont, &s->fontnum);
    TextFont(s->fontnum);
    TextFace(s->cfg.fontisbold ? bold : 0);
    TextSize(s->cfg.fontheight);
    GetFontInfo(&fi);
    s->font_width = CharWidth('W'); /* Well, it's what NCSA uses. */
    s->font_ascent = fi.ascent;
    s->font_leading = fi.leading;
    s->font_height = s->font_ascent + fi.descent + s->font_leading;
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
					kTextRegionDontCare, macfont,
					&enc) == noErr &&
	CreateUnicodeToTextInfoByEncoding(enc, &s->uni_to_font) == noErr) {
	if (uni_to_font_fallback_upp == NULL)
	    uni_to_font_fallback_upp =
		NewUnicodeToTextFallbackProc(&uni_to_font_fallback);
	fbflags = kUnicodeFallbackCustomOnly;
	if (mac_gestalts.uncvattr & kTECAddFallbackInterruptMask)
	    fbflags |= kUnicodeFallbackInterruptSafeMask;
	if (SetFallbackUnicodeToText(s->uni_to_font,
	    uni_to_font_fallback_upp, fbflags, NULL) != noErr) {
	    DisposeUnicodeToTextInfo(&s->uni_to_font);
	    goto no_encv;
	}
    } else {
      no_encv:
	s->uni_to_font = NULL;
	s->font_charset =
	    charset_from_macenc(FontToScript(s->fontnum),
				GetScriptManagerVariable(smRegionCode),
				mac_gestalts.sysvers, s->cfg.font);
    }

    mac_adjustsize(s, s->term->rows, s->term->cols);
}

static pascal OSStatus uni_to_font_fallback(UniChar *ucp,
    ByteCount ilen, ByteCount *iusedp, TextPtr obuf, ByteCount olen,
    ByteCount *ousedp, LogicalAddress *cookie, ConstUnicodeMappingPtr mapping)
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
 * Called every time round the event loop.
 */
void mac_pollterm(void)
{
    Session *s;

    for (s = sesslist; s != NULL; s = s->next) {
	term_out(s->term);
	term_update(s->term);
    }
}

/*
 * To be called whenever the window size changes.
 * rows and cols should be desired values.
 * It's assumed the terminal emulator will be informed, and will set rows
 * and cols for us.
 */
static void mac_adjustsize(Session *s, int newrows, int newcols) {
    int winwidth, winheight;

    winwidth = newcols * s->font_width + 15;
    winheight = newrows * s->font_height;
    SizeWindow(s->window, winwidth, winheight, true);
    HideControl(s->scrollbar);
    MoveControl(s->scrollbar, winwidth - 15, -1);
    SizeControl(s->scrollbar, 16, winheight - 13);
    ShowControl(s->scrollbar);
    mac_drawgrowicon(s);
}

static void mac_initpalette(Session *s) {

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
    s->palette = NewPalette(22, NULL, PM_NORMAL, PM_TOLERANCE);
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
static void mac_adjustwinbg(Session *s) {

    if (!HAVE_COLOR_QD())
	return;
#if !TARGET_CPU_68K
    if (mac_gestalts.windattr & gestaltWindowMgrPresent)
	SetWindowContentColor(s->window,
			      &(*s->palette)->pmInfo[DEFAULT_BG].ciRGB);
    else
#endif
    {
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
    }
}

/*
 * Set the cursor shape correctly
 */
void mac_adjusttermcursor(WindowPtr window, Point mouse, RgnHandle cursrgn) {
    Session *s;
    ControlHandle control;
    short part;
    int x, y;

    SetPort(window);
    s = (Session *)GetWRefCon(window);
    GlobalToLocal(&mouse);
    part = FindControl(mouse, window, &control);
    if (control == s->scrollbar) {
	SetCursor(&qd.arrow);
	RectRgn(cursrgn, &(*s->scrollbar)->contrlRect);
	SectRgn(cursrgn, window->visRgn, cursrgn);
    } else {
	x = mouse.h / s->font_width;
	y = mouse.v / s->font_height;
	if (s->raw_mouse)
	    SetCursor(&qd.arrow);
	else
	    SetCursor(*GetCursor(iBeamCursor));
	/* Ask for shape changes if we leave this character cell. */
	SetRectRgn(cursrgn, x * s->font_width, y * s->font_height,
		   (x + 1) * s->font_width, (y + 1) * s->font_height);
	SectRgn(cursrgn, window->visRgn, cursrgn);
    }
}

/*
 * Enable/disable menu items based on the active terminal window.
 */
void mac_adjusttermmenus(WindowPtr window) {
    Session *s;
    MenuHandle menu;
    long offset;

    s = (Session *)GetWRefCon(window);
    menu = GetMenuHandle(mEdit);
    EnableItem(menu, 0);
    DisableItem(menu, iUndo);
    DisableItem(menu, iCut);
    if (1/*s->term->selstate == SELECTED*/)
	EnableItem(menu, iCopy);
    else
	DisableItem(menu, iCopy);
    if (GetScrap(NULL, 'TEXT', &offset) == noTypeErr)
	DisableItem(menu, iPaste);
    else
	EnableItem(menu, iPaste);
    DisableItem(menu, iClear);
    EnableItem(menu, iSelectAll);
}

void mac_menuterm(WindowPtr window, short menu, short item) {
    Session *s;

    s = (Session *)GetWRefCon(window);
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
    }
}
	    
void mac_clickterm(WindowPtr window, EventRecord *event) {
    Session *s;
    Point mouse;
    ControlHandle control;
    int part;

    s = (Session *)GetWRefCon(window);
    SetPort(window);
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
	    TrackControl(control, mouse, &mac_scrolltracker_upp);
	    break;
	}
    } else {
	text_click(s, event);
    }
}

static void text_click(Session *s, EventRecord *event) {
    Point localwhere;
    int row, col;
    static UInt32 lastwhen = 0;
    static Session *lastsess = NULL;
    static int lastrow = -1, lastcol = -1;
    static Mouse_Action lastact = MA_NOTHING;

    SetPort(s->window);
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
    /* Fake right button with shift key */
    term_mouse(s->term, event->modifiers & shiftKey ? MBT_RIGHT : MBT_LEFT,
	       lastact, col, row, event->modifiers & shiftKey,
	       event->modifiers & controlKey, event->modifiers & optionKey);
    lastsess = s;
    lastrow = row;
    lastcol = col;
    while (StillDown()) {
	GetMouse(&localwhere);
	col = PTOCC(localwhere.h);
	row = PTOCR(localwhere.v);
	term_mouse(s->term,
		   event->modifiers & shiftKey ? MBT_RIGHT : MBT_LEFT,
		   MA_DRAG, col, row, event->modifiers & shiftKey,
		   event->modifiers & controlKey,
		   event->modifiers & optionKey);
	if (row > s->term->rows - 1)
	    term_scroll(s->term, 0, row - (s->term->rows - 1));
	else if (row < 0)
	    term_scroll(s->term, 0, row);
    }
    term_mouse(s->term, event->modifiers & shiftKey ? MBT_RIGHT : MBT_LEFT,
	       MA_RELEASE, col, row, event->modifiers & shiftKey,
	       event->modifiers & controlKey, event->modifiers & optionKey);
    lastwhen = TickCount();
}

Mouse_Button translate_button(void *frontend, Mouse_Button button)
{

    switch (button) {
      case MBT_LEFT:
	return MBT_SELECT;
      case MBT_RIGHT:
	return MBT_EXTEND;
      default:
	return 0;
    }
}

void write_clip(void *cookie, wchar_t *data, int len, int must_deselect) {
    
    /*
     * See "Programming with the Text Encoding Conversion Manager"
     * Appendix E for Unicode scrap conventions.
     *
     * XXX Need to support TEXT/styl scrap as well.
     * See STScrpRec in TextEdit (Inside Macintosh: Text) for styl details.
     * XXX Maybe PICT scrap too.
     */
    if (ZeroScrap() != noErr)
	return;
    PutScrap(len * sizeof(*data), 'utxt', data);
}

void get_clip(void *frontend, wchar_t **p, int *lenp) {
    Session *s = frontend;
    static Handle h = NULL;
    long offset;

    if (p == NULL) {
	/* release memory */
	if (h != NULL)
	    DisposeHandle(h);
	h = NULL;
    } else
	/* XXX Support TEXT-format scrap as well. */
	if (GetScrap(NULL, 'utxt', &offset) > 0) {
	    h = NewHandle(0);
	    *lenp = GetScrap(h, 'utxt', &offset) / sizeof(**p);
	    HLock(h);
	    *p = (wchar_t *)*h;
	    if (*p == NULL || *lenp <= 0)
		fatalbox("Empty scrap");
	} else {
	    *p = NULL;
	    *lenp = 0;
	}
}

static pascal void mac_scrolltracker(ControlHandle control, short part) {
    Session *s;

    s = (Session *)GetWRefCon((*control)->contrlOwner);
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
#define K_F13	0x6900
#define K_F14	0x6b00
#define K_F15	0x7100
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
    Session *s;

    s = (Session *)GetWRefCon(window);
    len = mac_keytrans(s, event, buf);
    ldisc_send(s->ldisc, (char *)buf, len, 1);
    ObscureCursor();
    term_seen_key_event(s->term);
    term_out(s->term);
    term_update(s->term);
}

static int mac_keytrans(Session *s, EventRecord *event,
			unsigned char *output) {
    unsigned char *p = output;
    int code;

    /* No meta key yet -- that'll be rather fun. */

    /* Keys that we handle locally */
    if (event->modifiers & shiftKey) {
	switch (event->message & keyCodeMask) {
	  case K_PRIOR: /* shift-pageup */
	    term_scroll(s->term, 0, -(s->term->rows - 1));
	    return 0;
	  case K_NEXT:  /* shift-pagedown */
	    term_scroll(s->term, 0, +(s->term->rows - 1));
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
    if (s->cfg.funky_type == 1 && code >= 11 && code <= 15) {
	p += sprintf((char *)p, "\x1B[[%c", code + 'A' - 11);
	return p - output;
    }
    if (s->cfg.rxvt_homeend && (code == 1 || code == 4)) {
	p += sprintf((char *)p, code == 1 ? "\x1B[H" : "\x1BOw");
	return p - output;
    }
    if (code) {
	p += sprintf((char *)p, "\x1B[%d~", code);
	return p - output;
    }

    if (s->term->app_keypad_keys) {
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
	p += sprintf((char *)p,
		     s->term->app_cursor_keys ? "\x1BOA" : "\x1B[A");
	return p - output;
      case K_DOWN:
	p += sprintf((char *)p,
		     s->term->app_cursor_keys ? "\x1BOB" : "\x1B[B");
	return p - output;
      case K_RIGHT:
	p += sprintf((char *)p,
		     s->term->app_cursor_keys ? "\x1BOC" : "\x1B[C");
	return p - output;
      case K_LEFT:
	p += sprintf((char *)p,
		     s->term->app_cursor_keys ? "\x1BOD" : "\x1B[D");
	return p - output;
      case KP_ENTER:
	*p++ = 0x0d;
	return p - output;
      case K_BS:
	*p++ = (s->cfg.bksp_is_delete ? 0x7f : 0x08);
	return p - output;
      default:
	*p++ = event->message & charCodeMask;
	return p - output;
    }
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

void mac_growterm(WindowPtr window, EventRecord *event) {
    Rect limits;
    long grow_result;
    int newrows, newcols;
    Session *s;
    DragGrayRgnUPP draghooksave;
    GrafPtr portsave;
    FontInfo fi;

    s = (Session *)GetWRefCon(window);

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

    SetRect(&limits, s->font_width + 15, s->font_height, SHRT_MAX, SHRT_MAX);
    grow_result = GrowWindow(window, event->where, &limits);

    DisposeDragGrayRgnUPP(LMGetDragHook());
    LMSetDragHook(draghooksave);
    InvalRect(&growterm_state.msgrect);

    SetPort(portsave);

    if (grow_result != 0) {
	newrows = HiWord(grow_result) / s->font_height;
	newcols = (LoWord(grow_result) - 15) / s->font_width;
	mac_adjustsize(s, newrows, newcols);
	term_size(s->term, newrows, newcols, s->cfg.savelines);
    }
}

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

void mac_activateterm(WindowPtr window, Boolean active) {
    Session *s;

    s = (Session *)GetWRefCon(window);
    s->term->has_focus = active;
    term_update(s->term);
    if (active)
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

void mac_updateterm(WindowPtr window) {
    Session *s;

    s = (Session *)GetWRefCon(window);
    SetPort(window);
    BeginUpdate(window);
    pre_paint(s);
    term_paint(s->term, s,
	       PTOCC((*window->visRgn)->rgnBBox.left),
	       PTOCR((*window->visRgn)->rgnBBox.top),
	       PTOCC((*window->visRgn)->rgnBBox.right),
	       PTOCR((*window->visRgn)->rgnBBox.bottom), 1);
    /* Restore default colours in case the Window Manager uses them */
    if (HAVE_COLOR_QD()) {
	PmForeColor(DEFAULT_FG);
	PmBackColor(DEFAULT_BG);
    } else {
	ForeColor(whiteColor);
	BackColor(blackColor);
    }
    if (FrontWindow() != window)
	EraseRect(&(*s->scrollbar)->contrlRect);
    UpdateControls(window, window->visRgn);
    mac_drawgrowicon(s);
    post_paint(s);
    EndUpdate(window);
}

static void mac_drawgrowicon(Session *s) {
    Rect clip;
    RgnHandle savergn;

    SetPort(s->window);
    /*
     * Stop DrawGrowIcon giving us space for a horizontal scrollbar
     * See Tech Note TB575 for details.
     */
    clip = s->window->portRect;
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
void do_text(Context ctx, int x, int y, char *text, int len,
	     unsigned long attr, int lattr) {
    Session *s = ctx;
    int style = 0;
    struct do_text_args a;
    RgnHandle textrgn;
    char mactextbuf[1024];
    UniChar unitextbuf[1024];
    wchar_t *unitextptr;
    int i;
    ByteCount iread, olen;
    OSStatus err;

    assert(len <= 1024);

    SetPort(s->window);
    
    /* First check this text is relevant */
    a.textrect.top = y * s->font_height;
    a.textrect.bottom = (y + 1) * s->font_height;
    a.textrect.left = x * s->font_width;
    a.textrect.right = (x + len) * s->font_width;
    if (!RectInRgn(&a.textrect, s->window->visRgn))
	return;

    /* Unpack Unicode from the mad format we get passed */
    for (i = 0; i < len; i++)
	unitextbuf[i] = (unsigned char)text[i] | (attr & CSET_MASK);

    if (s->uni_to_font != NULL) {
	err = ConvertFromUnicodeToText(s->uni_to_font, len * sizeof(UniChar),
				       unitextbuf, kUnicodeUseFallbacksMask,
				       0, NULL, NULL, NULL,
				       1024, &iread, &olen, mactextbuf);
	if (err != noErr && err != kTECUsedFallbacksStatus)
	    olen = 0;
    } else  if (s->font_charset != CS_NONE) {
	/* XXX this is bogus if wchar_t and UniChar are different sizes. */
	unitextptr = (wchar_t *)unitextbuf;
	olen = charset_from_unicode(&unitextptr, &len, mactextbuf, 1024,
				    s->font_charset, NULL, ".", 1);
    } else
	olen = 0;

    a.s = s;
    a.text = mactextbuf;
    a.len = olen;
    a.attr = attr;
    a.lattr = lattr;
    a.numer.h = a.numer.v = a.denom.h = a.denom.v = 1;
    SetPort(s->window);
    TextFont(s->fontnum);
    if (s->cfg.fontisbold || (attr & ATTR_BOLD) && !s->cfg.bold_colour)
    	style |= bold;
    if (attr & ATTR_UNDER)
	style |= underline;
    TextFace(style);
    TextSize(s->cfg.fontheight);
    TextMode(srcOr);
    if (HAVE_COLOR_QD())
	if (style & bold) {
	    SpaceExtra(s->font_boldadjust << 16);
	    CharExtra(s->font_boldadjust << 16);
	} else {
	    SpaceExtra(0);
	    CharExtra(0);
	}
    textrgn = NewRgn();
    RectRgn(textrgn, &a.textrect);
    if (HAVE_COLOR_QD())
	DeviceLoop(textrgn, &do_text_for_device_upp, (long)&a, 0);
    else
	do_text_for_device(1, 0, NULL, (long)&a);
    DisposeRgn(textrgn);
    /* Tell the window manager about it in case this isn't an update */
    ValidRect(&a.textrect);
}

static pascal void do_text_for_device(short depth, short devflags,
				      GDHandle device, long cookie) {
    struct do_text_args *a;
    int bgcolour, fgcolour, bright, reverse, tmp;

    a = (struct do_text_args *)cookie;

    bright = (a->attr & ATTR_BOLD) && a->s->cfg.bold_colour;
    reverse = a->attr & ATTR_REVERSE;

    if (depth == 1 && (a->attr & TATTR_ACTCURS))
	reverse = !reverse;

    if (HAVE_COLOR_QD()) {
	if (depth > 2) {
	    fgcolour = ((a->attr & ATTR_FGMASK) >> ATTR_FGSHIFT) * 2;
	    bgcolour = ((a->attr & ATTR_BGMASK) >> ATTR_BGSHIFT) * 2;
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
	    fgcolour++;
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

    EraseRect(&a->textrect);
    MoveTo(a->textrect.left, a->textrect.top + a->s->font_ascent);
    /* FIXME: Sort out bold width adjustments on Original QuickDraw. */
    if (a->s->window->grafProcs != NULL)
	InvokeQDTextUPP(a->len, a->text, a->numer, a->denom,
			a->s->window->grafProcs->textProc);
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

void do_cursor(Context ctx, int x, int y, char *text, int len,
	     unsigned long attr, int lattr)
{

    do_text(ctx, x, y, text, len, attr, lattr);
}

/*
 * Call from the terminal emulator to get its graphics context.
 * Should probably be called start_redraw or something.
 */
void pre_paint(Session *s) {
    GDHandle gdh;
    Rect myrect, tmprect;

    if (HAVE_COLOR_QD()) {
	s->term->attr_mask = 0;
	SetPort(s->window);
	myrect = (*s->window->visRgn)->rgnBBox;
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

Context get_ctx(void *frontend) {
    Session *s = frontend;

    pre_paint(s);
    return s;
}

void free_ctx(Context ctx) {

}

/*
 * Presumably this does something in Windows
 */
void post_paint(Session *s) {

}

/*
 * Set the scroll bar position
 *
 * total is the line number of the bottom of the working screen
 * start is the line number of the top of the display
 * page is the length of the displayed page
 */
void set_sbar(void *frontend, int total, int start, int page) {
    Session *s = frontend;

    /* We don't redraw until we've set everything up, to avoid glitches */
    (*s->scrollbar)->contrlMin = 0;
    (*s->scrollbar)->contrlMax = total - page;
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
void set_title(void *frontend, char *title) {
    Session *s = frontend;
    Str255 mactitle;

    c2pstrcpy(mactitle, title);
    SetWTitle(s->window, mactitle);
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
void request_resize(void *frontend, int w, int h) {
    Session *s = frontend;

    s->term->cols = w;
    s->term->rows = h;
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

    *x = s->window->portRect.left;
    *y = s->window->portRect.top;
}

/*
 * Report the window's pixel size, for terminal reports.
 */
void get_window_pixels(void *frontend, int *x, int *y)
{
    Session *s = frontend;

    *x = s->window->portRect.right - s->window->portRect.left;
    *y = s->window->portRect.bottom - s->window->portRect.top;
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
void palette_set(void *frontend, int n, int r, int g, int b) {
    Session *s = frontend;
    static const int first[21] = {
	0, 2, 4, 6, 8, 10, 12, 14,
	1, 3, 5, 7, 9, 11, 13, 15,
	16, 17, 18, 20, 21
    };
    
    if (!HAVE_COLOR_QD())
	return;
    real_palette_set(s, first[n], r, g, b);
    if (first[n] == 18)
	real_palette_set(s, first[n]+1, r, g, b);
    if (first[n] == DEFAULT_BG)
	mac_adjustwinbg(s);
    ActivatePalette(s->window);
}

/*
 * Reset to the default palette
 */
void palette_reset(void *frontend) {
    Session *s = frontend;
    /* This maps colour indices in cfg to those used in our palette. */
    static const int ww[] = {
	6, 7, 8, 9, 10, 11, 12, 13,
        14, 15, 16, 17, 18, 19, 20, 21,
	0, 1, 2, 3, 4, 5
    };
    int i;

    if (!HAVE_COLOR_QD())
	return;

    assert(lenof(ww) == NCOLOURS);

    for (i = 0; i < NCOLOURS; i++) {
	real_palette_set(s, i,
			 s->cfg.colours[ww[i]][0],
			 s->cfg.colours[ww[i]][1],
			 s->cfg.colours[ww[i]][2]);
    }
    mac_adjustwinbg(s);
    ActivatePalette(s->window);
    /* Palette Manager will generate update events as required. */
}

/*
 * Scroll the screen. (`lines' is +ve for scrolling forward, -ve
 * for backward.)
 */
void do_scroll(void *frontend, int topline, int botline, int lines) {
    Session *s = frontend;
    Rect r;
    RgnHandle scrollrgn = NewRgn();
    RgnHandle movedupdate = NewRgn();
    RgnHandle update = NewRgn();
    Point g2l = { 0, 0 };

    SetPort(s->window);

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
    CopyRgn(((WindowPeek)s->window)->updateRgn, movedupdate);
    GlobalToLocal(&g2l);
    OffsetRgn(movedupdate, g2l.h, g2l.v); /* Convert to local co-ords. */
    SectRgn(scrollrgn, movedupdate, movedupdate); /* Clip scrolled section. */
    ValidRgn(movedupdate);
    OffsetRgn(movedupdate, 0, -lines * s->font_height); /* Scroll it. */

    PenNormal();
    if (HAVE_COLOR_QD())
	PmBackColor(DEFAULT_BG);
    else
	BackColor(blackColor); /* XXX make configurable */
    SetRect(&r, 0, topline * s->font_height,
	    s->term->cols * s->font_width, (botline + 1) * s->font_height);
    ScrollRect(&r, 0, - lines * s->font_height, update);

    InvalRgn(update);
    InvalRgn(movedupdate);

    DisposeRgn(scrollrgn);
    DisposeRgn(movedupdate);
    DisposeRgn(update);
}

void logevent(void *frontend, char *str) {

    fprintf(stderr, "%s\n", str);
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
int askappend(void *frontend, char *filename)
{

    /* FIXME: not implemented yet. */
    return 2;
}

/*
 * Emacs magic:
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */

