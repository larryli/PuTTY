#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#ifndef AUTO_WINSOCK
#ifdef WINSOCK_TWO
#include <winsock2.h>
#else
#include <winsock.h>
#endif
#endif
#include <stdio.h>
#include <stdlib.h>

#include "ssh.h"
#include "putty.h"
#include "win_res.h"
#include "storage.h"

static char **events = NULL;
static int nevents = 0, negsize = 0;

static HWND logbox = NULL, abtbox = NULL;

static HINSTANCE hinst;

static int readytogo;

static void force_normal(HWND hwnd)
{
    static int recurse = 0;

    WINDOWPLACEMENT wp;

    if(recurse) return;
    recurse = 1;

    wp.length = sizeof(wp);
    if (GetWindowPlacement(hwnd, &wp))
    {
	wp.showCmd = SW_SHOWNORMAL;
	SetWindowPlacement(hwnd, &wp);
    }
    recurse = 0;
}

static void MyGetDlgItemInt (HWND hwnd, int id, int *result) {
    BOOL ok;
    int n;
    n = GetDlgItemInt (hwnd, id, &ok, FALSE);
    if (ok)
	*result = n;
}

static int CALLBACK LogProc (HWND hwnd, UINT msg,
			     WPARAM wParam, LPARAM lParam) {
    int i;

    switch (msg) {
      case WM_INITDIALOG:
	for (i=0; i<nevents; i++)
	    SendDlgItemMessage (hwnd, IDN_LIST, LB_ADDSTRING,
				0, (LPARAM)events[i]);
	return 1;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDOK:
	    logbox = NULL;
	    DestroyWindow (hwnd);
	    return 0;
          case IDN_COPY:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
                int selcount;
                int *selitems;
                selcount = SendDlgItemMessage(hwnd, IDN_LIST,
                                              LB_GETSELCOUNT, 0, 0);
                selitems = malloc(selcount * sizeof(int));
                if (selitems) {
                    int count = SendDlgItemMessage(hwnd, IDN_LIST,
                                                   LB_GETSELITEMS,
                                                   selcount, (LPARAM)selitems);
                    int i;
                    int size;
                    char *clipdata;
                    static unsigned char sel_nl[] = SEL_NL;

                    if (count == 0) {  /* can't copy zero stuff */
                        MessageBeep(0);
                        break;
                    }

                    size = 0;
                    for (i = 0; i < count; i++)
                        size += strlen(events[selitems[i]]) + sizeof(sel_nl);

                    clipdata = malloc(size);
                    if (clipdata) {
                        char *p = clipdata;
                        for (i = 0; i < count; i++) {
                            char *q = events[selitems[i]];
                            int qlen = strlen(q);
                            memcpy(p, q, qlen);
                            p += qlen;
                            memcpy(p, sel_nl, sizeof(sel_nl));
                            p += sizeof(sel_nl);
                        }
                        write_clip(clipdata, size, TRUE);
                        free(clipdata);
                    }
                    free(selitems);

                    for (i = 0; i < nevents; i++)
                        SendDlgItemMessage(hwnd, IDN_LIST, LB_SETSEL,
                                           FALSE, i);
                }
            }
            return 0;
	}
	return 0;
      case WM_CLOSE:
	logbox = NULL;
	DestroyWindow (hwnd);
	return 0;
    }
    return 0;
}

static int CALLBACK LicenceProc (HWND hwnd, UINT msg,
				 WPARAM wParam, LPARAM lParam) {
    switch (msg) {
      case WM_INITDIALOG:
	return 1;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDOK:
	    EndDialog(hwnd, 1);
	    return 0;
	}
	return 0;
      case WM_CLOSE:
	EndDialog(hwnd, 1);
	return 0;
    }
    return 0;
}

static int CALLBACK AboutProc (HWND hwnd, UINT msg,
			       WPARAM wParam, LPARAM lParam) {
    switch (msg) {
      case WM_INITDIALOG:
        SetDlgItemText (hwnd, IDA_VERSION, ver);
	return 1;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDOK:
	    abtbox = NULL;
	    DestroyWindow (hwnd);
	    return 0;
	  case IDA_LICENCE:
	    EnableWindow(hwnd, 0);
	    DialogBox (hinst, MAKEINTRESOURCE(IDD_LICENCEBOX),
		       NULL, LicenceProc);
	    EnableWindow(hwnd, 1);
            SetActiveWindow(hwnd);
	    return 0;
	}
	return 0;
      case WM_CLOSE:
	abtbox = NULL;
	DestroyWindow (hwnd);
	return 0;
    }
    return 0;
}

/* ----------------------------------------------------------------------
 * Routines to self-manage the controls in a dialog box.
 */

#define GAPBETWEEN 3
#define GAPWITHIN 1
#define GAPXBOX 7
#define GAPYBOX 4
#define DLGWIDTH 168
#define STATICHEIGHT 8
#define CHECKBOXHEIGHT 8
#define RADIOHEIGHT 8
#define EDITHEIGHT 12
#define COMBOHEIGHT 12
#define PUSHBTNHEIGHT 14

struct ctlpos {
    HWND hwnd;
    WPARAM font;
    int dlu4inpix;
    int ypos, width;
    int xoff;
    int boxystart, boxid, boxtextid;
    char *boxtext;
};

static void ctlposinit(struct ctlpos *cp, HWND hwnd,
		       int leftborder, int rightborder, int topborder) {
    RECT r, r2;
    cp->hwnd = hwnd;
    cp->font = SendMessage(hwnd, WM_GETFONT, 0, 0);
    cp->ypos = topborder;
    GetClientRect(hwnd, &r);
    r2.left = r2.top = 0;
    r2.right = 4;
    r2.bottom = 8;
    MapDialogRect(hwnd, &r2);
    cp->dlu4inpix = r2.right;
    cp->width = (r.right * 4) / (r2.right) - 2*GAPBETWEEN;
    cp->xoff = leftborder;
    cp->width -= leftborder + rightborder;
}

static void doctl(struct ctlpos *cp, RECT r,
                  char *wclass, int wstyle, int exstyle,
                  char *wtext, int wid) {
    HWND ctl;
    /*
     * Note nonstandard use of RECT. This is deliberate: by
     * transforming the width and height directly we arrange to
     * have all supposedly same-sized controls really same-sized.
     */

    r.left += cp->xoff;
    MapDialogRect(cp->hwnd, &r);

    ctl = CreateWindowEx(exstyle, wclass, wtext, wstyle,
                         r.left, r.top, r.right, r.bottom,
                         cp->hwnd, (HMENU)wid, hinst, NULL);
    SendMessage(ctl, WM_SETFONT, cp->font, MAKELPARAM(TRUE, 0));
}

/*
 * A title bar across the top of a sub-dialog.
 */
static void bartitle(struct ctlpos *cp, char *name, int id) {
    RECT r;

    r.left = GAPBETWEEN; r.right = cp->width;
    r.top = cp->ypos; r.bottom = STATICHEIGHT;
    cp->ypos += r.bottom + GAPBETWEEN;
    doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, name, id);
}

/*
 * Begin a grouping box, with or without a group title.
 */
static void beginbox(struct ctlpos *cp, char *name, int idbox, int idtext) {
    if (name)
        cp->ypos += STATICHEIGHT/2;
    cp->boxystart = cp->ypos;
    if (name)
        cp->ypos += STATICHEIGHT - (STATICHEIGHT/2);
    cp->ypos += GAPYBOX;
    cp->width -= 2*GAPXBOX;
    cp->xoff += GAPXBOX;
    cp->boxid = idbox;
    cp->boxtextid = idtext;
    cp->boxtext = name;
}

/*
 * End a grouping box.
 */
static void endbox(struct ctlpos *cp) {
    RECT r;
    cp->xoff -= GAPXBOX;
    cp->width += 2*GAPXBOX;
    cp->ypos += GAPYBOX - GAPBETWEEN;
    r.left = GAPBETWEEN; r.right = cp->width;
    r.top = cp->boxystart; r.bottom = cp->ypos - cp->boxystart;
    doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE | SS_ETCHEDFRAME, 0,
          "", cp->boxid);
    if (cp->boxtext) {
        SIZE s;
        HDC hdc;
        HFONT oldfont, dlgfont;
        hdc = GetDC(cp->hwnd);
        dlgfont = (HFONT)cp->font;
        oldfont = SelectObject(hdc, dlgfont);
        GetTextExtentPoint32(hdc, cp->boxtext, strlen(cp->boxtext), &s);
        SelectObject(hdc, oldfont);
        DeleteDC(hdc);
        r.left = GAPXBOX + GAPBETWEEN;
        r.right = (s.cx * 4 + cp->dlu4inpix-1) / cp->dlu4inpix;
        
        r.top = cp->boxystart - STATICHEIGHT/2; r.bottom = STATICHEIGHT;
        doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0,
              cp->boxtext, cp->boxtextid);
    }
    cp->ypos += GAPYBOX;
}

/*
 * Some edit boxes. Each one has a static above it. The percentages
 * of the horizontal space are provided.
 */
static void multiedit(struct ctlpos *cp, ...) {
    RECT r;
    va_list ap;
    int percent, xpos;

    percent = xpos = 0;
    va_start(ap, cp);
    while (1) {
        char *text;
        int staticid, editid, pcwidth;
        text = va_arg(ap, char *);
        if (!text)
            break;
        staticid = va_arg(ap, int);
        editid = va_arg(ap, int);
        pcwidth = va_arg(ap, int);

        r.left = xpos + GAPBETWEEN;
        percent += pcwidth;
        xpos = (cp->width + GAPBETWEEN) * percent / 100;
        r.right = xpos - r.left;

        r.top = cp->ypos; r.bottom = STATICHEIGHT;
        doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0,
              text, staticid);
        r.top = cp->ypos + 8 + GAPWITHIN; r.bottom = EDITHEIGHT;
        doctl(cp, r, "EDIT",
              WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
              WS_EX_CLIENTEDGE,
              "", editid);
    }
    va_end(ap);
    cp->ypos += 8+GAPWITHIN+12+GAPBETWEEN;
}

/*
 * A set of radio buttons on the same line, with a static above
 * them. `nacross' dictates how many parts the line is divided into
 * (you might want this not to equal the number of buttons if you
 * needed to line up some 2s and some 3s to look good in the same
 * panel).
 */
static void radioline(struct ctlpos *cp,
                      char *text, int id, int nacross, ...) {
    RECT r;
    va_list ap;
    int group;
    int i;

    r.left = GAPBETWEEN; r.top = cp->ypos;
    r.right = cp->width; r.bottom = STATICHEIGHT;
    cp->ypos += r.bottom + GAPWITHIN;
    doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, text, id);
    va_start(ap, nacross);
    group = WS_GROUP;
    i = 0;
    while (1) {
        char *btext;
        int bid;
        btext = va_arg(ap, char *);
        if (!btext)
            break;
        bid = va_arg(ap, int);
        r.left = GAPBETWEEN + i * (cp->width+GAPBETWEEN)/nacross;
        r.right = (i+1) * (cp->width+GAPBETWEEN)/nacross - r.left;
        r.top = cp->ypos; r.bottom = RADIOHEIGHT;
        doctl(cp, r, "BUTTON",
              BS_AUTORADIOBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP | group,
              0,
              btext, bid);
        group = 0;
        i++;
    }
    va_end(ap);
    cp->ypos += r.bottom + GAPBETWEEN;
}

/*
 * A set of radio buttons on multiple lines, with a static above
 * them.
 */
static void radiobig(struct ctlpos *cp, char *text, int id, ...) {
    RECT r;
    va_list ap;
    int group;

    r.left = GAPBETWEEN; r.top = cp->ypos;
    r.right = cp->width; r.bottom = STATICHEIGHT;
    cp->ypos += r.bottom + GAPWITHIN;
    doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, text, id);
    va_start(ap, id);
    group = WS_GROUP;
    while (1) {
        char *btext;
        int bid;
        btext = va_arg(ap, char *);
        if (!btext)
            break;
        bid = va_arg(ap, int);
        r.left = GAPBETWEEN; r.top = cp->ypos;
        r.right = cp->width; r.bottom = STATICHEIGHT;
        cp->ypos += r.bottom + GAPWITHIN;
        doctl(cp, r, "BUTTON",
              BS_AUTORADIOBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP | group,
              0,
              btext, bid);
        group = 0;
    }
    va_end(ap);
    cp->ypos += GAPBETWEEN - GAPWITHIN;
}

/*
 * A single standalone checkbox.
 */
static void checkbox(struct ctlpos *cp, char *text, int id) {
    RECT r;

    r.left = GAPBETWEEN; r.top = cp->ypos;
    r.right = cp->width; r.bottom = CHECKBOXHEIGHT;
    cp->ypos += r.bottom + GAPBETWEEN;
    doctl(cp, r, "BUTTON",
          BS_AUTOCHECKBOX | WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0,
          text, id);
}

/*
 * A button on the right hand side, with a static to its left.
 */
static void staticbtn(struct ctlpos *cp, char *stext, int sid,
                      char *btext, int bid) {
    const int height = (PUSHBTNHEIGHT > STATICHEIGHT ?
                        PUSHBTNHEIGHT : STATICHEIGHT);
    RECT r;
    int lwid, rwid, rpos;

    rpos = GAPBETWEEN + 3 * (cp->width + GAPBETWEEN) / 4;
    lwid = rpos - 2*GAPBETWEEN;
    rwid = cp->width + GAPBETWEEN - rpos;

    r.left = GAPBETWEEN; r.top = cp->ypos + (height-STATICHEIGHT)/2;
    r.right = lwid; r.bottom = STATICHEIGHT;
    doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, stext, sid);

    r.left = rpos; r.top = cp->ypos + (height-PUSHBTNHEIGHT)/2;
    r.right = rwid; r.bottom = PUSHBTNHEIGHT;
    doctl(cp, r, "BUTTON",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
          0,
          btext, bid);

    cp->ypos += height + GAPBETWEEN;
}

/*
 * An edit control on the right hand side, with a static to its left.
 */
static void staticedit(struct ctlpos *cp, char *stext,
                       int sid, int eid, int percentedit) {
    const int height = (EDITHEIGHT > STATICHEIGHT ?
                        EDITHEIGHT : STATICHEIGHT);
    RECT r;
    int lwid, rwid, rpos;

    rpos = GAPBETWEEN + (100-percentedit) * (cp->width + GAPBETWEEN) / 100;
    lwid = rpos - 2*GAPBETWEEN;
    rwid = cp->width + GAPBETWEEN - rpos;

    r.left = GAPBETWEEN; r.top = cp->ypos + (height-STATICHEIGHT)/2;
    r.right = lwid; r.bottom = STATICHEIGHT;
    doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, stext, sid);

    r.left = rpos; r.top = cp->ypos + (height-EDITHEIGHT)/2;
    r.right = rwid; r.bottom = EDITHEIGHT;
    doctl(cp, r, "EDIT",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
          WS_EX_CLIENTEDGE,
          "", eid);

    cp->ypos += height + GAPBETWEEN;
}

/*
 * A tab-control substitute when a real tab control is unavailable.
 */
static void ersatztab(struct ctlpos *cp, char *stext, int sid,
                      int lid, int s2id) {
    const int height = (COMBOHEIGHT > STATICHEIGHT ?
                        COMBOHEIGHT : STATICHEIGHT);
    RECT r;
    int bigwid, lwid, rwid, rpos;
    static const int BIGGAP = 15;
    static const int MEDGAP = 3;

    bigwid = cp->width + 2*GAPBETWEEN - 2*BIGGAP;
    cp->ypos += MEDGAP;
    rpos = BIGGAP + (bigwid + BIGGAP) / 2;
    lwid = rpos - 2*BIGGAP;
    rwid = bigwid + BIGGAP - rpos;

    r.left = BIGGAP; r.top = cp->ypos + (height-STATICHEIGHT)/2;
    r.right = lwid; r.bottom = STATICHEIGHT;
    doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, stext, sid);

    r.left = rpos; r.top = cp->ypos + (height-COMBOHEIGHT)/2;
    r.right = rwid; r.bottom = COMBOHEIGHT*10;
    doctl(cp, r, "COMBOBOX",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP |
          CBS_DROPDOWNLIST | CBS_HASSTRINGS,
          WS_EX_CLIENTEDGE,
          "", lid);

    cp->ypos += height + MEDGAP + GAPBETWEEN;

    r.left = GAPBETWEEN; r.top = cp->ypos;
    r.right = cp->width; r.bottom = 2;
    doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
          0, "", s2id);
}

/*
 * A static line, followed by an edit control on the left hand side
 * and a button on the right.
 */
static void editbutton(struct ctlpos *cp, char *stext, int sid,
                       int eid, char *btext, int bid) {
    const int height = (EDITHEIGHT > PUSHBTNHEIGHT ?
                        EDITHEIGHT : PUSHBTNHEIGHT);
    RECT r;
    int lwid, rwid, rpos;

    r.left = GAPBETWEEN; r.top = cp->ypos;
    r.right = cp->width; r.bottom = STATICHEIGHT;
    cp->ypos += r.bottom + GAPWITHIN;
    doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, stext, sid);

    rpos = GAPBETWEEN + 3 * (cp->width + GAPBETWEEN) / 4;
    lwid = rpos - 2*GAPBETWEEN;
    rwid = cp->width + GAPBETWEEN - rpos;

    r.left = GAPBETWEEN; r.top = cp->ypos + (height-EDITHEIGHT)/2;
    r.right = lwid; r.bottom = EDITHEIGHT;
    doctl(cp, r, "EDIT",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
          WS_EX_CLIENTEDGE,
          "", eid);

    r.left = rpos; r.top = cp->ypos + (height-PUSHBTNHEIGHT)/2;
    r.right = rwid; r.bottom = PUSHBTNHEIGHT;
    doctl(cp, r, "BUTTON",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
          0,
          btext, bid);

    cp->ypos += height + GAPBETWEEN;
}

/*
 * Special control which was hard to describe generically: the
 * session-saver assembly. A static; below that an edit box; below
 * that a list box. To the right of the list box, a column of
 * buttons.
 */
static void sesssaver(struct ctlpos *cp, char *text,
                      int staticid, int editid, int listid, ...) {
    RECT r;
    va_list ap;
    int lwid, rwid, rpos;
    int y;
    const int LISTDEFHEIGHT = 66;

    rpos = GAPBETWEEN + 3 * (cp->width + GAPBETWEEN) / 4;
    lwid = rpos - 2*GAPBETWEEN;
    rwid = cp->width + GAPBETWEEN - rpos;

    /* The static control. */
    r.left = GAPBETWEEN; r.top = cp->ypos;
    r.right = lwid; r.bottom = STATICHEIGHT;
    cp->ypos += r.bottom + GAPWITHIN;
    doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, text, staticid);

    /* The edit control. */
    r.left = GAPBETWEEN; r.top = cp->ypos;
    r.right = lwid; r.bottom = EDITHEIGHT;
    cp->ypos += r.bottom + GAPWITHIN;
    doctl(cp, r, "EDIT",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
          WS_EX_CLIENTEDGE,
          "", editid);

    /*
     * The buttons (we should hold off on the list box until we
     * know how big the buttons are).
     */
    va_start(ap, listid);
    y = cp->ypos;
    while (1) {
        char *btext = va_arg(ap, char *);
        int bid;
        if (!btext) break;
        bid = va_arg(ap, int);
        r.left = rpos; r.top = y;
        r.right = rwid; r.bottom = PUSHBTNHEIGHT;
        y += r.bottom + GAPWITHIN;
        doctl(cp, r, "BUTTON",
              WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
              0,
              btext, bid);
    }

    /* Compute list box height. LISTDEFHEIGHT, or height of buttons. */
    y -= cp->ypos;
    y -= GAPWITHIN;
    if (y < LISTDEFHEIGHT) y = LISTDEFHEIGHT;
    r.left = GAPBETWEEN; r.top = cp->ypos;
    r.right = lwid; r.bottom = y;
    cp->ypos += y + GAPBETWEEN;
    doctl(cp, r, "LISTBOX",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | 
          LBS_NOTIFY | LBS_HASSTRINGS,
          WS_EX_CLIENTEDGE,
          "", listid);
}

/*
 * Another special control: the environment-variable setter. A
 * static line first; then a pair of edit boxes with associated
 * statics, and two buttons; then a list box.
 */
static void envsetter(struct ctlpos *cp, char *stext, int sid,
                      char *e1stext, int e1sid, int e1id,
                      char *e2stext, int e2sid, int e2id,
                      int listid,
                      char *b1text, int b1id, char *b2text, int b2id) {
    RECT r;
    const int height = (STATICHEIGHT > EDITHEIGHT && STATICHEIGHT > PUSHBTNHEIGHT ?
                        STATICHEIGHT :
                        EDITHEIGHT > PUSHBTNHEIGHT ?
                        EDITHEIGHT : PUSHBTNHEIGHT);
    const static int percents[] = { 20, 35, 10, 25 };
    int i, j, xpos, percent;
    const int LISTHEIGHT = 42;

    /* The static control. */
    r.left = GAPBETWEEN; r.top = cp->ypos;
    r.right = cp->width; r.bottom = STATICHEIGHT;
    cp->ypos += r.bottom + GAPWITHIN;
    doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, stext, sid);

    /* The statics+edits+buttons. */
    for (j = 0; j < 2; j++) {
        percent = 10;
        for (i = 0; i < 4; i++) {
            xpos = (cp->width + GAPBETWEEN) * percent / 100;
            r.left = xpos + GAPBETWEEN;
            percent += percents[i];
            xpos = (cp->width + GAPBETWEEN) * percent / 100;
            r.right = xpos - r.left;
            r.top = cp->ypos;
            r.bottom = (i==0 ? STATICHEIGHT :
                        i==1 ? EDITHEIGHT :
                        PUSHBTNHEIGHT);
            r.top += (height-r.bottom)/2;
            if (i==0) {
                doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0,
                      j==0 ? e1stext : e2stext, j==0 ? e1sid : e2sid);
            } else if (i==1) {
                doctl(cp, r, "EDIT",
                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                      WS_EX_CLIENTEDGE,
                      "", j==0 ? e1id : e2id);
            } else if (i==3) {
                doctl(cp, r, "BUTTON",
                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                      0,
                      j==0 ? b1text : b2text, j==0 ? b1id : b2id);
            }
        }
        cp->ypos += height + GAPWITHIN;
    }

    /* The list box. */
    r.left = GAPBETWEEN; r.top = cp->ypos;
    r.right = cp->width; r.bottom = LISTHEIGHT;
    cp->ypos += r.bottom + GAPBETWEEN;
    doctl(cp, r, "LISTBOX",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_HASSTRINGS |
          LBS_USETABSTOPS,
          WS_EX_CLIENTEDGE,
          "", listid);
}

/*
 * Yet another special control: the character-class setter. A
 * static, then a list, then a line containing a
 * button-and-static-and-edit. 
 */
static void charclass(struct ctlpos *cp, char *stext, int sid, int listid,
                      char *btext, int bid, int eid, char *s2text, int s2id) {
    RECT r;
    const int height = (STATICHEIGHT > EDITHEIGHT && STATICHEIGHT > PUSHBTNHEIGHT ?
                        STATICHEIGHT :
                        EDITHEIGHT > PUSHBTNHEIGHT ?
                        EDITHEIGHT : PUSHBTNHEIGHT);
    const static int percents[] = { 30, 40, 30 };
    int i, xpos, percent;
    const int LISTHEIGHT = 66;

    /* The static control. */
    r.left = GAPBETWEEN; r.top = cp->ypos;
    r.right = cp->width; r.bottom = STATICHEIGHT;
    cp->ypos += r.bottom + GAPWITHIN;
    doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, stext, sid);

    /* The list box. */
    r.left = GAPBETWEEN; r.top = cp->ypos;
    r.right = cp->width; r.bottom = LISTHEIGHT;
    cp->ypos += r.bottom + GAPWITHIN;
    doctl(cp, r, "LISTBOX",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_HASSTRINGS |
          LBS_USETABSTOPS,
          WS_EX_CLIENTEDGE,
          "", listid);

    /* The button+static+edit. */
    percent = xpos = 0;
    for (i = 0; i < 3; i++) {
        r.left = xpos + GAPBETWEEN;
        percent += percents[i];
        xpos = (cp->width + GAPBETWEEN) * percent / 100;
        r.right = xpos - r.left;
        r.top = cp->ypos;
        r.bottom = (i==0 ? PUSHBTNHEIGHT :
                    i==1 ? STATICHEIGHT :
                    EDITHEIGHT);
        r.top += (height-r.bottom)/2;
        if (i==0) {
            doctl(cp, r, "BUTTON",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                  0, btext, bid);
        } else if (i==1) {
            doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE | SS_CENTER,
                  0, s2text, s2id);
        } else if (i==2) {
            doctl(cp, r, "EDIT",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                  WS_EX_CLIENTEDGE, "", eid);
        }
    }
    cp->ypos += height + GAPBETWEEN;
}

/*
 * A special control (horrors!). The colour editor. A static line;
 * then on the left, a list box, and on the right, a sequence of
 * two-part statics followed by a button.
 */
static void colouredit(struct ctlpos *cp, char *stext, int sid, int listid,
                       char *btext, int bid, ...) {
    RECT r;
    int y;
    va_list ap;
    int lwid, rwid, rpos;
    const int LISTHEIGHT = 66;

    /* The static control. */
    r.left = GAPBETWEEN; r.top = cp->ypos;
    r.right = cp->width; r.bottom = STATICHEIGHT;
    cp->ypos += r.bottom + GAPWITHIN;
    doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, stext, sid);
    
    rpos = GAPBETWEEN + 2 * (cp->width + GAPBETWEEN) / 3;
    lwid = rpos - 2*GAPBETWEEN;
    rwid = cp->width + GAPBETWEEN - rpos;

    /* The list box. */
    r.left = GAPBETWEEN; r.top = cp->ypos;
    r.right = lwid; r.bottom = LISTHEIGHT;
    doctl(cp, r, "LISTBOX",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_HASSTRINGS |
          LBS_USETABSTOPS,
          WS_EX_CLIENTEDGE,
          "", listid);

    /* The statics. */
    y = cp->ypos;
    va_start(ap, bid);
    while (1) {
        char *ltext;
        int lid, rid;
        ltext = va_arg(ap, char *);
        if (!ltext) break;
        lid = va_arg(ap, int);
        rid = va_arg(ap, int);
        r.top = y; r.bottom = STATICHEIGHT;
        y += r.bottom + GAPWITHIN;
        r.left = rpos; r.right = rwid/2;
        doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE, 0, ltext, lid);
        r.left = rpos + r.right; r.right = rwid - r.right;
        doctl(cp, r, "STATIC", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, "", rid);
    }
    va_end(ap);

    /* The button. */
    r.top = y + 2*GAPWITHIN; r.bottom = PUSHBTNHEIGHT;
    r.left = rpos; r.right = rwid;
    doctl(cp, r, "BUTTON",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
          0, btext, bid);

    cp->ypos += LISTHEIGHT + GAPBETWEEN;
}

static char savedsession[2048];

enum { IDCX_ABOUT = IDC_ABOUT, IDCX_TVSTATIC, IDCX_TREEVIEW, controlstartvalue,

    sessionpanelstart,
    IDC_TITLE_SESSION,
    IDC_BOX_SESSION1, IDC_BOXT_SESSION1,
    IDC_BOX_SESSION2, IDC_BOXT_SESSION2,
    IDC_BOX_SESSION3,
    IDC_HOSTSTATIC,
    IDC_HOST,
    IDC_PORTSTATIC,
    IDC_PORT,
    IDC_PROTSTATIC,
    IDC_PROTRAW,
    IDC_PROTTELNET,
    IDC_PROTSSH,
    IDC_SESSSTATIC,
    IDC_SESSEDIT,
    IDC_SESSLIST,
    IDC_SESSLOAD,
    IDC_SESSSAVE,
    IDC_SESSDEL,
    IDC_CLOSEEXIT,
    sessionpanelend,

    keyboardpanelstart,
    IDC_TITLE_KEYBOARD,
    IDC_BOX_KEYBOARD1, IDC_BOXT_KEYBOARD1,
    IDC_BOX_KEYBOARD2, IDC_BOXT_KEYBOARD2,
    IDC_DELSTATIC,
    IDC_DEL008,
    IDC_DEL127,
    IDC_HOMESTATIC,
    IDC_HOMETILDE,
    IDC_HOMERXVT,
    IDC_FUNCSTATIC,
    IDC_FUNCTILDE,
    IDC_FUNCLINUX,
    IDC_FUNCXTERM,
    IDC_FUNCVT400,
    IDC_KPSTATIC,
    IDC_KPNORMAL,
    IDC_KPAPPLIC,
    IDC_KPNH,
    IDC_CURSTATIC,
    IDC_CURNORMAL,
    IDC_CURAPPLIC,
    keyboardpanelend,

    terminalpanelstart,
    IDC_TITLE_TERMINAL,
    IDC_BOX_TERMINAL1, IDC_BOXT_TERMINAL1,
    IDC_BOX_TERMINAL2, IDC_BOXT_TERMINAL2,
    IDC_BOX_TERMINAL3, IDC_BOXT_TERMINAL3,
    IDC_WRAPMODE,
    IDC_DECOM,
    IDC_DIMSTATIC,
    IDC_ROWSSTATIC,
    IDC_ROWSEDIT,
    IDC_COLSSTATIC,
    IDC_COLSEDIT,
    IDC_LOCKSIZE,
    IDC_FONTSTATIC,
    IDC_CHOOSEFONT,
    IDC_LFHASCR,
    IDC_BEEP,
    IDC_BCE,
    IDC_BLINKTEXT,
    IDC_LDISCTERM,
    terminalpanelend,

    windowpanelstart,
    IDC_TITLE_WINDOW,
    IDC_BOX_WINDOW1, IDC_BOXT_WINDOW1,
    IDC_BOX_WINDOW2, IDC_BOXT_WINDOW2,
    IDC_BOX_WINDOW3, IDC_BOXT_WINDOW3,
    IDC_BOX_WINDOW4,
    IDC_WINNAME,
    IDC_BLINKCUR,
    IDC_SCROLLBAR,
    IDC_WINTITLE,
    IDC_WINEDIT,
    IDC_CLOSEWARN,
    IDC_SAVESTATIC,
    IDC_SAVEEDIT,
    IDC_ALTF4,
    IDC_ALTSPACE,
    IDC_SCROLLKEY,
    windowpanelend,

    connectionpanelstart,
    IDC_TITLE_CONNECTION,
    IDC_BOX_CONNECTION1, IDC_BOXT_CONNECTION1,
    IDC_BOX_CONNECTION2, IDC_BOXT_CONNECTION2,
    IDC_TTSTATIC,
    IDC_TTEDIT,
    IDC_LOGSTATIC,
    IDC_LOGEDIT,
    IDC_PINGSTATIC,
    IDC_PINGEDIT,
    connectionpanelend,

    telnetpanelstart,
    IDC_TITLE_TELNET,
    IDC_BOX_TELNET1, IDC_BOXT_TELNET1,
    IDC_BOX_TELNET2, IDC_BOXT_TELNET2,
    IDC_TSSTATIC,
    IDC_TSEDIT,
    IDC_ENVSTATIC,
    IDC_VARSTATIC,
    IDC_VAREDIT,
    IDC_VALSTATIC,
    IDC_VALEDIT,
    IDC_ENVLIST,
    IDC_ENVADD,
    IDC_ENVREMOVE,
    IDC_EMSTATIC,
    IDC_EMBSD,
    IDC_EMRFC,
    telnetpanelend,

    sshpanelstart,
    IDC_TITLE_SSH,
    IDC_BOX_SSH1, IDC_BOXT_SSH1,
    IDC_BOX_SSH2, IDC_BOXT_SSH2,
    IDC_BOX_SSH3, IDC_BOXT_SSH3,
    IDC_NOPTY,
    IDC_CIPHERSTATIC,
    IDC_CIPHER3DES,
    IDC_CIPHERBLOWF,
    IDC_CIPHERDES,
    IDC_AUTHTIS,
    IDC_PKSTATIC,
    IDC_PKEDIT,
    IDC_PKBUTTON,
    IDC_SSHPROTSTATIC,
    IDC_SSHPROT1,
    IDC_SSHPROT2,
    IDC_AGENTFWD,
    IDC_CMDSTATIC,
    IDC_CMDEDIT,
    sshpanelend,

    selectionpanelstart,
    IDC_TITLE_SELECTION,
    IDC_BOX_SELECTION1, IDC_BOXT_SELECTION1,
    IDC_BOX_SELECTION2, IDC_BOXT_SELECTION2,
    IDC_MBSTATIC,
    IDC_MBWINDOWS,
    IDC_MBXTERM,
    IDC_CCSTATIC,
    IDC_CCLIST,
    IDC_CCSET,
    IDC_CCSTATIC2,
    IDC_CCEDIT,
    selectionpanelend,

    colourspanelstart,
    IDC_TITLE_COLOURS,
    IDC_BOX_COLOURS1, IDC_BOXT_COLOURS1,
    IDC_BOX_COLOURS2, IDC_BOXT_COLOURS2,
    IDC_BOLDCOLOUR,
    IDC_PALETTE,
    IDC_STATIC,
    IDC_LIST,
    IDC_RSTATIC,
    IDC_GSTATIC,
    IDC_BSTATIC,
    IDC_RVALUE,
    IDC_GVALUE,
    IDC_BVALUE,
    IDC_CHANGE,
    colourspanelend,

    translationpanelstart,
    IDC_TITLE_TRANSLATION,
    IDC_BOX_TRANSLATION1, IDC_BOXT_TRANSLATION1,
    IDC_BOX_TRANSLATION2, IDC_BOXT_TRANSLATION2,
    IDC_BOX_TRANSLATION3, IDC_BOXT_TRANSLATION3,
    IDC_XLATSTATIC,
    IDC_NOXLAT,
    IDC_KOI8WIN1251,
    IDC_88592WIN1250,
    IDC_88592CP852,
    IDC_CAPSLOCKCYR,
    IDC_VTSTATIC,
    IDC_VTXWINDOWS,
    IDC_VTOEMANSI,
    IDC_VTOEMONLY,
    IDC_VTPOORMAN,
    translationpanelend,

    controlendvalue
};

static const char *const colours[] = {
    "Default Foreground", "Default Bold Foreground",
    "Default Background", "Default Bold Background",
    "Cursor Text", "Cursor Colour",
    "ANSI Black", "ANSI Black Bold",
    "ANSI Red", "ANSI Red Bold",
    "ANSI Green", "ANSI Green Bold",
    "ANSI Yellow", "ANSI Yellow Bold",
    "ANSI Blue", "ANSI Blue Bold",
    "ANSI Magenta", "ANSI Magenta Bold",
    "ANSI Cyan", "ANSI Cyan Bold",
    "ANSI White", "ANSI White Bold"
};
static const int permcolour[] = {
    TRUE, FALSE, TRUE, FALSE, TRUE, TRUE,
    TRUE, FALSE, TRUE, FALSE, TRUE, FALSE, TRUE, FALSE,
    TRUE, FALSE, TRUE, FALSE, TRUE, FALSE, TRUE, FALSE
};

static void fmtfont (char *buf) {
    sprintf (buf, "Font: %s, ", cfg.font);
    if (cfg.fontisbold)
	strcat(buf, "bold, ");
    if (cfg.fontheight == 0)
	strcat (buf, "default height");
    else
	sprintf (buf+strlen(buf), "%d-%s",
		 (cfg.fontheight < 0 ? -cfg.fontheight : cfg.fontheight),
		 (cfg.fontheight < 0 ? "pixel" : "point"));
}

static void init_dlg_ctrls(HWND hwnd) {
    int i;
    char fontstatic[256];

    SetDlgItemText (hwnd, IDC_HOST, cfg.host);
    SetDlgItemText (hwnd, IDC_SESSEDIT, savedsession);
    SetDlgItemInt (hwnd, IDC_PORT, cfg.port, FALSE);
    CheckRadioButton (hwnd, IDC_PROTRAW, IDC_PROTSSH,
		      cfg.protocol==PROT_SSH ? IDC_PROTSSH :
		      cfg.protocol==PROT_TELNET ? IDC_PROTTELNET : IDC_PROTRAW );
    SetDlgItemInt (hwnd, IDC_PINGEDIT, cfg.ping_interval, FALSE);

    CheckRadioButton (hwnd, IDC_DEL008, IDC_DEL127,
		      cfg.bksp_is_delete ? IDC_DEL127 : IDC_DEL008);
    CheckRadioButton (hwnd, IDC_HOMETILDE, IDC_HOMERXVT,
		      cfg.rxvt_homeend ? IDC_HOMERXVT : IDC_HOMETILDE);
    CheckRadioButton (hwnd, IDC_FUNCTILDE, IDC_FUNCVT400,
                      cfg.funky_type == 0 ? IDC_FUNCTILDE :
                      cfg.funky_type == 1 ? IDC_FUNCLINUX :
                      cfg.funky_type == 2 ? IDC_FUNCXTERM :
                      cfg.funky_type == 3 ? IDC_FUNCVT400 :
                      IDC_FUNCTILDE );
    CheckRadioButton (hwnd, IDC_CURNORMAL, IDC_CURAPPLIC,
		      cfg.app_cursor ? IDC_CURAPPLIC : IDC_CURNORMAL);
    CheckRadioButton (hwnd, IDC_KPNORMAL, IDC_KPNH,
		      cfg.nethack_keypad ? IDC_KPNH :
		      cfg.app_keypad ? IDC_KPAPPLIC : IDC_KPNORMAL);
    CheckDlgButton (hwnd, IDC_ALTF4, cfg.alt_f4);
    CheckDlgButton (hwnd, IDC_ALTSPACE, cfg.alt_space);
    CheckDlgButton (hwnd, IDC_LDISCTERM, cfg.ldisc_term);
    CheckDlgButton (hwnd, IDC_SCROLLKEY, cfg.scroll_on_key);

    CheckDlgButton (hwnd, IDC_WRAPMODE, cfg.wrap_mode);
    CheckDlgButton (hwnd, IDC_DECOM, cfg.dec_om);
    CheckDlgButton (hwnd, IDC_LFHASCR, cfg.lfhascr);
    SetDlgItemInt (hwnd, IDC_ROWSEDIT, cfg.height, FALSE);
    SetDlgItemInt (hwnd, IDC_COLSEDIT, cfg.width, FALSE);
    SetDlgItemInt (hwnd, IDC_SAVEEDIT, cfg.savelines, FALSE);
    fmtfont (fontstatic);
    SetDlgItemText (hwnd, IDC_FONTSTATIC, fontstatic);
    CheckDlgButton (hwnd, IDC_BEEP, cfg.beep);
    CheckDlgButton (hwnd, IDC_BCE, cfg.bce);
    CheckDlgButton (hwnd, IDC_BLINKTEXT, cfg.blinktext);

    SetDlgItemText (hwnd, IDC_WINEDIT, cfg.wintitle);
    CheckDlgButton (hwnd, IDC_WINNAME, cfg.win_name_always);
    CheckDlgButton (hwnd, IDC_BLINKCUR, cfg.blink_cur);
    CheckDlgButton (hwnd, IDC_SCROLLBAR, cfg.scrollbar);
    CheckDlgButton (hwnd, IDC_LOCKSIZE, cfg.locksize);
    CheckDlgButton (hwnd, IDC_CLOSEEXIT, cfg.close_on_exit);
    CheckDlgButton (hwnd, IDC_CLOSEWARN, cfg.warn_on_close);

    SetDlgItemText (hwnd, IDC_TTEDIT, cfg.termtype);
    SetDlgItemText (hwnd, IDC_TSEDIT, cfg.termspeed);
    SetDlgItemText (hwnd, IDC_LOGEDIT, cfg.username);
    {
	char *p = cfg.environmt;
	while (*p) {
	    SendDlgItemMessage (hwnd, IDC_ENVLIST, LB_ADDSTRING, 0,
				(LPARAM) p);
	    p += strlen(p)+1;
	}
    }
    CheckRadioButton (hwnd, IDC_EMBSD, IDC_EMRFC,
		      cfg.rfc_environ ? IDC_EMRFC : IDC_EMBSD);

    SetDlgItemText (hwnd, IDC_TTEDIT, cfg.termtype);
    SetDlgItemText (hwnd, IDC_LOGEDIT, cfg.username);
    CheckDlgButton (hwnd, IDC_NOPTY, cfg.nopty);
    CheckDlgButton (hwnd, IDC_AGENTFWD, cfg.agentfwd);
    CheckRadioButton (hwnd, IDC_CIPHER3DES, IDC_CIPHERDES,
		      cfg.cipher == CIPHER_BLOWFISH ? IDC_CIPHERBLOWF :
		      cfg.cipher == CIPHER_DES ? IDC_CIPHERDES :
		      IDC_CIPHER3DES);
    CheckRadioButton (hwnd, IDC_SSHPROT1, IDC_SSHPROT2,
		      cfg.sshprot == 1 ? IDC_SSHPROT1 : IDC_SSHPROT2);
    CheckDlgButton (hwnd, IDC_AUTHTIS, cfg.try_tis_auth);
    SetDlgItemText (hwnd, IDC_PKEDIT, cfg.keyfile);
    SetDlgItemText (hwnd, IDC_CMDEDIT, cfg.remote_cmd);

    CheckRadioButton (hwnd, IDC_MBWINDOWS, IDC_MBXTERM,
		      cfg.mouse_is_xterm ? IDC_MBXTERM : IDC_MBWINDOWS);
    {
	static int tabs[4] = {25, 61, 96, 128};
	SendDlgItemMessage (hwnd, IDC_CCLIST, LB_SETTABSTOPS, 4,
			    (LPARAM) tabs);
    }
    for (i=0; i<256; i++) {
	char str[100];
	sprintf(str, "%d\t(0x%02X)\t%c\t%d", i, i,
		(i>=0x21 && i != 0x7F) ? i : ' ',
		cfg.wordness[i]);
	SendDlgItemMessage (hwnd, IDC_CCLIST, LB_ADDSTRING, 0,
			    (LPARAM) str);
    }

    CheckDlgButton (hwnd, IDC_BOLDCOLOUR, cfg.bold_colour);
    CheckDlgButton (hwnd, IDC_PALETTE, cfg.try_palette);
    {
	int i;
	for (i=0; i<22; i++)
	    if (cfg.bold_colour || permcolour[i])
		SendDlgItemMessage (hwnd, IDC_LIST, LB_ADDSTRING, 0,
				    (LPARAM) colours[i]);
    }
    SendDlgItemMessage (hwnd, IDC_LIST, LB_SETCURSEL, 0, 0);
    SetDlgItemInt (hwnd, IDC_RVALUE, cfg.colours[0][0], FALSE);
    SetDlgItemInt (hwnd, IDC_GVALUE, cfg.colours[0][1], FALSE);
    SetDlgItemInt (hwnd, IDC_BVALUE, cfg.colours[0][2], FALSE);

    CheckRadioButton (hwnd, IDC_NOXLAT, IDC_88592CP852,
		      cfg.xlat_88592w1250 ? IDC_88592WIN1250 :
		      cfg.xlat_88592cp852 ? IDC_88592CP852 :
		      cfg.xlat_enablekoiwin ? IDC_KOI8WIN1251 :
		      IDC_NOXLAT);
    CheckDlgButton (hwnd, IDC_CAPSLOCKCYR, cfg.xlat_capslockcyr);
    CheckRadioButton (hwnd, IDC_VTXWINDOWS, IDC_VTPOORMAN,
		      cfg.vtmode == VT_XWINDOWS ? IDC_VTXWINDOWS :
		      cfg.vtmode == VT_OEMANSI ? IDC_VTOEMANSI :
		      cfg.vtmode == VT_OEMONLY ? IDC_VTOEMONLY :
		      IDC_VTPOORMAN);
}

static void hide(HWND hwnd, int hide, int minid, int maxid) {
    int i;
    for (i = minid; i < maxid; i++) {
	HWND ctl = GetDlgItem(hwnd, i);
	if (ctl) {
	    ShowWindow(ctl, hide ? SW_HIDE : SW_SHOW);
	}
    }
}

struct treeview_faff {
    HWND treeview;
    HTREEITEM lastat[4];
};

static HTREEITEM treeview_insert(struct treeview_faff *faff,
                                 int level, char *text) {
    TVINSERTSTRUCT ins;
    int i;
    HTREEITEM newitem;
    ins.hParent = (level > 0 ? faff->lastat[level-1] : TVI_ROOT);
    ins.hInsertAfter = faff->lastat[level];
#if _WIN32_IE >= 0x0400 && defined NONAMELESSUNION
#define INSITEM DUMMYUNIONNAME.item
#else
#define INSITEM item
#endif
    ins.INSITEM.mask = TVIF_TEXT;
    ins.INSITEM.pszText = text;
    newitem = TreeView_InsertItem(faff->treeview, &ins);
    if (level > 0)
        TreeView_Expand(faff->treeview, faff->lastat[level-1], TVE_EXPAND);
    faff->lastat[level] = newitem;
    for (i = level+1; i < 4; i++) faff->lastat[i] = NULL;
    return newitem;
}

/*
 * This _huge_ function is the configuration box.
 */
static int GenericMainDlgProc (HWND hwnd, UINT msg,
			       WPARAM wParam, LPARAM lParam,
			       int dlgtype) {
    HWND hw, treeview;
    struct treeview_faff tvfaff;
    HTREEITEM hsession;
    OPENFILENAME of;
    char filename[sizeof(cfg.keyfile)];
    CHOOSEFONT cf;
    LOGFONT lf;
    char fontstatic[256];
    int i;

    switch (msg) {
      case WM_INITDIALOG:
	SetWindowLong(hwnd, GWL_USERDATA, 0);
	/*
	 * Centre the window.
	 */
	{			       /* centre the window */
	    RECT rs, rd;

	    hw = GetDesktopWindow();
	    if (GetWindowRect (hw, &rs) && GetWindowRect (hwnd, &rd))
		MoveWindow (hwnd, (rs.right + rs.left + rd.left - rd.right)/2,
			    (rs.bottom + rs.top + rd.top - rd.bottom)/2,
			    rd.right-rd.left, rd.bottom-rd.top, TRUE);
	}

	/*
	 * Create the tree view.
	 */
        {
            RECT r;
	    WPARAM font;
            HWND tvstatic;

            r.left = 3; r.right = r.left + 75;
            r.top = 3; r.bottom = r.top + 10;
            MapDialogRect(hwnd, &r);
            tvstatic = CreateWindowEx(0, "STATIC", "Cate&gory:",
                                      WS_CHILD | WS_VISIBLE,
                                      r.left, r.top,
                                      r.right-r.left, r.bottom-r.top,
                                      hwnd, (HMENU)IDCX_TVSTATIC, hinst, NULL);
	    font = SendMessage(hwnd, WM_GETFONT, 0, 0);
	    SendMessage(tvstatic, WM_SETFONT, font, MAKELPARAM(TRUE, 0));

            r.left = 3; r.right = r.left + 75;
            r.top = 13; r.bottom = r.top + 196;
            MapDialogRect(hwnd, &r);
            treeview = CreateWindowEx(WS_EX_CLIENTEDGE, WC_TREEVIEW, "",
                                      WS_CHILD | WS_VISIBLE |
                                      WS_TABSTOP | TVS_HASLINES |
                                      TVS_DISABLEDRAGDROP | TVS_HASBUTTONS |
                                      TVS_LINESATROOT | TVS_SHOWSELALWAYS,
                                      r.left, r.top,
                                      r.right-r.left, r.bottom-r.top,
                                      hwnd, (HMENU)IDCX_TREEVIEW, hinst, NULL);
	    font = SendMessage(hwnd, WM_GETFONT, 0, 0);
	    SendMessage(treeview, WM_SETFONT, font, MAKELPARAM(TRUE, 0));
            tvfaff.treeview = treeview;
            memset(tvfaff.lastat, 0, sizeof(tvfaff.lastat));
        }

	/*
	 * Create the various panelfuls of controls.
	 */

	/* The Session panel. Accelerators used: [acgo] nprthelsdx */
	{
	    struct ctlpos cp;
	    ctlposinit(&cp, hwnd, 80, 3, 13);
            bartitle(&cp, "Basic options for your PuTTY session",
                     IDC_TITLE_SESSION);
	    if (dlgtype == 0) {
                beginbox(&cp, "Specify your connection by host name",
                         IDC_BOX_SESSION1, IDC_BOXT_SESSION1);
		multiedit(&cp,
			  "Host &Name", IDC_HOSTSTATIC, IDC_HOST, 75,
			  "&Port", IDC_PORTSTATIC, IDC_PORT, 25, NULL);
		if (backends[2].backend == NULL) {
		    /* this is PuTTYtel, so only two protocols available */
		    radioline(&cp, "Protocol:", IDC_PROTSTATIC, 3,
			      "&Raw", IDC_PROTRAW,
			      "&Telnet", IDC_PROTTELNET, NULL);
		} else {
		    radioline(&cp, "Protocol:", IDC_PROTSTATIC, 3,
			      "&Raw", IDC_PROTRAW,
			      "&Telnet", IDC_PROTTELNET,
#ifdef FWHACK
			      "SS&H/hack",
#else
			      "SS&H",
#endif
			      IDC_PROTSSH, NULL);
		}
                endbox(&cp);
                beginbox(&cp, "Load, save or delete a stored session",
                         IDC_BOX_SESSION2, IDC_BOXT_SESSION2);
		sesssaver(&cp, "Stor&ed Sessions",
			  IDC_SESSSTATIC, IDC_SESSEDIT, IDC_SESSLIST,
			  "&Load", IDC_SESSLOAD,
			  "&Save", IDC_SESSSAVE,
			  "&Delete", IDC_SESSDEL, NULL);
                endbox(&cp);
	    }
            beginbox(&cp, NULL, IDC_BOX_SESSION3, 0);
	    checkbox(&cp, "Close Window on E&xit", IDC_CLOSEEXIT);
            endbox(&cp);

            hsession = treeview_insert(&tvfaff, 0, "Session");
	}

        /* The Terminal panel. Accelerators used: [acgo] rmkh&dlbenu */
	{
	    struct ctlpos cp;
	    ctlposinit(&cp, hwnd, 80, 3, 13);
            bartitle(&cp, "Options controlling the terminal emulation",
                     IDC_TITLE_TERMINAL);
            beginbox(&cp, "Set the size of the terminal window",
                     IDC_BOX_TERMINAL1, IDC_BOXT_TERMINAL1);
	    multiedit(&cp,
		      "&Rows", IDC_ROWSSTATIC, IDC_ROWSEDIT, 50,
		      "Colu&mns", IDC_COLSSTATIC, IDC_COLSEDIT, 50,
		      NULL);
	    checkbox(&cp, "Loc&k window size against resizing", IDC_LOCKSIZE);
            endbox(&cp);
            beginbox(&cp, "Set the font used in the terminal window",
                     IDC_BOX_TERMINAL2, IDC_BOXT_TERMINAL2);
	    staticbtn(&cp, "", IDC_FONTSTATIC, "C&hange...", IDC_CHOOSEFONT);
            endbox(&cp);
            beginbox(&cp, "Set various terminal options",
                     IDC_BOX_TERMINAL3, IDC_BOXT_TERMINAL3);
	    checkbox(&cp, "Auto &wrap mode initially on", IDC_WRAPMODE);
	    checkbox(&cp, "&DEC Origin Mode initially on", IDC_DECOM);
	    checkbox(&cp, "Implicit CR in every &LF", IDC_LFHASCR);
	    checkbox(&cp, "&Beep enabled", IDC_BEEP);
	    checkbox(&cp, "Use background colour to &erase screen", IDC_BCE);
	    checkbox(&cp, "Enable bli&nking text", IDC_BLINKTEXT);
	    checkbox(&cp, "&Use local terminal line discipline", IDC_LDISCTERM);
            endbox(&cp);

            treeview_insert(&tvfaff, 0, "Terminal");
	}

	/* The Keyboard panel. Accelerators used: [acgo] h?srvlxvnpmie */
	{
	    struct ctlpos cp;
	    ctlposinit(&cp, hwnd, 80, 3, 13);
            bartitle(&cp, "Options controlling the effects of keys",
                     IDC_TITLE_KEYBOARD);
            beginbox(&cp, "Change the sequences sent by:",
                     IDC_BOX_KEYBOARD1, IDC_BOXT_KEYBOARD1);
	    radioline(&cp, "The Backspace key", IDC_DELSTATIC, 2,
		      "Control-&H", IDC_DEL008,
		      "Control-&? (127)", IDC_DEL127, NULL);
	    radioline(&cp, "The Home and End keys", IDC_HOMESTATIC, 2,
		      "&Standard", IDC_HOMETILDE,
		      "&rxvt", IDC_HOMERXVT, NULL);
	    radioline(&cp, "The Function keys and keypad", IDC_FUNCSTATIC, 4,
		      "ESC[n&~", IDC_FUNCTILDE,
		      "&Linux", IDC_FUNCLINUX,
		      "&Xterm R6", IDC_FUNCXTERM,
                      "&VT400", IDC_FUNCVT400, NULL);
            endbox(&cp);
            beginbox(&cp, "Change the initial state of:",
                     IDC_BOX_KEYBOARD2, IDC_BOXT_KEYBOARD2);
	    radioline(&cp, "Initial state of cursor keys:", IDC_CURSTATIC, 2,
		      "&Normal", IDC_CURNORMAL,
		      "A&pplication", IDC_CURAPPLIC, NULL);
	    radioline(&cp, "Initial state of numeric keypad:", IDC_KPSTATIC, 3,
		      "Nor&mal", IDC_KPNORMAL,
		      "Appl&ication", IDC_KPAPPLIC,
		      "N&etHack", IDC_KPNH, NULL);
            endbox(&cp);

            treeview_insert(&tvfaff, 1, "Keyboard");
	}

        /* The Window panel. Accelerators used: [acgo] tibsdkw4y */
	{
	    struct ctlpos cp;
	    ctlposinit(&cp, hwnd, 80, 3, 13);
            bartitle(&cp, "Options controlling PuTTY's window",
                     IDC_TITLE_WINDOW);
            beginbox(&cp, "Adjust the use of the window title",
                     IDC_BOX_WINDOW1, IDC_BOXT_WINDOW1);
	    if (dlgtype == 0)
		multiedit(&cp,
			  "Initial window &title:", IDC_WINTITLE,
			  IDC_WINEDIT, 100, NULL);
	    checkbox(&cp, "Avoid ever using &icon title", IDC_WINNAME);
            endbox(&cp);
            beginbox(&cp, "Adjust the use of the cursor",
                     IDC_BOX_WINDOW2, IDC_BOXT_WINDOW2);
	    checkbox(&cp, "Cursor &blinks", IDC_BLINKCUR);
            endbox(&cp);
            beginbox(&cp, "Control the scrollback in the window",
                     IDC_BOX_WINDOW3, IDC_BOXT_WINDOW3);
            staticedit(&cp, "Lines of &scrollback",
                       IDC_SAVESTATIC, IDC_SAVEEDIT, 50);
	    checkbox(&cp, "&Display scrollbar", IDC_SCROLLBAR);
	    checkbox(&cp, "Reset scrollback on &keypress", IDC_SCROLLKEY);
            endbox(&cp);
            beginbox(&cp, NULL, IDC_BOX_WINDOW4, 0);
	    checkbox(&cp, "&Warn before closing window", IDC_CLOSEWARN);
	    checkbox(&cp, "Window closes on ALT-F&4", IDC_ALTF4);
	    checkbox(&cp, "S&ystem menu appears on ALT-Space)", IDC_ALTSPACE);
            endbox(&cp);

            treeview_insert(&tvfaff, 0, "Window");
	}

	/* The Translation panel. Accelerators used: [acgo] xbepnkis */
	{
	    struct ctlpos cp;
	    ctlposinit(&cp, hwnd, 80, 3, 13);
            bartitle(&cp, "Options controlling character set translation",
                     IDC_TITLE_TRANSLATION);
            beginbox(&cp, "Adjust how PuTTY displays line drawing characters",
                     IDC_BOX_TRANSLATION1, IDC_BOXT_TRANSLATION1);
	    radiobig(&cp,
		     "Handling of line drawing characters:", IDC_VTSTATIC,
		     "Font has &XWindows encoding", IDC_VTXWINDOWS,
		     "Use font in &both ANSI and OEM modes", IDC_VTOEMANSI,
		     "Use font in O&EM mode only", IDC_VTOEMONLY,
		     "&Poor man's line drawing (""+"", ""-"" and ""|"")",
		     IDC_VTPOORMAN, NULL);
            endbox(&cp);
            beginbox(&cp, "Enable character set translation on received data",
                     IDC_BOX_TRANSLATION2, IDC_BOXT_TRANSLATION2);
	    radiobig(&cp,
		     "Character set translation:", IDC_XLATSTATIC,
		     "&None", IDC_NOXLAT,
		     "&KOI8 / Win-1251", IDC_KOI8WIN1251,
		     "&ISO-8859-2 / Win-1250", IDC_88592WIN1250,
                     "&ISO-8859-2 / CP852", IDC_88592CP852, NULL);
            endbox(&cp);
            beginbox(&cp, "Enable character set translation on input data",
                     IDC_BOX_TRANSLATION3, IDC_BOXT_TRANSLATION3);
	    checkbox(&cp, "CAP&S LOCK acts as cyrillic switch",
                     IDC_CAPSLOCKCYR);
            endbox(&cp);

            treeview_insert(&tvfaff, 1, "Translation");
	}

        /* The Selection panel. Accelerators used: [acgo] wxst */
	{
	    struct ctlpos cp;
	    ctlposinit(&cp, hwnd, 80, 3, 13);
            bartitle(&cp, "Options controlling copy and paste",
                     IDC_TITLE_SELECTION);
            beginbox(&cp, "Control which mouse button does which thing",
                     IDC_BOX_SELECTION1, IDC_BOXT_SELECTION1);
	    radiobig(&cp, "Action of mouse buttons:", IDC_MBSTATIC,
		     "&Windows (Right pastes, Middle extends)", IDC_MBWINDOWS,
		     "&xterm (Right extends, Middle pastes)", IDC_MBXTERM,
		     NULL);
            endbox(&cp);
            beginbox(&cp, "Control the select-one-word-at-a-time mode",
                     IDC_BOX_SELECTION2, IDC_BOXT_SELECTION2);
	    charclass(&cp, "Character classes:", IDC_CCSTATIC, IDC_CCLIST,
		      "&Set", IDC_CCSET, IDC_CCEDIT,
		      "&to class", IDC_CCSTATIC2);
            endbox(&cp);

            treeview_insert(&tvfaff, 1, "Selection");
	}

        /* The Colours panel. Accelerators used: [acgo] blum */
	{
	    struct ctlpos cp;
	    ctlposinit(&cp, hwnd, 80, 3, 13);
            bartitle(&cp, "Options controlling use of colours",
                     IDC_TITLE_COLOURS);
            beginbox(&cp, "General options for colour usage",
                     IDC_BOX_COLOURS1, IDC_BOXT_COLOURS1);
	    checkbox(&cp, "&Bolded text is a different colour", IDC_BOLDCOLOUR);
	    checkbox(&cp, "Attempt to use &logical palettes", IDC_PALETTE);
            endbox(&cp);
            beginbox(&cp, "Adjust the precise colours PuTTY displays",
                     IDC_BOX_COLOURS2, IDC_BOXT_COLOURS2);
	    colouredit(&cp, "Select a colo&ur and then click to modify it:",
		       IDC_STATIC, IDC_LIST,
		       "&Modify...", IDC_CHANGE,
		       "Red:", IDC_RSTATIC, IDC_RVALUE,
		       "Green:", IDC_GSTATIC, IDC_GVALUE,
		       "Blue:", IDC_BSTATIC, IDC_BVALUE, NULL);
            endbox(&cp);

            treeview_insert(&tvfaff, 1, "Colours");
	}

        /* The Connection panel. Accelerators used: [acgo] tuk */
        {
	    struct ctlpos cp;
	    ctlposinit(&cp, hwnd, 80, 3, 13);
            bartitle(&cp, "Options controlling the connection", IDC_TITLE_CONNECTION);
	    if (dlgtype == 0) {
                beginbox(&cp, "Data to send to the server",
                         IDC_BOX_CONNECTION1, IDC_BOXT_CONNECTION1);
                staticedit(&cp, "Terminal-&type string", IDC_TTSTATIC, IDC_TTEDIT, 50);
                staticedit(&cp, "Auto-login &username", IDC_LOGSTATIC, IDC_LOGEDIT, 50);
                endbox(&cp);
            }
            beginbox(&cp, "Sending of null packets to keep session active",
                     IDC_BOX_CONNECTION2, IDC_BOXT_CONNECTION2);
            staticedit(&cp, "Minutes between &keepalives (0 to turn off)",
                       IDC_PINGSTATIC, IDC_PINGEDIT, 25);
            endbox(&cp);

            treeview_insert(&tvfaff, 0, "Connection");
        }

        /* The Telnet panel. Accelerators used: [acgo] svldrbf */
	{
	    struct ctlpos cp;
	    ctlposinit(&cp, hwnd, 80, 3, 13);
	    if (dlgtype == 0) {
                bartitle(&cp, "Options controlling Telnet connections", IDC_TITLE_TELNET);
                beginbox(&cp, "Data to send to the server",
                         IDC_BOX_TELNET1, IDC_BOXT_TELNET1);
		staticedit(&cp, "Terminal-&speed string", IDC_TSSTATIC, IDC_TSEDIT, 50);
		envsetter(&cp, "Environment variables:", IDC_ENVSTATIC,
			  "&Variable", IDC_VARSTATIC, IDC_VAREDIT,
			  "Va&lue", IDC_VALSTATIC, IDC_VALEDIT,
			  IDC_ENVLIST,
			  "A&dd", IDC_ENVADD, "&Remove", IDC_ENVREMOVE);
                endbox(&cp);
                beginbox(&cp, "Telnet protocol adjustments",
                         IDC_BOX_TELNET2, IDC_BOXT_TELNET2);
		radioline(&cp, "Handling of OLD_ENVIRON ambiguity:", IDC_EMSTATIC, 2,
			  "&BSD (commonplace)", IDC_EMBSD,
			  "R&FC 1408 (unusual)", IDC_EMRFC, NULL);
                endbox(&cp);

                treeview_insert(&tvfaff, 1, "Telnet");
	    }
	}

	/* The SSH panel. Accelerators used: [acgo] rmakwp123bd */
	{
	    struct ctlpos cp;
	    ctlposinit(&cp, hwnd, 80, 3, 13);
	    if (dlgtype == 0) {
                bartitle(&cp, "Options controlling SSH connections", IDC_TITLE_SSH);
                beginbox(&cp, "Data to send to the server",
                         IDC_BOX_SSH1, IDC_BOXT_SSH1);
		multiedit(&cp,
			  "&Remote command:", IDC_CMDSTATIC, IDC_CMDEDIT, 100,
			  NULL);
                endbox(&cp);
                beginbox(&cp, "Authentication options",
                         IDC_BOX_SSH2, IDC_BOXT_SSH2);
		checkbox(&cp, "Atte&mpt TIS or CryptoCard authentication",
			 IDC_AUTHTIS);
		checkbox(&cp, "Allow &agent forwarding", IDC_AGENTFWD);
		editbutton(&cp, "Private &key file for authentication:",
			   IDC_PKSTATIC, IDC_PKEDIT, "Bro&wse...", IDC_PKBUTTON);
                endbox(&cp);
                beginbox(&cp, "Protocol options",
                         IDC_BOX_SSH3, IDC_BOXT_SSH3);
		checkbox(&cp, "Don't allocate a &pseudo-terminal", IDC_NOPTY);
		radioline(&cp, "Preferred SSH protocol version:",
			  IDC_SSHPROTSTATIC, 2,
			  "&1", IDC_SSHPROT1, "&2", IDC_SSHPROT2, NULL);
		radioline(&cp, "Preferred encryption algorithm:", IDC_CIPHERSTATIC, 3,
			  "&3DES", IDC_CIPHER3DES,
			  "&Blowfish", IDC_CIPHERBLOWF,
			  "&DES", IDC_CIPHERDES, NULL);
                endbox(&cp);

                treeview_insert(&tvfaff, 1, "SSH");
	    }
	}

	init_dlg_ctrls(hwnd);
        for (i = 0; i < nsessions; i++)
            SendDlgItemMessage (hwnd, IDC_SESSLIST, LB_ADDSTRING,
                                0, (LPARAM) (sessions[i]));

        /*
         * Hide all the controls to start with.
         */
	hide(hwnd, TRUE, controlstartvalue, controlendvalue);

        /*
         * Put the treeview selection on to the Session panel. This
         * should also cause unhiding of the relevant controls.
         */
        TreeView_SelectItem(treeview, hsession);

        /*
         * Set focus into the first available control.
         */
        {
            HWND ctl;
            ctl = GetDlgItem(hwnd, IDC_HOST);
            if (!ctl) ctl = GetDlgItem(hwnd, IDC_CLOSEEXIT);
            SetFocus(ctl);
        }

	SetWindowLong(hwnd, GWL_USERDATA, 1);
	return 0;
      case WM_LBUTTONUP:
        /*
         * Button release should trigger WM_OK if there was a
         * previous double click on the session list.
         */
        ReleaseCapture();
        if (readytogo)
            SendMessage (hwnd, WM_COMMAND, IDOK, 0);
        break;
      case WM_NOTIFY:
	if (LOWORD(wParam) == IDCX_TREEVIEW &&
	    ((LPNMHDR)lParam)->code == TVN_SELCHANGED) {
	    HTREEITEM i = TreeView_GetSelection(((LPNMHDR)lParam)->hwndFrom);
	    TVITEM item;
	    char buffer[64];
            item.hItem = i;
	    item.pszText = buffer;
	    item.cchTextMax = sizeof(buffer);
	    item.mask = TVIF_TEXT;
	    TreeView_GetItem(((LPNMHDR)lParam)->hwndFrom, &item);
	    hide(hwnd, TRUE, controlstartvalue, controlendvalue);
	    if (!strcmp(buffer, "Session"))
		hide(hwnd, FALSE, sessionpanelstart, sessionpanelend);
	    if (!strcmp(buffer, "Keyboard"))
		hide(hwnd, FALSE, keyboardpanelstart, keyboardpanelend);
	    if (!strcmp(buffer, "Terminal"))
		hide(hwnd, FALSE, terminalpanelstart, terminalpanelend);
	    if (!strcmp(buffer, "Window"))
		hide(hwnd, FALSE, windowpanelstart, windowpanelend);
	    if (!strcmp(buffer, "Connection"))
		hide(hwnd, FALSE, connectionpanelstart, connectionpanelend);
	    if (!strcmp(buffer, "Telnet"))
		hide(hwnd, FALSE, telnetpanelstart, telnetpanelend);
	    if (!strcmp(buffer, "SSH"))
		hide(hwnd, FALSE, sshpanelstart, sshpanelend);
	    if (!strcmp(buffer, "Selection"))
		hide(hwnd, FALSE, selectionpanelstart, selectionpanelend);
	    if (!strcmp(buffer, "Colours"))
		hide(hwnd, FALSE, colourspanelstart, colourspanelend);
	    if (!strcmp(buffer, "Translation"))
		hide(hwnd, FALSE, translationpanelstart, translationpanelend);

	    SetFocus (((LPNMHDR)lParam)->hwndFrom);   /* ensure focus stays */
	    return 0;
	}
	break;
      case WM_COMMAND:
	/*
	 * Only process WM_COMMAND once the dialog is fully formed.
	 */
	if (GetWindowLong(hwnd, GWL_USERDATA) == 1) switch (LOWORD(wParam)) {
	  case IDOK:
	    if (*cfg.host)
		EndDialog (hwnd, 1);
	    else
		MessageBeep (0);
	    return 0;
	  case IDCANCEL:
	    EndDialog (hwnd, 0);
	    return 0;
	  case IDC_PROTTELNET:
	  case IDC_PROTSSH:
	  case IDC_PROTRAW:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		int i = IsDlgButtonChecked (hwnd, IDC_PROTSSH);
		int j = IsDlgButtonChecked (hwnd, IDC_PROTTELNET);
		cfg.protocol = i ? PROT_SSH : j ? PROT_TELNET : PROT_RAW ;
		if ((cfg.protocol == PROT_SSH && cfg.port == 23) ||
		    (cfg.protocol == PROT_TELNET && cfg.port == 22)) {
		    cfg.port = i ? 22 : 23;
		    SetDlgItemInt (hwnd, IDC_PORT, cfg.port, FALSE);
		}
	    }
	    break;
	  case IDC_HOST:
	    if (HIWORD(wParam) == EN_CHANGE)
		GetDlgItemText (hwnd, IDC_HOST, cfg.host,
				sizeof(cfg.host)-1);
	    break;
	  case IDC_PORT:
	    if (HIWORD(wParam) == EN_CHANGE)
		MyGetDlgItemInt (hwnd, IDC_PORT, &cfg.port);
	    break;
	  case IDC_SESSEDIT:
	    if (HIWORD(wParam) == EN_CHANGE) {
		SendDlgItemMessage (hwnd, IDC_SESSLIST, LB_SETCURSEL,
				    (WPARAM) -1, 0);
                GetDlgItemText (hwnd, IDC_SESSEDIT,
                                savedsession, sizeof(savedsession)-1);
                savedsession[sizeof(savedsession)-1] = '\0';
            }
	    break;
	  case IDC_SESSSAVE:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		/*
		 * Save a session
		 */
		char str[2048];
		GetDlgItemText (hwnd, IDC_SESSEDIT, str, sizeof(str)-1);
		if (!*str) {
		    int n = SendDlgItemMessage (hwnd, IDC_SESSLIST,
						LB_GETCURSEL, 0, 0);
		    if (n == LB_ERR) {
			MessageBeep(0);
			break;
		    }
		    strcpy (str, sessions[n]);
		}
		save_settings (str, !!strcmp(str, "Default Settings"), &cfg);
		get_sesslist (FALSE);
		get_sesslist (TRUE);
		SendDlgItemMessage (hwnd, IDC_SESSLIST, LB_RESETCONTENT,
				    0, 0);
		for (i = 0; i < nsessions; i++)
		    SendDlgItemMessage (hwnd, IDC_SESSLIST, LB_ADDSTRING,
					0, (LPARAM) (sessions[i]));
		SendDlgItemMessage (hwnd, IDC_SESSLIST, LB_SETCURSEL,
				    (WPARAM) -1, 0);
	    }
	    break;
	  case IDC_SESSLIST:
	  case IDC_SESSLOAD:
	    if (LOWORD(wParam) == IDC_SESSLOAD &&
		HIWORD(wParam) != BN_CLICKED &&
		HIWORD(wParam) != BN_DOUBLECLICKED)
		break;
	    if (LOWORD(wParam) == IDC_SESSLIST &&
		HIWORD(wParam) != LBN_DBLCLK)
		break;
	    {
		int n = SendDlgItemMessage (hwnd, IDC_SESSLIST,
					    LB_GETCURSEL, 0, 0);
                int isdef;
		if (n == LB_ERR) {
		    MessageBeep(0);
		    break;
		}
                isdef = !strcmp(sessions[n], "Default Settings");
		load_settings (sessions[n], !isdef, &cfg);
		init_dlg_ctrls(hwnd);
                if (!isdef)
                    SetDlgItemText(hwnd, IDC_SESSEDIT, sessions[n]);
	    }
	    if (LOWORD(wParam) == IDC_SESSLIST) {
		/*
		 * A double-click on a saved session should
		 * actually start the session, not just load it.
		 * Unless it's Default Settings or some other
		 * host-less set of saved settings.
		 */
		if (*cfg.host) {
                    readytogo = TRUE;
                    SetCapture(hwnd);
                }
	    }
	    break;
	  case IDC_SESSDEL:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		int n = SendDlgItemMessage (hwnd, IDC_SESSLIST,
					    LB_GETCURSEL, 0, 0);
		if (n == LB_ERR || n == 0) {
		    MessageBeep(0);
		    break;
		}
		del_settings(sessions[n]);
		get_sesslist (FALSE);
		get_sesslist (TRUE);
		SendDlgItemMessage (hwnd, IDC_SESSLIST, LB_RESETCONTENT,
				    0, 0);
		for (i = 0; i < nsessions; i++)
		    SendDlgItemMessage (hwnd, IDC_SESSLIST, LB_ADDSTRING,
					0, (LPARAM) (sessions[i]));
		SendDlgItemMessage (hwnd, IDC_SESSLIST, LB_SETCURSEL,
				    (WPARAM) -1, 0);
	    }
          case IDC_PINGEDIT:
            if (HIWORD(wParam) == EN_CHANGE)
                MyGetDlgItemInt (hwnd, IDC_PINGEDIT, &cfg.ping_interval);
            break;
	  case IDC_DEL008:
	  case IDC_DEL127:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.bksp_is_delete = IsDlgButtonChecked (hwnd, IDC_DEL127);
	    break;
	  case IDC_HOMETILDE:
	  case IDC_HOMERXVT:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.rxvt_homeend = IsDlgButtonChecked (hwnd, IDC_HOMERXVT);
	    break;
	  case IDC_FUNCXTERM:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.funky_type = 2;
	    break;
	  case IDC_FUNCVT400:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.funky_type = 3;
	    break;
	  case IDC_FUNCTILDE:
	  case IDC_FUNCLINUX:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.funky_type = IsDlgButtonChecked (hwnd, IDC_FUNCLINUX);
	    break;
	  case IDC_KPNORMAL:
	  case IDC_KPAPPLIC:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		cfg.app_keypad = IsDlgButtonChecked (hwnd, IDC_KPAPPLIC);
		cfg.nethack_keypad = FALSE;
	    }
	    break;
	  case IDC_KPNH:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		cfg.app_keypad = FALSE;
		cfg.nethack_keypad = TRUE;
	    }
	    break;
	  case IDC_CURNORMAL:
	  case IDC_CURAPPLIC:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.app_cursor = IsDlgButtonChecked (hwnd, IDC_CURAPPLIC);
	    break;
	  case IDC_ALTF4:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.alt_f4 = IsDlgButtonChecked (hwnd, IDC_ALTF4);
	    break;
	  case IDC_ALTSPACE:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.alt_space = IsDlgButtonChecked (hwnd, IDC_ALTSPACE);
	    break;
	  case IDC_LDISCTERM:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.ldisc_term = IsDlgButtonChecked (hwnd, IDC_LDISCTERM);
	    break;
	  case IDC_SCROLLKEY:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.scroll_on_key = IsDlgButtonChecked (hwnd, IDC_SCROLLKEY);
	    break;
	  case IDC_WRAPMODE:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.wrap_mode = IsDlgButtonChecked (hwnd, IDC_WRAPMODE);
	    break;
	  case IDC_DECOM:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.dec_om = IsDlgButtonChecked (hwnd, IDC_DECOM);
	    break;
	  case IDC_LFHASCR:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.lfhascr = IsDlgButtonChecked (hwnd, IDC_LFHASCR);
	    break;
	  case IDC_ROWSEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
		MyGetDlgItemInt (hwnd, IDC_ROWSEDIT, &cfg.height);
	    break;
	  case IDC_COLSEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
		MyGetDlgItemInt (hwnd, IDC_COLSEDIT, &cfg.width);
	    break;
	  case IDC_SAVEEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
		MyGetDlgItemInt (hwnd, IDC_SAVEEDIT, &cfg.savelines);
	    break;
	  case IDC_CHOOSEFONT:
	    lf.lfHeight = cfg.fontheight;
	    lf.lfWidth = lf.lfEscapement = lf.lfOrientation = 0;
	    lf.lfItalic = lf.lfUnderline = lf.lfStrikeOut = 0;
	    lf.lfWeight = (cfg.fontisbold ? FW_BOLD : 0);
	    lf.lfCharSet = cfg.fontcharset;
	    lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
	    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
	    lf.lfQuality = DEFAULT_QUALITY;
	    lf.lfPitchAndFamily = FIXED_PITCH | FF_DONTCARE;
	    strncpy (lf.lfFaceName, cfg.font, sizeof(lf.lfFaceName)-1);
	    lf.lfFaceName[sizeof(lf.lfFaceName)-1] = '\0';

	    cf.lStructSize = sizeof(cf);
	    cf.hwndOwner = hwnd;
	    cf.lpLogFont = &lf;
	    cf.Flags = CF_FIXEDPITCHONLY | CF_FORCEFONTEXIST |
		CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS;

	    if (ChooseFont (&cf)) {
		strncpy (cfg.font, lf.lfFaceName, sizeof(cfg.font)-1);
		cfg.font[sizeof(cfg.font)-1] = '\0';
		cfg.fontisbold = (lf.lfWeight == FW_BOLD);
		cfg.fontcharset = lf.lfCharSet;
		cfg.fontheight = lf.lfHeight;
		fmtfont (fontstatic);
		SetDlgItemText (hwnd, IDC_FONTSTATIC, fontstatic);
	    }
	    break;
	  case IDC_BEEP:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.beep = IsDlgButtonChecked (hwnd, IDC_BEEP);
	    break;
	  case IDC_BLINKTEXT:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.blinktext = IsDlgButtonChecked (hwnd, IDC_BLINKTEXT);
	    break;
	  case IDC_BCE:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.bce = IsDlgButtonChecked (hwnd, IDC_BCE);
	    break;
	  case IDC_WINNAME:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.win_name_always = IsDlgButtonChecked (hwnd, IDC_WINNAME);
	    break;
          case IDC_BLINKCUR:
            if (HIWORD(wParam) == BN_CLICKED ||
                HIWORD(wParam) == BN_DOUBLECLICKED)
                cfg.blink_cur = IsDlgButtonChecked (hwnd, IDC_BLINKCUR);
            break;
          case IDC_SCROLLBAR:
            if (HIWORD(wParam) == BN_CLICKED ||
                HIWORD(wParam) == BN_DOUBLECLICKED)
                cfg.scrollbar = IsDlgButtonChecked (hwnd, IDC_SCROLLBAR);
            break;
          case IDC_LOCKSIZE:
	     if (HIWORD(wParam) == BN_CLICKED ||
		 HIWORD(wParam) == BN_DOUBLECLICKED)
                cfg.locksize = IsDlgButtonChecked (hwnd, IDC_LOCKSIZE);
            break;
	  case IDC_WINEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
		GetDlgItemText (hwnd, IDC_WINEDIT, cfg.wintitle,
				sizeof(cfg.wintitle)-1);
	    break;
	  case IDC_CLOSEEXIT:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.close_on_exit = IsDlgButtonChecked (hwnd, IDC_CLOSEEXIT);
	    break;
	  case IDC_CLOSEWARN:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.warn_on_close = IsDlgButtonChecked (hwnd, IDC_CLOSEWARN);
	    break;
	  case IDC_TTEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
	    GetDlgItemText (hwnd, IDC_TTEDIT, cfg.termtype,
			    sizeof(cfg.termtype)-1);
	    break;
	  case IDC_TSEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
		GetDlgItemText (hwnd, IDC_TSEDIT, cfg.termspeed,
				sizeof(cfg.termspeed)-1);
	    break;
	  case IDC_LOGEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
		GetDlgItemText (hwnd, IDC_LOGEDIT, cfg.username,
				sizeof(cfg.username)-1);
	    break;
	  case IDC_EMBSD:
	  case IDC_EMRFC:
	    cfg.rfc_environ = IsDlgButtonChecked (hwnd, IDC_EMRFC);
	    break;
	  case IDC_ENVADD:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
              char str[sizeof(cfg.environmt)];
		char *p;
		GetDlgItemText (hwnd, IDC_VAREDIT, str, sizeof(str)-1);
		if (!*str) {
		    MessageBeep(0);
		    break;
		}
		p = str + strlen(str);
		*p++ = '\t';
		GetDlgItemText (hwnd, IDC_VALEDIT, p, sizeof(str)-1-(p-str));
		if (!*p) {
		    MessageBeep(0);
		    break;
		}
              p = cfg.environmt;
		while (*p) {
		    while (*p) p++;
		    p++;
		}
              if ((p-cfg.environmt) + strlen(str) + 2 < sizeof(cfg.environmt)) {
		    strcpy (p, str);
		    p[strlen(str)+1] = '\0';
		    SendDlgItemMessage (hwnd, IDC_ENVLIST, LB_ADDSTRING,
					0, (LPARAM)str);
		    SetDlgItemText (hwnd, IDC_VAREDIT, "");
		    SetDlgItemText (hwnd, IDC_VALEDIT, "");
		} else {
		    MessageBox(hwnd, "Environment too big", "PuTTY Error",
			       MB_OK | MB_ICONERROR);
		}
	    }
	    break;
	  case IDC_ENVREMOVE:
	    if (HIWORD(wParam) != BN_CLICKED &&
		HIWORD(wParam) != BN_DOUBLECLICKED)
		break;
	    i = SendDlgItemMessage (hwnd, IDC_ENVLIST, LB_GETCURSEL, 0, 0);
	    if (i == LB_ERR)
		MessageBeep (0);
	    else {
		char *p, *q;

	        SendDlgItemMessage (hwnd, IDC_ENVLIST, LB_DELETESTRING,
				    i, 0);
              p = cfg.environmt;
		while (i > 0) {
		    if (!*p)
			goto disaster;
		    while (*p) p++;
		    p++;
		    i--;
		}
		q = p;
		if (!*p)
		    goto disaster;
		while (*p) p++;
		p++;
		while (*p) {
		    while (*p)
			*q++ = *p++;
		    *q++ = *p++;
		}
		*q = '\0';
		disaster:;
	    }
	    break;
	  case IDC_NOPTY:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.nopty = IsDlgButtonChecked (hwnd, IDC_NOPTY);
	    break;
	  case IDC_AGENTFWD:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.agentfwd = IsDlgButtonChecked (hwnd, IDC_AGENTFWD);
	    break;
	  case IDC_CIPHER3DES:
	  case IDC_CIPHERBLOWF:
	  case IDC_CIPHERDES:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		if (IsDlgButtonChecked (hwnd, IDC_CIPHER3DES))
		    cfg.cipher = CIPHER_3DES;
		else if (IsDlgButtonChecked (hwnd, IDC_CIPHERBLOWF))
		    cfg.cipher = CIPHER_BLOWFISH;
		else if (IsDlgButtonChecked (hwnd, IDC_CIPHERDES))
		    cfg.cipher = CIPHER_DES;
	    }
	    break;
	  case IDC_SSHPROT1:
	  case IDC_SSHPROT2:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		if (IsDlgButtonChecked (hwnd, IDC_SSHPROT1))
		    cfg.sshprot = 1;
		else if (IsDlgButtonChecked (hwnd, IDC_SSHPROT2))
		    cfg.sshprot = 2;
	    }
	    break;
	  case IDC_AUTHTIS:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.try_tis_auth = IsDlgButtonChecked (hwnd, IDC_AUTHTIS);
	    break;
	  case IDC_PKEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
		GetDlgItemText (hwnd, IDC_PKEDIT, cfg.keyfile,
				sizeof(cfg.keyfile)-1);
	    break;
	  case IDC_CMDEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
		GetDlgItemText (hwnd, IDC_CMDEDIT, cfg.remote_cmd,
				sizeof(cfg.remote_cmd)-1);
	    break;
	  case IDC_PKBUTTON:
            memset(&of, 0, sizeof(of));
#ifdef OPENFILENAME_SIZE_VERSION_400
            of.lStructSize = OPENFILENAME_SIZE_VERSION_400;
#else
            of.lStructSize = sizeof(of);
#endif
            of.hwndOwner = hwnd;
            of.lpstrFilter = "All Files\0*\0\0\0";
            of.lpstrCustomFilter = NULL;
            of.nFilterIndex = 1;
            of.lpstrFile = filename; strcpy(filename, cfg.keyfile);
            of.nMaxFile = sizeof(filename);
            of.lpstrFileTitle = NULL;
            of.lpstrInitialDir = NULL;
            of.lpstrTitle = "Select Public Key File";
            of.Flags = 0;
            if (GetOpenFileName(&of)) {
                strcpy(cfg.keyfile, filename);
                SetDlgItemText (hwnd, IDC_PKEDIT, cfg.keyfile);
            }
	    break;
	  case IDC_MBWINDOWS:
	  case IDC_MBXTERM:
	    cfg.mouse_is_xterm = IsDlgButtonChecked (hwnd, IDC_MBXTERM);
	    break;
	  case IDC_CCSET:
	    {
		BOOL ok;
		int i;
		int n = GetDlgItemInt (hwnd, IDC_CCEDIT, &ok, FALSE);

		if (!ok)
		    MessageBeep (0);
		else {
		    for (i=0; i<256; i++)
			if (SendDlgItemMessage (hwnd, IDC_CCLIST, LB_GETSEL,
						i, 0)) {
			    char str[100];
			    cfg.wordness[i] = n;
			    SendDlgItemMessage (hwnd, IDC_CCLIST,
						LB_DELETESTRING, i, 0);
			    sprintf(str, "%d\t(0x%02X)\t%c\t%d", i, i,
				    (i>=0x21 && i != 0x7F) ? i : ' ',
				    cfg.wordness[i]);
			    SendDlgItemMessage (hwnd, IDC_CCLIST,
						LB_INSERTSTRING, i,
						(LPARAM)str);
			}
		}
	    }
	    break;
	  case IDC_BOLDCOLOUR:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		int n, i;
		cfg.bold_colour = IsDlgButtonChecked (hwnd, IDC_BOLDCOLOUR);
		n = SendDlgItemMessage (hwnd, IDC_LIST, LB_GETCOUNT, 0, 0);
		if (cfg.bold_colour && n!=22) {
		    for (i=0; i<22; i++)
			if (!permcolour[i])
			    SendDlgItemMessage (hwnd, IDC_LIST,
						LB_INSERTSTRING, i,
						(LPARAM) colours[i]);
		} else if (!cfg.bold_colour && n!=12) {
		    for (i=22; i-- ;)
			if (!permcolour[i])
			    SendDlgItemMessage (hwnd, IDC_LIST,
						LB_DELETESTRING, i, 0);
		}
	    }
	    break;
	  case IDC_PALETTE:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.try_palette = IsDlgButtonChecked (hwnd, IDC_PALETTE);
	    break;
	  case IDC_LIST:
	    if (HIWORD(wParam) == LBN_DBLCLK ||
		HIWORD(wParam) == LBN_SELCHANGE) {
		int i = SendDlgItemMessage (hwnd, IDC_LIST, LB_GETCURSEL,
					    0, 0);
		if (!cfg.bold_colour)
		    i = (i < 3 ? i*2 : i == 3 ? 5 : i*2-2);
		SetDlgItemInt (hwnd, IDC_RVALUE, cfg.colours[i][0], FALSE);
		SetDlgItemInt (hwnd, IDC_GVALUE, cfg.colours[i][1], FALSE);
		SetDlgItemInt (hwnd, IDC_BVALUE, cfg.colours[i][2], FALSE);
	    }
	    break;
	  case IDC_CHANGE:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		static CHOOSECOLOR cc;
		static DWORD custom[16] = {0};   /* zero initialisers */
		int i = SendDlgItemMessage (hwnd, IDC_LIST, LB_GETCURSEL,
					    0, 0);
		if (!cfg.bold_colour)
		    i = (i < 3 ? i*2 : i == 3 ? 5 : i*2-2);
		cc.lStructSize = sizeof(cc);
		cc.hwndOwner = hwnd;
		cc.hInstance = (HWND)hinst;
		cc.lpCustColors = custom;
		cc.rgbResult = RGB (cfg.colours[i][0], cfg.colours[i][1],
				    cfg.colours[i][2]);
		cc.Flags = CC_FULLOPEN | CC_RGBINIT;
		if (ChooseColor(&cc)) {
		    cfg.colours[i][0] =
			(unsigned char) (cc.rgbResult & 0xFF);
		    cfg.colours[i][1] =
			(unsigned char) (cc.rgbResult >> 8) & 0xFF;
		    cfg.colours[i][2] =
			(unsigned char) (cc.rgbResult >> 16) & 0xFF;
		    SetDlgItemInt (hwnd, IDC_RVALUE, cfg.colours[i][0],
				   FALSE);
		    SetDlgItemInt (hwnd, IDC_GVALUE, cfg.colours[i][1],
				   FALSE);
		    SetDlgItemInt (hwnd, IDC_BVALUE, cfg.colours[i][2],
				   FALSE);
		}
	    }
	    break;
	  case IDC_NOXLAT:
	  case IDC_KOI8WIN1251:
	  case IDC_88592WIN1250:
	  case IDC_88592CP852:
	    cfg.xlat_enablekoiwin =
		IsDlgButtonChecked (hwnd, IDC_KOI8WIN1251);
	    cfg.xlat_88592w1250 =
		IsDlgButtonChecked (hwnd, IDC_88592WIN1250);
	    cfg.xlat_88592cp852 =
		IsDlgButtonChecked (hwnd, IDC_88592CP852);
	    break;
	  case IDC_CAPSLOCKCYR:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		cfg.xlat_capslockcyr =
		    IsDlgButtonChecked (hwnd, IDC_CAPSLOCKCYR);
	    }
	    break;
	  case IDC_VTXWINDOWS:
	  case IDC_VTOEMANSI:
	  case IDC_VTOEMONLY:
	  case IDC_VTPOORMAN:
	    cfg.vtmode =
		(IsDlgButtonChecked (hwnd, IDC_VTXWINDOWS) ? VT_XWINDOWS :
		 IsDlgButtonChecked (hwnd, IDC_VTOEMANSI) ? VT_OEMANSI :
		 IsDlgButtonChecked (hwnd, IDC_VTOEMONLY) ? VT_OEMONLY :
		 VT_POORMAN);
	    break;
	}
	return 0;
      case WM_CLOSE:
	EndDialog (hwnd, 0);
	return 0;

	/* Grrr Explorer will maximize Dialogs! */
      case WM_SIZE:
	if (wParam == SIZE_MAXIMIZED)
	   force_normal(hwnd);
	return 0;
    }
    return 0;
}

static int CALLBACK MainDlgProc (HWND hwnd, UINT msg,
				 WPARAM wParam, LPARAM lParam) {
    static HWND page = NULL;

    if (msg == WM_COMMAND && LOWORD(wParam) == IDOK) {
    }
    if (msg == WM_COMMAND && LOWORD(wParam) == IDCX_ABOUT) {
	EnableWindow(hwnd, 0);
	DialogBox(hinst, MAKEINTRESOURCE(IDD_ABOUTBOX),
		  GetParent(hwnd), AboutProc);
	EnableWindow(hwnd, 1);
        SetActiveWindow(hwnd);
    }
    return GenericMainDlgProc (hwnd, msg, wParam, lParam, 0);
}

static int CALLBACK ReconfDlgProc (HWND hwnd, UINT msg,
				   WPARAM wParam, LPARAM lParam) {
    static HWND page;
    return GenericMainDlgProc (hwnd, msg, wParam, lParam, 1);
}

int do_config (void) {
    int ret;

    get_sesslist(TRUE);
    savedsession[0] = '\0';
    ret = DialogBox (hinst, MAKEINTRESOURCE(IDD_MAINBOX), NULL, MainDlgProc);
    get_sesslist(FALSE);

    return ret;
}

int do_reconfig (HWND hwnd) {
    Config backup_cfg;
    int ret;

    backup_cfg = cfg;		       /* structure copy */
    ret = DialogBox (hinst, MAKEINTRESOURCE(IDD_RECONF), hwnd, ReconfDlgProc);
    if (!ret)
	cfg = backup_cfg;	       /* structure copy */
    else
        force_normal(hwnd);

    return ret;
}

void logevent (char *string) {
    if (nevents >= negsize) {
	negsize += 64;
	events = srealloc (events, negsize * sizeof(*events));
    }
    events[nevents] = smalloc(1+strlen(string));
    strcpy (events[nevents], string);
    nevents++;
    if (logbox) {
        int count;
	SendDlgItemMessage (logbox, IDN_LIST, LB_ADDSTRING,
			    0, (LPARAM)string);
	count = SendDlgItemMessage (logbox, IDN_LIST, LB_GETCOUNT, 0, 0);
	SendDlgItemMessage (logbox, IDN_LIST, LB_SETTOPINDEX, count-1, 0);
    }
}

void showeventlog (HWND hwnd) {
    if (!logbox) {
	logbox = CreateDialog (hinst, MAKEINTRESOURCE(IDD_LOGBOX),
			       hwnd, LogProc);
	ShowWindow (logbox, SW_SHOWNORMAL);
    }
}

void showabout (HWND hwnd) {
    if (!abtbox) {
	abtbox = CreateDialog (hinst, MAKEINTRESOURCE(IDD_ABOUTBOX),
			       hwnd, AboutProc);
	ShowWindow (abtbox, SW_SHOWNORMAL);
    }
}

void verify_ssh_host_key(char *host, int port, char *keytype,
                         char *keystr, char *fingerprint) {
    int ret;

    static const char absentmsg[] =
        "The server's host key is not cached in the registry. You\n"
        "have no guarantee that the server is the computer you\n"
        "think it is.\n"
        "The server's key fingerprint is:\n"
        "%s\n"
        "If you trust this host, hit Yes to add the key to\n"
        "PuTTY's cache and carry on connecting.\n"
        "If you do not trust this host, hit No to abandon the\n"
        "connection.\n";

    static const char wrongmsg[] =
        "WARNING - POTENTIAL SECURITY BREACH!\n"
        "\n"
        "The server's host key does not match the one PuTTY has\n"
        "cached in the registry. This means that either the\n"
        "server administrator has changed the host key, or you\n"
        "have actually connected to another computer pretending\n"
        "to be the server.\n"
        "The new key fingerprint is:\n"
        "%s\n"
        "If you were expecting this change and trust the new key,\n"
        "hit Yes to update PuTTY's cache and continue connecting.\n"
        "If you want to carry on connecting but without updating\n"
        "the cache, hit No.\n"
        "If you want to abandon the connection completely, hit\n"
        "Cancel. Hitting Cancel is the ONLY guaranteed safe\n"
        "choice.\n";

    static const char mbtitle[] = "PuTTY Security Alert";

    
    char message[160+                  /* sensible fingerprint max size */
                 (sizeof(absentmsg) > sizeof(wrongmsg) ?
                  sizeof(absentmsg) : sizeof(wrongmsg))];

    /*
     * Verify the key against the registry.
     */
    ret = verify_host_key(host, port, keytype, keystr);

    if (ret == 0)                      /* success - key matched OK */
        return;
    if (ret == 2) {                    /* key was different */
        int mbret;
        sprintf(message, wrongmsg, fingerprint);
        mbret = MessageBox(NULL, message, mbtitle,
                           MB_ICONWARNING | MB_YESNOCANCEL);
        if (mbret == IDYES)
            store_host_key(host, port, keytype, keystr);
        if (mbret == IDCANCEL)
            exit(0);
    }
    if (ret == 1) {                    /* key was absent */
        int mbret;
        sprintf(message, absentmsg, fingerprint);
        mbret = MessageBox(NULL, message, mbtitle,
                           MB_ICONWARNING | MB_YESNO);
        if (mbret == IDNO)
            exit(0);
        store_host_key(host, port, keytype, keystr);
    }
}
