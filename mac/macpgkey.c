/* $Id: macpgkey.c,v 1.2 2003/02/16 14:27:37 ben Exp $ */
/*
 * Copyright (c) 2003 Ben Harris
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

/* Stuff to handle the key window in PuTTYgen */

#include <MacTypes.h>
#include <Dialogs.h>
#include <MacWindows.h>

#include "putty.h"
#include "mac.h"
#include "macpgrid.h"

static void mac_clickkey(WindowPtr window, EventRecord *event)
{
    short item;
    DialogRef dialog;

    dialog = GetDialogFromWindow(window);
    if (DialogSelect(event, &dialog, &item))
	switch (item) {
	  case wiKeyGenerate:
	    /* Do something */
	    break;
	}
}

static void mac_activatekey(WindowPtr window, EventRecord *event)
{
    DialogRef dialog;
    DialogItemType itemtype;
    Handle itemhandle;
    short item;
    Rect itemrect;
    int active;

    dialog = GetDialogFromWindow(window);
    active = (event->modifiers & activeFlag) != 0;
    GetDialogItem(dialog, wiKeyGenerate, &itemtype, &itemhandle, &itemrect);
    HiliteControl((ControlHandle)itemhandle, active ? 0 : 255);
    DialogSelect(event, &dialog, &item);
}

static void mac_updatekey(WindowPtr window)
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

void mac_newkey(void)
{
    KeyState *ks;
    WinInfo *wi;

    ks = smalloc(sizeof(*ks));
    ks->box = GetNewDialog(wKey, NULL, (WindowPtr)-1);
    wi = smalloc(sizeof(*wi));
    memset(wi, 0, sizeof(*wi));
    wi->ks = ks;
    wi->wtype = wKey;
    wi->update = &mac_updatekey;
    wi->click = &mac_clickkey;
    wi->activate = &mac_activatekey;
    SetWRefCon(GetDialogWindow(ks->box), (long)wi);
    ShowWindow(GetDialogWindow(ks->box));
}

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */
