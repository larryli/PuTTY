/* $Id$ */
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
#include <Navigation.h>
#include <Resources.h>
#include <StandardFile.h>
#include <TextUtils.h>
#include <Windows.h>

#include <assert.h>
#include <string.h>

#include "putty.h"
#include "dialog.h"
#include "mac.h"
#include "macresid.h"
#include "storage.h"

static void mac_config(int);
static void mac_closedlg(WindowPtr);
static void mac_enddlg_config(WindowPtr, int);
static void mac_enddlg_reconfig(WindowPtr, int);

void mac_newsession(void)
{
    mac_config(FALSE);
}

void mac_reconfig(void)
{
    mac_config(TRUE);
}

static void mac_config(int midsession)
{
    Session *s;
    WinInfo *wi;
    static struct sesslist sesslist;
    Str255 mactitle;
    char *str;

    if (midsession) {
        s = mac_windowsession(FrontWindow());
    } else {  
        s = snew(Session);
        memset(s, 0, sizeof(*s));
        do_defaults(NULL, &s->cfg);
        s->hasfile = FALSE;
    }

    /* Copy the configuration somewhere else in case this is a *
     * reconfiguration and the user cancels the operation      */

    s->temp_cfg = s->cfg;

    if (HAVE_COLOR_QD())
	s->settings_window = GetNewCWindow(wSettings, NULL, (WindowPtr)-1);
    else
	s->settings_window = GetNewWindow(wSettings, NULL, (WindowPtr)-1);

    get_sesslist(&sesslist, TRUE);
    s->ctrlbox = ctrl_new_box();
    setup_config_box(s->ctrlbox, &sesslist, midsession, 0, 0);

    s->settings_ctrls.data = &s->temp_cfg;
    if (midsession)
        s->settings_ctrls.end = &mac_enddlg_reconfig;
    else
        s->settings_ctrls.end = &mac_enddlg_config;

    macctrl_layoutbox(s->ctrlbox, s->settings_window, &s->settings_ctrls);

    wi = snew(WinInfo);
    memset(wi, 0, sizeof(*wi));
    wi->s = s;
    wi->mcs = &s->settings_ctrls;
    wi->wtype = wSettings;
    wi->update = &macctrl_update;
    wi->click = &macctrl_click;
    wi->key = &macctrl_key;
    wi->activate = &macctrl_activate;
    wi->adjustmenus = &macctrl_adjustmenus;
    wi->close = &mac_closedlg;
    SetWRefCon(s->settings_window, (long)wi);
    if (midsession)
        str = dupprintf("%s Reconfiguration", appname);
    else
        str = dupprintf("%s Configuration", appname);
    c2pstrcpy(mactitle, str);
    sfree(str);
    SetWTitle(s->settings_window, mactitle);
    ShowWindow(s->settings_window);
}

static void mac_closedlg(WindowPtr window)
{
    Session *s = mac_windowsession(window);

    macctrl_close(window);
    DisposeWindow(window);
    if (s->window == NULL)
	sfree(s);
}

static void mac_enddlg_config(WindowPtr window, int value)
{
    Session *s = mac_windowsession(window);

    if (value == 0)
	mac_closedlg(window);
    else {
        s->cfg = s->temp_cfg;
	mac_startsession(s);
	mac_closedlg(window);
    }
}

static void mac_enddlg_reconfig(WindowPtr window, int value)
{
    Session *s = mac_windowsession(window);

    if (value == 0)
	mac_closedlg(window);
    else {
        Config prev_cfg = s->cfg;
        s->cfg = s->temp_cfg;
	mac_closedlg(window);

	/* Pass new config data to the logging module */
	log_reconfig(s->logctx, &s->cfg);

	/*
	 * Flush the line discipline's edit buffer in the
	 * case where local editing has just been disabled.
	 */
	if (s->ldisc)
	    ldisc_send(s->ldisc, NULL, 0, 0);

	/* Change the palette */
	palette_reset(s);

	/* Pass new config data to the terminal */
	term_reconfig(s->term, &s->cfg);

	/* Pass new config data to the back end */
	if (s->back)
            s->back->reconfig(s->backhandle, &s->cfg);

	/* Screen size changed ? */
	if (s->cfg.height != prev_cfg.height ||
	    s->cfg.width != prev_cfg.width ||
	    s->cfg.savelines != prev_cfg.savelines) {
	    request_resize(s, s->cfg.width, s->cfg.height);
	}

	/* Set the window title */
	if (s->cfg.wintitle[0])
            set_title(s, s->cfg.wintitle);

	/* Scroll bar */
	if (s->cfg.scrollbar != prev_cfg.scrollbar)
           request_resize(s, s->cfg.width, s->cfg.height);

	/* TODO: zoom, font */
    }
}

void mac_dupsession(void)
{
    Session *s1 = mac_windowsession(FrontWindow());
    Session *s2;

    s2 = snew(Session);
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

    s = snew(Session);
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

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */
