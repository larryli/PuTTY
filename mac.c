/* $Id: mac.c,v 1.1.2.4 1999/02/20 22:10:33 ben Exp $ */
/*
 * mac.c -- miscellaneous Mac-specific routines
 */

#include <MacTypes.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <MacWindows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Devices.h>
#include <DiskInit.h>
#include <ToolUtils.h>

#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>		/* putty.h needs size_t */

#include "macresid.h"
#include "putty.h"

QDGlobals qd;

int cold = 1;

static void mac_startup(void);
static void mac_eventloop(void);
static void mac_event(EventRecord *);
static void mac_contentclick(WindowPtr, EventRecord *);
static void mac_updatewindow(WindowPtr);
static void mac_keypress(EventRecord *);
static int mac_windowtype(WindowPtr);
static void mac_menucommand(long);
static void mac_adjustcursor(void);
static void mac_adjustmenus(void);
static void mac_closewindow(WindowPtr);
static void mac_zoomwindow(WindowPtr, short);
static void mac_shutdown(void);

int main (int argc, char **argv) {

    mac_startup();
    mac_eventloop();
}

static void mac_startup(void) {
    Handle menuBar;

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
    InitDialogs(nil);
    cold = 0;
    
    menuBar = GetNewMBar(128);
    if (menuBar == NULL)
	fatalbox("Unable to create menu bar.");
    SetMenuBar(menuBar);
    AppendResMenu(GetMenuHandle(mApple), 'DRVR');
    mac_adjustmenus();
    DrawMenuBar();
    InitCursor();
}

static void mac_eventloop(void) {
    Boolean gotevent;
    EventRecord event;
    int i;

    for (;;) {
    	mac_adjustcursor();
	gotevent = WaitNextEvent(everyEvent, &event, LONG_MAX, NULL);
	mac_adjustcursor();
	if (gotevent)
	    mac_event(&event);
    }
}

static void mac_event(EventRecord *event) {
    short part;
    WindowPtr window;
    Point pt;

    switch (event->what) {
      case mouseDown:
	part = FindWindow(event->where, &window);
	switch (part) {
	  case inMenuBar:
	    mac_adjustmenus();
	    mac_menucommand(MenuSelect(event->where));
	    break;
	  case inSysWindow:
	    SystemClick(event, window);
	    break;
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
	    DragWindow(window, event->where, &qd.screenBits.bounds);
	    break;
	  case inGrow:
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
        /* FIXME: Do something */
        break;
      case updateEvt:
        mac_updatewindow((WindowPtr)event->message);
        break;
      case diskEvt:
	if (HiWord(event->message) != noErr) {
	    SetPt(&pt, 120, 120);
	    DIBadMount(pt, event->message);
        }
        break;
    }
}

static void mac_contentclick(WindowPtr window, EventRecord *event) {
    short item;

    switch (mac_windowtype(window)) {
      case wTerminal:
	/* XXX: Do something. */
	break;
      case wAbout:
	if (DialogSelect(event, &(DialogPtr)window, &item))
	    switch (item) {
	      case wiAboutClose:
		mac_closewindow(window);
		break;
	      case wiAboutLicence:
	        /* XXX: Do something */
		break;
	    }
	break;
    }
}

static void mac_updatewindow(WindowPtr window) {

    switch (mac_windowtype(window)) {
      case wTerminal:
	/* XXX: DO something */
	break;
      case wAbout:
	BeginUpdate(window);
	UpdateDialog(window, window->visRgn);
	EndUpdate(window);
	break;
    }
}

/*
 * Work out what kind of window we're dealing with.
 * Concept shamelessly nicked from SurfWriter.
 */
static int mac_windowtype(WindowPtr window) {
    int kind;

    if (window == NULL)
	return wNone;
    kind = ((WindowPeek)window)->windowKind;
    if (kind < 0)
	return wDA;
    else if (kind == userKind)
	return wTerminal;
    else
	return GetWRefCon(window);
}

/*
 * Handle a key press
 */
static void mac_keypress(EventRecord *event) {
    char key;

    if (event->what == keyDown && (event->modifiers & cmdKey)) {
	mac_adjustmenus();
	mac_menucommand(MenuKey(event->message & charCodeMask));
    }
}

static void mac_menucommand(long result) {
    short menu, item;
    Str255 da;

    menu = HiWord(result);
    item = LoWord(result);
    switch (menu) {
      case mApple:
        switch (item) {
          case iAbout:
            GetNewDialog(wAbout, NULL, (GrafPort *)-1);
            break;
          default:
            GetMenuItemText(GetMenuHandle(mApple), item, da);
            OpenDeskAcc(da);
            break;
        }
        break;
      case mFile:
        switch (item) {
          case iClose:
            mac_closewindow(FrontWindow());
            break;
          case iQuit:
            mac_shutdown();
            break;
        }
        break;
    }
    HiliteMenu(0);
}

static void mac_closewindow(WindowPtr window) {

    switch (mac_windowtype(window)) {
      case wDA:
	CloseDeskAcc(((WindowPeek)window)->windowKind);
	break;
      case wTerminal:
	/* FIXME: end session and stuff */
	break;
      default:
	CloseWindow(window);
	break;
    }
}

static void mac_zoomwindow(WindowPtr window, short part) {

    /* FIXME: do something */
}

/*
 * Make the menus look right before the user gets to see them.
 */
static void mac_adjustmenus(void) {

}

/*
 * Make sure the right cursor's being displayed.
 */
static void mac_adjustcursor(void) {

    SetCursor(&qd.arrow);
}

static void mac_shutdown(void) {

    ExitToShell();
}

void fatalbox(const char *fmt, ...) {
    va_list ap;
    Str255 stuff;
    
    va_start(ap, fmt);
    /* We'd like stuff to be a Pascal string */
    stuff[0] = vsprintf((char *)(&stuff[1]), fmt, ap);
    va_end(ap);
    ParamText(stuff, NULL, NULL, NULL);
    StopAlert(128, nil);
    exit(1);
}
