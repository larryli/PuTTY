/* $Id: macdlg.c,v 1.12 2003/02/15 16:22:15 ben Exp $ */
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
#include <Navigation.h>
#include <Resources.h>
#include <StandardFile.h>
#include <Windows.h>

#include <assert.h>
#include <string.h>

#include "putty.h"
#include "mac.h"
#include "macresid.h"
#include "storage.h"

static void mac_clickdlg(WindowPtr, EventRecord *);
static void mac_activatedlg(WindowPtr, EventRecord *);
static void mac_updatedlg(WindowPtr);
static void mac_adjustdlgmenus(WindowPtr);

void mac_newsession(void)
{
    Session *s;
    WinInfo *wi;

    /* This should obviously be initialised by other means */
    s = smalloc(sizeof(*s));
    memset(s, 0, sizeof(*s));
    do_defaults(NULL, &s->cfg);
    s->hasfile = FALSE;

    s->settings_window =
	GetDialogWindow(GetNewDialog(wSettings, NULL, (WindowPtr)-1));

    wi = smalloc(sizeof(*wi));
    memset(wi, 0, sizeof(*wi));
    wi->s = s;
    wi->wtype = wSettings;
    wi->update = &mac_updatedlg;
    wi->click = &mac_clickdlg;
    wi->activate = &mac_activatedlg;
    wi->adjustmenus = &mac_adjustdlgmenus;
    SetWRefCon(s->settings_window, (long)wi);
    ShowWindow(s->settings_window);
}

void mac_dupsession(void)
{
    Session *s1 = mac_windowsession(FrontWindow());
    Session *s2;

    s2 = smalloc(sizeof(*s2));
    memset(s2, 0, sizeof(*s2));
    s2->cfg = s1->cfg;
    s2->hasfile = s1->hasfile;
    s2->savefile = s1->savefile;

    mac_startsession(s2);
}

static OSErr mac_opensessionfrom(FSSpec *fss)
{
    FInfo fi;
    Session *s;
    void *sesshandle;
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

    mac_startsession(s);
    return noErr;

  fail:
    sfree(s);
    return err;
}

static OSErr mac_openlist(AEDesc docs)
{
    OSErr err;
    long ndocs, i;
    FSSpec fss;
    AEKeyword keywd;
    DescType type;
    Size size;

    err = AECountItems(&docs, &ndocs);
    if (err != noErr) return err;

    for (i = 0; i < ndocs; i++) {
	err = AEGetNthPtr(&docs, i + 1, typeFSS,
			  &keywd, &type, &fss, sizeof(fss), &size);
	if (err != noErr) return err;;
	err = mac_opensessionfrom(&fss);
	if (err != noErr) return err;
    }
    return noErr;
}

void mac_opensession(void)
{

    if (mac_gestalts.navsvers > 0) {
	NavReplyRecord navr;
	NavDialogOptions navopts;
	NavTypeListHandle navtypes;
	AEDesc defaultloc = { 'null', NULL };
	AEDesc *navdefault = NULL;
	short vol;
	long dirid;
	FSSpec fss;

	if (NavGetDefaultDialogOptions(&navopts) != noErr) return;
	/* XXX should we create sessions dir? */
	if (get_session_dir(FALSE, &vol, &dirid) == noErr &&
	    FSMakeFSSpec(vol, dirid, NULL, &fss) == noErr &&
	    AECreateDesc(typeFSS, &fss, sizeof(fss), &defaultloc) == noErr)
	    navdefault = &defaultloc;
	/* Can't meaningfully preview a saved session yet */
	navopts.dialogOptionFlags &= ~kNavAllowPreviews;
	navtypes = (NavTypeListHandle)GetResource('open', open_pTTY);
	if (NavGetFile(navdefault, &navr, &navopts, NULL, NULL, NULL, navtypes,
		       NULL) == noErr && navr.validRecord)
	    mac_openlist(navr.selection);
	NavDisposeReply(&navr);
	if (navtypes != NULL)
	    ReleaseResource((Handle)navtypes);
    }
#if !TARGET_API_MAC_CARBON /* XXX Navigation Services */
    else {
	StandardFileReply sfr;
	static const OSType sftypes[] = { 'Sess', 0, 0, 0 };

	StandardGetFile(NULL, 1, sftypes, &sfr);
	if (!sfr.sfGood) return;

	mac_opensessionfrom(&sfr.sfFile);
	/* XXX handle error */
    }
#endif
}

void mac_savesession(void)
{
    Session *s = mac_windowsession(FrontWindow());
    void *sesshandle;

    assert(s->hasfile);
    sesshandle = open_settings_w_fsp(&s->savefile);
    if (sesshandle == NULL) return; /* XXX report error */
    save_open_settings(sesshandle, TRUE, &s->cfg);
    close_settings_w(sesshandle);
}

void mac_savesessionas(void)
{
#if !TARGET_API_MAC_CARBON /* XXX Navigation Services */
    Session *s = mac_windowsession(FrontWindow());
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
#endif
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
    Size size;
    AEDescList docs = { typeNull, NULL };
    OSErr err;

    err = AEGetParamDesc(req, keyDirectObject, typeAEList, &docs);
    if (err != noErr) goto out;

    if (AEGetAttributePtr(req, keyMissedKeywordAttr, typeWildCard,
			  &type, NULL, 0, &size) == noErr) {
	err = errAEParamMissed;
	goto out;
    }

    err = mac_openlist(docs);

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

static void mac_activatedlg(WindowPtr window, EventRecord *event)
{
    DialogItemType itemtype;
    Handle itemhandle;
    short item;
    Rect itemrect;
    int active;
    DialogRef dialog = GetDialogFromWindow(window);

    active = (event->modifiers & activeFlag) != 0;
    GetDialogItem(dialog, wiSettingsOpen, &itemtype, &itemhandle, &itemrect);
    HiliteControl((ControlHandle)itemhandle, active ? 0 : 255);
    DialogSelect(event, &dialog, &item);
}

static void mac_clickdlg(WindowPtr window, EventRecord *event)
{
    short item;
    Session *s = mac_windowsession(window);
    DialogRef dialog = GetDialogFromWindow(window);

    if (DialogSelect(event, &dialog, &item))
	switch (item) {
	  case wiSettingsOpen:
	    HideWindow(window);
	    mac_startsession(s);
	    break;
	}
}

static void mac_updatedlg(WindowPtr window)
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

#if TARGET_API_MAC_CARBON
#define EnableItem EnableMenuItem
#define DisableItem DisableMenuItem
#endif
static void mac_adjustdlgmenus(WindowPtr window)
{
    MenuHandle menu;

    menu = GetMenuHandle(mFile);
    DisableItem(menu, iSave); /* XXX enable if modified */
    EnableItem(menu, iSaveAs);
    EnableItem(menu, iDuplicate);

    menu = GetMenuHandle(mEdit);
    DisableItem(menu, 0);
}

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */
