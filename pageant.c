/*
 * Pageant: the PuTTY Authentication Agent.
 */

#include <windows.h>
#ifndef NO_SECURITY
#include <aclapi.h>
#endif
#include <stdio.h>
#include "ssh.h"
#include "tree234.h"

#define IDI_MAINICON 200
#define IDI_TRAYICON 201

#define WM_XUSER     (WM_USER + 0x2000)
#define WM_SYSTRAY   (WM_XUSER + 6)
#define WM_SYSTRAY2  (WM_XUSER + 7)

#define AGENT_COPYDATA_ID 0x804e50ba   /* random goop */

/*
 * FIXME: maybe some day we can sort this out ...
 */
#define AGENT_MAX_MSGLEN  8192

#define IDM_CLOSE    0x0010
#define IDM_VIEWKEYS 0x0020
#define IDM_ADDKEY   0x0030
#define IDM_ABOUT    0x0040

#define APPNAME "Pageant"

#define SSH_AGENTC_REQUEST_RSA_IDENTITIES    1
#define SSH_AGENT_RSA_IDENTITIES_ANSWER      2
#define SSH_AGENTC_RSA_CHALLENGE             3
#define SSH_AGENT_RSA_RESPONSE               4
#define SSH_AGENT_FAILURE                    5
#define SSH_AGENT_SUCCESS                    6
#define SSH_AGENTC_ADD_RSA_IDENTITY          7
#define SSH_AGENTC_REMOVE_RSA_IDENTITY       8

extern char ver[];

static HINSTANCE instance;
static HWND hwnd;
static HWND keylist;
static HWND aboutbox;
static HMENU systray_menu;

static tree234 *rsakeys;

static int has_security;
#ifndef NO_SECURITY
typedef DWORD (WINAPI *gsi_fn_t)
    (HANDLE, SE_OBJECT_TYPE, SECURITY_INFORMATION,
                                 PSID *, PSID *, PACL *, PACL *,
                                 PSECURITY_DESCRIPTOR *);
static gsi_fn_t getsecurityinfo;
#endif

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

struct PassphraseProcStruct {
    char *passphrase;
    char *comment;
};

/*
 * Dialog-box function for the Licence box.
 */
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

/*
 * Dialog-box function for the About box.
 */
static int CALLBACK AboutProc (HWND hwnd, UINT msg,
			       WPARAM wParam, LPARAM lParam) {
    switch (msg) {
      case WM_INITDIALOG:
        SetDlgItemText (hwnd, 100, ver);
	return 1;
      case WM_COMMAND:
	switch (LOWORD(wParam)) {
	  case IDOK:
	    aboutbox = NULL;
	    DestroyWindow (hwnd);
	    return 0;
	  case 101:
	    EnableWindow(hwnd, 0);
	    DialogBox (instance, MAKEINTRESOURCE(214), NULL, LicenceProc);
	    EnableWindow(hwnd, 1);
            SetActiveWindow(hwnd);
	    return 0;
	}
	return 0;
      case WM_CLOSE:
	aboutbox = NULL;
	DestroyWindow (hwnd);
	return 0;
    }
    return 0;
}

/*
 * Dialog-box function for the passphrase box.
 */
static int CALLBACK PassphraseProc(HWND hwnd, UINT msg,
                                   WPARAM wParam, LPARAM lParam) {
    static char *passphrase;
    struct PassphraseProcStruct *p;

    switch (msg) {
      case WM_INITDIALOG:
        SetForegroundWindow(hwnd);
        SetWindowPos (hwnd, HWND_TOP, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        p = (struct PassphraseProcStruct *)lParam;
        passphrase = p->passphrase;
        if (p->comment)
            SetDlgItemText(hwnd, 101, p->comment);
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
 * Update the visible key list.
 */
static void keylist_update(void) {
    struct RSAKey *key;
    enum234 e;

    if (keylist) {
        SendDlgItemMessage(keylist, 100, LB_RESETCONTENT, 0, 0);
        for (key = first234(rsakeys, &e); key; key = next234(&e)) {
            char listentry[512], *p;
            /*
             * Replace two spaces in the fingerprint with tabs, for
             * nice alignment in the box.
             */
            rsa_fingerprint(listentry, sizeof(listentry), key);
            p = strchr(listentry, ' '); if (p) *p = '\t';
            p = strchr(listentry, ' '); if (p) *p = '\t';
            SendDlgItemMessage (keylist, 100, LB_ADDSTRING,
                                0, (LPARAM)listentry);
        }
        SendDlgItemMessage (keylist, 100, LB_SETCURSEL, (WPARAM) -1, 0);
    }
}

/*
 * This function loads a key from a file and adds it.
 */
static void add_keyfile(char *filename) {
    char passphrase[PASSPHRASE_MAXLEN];
    struct RSAKey *key;
    int needs_pass;
    int ret;
    int attempts;
    char *comment;
    struct PassphraseProcStruct pps;

    needs_pass = rsakey_encrypted(filename, &comment);
    attempts = 0;
    key = malloc(sizeof(*key));
    pps.passphrase = passphrase;
    pps.comment = comment;
    do {
        if (needs_pass) {
            int dlgret;
            dlgret = DialogBoxParam(instance, MAKEINTRESOURCE(210),
                                    NULL, PassphraseProc,
                                    (LPARAM)&pps);
            if (!dlgret) {
                if (comment) free(comment);
                free(key);
                return;                /* operation cancelled */
            }
        } else
            *passphrase = '\0';
        ret = loadrsakey(filename, key, NULL, passphrase);
        attempts++;
    } while (ret == -1);
    if (comment) free(comment);
    if (ret == 0) {
        MessageBox(NULL, "Couldn't load private key.", APPNAME,
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
static void answer_msg(void *msg) {
    unsigned char *p = msg;
    unsigned char *ret = msg;
    int type;

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
            if (len > AGENT_MAX_MSGLEN)
                goto failure;          /* aaargh! too much stuff! */
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
            PUT_32BIT(ret, len-4);
            ret[4] = SSH_AGENT_RSA_RESPONSE;
            memcpy(ret+5, response_md5, 16);
        }
        break;
      case SSH_AGENTC_ADD_RSA_IDENTITY:
        /*
         * Add to the list and return SSH_AGENT_SUCCESS, or
         * SSH_AGENT_FAILURE if the key was malformed.
         */
        {
            struct RSAKey *key;
            char *comment;
            key = malloc(sizeof(struct RSAKey));
            memset(key, 0, sizeof(key));
            p += makekey(p, key, NULL, 1);
            p += makeprivate(p, key);
            p += ssh1_read_bignum(p, NULL);    /* p^-1 mod q */
            p += ssh1_read_bignum(p, NULL);    /* p */
            p += ssh1_read_bignum(p, NULL);    /* q */
            comment = malloc(GET_32BIT(p));
            if (comment) {
                memcpy(comment, p+4, GET_32BIT(p));
                key->comment = comment;
            }
            PUT_32BIT(ret, 1);
            ret[4] = SSH_AGENT_FAILURE;
            if (add234(rsakeys, key) == key) {
                keylist_update();
                ret[4] = SSH_AGENT_SUCCESS;
            } else {
                freersakey(key);
                free(key);
            }
        }
        break;
      case SSH_AGENTC_REMOVE_RSA_IDENTITY:
        /*
         * Remove from the list and return SSH_AGENT_SUCCESS, or
         * perhaps SSH_AGENT_FAILURE if it wasn't in the list to
         * start with.
         */
        {
            struct RSAKey reqkey, *key;

            p += makekey(p, &reqkey, NULL, 0);
            key = find234(rsakeys, &reqkey, NULL);
            freebn(reqkey.exponent);
            freebn(reqkey.modulus);
            PUT_32BIT(ret, 1);
            ret[4] = SSH_AGENT_FAILURE;
            if (key) {
                del234(rsakeys, key);
                keylist_update();
                freersakey(key);
                ret[4] = SSH_AGENT_SUCCESS;
            }
        }
        break;
      default:
        failure:
        /*
         * Unrecognised message. Return SSH_AGENT_FAILURE.
         */
        PUT_32BIT(ret, 1);
        ret[4] = SSH_AGENT_FAILURE;
        break;
    }
}

/*
 * Key comparison function for the 2-3-4 tree of RSA keys.
 */
static int cmpkeys(void *av, void *bv) {
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
 * Prompt for a key file to add, and add it.
 */
static void prompt_add_keyfile(void) {
    OPENFILENAME of;
    char filename[FILENAME_MAX];
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
    of.lpstrTitle = "Select Private Key File";
    of.Flags = 0;
    if (GetOpenFileName(&of)) {
        add_keyfile(filename);
        keylist_update();
    }
}

/*
 * Dialog-box function for the key list box.
 */
static int CALLBACK KeyListProc(HWND hwnd, UINT msg,
                                WPARAM wParam, LPARAM lParam) {
    enum234 e;
    struct RSAKey *key;

    switch (msg) {
      case WM_INITDIALOG:
        keylist = hwnd;
	{
	    static int tabs[2] = {25, 175};
	    SendDlgItemMessage (hwnd, 100, LB_SETTABSTOPS, 2,
				(LPARAM) tabs);
	}
        keylist_update();
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
                prompt_add_keyfile();
            }
            return 0;
          case 102:                    /* remove key */
	    if (HIWORD(wParam) == BN_CLICKED ||
		HIWORD(wParam) == BN_DOUBLECLICKED) {
		int n = SendDlgItemMessage (hwnd, 100, LB_GETCURSEL, 0, 0);
		if (n == LB_ERR) {
		    MessageBeep(0);
		    break;
		}
                for (key = first234(rsakeys, &e); key; key = next234(&e))
                    if (n-- == 0)
                        break;
                del234(rsakeys, key);
                freersakey(key); free(key);
                keylist_update();
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
        } else if (lParam == WM_LBUTTONDBLCLK) {
            /* Equivalent to IDM_VIEWKEYS. */
            PostMessage(hwnd, WM_COMMAND, IDM_VIEWKEYS, 0);
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
                /* 
                 * Sometimes the window comes up minimised / hidden
                 * for no obvious reason. Prevent this.
                 */
                SetForegroundWindow(keylist);
                SetWindowPos (keylist, HWND_TOP, 0, 0, 0, 0,
                              SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            }
            break;
          case IDM_ADDKEY:
            prompt_add_keyfile();
            break;
          case IDM_ABOUT:
            if (!aboutbox) {
                aboutbox = CreateDialog (instance, MAKEINTRESOURCE(213),
                                         NULL, AboutProc);
                ShowWindow (aboutbox, SW_SHOWNORMAL);
                /* 
                 * Sometimes the window comes up minimised / hidden
                 * for no obvious reason. Prevent this.
                 */
                SetForegroundWindow(aboutbox);
                SetWindowPos (aboutbox, HWND_TOP, 0, 0, 0, 0,
                              SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
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
            char *mapname;
            void *p;
            HANDLE filemap, proc;
            PSID mapowner, procowner;
            PSECURITY_DESCRIPTOR psd1 = NULL, psd2 = NULL;
            int ret = 0;

            cds = (COPYDATASTRUCT *)lParam;
            if (cds->dwData != AGENT_COPYDATA_ID)
                return 0;              /* not our message, mate */
            mapname = (char *)cds->lpData;
            if (mapname[cds->cbData - 1] != '\0')
                return 0;              /* failure to be ASCIZ! */
#ifdef DEBUG_IPC
            debug(("mapname is :%s:\r\n", mapname));
#endif
            filemap = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, mapname);
#ifdef DEBUG_IPC
            debug(("filemap is %p\r\n", filemap));
#endif
            if (filemap != NULL && filemap != INVALID_HANDLE_VALUE) {
                int rc;
#ifndef NO_SECURITY
                if (has_security) {
                    if ((proc = OpenProcess(MAXIMUM_ALLOWED, FALSE,
                                            GetCurrentProcessId())) == NULL) {
#ifdef DEBUG_IPC
                        debug(("couldn't get handle for process\r\n"));
#endif
                        return 0;
                    }
                    if (getsecurityinfo(proc, SE_KERNEL_OBJECT,
                                        OWNER_SECURITY_INFORMATION,
                                        &procowner, NULL, NULL, NULL,
                                        &psd2) != ERROR_SUCCESS) {
#ifdef DEBUG_IPC
                        debug(("couldn't get owner info for process\r\n"));
#endif
                        CloseHandle(proc);
                        return 0;          /* unable to get security info */
                    }
                    CloseHandle(proc);
                    if ((rc = getsecurityinfo(filemap, SE_KERNEL_OBJECT,
                                              OWNER_SECURITY_INFORMATION,
                                              &mapowner, NULL, NULL, NULL,
                                              &psd1) != ERROR_SUCCESS)) {
#ifdef DEBUG_IPC
                        debug(("couldn't get owner info for filemap: %d\r\n", rc));
#endif
                        return 0;
                    }
#ifdef DEBUG_IPC
                    debug(("got security stuff\r\n"));
#endif
                    if (!EqualSid(mapowner, procowner))
                        return 0;          /* security ID mismatch! */
#ifdef DEBUG_IPC
                    debug(("security stuff matched\r\n"));
#endif
                    LocalFree(psd1);
                    LocalFree(psd2);
                } else {
#ifdef DEBUG_IPC
                    debug(("security APIs not present\r\n"));
#endif
                }
#endif
                p = MapViewOfFile(filemap, FILE_MAP_WRITE, 0, 0, 0);
#ifdef DEBUG_IPC
                debug(("p is %p\r\n", p));
                {int i; for(i=0;i<5;i++)debug(("p[%d]=%02x\r\n", i, ((unsigned char *)p)[i]));}
#endif
                answer_msg(p);
                ret = 1;
                UnmapViewOfFile(p);
            }
            CloseHandle(filemap);
            return ret;
        }
    }

    return DefWindowProc (hwnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show) {
    WNDCLASS wndclass;
    MSG msg;
    OSVERSIONINFO osi;
    HMODULE advapi;

    /*
     * Determine whether we're an NT system (should have security
     * APIs) or a non-NT system (don't do security).
     */
    memset(&osi, 0, sizeof(OSVERSIONINFO));
    osi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    if (GetVersionEx(&osi) && osi.dwPlatformId==VER_PLATFORM_WIN32_NT) {
        has_security = TRUE;
    } else
        has_security = FALSE;

    if (has_security) {
#ifndef NO_SECURITY
        /*
         * Attempt to ge the security API we need.
         */
        advapi = LoadLibrary("ADVAPI32.DLL");
        getsecurityinfo = (gsi_fn_t)GetProcAddress(advapi, "GetSecurityInfo");
        if (!getsecurityinfo) {
            MessageBox(NULL,
                       "Unable to access security APIs. Pageant will\n"
                       "not run, in case it causes a security breach.",
                       "Pageant Fatal Error", MB_ICONERROR | MB_OK);
            return 1;
        }
#else
	MessageBox(NULL,
		   "This program has been compiled for Win9X and will\n"
		   "not run on NT, in case it causes a security breach.",
		   "Pageant Fatal Error", MB_ICONERROR | MB_OK);
	return 1;
#endif
    } else
        advapi = NULL;

    /*
     * First bomb out totally if we are already running.
     */
    if (FindWindow("Pageant", "Pageant")) {
        MessageBox(NULL, "Pageant is already running", "Pageant Error",
                   MB_ICONERROR | MB_OK);
        if (advapi) FreeLibrary(advapi);
        return 0;
    }

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
        /* accelerators used: vkxa */
        AppendMenu (systray_menu, MF_ENABLED, IDM_VIEWKEYS, "&View Keys");
        AppendMenu (systray_menu, MF_ENABLED, IDM_ADDKEY, "Add &Key");
        AppendMenu (systray_menu, MF_ENABLED, IDM_ABOUT, "&About");
        AppendMenu (systray_menu, MF_ENABLED, IDM_CLOSE, "E&xit");
    }

    ShowWindow (hwnd, SW_HIDE);

    /*
     * Initialise storage for RSA keys.
     */
    rsakeys = newtree234(cmpkeys);

    /*
     * Process the command line and add RSA keys as listed on it.
     */
    {
        char *p;
        int inquotes = 0;
        p = cmdline;
        while (*p) {
            while (*p && isspace(*p)) p++;
            if (*p && !isspace(*p)) {
                char *q = p, *pp = p;
                while (*p && (inquotes || !isspace(*p)))
                {
                    if (*p == '"') {
                        inquotes = !inquotes;
                        p++;
                        continue;
                    }
                    *pp++ = *p++;
                }
                if (*pp) {
                    if (*p) p++;
                    *pp++ = '\0';
                }
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

    if (advapi) FreeLibrary(advapi);
    exit(msg.wParam);
}
