/*
 * Pageant: the PuTTY Authentication Agent.
 */

#include <windows.h>
#include <stdio.h> /* FIXME */
#include "putty.h" /* FIXME */
#include "ssh.h"
#include "tree234.h"

#define IDI_MAINICON 200
#define IDI_TRAYICON 201

#define WM_XUSER     (WM_USER + 0x2000)
#define WM_SYSTRAY   (WM_XUSER + 6)
#define WM_SYSTRAY2  (WM_XUSER + 7)
#define WM_CLOSEMEM  (WM_XUSER + 10)

#define IDM_CLOSE    0x0010
#define IDM_VIEWKEYS 0x0020

#define APPNAME "Pageant"

#define SSH_AGENTC_REQUEST_RSA_IDENTITIES    1
#define SSH_AGENT_RSA_IDENTITIES_ANSWER      2
#define SSH_AGENTC_RSA_CHALLENGE             3
#define SSH_AGENT_RSA_RESPONSE               4
#define SSH_AGENT_FAILURE                    5
#define SSH_AGENT_SUCCESS                    6
#define SSH_AGENTC_ADD_RSA_IDENTITY          7
#define SSH_AGENTC_REMOVE_RSA_IDENTITY       8

HINSTANCE instance;
HWND hwnd;
HWND keylist;
HMENU systray_menu;

tree234 *rsakeys;

/*
 * We need this to link with the RSA code, because rsaencrypt()
 * pads its data with random bytes. Since we only use rsadecrypt(),
 * which is deterministic, this should never be called.
 *
 * If it _is_ called, there is a _serious_ problem, because it
 * won't generate true random numbers. So we must scream, panic,
 * and exit immediately if that should happen.
 */
int random_byte(void) {
    MessageBox(hwnd, "Internal Error", APPNAME, MB_OK | MB_ICONERROR);
    exit(0);
}

/*
 * This function is needed to link with the DES code. We need not
 * have it do anything at all.
 */
void logevent(char *msg) {
}

#define GET_32BIT(cp) \
    (((unsigned long)(unsigned char)(cp)[0] << 24) | \
    ((unsigned long)(unsigned char)(cp)[1] << 16) | \
    ((unsigned long)(unsigned char)(cp)[2] << 8) | \
    ((unsigned long)(unsigned char)(cp)[3]))

#define PUT_32BIT(cp, value) { \
    (cp)[0] = (unsigned char)((value) >> 24); \
    (cp)[1] = (unsigned char)((value) >> 16); \
    (cp)[2] = (unsigned char)((value) >> 8); \
    (cp)[3] = (unsigned char)(value); }

#define PASSPHRASE_MAXLEN 512

/*
 * Dialog-box function for the passphrase box.
 */
static int CALLBACK PassphraseProc(HWND hwnd, UINT msg,
                                   WPARAM wParam, LPARAM lParam) {
    static char *passphrase;

    switch (msg) {
      case WM_INITDIALOG:
        passphrase = (char *)lParam;
        *passphrase = 0;
        return 0;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDOK:
	    if (*passphrase)
		EndDialog (hwnd, 1);
	    else
		MessageBeep (0);
	    return 0;
	  case IDCANCEL:
	    EndDialog (hwnd, 0);
	    return 0;
          case 102:                    /* edit box */
	    if (HIWORD(wParam) == EN_CHANGE) {
                GetDlgItemText (hwnd, 102, passphrase, PASSPHRASE_MAXLEN-1);
                passphrase[PASSPHRASE_MAXLEN-1] = '\0';
            }
            return 0;
	}
	return 0;
      case WM_CLOSE:
	EndDialog (hwnd, 0);
	return 0;
    }
    return 0;
}

/*
 * This function loads a key from a file and adds it.
 */
void add_keyfile(char *filename) {
    char passphrase[PASSPHRASE_MAXLEN];
    struct RSAKey *key;
    int needs_pass;
    int ret;
    int attempts;

    needs_pass = rsakey_encrypted(filename);
    attempts = 0;
    key = malloc(sizeof(*key));
    do {
        if (needs_pass) {
            int dlgret;
            dlgret = DialogBoxParam(instance, MAKEINTRESOURCE(210),
                                    NULL, PassphraseProc,
                                    (LPARAM)passphrase);
            if (!dlgret) {
                free(key);
                return;                /* operation cancelled */
            }
        } else
            *passphrase = '\0';
        ret = loadrsakey(filename, key, passphrase);
        attempts++;
    } while (ret == -1);
    if (ret == 0) {
        MessageBox(NULL, "Couldn't load public key.", APPNAME,
                   MB_OK | MB_ICONERROR);
        free(key);
        return;
    }
    if (add234(rsakeys, key) != key)
        free(key);                     /* already present, don't waste RAM */
}

/*
 * This is the main agent function that answers messages.
 */
void answer_msg(void *in, int inlen, void **out, int *outlen) {
    unsigned char *ret;
    unsigned char *p = in;
    int type;

    *out = NULL;                       /* default `no go' response */

    /*
     * Basic sanity checks. len >= 5, and len[0:4] holds len-4.
     */
    if (inlen < 5 || GET_32BIT(p) != (unsigned long)(inlen-4))
        return;

    /*
     * Get the message type.
     */
    type = p[4];

    p += 5;

    switch (type) {
      case SSH_AGENTC_REQUEST_RSA_IDENTITIES:
        /*
         * Reply with SSH_AGENT_RSA_IDENTITIES_ANSWER.
         */
        {
            enum234 e;
            struct RSAKey *key;
            int len, nkeys;

            /*
             * Count up the number and length of keys we hold.
             */
            len = nkeys = 0;
            for (key = first234(rsakeys, &e); key; key = next234(&e)) {
                nkeys++;
                len += 4;              /* length field */
                len += ssh1_bignum_length(key->exponent);
                len += ssh1_bignum_length(key->modulus);
                len += 4 + strlen(key->comment);
            }

            /*
             * Packet header is the obvious five bytes, plus four
             * bytes for the key count.
             */
            len += 5 + 4;
            if ((ret = malloc(len)) != NULL) {
                PUT_32BIT(ret, len-4);
                ret[4] = SSH_AGENT_RSA_IDENTITIES_ANSWER;
                PUT_32BIT(ret+5, nkeys);
                p = ret + 5 + 4;
                for (key = first234(rsakeys, &e); key; key = next234(&e)) {
                    PUT_32BIT(p, ssh1_bignum_bitcount(key->modulus));
                    p += 4;
                    p += ssh1_write_bignum(p, key->exponent);
                    p += ssh1_write_bignum(p, key->modulus);
                    PUT_32BIT(p, strlen(key->comment));
                    memcpy(p+4, key->comment, strlen(key->comment));
                    p += 4 + strlen(key->comment);
                }
            }
        }
        break;
      case SSH_AGENTC_RSA_CHALLENGE:
        /*
         * Reply with either SSH_AGENT_RSA_RESPONSE or
         * SSH_AGENT_FAILURE, depending on whether we have that key
         * or not.
         */
        {
            struct RSAKey reqkey, *key;
            Bignum challenge, response;
            unsigned char response_source[48], response_md5[16];
            struct MD5Context md5c;
            int i, len;

            p += 4;
            p += ssh1_read_bignum(p, &reqkey.exponent);
            p += ssh1_read_bignum(p, &reqkey.modulus);
            p += ssh1_read_bignum(p, &challenge);
            memcpy(response_source+32, p, 16); p += 16;
            if (GET_32BIT(p) != 1 ||
                (key = find234(rsakeys, &reqkey, NULL)) == NULL) {
                freebn(reqkey.exponent);
                freebn(reqkey.modulus);
                freebn(challenge);
                goto failure;
            }
            response = rsadecrypt(challenge, key);
            for (i = 0; i < 32; i++)
                response_source[i] = bignum_byte(response, 31-i);

            MD5Init(&md5c);
            MD5Update(&md5c, response_source, 48);
            MD5Final(response_md5, &md5c);
            memset(response_source, 0, 48);   /* burn the evidence */
            freebn(response);          /* and that evidence */
            freebn(challenge);         /* yes, and that evidence */
            freebn(reqkey.exponent);   /* and free some memory ... */
            freebn(reqkey.modulus);    /* ... while we're at it. */

            /*
             * Packet is the obvious five byte header, plus sixteen
             * bytes of MD5.
             */
            len = 5 + 16;
            if ((ret = malloc(len)) != NULL) {
                PUT_32BIT(ret, len-4);
                ret[4] = SSH_AGENT_RSA_RESPONSE;
                memcpy(ret+5, response_md5, 16);
            }
        }
        break;
#if 0 /* FIXME: implement these */
      case SSH_AGENTC_ADD_RSA_IDENTITY:
        /*
         * Add to the list and return SSH_AGENT_SUCCESS, or
         * SSH_AGENT_FAILURE if the key was malformed.
         */
        break;
      case SSH_AGENTC_REMOVE_RSA_IDENTITY:
        /*
         * Remove from the list and return SSH_AGENT_SUCCESS, or
         * perhaps SSH_AGENT_FAILURE if it wasn't in the list to
         * start with.
         */
        break;
#endif
      default:
        failure:
        /*
         * Unrecognised message. Return SSH_AGENT_FAILURE.
         */
        if ((ret = malloc(5)) != NULL) {
            PUT_32BIT(ret, 1);
            ret[4] = SSH_AGENT_FAILURE;
        }
        break;
    }

    if (ret) {
        *out = ret;
        *outlen = 4 + GET_32BIT(ret);
    }
}

/*
 * Key comparison function for the 2-3-4 tree of RSA keys.
 */
int cmpkeys(void *av, void *bv) {
    struct RSAKey *a = (struct RSAKey *)av;
    struct RSAKey *b = (struct RSAKey *)bv;
    Bignum am, bm;
    int alen, blen;

    am = a->modulus;
    bm = b->modulus;
    /*
     * Compare by length of moduli.
     */
    alen = ssh1_bignum_bitcount(am);
    blen = ssh1_bignum_bitcount(bm);
    if (alen > blen) return +1; else if (alen < blen) return -1;
    /*
     * Now compare by moduli themselves.
     */
    alen = (alen + 7) / 8;             /* byte count */
    while (alen-- > 0) {
        int abyte, bbyte;
        abyte = bignum_byte(am, alen);
        bbyte = bignum_byte(bm, alen);
        if (abyte > bbyte) return +1; else if (abyte < bbyte) return -1;
    }
    /*
     * Give up.
     */
    return 0;
}

static void error(char *s) {
    MessageBox(hwnd, s, APPNAME, MB_OK | MB_ICONERROR);
}

/*
 * Dialog-box function for the key list box.
 */
static int CALLBACK KeyListProc(HWND hwnd, UINT msg,
                                WPARAM wParam, LPARAM lParam) {
    enum234 e;
    struct RSAKey *key;
    OPENFILENAME of;
    char filename[FILENAME_MAX];

    switch (msg) {
      case WM_INITDIALOG:
        for (key = first234(rsakeys, &e); key; key = next234(&e)) {
            SendDlgItemMessage (hwnd, 100, LB_ADDSTRING,
                                0, (LPARAM) key->comment);
        }
        return 0;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDOK:
	  case IDCANCEL:
            keylist = NULL;
            DestroyWindow(hwnd);
            return 0;
          case 101:                    /* add key */
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
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
                of.lpstrFile = filename; *filename = '\0';
                of.nMaxFile = sizeof(filename);
                of.lpstrFileTitle = NULL;
                of.lpstrInitialDir = NULL;
                of.lpstrTitle = "Select Public Key File";
                of.Flags = 0;
                if (GetOpenFileName(&of)) {
                    add_keyfile(filename);
                }
                SendDlgItemMessage(hwnd, 100, LB_RESETCONTENT, 0, 0);
                for (key = first234(rsakeys, &e); key; key = next234(&e)) {
                    SendDlgItemMessage (hwnd, 100, LB_ADDSTRING,
                                        0, (LPARAM) key->comment);
                }
		SendDlgItemMessage (hwnd, 100, LB_SETCURSEL, (WPARAM) -1, 0);
            }
            return 0;
          case 102:                    /* remove key */
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		int n = SendDlgItemMessage (hwnd, 100, LB_GETCURSEL, 0, 0);
		if (n == LB_ERR || n == 0) {
		    MessageBeep(0);
		    break;
		}
                for (key = first234(rsakeys, &e); key; key = next234(&e))
                    if (n-- == 0)
                        break;
                del234(rsakeys, key);
                freersakey(key); free(key);
                SendDlgItemMessage(hwnd, 100, LB_RESETCONTENT, 0, 0);
                for (key = first234(rsakeys, &e); key; key = next234(&e)) {
                    SendDlgItemMessage (hwnd, 100, LB_ADDSTRING,
                                        0, (LPARAM) key->comment);
                }
		SendDlgItemMessage (hwnd, 100, LB_SETCURSEL, (WPARAM) -1, 0);
            }
            return 0;
	}
	return 0;
      case WM_CLOSE:
        keylist = NULL;
        DestroyWindow(hwnd);
	return 0;
    }
    return 0;
}

static LRESULT CALLBACK WndProc (HWND hwnd, UINT message,
                                 WPARAM wParam, LPARAM lParam) {
    int ret;
    static int menuinprogress;

    switch (message) {
      case WM_SYSTRAY:
        if (lParam == WM_RBUTTONUP) {
            POINT cursorpos;
            GetCursorPos(&cursorpos);
            PostMessage(hwnd, WM_SYSTRAY2, cursorpos.x, cursorpos.y);
        }
        break;
      case WM_SYSTRAY2:
        if (!menuinprogress) {
            menuinprogress = 1;
            SetForegroundWindow(hwnd);
            ret = TrackPopupMenu(systray_menu,
                                 TPM_RIGHTALIGN | TPM_BOTTOMALIGN |
                                 TPM_RIGHTBUTTON,
                                 wParam, lParam, 0, hwnd, NULL);
            menuinprogress = 0;
        }
        break;
      case WM_COMMAND:
      case WM_SYSCOMMAND:
	switch (wParam & ~0xF) {       /* low 4 bits reserved to Windows */
          case IDM_CLOSE:
            SendMessage(hwnd, WM_CLOSE, 0, 0);
            break;
          case IDM_VIEWKEYS:
            if (!keylist) {
                keylist = CreateDialog (instance, MAKEINTRESOURCE(211),
                                        NULL, KeyListProc);
                ShowWindow (keylist, SW_SHOWNORMAL);
            }
            break;
        }
        break;
      case WM_DESTROY:
	PostQuitMessage (0);
	return 0;
      case WM_COPYDATA:
        {
            COPYDATASTRUCT *cds;
            void *in, *out, *ret;
            int inlen, outlen;
            HANDLE filemap;
            char mapname[64];
            int id;

            cds = (COPYDATASTRUCT *)lParam;
            /*
             * FIXME: use dwData somehow.
             */
            in = cds->lpData;
            inlen = cds->cbData;
            answer_msg(in, inlen, &out, &outlen);
            if (out) {
                id = 0;
                do {
                    sprintf(mapname, "PageantReply%08x", ++id);
		    filemap = CreateFileMapping(INVALID_HANDLE_VALUE,
                                                NULL, PAGE_READWRITE,
						0, outlen+sizeof(int),
                                                mapname);
                } while (filemap == INVALID_HANDLE_VALUE);
                ret = MapViewOfFile(filemap, FILE_MAP_WRITE, 0, 0,
                                    outlen+sizeof(int));
                if (ret) {
                    *((int *)ret) = outlen;
                    memcpy(((int *)ret)+1, out, outlen);
                    UnmapViewOfFile(ret);
                    return id;
                }
            } else
                return 0;              /* invalid request */
        }
        break;
      case WM_CLOSEMEM:
        /*
         * FIXME!
         */
        break;
    }

    return DefWindowProc (hwnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show) {
    WNDCLASS wndclass;
    MSG msg;

    instance = inst;

    if (!prev) {
	wndclass.style         = 0;
	wndclass.lpfnWndProc   = WndProc;
	wndclass.cbClsExtra    = 0;
	wndclass.cbWndExtra    = 0;
	wndclass.hInstance     = inst;
	wndclass.hIcon         = LoadIcon (inst,
					   MAKEINTRESOURCE(IDI_MAINICON));
	wndclass.hCursor       = LoadCursor (NULL, IDC_IBEAM);
	wndclass.hbrBackground = GetStockObject (BLACK_BRUSH);
	wndclass.lpszMenuName  = NULL;
	wndclass.lpszClassName = APPNAME;

	RegisterClass (&wndclass);
    }

    hwnd = keylist = NULL;

    hwnd = CreateWindow (APPNAME, APPNAME,
                         WS_OVERLAPPEDWINDOW | WS_VSCROLL,
                         CW_USEDEFAULT, CW_USEDEFAULT,
                         100, 100, NULL, NULL, inst, NULL);

    /* Set up a system tray icon */
    {
        BOOL res;
        NOTIFYICONDATA tnid;
        HICON hicon;

#ifdef NIM_SETVERSION
        tnid.uVersion = 0;
        res = Shell_NotifyIcon(NIM_SETVERSION, &tnid);
#endif

        tnid.cbSize = sizeof(NOTIFYICONDATA); 
        tnid.hWnd = hwnd; 
        tnid.uID = 1;                  /* unique within this systray use */
        tnid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; 
        tnid.uCallbackMessage = WM_SYSTRAY;
        tnid.hIcon = hicon = LoadIcon (instance, MAKEINTRESOURCE(201));
        strcpy(tnid.szTip, "Pageant (PuTTY authentication agent)");

        res = Shell_NotifyIcon(NIM_ADD, &tnid); 

        if (hicon) 
            DestroyIcon(hicon); 

        systray_menu = CreatePopupMenu();
        AppendMenu (systray_menu, MF_ENABLED, IDM_VIEWKEYS, "View Keys");
        AppendMenu (systray_menu, MF_ENABLED, IDM_CLOSE, "Terminate");
    }

    ShowWindow (hwnd, SW_HIDE);

    /*
     * Initialise storage for RSA keys.
     */
    rsakeys = newtree234(cmpkeys);

    /*
     * Process the command line and add RSA keys as listed on it.
     * FIXME: we don't support spaces in filenames here. We should.
     */
    {
        char *p = cmdline;
        while (*p) {
            while (*p && isspace(*p)) p++;
            if (*p && !isspace(*p)) {
                char *q = p;
                while (*p && !isspace(*p)) p++;
                if (*p) *p++ = '\0';
                add_keyfile(q);
            }
        }
    }

    /*
     * Main message loop.
     */
    while (GetMessage(&msg, NULL, 0, 0) == 1) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    /* Clean up the system tray icon */
    {
        NOTIFYICONDATA tnid;

        tnid.cbSize = sizeof(NOTIFYICONDATA); 
        tnid.hWnd = hwnd;
        tnid.uID = 1;

        Shell_NotifyIcon(NIM_DELETE, &tnid); 

        DestroyMenu(systray_menu);
    }

    exit(msg.wParam);
}
