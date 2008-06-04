/* $Id$ */
/*
 * Copyright (c) 1999, 2002, 2003 Ben Harris
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

#include <MacTypes.h>
#include <Dialogs.h>
#include <MacWindows.h>
#include <Resources.h>
#include <Script.h>
#include <ToolUtils.h>

#include <assert.h>
#include <stdlib.h>

#include "putty.h"
#include "mac.h"
#include "macresid.h"

static struct mac_windows {
    WindowPtr about;
    WindowPtr licence;
} windows;

static void mac_openlicence(void);

static void mac_clickabout(WindowPtr window, EventRecord *event)
{
    short item;
    DialogRef dialog;

    dialog = GetDialogFromWindow(window);
    if (DialogSelect(event, &dialog, &item))
	switch (item) {
	  case wiAboutLicence:
	    mac_openlicence();
	    break;
	}
}

static void mac_activateabout(WindowPtr window, EventRecord *event)
{
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

static void mac_updateabout(WindowPtr window)
{
#if TARGET_API_MAC_CARBON
    RgnHandle rgn;
#endif

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
}

static void mac_closeabout(WindowPtr window)
{

    windows.about = NULL;
    DisposeDialog(GetDialogFromWindow(window));
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

static void mac_closelicence(WindowPtr window)
{

    windows.licence = NULL;
    DisposeWindow(window);
}

void mac_openabout(void)
{
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
	wi = snew(WinInfo);
	memset(wi, 0, sizeof(*wi));
	wi->wtype = wAbout;
	wi->update = &mac_updateabout;
	wi->click = &mac_clickabout;
	wi->activate = &mac_activateabout;
	wi->close = &mac_closeabout;
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

static void mac_openlicence(void)
{
    WinInfo *wi;

    if (windows.licence)
	SelectWindow(windows.licence);
    else {
	windows.licence = GetNewWindow(wLicence, NULL, (WindowPtr)-1);
	wi = snew(WinInfo);
	memset(wi, 0, sizeof(*wi));
	wi->wtype = wLicence;
	wi->update = &mac_updatelicence;
	wi->close = &mac_closelicence;
	SetWRefCon(windows.licence, (long)wi);
	ShowWindow(windows.licence);
    }
}

