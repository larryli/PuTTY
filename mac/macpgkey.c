/* $Id: macpgkey.c,v 1.3 2003/02/20 22:55:09 ben Exp $ */
/*
 * Copyright (c) 2003 Ben Harris
 * Copyright (c) 1997-2003 Simon Tatham
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
#include <Controls.h>
#include <Dialogs.h>
#include <MacWindows.h>

#include "putty.h"
#include "mac.h"
#include "macpgrid.h"
#include "ssh.h"

/* ----------------------------------------------------------------------
 * Progress report code. This is really horrible :-)
 */
#define PROGRESSRANGE 65535
#define MAXPHASE 5
struct progress {
    int nphases;
    struct {
	int exponential;
	unsigned startpoint, total;
	unsigned param, current, n;    /* if exponential */
	unsigned mult;		       /* if linear */
    } phases[MAXPHASE];
    unsigned total, divisor, range;
    ControlHandle progbar;
};

static void progress_update(void *param, int action, int phase, int iprogress)
{
    struct progress *p = (struct progress *) param;
    unsigned progress = iprogress;
    int position;

    if (action < PROGFN_READY && p->nphases < phase)
	p->nphases = phase;
    switch (action) {
      case PROGFN_INITIALISE:
	p->nphases = 0;
	break;
      case PROGFN_LIN_PHASE:
	p->phases[phase-1].exponential = 0;
	p->phases[phase-1].mult = p->phases[phase].total / progress;
	break;
      case PROGFN_EXP_PHASE:
	p->phases[phase-1].exponential = 1;
	p->phases[phase-1].param = 0x10000 + progress;
	p->phases[phase-1].current = p->phases[phase-1].total;
	p->phases[phase-1].n = 0;
	break;
      case PROGFN_PHASE_EXTENT:
	p->phases[phase-1].total = progress;
	break;
      case PROGFN_READY:
	{
	    unsigned total = 0;
	    int i;
	    for (i = 0; i < p->nphases; i++) {
		p->phases[i].startpoint = total;
		total += p->phases[i].total;
	    }
	    p->total = total;
	    p->divisor = ((p->total + PROGRESSRANGE - 1) / PROGRESSRANGE);
	    p->range = p->total / p->divisor;
	    SetControlMaximum(p->progbar, p->range);
	}
	break;
      case PROGFN_PROGRESS:
	if (p->phases[phase-1].exponential) {
	    while (p->phases[phase-1].n < progress) {
		p->phases[phase-1].n++;
		p->phases[phase-1].current *= p->phases[phase-1].param;
		p->phases[phase-1].current /= 0x10000;
	    }
	    position = (p->phases[phase-1].startpoint +
			p->phases[phase-1].total - p->phases[phase-1].current);
	} else {
	    position = (p->phases[phase-1].startpoint +
			progress * p->phases[phase-1].mult);
	}
	SetControlValue(p->progbar, position / p->divisor);
	break;
    }
}

static void mac_clickkey(WindowPtr window, EventRecord *event)
{
    short item;
    DialogRef dialog;
    KeyState *ks = mac_windowkey(window);

    dialog = GetDialogFromWindow(window);
    if (DialogSelect(event, &dialog, &item))
	switch (item) {
	  case wiKeyGenerate:
	    SetControlMaximum(ks->progress, 1024);
	    ks->entropy = smalloc(1024 * sizeof(*ks->entropy));
	    ks->entropy_required = 1024;
	    ks->entropy_got = 0;
	    ks->collecting_entropy = TRUE;
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
    Handle h;
    short type;
    Rect rect;

    ks = smalloc(sizeof(*ks));
    ks->box = GetNewDialog(wKey, NULL, (WindowPtr)-1);
    GetDialogItem(ks->box, wiKeyProgress, &type, &h, &rect);
    ks->progress = (ControlHandle)h;
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
