/* $Id: macctrls.c,v 1.24 2003/04/03 23:18:06 ben Exp $ */
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

#include <MacTypes.h>
#include <Appearance.h>
#include <Controls.h>
#include <ControlDefinitions.h>
#include <Menus.h>
#include <Resources.h>
#include <Script.h>
#include <Sound.h>
#include <TextEdit.h>
#include <TextUtils.h>
#include <ToolUtils.h>
#include <Windows.h>

#include <assert.h>
#include <string.h>

#include "putty.h"
#include "mac.h"
#include "macresid.h"
#include "dialog.h"
#include "tree234.h"

/* Range of menu IDs for popup menus */
#define MENU_MIN	1024
#define MENU_MAX	2048


union macctrl {
    struct macctrl_generic {
	enum {
	    MACCTRL_TEXT,
	    MACCTRL_EDITBOX,
	    MACCTRL_RADIO,
	    MACCTRL_CHECKBOX,
	    MACCTRL_BUTTON,
	    MACCTRL_POPUP
	} type;
	/* Template from which this was generated */
	union control *ctrl;
	/* Next control in this panel */
	union macctrl *next;
	void *privdata;
	int freeprivdata;
    } generic;
    struct {
	struct macctrl_generic generic;
	ControlRef tbctrl;
    } text;
    struct {
	struct macctrl_generic generic;
	ControlRef tbctrl;
	ControlRef tblabel;
    } editbox;
    struct {
	struct macctrl_generic generic;
	ControlRef *tbctrls;
	ControlRef tblabel;
    } radio;
    struct {
	struct macctrl_generic generic;
	ControlRef tbctrl;
    } checkbox;
    struct {
	struct macctrl_generic generic;
	ControlRef tbctrl;
    } button;
    struct {
	struct macctrl_generic generic;
	ControlRef tbctrl;
	MenuRef menu;
	int menuid;
	unsigned int nids;
	int *ids;
    } popup;
};

struct mac_layoutstate {
    Point pos;
    unsigned int width;
    unsigned int panelnum;
};

#define ctrlevent(mcs, mc, event) do {					\
    if ((mc)->generic.ctrl->generic.handler != NULL)			\
	(*(mc)->generic.ctrl->generic.handler)((mc)->generic.ctrl, (mcs),\
					       (mcs)->data, (event));	\
} while (0)

#define findbyctrl(mcs, ctrl)						\
    find234((mcs)->byctrl, (ctrl), macctrl_cmp_byctrl_find)

static void macctrl_layoutset(struct mac_layoutstate *, struct controlset *, 
			      WindowPtr, struct macctrls *);
static void macctrl_switchtopanel(struct macctrls *, unsigned int);
static void macctrl_setfocus(struct macctrls *, union macctrl *);
static void macctrl_text(struct macctrls *, WindowPtr,
			 struct mac_layoutstate *, union control *);
static void macctrl_editbox(struct macctrls *, WindowPtr,
			    struct mac_layoutstate *, union control *);
static void macctrl_radio(struct macctrls *, WindowPtr,
			  struct mac_layoutstate *, union control *);
static void macctrl_checkbox(struct macctrls *, WindowPtr,
			     struct mac_layoutstate *, union control *);
static void macctrl_button(struct macctrls *, WindowPtr,
			   struct mac_layoutstate *, union control *);
static void macctrl_popup(struct macctrls *, WindowPtr,
			  struct mac_layoutstate *, union control *);
#if !TARGET_API_MAC_CARBON
static pascal SInt32 macctrl_sys7_editbox_cdef(SInt16, ControlRef,
					       ControlDefProcMessage, SInt32);
static pascal SInt32 macctrl_sys7_default_cdef(SInt16, ControlRef,
					       ControlDefProcMessage, SInt32);
#endif

#if !TARGET_API_MAC_CARBON
/*
 * This trick enables us to keep all the CDEF code in the main
 * application, which makes life easier.  For details, see
 * <http://developer.apple.com/technotes/tn/tn2003.html#custom_code_base>.
 */

#pragma options align=mac68k
typedef struct {
    short		jmpabs;	/* 4EF9 */
    ControlDefUPP	theUPP;
} **PatchCDEF;
#pragma options align=reset
#endif

static void macctrl_init()
{
#if !TARGET_API_MAC_CARBON
    static int inited = 0;
    PatchCDEF cdef;

    if (inited) return;
    cdef = (PatchCDEF)GetResource(kControlDefProcResourceType, CDEF_EditBox);
    (*cdef)->theUPP = NewControlDefProc(macctrl_sys7_editbox_cdef);
    cdef = (PatchCDEF)GetResource(kControlDefProcResourceType, CDEF_Default);
    (*cdef)->theUPP = NewControlDefProc(macctrl_sys7_default_cdef);
    inited = 1;
#endif
}


static int macctrl_cmp_byctrl(void *av, void *bv)
{
    union macctrl *a = (union macctrl *)av;
    union macctrl *b = (union macctrl *)bv;

    if (a->generic.ctrl < b->generic.ctrl)
	return -1;
    else if (a->generic.ctrl > b->generic.ctrl)
	return +1;
    else
	return 0;
}

static int macctrl_cmp_byctrl_find(void *av, void *bv)
{
    union control *a = (union control *)av;
    union macctrl *b = (union macctrl *)bv;

    if (a < b->generic.ctrl)
	return -1;
    else if (a > b->generic.ctrl)
	return +1;
    else
	return 0;
}

void macctrl_layoutbox(struct controlbox *cb, WindowPtr window,
		       struct macctrls *mcs)
{
    int i;
    struct mac_layoutstate curstate;
    ControlRef root;
    Rect rect;

    macctrl_init();
#if TARGET_API_MAC_CARBON
    GetPortBounds(GetWindowPort(window), &rect);
#else
    rect = window->portRect;
#endif
    curstate.pos.h = rect.left + 13;
    curstate.pos.v = rect.bottom - 33;
    curstate.width = rect.right - rect.left - (13 * 2);
    if (mac_gestalts.apprvers >= 0x100)
	CreateRootControl(window, &root);
    mcs->window = window;
    mcs->byctrl = newtree234(macctrl_cmp_byctrl);
    mcs->focus = NULL;
    /* Count the number of panels */
    mcs->npanels = 1;
    for (i = 1; i < cb->nctrlsets; i++)
	if (strcmp(cb->ctrlsets[i]->pathname, cb->ctrlsets[i-1]->pathname))
	    mcs->npanels++;
    mcs->panels = snewn(mcs->npanels, union macctrl *);
    memset(mcs->panels, 0, sizeof(*mcs->panels) * mcs->npanels);
    curstate.panelnum = 0;
    for (i = 0; i < cb->nctrlsets; i++) {
	if (i > 0 && strcmp(cb->ctrlsets[i]->pathname,
			    cb->ctrlsets[i-1]->pathname)) {
	    curstate.pos.v = rect.top + 13;
	    curstate.panelnum++;
	    assert(curstate.panelnum < mcs->npanels);
	}
	macctrl_layoutset(&curstate, cb->ctrlsets[i], window, mcs);
    }
    macctrl_switchtopanel(mcs, 1);
    /* 14 = proxies, 20 = SSH bugs */
}

#define MAXCOLS 16

static void macctrl_layoutset(struct mac_layoutstate *curstate,
			      struct controlset *s,
			      WindowPtr window, struct macctrls *mcs)
{
    unsigned int i, j, ncols, colstart;
    struct mac_layoutstate cols[MAXCOLS];

    fprintf(stderr, "--- begin set ---\n");
    fprintf(stderr, "pathname = %s\n", s->pathname);
    if (s->boxname && *s->boxname)
	fprintf(stderr, "boxname = %s\n", s->boxname);
    if (s->boxtitle)
	fprintf(stderr, "boxtitle = %s\n", s->boxtitle);

    cols[0] = *curstate;
    ncols = 1;

    for (i = 0; i < s->ncontrols; i++) {
	union control *ctrl = s->ctrls[i];
	char const *s;

	colstart = COLUMN_START(ctrl->generic.column);
	switch (ctrl->generic.type) {
	  case CTRL_TEXT: s = "text"; break;
	  case CTRL_EDITBOX: s = "editbox"; break;
	  case CTRL_RADIO: s = "radio"; break;
	  case CTRL_CHECKBOX: s = "checkbox"; break;
	  case CTRL_BUTTON: s = "button"; break;
	  case CTRL_LISTBOX: s = "listbox"; break;
	  case CTRL_COLUMNS: s = "columns"; break;
	  case CTRL_FILESELECT: s = "fileselect"; break;
	  case CTRL_FONTSELECT: s = "fontselect"; break;
	  case CTRL_TABDELAY: s = "tabdelay"; break;
	  default: s = "unknown"; break;
	}
	fprintf(stderr, "  control: %s\n", s);
	switch (ctrl->generic.type) {
	  case CTRL_COLUMNS:
	    if (ctrl->columns.ncols != 1) {
		ncols = ctrl->columns.ncols;
		fprintf(stderr, "  split to %d\n", ncols);
		assert(ncols <= MAXCOLS);
		for (j = 0; j < ncols; j++) {
		    cols[j] = cols[0];
 		    if (j > 0)
			cols[j].pos.h = cols[j-1].pos.h + cols[j-1].width + 6;
		    if (j == ncols - 1)
			cols[j].width = curstate->width -
			    (cols[j].pos.h - curstate->pos.h);
		    else
			cols[j].width = (curstate->width + 6) * 
			    ctrl->columns.percentages[j] / 100 - 6;
		}
	    } else {
		fprintf(stderr, "  join\n");
		for (j = 0; j < ncols; j++)
		    if (cols[j].pos.v > cols[0].pos.v)
			cols[0].pos.v = cols[j].pos.v;
		cols[0].width = curstate->width;
		ncols = 1;
	    }
	    break;
	  case CTRL_TEXT:
	    macctrl_text(mcs, window, &cols[colstart], ctrl);
	    break;
	  case CTRL_EDITBOX:
	    macctrl_editbox(mcs, window, &cols[colstart], ctrl);
	    break;
	  case CTRL_RADIO:
	    macctrl_radio(mcs, window, &cols[colstart], ctrl);
	    break;
	  case CTRL_CHECKBOX:
	    macctrl_checkbox(mcs, window, &cols[colstart], ctrl);
	    break;
	  case CTRL_BUTTON:
	    macctrl_button(mcs, window, &cols[colstart], ctrl);
	    break;
	  case CTRL_LISTBOX:
	    if (ctrl->listbox.height == 0)
		macctrl_popup(mcs, window, &cols[colstart], ctrl);
	    break;
	}
    }
    for (j = 0; j < ncols; j++)
	if (cols[j].pos.v > curstate->pos.v)
	    curstate->pos.v = cols[j].pos.v;
}

static void macctrl_switchtopanel(struct macctrls *mcs, unsigned int which)
{
    unsigned int i, j;
    union macctrl *mc;

#define hideshow(c) do {						\
    if (i == which) ShowControl(c); else HideControl(c);		\
} while (0)

    mcs->curpanel = which;
    /* Panel 0 is special and always visible. */
    for (i = 1; i < mcs->npanels; i++)
	for (mc = mcs->panels[i]; mc != NULL; mc = mc->generic.next) {
#if !TARGET_API_MAC_CARBON
	    if (mcs->focus == mc)
		macctrl_setfocus(mcs, NULL);
#endif
	    switch (mc->generic.type) {
	      case MACCTRL_TEXT:
		hideshow(mc->text.tbctrl);
		break;
	      case MACCTRL_EDITBOX:
		hideshow(mc->editbox.tbctrl);
		hideshow(mc->editbox.tblabel);
		break;
	      case MACCTRL_RADIO:
		for (j = 0; j < mc->generic.ctrl->radio.nbuttons; j++)
		    hideshow(mc->radio.tbctrls[j]);
		hideshow(mc->radio.tblabel);
		break;
	      case MACCTRL_CHECKBOX:
		hideshow(mc->checkbox.tbctrl);
		break;
	      case MACCTRL_BUTTON:
		hideshow(mc->button.tbctrl);
		break;
	      case MACCTRL_POPUP:
		hideshow(mc->popup.tbctrl);
		break;
	    }
	}
}

#if !TARGET_API_MAC_CARBON
/*
 * System 7 focus manipulation
 */
static void macctrl_defocus(union macctrl *mc)
{

    assert(mac_gestalts.apprvers < 0x100);
    switch (mc->generic.type) {
      case MACCTRL_EDITBOX:
	TEDeactivate((TEHandle)(*mc->editbox.tbctrl)->contrlData);
	break;
    }
}

static void macctrl_enfocus(union macctrl *mc)
{

    assert(mac_gestalts.apprvers < 0x100);
    switch (mc->generic.type) {
      case MACCTRL_EDITBOX:
	TEActivate((TEHandle)(*mc->editbox.tbctrl)->contrlData);
	break;
    }
}

static void macctrl_setfocus(struct macctrls *mcs, union macctrl *mc)
{

    if (mcs->focus != NULL)
	macctrl_defocus(mcs->focus);
    mcs->focus = mc;
    if (mc != NULL)
	macctrl_enfocus(mc);
}
#endif

static void macctrl_text(struct macctrls *mcs, WindowPtr window,
			 struct mac_layoutstate *curstate,
			 union control *ctrl)
{
    union macctrl *mc = snew(union macctrl);
    Rect bounds;
    SInt16 height;

    fprintf(stderr, "    label = %s\n", ctrl->text.label);
    mc->generic.type = MACCTRL_TEXT;
    mc->generic.ctrl = ctrl;
    mc->generic.privdata = NULL;
    bounds.left = curstate->pos.h;
    bounds.right = bounds.left + curstate->width;
    bounds.top = curstate->pos.v;
    bounds.bottom = bounds.top + 16;
    if (mac_gestalts.apprvers >= 0x100) {
	Size olen;

	mc->text.tbctrl = NewControl(window, &bounds, NULL, TRUE, 0, 0, 0,
				     kControlStaticTextProc, (long)mc);
	SetControlData(mc->text.tbctrl, kControlEntireControl,
		       kControlStaticTextTextTag,
		       strlen(ctrl->text.label), ctrl->text.label);
	GetControlData(mc->text.tbctrl, kControlEntireControl,
		       kControlStaticTextTextHeightTag,
		       sizeof(height), &height, &olen);
    }
#if !TARGET_API_MAC_CARBON
    else {
	TEHandle te;

	mc->text.tbctrl = NewControl(window, &bounds, NULL, TRUE, 0, 0, 0,
				     SYS7_TEXT_PROC, (long)mc);
	te = (TEHandle)(*mc->text.tbctrl)->contrlData;
	TESetText(ctrl->text.label, strlen(ctrl->text.label), te);
	height = TEGetHeight(1, (*te)->nLines, te);
    }
#endif
    fprintf(stderr, "    height = %d\n", height);
    SizeControl(mc->text.tbctrl, curstate->width, height);
    curstate->pos.v += height + 6;
    add234(mcs->byctrl, mc);
    mc->generic.next = mcs->panels[curstate->panelnum];
    mcs->panels[curstate->panelnum] = mc;
}

static void macctrl_editbox(struct macctrls *mcs, WindowPtr window,
			    struct mac_layoutstate *curstate,
			    union control *ctrl)
{
    union macctrl *mc = snew(union macctrl);
    Rect lbounds, bounds;

    fprintf(stderr, "    label = %s\n", ctrl->editbox.label);
    fprintf(stderr, "    percentwidth = %d\n", ctrl->editbox.percentwidth);
    if (ctrl->editbox.password) fprintf(stderr, "    password\n");
    if (ctrl->editbox.has_list) fprintf(stderr, "    has list\n");
    mc->generic.type = MACCTRL_EDITBOX;
    mc->generic.ctrl = ctrl;
    mc->generic.privdata = NULL;
    lbounds.left = curstate->pos.h;
    lbounds.top = curstate->pos.v;
    if (ctrl->editbox.percentwidth == 100) {
	lbounds.right = lbounds.left + curstate->width;
	lbounds.bottom = lbounds.top + 16;
	bounds.left = curstate->pos.h;
	bounds.right = bounds.left + curstate->width;
	curstate->pos.v += 18;
    } else {
	lbounds.right = lbounds.left +
	    curstate->width * (100 - ctrl->editbox.percentwidth) / 100;
	lbounds.bottom = lbounds.top + 22;
	bounds.left = lbounds.right;
	bounds.right = lbounds.left + curstate->width;
    }
    bounds.top = curstate->pos.v;
    bounds.bottom = bounds.top + 22;
    if (mac_gestalts.apprvers >= 0x100) {
	mc->editbox.tblabel = NewControl(window, &lbounds, NULL, TRUE, 0, 0, 0,
					 kControlStaticTextProc, (long)mc);
	SetControlData(mc->editbox.tblabel, kControlEntireControl,
		       kControlStaticTextTextTag,
		       strlen(ctrl->editbox.label), ctrl->editbox.label);
	InsetRect(&bounds, 3, 3);
	mc->editbox.tbctrl = NewControl(window, &bounds, NULL, TRUE, 0, 0, 0,
					ctrl->editbox.password ?
					kControlEditTextPasswordProc :
					kControlEditTextProc, (long)mc);
    }
#if !TARGET_API_MAC_CARBON
    else {
	mc->editbox.tblabel = NewControl(window, &lbounds, NULL, TRUE,
					 0, 0, 0, SYS7_TEXT_PROC, (long)mc);
	TESetText(ctrl->editbox.label, strlen(ctrl->editbox.label),
		  (TEHandle)(*mc->editbox.tblabel)->contrlData);
	mc->editbox.tbctrl = NewControl(window, &bounds, NULL, TRUE, 0, 0, 0,
					SYS7_EDITBOX_PROC, (long)mc);
    }
#endif
    curstate->pos.v += 28;
    add234(mcs->byctrl, mc);
    mc->generic.next = mcs->panels[curstate->panelnum];
    mcs->panels[curstate->panelnum] = mc;
    ctrlevent(mcs, mc, EVENT_REFRESH);
}

#if !TARGET_API_MAC_CARBON
static pascal SInt32 macctrl_sys7_editbox_cdef(SInt16 variant,
					       ControlRef control,
					       ControlDefProcMessage msg,
					       SInt32 param)
{
    RgnHandle rgn;
    Rect rect;
    TEHandle te;
    long ssfs;
    Point mouse;

    switch (msg) {
      case initCntl:
	rect = (*control)->contrlRect;
	if (variant == SYS7_EDITBOX_VARIANT)
	    InsetRect(&rect, 3, 3); /* 2 if it's 20 pixels high */
	te = TENew(&rect, &rect);
	ssfs = GetScriptVariable(smSystemScript, smScriptSysFondSize);
	(*te)->txSize = LoWord(ssfs);
	(*te)->txFont = HiWord(ssfs);
	(*control)->contrlData = (Handle)te;
	return noErr;
      case dispCntl:
	TEDispose((TEHandle)(*control)->contrlData);
	return 0;
      case drawCntl:
	if ((*control)->contrlVis) {
	    rect = (*control)->contrlRect;
	    if (variant == SYS7_EDITBOX_VARIANT) {
		PenNormal();
		FrameRect(&rect);
		InsetRect(&rect, 3, 3);
	    }
	    (*(TEHandle)(*control)->contrlData)->viewRect = rect;
	    TEUpdate(&rect, (TEHandle)(*control)->contrlData);
	}
	return 0;
      case testCntl:
	if (variant == SYS7_TEXT_VARIANT)
	    return kControlNoPart;
	mouse.h = LoWord(param);
	mouse.v = HiWord(param);
	rect = (*control)->contrlRect;
	InsetRect(&rect, 3, 3);
	return PtInRect(mouse, &rect) ? kControlEditTextPart : kControlNoPart;
      case calcCRgns:
	if (param & (1 << 31)) {
	    param &= ~(1 << 31);
	    goto calcthumbrgn;
	}
	/* FALLTHROUGH */
      case calcCntlRgn:
	rgn = (RgnHandle)param;
	RectRgn(rgn, &(*control)->contrlRect);
	return 0;
      case calcThumbRgn:
      calcthumbrgn:
	rgn = (RgnHandle)param;
	SetEmptyRgn(rgn);
	return 0;
    }

    return 0;
}
#endif

static void macctrl_radio(struct macctrls *mcs, WindowPtr window,
			  struct mac_layoutstate *curstate,
			  union control *ctrl)
{
    union macctrl *mc = snew(union macctrl);
    Rect bounds;
    Str255 title;
    unsigned int i, colwidth;

    fprintf(stderr, "    label = %s\n", ctrl->radio.label);
    mc->generic.type = MACCTRL_RADIO;
    mc->generic.ctrl = ctrl;
    mc->generic.privdata = NULL;
    mc->radio.tbctrls = snewn(ctrl->radio.nbuttons, ControlRef);
    colwidth = (curstate->width + 13) /	ctrl->radio.ncolumns;
    bounds.top = curstate->pos.v;
    bounds.bottom = bounds.top + 16;
    bounds.left = curstate->pos.h;
    bounds.right = bounds.left + curstate->width;
    if (mac_gestalts.apprvers >= 0x100) {
	mc->radio.tblabel = NewControl(window, &bounds, NULL, TRUE, 0, 0, 0,
				       kControlStaticTextProc, (long)mc);
	SetControlData(mc->radio.tblabel, kControlEntireControl,
		       kControlStaticTextTextTag,
		       strlen(ctrl->radio.label), ctrl->radio.label);
    }
 #if !TARGET_API_MAC_CARBON
    else {
	mc->radio.tblabel = NewControl(window, &bounds, NULL, TRUE,
					 0, 0, 0, SYS7_TEXT_PROC, (long)mc);
	TESetText(ctrl->radio.label, strlen(ctrl->radio.label),
		  (TEHandle)(*mc->radio.tblabel)->contrlData);
    }
#endif
    curstate->pos.v += 18;
    for (i = 0; i < ctrl->radio.nbuttons; i++) {
	fprintf(stderr, "    button = %s\n", ctrl->radio.buttons[i]);
	bounds.top = curstate->pos.v - 2;
	bounds.bottom = bounds.top + 18;
	bounds.left = curstate->pos.h + colwidth * (i % ctrl->radio.ncolumns);
	if (i == ctrl->radio.nbuttons - 1 ||
	    i % ctrl->radio.ncolumns == ctrl->radio.ncolumns - 1) {
	    bounds.right = curstate->pos.h + curstate->width;
	    curstate->pos.v += 18;
	} else
	    bounds.right = bounds.left + colwidth - 13;
	c2pstrcpy(title, ctrl->radio.buttons[i]);
	mc->radio.tbctrls[i] = NewControl(window, &bounds, title, TRUE,
					  0, 0, 1, radioButProc, (long)mc);
    }
    curstate->pos.v += 4;
    add234(mcs->byctrl, mc);
    mc->generic.next = mcs->panels[curstate->panelnum];
    mcs->panels[curstate->panelnum] = mc;
    ctrlevent(mcs, mc, EVENT_REFRESH);
}

static void macctrl_checkbox(struct macctrls *mcs, WindowPtr window,
			     struct mac_layoutstate *curstate,
			     union control *ctrl)
{
    union macctrl *mc = snew(union macctrl);
    Rect bounds;
    Str255 title;

    fprintf(stderr, "    label = %s\n", ctrl->checkbox.label);
    mc->generic.type = MACCTRL_CHECKBOX;
    mc->generic.ctrl = ctrl;
    mc->generic.privdata = NULL;
    bounds.left = curstate->pos.h;
    bounds.right = bounds.left + curstate->width;
    bounds.top = curstate->pos.v;
    bounds.bottom = bounds.top + 16;
    c2pstrcpy(title, ctrl->checkbox.label);
    mc->checkbox.tbctrl = NewControl(window, &bounds, title, TRUE, 0, 0, 1,
				     checkBoxProc, (long)mc);
    add234(mcs->byctrl, mc);
    curstate->pos.v += 22;
    mc->generic.next = mcs->panels[curstate->panelnum];
    mcs->panels[curstate->panelnum] = mc;
    ctrlevent(mcs, mc, EVENT_REFRESH);
}

static void macctrl_button(struct macctrls *mcs, WindowPtr window,
			   struct mac_layoutstate *curstate,
			   union control *ctrl)
{
    union macctrl *mc = snew(union macctrl);
    Rect bounds;
    Str255 title;

    fprintf(stderr, "    label = %s\n", ctrl->button.label);
    if (ctrl->button.isdefault)
	fprintf(stderr, "    is default\n");
    mc->generic.type = MACCTRL_BUTTON;
    mc->generic.ctrl = ctrl;
    mc->generic.privdata = NULL;
    bounds.left = curstate->pos.h;
    bounds.right = bounds.left + curstate->width;
    bounds.top = curstate->pos.v;
    bounds.bottom = bounds.top + 20;
    c2pstrcpy(title, ctrl->button.label);
    mc->button.tbctrl = NewControl(window, &bounds, title, TRUE, 0, 0, 1,
				   pushButProc, (long)mc);
    if (mac_gestalts.apprvers >= 0x100) {
	Boolean isdefault = ctrl->button.isdefault;

	SetControlData(mc->button.tbctrl, kControlEntireControl,
		       kControlPushButtonDefaultTag,
		       sizeof(isdefault), &isdefault);
    } else if (ctrl->button.isdefault) {
	InsetRect(&bounds, -4, -4);
	NewControl(window, &bounds, title, TRUE, 0, 0, 1,
		   SYS7_DEFAULT_PROC, (long)mc);
    }
    if (mac_gestalts.apprvers >= 0x110) {
	Boolean iscancel = ctrl->button.iscancel;

	SetControlData(mc->button.tbctrl, kControlEntireControl,
		       kControlPushButtonCancelTag,
		       sizeof(iscancel), &iscancel);
    }
    add234(mcs->byctrl, mc);
    mc->generic.next = mcs->panels[curstate->panelnum];
    mcs->panels[curstate->panelnum] = mc;
    curstate->pos.v += 26;
}

#if !TARGET_API_MAC_CARBON
static pascal SInt32 macctrl_sys7_default_cdef(SInt16 variant,
					       ControlRef control,
					       ControlDefProcMessage msg,
					       SInt32 param)
{
    RgnHandle rgn;
    Rect rect;
    int oval;

    switch (msg) {
      case drawCntl:
	if ((*control)->contrlVis) {
	    rect = (*control)->contrlRect;
	    PenNormal();
	    PenSize(3, 3);
	    oval = (rect.bottom - rect.top) / 2 + 2;
	    FrameRoundRect(&rect, oval, oval);
	}
	return 0;
      case calcCRgns:
	if (param & (1 << 31)) {
	    param &= ~(1 << 31);
	    goto calcthumbrgn;
	}
	/* FALLTHROUGH */
      case calcCntlRgn:
	rgn = (RgnHandle)param;
	RectRgn(rgn, &(*control)->contrlRect);
	return 0;
      case calcThumbRgn:
      calcthumbrgn:
	rgn = (RgnHandle)param;
	SetEmptyRgn(rgn);
	return 0;
    }

    return 0;
}
#endif

static void macctrl_popup(struct macctrls *mcs, WindowPtr window,
			  struct mac_layoutstate *curstate,
			  union control *ctrl)
{
    union macctrl *mc = snew(union macctrl);
    Rect bounds;
    Str255 title;
    unsigned int labelwidth;
    static int nextmenuid = MENU_MIN;
    int menuid;
    MenuRef menu;

    /* 
     * <http://developer.apple.com/qa/tb/tb42.html> explains how to
     * create a popup menu with dynamic content.
     */
    assert(ctrl->listbox.height == 0);
    assert(!ctrl->listbox.draglist);
    assert(!ctrl->listbox.multisel);

    fprintf(stderr, "    label = %s\n", ctrl->listbox.label);
    fprintf(stderr, "    percentwidth = %d\n", ctrl->listbox.percentwidth);

    mc->generic.type = MACCTRL_POPUP;
    mc->generic.ctrl = ctrl;
    mc->generic.privdata = NULL;
    c2pstrcpy(title, ctrl->button.label);

    /* Find a spare menu ID and create the menu */
    while (GetMenuHandle(nextmenuid) != NULL)
	if (++nextmenuid >= MENU_MAX) nextmenuid = MENU_MIN;
    menuid = nextmenuid++;
    menu = NewMenu(menuid, "\pdummy");
    if (menu == NULL) return;
    mc->popup.menu = menu;
    mc->popup.menuid = menuid;
    InsertMenu(menu, kInsertHierarchicalMenu);

    /* The menu starts off empty */
    mc->popup.nids = 0;
    mc->popup.ids = NULL;

    bounds.left = curstate->pos.h;
    bounds.right = bounds.left + curstate->width;
    bounds.top = curstate->pos.v;
    bounds.bottom = bounds.top + 20;
    /* XXX handle percentwidth == 100 */
    labelwidth = curstate->width * (100 - ctrl->listbox.percentwidth) / 100;
    mc->popup.tbctrl = NewControl(window, &bounds, title, TRUE,
				  popupTitleLeftJust, menuid, labelwidth,
				  popupMenuProc + popupFixedWidth, (long)mc);
    add234(mcs->byctrl, mc);
    curstate->pos.v += 26;
    mc->generic.next = mcs->panels[curstate->panelnum];
    mcs->panels[curstate->panelnum] = mc;
    ctrlevent(mcs, mc, EVENT_REFRESH);
}


void macctrl_activate(WindowPtr window, EventRecord *event)
{
    struct macctrls *mcs = mac_winctrls(window);
    Boolean active = (event->modifiers & activeFlag) != 0;
    GrafPtr saveport;
    int i, j;
    ControlPartCode state;
    union macctrl *mc;

    GetPort(&saveport);
    SetPort((GrafPtr)GetWindowPort(window));
    if (mac_gestalts.apprvers >= 0x100)
	SetThemeWindowBackground(window, active ?
				 kThemeBrushModelessDialogBackgroundActive :
				 kThemeBrushModelessDialogBackgroundInactive,
				 TRUE);
    state = active ? kControlNoPart : kControlInactivePart;
    for (i = 0; i <= mcs->curpanel; i += mcs->curpanel)
	for (mc = mcs->panels[i]; mc != NULL; mc = mc->generic.next) {
	    switch (mc->generic.type) {
	      case MACCTRL_TEXT:
		HiliteControl(mc->text.tbctrl, state);
		break;
	      case MACCTRL_EDITBOX:
		HiliteControl(mc->editbox.tbctrl, state);
		HiliteControl(mc->editbox.tblabel, state);
		break;
	      case MACCTRL_RADIO:
		for (j = 0; j < mc->generic.ctrl->radio.nbuttons; j++)
		    HiliteControl(mc->radio.tbctrls[j], state);
		HiliteControl(mc->radio.tblabel, state);
		break;
	      case MACCTRL_CHECKBOX:
		HiliteControl(mc->checkbox.tbctrl, state);
		break;
	      case MACCTRL_BUTTON:
		HiliteControl(mc->button.tbctrl, state);
		break;
	      case MACCTRL_POPUP:
		HiliteControl(mc->popup.tbctrl, state);
		break;
	    }
#if !TARGET_API_MAC_CARBON
	    if (mcs->focus == mc) {
		if (active)
		    macctrl_enfocus(mc);
		else
		    macctrl_defocus(mc);
	    }
#endif
	}
    SetPort(saveport);
}

void macctrl_click(WindowPtr window, EventRecord *event)
{
    Point mouse;
    ControlHandle control;
    int part, trackresult;
    GrafPtr saveport;
    union macctrl *mc;
    struct macctrls *mcs = mac_winctrls(window);
    int i;
    UInt32 features;

    GetPort(&saveport);
    SetPort((GrafPtr)GetWindowPort(window));
    mouse = event->where;
    GlobalToLocal(&mouse);
    part = FindControl(mouse, window, &control);
    if (control != NULL) {
	mc = (union macctrl *)GetControlReference(control);
	if (mac_gestalts.apprvers >= 0x100) {
	    if (GetControlFeatures(control, &features) == noErr &&
		(features & kControlSupportsFocus) &&
		(features & kControlGetsFocusOnClick))
		SetKeyboardFocus(window, control, part);
	    trackresult = HandleControlClick(control, mouse, event->modifiers,
					     (ControlActionUPP)-1);
	} else {
#if !TARGET_API_MAC_CARBON
	    if (mc->generic.type == MACCTRL_EDITBOX &&
		control == mc->editbox.tbctrl) {
		TEHandle te = (TEHandle)(*control)->contrlData;

		macctrl_setfocus(mcs, mc);
		TEClick(mouse, !!(event->modifiers & shiftKey), te);
		goto done;
	    }
#endif
	    trackresult = TrackControl(control, mouse, (ControlActionUPP)-1);
	}
	switch (mc->generic.type) {
	  case MACCTRL_RADIO:
	    if (trackresult != 0) {
		for (i = 0; i < mc->generic.ctrl->radio.nbuttons; i++)
		    if (mc->radio.tbctrls[i] == control)
			SetControlValue(mc->radio.tbctrls[i],
					kControlRadioButtonCheckedValue);
		    else
			SetControlValue(mc->radio.tbctrls[i],
					kControlRadioButtonUncheckedValue);
		ctrlevent(mcs, mc, EVENT_VALCHANGE);
	    }
	    break;
	  case MACCTRL_CHECKBOX:
	    if (trackresult != 0) {
		SetControlValue(control, !GetControlValue(control));
		ctrlevent(mcs, mc, EVENT_VALCHANGE);
	    }
	    break;
	  case MACCTRL_BUTTON:
	    if (trackresult != 0)
		ctrlevent(mcs, mc, EVENT_ACTION);
	    break;
	  case MACCTRL_POPUP:
	    ctrlevent(mcs, mc, EVENT_SELCHANGE);
	    break;
	}
    }
  done:
    SetPort(saveport);
}

void macctrl_key(WindowPtr window, EventRecord *event)
{
    ControlRef control;
    struct macctrls *mcs = mac_winctrls(window);
    union macctrl *mc;

    if (mac_gestalts.apprvers >= 0x100) {
	if (GetKeyboardFocus(window, &control) == noErr && control != NULL) {
	    HandleControlKey(control, (event->message & keyCodeMask) >> 8,
			     event->message & charCodeMask, event->modifiers);
	    mc = (union macctrl *)GetControlReference(control);
	    ctrlevent(mcs, mc, EVENT_VALCHANGE);
	}
    }
#if !TARGET_API_MAC_CARBON
    else {
	TEHandle te;

	if (mcs->focus != NULL) {
	    switch (mcs->focus->generic.type) {
	      case MACCTRL_EDITBOX:
		te = (TEHandle)(*mcs->focus->editbox.tbctrl)->contrlData;
		TEKey(event->message & charCodeMask, te);
		break;
	    }
	}
    }
#endif
}

void macctrl_update(WindowPtr window)
{
#if TARGET_API_MAC_CARBON
    RgnHandle visrgn;
#endif
    Rect rect;
    GrafPtr saveport;

    BeginUpdate(window);
    GetPort(&saveport);
    SetPort((GrafPtr)GetWindowPort(window));
    if (mac_gestalts.apprvers >= 0x101) {
#if TARGET_API_MAC_CARBON
	GetPortBounds(GetWindowPort(window), &rect);
#else
	rect = window->portRect;
#endif
	InsetRect(&rect, -1, -1);
	DrawThemeModelessDialogFrame(&rect, mac_frontwindow() == window ?
				     kThemeStateActive : kThemeStateInactive);
    }
#if TARGET_API_MAC_CARBON
    visrgn = NewRgn();
    GetPortVisibleRegion(GetWindowPort(window), visrgn);
    UpdateControls(window, visrgn);
    DisposeRgn(visrgn);
#else
    UpdateControls(window, window->visRgn);
#endif
    SetPort(saveport);
    EndUpdate(window);
}

#if TARGET_API_MAC_CARBON
#define EnableItem EnableMenuItem
#define DisableItem DisableMenuItem
#endif
void macctrl_adjustmenus(WindowPtr window)
{
    MenuHandle menu;

    menu = GetMenuHandle(mFile);
    DisableItem(menu, iSave); /* XXX enable if modified */
    EnableItem(menu, iSaveAs);
    EnableItem(menu, iDuplicate);

    menu = GetMenuHandle(mEdit);
    DisableItem(menu, 0);
}

void macctrl_close(WindowPtr window)
{
    struct macctrls *mcs = mac_winctrls(window);
    union macctrl *mc;

    /*
     * Mostly, we don't bother disposing of the Toolbox controls,
     * since that will happen automatically when the window is
     * disposed of.  Popup menus are an exception, because we have to
     * dispose of the menu ourselves, and doing that while the control
     * still holds a reference to it seems rude.
     */
    while ((mc = index234(mcs->byctrl, 0)) != NULL) {
	if (mc->generic.privdata != NULL && mc->generic.freeprivdata)
	    sfree(mc->generic.privdata);
	switch (mc->generic.type) {
	  case MACCTRL_POPUP:
	    DisposeControl(mc->popup.tbctrl);
	    DeleteMenu(mc->popup.menuid);
	    DisposeMenu(mc->popup.menu);
	    break;
	}
	del234(mcs->byctrl, mc);
	sfree(mc);
    }

    freetree234(mcs->byctrl);
    mcs->byctrl = NULL;
    sfree(mcs->panels);
    mcs->panels = NULL;
}

void dlg_update_start(union control *ctrl, void *dlg)
{

    /* No-op for now */
}

void dlg_update_done(union control *ctrl, void *dlg)
{

    /* No-op for now */
}

void dlg_set_focus(union control *ctrl, void *dlg)
{

    if (mac_gestalts.apprvers >= 0x100) {
	/* Use SetKeyboardFocus() */
    } else {
	/* Do our own mucking around */
    }
}

union control *dlg_last_focused(union control *ctrl, void *dlg)
{

    return NULL;
}

void dlg_beep(void *dlg)
{

    SysBeep(30);
}

void dlg_error_msg(void *dlg, char *msg)
{
    Str255 pmsg;

    c2pstrcpy(pmsg, msg);
    ParamText(pmsg, NULL, NULL, NULL);
    StopAlert(128, NULL);
}

void dlg_end(void *dlg, int value)
{

};

void dlg_refresh(union control *ctrl, void *dlg)
{
    struct macctrls *mcs = dlg;
    union macctrl *mc;

    if (ctrl == NULL)
	return; /* FIXME */
    mc = findbyctrl(mcs, ctrl);
    assert(mc != NULL);
    ctrlevent(mcs, mc, EVENT_REFRESH);
};

void *dlg_get_privdata(union control *ctrl, void *dlg)
{
    struct macctrls *mcs = dlg;
    union macctrl *mc = findbyctrl(mcs, ctrl);

    assert(mc != NULL);
    return mc->generic.privdata;
}

void dlg_set_privdata(union control *ctrl, void *dlg, void *ptr)
{
    struct macctrls *mcs = dlg;
    union macctrl *mc = findbyctrl(mcs, ctrl);

    assert(mc != NULL);
    mc->generic.privdata = ptr;
    mc->generic.freeprivdata = FALSE;
}

void *dlg_alloc_privdata(union control *ctrl, void *dlg, size_t size)
{
    struct macctrls *mcs = dlg;
    union macctrl *mc = findbyctrl(mcs, ctrl);

    assert(mc != NULL);
    mc->generic.privdata = smalloc(size);
    mc->generic.freeprivdata = TRUE;
    return mc->generic.privdata;
}


/*
 * Radio Button control
 */

void dlg_radiobutton_set(union control *ctrl, void *dlg, int whichbutton)
{
    struct macctrls *mcs = dlg;
    union macctrl *mc = findbyctrl(mcs, ctrl);
    int i;

    assert(mc != NULL);
    for (i = 0; i < ctrl->radio.nbuttons; i++) {
	if (i == whichbutton)
	    SetControlValue(mc->radio.tbctrls[i],
			    kControlRadioButtonCheckedValue);
	else
	    SetControlValue(mc->radio.tbctrls[i],
			    kControlRadioButtonUncheckedValue);
    }

};

int dlg_radiobutton_get(union control *ctrl, void *dlg)
{
    struct macctrls *mcs = dlg;
    union macctrl *mc = findbyctrl(mcs, ctrl);
    int i;

    assert(mc != NULL);
    for (i = 0; i < ctrl->radio.nbuttons; i++) {
	if (GetControlValue(mc->radio.tbctrls[i])  ==
	    kControlRadioButtonCheckedValue)
	    return i;
    }
    return -1;
};


/*
 * Check Box control
 */

void dlg_checkbox_set(union control *ctrl, void *dlg, int checked)
{
    struct macctrls *mcs = dlg;
    union macctrl *mc = findbyctrl(mcs, ctrl);

    assert(mc != NULL);
    SetControlValue(mc->checkbox.tbctrl,
		    checked ? kControlCheckBoxCheckedValue :
		              kControlCheckBoxUncheckedValue);
}

int dlg_checkbox_get(union control *ctrl, void *dlg)
{
    struct macctrls *mcs = dlg;
    union macctrl *mc = findbyctrl(mcs, ctrl);

    assert(mc != NULL);
    return GetControlValue(mc->checkbox.tbctrl);
}


/*
 * Edit Box control
 */

void dlg_editbox_set(union control *ctrl, void *dlg, char const *text)
{
    struct macctrls *mcs = dlg;
    union macctrl *mc = findbyctrl(mcs, ctrl);
    GrafPtr saveport;

    assert(mc != NULL);
    assert(mc->generic.type == MACCTRL_EDITBOX);
    GetPort(&saveport);
    SetPort((GrafPtr)(GetWindowPort(mcs->window)));
    if (mac_gestalts.apprvers >= 0x100)
	SetControlData(mc->editbox.tbctrl, kControlEntireControl,
		       ctrl->editbox.password ?
		       kControlEditTextPasswordTag :
		       kControlEditTextTextTag,
		       strlen(text), text);
#if !TARGET_API_MAC_CARBON
    else
	TESetText(text, strlen(text),
		  (TEHandle)(*mc->editbox.tbctrl)->contrlData);
#endif
    DrawOneControl(mc->editbox.tbctrl);
    SetPort(saveport);
}

void dlg_editbox_get(union control *ctrl, void *dlg, char *buffer, int length)
{
    struct macctrls *mcs = dlg;
    union macctrl *mc = findbyctrl(mcs, ctrl);
    Size olen;

    assert(mc != NULL);
    assert(mc->generic.type == MACCTRL_EDITBOX);
    if (mac_gestalts.apprvers >= 0x100) {
	if (GetControlData(mc->editbox.tbctrl, kControlEntireControl,
			   ctrl->editbox.password ?
			   kControlEditTextPasswordTag :
			   kControlEditTextTextTag,
			   length - 1, buffer, &olen) != noErr)
	    olen = 0;
	if (olen > length - 1)
	    olen = length - 1;
    }
#if !TARGET_API_MAC_CARBON
    else {
	TEHandle te = (TEHandle)(*mc->editbox.tbctrl)->contrlData;

	olen = (*te)->teLength;
	if (olen > length - 1)
	    olen = length - 1;
	memcpy(buffer, *(*te)->hText, olen);
    }
#endif
    buffer[olen] = '\0';
    fprintf(stderr, "dlg_editbox_get: %s\n", buffer);
}


/*
 * List Box control
 */

static void dlg_macpopup_clear(union control *ctrl, void *dlg)
{
    struct macctrls *mcs = dlg;
    union macctrl *mc = findbyctrl(mcs, ctrl);
    MenuRef menu = mc->popup.menu;
    unsigned int i, n;

    fprintf(stderr, "      popup_clear\n");
    n = CountMenuItems(menu);
    for (i = 0; i < n; i++)
	DeleteMenuItem(menu, n - i);
    mc->popup.nids = 0;
    sfree(mc->popup.ids);
    mc->popup.ids = NULL;
    SetControlMaximum(mc->popup.tbctrl, CountMenuItems(menu));
}

void dlg_listbox_clear(union control *ctrl, void *dlg)
{

    if (ctrl->listbox.height == 0)
	dlg_macpopup_clear(ctrl, dlg);
}

static void dlg_macpopup_del(union control *ctrl, void *dlg, int index)
{
    struct macctrls *mcs = dlg;
    union macctrl *mc = findbyctrl(mcs, ctrl);
    MenuRef menu = mc->popup.menu;

    fprintf(stderr, "      popup_del %d\n", index);
    DeleteMenuItem(menu, index + 1);
    if (mc->popup.ids != NULL)
	memcpy(mc->popup.ids + index, mc->popup.ids + index + 1,
	       (mc->popup.nids - index - 1) * sizeof(*mc->popup.ids));
    SetControlMaximum(mc->popup.tbctrl, CountMenuItems(menu));
}

void dlg_listbox_del(union control *ctrl, void *dlg, int index)
{

    if (ctrl->listbox.height == 0)
	dlg_macpopup_del(ctrl, dlg, index);
}

static void dlg_macpopup_add(union control *ctrl, void *dlg, char const *text)
{
    struct macctrls *mcs = dlg;
    union macctrl *mc = findbyctrl(mcs, ctrl);
    MenuRef menu = mc->popup.menu;
    Str255 itemstring;

    fprintf(stderr, "      popup_add %s\n", text);
    assert(text[0] != '\0');
    c2pstrcpy(itemstring, text);
    AppendMenu(menu, "\pdummy");
    SetMenuItemText(menu, CountMenuItems(menu), itemstring);
    SetControlMaximum(mc->popup.tbctrl, CountMenuItems(menu));
}

void dlg_listbox_add(union control *ctrl, void *dlg, char const *text)
{

    if (ctrl->listbox.height == 0)
	dlg_macpopup_add(ctrl, dlg, text);
}

static void dlg_macpopup_addwithid(union control *ctrl, void *dlg,
				   char const *text, int id)
{
    struct macctrls *mcs = dlg;
    union macctrl *mc = findbyctrl(mcs, ctrl);
    MenuRef menu = mc->popup.menu;
    unsigned int index;

    fprintf(stderr, "      popup_addwthindex %s, %d\n", text, id);
    dlg_macpopup_add(ctrl, dlg, text);
    index = CountMenuItems(menu) - 1;
    if (mc->popup.nids <= index) {
	mc->popup.nids = index + 1;
	mc->popup.ids = sresize(mc->popup.ids, mc->popup.nids, int);
    }
    mc->popup.ids[index] = id;
}

void dlg_listbox_addwithid(union control *ctrl, void *dlg,
			   char const *text, int id)
{

    if (ctrl->listbox.height == 0)
	dlg_macpopup_addwithid(ctrl, dlg, text, id);
}

int dlg_listbox_getid(union control *ctrl, void *dlg, int index)
{
    struct macctrls *mcs = dlg;
    union macctrl *mc = findbyctrl(mcs, ctrl);

    if (ctrl->listbox.height == 0) {
	assert(mc->popup.ids != NULL && mc->popup.nids > index);
	return mc->popup.ids[index];
    }
    return 0;
}

int dlg_listbox_index(union control *ctrl, void *dlg)
{
    struct macctrls *mcs = dlg;
    union macctrl *mc = findbyctrl(mcs, ctrl);

    if (ctrl->listbox.height == 0)
	return GetControlValue(mc->popup.tbctrl) - 1;
    return 0;
};

int dlg_listbox_issel(union control *ctrl, void *dlg, int index)
{
    struct macctrls *mcs = dlg;
    union macctrl *mc = findbyctrl(mcs, ctrl);

    if (ctrl->listbox.height == 0)
	return GetControlValue(mc->popup.tbctrl) - 1 == index;
    return 0;
};

void dlg_listbox_select(union control *ctrl, void *dlg, int index)
{
    struct macctrls *mcs = dlg;
    union macctrl *mc = findbyctrl(mcs, ctrl);

    if (ctrl->listbox.height == 0)
	SetControlValue(mc->popup.tbctrl, index + 1);
};


/*
 * Text control
 */

void dlg_text_set(union control *ctrl, void *dlg, char const *text)
{
    struct macctrls *mcs = dlg;
    union macctrl *mc = findbyctrl(mcs, ctrl);

    assert(mc != NULL);
    if (mac_gestalts.apprvers >= 0x100)
	SetControlData(mc->text.tbctrl, kControlEntireControl,
		       kControlStaticTextTextTag, strlen(text), text);
#if !TARGET_API_MAC_CARBON
    else
	TESetText(text, strlen(text),
		  (TEHandle)(*mc->text.tbctrl)->contrlData);
#endif
}


/*
 * File Selector control
 */

void dlg_filesel_set(union control *ctrl, void *dlg, Filename fn)
{

}

void dlg_filesel_get(union control *ctrl, void *dlg, Filename *fn)
{

}


/*
 * Font Selector control
 */

void dlg_fontsel_set(union control *ctrl, void *dlg, FontSpec fn)
{

}

void dlg_fontsel_get(union control *ctrl, void *dlg, FontSpec *fn)
{

}


/*
 * Printer enumeration
 */

printer_enum *printer_start_enum(int *nprinters)
{

    *nprinters = 0;
    return NULL;
}

char *printer_get_name(printer_enum *pe, int thing)
{

    return "<none>";
}

void printer_finish_enum(printer_enum *pe)
{

}


/*
 * Colour selection stuff
 */

void dlg_coloursel_start(union control *ctrl, void *dlg,
			 int r, int g, int b)
{

}

int dlg_coloursel_results(union control *ctrl, void *dlg,
			  int *r, int *g, int *b)
{

    return 0;
}

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */
