/* $Id: macctrls.c,v 1.6 2003/03/19 00:40:15 ben Exp $ */
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
#include <Resources.h>
#include <Sound.h>
#include <TextUtils.h>
#include <Windows.h>

#include "putty.h"
#include "mac.h"
#include "macresid.h"
#include "dialog.h"
#include "tree234.h"

union macctrl {
    struct macctrl_generic {
	enum {
	    MACCTRL_TEXT,
	    MACCTRL_RADIO,
	    MACCTRL_CHECKBOX,
	    MACCTRL_BUTTON
	} type;
	/* Template from which this was generated */
	union control *ctrl;
    } generic;
    struct {
	struct macctrl_generic generic;
	ControlRef tbctrl;
    } text;
    struct {
	struct macctrl_generic generic;
	ControlRef *tbctrls;
    } radio;
    struct {
	struct macctrl_generic generic;
	ControlRef tbctrl;
    } checkbox;
    struct {
	struct macctrl_generic generic;
	ControlRef tbctrl;
    } button;
};

struct mac_layoutstate {
    Point pos;
    unsigned int width;
};

#define ctrlevent(mcs, mc, event) do {					\
    if ((mc)->generic.ctrl->generic.handler != NULL)			\
	(*(mc)->generic.ctrl->generic.handler)((mc)->generic.ctrl, (mc),\
					       (mcs)->data, (event));	\
} while (0)

static void macctrl_layoutset(struct mac_layoutstate *, struct controlset *, 
			      WindowPtr, struct macctrls *);
static void macctrl_text(struct macctrls *, WindowPtr,
			 struct mac_layoutstate *, union control *);
static void macctrl_radio(struct macctrls *, WindowPtr,
			  struct mac_layoutstate *, union control *);
static void macctrl_checkbox(struct macctrls *, WindowPtr,
			     struct mac_layoutstate *, union control *);
static void macctrl_button(struct macctrls *, WindowPtr,
			   struct mac_layoutstate *, union control *);
#if !TARGET_API_MAC_CARBON
static pascal SInt32 macctrl_sys7_text_cdef(SInt16, ControlRef,
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
    cdef = (PatchCDEF)GetResource(kControlDefProcResourceType, CDEF_Text);
    (*cdef)->theUPP = NewControlDefProc(macctrl_sys7_text_cdef);
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
    curstate.pos.v = rect.top + 13;
    curstate.width = rect.right - rect.left - (13 * 2);
    if (mac_gestalts.apprvers >= 0x100)
	CreateRootControl(window, &root);
    mcs->byctrl = newtree234(macctrl_cmp_byctrl);
    for (i = 0; i < cb->nctrlsets; i++)
	macctrl_layoutset(&curstate, cb->ctrlsets[i], window, mcs);
}

static void macctrl_layoutset(struct mac_layoutstate *curstate,
			      struct controlset *s,
			      WindowPtr window, struct macctrls *mcs)
{
    unsigned int i;

    fprintf(stderr, "--- begin set ---\n");
    if (s->boxname && *s->boxname)
	fprintf(stderr, "boxname = %s\n", s->boxname);
    if (s->boxtitle)
	fprintf(stderr, "boxtitle = %s\n", s->boxtitle);


    for (i = 0; i < s->ncontrols; i++) {
	union control *ctrl = s->ctrls[i];
	char const *s;

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
	  case CTRL_TEXT:
	    macctrl_text(mcs, window, curstate, ctrl);
	    break;
	  case CTRL_RADIO:
	    macctrl_radio(mcs, window, curstate, ctrl);
	    break;
	  case CTRL_CHECKBOX:
	    macctrl_checkbox(mcs, window, curstate, ctrl);
	    break;
	  case CTRL_BUTTON:
	    macctrl_button(mcs, window, curstate, ctrl);
	    break;

	}
    }
}

static void macctrl_text(struct macctrls *mcs, WindowPtr window,
			 struct mac_layoutstate *curstate,
			 union control *ctrl)
{
    union macctrl *mc = smalloc(sizeof *mc);
    Rect bounds;

    fprintf(stderr, "    label = %s\n", ctrl->text.label);
    mc->generic.type = MACCTRL_TEXT;
    mc->generic.ctrl = ctrl;
    bounds.left = curstate->pos.h;
    bounds.right = bounds.left + curstate->width;
    bounds.top = curstate->pos.v;
    bounds.bottom = bounds.top + 16;
    if (mac_gestalts.apprvers >= 0x100) {
	SInt16 height;
	Size olen;

	mc->text.tbctrl = NewControl(window, &bounds, NULL, TRUE, 0, 0, 0,
				     kControlStaticTextProc, (long)mc);
	SetControlData(mc->text.tbctrl, kControlEntireControl,
		       kControlStaticTextTextTag,
		       strlen(ctrl->text.label), ctrl->text.label);
	GetControlData(mc->text.tbctrl, kControlEntireControl,
		       kControlStaticTextTextHeightTag,
		       sizeof(height), &height, &olen);
	fprintf(stderr, "    height = %d\n", height);
	SizeControl(mc->text.tbctrl, curstate->width, height);
	curstate->pos.v += height + 6;
    } else {
	Str255 title;

	c2pstrcpy(title, ctrl->text.label);
	mc->text.tbctrl = NewControl(window, &bounds, title, TRUE, 0, 0, 0,
				     SYS7_TEXT_PROC, (long)mc);
    }
    add234(mcs->byctrl, mc);
}

#if !TARGET_API_MAC_CARBON
static pascal SInt32 macctrl_sys7_text_cdef(SInt16 variant, ControlRef control,
				     ControlDefProcMessage msg, SInt32 param)
{
    RgnHandle rgn;

    switch (msg) {
      case drawCntl:
	if ((*control)->contrlVis)
	    TETextBox((*control)->contrlTitle + 1, (*control)->contrlTitle[0],
		      &(*control)->contrlRect, teFlushDefault);
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

static void macctrl_radio(struct macctrls *mcs, WindowPtr window,
			  struct mac_layoutstate *curstate,
			  union control *ctrl)
{
    union macctrl *mc = smalloc(sizeof *mc);
    Rect bounds;
    Str255 title;
    unsigned int i, colwidth;

    fprintf(stderr, "    label = %s\n", ctrl->radio.label);
    mc->generic.type = MACCTRL_RADIO;
    mc->generic.ctrl = ctrl;
    mc->radio.tbctrls =
	smalloc(sizeof(*mc->radio.tbctrls) * ctrl->radio.nbuttons);
    colwidth = (curstate->width + 13) /	ctrl->radio.ncolumns;
    for (i = 0; i < ctrl->radio.nbuttons; i++) {
	fprintf(stderr, "    button = %s\n", ctrl->radio.buttons[i]);
	bounds.top = curstate->pos.v;
	bounds.bottom = bounds.top + 16;
	bounds.left = curstate->pos.h + colwidth * (i % ctrl->radio.ncolumns);
	if (i == ctrl->radio.nbuttons - 1 ||
	    i % ctrl->radio.ncolumns == ctrl->radio.ncolumns - 1) {
	    bounds.right = curstate->pos.h + curstate->width;
	    curstate->pos.v += 22;
	} else
	    bounds.right = bounds.left + colwidth - 13;
	c2pstrcpy(title, ctrl->radio.buttons[i]);
	mc->radio.tbctrls[i] = NewControl(window, &bounds, title, TRUE,
					  0, 0, 1, radioButProc, (long)mc);
    }
    add234(mcs->byctrl, mc);
    ctrlevent(mcs, mc, EVENT_REFRESH);
}

static void macctrl_checkbox(struct macctrls *mcs, WindowPtr window,
			     struct mac_layoutstate *curstate,
			     union control *ctrl)
{
    union macctrl *mc = smalloc(sizeof *mc);
    Rect bounds;
    Str255 title;

    fprintf(stderr, "    label = %s\n", ctrl->checkbox.label);
    mc->generic.type = MACCTRL_CHECKBOX;
    mc->generic.ctrl = ctrl;
    bounds.left = curstate->pos.h;
    bounds.right = bounds.left + curstate->width;
    bounds.top = curstate->pos.v;
    bounds.bottom = bounds.top + 16;
    c2pstrcpy(title, ctrl->checkbox.label);
    mc->checkbox.tbctrl = NewControl(window, &bounds, title, TRUE, 0, 0, 1,
				     checkBoxProc, (long)mc);
    add234(mcs->byctrl, mc);
    curstate->pos.v += 22;
    ctrlevent(mcs, mc, EVENT_REFRESH);
}

static void macctrl_button(struct macctrls *mcs, WindowPtr window,
			   struct mac_layoutstate *curstate,
			   union control *ctrl)
{
    union macctrl *mc = smalloc(sizeof *mc);
    Rect bounds;
    Str255 title;

    fprintf(stderr, "    label = %s\n", ctrl->button.label);
    if (ctrl->button.isdefault)
	fprintf(stderr, "    is default\n");
    mc->generic.type = MACCTRL_BUTTON;
    mc->generic.ctrl = ctrl;
    bounds.left = curstate->pos.h;
    bounds.right = bounds.left + 100; /* XXX measure string */
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


void macctrl_activate(WindowPtr window, EventRecord *event)
{
    Boolean active = (event->modifiers & activeFlag) != 0;
    GrafPtr saveport;
    ControlRef root;

    GetPort(&saveport);
    SetPort((GrafPtr)GetWindowPort(window));
    if (mac_gestalts.apprvers >= 0x100) {
	SetThemeWindowBackground(window, active ?
				 kThemeBrushModelessDialogBackgroundActive :
				 kThemeBrushModelessDialogBackgroundInactive,
				 TRUE);
	GetRootControl(window, &root);
	if (active)
	    ActivateControl(root);
	else
	    DeactivateControl(root);
    } else {
	/* (De)activate controls one at a time */
    }
    SetPort(saveport);
}

void macctrl_click(WindowPtr window, EventRecord *event)
{
    Point mouse;
    ControlHandle control;
    int part;
    GrafPtr saveport;
    union macctrl *mc;
    struct macctrls *mcs = mac_winctrls(window);
    int i;

    GetPort(&saveport);
    SetPort((GrafPtr)GetWindowPort(window));
    mouse = event->where;
    GlobalToLocal(&mouse);
    part = FindControl(mouse, window, &control);
    if (control != NULL)
	if (TrackControl(control, mouse, NULL) != 0) {
	    mc = (union macctrl *)GetControlReference(control);
	    switch (mc->generic.type) {
	      case MACCTRL_RADIO:
		for (i = 0; i < mc->generic.ctrl->radio.nbuttons; i++) {
		    if (mc->radio.tbctrls[i] == control)
			SetControlValue(mc->radio.tbctrls[i],
					kControlRadioButtonCheckedValue);
		    else
			SetControlValue(mc->radio.tbctrls[i],
					kControlRadioButtonUncheckedValue);
		}
		ctrlevent(mcs, mc, EVENT_VALCHANGE);
		break;
	      case MACCTRL_CHECKBOX:
		SetControlValue(control, !GetControlValue(control));
		ctrlevent(mcs, mc, EVENT_VALCHANGE);
		break;
	      case MACCTRL_BUTTON:
		ctrlevent(mcs, mc, EVENT_ACTION);
		break;
	    }
	}
    SetPort(saveport);
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

    while ((mc = index234(mcs->byctrl, 0)) != NULL) {
	del234(mcs->byctrl, mc);
	sfree(mc);
    }

    freetree234(mcs->byctrl);
    mcs->byctrl = NULL;

/* XXX
    DisposeWindow(window);
    if (s->window == NULL)
	sfree(s);
*/
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

};

void *dlg_get_privdata(union control *ctrl, void *dlg)
{

    return NULL;
}

void dlg_set_privdata(union control *ctrl, void *dlg, void *ptr)
{

    fatalbox("dlg_set_privdata");
}

void *dlg_alloc_privdata(union control *ctrl, void *dlg, size_t size)
{

    fatalbox("dlg_alloc_privdata");
}


/*
 * Radio Button control
 */

void dlg_radiobutton_set(union control *ctrl, void *dlg, int whichbutton)
{
    union macctrl *mc = dlg;
    int i;

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
    union macctrl *mc = dlg;
    int i;

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
    union macctrl *mc = dlg;

    SetControlValue(mc->checkbox.tbctrl,
		    checked ? kControlCheckBoxCheckedValue :
		              kControlCheckBoxUncheckedValue);
}

int dlg_checkbox_get(union control *ctrl, void *dlg)
{
    union macctrl *mc = dlg;

    return GetControlValue(mc->checkbox.tbctrl);
}


/*
 * Edit Box control
 */

void dlg_editbox_set(union control *ctrl, void *dlg, char const *text)
{

};

void dlg_editbox_get(union control *ctrl, void *dlg, char *buffer, int length)
{

};


/*
 * List Box control
 */

void dlg_listbox_clear(union control *ctrl, void *dlg)
{

};

void dlg_listbox_del(union control *ctrl, void *dlg, int index)
{

};

void dlg_listbox_add(union control *ctrl, void *dlg, char const *text)
{

};

void dlg_listbox_addwithindex(union control *ctrl, void *dlg,
			      char const *text, int id)
{

};

int dlg_listbox_getid(union control *ctrl, void *dlg, int index)
{

    return 0;
};

int dlg_listbox_index(union control *ctrl, void *dlg)
{

    return 0;
};

int dlg_listbox_issel(union control *ctrl, void *dlg, int index)
{

    return 0;
};

void dlg_listbox_select(union control *ctrl, void *dlg, int index)
{

};


/*
 * Text control
 */

void dlg_text_set(union control *ctrl, void *dlg, char const *text)
{
    union macctrl *mc = dlg;
    Str255 title;

    if (mac_gestalts.apprvers >= 0x100)
	SetControlData(mc->text.tbctrl, kControlEntireControl,
		       kControlStaticTextTextTag,
		       strlen(ctrl->text.label), ctrl->text.label);
    else {
	c2pstrcpy(title, text);
	SetControlTitle(mc->text.tbctrl, title);
    }
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
