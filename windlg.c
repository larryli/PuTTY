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

#define NPANELS 9
#define MAIN_NPANELS 9
#define RECONF_NPANELS 6

static char **events = NULL;
static int nevents = 0, negsize = 0;

static HWND logbox = NULL, abtbox = NULL;

static void gpps(void *handle, char *name, char *def, char *val, int len) {
    if (!read_setting_s(handle, name, val, len)) {
	strncpy(val, def, len);
	val[len-1] = '\0';
    }
}

static void gppi(void *handle, char *name, int def, int *i) {
    *i = read_setting_i(handle, name, def);
}

static HINSTANCE hinst;

static int readytogo;

static void save_settings (char *section, int do_host) {
    int i;
    char *p;
    void *sesskey;

    sesskey = open_settings_w(section);
    if (!sesskey)
        return;

    write_setting_i (sesskey, "Present", 1);
    if (do_host) {
	write_setting_s (sesskey, "HostName", cfg.host);
	write_setting_i (sesskey, "PortNumber", cfg.port);
        p = "raw";
        for (i = 0; backends[i].name != NULL; i++)
            if (backends[i].protocol == cfg.protocol) {
                p = backends[i].name;
                break;
            }
        write_setting_s (sesskey, "Protocol", p);
    }
    write_setting_i (sesskey, "CloseOnExit", !!cfg.close_on_exit);
    write_setting_i (sesskey, "WarnOnClose", !!cfg.warn_on_close);
    write_setting_s (sesskey, "TerminalType", cfg.termtype);
    write_setting_s (sesskey, "TerminalSpeed", cfg.termspeed);
    {
      char buf[2*sizeof(cfg.environmt)], *p, *q;
	p = buf;
      q = cfg.environmt;
	while (*q) {
	    while (*q) {
		int c = *q++;
		if (c == '=' || c == ',' || c == '\\')
		    *p++ = '\\';
		if (c == '\t')
		    c = '=';
		*p++ = c;
	    }
	    *p++ = ',';
	    q++;
	}
	*p = '\0';
	write_setting_s (sesskey, "Environment", buf);
    }
    write_setting_s (sesskey, "UserName", cfg.username);
    write_setting_i (sesskey, "NoPTY", cfg.nopty);
    write_setting_i (sesskey, "AgentFwd", cfg.agentfwd);
    write_setting_s (sesskey, "RemoteCmd", cfg.remote_cmd);
    write_setting_s (sesskey, "Cipher", cfg.cipher == CIPHER_BLOWFISH ? "blowfish" :
                             cfg.cipher == CIPHER_DES ? "des" : "3des");
    write_setting_i (sesskey, "AuthTIS", cfg.try_tis_auth);
    write_setting_i (sesskey, "SshProt", cfg.sshprot);
    write_setting_s (sesskey, "PublicKeyFile", cfg.keyfile);
    write_setting_s (sesskey, "RemoteCommand", cfg.remote_cmd);
    write_setting_i (sesskey, "RFCEnviron", cfg.rfc_environ);
    write_setting_i (sesskey, "BackspaceIsDelete", cfg.bksp_is_delete);
    write_setting_i (sesskey, "RXVTHomeEnd", cfg.rxvt_homeend);
    write_setting_i (sesskey, "LinuxFunctionKeys", cfg.funky_type);
    write_setting_i (sesskey, "ApplicationCursorKeys", cfg.app_cursor);
    write_setting_i (sesskey, "ApplicationKeypad", cfg.app_keypad);
    write_setting_i (sesskey, "NetHackKeypad", cfg.nethack_keypad);
    write_setting_i (sesskey, "AltF4", cfg.alt_f4);
    write_setting_i (sesskey, "AltSpace", cfg.alt_space);
    write_setting_i (sesskey, "LdiscTerm", cfg.ldisc_term);
    write_setting_i (sesskey, "BlinkCur", cfg.blink_cur);
    write_setting_i (sesskey, "Beep", cfg.beep);
    write_setting_i (sesskey, "ScrollbackLines", cfg.savelines);
    write_setting_i (sesskey, "DECOriginMode", cfg.dec_om);
    write_setting_i (sesskey, "AutoWrapMode", cfg.wrap_mode);
    write_setting_i (sesskey, "LFImpliesCR", cfg.lfhascr);
    write_setting_i (sesskey, "WinNameAlways", cfg.win_name_always);
    write_setting_s (sesskey, "WinTitle", cfg.wintitle);
    write_setting_i (sesskey, "TermWidth", cfg.width);
    write_setting_i (sesskey, "TermHeight", cfg.height);
    write_setting_s (sesskey, "Font", cfg.font);
    write_setting_i (sesskey, "FontIsBold", cfg.fontisbold);
    write_setting_i (sesskey, "FontCharSet", cfg.fontcharset);
    write_setting_i (sesskey, "FontHeight", cfg.fontheight);
    write_setting_i (sesskey, "FontVTMode", cfg.vtmode);
    write_setting_i (sesskey, "TryPalette", cfg.try_palette);
    write_setting_i (sesskey, "BoldAsColour", cfg.bold_colour);
    for (i=0; i<22; i++) {
	char buf[20], buf2[30];
	sprintf(buf, "Colour%d", i);
	sprintf(buf2, "%d,%d,%d", cfg.colours[i][0],
		cfg.colours[i][1], cfg.colours[i][2]);
	write_setting_s (sesskey, buf, buf2);
    }
    write_setting_i (sesskey, "MouseIsXterm", cfg.mouse_is_xterm);
    for (i=0; i<256; i+=32) {
	char buf[20], buf2[256];
	int j;
	sprintf(buf, "Wordness%d", i);
	*buf2 = '\0';
	for (j=i; j<i+32; j++) {
	    sprintf(buf2+strlen(buf2), "%s%d",
		    (*buf2 ? "," : ""), cfg.wordness[j]);
	}
	write_setting_s (sesskey, buf, buf2);
    }
    write_setting_i (sesskey, "KoiWinXlat", cfg.xlat_enablekoiwin);
    write_setting_i (sesskey, "88592Xlat", cfg.xlat_88592w1250);
    write_setting_i (sesskey, "CapsLockCyr", cfg.xlat_capslockcyr);
    write_setting_i (sesskey, "ScrollBar", cfg.scrollbar);
    write_setting_i (sesskey, "ScrollOnKey", cfg.scroll_on_key);
    write_setting_i (sesskey, "LockSize", cfg.locksize);
    write_setting_i (sesskey, "BCE", cfg.bce);
    write_setting_i (sesskey, "BlinkText", cfg.blinktext);

    close_settings_w(sesskey);
}

static void load_settings (char *section, int do_host) {
    int i;
    char prot[10];
    void *sesskey;

    sesskey = open_settings_r(section);

    gpps (sesskey, "HostName", "", cfg.host, sizeof(cfg.host));
    gppi (sesskey, "PortNumber", default_port, &cfg.port);

    gpps (sesskey, "Protocol", "default", prot, 10);
    cfg.protocol = default_protocol;
    for (i = 0; backends[i].name != NULL; i++)
        if (!strcmp(prot, backends[i].name)) {
            cfg.protocol = backends[i].protocol;
            break;
        }

    gppi (sesskey, "CloseOnExit", 1, &cfg.close_on_exit);
    gppi (sesskey, "WarnOnClose", 1, &cfg.warn_on_close);
    gpps (sesskey, "TerminalType", "xterm", cfg.termtype,
	  sizeof(cfg.termtype));
    gpps (sesskey, "TerminalSpeed", "38400,38400", cfg.termspeed,
	  sizeof(cfg.termspeed));
    {
      char buf[2*sizeof(cfg.environmt)], *p, *q;
	gpps (sesskey, "Environment", "", buf, sizeof(buf));
	p = buf;
	q = cfg.environmt;
	while (*p) {
	    while (*p && *p != ',') {
		int c = *p++;
		if (c == '=')
		    c = '\t';
		if (c == '\\')
		    c = *p++;
		*q++ = c;
	    }
	    if (*p == ',') p++;
	    *q++ = '\0';
	}
	*q = '\0';
    }
    gpps (sesskey, "UserName", "", cfg.username, sizeof(cfg.username));
    gppi (sesskey, "NoPTY", 0, &cfg.nopty);
    gppi (sesskey, "AgentFwd", 0, &cfg.agentfwd);
    gpps (sesskey, "RemoteCmd", "", cfg.remote_cmd, sizeof(cfg.remote_cmd));
    {
	char cipher[10];
	gpps (sesskey, "Cipher", "3des", cipher, 10);
	if (!strcmp(cipher, "blowfish"))
	    cfg.cipher = CIPHER_BLOWFISH;
	else if (!strcmp(cipher, "des"))
	    cfg.cipher = CIPHER_DES;
	else
	    cfg.cipher = CIPHER_3DES;
    }
    gppi (sesskey, "SshProt", 1, &cfg.sshprot);
    gppi (sesskey, "AuthTIS", 0, &cfg.try_tis_auth);
    gpps (sesskey, "PublicKeyFile", "", cfg.keyfile, sizeof(cfg.keyfile));
    gpps (sesskey, "RemoteCommand", "", cfg.remote_cmd,
          sizeof(cfg.remote_cmd));
    gppi (sesskey, "RFCEnviron", 0, &cfg.rfc_environ);
    gppi (sesskey, "BackspaceIsDelete", 1, &cfg.bksp_is_delete);
    gppi (sesskey, "RXVTHomeEnd", 0, &cfg.rxvt_homeend);
    gppi (sesskey, "LinuxFunctionKeys", 0, &cfg.funky_type);
    gppi (sesskey, "ApplicationCursorKeys", 0, &cfg.app_cursor);
    gppi (sesskey, "ApplicationKeypad", 0, &cfg.app_keypad);
    gppi (sesskey, "NetHackKeypad", 0, &cfg.nethack_keypad);
    gppi (sesskey, "AltF4", 1, &cfg.alt_f4);
    gppi (sesskey, "AltSpace", 0, &cfg.alt_space);
    gppi (sesskey, "LdiscTerm", 0, &cfg.ldisc_term);
    gppi (sesskey, "BlinkCur", 0, &cfg.blink_cur);
    gppi (sesskey, "Beep", 1, &cfg.beep);
    gppi (sesskey, "ScrollbackLines", 200, &cfg.savelines);
    gppi (sesskey, "DECOriginMode", 0, &cfg.dec_om);
    gppi (sesskey, "AutoWrapMode", 1, &cfg.wrap_mode);
    gppi (sesskey, "LFImpliesCR", 0, &cfg.lfhascr);
    gppi (sesskey, "WinNameAlways", 0, &cfg.win_name_always);
    gpps (sesskey, "WinTitle", "", cfg.wintitle, sizeof(cfg.wintitle));
    gppi (sesskey, "TermWidth", 80, &cfg.width);
    gppi (sesskey, "TermHeight", 24, &cfg.height);
    gpps (sesskey, "Font", "Courier", cfg.font, sizeof(cfg.font));
    gppi (sesskey, "FontIsBold", 0, &cfg.fontisbold);
    gppi (sesskey, "FontCharSet", ANSI_CHARSET, &cfg.fontcharset);
    gppi (sesskey, "FontHeight", 10, &cfg.fontheight);
    gppi (sesskey, "FontVTMode", VT_OEMANSI, (int *)&cfg.vtmode);
    gppi (sesskey, "TryPalette", 0, &cfg.try_palette);
    gppi (sesskey, "BoldAsColour", 1, &cfg.bold_colour);
    for (i=0; i<22; i++) {
	static char *defaults[] = {
	    "187,187,187", "255,255,255", "0,0,0", "85,85,85", "0,0,0",
	    "0,255,0", "0,0,0", "85,85,85", "187,0,0", "255,85,85",
	    "0,187,0", "85,255,85", "187,187,0", "255,255,85", "0,0,187",
	    "85,85,255", "187,0,187", "255,85,255", "0,187,187",
	    "85,255,255", "187,187,187", "255,255,255"
 	};
	char buf[20], buf2[30];
	int c0, c1, c2;
	sprintf(buf, "Colour%d", i);
	gpps (sesskey, buf, defaults[i], buf2, sizeof(buf2));
	if(sscanf(buf2, "%d,%d,%d", &c0, &c1, &c2) == 3) {
	    cfg.colours[i][0] = c0;
	    cfg.colours[i][1] = c1;
	    cfg.colours[i][2] = c2;
	}
    }
    gppi (sesskey, "MouseIsXterm", 0, &cfg.mouse_is_xterm);
    for (i=0; i<256; i+=32) {
	static char *defaults[] = {
	    "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0",
	    "0,1,2,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,1",
	    "1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,2",
	    "1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1",
	    "1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1",
	    "1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1",
	    "2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2",
	    "2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2"
	};
	char buf[20], buf2[256], *p;
	int j;
	sprintf(buf, "Wordness%d", i);
	gpps (sesskey, buf, defaults[i/32], buf2, sizeof(buf2));
	p = buf2;
	for (j=i; j<i+32; j++) {
	    char *q = p;
	    while (*p && *p != ',') p++;
	    if (*p == ',') *p++ = '\0';
	    cfg.wordness[j] = atoi(q);
	}
    }
    gppi (sesskey, "KoiWinXlat", 0, &cfg.xlat_enablekoiwin);
    gppi (sesskey, "88592Xlat", 0, &cfg.xlat_88592w1250);
    gppi (sesskey, "CapsLockCyr", 0, &cfg.xlat_capslockcyr);
    gppi (sesskey, "ScrollBar", 1, &cfg.scrollbar);
    gppi (sesskey, "ScrollOnKey", 0, &cfg.scroll_on_key);
    gppi (sesskey, "LockSize", 0, &cfg.locksize);
    gppi (sesskey, "BCE", 0, &cfg.bce);
    gppi (sesskey, "BlinkText", 0, &cfg.blinktext);

    close_settings_r(sesskey);
}

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
/*      case WM_CTLCOLORDLG: */
/*	return (int) GetStockObject (LTGRAY_BRUSH); */
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
                        write_clip(clipdata, size);
                        term_deselect();
                        free(clipdata);
                    }
                    free(selitems);
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
/*      case WM_CTLCOLORDLG: */
/*	return (int) GetStockObject (LTGRAY_BRUSH); */
/*      case WM_CTLCOLORSTATIC: */
/*	SetBkColor ((HDC)wParam, RGB(192,192,192)); */
/*	return (int) GetStockObject (LTGRAY_BRUSH); */
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
    LONG units;
    WPARAM font;
    int ypos, width;
};

/* Used on self-constructed dialogs. */
void ctlposinit(struct ctlpos *cp, HWND hwnd) {
    RECT r;
    cp->hwnd = hwnd;
    cp->units = GetWindowLong(hwnd, GWL_USERDATA);
    cp->font = GetWindowLong(hwnd, DWL_USER);
    cp->ypos = GAPBETWEEN;
    GetClientRect(hwnd, &r);
    cp->width = (r.right * 4) / (cp->units & 0xFFFF) - 2*GAPBETWEEN;
}

/* Used on kosher dialogs. */
void ctlposinit2(struct ctlpos *cp, HWND hwnd) {
    RECT r;
    cp->hwnd = hwnd;
    r.left = r.top = 0;
    r.right = 4;
    r.bottom = 8;
    MapDialogRect(hwnd, &r);
    cp->units = (r.bottom << 16) | r.right;
    cp->font = SendMessage(hwnd, WM_GETFONT, 0, 0);
    cp->ypos = GAPBETWEEN;
    GetClientRect(hwnd, &r);
    cp->width = (r.right * 4) / (cp->units & 0xFFFF) - 2*GAPBETWEEN;
}

void doctl(struct ctlpos *cp, RECT r, char *wclass, int wstyle, int exstyle,
           char *wtext, int wid) {
    HWND ctl;
    /*
     * Note nonstandard use of RECT. This is deliberate: by
     * transforming the width and height directly we arrange to
     * have all supposedly same-sized controls really same-sized.
     */

    /* MapDialogRect, or its near equivalent. */
    r.left = (r.left * (cp->units & 0xFFFF)) / 4;
    r.right = (r.right * (cp->units & 0xFFFF)) / 4;
    r.top = (r.top * ((cp->units>>16) & 0xFFFF)) / 8;
    r.bottom = (r.bottom * ((cp->units>>16) & 0xFFFF)) / 8;

    ctl = CreateWindowEx(exstyle, wclass, wtext, wstyle,
                         r.left, r.top, r.right, r.bottom,
                         cp->hwnd, (HMENU)wid, hinst, NULL);
    SendMessage(ctl, WM_SETFONT, cp->font, MAKELPARAM(TRUE, 0));
}

/*
 * Some edit boxes. Each one has a static above it. The percentages
 * of the horizontal space are provided.
 */
void multiedit(struct ctlpos *cp, ...) {
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
void radioline(struct ctlpos *cp, char *text, int id, int nacross, ...) {
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
void radiobig(struct ctlpos *cp, char *text, int id, ...) {
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
void checkbox(struct ctlpos *cp, char *text, int id) {
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
void staticbtn(struct ctlpos *cp, char *stext, int sid, char *btext, int bid) {
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
void staticedit(struct ctlpos *cp, char *stext, int sid, int eid) {
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
void ersatztab(struct ctlpos *cp, char *stext, int sid, int lid, int s2id) {
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
void editbutton(struct ctlpos *cp, char *stext, int sid,
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
void sesssaver(struct ctlpos *cp, char *text,
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
          "", staticid);

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
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_HASSTRINGS,
          WS_EX_CLIENTEDGE,
          "", listid);
}

/*
 * Another special control: the environment-variable setter. A
 * static line first; then a pair of edit boxes with associated
 * statics, and two buttons; then a list box.
 */
void envsetter(struct ctlpos *cp, char *stext, int sid,
               char *e1stext, int e1sid, int e1id,
               char *e2stext, int e2sid, int e2id,
               int listid, char *b1text, int b1id, char *b2text, int b2id) {
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
void charclass(struct ctlpos *cp, char *stext, int sid, int listid,
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
void colouredit(struct ctlpos *cp, char *stext, int sid, int listid,
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

static int GeneralPanelProc (HWND hwnd, UINT msg,
			     WPARAM wParam, LPARAM lParam) {
    switch (msg) {
      case WM_SETFONT:
        {
            HFONT hfont = (HFONT)wParam;
            HFONT oldfont;
            HDC hdc;
            TEXTMETRIC tm;
            LONG units;

            hdc = GetDC(hwnd);
            oldfont = SelectObject(hdc, hfont);
            GetTextMetrics(hdc, &tm);
            units = (tm.tmHeight << 16) | tm.tmAveCharWidth;
            SelectObject(hdc, oldfont);
            DeleteDC(hdc);
            SetWindowLong(hwnd, GWL_USERDATA, units);
            SetWindowLong(hwnd, DWL_USER, wParam);
        }
        return 0;
      case WM_INITDIALOG:
	SetWindowPos (hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	return 1;
      case WM_CLOSE:
	DestroyWindow (hwnd);
	return 1;
    }
    return 0;
}

static char savedsession[2048];

static int CALLBACK ConnectionProc (HWND hwnd, UINT msg,
				    WPARAM wParam, LPARAM lParam) {
    int i;
    struct ctlpos cp;

    switch (msg) {
      case WM_INITDIALOG:
        /* Accelerators used: [aco] dehlnprstwx */
        ctlposinit(&cp, hwnd);
        multiedit(&cp,
                  "Host &Name", IDC0_HOSTSTATIC, IDC0_HOST, 75,
                  "&Port", IDC0_PORTSTATIC, IDC0_PORT, 25, NULL);
        radioline(&cp, "Protocol:", IDC0_PROTSTATIC, 3,
                  "&Raw", IDC0_PROTRAW,
                  "&Telnet", IDC0_PROTTELNET,
#ifdef FWHACK
                  "SS&H/hack",
#else
                  "SS&H",
#endif
                  IDC0_PROTSSH, NULL);
        sesssaver(&cp, "Stor&ed Sessions",
                  IDC0_SESSSTATIC, IDC0_SESSEDIT, IDC0_SESSLIST,
                  "&Load", IDC0_SESSLOAD,
                  "&Save", IDC0_SESSSAVE,
                  "&Delete", IDC0_SESSDEL, NULL);
        checkbox(&cp, "Close Window on E&xit", IDC0_CLOSEEXIT);
        checkbox(&cp, "&Warn on Close", IDC0_CLOSEWARN);

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
	break;
      case WM_LBUTTONUP:
        /*
         * Button release should trigger WM_OK if there was a
         * previous double click on the session list.
         */
        ReleaseCapture();
        if (readytogo)
            SendMessage (GetParent(hwnd), WM_COMMAND, IDOK, 0);
        break;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
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
		save_settings (str, !!strcmp(str, "Default Settings"));
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
			       !!strcmp(sessions[n], "Default Settings"));
		SetDlgItemText (hwnd, IDC0_HOST, cfg.host);
		SetDlgItemInt (hwnd, IDC0_PORT, cfg.port, FALSE);
		CheckRadioButton (hwnd, IDC0_PROTRAW, IDC0_PROTSSH,
				  (cfg.protocol==PROT_SSH ? IDC0_PROTSSH :
				  cfg.protocol==PROT_TELNET ? IDC0_PROTTELNET : IDC0_PROTRAW));
		CheckDlgButton (hwnd, IDC0_CLOSEEXIT, cfg.close_on_exit);
		CheckDlgButton (hwnd, IDC0_CLOSEWARN, cfg.warn_on_close);
		SendDlgItemMessage (hwnd, IDC0_SESSLIST, LB_SETCURSEL,
				    (WPARAM) -1, 0);
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
	}
    }
    return GeneralPanelProc (hwnd, msg, wParam, lParam);
}

static int CALLBACK KeyboardProc (HWND hwnd, UINT msg,
                                  WPARAM wParam, LPARAM lParam) {
    struct ctlpos cp;
    switch (msg) {
      case WM_INITDIALOG:
        /* Accelerators used: [aco] 4?ehiklmnprsuvxy */
        ctlposinit(&cp, hwnd);
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
	break;
      case WM_COMMAND:
	if (HIWORD(wParam) == BN_CLICKED ||
	    HIWORD(wParam) == BN_DOUBLECLICKED)
	    switch (LOWORD(wParam)) {
	      case IDC1_DEL008:
	      case IDC1_DEL127:
		cfg.bksp_is_delete = IsDlgButtonChecked (hwnd, IDC1_DEL127);
		break;
	      case IDC1_HOMETILDE:
	      case IDC1_HOMERXVT:
		cfg.rxvt_homeend = IsDlgButtonChecked (hwnd, IDC1_HOMERXVT);
		break;
	      case IDC1_FUNCXTERM:
		cfg.funky_type = 2;
		break;
	      case IDC1_FUNCTILDE:
	      case IDC1_FUNCLINUX:
		cfg.funky_type = IsDlgButtonChecked (hwnd, IDC1_FUNCLINUX);
		break;
	      case IDC1_KPNORMAL:
	      case IDC1_KPAPPLIC:
		cfg.app_keypad = IsDlgButtonChecked (hwnd, IDC1_KPAPPLIC);
		cfg.nethack_keypad = FALSE;
		break;
	      case IDC1_KPNH:
		cfg.app_keypad = FALSE;
		cfg.nethack_keypad = TRUE;
		break;
	      case IDC1_CURNORMAL:
	      case IDC1_CURAPPLIC:
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
	    }
    }
    return GeneralPanelProc (hwnd, msg, wParam, lParam);
}

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

static int CALLBACK TerminalProc (HWND hwnd, UINT msg,
				    WPARAM wParam, LPARAM lParam) {
    struct ctlpos cp;
    CHOOSEFONT cf;
    LOGFONT lf;
    char fontstatic[256];

    switch (msg) {
      case WM_INITDIALOG:
        /* Accelerators used: [aco] dghlmnprsw */
        ctlposinit(&cp, hwnd);
        multiedit(&cp,
                  "&Rows", IDC2_ROWSSTATIC, IDC2_ROWSEDIT, 33,
                  "Colu&mns", IDC2_COLSSTATIC, IDC2_COLSEDIT, 33,
                  "&Scrollback", IDC2_SAVESTATIC, IDC2_SAVEEDIT, 33,
                  NULL);
        staticbtn(&cp, "", IDC2_FONTSTATIC, "C&hange...", IDC2_CHOOSEFONT);
        checkbox(&cp, "Auto &wrap mode initially on", IDC2_WRAPMODE);
        checkbox(&cp, "&DEC Origin Mode initially on", IDC2_DECOM);
        checkbox(&cp, "Implicit CR in every &LF", IDC2_LFHASCR);
        checkbox(&cp, "Bee&p enabled", IDC1_BEEP);
        checkbox(&cp, "Use Back&ground colour erase", IDC2_BCE);
        checkbox(&cp, "Enable bli&nking text", IDC2_BLINKTEXT);

	CheckDlgButton (hwnd, IDC2_WRAPMODE, cfg.wrap_mode);
	CheckDlgButton (hwnd, IDC2_DECOM, cfg.dec_om);
	CheckDlgButton (hwnd, IDC2_LFHASCR, cfg.lfhascr);
	SetDlgItemInt (hwnd, IDC2_ROWSEDIT, cfg.height, FALSE);
	SetDlgItemInt (hwnd, IDC2_COLSEDIT, cfg.width, FALSE);
	SetDlgItemInt (hwnd, IDC2_SAVEEDIT, cfg.savelines, FALSE);
	fmtfont (fontstatic);
	SetDlgItemText (hwnd, IDC2_FONTSTATIC, fontstatic);
        CheckDlgButton (hwnd, IDC1_BEEP, cfg.beep);
        CheckDlgButton (hwnd, IDC2_BCE, cfg.bce);
        CheckDlgButton (hwnd, IDC2_BLINKTEXT, cfg.blinktext);
	break;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
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
	   case IDC1_BEEP:
	     if (HIWORD(wParam) == BN_CLICKED ||
		 HIWORD(wParam) == BN_DOUBLECLICKED)
		 cfg.beep = IsDlgButtonChecked (hwnd, IDC1_BEEP);
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
	}
	break;
    }
    return GeneralPanelProc (hwnd, msg, wParam, lParam);
}

static int CALLBACK WindowProc (HWND hwnd, UINT msg,
				    WPARAM wParam, LPARAM lParam) {
    struct ctlpos cp;
    switch (msg) {
      case WM_INITDIALOG:
        /* Accelerators used: [aco] bikty */
        ctlposinit(&cp, hwnd);
        multiedit(&cp,
                  "Initial window &title:", IDCW_WINTITLE, IDCW_WINEDIT, 100,
                  NULL);
        checkbox(&cp, "Avoid ever using &icon title", IDCW_WINNAME);
        checkbox(&cp, "&Blinking cursor", IDCW_BLINKCUR);
        checkbox(&cp, "Displa&y scrollbar", IDCW_SCROLLBAR);
        checkbox(&cp, "Loc&k Window size", IDCW_LOCKSIZE);

	SetDlgItemText (hwnd, IDCW_WINEDIT, cfg.wintitle);
	CheckDlgButton (hwnd, IDCW_WINNAME, cfg.win_name_always);
	CheckDlgButton (hwnd, IDCW_BLINKCUR, cfg.blink_cur);
        CheckDlgButton (hwnd, IDCW_SCROLLBAR, cfg.scrollbar);
        CheckDlgButton (hwnd, IDCW_LOCKSIZE, cfg.locksize);
	break;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDCW_WINNAME:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.win_name_always = IsDlgButtonChecked (hwnd, IDCW_WINNAME);
	    break;
          case IDCW_BLINKCUR:
            if (HIWORD(wParam) == BN_CLICKED ||
                HIWORD(wParam) == BN_DOUBLECLICKED)
                cfg.blink_cur = IsDlgButtonChecked (hwnd, IDCW_BLINKCUR);
            break;
          case IDCW_SCROLLBAR:
            if (HIWORD(wParam) == BN_CLICKED ||
                HIWORD(wParam) == BN_DOUBLECLICKED)
                cfg.scrollbar = IsDlgButtonChecked (hwnd, IDCW_SCROLLBAR);
            break;
          case IDCW_LOCKSIZE:
	     if (HIWORD(wParam) == BN_CLICKED ||
		 HIWORD(wParam) == BN_DOUBLECLICKED)
                cfg.locksize = IsDlgButtonChecked (hwnd, IDCW_LOCKSIZE);
            break;
	  case IDCW_WINEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
		GetDlgItemText (hwnd, IDCW_WINEDIT, cfg.wintitle,
				sizeof(cfg.wintitle)-1);
	    break;
	}
	break;
    }
    return GeneralPanelProc (hwnd, msg, wParam, lParam);
}

static int CALLBACK TelnetProc (HWND hwnd, UINT msg,
				    WPARAM wParam, LPARAM lParam) {
    int i;
    struct ctlpos cp;

    switch (msg) {
      case WM_INITDIALOG:
        /* Accelerators used: [aco] bdflrstuv */
        ctlposinit(&cp, hwnd);
        staticedit(&cp, "Terminal-&type string", IDC3_TTSTATIC, IDC3_TTEDIT);
        staticedit(&cp, "Terminal-&speed string", IDC3_TSSTATIC, IDC3_TSEDIT);
        staticedit(&cp, "Auto-login &username", IDC3_LOGSTATIC, IDC3_LOGEDIT);
        envsetter(&cp, "Environment variables:", IDC3_ENVSTATIC,
                  "&Variable", IDC3_VARSTATIC, IDC3_VAREDIT,
                  "Va&lue", IDC3_VALSTATIC, IDC3_VALEDIT,
                  IDC3_ENVLIST,
                  "A&dd", IDC3_ENVADD, "&Remove", IDC3_ENVREMOVE);
        radioline(&cp, "Handling of OLD_ENVIRON ambiguity:", IDC3_EMSTATIC, 2,
                  "&BSD (commonplace)", IDC3_EMBSD,
                  "R&FC 1408 (unusual)", IDC3_EMRFC, NULL);

	SetDlgItemText (hwnd, IDC3_TTEDIT, cfg.termtype);
	SetDlgItemText (hwnd, IDC3_TSEDIT, cfg.termspeed);
	SetDlgItemText (hwnd, IDC3_LOGEDIT, cfg.username);
	{
          char *p = cfg.environmt;
	    while (*p) {
		SendDlgItemMessage (hwnd, IDC3_ENVLIST, LB_ADDSTRING, 0,
				    (LPARAM) p);
		p += strlen(p)+1;
	    }
	}
	CheckRadioButton (hwnd, IDC3_EMBSD, IDC3_EMRFC,
			  cfg.rfc_environ ? IDC3_EMRFC : IDC3_EMBSD);
	break;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDC3_TTEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
	    GetDlgItemText (hwnd, IDC3_TTEDIT, cfg.termtype,
			    sizeof(cfg.termtype)-1);
	    break;
	  case IDC3_TSEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
		GetDlgItemText (hwnd, IDC3_TSEDIT, cfg.termspeed,
				sizeof(cfg.termspeed)-1);
	    break;
	  case IDC3_LOGEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
		GetDlgItemText (hwnd, IDC3_LOGEDIT, cfg.username,
				sizeof(cfg.username)-1);
	    break;
	  case IDC3_EMBSD:
	  case IDC3_EMRFC:
	    cfg.rfc_environ = IsDlgButtonChecked (hwnd, IDC3_EMRFC);
	    break;
	  case IDC3_ENVADD:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
              char str[sizeof(cfg.environmt)];
		char *p;
		GetDlgItemText (hwnd, IDC3_VAREDIT, str, sizeof(str)-1);
		if (!*str) {
		    MessageBeep(0);
		    break;
		}
		p = str + strlen(str);
		*p++ = '\t';
		GetDlgItemText (hwnd, IDC3_VALEDIT, p, sizeof(str)-1-(p-str));
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
		    SendDlgItemMessage (hwnd, IDC3_ENVLIST, LB_ADDSTRING,
					0, (LPARAM)str);
		    SetDlgItemText (hwnd, IDC3_VAREDIT, "");
		    SetDlgItemText (hwnd, IDC3_VALEDIT, "");
		} else {
		    MessageBox(hwnd, "Environment too big", "PuTTY Error",
			       MB_OK | MB_ICONERROR);
		}
	    }
	    break;
	  case IDC3_ENVREMOVE:
	    if (HIWORD(wParam) != BN_CLICKED &&
		HIWORD(wParam) != BN_DOUBLECLICKED)
		break;
	    i = SendDlgItemMessage (hwnd, IDC3_ENVLIST, LB_GETCURSEL, 0, 0);
	    if (i == LB_ERR)
		MessageBeep (0);
	    else {
		char *p, *q;

	        SendDlgItemMessage (hwnd, IDC3_ENVLIST, LB_DELETESTRING,
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
	}
	break;
    }
    return GeneralPanelProc (hwnd, msg, wParam, lParam);
}

static int CALLBACK SshProc (HWND hwnd, UINT msg,
			     WPARAM wParam, LPARAM lParam) {
    struct ctlpos cp;
    OPENFILENAME of;
    char filename[sizeof(cfg.keyfile)];

    switch (msg) {
      case WM_INITDIALOG:
        /* Accelerators used: [aco] 123abdkmprtuw */
        ctlposinit(&cp, hwnd);
        staticedit(&cp, "Terminal-&type string", IDC3_TTSTATIC, IDC3_TTEDIT);
        staticedit(&cp, "Auto-login &username", IDC3_LOGSTATIC, IDC3_LOGEDIT);
        multiedit(&cp,
                  "&Remote command:", IDC3_CMDSTATIC, IDC3_CMDEDIT, 100,
                  NULL);
        checkbox(&cp, "Don't allocate a &pseudo-terminal", IDC3_NOPTY);
        checkbox(&cp, "Atte&mpt TIS or CryptoCard authentication",
                 IDC3_AUTHTIS);
        checkbox(&cp, "Allow &agent forwarding", IDC3_AGENTFWD);
        editbutton(&cp, "Private &key file for authentication:",
                    IDC3_PKSTATIC, IDC3_PKEDIT, "Bro&wse...", IDC3_PKBUTTON);
        radioline(&cp, "Preferred SSH protocol version:",
                  IDC3_SSHPROTSTATIC, 2,
                  "&1", IDC3_SSHPROT1, "&2", IDC3_SSHPROT2, NULL);
        radioline(&cp, "Preferred encryption algorithm:", IDC3_CIPHERSTATIC, 3,
                  "&3DES", IDC3_CIPHER3DES,
                  "&Blowfish", IDC3_CIPHERBLOWF,
                  "&DES", IDC3_CIPHERDES, NULL);    

	SetDlgItemText (hwnd, IDC3_TTEDIT, cfg.termtype);
	SetDlgItemText (hwnd, IDC3_LOGEDIT, cfg.username);
	CheckDlgButton (hwnd, IDC3_NOPTY, cfg.nopty);
	CheckDlgButton (hwnd, IDC3_AGENTFWD, cfg.agentfwd);
	CheckRadioButton (hwnd, IDC3_CIPHER3DES, IDC3_CIPHERDES,
			  cfg.cipher == CIPHER_BLOWFISH ? IDC3_CIPHERBLOWF :
			  cfg.cipher == CIPHER_DES ? IDC3_CIPHERDES :
			  IDC3_CIPHER3DES);
	CheckRadioButton (hwnd, IDC3_SSHPROT1, IDC3_SSHPROT2,
			  cfg.sshprot == 1 ? IDC3_SSHPROT1 : IDC3_SSHPROT2);
	CheckDlgButton (hwnd, IDC3_AUTHTIS, cfg.try_tis_auth);
	SetDlgItemText (hwnd, IDC3_PKEDIT, cfg.keyfile);
	SetDlgItemText (hwnd, IDC3_CMDEDIT, cfg.remote_cmd);
	break;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDC3_TTEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
	    GetDlgItemText (hwnd, IDC3_TTEDIT, cfg.termtype,
			    sizeof(cfg.termtype)-1);
	    break;
	  case IDC3_LOGEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
		GetDlgItemText (hwnd, IDC3_LOGEDIT, cfg.username,
				sizeof(cfg.username)-1);
	    break;
	  case IDC3_NOPTY:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.nopty = IsDlgButtonChecked (hwnd, IDC3_NOPTY);
	    break;
	  case IDC3_AGENTFWD:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.agentfwd = IsDlgButtonChecked (hwnd, IDC3_AGENTFWD);
	    break;
	  case IDC3_CIPHER3DES:
	  case IDC3_CIPHERBLOWF:
	  case IDC3_CIPHERDES:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		if (IsDlgButtonChecked (hwnd, IDC3_CIPHER3DES))
		    cfg.cipher = CIPHER_3DES;
		else if (IsDlgButtonChecked (hwnd, IDC3_CIPHERBLOWF))
		    cfg.cipher = CIPHER_BLOWFISH;
		else if (IsDlgButtonChecked (hwnd, IDC3_CIPHERDES))
		    cfg.cipher = CIPHER_DES;
	    }
	    break;
	  case IDC3_SSHPROT1:
	  case IDC3_SSHPROT2:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		if (IsDlgButtonChecked (hwnd, IDC3_SSHPROT1))
		    cfg.sshprot = 1;
		else if (IsDlgButtonChecked (hwnd, IDC3_SSHPROT2))
		    cfg.sshprot = 2;
	    }
	    break;
	  case IDC3_AUTHTIS:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.try_tis_auth = IsDlgButtonChecked (hwnd, IDC3_AUTHTIS);
	    break;
	  case IDC3_PKEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
		GetDlgItemText (hwnd, IDC3_PKEDIT, cfg.keyfile,
				sizeof(cfg.keyfile)-1);
	    break;
	  case IDC3_CMDEDIT:
	    if (HIWORD(wParam) == EN_CHANGE)
		GetDlgItemText (hwnd, IDC3_CMDEDIT, cfg.remote_cmd,
				sizeof(cfg.remote_cmd)-1);
	    break;
	  case IDC3_PKBUTTON:
            /*
             * FIXME: this crashes. Find out why.
             */
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
                SetDlgItemText (hwnd, IDC3_PKEDIT, cfg.keyfile);
            }
	    break;
	}
	break;
    }
    return GeneralPanelProc (hwnd, msg, wParam, lParam);
}

static int CALLBACK SelectionProc (HWND hwnd, UINT msg,
				    WPARAM wParam, LPARAM lParam) {
    struct ctlpos cp;
    int i;

    switch (msg) {
      case WM_INITDIALOG:
        /* Accelerators used: [aco] stwx */
        ctlposinit(&cp, hwnd);
        radiobig(&cp, "Action of mouse buttons:", IDC4_MBSTATIC,
                 "&Windows (Right pastes, Middle extends)", IDC4_MBWINDOWS,
                 "&xterm (Right extends, Middle pastes)", IDC4_MBXTERM,
                 NULL);
        charclass(&cp, "Character classes:", IDC4_CCSTATIC, IDC4_CCLIST,
                  "&Set", IDC4_CCSET, IDC4_CCEDIT,
                  "&to class", IDC4_CCSTATIC2);

	CheckRadioButton (hwnd, IDC4_MBWINDOWS, IDC4_MBXTERM,
			  cfg.mouse_is_xterm ? IDC4_MBXTERM : IDC4_MBWINDOWS);
	{
	    static int tabs[4] = {25, 61, 96, 128};
	    SendDlgItemMessage (hwnd, IDC4_CCLIST, LB_SETTABSTOPS, 4,
				(LPARAM) tabs);
	}
	for (i=0; i<256; i++) {
	    char str[100];
	    sprintf(str, "%d\t(0x%02X)\t%c\t%d", i, i,
		    (i>=0x21 && i != 0x7F) ? i : ' ',
		    cfg.wordness[i]);
	    SendDlgItemMessage (hwnd, IDC4_CCLIST, LB_ADDSTRING, 0,
				(LPARAM) str);
	}
	break;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDC4_MBWINDOWS:
	  case IDC4_MBXTERM:
	    cfg.mouse_is_xterm = IsDlgButtonChecked (hwnd, IDC4_MBXTERM);
	    break;
	  case IDC4_CCSET:
	    {
		BOOL ok;
		int i;
		int n = GetDlgItemInt (hwnd, IDC4_CCEDIT, &ok, FALSE);

		if (!ok)
		    MessageBeep (0);
		else {
		    for (i=0; i<256; i++)
			if (SendDlgItemMessage (hwnd, IDC4_CCLIST, LB_GETSEL,
						i, 0)) {
			    char str[100];
			    cfg.wordness[i] = n;
			    SendDlgItemMessage (hwnd, IDC4_CCLIST,
						LB_DELETESTRING, i, 0);
			    sprintf(str, "%d\t(0x%02X)\t%c\t%d", i, i,
				    (i>=0x21 && i != 0x7F) ? i : ' ',
				    cfg.wordness[i]);
			    SendDlgItemMessage (hwnd, IDC4_CCLIST,
						LB_INSERTSTRING, i,
						(LPARAM)str);
			}
		}
	    }
	    break;
	}
	break;
    }
    return GeneralPanelProc (hwnd, msg, wParam, lParam);
}

static int CALLBACK ColourProc (HWND hwnd, UINT msg,
				    WPARAM wParam, LPARAM lParam) {
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
    static const int permanent[] = {
	TRUE, FALSE, TRUE, FALSE, TRUE, TRUE,
	TRUE, FALSE, TRUE, FALSE, TRUE, FALSE, TRUE, FALSE,
	TRUE, FALSE, TRUE, FALSE, TRUE, FALSE, TRUE, FALSE
    };
    struct ctlpos cp;

    switch (msg) {
      case WM_INITDIALOG:
        /* Accelerators used: [aco] bmlu */
        ctlposinit(&cp, hwnd);
        checkbox(&cp, "&Bolded text is a different colour", IDC5_BOLDCOLOUR);
        checkbox(&cp, "Attempt to use &logical palettes", IDC5_PALETTE);
        colouredit(&cp, "Select a colo&ur and click to modify it:",
                   IDC5_STATIC, IDC5_LIST,
                   "&Modify...", IDC5_CHANGE,
                   "Red:", IDC5_RSTATIC, IDC5_RVALUE,
                   "Green:", IDC5_GSTATIC, IDC5_GVALUE,
                   "Blue:", IDC5_BSTATIC, IDC5_BVALUE, NULL);

	CheckDlgButton (hwnd, IDC5_BOLDCOLOUR, cfg.bold_colour);
	CheckDlgButton (hwnd, IDC5_PALETTE, cfg.try_palette);
	{
	    int i;
	    for (i=0; i<22; i++)
		if (cfg.bold_colour || permanent[i])
		    SendDlgItemMessage (hwnd, IDC5_LIST, LB_ADDSTRING, 0,
					(LPARAM) colours[i]);
	}
	SendDlgItemMessage (hwnd, IDC5_LIST, LB_SETCURSEL, 0, 0);
	SetDlgItemInt (hwnd, IDC5_RVALUE, cfg.colours[0][0], FALSE);
	SetDlgItemInt (hwnd, IDC5_GVALUE, cfg.colours[0][1], FALSE);
	SetDlgItemInt (hwnd, IDC5_BVALUE, cfg.colours[0][2], FALSE);
	break;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDC5_BOLDCOLOUR:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		int n, i;
		cfg.bold_colour = IsDlgButtonChecked (hwnd, IDC5_BOLDCOLOUR);
		n = SendDlgItemMessage (hwnd, IDC5_LIST, LB_GETCOUNT, 0, 0);
		if (cfg.bold_colour && n!=22) {
		    for (i=0; i<22; i++)
			if (!permanent[i])
			    SendDlgItemMessage (hwnd, IDC5_LIST,
						LB_INSERTSTRING, i,
						(LPARAM) colours[i]);
		} else if (!cfg.bold_colour && n!=12) {
		    for (i=22; i-- ;)
			if (!permanent[i])
			    SendDlgItemMessage (hwnd, IDC5_LIST,
						LB_DELETESTRING, i, 0);
		}
	    }
	    break;
	  case IDC5_PALETTE:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.try_palette = IsDlgButtonChecked (hwnd, IDC5_PALETTE);
	    break;
	  case IDC5_LIST:
	    if (HIWORD(wParam) == LBN_DBLCLK ||
		HIWORD(wParam) == LBN_SELCHANGE) {
		int i = SendDlgItemMessage (hwnd, IDC5_LIST, LB_GETCURSEL,
					    0, 0);
		if (!cfg.bold_colour)
		    i = (i < 3 ? i*2 : i == 3 ? 5 : i*2-2);
		SetDlgItemInt (hwnd, IDC5_RVALUE, cfg.colours[i][0], FALSE);
		SetDlgItemInt (hwnd, IDC5_GVALUE, cfg.colours[i][1], FALSE);
		SetDlgItemInt (hwnd, IDC5_BVALUE, cfg.colours[i][2], FALSE);
	    }
	    break;
	  case IDC5_CHANGE:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		static CHOOSECOLOR cc;
		static DWORD custom[16] = {0};   /* zero initialisers */
		int i = SendDlgItemMessage (hwnd, IDC5_LIST, LB_GETCURSEL,
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
		    SetDlgItemInt (hwnd, IDC5_RVALUE, cfg.colours[i][0],
				   FALSE);
		    SetDlgItemInt (hwnd, IDC5_GVALUE, cfg.colours[i][1],
				   FALSE);
		    SetDlgItemInt (hwnd, IDC5_BVALUE, cfg.colours[i][2],
				   FALSE);
		}
	    }
	    break;
	}
	break;
    }
    return GeneralPanelProc (hwnd, msg, wParam, lParam);
}

static int CALLBACK TranslationProc (HWND hwnd, UINT msg,
				  WPARAM wParam, LPARAM lParam) {
    struct ctlpos cp;

    switch (msg) {
      case WM_INITDIALOG:
        /* Accelerators used: [aco] beiknpsx */
        ctlposinit(&cp, hwnd);
        radiobig(&cp,
                 "Handling of VT100 line drawing characters:", IDC2_VTSTATIC,
                 "Font has &XWindows encoding", IDC2_VTXWINDOWS,
                 "Use font in &both ANSI and OEM modes", IDC2_VTOEMANSI,
                 "Use font in O&EM mode only", IDC2_VTOEMONLY,
                 "&Poor man's line drawing (""+"", ""-"" and ""|"")",
                 IDC2_VTPOORMAN, NULL);
        radiobig(&cp,
                 "Character set translation:", IDC6_XLATSTATIC,
                 "&None", IDC6_NOXLAT,
                 "&KOI8 / Win-1251", IDC6_KOI8WIN1251,
                 "&ISO-8859-2 / Win-1250", IDC6_88592WIN1250, NULL);
        checkbox(&cp, "CAP&S LOCK acts as cyrillic switch", IDC6_CAPSLOCKCYR);

	CheckRadioButton (hwnd, IDC6_NOXLAT, IDC6_88592WIN1250,
			  cfg.xlat_88592w1250 ? IDC6_88592WIN1250 :
			  cfg.xlat_enablekoiwin ? IDC6_KOI8WIN1251 :
			  IDC6_NOXLAT);
	CheckDlgButton (hwnd, IDC6_CAPSLOCKCYR, cfg.xlat_capslockcyr);
	CheckRadioButton (hwnd, IDC2_VTXWINDOWS, IDC2_VTPOORMAN,
			  cfg.vtmode == VT_XWINDOWS ? IDC2_VTXWINDOWS :
			  cfg.vtmode == VT_OEMANSI ? IDC2_VTOEMANSI :
			  cfg.vtmode == VT_OEMONLY ? IDC2_VTOEMONLY :
			  IDC2_VTPOORMAN);
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDC6_NOXLAT:
	  case IDC6_KOI8WIN1251:
	  case IDC6_88592WIN1250:
	    cfg.xlat_enablekoiwin =
		IsDlgButtonChecked (hwnd, IDC6_KOI8WIN1251);
	    cfg.xlat_88592w1250 =
		IsDlgButtonChecked (hwnd, IDC6_88592WIN1250);
	    break;
	  case IDC6_CAPSLOCKCYR:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		cfg.xlat_capslockcyr =
		    IsDlgButtonChecked (hwnd, IDC6_CAPSLOCKCYR);
	    }
	    break;
	  case IDC2_VTXWINDOWS:
	  case IDC2_VTOEMANSI:
	  case IDC2_VTOEMONLY:
	  case IDC2_VTPOORMAN:
	    cfg.vtmode =
		(IsDlgButtonChecked (hwnd, IDC2_VTXWINDOWS) ? VT_XWINDOWS :
		 IsDlgButtonChecked (hwnd, IDC2_VTOEMANSI) ? VT_OEMANSI :
		 IsDlgButtonChecked (hwnd, IDC2_VTOEMONLY) ? VT_OEMONLY :
		 VT_POORMAN);
	    break;
	}
    }
    return GeneralPanelProc (hwnd, msg, wParam, lParam);
}

static DLGPROC panelproc[NPANELS] = {
    ConnectionProc, KeyboardProc, TerminalProc, WindowProc,
    TelnetProc, SshProc, SelectionProc, ColourProc, TranslationProc
};
static char *panelids[NPANELS] = {
    MAKEINTRESOURCE(IDD_PANEL0),
    MAKEINTRESOURCE(IDD_PANEL1),
    MAKEINTRESOURCE(IDD_PANEL2),
    MAKEINTRESOURCE(IDD_PANELW),
    MAKEINTRESOURCE(IDD_PANEL3),
    MAKEINTRESOURCE(IDD_PANEL35),
    MAKEINTRESOURCE(IDD_PANEL4),
    MAKEINTRESOURCE(IDD_PANEL5),
    MAKEINTRESOURCE(IDD_PANEL6)
};

static char *names[NPANELS] = {
    "Connection", "Keyboard", "Terminal", "Window", "Telnet",
    "SSH", "Selection", "Colours", "Translation"
};

static int mainp[MAIN_NPANELS] = { 0, 1, 2, 3, 4, 5, 6, 7, 8};
static int reconfp[RECONF_NPANELS] = { 1, 2, 3, 6, 7, 8};

static HWND makesubdialog(HWND hwnd, int x, int y, int w, int h, int n) {
    RECT r;
    HWND ret;
    WPARAM font;
    r.left = x; r.top = y;
    r.right = r.left + w; r.bottom = r.top + h;
    MapDialogRect(hwnd, &r);
    ret = CreateWindowEx(WS_EX_CONTROLPARENT,
                           WC_DIALOG, "",   /* no title */
                           WS_CHILD | WS_VISIBLE | DS_SETFONT,
                           r.left, r.top,
                           r.right-r.left, r.bottom-r.top,
                           hwnd, (HMENU)(panelids[n]),
                           hinst, NULL);
    SetWindowLong (ret, DWL_DLGPROC, (LONG)panelproc[n]);
    font = SendMessage(hwnd, WM_GETFONT, 0, 0);
    SendMessage (ret, WM_SETFONT, font, MAKELPARAM(0, 0));
    SendMessage (ret, WM_INITDIALOG, 0, 0);
    return ret;
}

static int GenericMainDlgProc (HWND hwnd, UINT msg,
			       WPARAM wParam, LPARAM lParam,
			       int npanels, int *panelnums, HWND *page) {
    HWND hw, tabctl;

    switch (msg) {
      case WM_INITDIALOG:
	{			       /* centre the window */
	    RECT rs, rd;

	    hw = GetDesktopWindow();
	    if (GetWindowRect (hw, &rs) && GetWindowRect (hwnd, &rd))
		MoveWindow (hwnd, (rs.right + rs.left + rd.left - rd.right)/2,
			    (rs.bottom + rs.top + rd.top - rd.bottom)/2,
			    rd.right-rd.left, rd.bottom-rd.top, TRUE);
	}
        {
            RECT r;
            r.left = 3; r.right = r.left + 174;
            r.top = 3; r.bottom = r.top + 193;
            MapDialogRect(hwnd, &r);
            tabctl = CreateWindowEx(0, WC_TABCONTROL, "",
                                    WS_CHILD | WS_VISIBLE |
                                    WS_TABSTOP | TCS_MULTILINE,
                                    r.left, r.top,
                                    r.right-r.left, r.bottom-r.top,
                                    hwnd, (HMENU)IDC_TAB, hinst, NULL);

            if (!tabctl) {
                struct ctlpos cp;
                ctlposinit2(&cp, hwnd);
                ersatztab(&cp, "Category:", IDC_TABSTATIC1, IDC_TABLIST,
                          IDC_TABSTATIC2);
            } else {
                WPARAM font = SendMessage(hwnd, WM_GETFONT, 0, 0);
                SendMessage(tabctl, WM_SETFONT, font, MAKELPARAM(TRUE, 0));
            }
        }
	*page = NULL;
	if (tabctl) {                  /* initialise the tab control */
	    TC_ITEMHEADER tab;
	    int i;

	    for (i=0; i<npanels; i++) {
		tab.mask = TCIF_TEXT;
		tab.pszText = names[panelnums[i]];
		TabCtrl_InsertItem (tabctl, i, &tab);
	    }
        } else {
	    int i;

	    for (i=0; i<npanels; i++) {
                SendDlgItemMessage(hwnd, IDC_TABLIST, CB_ADDSTRING,
                                   0, (LPARAM)names[panelnums[i]]);
	    }
            SendDlgItemMessage(hwnd, IDC_TABLIST, CB_SETCURSEL, 0, 0);
        }
        *page = makesubdialog(hwnd, 6, 30, 168, 163, panelnums[0]);
	SetFocus (*page);
	return 0;
      case WM_NOTIFY:
	if (LOWORD(wParam) == IDC_TAB &&
	    ((LPNMHDR)lParam)->code == TCN_SELCHANGE) {
	    int i = TabCtrl_GetCurSel(((LPNMHDR)lParam)->hwndFrom);
	    if (*page)
		DestroyWindow (*page);
            *page = makesubdialog(hwnd, 6, 30, 168, 163, panelnums[i]);
	    SetFocus (((LPNMHDR)lParam)->hwndFrom);   /* ensure focus stays */
	    return 0;
	}
	break;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
          case IDC_TABLIST:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                HWND tablist = GetDlgItem (hwnd, IDC_TABLIST);
                int i = SendMessage (tablist, CB_GETCURSEL, 0, 0);
                if (*page)
                    DestroyWindow (*page);
                *page = makesubdialog(hwnd, 6, 30, 168, 163, panelnums[i]);
                SetFocus(tablist);     /* ensure focus stays */
                return 0;
            }
            break;
	  case IDOK:
	    if (*cfg.host)
		EndDialog (hwnd, 1);
	    else
		MessageBeep (0);
	    return 0;
	  case IDCANCEL:
	    EndDialog (hwnd, 0);
	    return 0;
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
#if 0
    HWND hw;
    int i;
#endif
    static HWND page = NULL;

    if (msg == WM_COMMAND && LOWORD(wParam) == IDOK) {
#if 0
	/*
	 * If the Connection panel is active and the Session List
	 * box is selected, we treat a press of Open to have an
	 * implicit press of Load preceding it.
	 */
	hw = GetDlgItem (hwnd, IDC_TAB);
	i = TabCtrl_GetCurSel(hw);
	if (panelproc[mainp[i]] == ConnectionProc &&
	    page && implicit_load_ok) {
	    SendMessage (page, WM_COMMAND,
			 MAKELONG(IDC0_SESSLOAD, BN_CLICKED), 0);
	}
#endif
    }
    if (msg == WM_COMMAND && LOWORD(wParam) == IDC_ABOUT) {
	EnableWindow(hwnd, 0);
	DialogBox(hinst, MAKEINTRESOURCE(IDD_ABOUTBOX),
		  GetParent(hwnd), AboutProc);
	EnableWindow(hwnd, 1);
        SetActiveWindow(hwnd);
    }
    return GenericMainDlgProc (hwnd, msg, wParam, lParam,
			       MAIN_NPANELS, mainp, &page);
}

static int CALLBACK ReconfDlgProc (HWND hwnd, UINT msg,
				   WPARAM wParam, LPARAM lParam) {
    static HWND page;
    return GenericMainDlgProc (hwnd, msg, wParam, lParam,
			       RECONF_NPANELS, reconfp, &page);
}

void get_sesslist(int allocate) {
    static char otherbuf[2048];
    static char *buffer;
    int buflen, bufsize, i;
    char *p, *ret;
    void *handle;

    if (allocate) {
        
	if ((handle = enum_settings_start()) == NULL)
	    return;

	buflen = bufsize = 0;
	buffer = NULL;
	do {
            ret = enum_settings_next(handle, otherbuf, sizeof(otherbuf));
	    if (ret) {
                int len = strlen(otherbuf)+1;
                if (bufsize < buflen+len) {
                    bufsize = buflen + len + 2048;
                    buffer = srealloc(buffer, bufsize);
                }
		strcpy(buffer+buflen, otherbuf);
		buflen += strlen(buffer+buflen)+1;
	    }
	} while (ret);
        enum_settings_finish(handle);
	buffer = srealloc(buffer, buflen+1);
	buffer[buflen] = '\0';

	p = buffer;
	nsessions = 1;		       /* "Default Settings" counts as one */
	while (*p) {
	    if (strcmp(p, "Default Settings"))
		nsessions++;
	    while (*p) p++;
	    p++;
	}

	sessions = smalloc(nsessions * sizeof(char *));
	sessions[0] = "Default Settings";
	p = buffer;
	i = 1;
	while (*p) {
	    if (strcmp(p, "Default Settings"))
		sessions[i++] = p;
	    while (*p) p++;
	    p++;
	}
    } else {
	sfree (buffer);
	sfree (sessions);
    }
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

void do_defaults (char *session) {
    if (session)
	load_settings (session, TRUE);
    else
	load_settings ("Default Settings", FALSE);
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
