/* $Id$ */
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
 * mac.c -- miscellaneous Mac-specific routines
 */

#include <MacTypes.h>
#include <AEDataModel.h>
#include <AppleEvents.h>
#include <Controls.h>
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

#include "macresid.h"
#include "putty.h"
#include "ssh.h"
#include "terminal.h"
#include "mac.h"

Session *sesslist;

static int cold = 1;
static int borednow = FALSE;
struct mac_gestalts mac_gestalts;
UInt32 sleeptime;
static long timing_next_time;

static void mac_startup(void);
static void mac_eventloop(void);
#pragma noreturn (mac_eventloop)
static void mac_event(EventRecord *);
static void mac_contentclick(WindowPtr, EventRecord *);
static void mac_growwindow(WindowPtr, EventRecord *);
static void mac_activatewindow(WindowPtr, EventRecord *);
static void mac_suspendresume(EventRecord *);
static void mac_activateabout(WindowPtr, EventRecord *);
static void mac_updatewindow(WindowPtr);
static void mac_updatelicence(WindowPtr);
static void mac_keypress(EventRecord *);
static int mac_windowtype(WindowPtr);
static void mac_menucommand(long);
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
    cold = 0;
    
    /* Get base system version (only used if there's no better selector) */
    if (Gestalt(gestaltSystemVersion, &mac_gestalts.sysvers) != noErr ||
	(mac_gestalts.sysvers &= 0xffff) < 0x700)
	fatalbox("PuTTY requires System 7 or newer");
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
    /* Mac OS 8.5 Menu Manager? */
    if (Gestalt(gestaltMenuMgrAttr, &mac_gestalts.menuattr) != noErr)
	mac_gestalts.menuattr = 0;
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

    sk_init();

    /* We've been tested with the Appearance Manager */
    if (mac_gestalts.apprvers != 0)
	RegisterAppearanceClient();

    menuBar = GetNewMBar(128);
    if (menuBar == NULL)
	fatalbox("Unable to create menu bar.");
    SetMenuBar(menuBar);
    AppendResMenu(GetMenuHandle(mApple), 'DRVR');
    if (mac_gestalts.menuattr & gestaltMenuMgrAquaLayoutMask) {
	DeleteMenuItem(GetMenuHandle(mFile), iQuit);
	/* Also delete the separator above the Quit item. */
	DeleteMenuItem(GetMenuHandle(mFile), iQuit - 1);
    }
    mac_adjustmenus();
    DrawMenuBar();
    InitCursor();
    windows.about = NULL;
    windows.licence = NULL;

    default_protocol = be_default_protocol;
    /* Find the appropriate default port. */
    {
	int i;
	default_port = 0; /* illegal */
	for (i = 0; backends[i].backend != NULL; i++)
	    if (backends[i].protocol == default_protocol) {
		default_port = backends[i].backend->default_port;
		break;
	    }
    }
    flags = FLAG_INTERACTIVE;

#if !TARGET_API_MAC_CARBON
    {
	short vol;
	long dirid;

	/* Set the default directory for loading and saving settings. */
	/* XXX Should we create it? */
	if (get_session_dir(FALSE, &vol, &dirid) == noErr) {
	    LMSetSFSaveDisk(-vol);
	    LMSetCurDirStore(dirid);
	}
    }
#endif

    /* Install Apple Event handlers. */
    AEInstallEventHandler(kCoreEventClass, kAEOpenApplication,
			  NewAEEventHandlerUPP(&mac_aevt_oapp), 0, FALSE);
    AEInstallEventHandler(kCoreEventClass, kAEOpenDocuments,
			  NewAEEventHandlerUPP(&mac_aevt_odoc), 0, FALSE);
    AEInstallEventHandler(kCoreEventClass, kAEPrintDocuments,
			  NewAEEventHandlerUPP(&mac_aevt_pdoc), 0, FALSE);
    AEInstallEventHandler(kCoreEventClass, kAEQuitApplication,
			  NewAEEventHandlerUPP(&mac_aevt_quit), 0, FALSE);
}

void timer_change_notify(long next)
{
    timing_next_time = next;
}

static void mac_eventloop(void) {
    Boolean gotevent;
    EventRecord event;
    RgnHandle cursrgn;
    long next;
    long ticksleft;

    cursrgn = NewRgn();
    sleeptime = 0;
    for (;;) {
    	mac_adjustcursor(cursrgn);
	ticksleft=timing_next_time-GETTICKCOUNT();
	if (sleeptime > ticksleft && ticksleft >=0)
	    sleeptime=ticksleft;
	gotevent = WaitNextEvent(everyEvent, &event, sleeptime, cursrgn);
	if (timing_next_time <= GETTICKCOUNT()) {
            if (run_timers(timing_next_time, &next)) {
                timer_change_notify(next);
            }
        }

	/*
	 * XXX For now, limit sleep time to 1/10 s to work around
	 * wake-before-sleep race in MacTCP code.
	 */
	sleeptime = 6;
	mac_adjustcursor(cursrgn);
	if (gotevent) {
	    /* Ensure we get a null event when the real ones run out. */
	    sleeptime = 0;
	    mac_event(&event);
	    if (borednow)
		cleanup_exit(0);
	}
	if (!gotevent)
	    sk_poll();
	    mac_pollterm();
	if (mac_gestalts.apprvers >= 0x100 && mac_frontwindow() != NULL)
	    IdleControls(mac_frontwindow());
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
      case osEvt:
	switch ((event->message & osEvtMessageMask) >> 24) {
	  case suspendResumeMessage:
	    mac_suspendresume(event);
	    break;
	}
	break;
      case kHighLevelEvent:
	AEProcessAppleEvent(event); /* errors? */
	break;
    }
}

static void mac_contentclick(WindowPtr window, EventRecord *event)
{

    if (mac_wininfo(window)->click != NULL)
	(*mac_wininfo(window)->click)(window, event);
}

static void mac_growwindow(WindowPtr window, EventRecord *event)
{

    if (mac_wininfo(window)->grow != NULL)
	(*mac_wininfo(window)->grow)(window, event);
}

static void mac_activatewindow(WindowPtr window, EventRecord *event)
{

    mac_adjustmenus();
    if (mac_wininfo(window)->activate != NULL)
	(*mac_wininfo(window)->activate)(window, event);
}

static void mac_updatewindow(WindowPtr window)
{

    if (mac_wininfo(window)->update != NULL)
	(*mac_wininfo(window)->update)(window);
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

    window = mac_frontwindow();
    /*
     * Check for a command-key combination, but ignore it if it counts
     * as a meta-key combination and we're in a terminal window.
     */
    if (event->what == keyDown && (event->modifiers & cmdKey) /*&&
	!((event->modifiers & cfg.meta_modifiers) == cfg.meta_modifiers &&
	    mac_windowtype(window) == wTerminal)*/) {
	mac_adjustmenus();
	mac_menucommand(MenuKey(event->message & charCodeMask));
    } else {
	if (window != NULL && mac_wininfo(window)->key != NULL)
	    (*mac_wininfo(window)->key)(window, event);
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
    window = mac_frontwindow();
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
            mac_newsession();
            goto done;
	  case iOpen:
	    mac_opensession();
	    goto done;
          case iClose:
            mac_closewindow(window);
            goto done;
	  case iSave:
	    mac_savesession();
	    goto done;
	  case iSaveAs:
	    mac_savesessionas();
	    goto done;
	  case iDuplicate:
	    mac_dupsession();
	    goto done;
          case iQuit:
            cleanup_exit(0);
            goto done;
        }
        break;
    }
    /* If we get here, handling is up to window-specific code. */
    if (window != NULL && mac_wininfo(window)->menu != NULL)
	(*mac_wininfo(window)->menu)(window, menu, item);

  done:
    HiliteMenu(0);
}

static void mac_closewindow(WindowPtr window) {

    switch (mac_windowtype(window)) {
#if !TARGET_API_MAC_CARBON
      case wDA:
	CloseDeskAcc(GetWindowKind(window));
	break;
#endif
      default:
	if (mac_wininfo(window)->close != NULL)
	    (*mac_wininfo(window)->close)(window);
	break;
    }
}

static void mac_suspendresume(EventRecord *event)
{
    WindowPtr front;
    EventRecord fakeevent;

    /*
     * We're called either before we're suspended or after we're
     * resumed, so we're the front application at this point.
     */
    front = FrontWindow();
    if (front != NULL) {
	fakeevent.what = activateEvt;
	fakeevent.message = (UInt32)front;
	fakeevent.when = event->when;
	fakeevent.where = event->where;
	fakeevent.modifiers =
	    (event->message & resumeFlag) ? activeFlag : 0;
	mac_activatewindow(front, &fakeevent);
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

    window = mac_frontwindow();
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

    if (window != NULL && mac_wininfo(window)->adjustmenus != NULL)
	(*mac_wininfo(window)->adjustmenus)(window);
    else {
	DisableItem(menu, iSave);
	DisableItem(menu, iSaveAs);
	DisableItem(menu, iDuplicate);
	menu = GetMenuHandle(mEdit);
	DisableItem(menu, 0);
	menu = GetMenuHandle(mWindow);
	DisableItem(menu, 0); /* Until we get more than 1 item on it. */
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
	if (mac_wininfo(window)->adjustcursor != NULL)
	    (*mac_wininfo(window)->adjustcursor)(window, mouse, cursrgn);
	else {
#if TARGET_API_MAC_CARBON
	    GetQDGlobalsArrow(&arrow);
	    SetCursor(&arrow);
	    GetPortVisibleRegion(GetWindowPort(window), cursrgn);
#else
	    SetCursor(&qd.arrow);
	    CopyRgn(window->visRgn, cursrgn);
#endif
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
    sk_cleanup();
    exit(status);
}

/* This should only kill the current session, not the whole application. */
void connection_fatal(void *fontend, char *fmt, ...) {
    va_list ap;
    Str255 stuff;
    
    va_start(ap, fmt);
    /* We'd like stuff to be a Pascal string */
    stuff[0] = vsprintf((char *)(&stuff[1]), fmt, ap);
    va_end(ap);
    ParamText(stuff, NULL, NULL, NULL);
    StopAlert(128, NULL);
    cleanup_exit(1);
}

/* Null SSH agent client -- never finds an agent. */

int agent_exists(void)
{

    return FALSE;
}

int agent_query(void *in, int inlen, void **out, int *outlen,
		void (*callback)(void *, void *, int), void *callback_ctx)
{

    *out = NULL;
    *outlen = 0;
    return 1;
}

/* Temporary null routines for testing. */

void verify_ssh_host_key(void *frontend, char *host, int port, char *keytype,
			 char *keystr, char *fingerprint)
{
    Str255 stuff;
    Session *s = frontend;

    /*
     * This function is horribly wrong.  For one thing, the alert
     * shouldn't be modal, it should be movable modal, or a sheet in
     * Aqua.  Also, PuTTY might be in the background, in which case we
     * should use the Notification Manager to wake up the user.  In
     * any case, we shouldn't hold up processing of other connections'
     * data just because this one's waiting for the user.  It should
     * also handle a host key cache, of course, and see the note below
     * about closing the connection.  All in all, a bit of a mess
     * really.
     */

    stuff[0] = sprintf((char *)(&stuff[1]),
		       "The server's key fingerprint is: %s\n"
		       "Continue connecting?", fingerprint);
    ParamText(stuff, NULL, NULL, NULL);
    if (CautionAlert(wQuestion, NULL) == 2) {
	/*
	 * User chose "Cancel".  Unfortunately, if I tear the
	 * connection down here, Bad Things happen when I return.  I
	 * think this function should actually return something
	 * telling the SSH code to abandon the connection.
	 */
    }
}

void askalg(void *frontend, const char *algtype, const char *algname)
{

}

void old_keyfile_warning(void)
{

}

FontSpec platform_default_fontspec(char const *name)
{
    FontSpec ret;
    long smfs;

    if (!strcmp(name, "Font")) {
	smfs = GetScriptVariable(smSystemScript, smScriptMonoFondSize);
	if (smfs == 0)
	    smfs = GetScriptVariable(smRoman, smScriptMonoFondSize);
	if (smfs != 0) {
	    GetFontName(HiWord(smfs), ret.name);
	    if (ret.name[0] == 0)
		memcpy(ret.name, "\pMonaco", 7);
	    ret.size = LoWord(smfs);
	} else {
	    memcpy(ret.name, "\pMonaco", 7);
	    ret.size = 9;
	}
	ret.face = 0;
    } else {
	ret.name[0] = 0;
    }

    return ret;
}

Filename platform_default_filename(const char *name)
{
    Filename ret;
    if (!strcmp(name, "LogFileName"))
	FSMakeFSSpec(0, 0, "\pputty.log", &ret.fss);
    else
	memset(&ret, 0, sizeof(ret));
    return ret;
}

char *platform_default_s(char const *name)
{
    return NULL;
}

int platform_default_i(char const *name, int def)
{

    /* Non-raw cut and paste of line-drawing chars works badly on the
     * current Unix stub implementation of the Unicode functions.
     * So I'm going to temporarily set the default to raw mode so
     * that the failure mode isn't quite so drastically horrid.
     * When Unicode comes in, this can all be put right. */
    if (!strcmp(name, "RawCNP"))
	return 1;
    return def;
}

void platform_get_x11_auth(char *display, int *proto,
                           unsigned char *data, int *datalen)
{
    /* SGT: I have no idea whether Mac X servers need anything here. */
}

void update_specials_menu(void *frontend)
{
    Session *s = frontend;
    WindowPtr front;

    front = mac_frontwindow();
    if (front != NULL && mac_windowsession(front) == s)
	mac_adjustmenus();
}

void notify_remote_exit(void *fe) { /* XXX anything needed here? */ }

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */
