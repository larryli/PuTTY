/* $Id: macdlg.c,v 1.7 2003/01/25 14:04:47 ben Exp $ */
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
#include <AEDataModel.h>
#include <AppleEvents.h>
#include <Dialogs.h>
#include <Resources.h>
#include <StandardFile.h>
#include <Windows.h>

#include <assert.h>
#include <string.h>

#include "putty.h"
#include "mac.h"
#include "macresid.h"
#include "storage.h"

void mac_newsession(void)
{
    Session *s;

    /* This should obviously be initialised by other means */
    s = smalloc(sizeof(*s));
    memset(s, 0, sizeof(*s));
    do_defaults(NULL, &s->cfg);
    s->back = &loop_backend;
    s->hasfile = FALSE;

    s->settings_window = GetNewDialog(wSettings, NULL, (WindowPtr)-1);

    SetWRefCon(s->settings_window, (long)s);
    ShowWindow(s->settings_window);
}

static OSErr mac_opensessionfrom(FSSpec *fss)
{
    FInfo fi;
    Session *s;
    void *sesshandle;
    int i;
    OSErr err;

    s = smalloc(sizeof(*s));
    memset(s, 0, sizeof(*s));

    err = FSpGetFInfo(fss, &fi);
    if (err != noErr) return err;
    if (fi.fdFlags & kIsStationery)
	s->hasfile = FALSE;
    else {
	s->hasfile = TRUE;
	s->savefile = *fss;
    }

    sesshandle = open_settings_r_fsp(fss);
    if (sesshandle == NULL) {
	/* XXX need a way to pass up an error number */
	err = -9999;
	goto fail;
    }
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
    return noErr;

  fail:
    sfree(s);
    return err;
}

void mac_opensession(void) {
    StandardFileReply sfr;
    static const OSType sftypes[] = { 'Sess', 0, 0, 0 };

    StandardGetFile(NULL, 1, sftypes, &sfr);
    if (!sfr.sfGood) return;

    mac_opensessionfrom(&sfr.sfFile);
    /* XXX handle error */
}

void mac_savesession(void)
{
    Session *s = (Session *)GetWRefCon(FrontWindow());
    void *sesshandle;

    assert(s->hasfile);
    sesshandle = open_settings_w_fsp(&s->savefile);
    if (sesshandle == NULL) return; /* XXX report error */
    save_open_settings(sesshandle, TRUE, &s->cfg);
    close_settings_w(sesshandle);
}

void mac_savesessionas(void)
{
    Session *s = (Session *)GetWRefCon(FrontWindow());
    StandardFileReply sfr;
    void *sesshandle;

    StandardPutFile("\pSave session as:",
		    s->hasfile ? s->savefile.name : "\puntitled", &sfr);
    if (!sfr.sfGood) return;

    if (!sfr.sfReplacing) {
	FSpCreateResFile(&sfr.sfFile, PUTTY_CREATOR, SESS_TYPE, sfr.sfScript);
	if (ResError() != noErr) return; /* XXX report error */
    }
    sesshandle = open_settings_w_fsp(&sfr.sfFile);
    if (sesshandle == NULL) return; /* XXX report error */
    save_open_settings(sesshandle, TRUE, &s->cfg);
    close_settings_w(sesshandle);
    s->hasfile = TRUE;
    s->savefile = sfr.sfFile;
}

pascal OSErr mac_aevt_oapp(const AppleEvent *req, AppleEvent *reply,
			   long refcon)
{
    DescType type;
    Size size;

    if (AEGetAttributePtr(req, keyMissedKeywordAttr, typeWildCard,
			  &type, NULL, 0, &size) == noErr)
	return errAEParamMissed;

    /* XXX we should do something here. */
    return noErr;
}

pascal OSErr mac_aevt_odoc(const AppleEvent *req, AppleEvent *reply,
			   long refcon)
{
    DescType type;
    AEKeyword keywd;
    Size size;
    AEDescList docs = { typeNull, NULL };
    OSErr err;
    long ndocs, i;
    FSSpec fss;

    err = AEGetParamDesc(req, keyDirectObject, typeAEList, &docs);
    if (err != noErr) goto out;

    if (AEGetAttributePtr(req, keyMissedKeywordAttr, typeWildCard,
			  &type, NULL, 0, &size) == noErr) {
	err = errAEParamMissed;
	goto out;
    }

    err = AECountItems(&docs, &ndocs);
    if (err != noErr) goto out;

    for (i = 0; i < ndocs; i++) {
	err = AEGetNthPtr(&docs, i + 1, typeFSS,
			  &keywd, &type, &fss, sizeof(fss), &size);
	if (err != noErr) goto out;
	err = mac_opensessionfrom(&fss);
	if (err != noErr) goto out;
    }

  out:
    AEDisposeDesc(&docs);
    return err;
}

pascal OSErr mac_aevt_pdoc(const AppleEvent *req, AppleEvent *reply,
			   long refcon)
{
    DescType type;
    Size size;

    if (AEGetAttributePtr(req, keyMissedKeywordAttr, typeWildCard,
			  &type, NULL, 0, &size) == noErr)
	return errAEParamMissed;

    /* We can't meaningfully do anything here. */
    return errAEEventNotHandled;
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
