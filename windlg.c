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
    int ypos, width;
    int xoff, yoff;
};

static void ctlposinit(struct ctlpos *cp, HWND hwnd,
		       int sideborder, int topborder) {
    RECT r, r2;
    cp->hwnd = hwnd;
    cp->font = SendMessage(hwnd, WM_GETFONT, 0, 0);
    cp->ypos = GAPBETWEEN;
    GetClientRect(hwnd, &r);
    r2.left = r2.top = 0;
    r2.right = 4;
    r2.bottom = 8;
    MapDialogRect(hwnd, &r2);
    cp->width = (r.right * 4) / (r2.right) - 2*GAPBETWEEN;
    cp->xoff = sideborder;
    cp->width -= 2*sideborder;
    cp->yoff = topborder;
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
    r.top += cp->yoff;
    MapDialogRect(cp->hwnd, &r);

    ctl = CreateWindowEx(exstyle, wclass, wtext, wstyle,
                         r.left, r.top, r.right, r.bottom,
                         cp->hwnd, (HMENU)wid, hinst, NULL);
    SendMessage(ctl, WM_SETFONT, cp->font, MAKELPARAM(TRUE, 0));
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
static void staticedit(struct ctlpos *cp, char *stext, int sid, int eid) {
    const int height = (EDITHEIGHT > STATICHEIGHT ?
                        EDITHEIGHT : STATICHEIGHT);
    RECT r;
    int lwid, rwid, rpos;

    rpos = GAPBETWEEN + (cp->width + GAPBETWEEN) / 2;
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

enum { IDCX_ABOUT = IDC_ABOUT, IDCX_TAB, controlstartvalue,

    connectionpanelstart,
    IDC0_HOSTSTATIC,
    IDC0_HOST,
    IDC0_PORTSTATIC,
    IDC0_PORT,
    IDC0_PROTSTATIC,
    IDC0_PROTRAW,
    IDC0_PROTTELNET,
    IDC0_PROTSSH,
    IDC0_SESSSTATIC,
    IDC0_SESSEDIT,
    IDC0_SESSLIST,
    IDC0_SESSLOAD,
    IDC0_SESSSAVE,
    IDC0_SESSDEL,
    IDC0_CLOSEEXIT,
    IDC0_CLOSEWARN,
    connectionpanelend,

    keyboardpanelstart,
    IDC1_DELSTATIC,
    IDC1_DEL008,
    IDC1_DEL127,
    IDC1_HOMESTATIC,
    IDC1_HOMETILDE,
    IDC1_HOMERXVT,
    IDC1_FUNCSTATIC,
    IDC1_FUNCTILDE,
    IDC1_FUNCLINUX,
    IDC1_FUNCXTERM,
    IDC1_KPSTATIC,
    IDC1_KPNORMAL,
    IDC1_KPAPPLIC,
    IDC1_KPNH,
    IDC1_CURSTATIC,
    IDC1_CURNORMAL,
    IDC1_CURAPPLIC,
    IDC1_ALTF4,
    IDC1_ALTSPACE,
    IDC1_LDISCTERM,
    IDC1_SCROLLKEY,
    keyboardpanelend,

    terminalpanelstart,
    IDC2_WRAPMODE,
    IDC2_DECOM,
    IDC2_DIMSTATIC,
    IDC2_ROWSSTATIC,
    IDC2_ROWSEDIT,
    IDC2_COLSSTATIC,
    IDC2_COLSEDIT,
    IDC2_SAVESTATIC,
    IDC2_SAVEEDIT,
    IDC2_FONTSTATIC,
    IDC2_CHOOSEFONT,
    IDC2_LFHASCR,
    IDC2_BEEP,
    IDC2_BCE,
    IDC2_BLINKTEXT,
    terminalpanelend,

    windowpanelstart,
    IDC3_WINNAME,
    IDC3_BLINKCUR,
    IDC3_SCROLLBAR,
    IDC3_LOCKSIZE,
    IDC3_WINTITLE,
    IDC3_WINEDIT,
    windowpanelend,

    telnetpanelstart,
    IDC4_TTSTATIC,
    IDC4_TTEDIT,
    IDC4_TSSTATIC,
    IDC4_TSEDIT,
    IDC4_LOGSTATIC,
    IDC4_LOGEDIT,
    IDC4_ENVSTATIC,
    IDC4_VARSTATIC,
    IDC4_VAREDIT,
    IDC4_VALSTATIC,
    IDC4_VALEDIT,
    IDC4_ENVLIST,
    IDC4_ENVADD,
    IDC4_ENVREMOVE,
    IDC4_EMSTATIC,
    IDC4_EMBSD,
    IDC4_EMRFC,
    telnetpanelend,

    sshpanelstart,
    IDC5_TTSTATIC,
    IDC5_TTEDIT,
    IDC5_LOGSTATIC,
    IDC5_LOGEDIT,
    IDC5_NOPTY,
    IDC5_CIPHERSTATIC,
    IDC5_CIPHER3DES,
    IDC5_CIPHERBLOWF,
    IDC5_CIPHERDES,
    IDC5_AUTHTIS,
    IDC5_PKSTATIC,
    IDC5_PKEDIT,
    IDC5_PKBUTTON,
    IDC5_SSHPROTSTATIC,
    IDC5_SSHPROT1,
    IDC5_SSHPROT2,
    IDC5_AGENTFWD,
    IDC5_CMDSTATIC,
    IDC5_CMDEDIT,
    sshpanelend,

    selectionpanelstart,
    IDC6_MBSTATIC,
    IDC6_MBWINDOWS,
    IDC6_MBXTERM,
    IDC6_CCSTATIC,
    IDC6_CCLIST,
    IDC6_CCSET,
    IDC6_CCSTATIC2,
    IDC6_CCEDIT,
    selectionpanelend,

    colourspanelstart,
    IDC7_BOLDCOLOUR,
    IDC7_PALETTE,
    IDC7_STATIC,
    IDC7_LIST,
    IDC7_RSTATIC,
    IDC7_GSTATIC,
    IDC7_BSTATIC,
    IDC7_RVALUE,
    IDC7_GVALUE,
    IDC7_BVALUE,
    IDC7_CHANGE,
    colourspanelend,

    translationpanelstart,
    IDC8_XLATSTATIC,
    IDC8_NOXLAT,
    IDC8_KOI8WIN1251,
    IDC8_88592WIN1250,
    IDC8_CAPSLOCKCYR,
    IDC8_VTSTATIC,
    IDC8_VTXWINDOWS,
    IDC8_VTOEMANSI,
    IDC8_VTOEMONLY,
    IDC8_VTPOORMAN,
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

    SetDlgItemText (hwnd, IDC0_HOST, cfg.host);
    SetDlgItemText (hwnd, IDC0_SESSEDIT, savedsession);
    SetDlgItemInt (hwnd, IDC0_PORT, cfg.port, FALSE);
    for (i = 0; i < nsessions; i++)
	SendDlgItemMessage (hwnd, IDC0_SESSLIST, LB_ADDSTRING,
			    0, (LPARAM) (sessions[i]));
    CheckRadioButton (hwnd, IDC0_PROTRAW, IDC0_PROTSSH,
		      cfg.protocol==PROT_SSH ? IDC0_PROTSSH :
		      cfg.protocol==PROT_TELNET ? IDC0_PROTTELNET : IDC0_PROTRAW );
    CheckDlgButton (hwnd, IDC0_CLOSEEXIT, cfg.close_on_exit);
    CheckDlgButton (hwnd, IDC0_CLOSEWARN, cfg.warn_on_close);

    CheckRadioButton (hwnd, IDC1_DEL008, IDC1_DEL127,
		      cfg.bksp_is_delete ? IDC1_DEL127 : IDC1_DEL008);
    CheckRadioButton (hwnd, IDC1_HOMETILDE, IDC1_HOMERXVT,
		      cfg.rxvt_homeend ? IDC1_HOMERXVT : IDC1_HOMETILDE);
    CheckRadioButton (hwnd, IDC1_FUNCTILDE, IDC1_FUNCXTERM,
		      cfg.funky_type ?
		      (cfg.funky_type==2 ? IDC1_FUNCXTERM
		       : IDC1_FUNCLINUX )
		      : IDC1_FUNCTILDE);
    CheckRadioButton (hwnd, IDC1_CURNORMAL, IDC1_CURAPPLIC,
		      cfg.app_cursor ? IDC1_CURAPPLIC : IDC1_CURNORMAL);
    CheckRadioButton (hwnd, IDC1_KPNORMAL, IDC1_KPNH,
		      cfg.nethack_keypad ? IDC1_KPNH :
		      cfg.app_keypad ? IDC1_KPAPPLIC : IDC1_KPNORMAL);
    CheckDlgButton (hwnd, IDC1_ALTF4, cfg.alt_f4);
    CheckDlgButton (hwnd, IDC1_ALTSPACE, cfg.alt_space);
    CheckDlgButton (hwnd, IDC1_LDISCTERM, cfg.ldisc_term);
    CheckDlgButton (hwnd, IDC1_SCROLLKEY, cfg.scroll_on_key);

    CheckDlgButton (hwnd, IDC2_WRAPMODE, cfg.wrap_mode);
    CheckDlgButton (hwnd, IDC2_DECOM, cfg.dec_om);
    CheckDlgButton (hwnd, IDC2_LFHASCR, cfg.lfhascr);
    SetDlgItemInt (hwnd, IDC2_ROWSEDIT, cfg.height, FALSE);
    SetDlgItemInt (hwnd, IDC2_COLSEDIT, cfg.width, FALSE);
    SetDlgItemInt (hwnd, IDC2_SAVEEDIT, cfg.savelines, FALSE);
    fmtfont (fontstatic);
    SetDlgItemText (hwnd, IDC2_FONTSTATIC, fontstatic);
    CheckDlgButton (hwnd, IDC2_BEEP, cfg.beep);
    CheckDlgButton (hwnd, IDC2_BCE, cfg.bce);
    CheckDlgButton (hwnd, IDC2_BLINKTEXT, cfg.blinktext);

    SetDlgItemText (hwnd, IDC3_WINEDIT, cfg.wintitle);
    CheckDlgButton (hwnd, IDC3_WINNAME, cfg.win_name_always);
    CheckDlgButton (hwnd, IDC3_BLINKCUR, cfg.blink_cur);
    CheckDlgButton (hwnd, IDC3_SCROLLBAR, cfg.scrollbar);
    CheckDlgButton (hwnd, IDC3_LOCKSIZE, cfg.locksize);

    SetDlgItemText (hwnd, IDC4_TTEDIT, cfg.termtype);
    SetDlgItemText (hwnd, IDC4_TSEDIT, cfg.termspeed);
    SetDlgItemText (hwnd, IDC4_LOGEDIT, cfg.username);
    {
	char *p = cfg.environmt;
	while (*p) {
	    SendDlgItemMessage (hwnd, IDC4_ENVLIST, LB_ADDSTRING, 0,
				(LPARAM) p);
	    p += strlen(p)+1;
	}
    }
    CheckRadioButton (hwnd, IDC4_EMBSD, IDC4_EMRFC,
		      cfg.rfc_environ ? IDC4_EMRFC : IDC4_EMBSD);

    SetDlgItemText (hwnd, IDC5_TTEDIT, cfg.termtype);
    SetDlgItemText (hwnd, IDC5_LOGEDIT, cfg.username);
    CheckDlgButton (hwnd, IDC5_NOPTY, cfg.nopty);
    CheckDlgButton (hwnd, IDC5_AGENTFWD, cfg.agentfwd);
    CheckRadioButton (hwnd, IDC5_CIPHER3DES, IDC5_CIPHERDES,
		      cfg.cipher == CIPHER_BLOWFISH ? IDC5_CIPHERBLOWF :
		      cfg.cipher == CIPHER_DES ? IDC5_CIPHERDES :
		      IDC5_CIPHER3DES);
    CheckRadioButton (hwnd, IDC5_SSHPROT1, IDC5_SSHPROT2,
		      cfg.sshprot == 1 ? IDC5_SSHPROT1 : IDC5_SSHPROT2);
    CheckDlgButton (hwnd, IDC5_AUTHTIS, cfg.try_tis_auth);
    SetDlgItemText (hwnd, IDC5_PKEDIT, cfg.keyfile);
    SetDlgItemText (hwnd, IDC5_CMDEDIT, cfg.remote_cmd);

    CheckRadioButton (hwnd, IDC6_MBWINDOWS, IDC6_MBXTERM,
		      cfg.mouse_is_xterm ? IDC6_MBXTERM : IDC6_MBWINDOWS);
    {
	static int tabs[4] = {25, 61, 96, 128};
	SendDlgItemMessage (hwnd, IDC6_CCLIST, LB_SETTABSTOPS, 4,
			    (LPARAM) tabs);
    }
    for (i=0; i<256; i++) {
	char str[100];
	sprintf(str, "%d\t(0x%02X)\t%c\t%d", i, i,
		(i>=0x21 && i != 0x7F) ? i : ' ',
		cfg.wordness[i]);
	SendDlgItemMessage (hwnd, IDC6_CCLIST, LB_ADDSTRING, 0,
			    (LPARAM) str);
    }

    CheckDlgButton (hwnd, IDC7_BOLDCOLOUR, cfg.bold_colour);
    CheckDlgButton (hwnd, IDC7_PALETTE, cfg.try_palette);
    {
	int i;
	for (i=0; i<22; i++)
	    if (cfg.bold_colour || permcolour[i])
		SendDlgItemMessage (hwnd, IDC7_LIST, LB_ADDSTRING, 0,
				    (LPARAM) colours[i]);
    }
    SendDlgItemMessage (hwnd, IDC7_LIST, LB_SETCURSEL, 0, 0);
    SetDlgItemInt (hwnd, IDC7_RVALUE, cfg.colours[0][0], FALSE);
    SetDlgItemInt (hwnd, IDC7_GVALUE, cfg.colours[0][1], FALSE);
    SetDlgItemInt (hwnd, IDC7_BVALUE, cfg.colours[0][2], FALSE);

    CheckRadioButton (hwnd, IDC8_NOXLAT, IDC8_88592WIN1250,
		      cfg.xlat_88592w1250 ? IDC8_88592WIN1250 :
		      cfg.xlat_enablekoiwin ? IDC8_KOI8WIN1251 :
		      IDC8_NOXLAT);
    CheckDlgButton (hwnd, IDC8_CAPSLOCKCYR, cfg.xlat_capslockcyr);
    CheckRadioButton (hwnd, IDC8_VTXWINDOWS, IDC8_VTPOORMAN,
		      cfg.vtmode == VT_XWINDOWS ? IDC8_VTXWINDOWS :
		      cfg.vtmode == VT_OEMANSI ? IDC8_VTOEMANSI :
		      cfg.vtmode == VT_OEMONLY ? IDC8_VTOEMONLY :
		      IDC8_VTPOORMAN);
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

/*
 * This _huge_ function is the configuration box.
 */
static int GenericMainDlgProc (HWND hwnd, UINT msg,
			       WPARAM wParam, LPARAM lParam,
			       int dlgtype) {
    HWND hw, tabctl;
    TC_ITEMHEADER tab;
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
	 * Create the tab control.
	 */
        {
            RECT r;
	    WPARAM font;

            r.left = 3; r.right = r.left + 174;
            r.top = 3; r.bottom = r.top + 193;
            MapDialogRect(hwnd, &r);
            tabctl = CreateWindowEx(0, WC_TABCONTROL, "",
                                    WS_CHILD | WS_VISIBLE |
                                    WS_TABSTOP | TCS_MULTILINE,
                                    r.left, r.top,
                                    r.right-r.left, r.bottom-r.top,
                                    hwnd, (HMENU)IDCX_TAB, hinst, NULL);
	    font = SendMessage(hwnd, WM_GETFONT, 0, 0);
	    SendMessage(tabctl, WM_SETFONT, font, MAKELPARAM(TRUE, 0));
        }

	/*
	 * Create the various panelfuls of controls.
	 */

	i = 0;

	/* The Connection panel. Accelerators used: [aco] dehlnprstwx */
	{
	    struct ctlpos cp;
	    ctlposinit(&cp, hwnd, 6, 30);
	    if (dlgtype == 0) {
		multiedit(&cp,
			  "Host &Name", IDC0_HOSTSTATIC, IDC0_HOST, 75,
			  "&Port", IDC0_PORTSTATIC, IDC0_PORT, 25, NULL);
		if (backends[2].backend == NULL) {
		    /* this is PuTTYtel, so only two protocols available */
		    radioline(&cp, "Protocol:", IDC0_PROTSTATIC, 3,
			      "&Raw", IDC0_PROTRAW,
			      "&Telnet", IDC0_PROTTELNET, NULL);
		} else {
		    radioline(&cp, "Protocol:", IDC0_PROTSTATIC, 3,
			      "&Raw", IDC0_PROTRAW,
			      "&Telnet", IDC0_PROTTELNET,
#ifdef FWHACK
			      "SS&H/hack",
#else
			      "SS&H",
#endif
			      IDC0_PROTSSH, NULL);
		}
		sesssaver(&cp, "Stor&ed Sessions",
			  IDC0_SESSSTATIC, IDC0_SESSEDIT, IDC0_SESSLIST,
			  "&Load", IDC0_SESSLOAD,
			  "&Save", IDC0_SESSSAVE,
			  "&Delete", IDC0_SESSDEL, NULL);
	    }
	    checkbox(&cp, "Close Window on E&xit", IDC0_CLOSEEXIT);
	    checkbox(&cp, "&Warn on Close", IDC0_CLOSEWARN);

	    tab.mask = TCIF_TEXT; tab.pszText = "Connection";
	    TabCtrl_InsertItem (tabctl, i++, &tab);
	}

	/* The Keyboard panel. Accelerators used: [aco] 4?ehiklmnprsuvxy */
	{
	    struct ctlpos cp;
	    ctlposinit(&cp, hwnd, 6, 30);
	    radioline(&cp, "Action of Backspace:", IDC1_DELSTATIC, 2,
		      "Control-&H", IDC1_DEL008,
		      "Control-&? (127)", IDC1_DEL127, NULL);
	    radioline(&cp, "Action of Home and End:", IDC1_HOMESTATIC, 2,
		      "&Standard", IDC1_HOMETILDE,
		      "&rxvt", IDC1_HOMERXVT, NULL);
	    radioline(&cp, "Function key and keypad layout:", IDC1_FUNCSTATIC, 3,
		      "&VT400", IDC1_FUNCTILDE,
		      "&Linux", IDC1_FUNCLINUX,
		      "&Xterm R6", IDC1_FUNCXTERM, NULL);
	    radioline(&cp, "Initial state of cursor keys:", IDC1_CURSTATIC, 2,
		      "&Normal", IDC1_CURNORMAL,
		      "A&pplication", IDC1_CURAPPLIC, NULL);
	    radioline(&cp, "Initial state of numeric keypad:", IDC1_KPSTATIC, 3,
		      "Nor&mal", IDC1_KPNORMAL,
		      "Appl&ication", IDC1_KPAPPLIC,
		      "N&etHack", IDC1_KPNH, NULL);
	    checkbox(&cp, "ALT-F&4 is special (closes window)", IDC1_ALTF4);
	    checkbox(&cp, "ALT-Space is special (S&ystem menu)", IDC1_ALTSPACE);
	    checkbox(&cp, "&Use local terminal line discipline", IDC1_LDISCTERM);
	    checkbox(&cp, "Reset scrollback on &keypress", IDC1_SCROLLKEY);

	    tab.mask = TCIF_TEXT; tab.pszText = "Keyboard";
	    TabCtrl_InsertItem (tabctl, i++, &tab);
	}

        /* The Terminal panel. Accelerators used: [aco] dghlmnprsw */
	{
	    struct ctlpos cp;
	    ctlposinit(&cp, hwnd, 6, 30);
	    multiedit(&cp,
		      "&Rows", IDC2_ROWSSTATIC, IDC2_ROWSEDIT, 33,
		      "Colu&mns", IDC2_COLSSTATIC, IDC2_COLSEDIT, 33,
		      "&Scrollback", IDC2_SAVESTATIC, IDC2_SAVEEDIT, 33,
		      NULL);
	    staticbtn(&cp, "", IDC2_FONTSTATIC, "C&hange...", IDC2_CHOOSEFONT);
	    checkbox(&cp, "Auto &wrap mode initially on", IDC2_WRAPMODE);
	    checkbox(&cp, "&DEC Origin Mode initially on", IDC2_DECOM);
	    checkbox(&cp, "Implicit CR in every &LF", IDC2_LFHASCR);
	    checkbox(&cp, "Bee&p enabled", IDC2_BEEP);
	    checkbox(&cp, "Use Back&ground colour erase", IDC2_BCE);
	    checkbox(&cp, "Enable bli&nking text", IDC2_BLINKTEXT);
	    tab.mask = TCIF_TEXT; tab.pszText = "Terminal";
	    TabCtrl_InsertItem (tabctl, i++, &tab);
	}

        /* The Window panel. Accelerators used: [aco] bikty */
	{
	    struct ctlpos cp;
	    ctlposinit(&cp, hwnd, 6, 30);
	    if (dlgtype == 0)
		multiedit(&cp,
			  "Initial window &title:", IDC3_WINTITLE,
			  IDC3_WINEDIT, 100, NULL);
	    checkbox(&cp, "Avoid ever using &icon title", IDC3_WINNAME);
	    checkbox(&cp, "&Blinking cursor", IDC3_BLINKCUR);
	    checkbox(&cp, "Displa&y scrollbar", IDC3_SCROLLBAR);
	    checkbox(&cp, "Loc&k Window size", IDC3_LOCKSIZE);
	    tab.mask = TCIF_TEXT; tab.pszText = "Window";
	    TabCtrl_InsertItem (tabctl, i++, &tab);
	}

        /* The Telnet panel. Accelerators used: [aco] bdflrstuv */
	{
	    struct ctlpos cp;
	    ctlposinit(&cp, hwnd, 6, 30);
	    if (dlgtype == 0) {
		staticedit(&cp, "Terminal-&type string", IDC4_TTSTATIC, IDC4_TTEDIT);
		staticedit(&cp, "Terminal-&speed string", IDC4_TSSTATIC, IDC4_TSEDIT);
		staticedit(&cp, "Auto-login &username", IDC4_LOGSTATIC, IDC4_LOGEDIT);
		envsetter(&cp, "Environment variables:", IDC4_ENVSTATIC,
			  "&Variable", IDC4_VARSTATIC, IDC4_VAREDIT,
			  "Va&lue", IDC4_VALSTATIC, IDC4_VALEDIT,
			  IDC4_ENVLIST,
			  "A&dd", IDC4_ENVADD, "&Remove", IDC4_ENVREMOVE);
		radioline(&cp, "Handling of OLD_ENVIRON ambiguity:", IDC4_EMSTATIC, 2,
			  "&BSD (commonplace)", IDC4_EMBSD,
			  "R&FC 1408 (unusual)", IDC4_EMRFC, NULL);
		tab.mask = TCIF_TEXT; tab.pszText = "Telnet";
		TabCtrl_InsertItem (tabctl, i++, &tab);
	    }
	}

	/* The SSH panel. Accelerators used: [aco] 123abdkmprtuw */
	{
	    struct ctlpos cp;
	    ctlposinit(&cp, hwnd, 6, 30);
	    if (dlgtype == 0) {
		staticedit(&cp, "Terminal-&type string", IDC5_TTSTATIC, IDC5_TTEDIT);
		staticedit(&cp, "Auto-login &username", IDC5_LOGSTATIC, IDC5_LOGEDIT);
		multiedit(&cp,
			  "&Remote command:", IDC5_CMDSTATIC, IDC5_CMDEDIT, 100,
			  NULL);
		checkbox(&cp, "Don't allocate a &pseudo-terminal", IDC5_NOPTY);
		checkbox(&cp, "Atte&mpt TIS or CryptoCard authentication",
			 IDC5_AUTHTIS);
		checkbox(&cp, "Allow &agent forwarding", IDC5_AGENTFWD);
		editbutton(&cp, "Private &key file for authentication:",
			   IDC5_PKSTATIC, IDC5_PKEDIT, "Bro&wse...", IDC5_PKBUTTON);
		radioline(&cp, "Preferred SSH protocol version:",
			  IDC5_SSHPROTSTATIC, 2,
			  "&1", IDC5_SSHPROT1, "&2", IDC5_SSHPROT2, NULL);
		radioline(&cp, "Preferred encryption algorithm:", IDC5_CIPHERSTATIC, 3,
			  "&3DES", IDC5_CIPHER3DES,
			  "&Blowfish", IDC5_CIPHERBLOWF,
			  "&DES", IDC5_CIPHERDES, NULL);
		tab.mask = TCIF_TEXT; tab.pszText = "SSH";
		TabCtrl_InsertItem (tabctl, i++, &tab);
	    }
	}

        /* The Selection panel. Accelerators used: [aco] stwx */
	{
	    struct ctlpos cp;
	    ctlposinit(&cp, hwnd, 6, 30);
	    radiobig(&cp, "Action of mouse buttons:", IDC6_MBSTATIC,
		     "&Windows (Right pastes, Middle extends)", IDC6_MBWINDOWS,
		     "&xterm (Right extends, Middle pastes)", IDC6_MBXTERM,
		     NULL);
	    charclass(&cp, "Character classes:", IDC6_CCSTATIC, IDC6_CCLIST,
		      "&Set", IDC6_CCSET, IDC6_CCEDIT,
		      "&to class", IDC6_CCSTATIC2);
	    tab.mask = TCIF_TEXT; tab.pszText = "Selection";
	    TabCtrl_InsertItem (tabctl, i++, &tab);
	}

        /* The Colours panel. Accelerators used: [aco] bmlu */
	{
	    struct ctlpos cp;
	    ctlposinit(&cp, hwnd, 6, 30);
	    checkbox(&cp, "&Bolded text is a different colour", IDC7_BOLDCOLOUR);
	    checkbox(&cp, "Attempt to use &logical palettes", IDC7_PALETTE);
	    colouredit(&cp, "Select a colo&ur and click to modify it:",
		       IDC7_STATIC, IDC7_LIST,
		       "&Modify...", IDC7_CHANGE,
		       "Red:", IDC7_RSTATIC, IDC7_RVALUE,
		       "Green:", IDC7_GSTATIC, IDC7_GVALUE,
		       "Blue:", IDC7_BSTATIC, IDC7_BVALUE, NULL);
	    tab.mask = TCIF_TEXT; tab.pszText = "Colours";
	    TabCtrl_InsertItem (tabctl, i++, &tab);
	}

	/* The Translation panel. Accelerators used: [aco] beiknpsx */
	{
	    struct ctlpos cp;
	    ctlposinit(&cp, hwnd, 6, 30);
	    radiobig(&cp,
		     "Handling of VT100 line drawing characters:", IDC8_VTSTATIC,
		     "Font has &XWindows encoding", IDC8_VTXWINDOWS,
		     "Use font in &both ANSI and OEM modes", IDC8_VTOEMANSI,
		     "Use font in O&EM mode only", IDC8_VTOEMONLY,
		     "&Poor man's line drawing (""+"", ""-"" and ""|"")",
		     IDC8_VTPOORMAN, NULL);
	    radiobig(&cp,
		     "Character set translation:", IDC8_XLATSTATIC,
		     "&None", IDC8_NOXLAT,
		     "&KOI8 / Win-1251", IDC8_KOI8WIN1251,
		     "&ISO-8859-2 / Win-1250", IDC8_88592WIN1250, NULL);
	    checkbox(&cp, "CAP&S LOCK acts as cyrillic switch", IDC8_CAPSLOCKCYR);
	    tab.mask = TCIF_TEXT; tab.pszText = "Translation";
	    TabCtrl_InsertItem (tabctl, i++, &tab);
	}

	init_dlg_ctrls(hwnd);

	hide(hwnd, TRUE, controlstartvalue, controlendvalue);
	hide(hwnd, FALSE, connectionpanelstart, connectionpanelend);

        /*
         * Set focus into the first available control.
         */
        {
            HWND ctl;
            ctl = GetDlgItem(hwnd, IDC0_HOST);
            if (!ctl) ctl = GetDlgItem(hwnd, IDC0_CLOSEEXIT);
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
	if (LOWORD(wParam) == IDCX_TAB &&
	    ((LPNMHDR)lParam)->code == TCN_SELCHANGE) {
	    int i = TabCtrl_GetCurSel(((LPNMHDR)lParam)->hwndFrom);
	    TCITEM item;
	    char buffer[64];
	    item.pszText = buffer;
	    item.cchTextMax = sizeof(buffer);
	    item.mask = TCIF_TEXT;
	    TabCtrl_GetItem(((LPNMHDR)lParam)->hwndFrom, i, &item);
	    hide(hwnd, TRUE, controlstartvalue, controlendvalue);
	    if (!strcmp(buffer, "Connection"))
		hide(hwnd, FALSE, connectionpanelstart, connectionpanelend);
	    if (!strcmp(buffer, "Keyboard"))
		hide(hwnd, FALSE, keyboardpanelstart, keyboardpanelend);
	    if (!strcmp(buffer, "Terminal"))
		hide(hwnd, FALSE, terminalpanelstart, terminalpanelend);
	    if (!strcmp(buffer, "Window"))
		hide(hwnd, FALSE, windowpanelstart, windowpanelend);
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
	  case IDC0_PROTTELNET:
	  case IDC0_PROTSSH:
	  case IDC0_PROTRAW:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		int i = IsDlgButtonChecked (hwnd, IDC0_PROTSSH);
		int j = IsDlgButtonChecked (hwnd, IDC0_PROTTELNET);
		cfg.protocol = i ? PROT_SSH : j ? PROT_TELNET : PROT_RAW ;
		if ((cfg.protocol == PROT_SSH && cfg.port == 23) ||
		    (cfg.protocol == PROT_TELNET && cfg.port == 22)) {
		    cfg.port = i ? 22 : 23;
		    SetDlgItemInt (hwnd, IDC0_PORT, cfg.port, FALSE);
		}
	    }
	    break;
	  case IDC0_HOST:
	    if (HIWORD(wParam) == EN_CHANGE)
		GetDlgItemText (hwnd, IDC0_HOST, cfg.host,
				sizeof(cfg.host)-1);
	    break;
	  case IDC0_PORT:
	    if (HIWORD(wParam) == EN_CHANGE)
		MyGetDlgItemInt (hwnd, IDC0_PORT, &cfg.port);
	    break;
	  case IDC0_CLOSEEXIT:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.close_on_exit = IsDlgButtonChecked (hwnd, IDC0_CLOSEEXIT);
	    break;
	  case IDC0_CLOSEWARN:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.warn_on_close = IsDlgButtonChecked (hwnd, IDC0_CLOSEWARN);
	    break;
	  case IDC0_SESSEDIT:
	    if (HIWORD(wParam) == EN_CHANGE) {
		SendDlgItemMessage (hwnd, IDC0_SESSLIST, LB_SETCURSEL,
				    (WPARAM) -1, 0);
                GetDlgItemText (hwnd, IDC0_SESSEDIT,
                                savedsession, sizeof(savedsession)-1);
                savedsession[sizeof(savedsession)-1] = '\0';
            }
	    break;
	  case IDC0_SESSSAVE:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		/*
		 * Save a session
		 */
		char str[2048];
		GetDlgItemText (hwnd, IDC0_SESSEDIT, str, sizeof(str)-1);
		if (!*str) {
		    int n = SendDlgItemMessage (hwnd, IDC0_SESSLIST,
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
		SendDlgItemMessage (hwnd, IDC0_SESSLIST, LB_RESETCONTENT,
				    0, 0);
		for (i = 0; i < nsessions; i++)
		    SendDlgItemMessage (hwnd, IDC0_SESSLIST, LB_ADDSTRING,
					0, (LPARAM) (sessions[i]));
		SendDlgItemMessage (hwnd, IDC0_SESSLIST, LB_SETCURSEL,
				    (WPARAM) -1, 0);
	    }
	    break;
	  case IDC0_SESSLIST:
	  case IDC0_SESSLOAD:
	    if (LOWORD(wParam) == IDC0_SESSLOAD &&
		HIWORD(wParam) != BN_CLICKED &&
		HIWORD(wParam) != BN_DOUBLECLICKED)
		break;
	    if (LOWORD(wParam) == IDC0_SESSLIST &&
		HIWORD(wParam) != LBN_DBLCLK)
		break;
	    {
		int n = SendDlgItemMessage (hwnd, IDC0_SESSLIST,
					    LB_GETCURSEL, 0, 0);
		if (n == LB_ERR) {
		    MessageBeep(0);
		    break;
		}
		load_settings (sessions[n],
			       !!strcmp(sessions[n], "Default Settings"),
                               &cfg);
		init_dlg_ctrls(hwnd);
	    }
	    if (LOWORD(wParam) == IDC0_SESSLIST) {
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
	  case IDC0_SESSDEL:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		int n = SendDlgItemMessage (hwnd, IDC0_SESSLIST,
					    LB_GETCURSEL, 0, 0);
		if (n == LB_ERR || n == 0) {
		    MessageBeep(0);
		    break;
		}
		del_settings(sessions[n]);
		get_sesslist (FALSE);
		get_sesslist (TRUE);
		SendDlgItemMessage (hwnd, IDC0_SESSLIST, LB_RESETCONTENT,
				    0, 0);
		for (i = 0; i < nsessions; i++)
		    SendDlgItemMessage (hwnd, IDC0_SESSLIST, LB_ADDSTRING,
					0, (LPARAM) (sessions[i]));
		SendDlgItemMessage (hwnd, IDC0_SESSLIST, LB_SETCURSEL,
				    (WPARAM) -1, 0);
	    }
	  case IDC1_DEL008:
	  case IDC1_DEL127:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.bksp_is_delete = IsDlgButtonChecked (hwnd, IDC1_DEL127);
	    break;
	  case IDC1_HOMETILDE:
	  case IDC1_HOMERXVT:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.rxvt_homeend = IsDlgButtonChecked (hwnd, IDC1_HOMERXVT);
	    break;
	  case IDC1_FUNCXTERM:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.funky_type = 2;
	    break;
	  case IDC1_FUNCTILDE:
	  case IDC1_FUNCLINUX:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.funky_type = IsDlgButtonChecked (hwnd, IDC1_FUNCLINUX);
	    break;
	  case IDC1_KPNORMAL:
	  case IDC1_KPAPPLIC:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		cfg.app_keypad = IsDlgButtonChecked (hwnd, IDC1_KPAPPLIC);
		cfg.nethack_keypad = FALSE;
	    }
	    break;
	  case IDC1_KPNH:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		cfg.app_keypad = FALSE;
		cfg.nethack_keypad = TRUE;
	    }
	    break;
	  case IDC1_CURNORMAL:
	  case IDC1_CURAPPLIC:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.app_cursor = IsDlgButtonChecked (hwnd, IDC1_CURAPPLIC);
	    break;
	  case IDC1_ALTF4:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.alt_f4 = IsDlgButtonChecked (hwnd, IDC1_ALTF4);
	    break;
	  case IDC1_ALTSPACE:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.alt_space = IsDlgButtonChecked (hwnd, IDC1_ALTSPACE);
	    break;
	  case IDC1_LDISCTERM:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.ldisc_term = IsDlgButtonChecked (hwnd, IDC1_LDISCTERM);
	    break;
	  case IDC1_SCROLLKEY:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.scroll_on_key = IsDlgButtonChecked (hwnd, IDC1_SCROLLKEY);
	    break;
	  case IDC2_WRAPMODE:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.wrap_mode = IsDlgButtonChecked (hwnd, IDC2_WRAPMODE);
	    break;
	  case IDC2_DECOM:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.dec_om = IsDlgButtonChecked (hwnd, IDC2_DECOM);
	    break;
	  case IDC2_LFHASCR:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.lfhascr = IsDlgButtonChecked (hwnd, IDC2_LFHASCR);
	    break;
	  case IDC2_ROWSEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
		MyGetDlgItemInt (hwnd, IDC2_ROWSEDIT, &cfg.height);
	    break;
	  case IDC2_COLSEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
		MyGetDlgItemInt (hwnd, IDC2_COLSEDIT, &cfg.width);
	    break;
	  case IDC2_SAVEEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
		MyGetDlgItemInt (hwnd, IDC2_SAVEEDIT, &cfg.savelines);
	    break;
	  case IDC2_CHOOSEFONT:
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
		SetDlgItemText (hwnd, IDC2_FONTSTATIC, fontstatic);
	    }
	    break;
	  case IDC2_BEEP:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.beep = IsDlgButtonChecked (hwnd, IDC2_BEEP);
	    break;
	  case IDC2_BLINKTEXT:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.blinktext = IsDlgButtonChecked (hwnd, IDC2_BLINKTEXT);
	    break;
	  case IDC2_BCE:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.bce = IsDlgButtonChecked (hwnd, IDC2_BCE);
	    break;
	  case IDC3_WINNAME:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.win_name_always = IsDlgButtonChecked (hwnd, IDC3_WINNAME);
	    break;
          case IDC3_BLINKCUR:
            if (HIWORD(wParam) == BN_CLICKED ||
                HIWORD(wParam) == BN_DOUBLECLICKED)
                cfg.blink_cur = IsDlgButtonChecked (hwnd, IDC3_BLINKCUR);
            break;
          case IDC3_SCROLLBAR:
            if (HIWORD(wParam) == BN_CLICKED ||
                HIWORD(wParam) == BN_DOUBLECLICKED)
                cfg.scrollbar = IsDlgButtonChecked (hwnd, IDC3_SCROLLBAR);
            break;
          case IDC3_LOCKSIZE:
	     if (HIWORD(wParam) == BN_CLICKED ||
		 HIWORD(wParam) == BN_DOUBLECLICKED)
                cfg.locksize = IsDlgButtonChecked (hwnd, IDC3_LOCKSIZE);
            break;
	  case IDC3_WINEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
		GetDlgItemText (hwnd, IDC3_WINEDIT, cfg.wintitle,
				sizeof(cfg.wintitle)-1);
	    break;
	  case IDC4_TTEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
	    GetDlgItemText (hwnd, IDC4_TTEDIT, cfg.termtype,
			    sizeof(cfg.termtype)-1);
	    break;
	  case IDC4_TSEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
		GetDlgItemText (hwnd, IDC4_TSEDIT, cfg.termspeed,
				sizeof(cfg.termspeed)-1);
	    break;
	  case IDC4_LOGEDIT:
	    if (HIWORD(wParam) == EN_CHANGE) {
		GetDlgItemText (hwnd, IDC4_LOGEDIT, cfg.username,
				sizeof(cfg.username)-1);
		cfg.username[sizeof(cfg.username)-1] = '\0';
		SetWindowLong(hwnd, GWL_USERDATA, 0);
		SetDlgItemText (hwnd, IDC5_LOGEDIT, cfg.username);
		SetWindowLong(hwnd, GWL_USERDATA, 1);
	    }
	    break;
	  case IDC4_EMBSD:
	  case IDC4_EMRFC:
	    cfg.rfc_environ = IsDlgButtonChecked (hwnd, IDC4_EMRFC);
	    break;
	  case IDC4_ENVADD:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
              char str[sizeof(cfg.environmt)];
		char *p;
		GetDlgItemText (hwnd, IDC4_VAREDIT, str, sizeof(str)-1);
		if (!*str) {
		    MessageBeep(0);
		    break;
		}
		p = str + strlen(str);
		*p++ = '\t';
		GetDlgItemText (hwnd, IDC4_VALEDIT, p, sizeof(str)-1-(p-str));
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
		    SendDlgItemMessage (hwnd, IDC4_ENVLIST, LB_ADDSTRING,
					0, (LPARAM)str);
		    SetDlgItemText (hwnd, IDC4_VAREDIT, "");
		    SetDlgItemText (hwnd, IDC4_VALEDIT, "");
		} else {
		    MessageBox(hwnd, "Environment too big", "PuTTY Error",
			       MB_OK | MB_ICONERROR);
		}
	    }
	    break;
	  case IDC4_ENVREMOVE:
	    if (HIWORD(wParam) != BN_CLICKED &&
		HIWORD(wParam) != BN_DOUBLECLICKED)
		break;
	    i = SendDlgItemMessage (hwnd, IDC4_ENVLIST, LB_GETCURSEL, 0, 0);
	    if (i == LB_ERR)
		MessageBeep (0);
	    else {
		char *p, *q;

	        SendDlgItemMessage (hwnd, IDC4_ENVLIST, LB_DELETESTRING,
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
	  case IDC5_TTEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
	    GetDlgItemText (hwnd, IDC5_TTEDIT, cfg.termtype,
			    sizeof(cfg.termtype)-1);
	    break;
	  case IDC5_LOGEDIT:
	    if (HIWORD(wParam) == EN_CHANGE) {
		GetDlgItemText (hwnd, IDC5_LOGEDIT, cfg.username,
				sizeof(cfg.username)-1);
		cfg.username[sizeof(cfg.username)-1] = '\0';
		SetWindowLong(hwnd, GWL_USERDATA, 0);
		SetDlgItemText (hwnd, IDC4_LOGEDIT, cfg.username);
		SetWindowLong(hwnd, GWL_USERDATA, 1);
	    }
	    break;
	  case IDC5_NOPTY:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.nopty = IsDlgButtonChecked (hwnd, IDC5_NOPTY);
	    break;
	  case IDC5_AGENTFWD:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.agentfwd = IsDlgButtonChecked (hwnd, IDC5_AGENTFWD);
	    break;
	  case IDC5_CIPHER3DES:
	  case IDC5_CIPHERBLOWF:
	  case IDC5_CIPHERDES:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		if (IsDlgButtonChecked (hwnd, IDC5_CIPHER3DES))
		    cfg.cipher = CIPHER_3DES;
		else if (IsDlgButtonChecked (hwnd, IDC5_CIPHERBLOWF))
		    cfg.cipher = CIPHER_BLOWFISH;
		else if (IsDlgButtonChecked (hwnd, IDC5_CIPHERDES))
		    cfg.cipher = CIPHER_DES;
	    }
	    break;
	  case IDC5_SSHPROT1:
	  case IDC5_SSHPROT2:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		if (IsDlgButtonChecked (hwnd, IDC5_SSHPROT1))
		    cfg.sshprot = 1;
		else if (IsDlgButtonChecked (hwnd, IDC5_SSHPROT2))
		    cfg.sshprot = 2;
	    }
	    break;
	  case IDC5_AUTHTIS:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.try_tis_auth = IsDlgButtonChecked (hwnd, IDC5_AUTHTIS);
	    break;
	  case IDC5_PKEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
		GetDlgItemText (hwnd, IDC5_PKEDIT, cfg.keyfile,
				sizeof(cfg.keyfile)-1);
	    break;
	  case IDC5_CMDEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
		GetDlgItemText (hwnd, IDC5_CMDEDIT, cfg.remote_cmd,
				sizeof(cfg.remote_cmd)-1);
	    break;
	  case IDC5_PKBUTTON:
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
                SetDlgItemText (hwnd, IDC5_PKEDIT, cfg.keyfile);
            }
	    break;
	  case IDC6_MBWINDOWS:
	  case IDC6_MBXTERM:
	    cfg.mouse_is_xterm = IsDlgButtonChecked (hwnd, IDC6_MBXTERM);
	    break;
	  case IDC6_CCSET:
	    {
		BOOL ok;
		int i;
		int n = GetDlgItemInt (hwnd, IDC6_CCEDIT, &ok, FALSE);

		if (!ok)
		    MessageBeep (0);
		else {
		    for (i=0; i<256; i++)
			if (SendDlgItemMessage (hwnd, IDC6_CCLIST, LB_GETSEL,
						i, 0)) {
			    char str[100];
			    cfg.wordness[i] = n;
			    SendDlgItemMessage (hwnd, IDC6_CCLIST,
						LB_DELETESTRING, i, 0);
			    sprintf(str, "%d\t(0x%02X)\t%c\t%d", i, i,
				    (i>=0x21 && i != 0x7F) ? i : ' ',
				    cfg.wordness[i]);
			    SendDlgItemMessage (hwnd, IDC6_CCLIST,
						LB_INSERTSTRING, i,
						(LPARAM)str);
			}
		}
	    }
	    break;
	  case IDC7_BOLDCOLOUR:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		int n, i;
		cfg.bold_colour = IsDlgButtonChecked (hwnd, IDC7_BOLDCOLOUR);
		n = SendDlgItemMessage (hwnd, IDC7_LIST, LB_GETCOUNT, 0, 0);
		if (cfg.bold_colour && n!=22) {
		    for (i=0; i<22; i++)
			if (!permcolour[i])
			    SendDlgItemMessage (hwnd, IDC7_LIST,
						LB_INSERTSTRING, i,
						(LPARAM) colours[i]);
		} else if (!cfg.bold_colour && n!=12) {
		    for (i=22; i-- ;)
			if (!permcolour[i])
			    SendDlgItemMessage (hwnd, IDC7_LIST,
						LB_DELETESTRING, i, 0);
		}
	    }
	    break;
	  case IDC7_PALETTE:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.try_palette = IsDlgButtonChecked (hwnd, IDC7_PALETTE);
	    break;
	  case IDC7_LIST:
	    if (HIWORD(wParam) == LBN_DBLCLK ||
		HIWORD(wParam) == LBN_SELCHANGE) {
		int i = SendDlgItemMessage (hwnd, IDC7_LIST, LB_GETCURSEL,
					    0, 0);
		if (!cfg.bold_colour)
		    i = (i < 3 ? i*2 : i == 3 ? 5 : i*2-2);
		SetDlgItemInt (hwnd, IDC7_RVALUE, cfg.colours[i][0], FALSE);
		SetDlgItemInt (hwnd, IDC7_GVALUE, cfg.colours[i][1], FALSE);
		SetDlgItemInt (hwnd, IDC7_BVALUE, cfg.colours[i][2], FALSE);
	    }
	    break;
	  case IDC7_CHANGE:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		static CHOOSECOLOR cc;
		static DWORD custom[16] = {0};   /* zero initialisers */
		int i = SendDlgItemMessage (hwnd, IDC7_LIST, LB_GETCURSEL,
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
		    SetDlgItemInt (hwnd, IDC7_RVALUE, cfg.colours[i][0],
				   FALSE);
		    SetDlgItemInt (hwnd, IDC7_GVALUE, cfg.colours[i][1],
				   FALSE);
		    SetDlgItemInt (hwnd, IDC7_BVALUE, cfg.colours[i][2],
				   FALSE);
		}
	    }
	    break;
	  case IDC8_NOXLAT:
	  case IDC8_KOI8WIN1251:
	  case IDC8_88592WIN1250:
	    cfg.xlat_enablekoiwin =
		IsDlgButtonChecked (hwnd, IDC8_KOI8WIN1251);
	    cfg.xlat_88592w1250 =
		IsDlgButtonChecked (hwnd, IDC8_88592WIN1250);
	    break;
	  case IDC8_CAPSLOCKCYR:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		cfg.xlat_capslockcyr =
		    IsDlgButtonChecked (hwnd, IDC8_CAPSLOCKCYR);
	    }
	    break;
	  case IDC8_VTXWINDOWS:
	  case IDC8_VTOEMANSI:
	  case IDC8_VTOEMONLY:
	  case IDC8_VTPOORMAN:
	    cfg.vtmode =
		(IsDlgButtonChecked (hwnd, IDC8_VTXWINDOWS) ? VT_XWINDOWS :
		 IsDlgButtonChecked (hwnd, IDC8_VTOEMANSI) ? VT_OEMANSI :
		 IsDlgButtonChecked (hwnd, IDC8_VTOEMONLY) ? VT_OEMONLY :
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
