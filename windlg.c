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

#define NPANELS 8
#define MAIN_NPANELS 8
#define RECONF_NPANELS 5

static const char *const puttystr = PUTTY_REG_POS "\\Sessions";

static char **events = NULL;
static int nevents = 0, negsize = 0;

static HWND logbox = NULL, abtbox = NULL;

static char hex[16] = "0123456789ABCDEF";

static void mungestr(char *in, char *out) {
    int candot = 0;

    while (*in) {
	if (*in == ' ' || *in == '\\' || *in == '*' || *in == '?' ||
	    *in == '%' || *in < ' ' || *in > '~' || (*in == '.' && !candot)) {
	    *out++ = '%';
	    *out++ = hex[((unsigned char)*in) >> 4];
	    *out++ = hex[((unsigned char)*in) & 15];
	} else
	    *out++ = *in;
	in++;
	candot = 1;
    }
    *out = '\0';
    return;
}

static void unmungestr(char *in, char *out) {
    while (*in) {
	if (*in == '%' && in[1] && in[2]) {
	    int i, j;

	    i = in[1] - '0'; i -= (i > 9 ? 7 : 0);
	    j = in[2] - '0'; j -= (j > 9 ? 7 : 0);

	    *out++ = (i<<4) + j;
	    in += 3;
	} else
	    *out++ = *in++;
    }
    *out = '\0';
    return;
}

static void wpps(HKEY key, LPCTSTR name, LPCTSTR value) {
    RegSetValueEx(key, name, 0, REG_SZ, value, 1+strlen(value));
}

static void wppi(HKEY key, LPCTSTR name, int value) {
    RegSetValueEx(key, name, 0, REG_DWORD,
		  (CONST BYTE *)&value, sizeof(value));
}

static void gpps(HKEY key, LPCTSTR name, LPCTSTR def,
		 LPTSTR val, int len) {
    DWORD type, size;
    size = len;

    if (key == NULL ||
	RegQueryValueEx(key, name, 0, &type, val, &size) != ERROR_SUCCESS ||
	type != REG_SZ) {
	strncpy(val, def, len);
	val[len-1] = '\0';
    }
}

static void gppi(HKEY key, LPCTSTR name, int def, int *i) {
    DWORD type, val, size;
    size = sizeof(val);

    if (key == NULL ||
	RegQueryValueEx(key, name, 0, &type,
			(BYTE *)&val, &size) != ERROR_SUCCESS ||
	size != sizeof(val) || type != REG_DWORD)
	*i = def;
    else
	*i = val;
}

static HINSTANCE hinst;

static int readytogo;

static void save_settings (char *section, int do_host) {
    int i;
    HKEY subkey1, sesskey;
    char *p;

    p = malloc(3*strlen(section)+1);
    mungestr(section, p);
    
    if (RegCreateKey(HKEY_CURRENT_USER, puttystr, &subkey1)!=ERROR_SUCCESS ||
	RegCreateKey(subkey1, p, &sesskey) != ERROR_SUCCESS) {
	sesskey = NULL;
    }

    free(p);
    RegCloseKey(subkey1);

    wppi (sesskey, "Present", 1);
    if (do_host) {
	wpps (sesskey, "HostName", cfg.host);
	wppi (sesskey, "PortNumber", cfg.port);
        p = "raw";
        for (i = 0; backends[i].name != NULL; i++)
            if (backends[i].protocol == cfg.protocol) {
                p = backends[i].name;
                break;
            }
        wpps (sesskey, "Protocol", p);
    }
    wppi (sesskey, "CloseOnExit", !!cfg.close_on_exit);
    wppi (sesskey, "WarnOnClose", !!cfg.warn_on_close);
    wpps (sesskey, "TerminalType", cfg.termtype);
    wpps (sesskey, "TerminalSpeed", cfg.termspeed);
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
	wpps (sesskey, "Environment", buf);
    }
    wpps (sesskey, "UserName", cfg.username);
    wppi (sesskey, "NoPTY", cfg.nopty);
    wppi (sesskey, "AgentFwd", cfg.agentfwd);
    wpps (sesskey, "RemoteCmd", cfg.remote_cmd);
    wpps (sesskey, "Cipher", cfg.cipher == CIPHER_BLOWFISH ? "blowfish" :
                             cfg.cipher == CIPHER_DES ? "des" : "3des");
    wppi (sesskey, "AuthTIS", cfg.try_tis_auth);
    wppi (sesskey, "SshProt", cfg.sshprot);
    wpps (sesskey, "PublicKeyFile", cfg.keyfile);
    wppi (sesskey, "RFCEnviron", cfg.rfc_environ);
    wppi (sesskey, "BackspaceIsDelete", cfg.bksp_is_delete);
    wppi (sesskey, "RXVTHomeEnd", cfg.rxvt_homeend);
    wppi (sesskey, "LinuxFunctionKeys", cfg.funky_type);
    wppi (sesskey, "ApplicationCursorKeys", cfg.app_cursor);
    wppi (sesskey, "ApplicationKeypad", cfg.app_keypad);
    wppi (sesskey, "NetHackKeypad", cfg.nethack_keypad);
    wppi (sesskey, "AltF4", cfg.alt_f4);
    wppi (sesskey, "AltSpace", cfg.alt_space);
    wppi (sesskey, "LdiscTerm", cfg.ldisc_term);
    wppi (sesskey, "BlinkCur", cfg.blink_cur);
    wppi (sesskey, "Beep", cfg.beep);
    wppi (sesskey, "ScrollbackLines", cfg.savelines);
    wppi (sesskey, "DECOriginMode", cfg.dec_om);
    wppi (sesskey, "AutoWrapMode", cfg.wrap_mode);
    wppi (sesskey, "LFImpliesCR", cfg.lfhascr);
    wppi (sesskey, "WinNameAlways", cfg.win_name_always);
    wppi (sesskey, "TermWidth", cfg.width);
    wppi (sesskey, "TermHeight", cfg.height);
    wpps (sesskey, "Font", cfg.font);
    wppi (sesskey, "FontIsBold", cfg.fontisbold);
    wppi (sesskey, "FontCharSet", cfg.fontcharset);
    wppi (sesskey, "FontHeight", cfg.fontheight);
    wppi (sesskey, "FontVTMode", cfg.vtmode);
    wppi (sesskey, "TryPalette", cfg.try_palette);
    wppi (sesskey, "BoldAsColour", cfg.bold_colour);
    for (i=0; i<22; i++) {
	char buf[20], buf2[30];
	sprintf(buf, "Colour%d", i);
	sprintf(buf2, "%d,%d,%d", cfg.colours[i][0],
		cfg.colours[i][1], cfg.colours[i][2]);
	wpps (sesskey, buf, buf2);
    }
    wppi (sesskey, "MouseIsXterm", cfg.mouse_is_xterm);
    for (i=0; i<256; i+=32) {
	char buf[20], buf2[256];
	int j;
	sprintf(buf, "Wordness%d", i);
	*buf2 = '\0';
	for (j=i; j<i+32; j++) {
	    sprintf(buf2+strlen(buf2), "%s%d",
		    (*buf2 ? "," : ""), cfg.wordness[j]);
	}
	wpps (sesskey, buf, buf2);
    }
    wppi (sesskey, "KoiWinXlat", cfg.xlat_enablekoiwin);
    wppi (sesskey, "88592Xlat", cfg.xlat_88592w1250);
    wppi (sesskey, "CapsLockCyr", cfg.xlat_capslockcyr);
    wppi (sesskey, "ScrollBar", cfg.scrollbar);
    wppi (sesskey, "ScrollOnKey", cfg.scroll_on_key);
    wppi (sesskey, "LockSize", cfg.locksize);
    wppi (sesskey, "BCE", cfg.bce);
    wppi (sesskey, "BlinkText", cfg.blinktext);

    RegCloseKey(sesskey);
}

static void del_session (char *section) {
    HKEY subkey1;
    char *p;

    if (RegOpenKey(HKEY_CURRENT_USER, puttystr, &subkey1) != ERROR_SUCCESS)
	return;

    p = malloc(3*strlen(section)+1);
    mungestr(section, p);
    RegDeleteKey(subkey1, p);
    free(p);

    RegCloseKey(subkey1);
}

static void load_settings (char *section, int do_host) {
    int i;
    HKEY subkey1, sesskey;
    char *p;
    char prot[10];

    p = malloc(3*strlen(section)+1);
    mungestr(section, p);

    if (RegOpenKey(HKEY_CURRENT_USER, puttystr, &subkey1) != ERROR_SUCCESS) {
	sesskey = NULL;
    } else {
	if (RegOpenKey(subkey1, p, &sesskey) != ERROR_SUCCESS) {
	    sesskey = NULL;
	}
	RegCloseKey(subkey1);
    }

    free(p);

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

    RegCloseKey(sesskey);
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
	    abtbox = NULL;
	    DestroyWindow (hwnd);
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

static int GeneralPanelProc (HWND hwnd, UINT msg,
			     WPARAM wParam, LPARAM lParam) {
    switch (msg) {
      case WM_INITDIALOG:
	SetWindowPos (hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	return 1;
/*      case WM_CTLCOLORDLG: */
/*	return (int) GetStockObject (LTGRAY_BRUSH); */
/*      case WM_CTLCOLORSTATIC: */
/*      case WM_CTLCOLORBTN: */
/*	SetBkColor ((HDC)wParam, RGB(192,192,192)); */
/*	return (int) GetStockObject (LTGRAY_BRUSH); */
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

    switch (msg) {
      case WM_INITDIALOG:
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
		del_session(sessions[n]);
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
    switch (msg) {
      case WM_INITDIALOG:
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
    CHOOSEFONT cf;
    LOGFONT lf;
    char fontstatic[256];

    switch (msg) {
      case WM_INITDIALOG:
	CheckDlgButton (hwnd, IDC2_WRAPMODE, cfg.wrap_mode);
	CheckDlgButton (hwnd, IDC2_WINNAME, cfg.win_name_always);
	CheckDlgButton (hwnd, IDC2_DECOM, cfg.dec_om);
	CheckDlgButton (hwnd, IDC2_LFHASCR, cfg.lfhascr);
	SetDlgItemInt (hwnd, IDC2_ROWSEDIT, cfg.height, FALSE);
	SetDlgItemInt (hwnd, IDC2_COLSEDIT, cfg.width, FALSE);
	SetDlgItemInt (hwnd, IDC2_SAVEEDIT, cfg.savelines, FALSE);
	fmtfont (fontstatic);
	SetDlgItemText (hwnd, IDC2_FONTSTATIC, fontstatic);
	CheckDlgButton (hwnd, IDC1_BLINKCUR, cfg.blink_cur);
        CheckDlgButton (hwnd, IDC1_BEEP, cfg.beep);
        CheckDlgButton (hwnd, IDC2_SCROLLBAR, cfg.scrollbar);
        CheckDlgButton (hwnd, IDC2_LOCKSIZE, cfg.locksize);
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
	  case IDC2_WINNAME:
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED)
		cfg.win_name_always = IsDlgButtonChecked (hwnd, IDC2_WINNAME);
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
	   case IDC1_BLINKCUR:
	     if (HIWORD(wParam) == BN_CLICKED ||
		 HIWORD(wParam) == BN_DOUBLECLICKED)
		 cfg.blink_cur = IsDlgButtonChecked (hwnd, IDC1_BLINKCUR);
	     break;
	   case IDC1_BEEP:
	     if (HIWORD(wParam) == BN_CLICKED ||
		 HIWORD(wParam) == BN_DOUBLECLICKED)
		 cfg.beep = IsDlgButtonChecked (hwnd, IDC1_BEEP);
	     break;
	   case IDC2_SCROLLBAR:
	     if (HIWORD(wParam) == BN_CLICKED ||
		 HIWORD(wParam) == BN_DOUBLECLICKED)
		 cfg.scrollbar = IsDlgButtonChecked (hwnd, IDC2_SCROLLBAR);
	     break;
	   case IDC2_LOCKSIZE:
	     if (HIWORD(wParam) == BN_CLICKED ||
		 HIWORD(wParam) == BN_DOUBLECLICKED)
		 cfg.locksize = IsDlgButtonChecked (hwnd, IDC2_LOCKSIZE);
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

static int CALLBACK TelnetProc (HWND hwnd, UINT msg,
				    WPARAM wParam, LPARAM lParam) {
    int i;

    switch (msg) {
      case WM_INITDIALOG:
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
    OPENFILENAME of;
    char filename[sizeof(cfg.keyfile)];

    switch (msg) {
      case WM_INITDIALOG:
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
    int i;

    switch (msg) {
      case WM_INITDIALOG:
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
    switch (msg) {
      case WM_INITDIALOG:
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
    switch (msg) {
      case WM_INITDIALOG:
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
    ConnectionProc, KeyboardProc, TerminalProc,
    TelnetProc, SshProc, SelectionProc, ColourProc, TranslationProc
};
static char *panelids[NPANELS] = {
    MAKEINTRESOURCE(IDD_PANEL0),
    MAKEINTRESOURCE(IDD_PANEL1),
    MAKEINTRESOURCE(IDD_PANEL2),
    MAKEINTRESOURCE(IDD_PANEL3),
    MAKEINTRESOURCE(IDD_PANEL35),
    MAKEINTRESOURCE(IDD_PANEL4),
    MAKEINTRESOURCE(IDD_PANEL5),
    MAKEINTRESOURCE(IDD_PANEL6)
};

static char *names[NPANELS] = {
    "Connection", "Keyboard", "Terminal", "Telnet",
    "SSH", "Selection", "Colours", "Translation"
};

static int mainp[MAIN_NPANELS] = { 0, 1, 2, 3, 4, 5, 6, 7};
static int reconfp[RECONF_NPANELS] = { 1, 2, 5, 6, 7};

static int GenericMainDlgProc (HWND hwnd, UINT msg,
			       WPARAM wParam, LPARAM lParam,
			       int npanels, int *panelnums, HWND *page) {
    HWND hw;

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
	*page = NULL;
	{			       /* initialise the tab control */
	    TC_ITEMHEADER tab;
	    int i;

	    hw = GetDlgItem (hwnd, IDC_TAB);
	    for (i=0; i<npanels; i++) {
		tab.mask = TCIF_TEXT;
		tab.pszText = names[panelnums[i]];
		TabCtrl_InsertItem (hw, i, &tab);
	    }
/*	    *page = CreateDialogIndirect (hinst, panels[panelnums[0]].temp,
					  hwnd, panelproc[panelnums[0]]);*/
	    *page = CreateDialog (hinst, panelids[panelnums[0]],
				  hwnd, panelproc[panelnums[0]]);
	    SetWindowLong (*page, GWL_EXSTYLE,
			   GetWindowLong (*page, GWL_EXSTYLE) |
			   WS_EX_CONTROLPARENT);
	}
	SetFocus (*page);
	return 0;
      case WM_NOTIFY:
	if (LOWORD(wParam) == IDC_TAB &&
	    ((LPNMHDR)lParam)->code == TCN_SELCHANGE) {
	    int i = TabCtrl_GetCurSel(((LPNMHDR)lParam)->hwndFrom);
	    if (*page)
		DestroyWindow (*page);
/*	    *page = CreateDialogIndirect (hinst, panels[panelnums[i]].temp,	
					  hwnd, panelproc[panelnums[i]]);*/
	    *page = CreateDialog (hinst, panelids[panelnums[i]],
				  hwnd, panelproc[panelnums[i]]);
	    SetWindowLong (*page, GWL_EXSTYLE,
			   GetWindowLong (*page, GWL_EXSTYLE) |
			   WS_EX_CONTROLPARENT);
	    SetFocus (((LPNMHDR)lParam)->hwndFrom);   /* ensure focus stays */
	    return 0;
	}
	break;
/*      case WM_CTLCOLORDLG: */
/*	return (int) GetStockObject (LTGRAY_BRUSH); */
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
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
    static char *buffer;
    int buflen, bufsize, i, ret;
    char otherbuf[2048];
    char *p;
    HKEY subkey1;

    if (allocate) {
	if (RegCreateKey(HKEY_CURRENT_USER,
			 puttystr, &subkey1) != ERROR_SUCCESS)
	    return;

	buflen = bufsize = 0;
	buffer = NULL;
	i = 0;
	do {
	    ret = RegEnumKey(subkey1, i++, otherbuf, sizeof(otherbuf));
	    if (ret == ERROR_SUCCESS) {
		bufsize = buflen + 2048;
		buffer = srealloc(buffer, bufsize);
		unmungestr(otherbuf, buffer+buflen);
		buflen += strlen(buffer+buflen)+1;
	    }
	} while (ret == ERROR_SUCCESS);
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
	SendDlgItemMessage (logbox, IDN_LIST, LB_SETCURSEL, count-1, 0);
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

void verify_ssh_host_key(char *host, char *keystr) {
    char *otherstr, *mungedhost;
    int len;
    HKEY rkey;

    len = 1 + strlen(keystr);

    /*
     * Now read a saved key in from the registry and see what it
     * says.
     */
    otherstr = smalloc(len);
    mungedhost = smalloc(3*strlen(host)+1);
    if (!otherstr || !mungedhost)
	fatalbox("Out of memory");

    mungestr(host, mungedhost);

    if (RegCreateKey(HKEY_CURRENT_USER, PUTTY_REG_POS "\\SshHostKeys",
		     &rkey) != ERROR_SUCCESS) {
	if (MessageBox(NULL, "PuTTY was unable to open the host key cache\n"
		       "in the registry. There is thus no way to tell\n"
		       "if the remote host is what you think it is.\n"
		       "Connect anyway?", "PuTTY Problem",
		       MB_ICONWARNING | MB_YESNO) == IDNO)
	    exit(0);
    } else {
	DWORD readlen = len;
	DWORD type;
	int ret;

	ret = RegQueryValueEx(rkey, mungedhost, NULL,
			      &type, otherstr, &readlen);

	if (ret == ERROR_MORE_DATA ||
	    (ret == ERROR_SUCCESS && type == REG_SZ &&
	     strcmp(otherstr, keystr))) {
	    if (MessageBox(NULL,
			   "This host's host key is different from the\n"
			   "one cached in the registry! Someone may be\n"
			   "impersonating this host for malicious reasons;\n"
			   "alternatively, the host key may have changed\n"
			   "due to sloppy system administration.\n"
			   "Replace key in registry and connect?",
			   "PuTTY: Security Warning",
			   MB_ICONWARNING | MB_YESNO) == IDNO)
		exit(0);
	    RegSetValueEx(rkey, mungedhost, 0, REG_SZ, keystr,
			  strlen(keystr)+1);
	} else if (ret != ERROR_SUCCESS || type != REG_SZ) {
	    if (MessageBox(NULL,
			   "This host's host key is not cached in the\n"
			   "registry. Do you want to add it to the cache\n"
			   "and carry on connecting?",
			   "PuTTY: New Host",
			   MB_ICONWARNING | MB_YESNO) == IDNO)
		exit(0);
	    RegSetValueEx(rkey, mungedhost, 0, REG_SZ, keystr,
			  strlen(keystr)+1);
	}

	RegCloseKey(rkey);
    }
}
