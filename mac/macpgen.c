/* $Id: macpgen.c,v 1.2 2003/02/15 14:20:43 ben Exp $ */
/*
 * Copyright (c) 1999, 2003 Ben Harris
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
 * macpgen.c - PuTTYgen for Mac OS
 */

#include <MacTypes.h>
#include <AEDataModel.h>
#include <AppleEvents.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <MacWindows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Appearance.h>
#include <CodeFragments.h>
#include <Dialogs.h>
#include <Devices.h>
#include <DiskInit.h>
#include <Gestalt.h>
#include <LowMem.h>
#include <Navigation.h>
#include <Resources.h>
#include <Script.h>
#include <TextCommon.h>
#include <ToolUtils.h>
#include <UnicodeConverter.h>

#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>		/* putty.h needs size_t */
#include <stdio.h>		/* for vsprintf */

#define PUTTY_DO_GLOBALS

#include "macpgrid.h"
#include "putty.h"
#include "ssh.h"
#include "mac.h"

static void mac_startup(void);
static void mac_eventloop(void);
#pragma noreturn (mac_eventloop)
static void mac_event(EventRecord *);
static void mac_contentclick(WindowPtr, EventRecord *);
static void mac_growwindow(WindowPtr, EventRecord *);
static void mac_activatewindow(WindowPtr, EventRecord *);
static void mac_activateabout(WindowPtr, EventRecord *);
static void mac_updatewindow(WindowPtr);
static void mac_updatelicence(WindowPtr);
static void mac_keypress(EventRecord *);
static int mac_windowtype(WindowPtr);
static void mac_menucommand(long);
static void mac_openabout(void);
static void mac_openlicence(void);
static void mac_adjustcursor(RgnHandle);
static void mac_adjustmenus(void);
static void mac_closewindow(WindowPtr);
static void mac_zoomwindow(WindowPtr, short);
#pragma noreturn (cleanup_exit)

struct mac_windows {
    WindowPtr about;
    WindowPtr licence;
};

struct mac_windows windows;
int borednow;
struct mac_gestalts mac_gestalts;

int main (int argc, char **argv) {

    mac_startup();
    mac_eventloop();
}

#pragma noreturn (main)

static void mac_startup(void) {
    Handle menuBar;
    TECInfoHandle ti;

#if !TARGET_API_MAC_CARBON
    /* Init Memory Manager */
    MaxApplZone();
    /* Init QuickDraw */
    InitGraf(&qd.thePort);
    /* Init Font Manager */
    InitFonts();
    /* Init Window Manager */
    InitWindows();
    /* Init Menu Manager */
    InitMenus();
    /* Init TextEdit */
    TEInit();
    /* Init Dialog Manager */
    InitDialogs(NULL);
#endif
    
    /* Get base system version (only used if there's no better selector) */
    if (Gestalt(gestaltSystemVersion, &mac_gestalts.sysvers) != noErr ||
	(mac_gestalts.sysvers &= 0xffff) < 0x700)
	fatalbox("PuTTYgen requires System 7 or newer");
    /* Find out if we've got Color Quickdraw */
    if (Gestalt(gestaltQuickdrawVersion, &mac_gestalts.qdvers) != noErr)
    	mac_gestalts.qdvers = gestaltOriginalQD;
    /* ... and the Appearance Manager? */
    if (Gestalt(gestaltAppearanceVersion, &mac_gestalts.apprvers) != noErr)
	if (Gestalt(gestaltAppearanceAttr, NULL) == noErr)
	    mac_gestalts.apprvers = 0x0100;
	else
	    mac_gestalts.apprvers = 0;
#if TARGET_RT_MAC_CFM
    /* Paranoia: Did we manage to pull in AppearanceLib? */
    if (&RegisterAppearanceClient == kUnresolvedCFragSymbolAddress)
	mac_gestalts.apprvers = 0;
#endif
#if TARGET_CPU_68K
    mac_gestalts.cntlattr = 0;
    mac_gestalts.windattr = 0;
#else
    /* Mac OS 8.5 Control Manager (proportional scrollbars)? */
    if (Gestalt(gestaltControlMgrAttr, &mac_gestalts.cntlattr) != noErr ||
	&SetControlViewSize == kUnresolvedCFragSymbolAddress)
	mac_gestalts.cntlattr = 0;
    /* Mac OS 8.5 Window Manager? */
    if (Gestalt(gestaltWindowMgrAttr, &mac_gestalts.windattr) != noErr ||
	&SetWindowContentColor == kUnresolvedCFragSymbolAddress)
	mac_gestalts.windattr = 0;
#endif
    /* Text Encoding Conversion Manager? */
    if (
#if TARGET_RT_MAC_CFM
	&TECGetInfo == kUnresolvedCFragSymbolAddress ||
#else
	InitializeUnicodeConverter(NULL) != noErr ||
#endif
	TECGetInfo(&ti) != noErr)
	mac_gestalts.encvvers = 0;
    else {
	mac_gestalts.encvvers = (*ti)->tecVersion;
	mac_gestalts.uncvattr = (*ti)->tecUnicodeConverterFeatures;
	DisposeHandle((Handle)ti);
    }
    /* Navigation Services? */
    if (NavServicesAvailable())
	mac_gestalts.navsvers = NavLibraryVersion();
    else
	mac_gestalts.navsvers = 0;

    /* We've been tested with the Appearance Manager */
    if (mac_gestalts.apprvers != 0)
	RegisterAppearanceClient();

    menuBar = GetNewMBar(128);
    if (menuBar == NULL)
	fatalbox("Unable to create menu bar.");
    SetMenuBar(menuBar);
    AppendResMenu(GetMenuHandle(mApple), 'DRVR');
    mac_adjustmenus();
    DrawMenuBar();
    InitCursor();
    windows.about = NULL;
    windows.licence = NULL;

    flags = FLAG_INTERACTIVE;

    /* Install Apple Event handlers. */
#if 0
    AEInstallEventHandler(kCoreEventClass, kAEOpenApplication,
			  NewAEEventHandlerUPP(&mac_aevt_oapp), 0, FALSE);
    AEInstallEventHandler(kCoreEventClass, kAEOpenDocuments,
			  NewAEEventHandlerUPP(&mac_aevt_odoc), 0, FALSE);
    AEInstallEventHandler(kCoreEventClass, kAEPrintDocuments,
			  NewAEEventHandlerUPP(&mac_aevt_pdoc), 0, FALSE);
#endif
    AEInstallEventHandler(kCoreEventClass, kAEQuitApplication,
			  NewAEEventHandlerUPP(&mac_aevt_quit), 0, FALSE);
}

static void mac_eventloop(void) {
    Boolean gotevent;
    EventRecord event;
    RgnHandle cursrgn;

    cursrgn = NewRgn();
    for (;;) {
    	mac_adjustcursor(cursrgn);
	gotevent = WaitNextEvent(everyEvent, &event, LONG_MAX, cursrgn);
	mac_adjustcursor(cursrgn);
	if (gotevent)
	    mac_event(&event);
	if (borednow)
	    cleanup_exit(0);
    }
    DisposeRgn(cursrgn);
}

static void mac_event(EventRecord *event) {
    short part;
    WindowPtr window;

    switch (event->what) {
      case mouseDown:
	part = FindWindow(event->where, &window);
	switch (part) {
	  case inMenuBar:
	    mac_adjustmenus();
	    mac_menucommand(MenuSelect(event->where));
	    break;
#if !TARGET_API_MAC_CARBON
	  case inSysWindow:
	    SystemClick(event, window);
	    break;
#endif
	  case inContent:
	    if (window != FrontWindow())
	    	/* XXX: check for movable modal dboxes? */
		SelectWindow(window);
	    else
		mac_contentclick(window, event);
	    break;
	  case inGoAway:
	    if (TrackGoAway(window, event->where))
		mac_closewindow(window);
	    break;
	  case inDrag:
	    /* XXX: moveable modal check? */
#if TARGET_API_MAC_CARBON
	    {
		BitMap screenBits;

		GetQDGlobalsScreenBits(&screenBits);
		DragWindow(window, event->where, &screenBits.bounds);
	    }
#else
	    DragWindow(window, event->where, &qd.screenBits.bounds);
#endif
	    break;
	  case inGrow:
	    mac_growwindow(window, event);
	    break;
	  case inZoomIn:
	  case inZoomOut:
	    if (TrackBox(window, event->where, part))
		mac_zoomwindow(window, part);
	    break;
	}
	break;
      case keyDown:
      case autoKey:
        mac_keypress(event);
        break;
      case activateEvt:
	mac_activatewindow((WindowPtr)event->message, event);
        break;
      case updateEvt:
        mac_updatewindow((WindowPtr)event->message);
        break;
#if !TARGET_API_MAC_CARBON
      case diskEvt:
	if (HiWord(event->message) != noErr) {
	    Point pt;

	    SetPt(&pt, 120, 120);
	    DIBadMount(pt, event->message);
        }
        break;
#endif
      case kHighLevelEvent:
	AEProcessAppleEvent(event); /* errors? */
	break;
    }
}

static void mac_contentclick(WindowPtr window, EventRecord *event) {
    short item;
    DialogRef dialog;

    switch (mac_windowtype(window)) {
      case wAbout:
	dialog = GetDialogFromWindow(window);
	if (DialogSelect(event, &dialog, &item))
	    switch (item) {
	      case wiAboutLicence:
		mac_openlicence();
		break;
	    }
	break;
    }
}

static void mac_growwindow(WindowPtr window, EventRecord *event) {

    switch (mac_windowtype(window)) {
    }
}

static void mac_activatewindow(WindowPtr window, EventRecord *event) {
    int active;

    active = (event->modifiers & activeFlag) != 0;
    mac_adjustmenus();
    switch (mac_windowtype(window)) {
      case wAbout:
	mac_activateabout(window, event);
	break;
    }
}

static void mac_activateabout(WindowPtr window, EventRecord *event) {
    DialogRef dialog;
    DialogItemType itemtype;
    Handle itemhandle;
    short item;
    Rect itemrect;
    int active;

    dialog = GetDialogFromWindow(window);
    active = (event->modifiers & activeFlag) != 0;
    GetDialogItem(dialog, wiAboutLicence, &itemtype, &itemhandle, &itemrect);
    HiliteControl((ControlHandle)itemhandle, active ? 0 : 255);
    DialogSelect(event, &dialog, &item);
}

static void mac_updatewindow(WindowPtr window)
{
#if TARGET_API_MAC_CARBON
    RgnHandle rgn;
#endif

    switch (mac_windowtype(window)) {
      case wAbout:
	BeginUpdate(window);
#if TARGET_API_MAC_CARBON
	rgn = NewRgn();
	GetPortVisibleRegion(GetWindowPort(window), rgn);
	UpdateDialog(GetDialogFromWindow(window), rgn);
	DisposeRgn(rgn);
#else
	UpdateDialog(window, window->visRgn);
#endif
	EndUpdate(window);
	break;
      case wLicence:
	mac_updatelicence(window);
	break;
    }
}

static void mac_updatelicence(WindowPtr window)
{
    Handle h;
    int len;
    long fondsize;
    Rect textrect;

    SetPort((GrafPtr)GetWindowPort(window));
    BeginUpdate(window);
    fondsize = GetScriptVariable(smRoman, smScriptSmallFondSize);
    TextFont(HiWord(fondsize));
    TextSize(LoWord(fondsize));
    h = Get1Resource('TEXT', wLicence);
    len = GetResourceSizeOnDisk(h);
#if TARGET_API_MAC_CARBON
    GetPortBounds(GetWindowPort(window), &textrect);
#else
    textrect = window->portRect;
#endif
    if (h != NULL) {
	HLock(h);
	TETextBox(*h, len, &textrect, teFlushDefault);
	HUnlock(h);
    }
    EndUpdate(window);
}

/*
 * Work out what kind of window we're dealing with.
 */
static int mac_windowtype(WindowPtr window)
{

#if !TARGET_API_MAC_CARBON
    if (GetWindowKind(window) < 0)
	return wDA;
#endif
    return ((WinInfo *)GetWRefCon(window))->wtype;
}

/*
 * Handle a key press
 */
static void mac_keypress(EventRecord *event) {
    WindowPtr window;

    window = FrontWindow();
    if (event->what == keyDown && (event->modifiers & cmdKey)) {
	mac_adjustmenus();
	mac_menucommand(MenuKey(event->message & charCodeMask));
    } else {
	switch (mac_windowtype(window)) {
	}
    }       
}

static void mac_menucommand(long result) {
    short menu, item;
    WindowPtr window;
#if !TARGET_API_MAC_CARBON
    Str255 da;
#endif

    menu = HiWord(result);
    item = LoWord(result);
    window = FrontWindow();
    /* Things which do the same whatever window we're in. */
    switch (menu) {
      case mApple:
        switch (item) {
          case iAbout:
	    mac_openabout();
            goto done;
#if !TARGET_API_MAC_CARBON
          default:
            GetMenuItemText(GetMenuHandle(mApple), item, da);
            OpenDeskAcc(da);
            goto done;
#endif
        }
        break;
      case mFile:
        switch (item) {
	  case iNew:
	    mac_newkey();
	    goto done;
          case iClose:
            mac_closewindow(window);
            goto done;
          case iQuit:
            cleanup_exit(0);
            goto done;
        }
        break;
    }
    /* If we get here, handling is up to window-specific code. */
    switch (mac_windowtype(window)) {
    }
  done:
    HiliteMenu(0);
}

static void mac_openabout(void) {
    DialogItemType itemtype;
    Handle item;
    VersRecHndl vers;
    Rect box;
    StringPtr longvers;
    WinInfo *wi;

    if (windows.about)
	SelectWindow(windows.about);
    else {
	windows.about =
	    GetDialogWindow(GetNewDialog(wAbout, NULL, (WindowPtr)-1));
	wi = smalloc(sizeof(*wi));
	wi->s = NULL;
	wi->wtype = wAbout;
	SetWRefCon(windows.about, (long)wi);
	vers = (VersRecHndl)Get1Resource('vers', 1);
	if (vers != NULL && *vers != NULL) {
	    longvers = (*vers)->shortVersion + (*vers)->shortVersion[0] + 1;
	    GetDialogItem(GetDialogFromWindow(windows.about), wiAboutVersion,
			  &itemtype, &item, &box);
	    assert(itemtype & kStaticTextDialogItem);
	    SetDialogItemText(item, longvers);
	}
	ShowWindow(windows.about);
    }
}

static void mac_openlicence(void) {
    WinInfo *wi;

    if (windows.licence)
	SelectWindow(windows.licence);
    else {
	windows.licence = GetNewWindow(wLicence, NULL, (WindowPtr)-1);
	wi = smalloc(sizeof(*wi));
	wi->s = NULL;
	wi->wtype = wLicence;
	SetWRefCon(windows.licence, (long)wi);
	ShowWindow(windows.licence);
    }
}

static void mac_closewindow(WindowPtr window) {

    switch (mac_windowtype(window)) {
#if !TARGET_API_MAC_CARBON
      case wDA:
	CloseDeskAcc(GetWindowKind(window));
	break;
#endif
      case wAbout:
	windows.about = NULL;
	DisposeDialog(GetDialogFromWindow(window));
	break;
      case wLicence:
	windows.licence = NULL;
	DisposeWindow(window);
	break;
    }
}

static void mac_zoomwindow(WindowPtr window, short part) {

    /* FIXME: do something */
}

/*
 * Make the menus look right before the user gets to see them.
 */
#if TARGET_API_MAC_CARBON
#define EnableItem EnableMenuItem
#define DisableItem DisableMenuItem
#endif
static void mac_adjustmenus(void) {
    WindowPtr window;
    MenuHandle menu;

    window = FrontWindow();
    menu = GetMenuHandle(mApple);
    EnableItem(menu, 0);
    EnableItem(menu, iAbout);

    menu = GetMenuHandle(mFile);
    EnableItem(menu, 0);
    EnableItem(menu, iNew);
    if (window != NULL)
	EnableItem(menu, iClose);
    else
	DisableItem(menu, iClose);
    EnableItem(menu, iQuit);

    switch (mac_windowtype(window)) {
      default:
	DisableItem(menu, iSave);
	DisableItem(menu, iSaveAs);
	menu = GetMenuHandle(mEdit);
	DisableItem(menu, 0);
	menu = GetMenuHandle(mWindow);
	DisableItem(menu, 0); /* Until we get more than 1 item on it. */
	break;
    }
    DrawMenuBar();
}

/*
 * Make sure the right cursor's being displayed.
 */
static void mac_adjustcursor(RgnHandle cursrgn) {
    Point mouse;
    WindowPtr window, front;
    short part;
#if TARGET_API_MAC_CARBON
    Cursor arrow;
    RgnHandle visrgn;
#endif

    GetMouse(&mouse);
    LocalToGlobal(&mouse);
    part = FindWindow(mouse, &window);
    front = FrontWindow();
    if (part != inContent || window == NULL || window != front) {
	/* Cursor isn't in the front window, so switch to arrow */
#if TARGET_API_MAC_CARBON
	GetQDGlobalsArrow(&arrow);
	SetCursor(&arrow);
#else
	SetCursor(&qd.arrow);
#endif
	SetRectRgn(cursrgn, SHRT_MIN, SHRT_MIN, SHRT_MAX, SHRT_MAX);
	if (front != NULL) {
#if TARGET_API_MAC_CARBON
	    visrgn = NewRgn();
	    GetPortVisibleRegion(GetWindowPort(front), visrgn);
	    DiffRgn(cursrgn, visrgn, cursrgn);
	    DisposeRgn(visrgn);
#else
	    DiffRgn(cursrgn, front->visRgn, cursrgn);
#endif
	}
    } else {
	switch (mac_windowtype(window)) {
	  default:
#if TARGET_API_MAC_CARBON
	    GetQDGlobalsArrow(&arrow);
	    SetCursor(&arrow);
	    GetPortVisibleRegion(GetWindowPort(window), cursrgn);
#else
	    SetCursor(&qd.arrow);
	    CopyRgn(window->visRgn, cursrgn);
#endif
	    break;
	}
    }
}

pascal OSErr mac_aevt_quit(const AppleEvent *req, AppleEvent *reply,
				  long refcon)
{
    DescType type;
    Size size;

    if (AEGetAttributePtr(req, keyMissedKeywordAttr, typeWildCard,
			  &type, NULL, 0, &size) == noErr)
	return errAEParamMissed;

    borednow = 1;
    return noErr;
}

void cleanup_exit(int status)
{

#if !TARGET_RT_MAC_CFM
    if (mac_gestalts.encvvers != 0)
	TerminateUnicodeConverter();
#endif
    exit(status);
}

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */
