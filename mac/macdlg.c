/* $Id: macdlg.c,v 1.2 2003/01/01 11:45:43 ben Exp $ */
/*
 * Copyright (c) 2002 Ben Harris
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
 * macdlg.c - settings dialogue box for Mac OS.
 */

#include <MacTypes.h>
#include <Dialogs.h>
#include <Windows.h>

#include <string.h>

#include "putty.h"
#include "mac.h"
#include "macresid.h"

void mac_newsession(void)
{
    Session *s;

    /* This should obviously be initialised by other means */
    s = smalloc(sizeof(*s));
    memset(s, 0, sizeof(*s));
    do_defaults(NULL, &s->cfg);
    s->back = &loop_backend;

    s->settings_window = GetNewDialog(wSettings, NULL, (WindowPtr)-1);

    SetWRefCon(s->settings_window, (long)s);
    ShowWindow(s->settings_window);
}

void mac_activatedlg(WindowPtr window, EventRecord *event)
{
    DialogItemType itemtype;
    Handle itemhandle;
    short item;
    Rect itemrect;
    int active;

    active = (event->modifiers & activeFlag) != 0;
    GetDialogItem(window, wiSettingsOpen, &itemtype, &itemhandle, &itemrect);
    HiliteControl((ControlHandle)itemhandle, active ? 0 : 255);
    DialogSelect(event, &window, &item);
}

void mac_clickdlg(WindowPtr window, EventRecord *event)
{
    short item;
    Session *s = (Session *)GetWRefCon(window);

    if (DialogSelect(event, &window, &item))
	switch (item) {
	  case wiSettingsOpen:
	    CloseWindow(window);
	    mac_startsession(s);
	    break;
	}
}

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */
