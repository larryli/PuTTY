#include <windows.h>
#include <imm.h>
#include <commctrl.h>
#ifndef AUTO_WINSOCK
#ifdef WINSOCK_TWO
#include <winsock2.h>
#else
#include <winsock.h>
#endif
#endif
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#define PUTTY_DO_GLOBALS		       /* actually _define_ globals */
#include "putty.h"
#include "winstuff.h"
#include "storage.h"
#include "win_res.h"

#define IDM_SHOWLOG   0x0010
#define IDM_NEWSESS   0x0020
#define IDM_DUPSESS   0x0030
#define IDM_RECONF    0x0040
#define IDM_CLRSB     0x0050
#define IDM_RESET     0x0060
#define IDM_TEL_AYT   0x0070
#define IDM_TEL_BRK   0x0080
#define IDM_TEL_SYNCH 0x0090
#define IDM_TEL_EC    0x00a0
#define IDM_TEL_EL    0x00b0
#define IDM_TEL_GA    0x00c0
#define IDM_TEL_NOP   0x00d0
#define IDM_TEL_ABORT 0x00e0
#define IDM_TEL_AO    0x00f0
#define IDM_TEL_IP    0x0100
#define IDM_TEL_SUSP  0x0110
#define IDM_TEL_EOR   0x0120
#define IDM_TEL_EOF   0x0130
#define IDM_ABOUT     0x0140
#define IDM_SAVEDSESS 0x0150
#define IDM_COPYALL   0x0160

#define IDM_SAVED_MIN 0x1000
#define IDM_SAVED_MAX 0x2000

#define WM_IGNORE_SIZE (WM_XUSER + 1)
#define WM_IGNORE_CLIP (WM_XUSER + 2)

/* Needed for Chinese support and apparently not always defined. */
#ifndef VK_PROCESSKEY
#define VK_PROCESSKEY 0xE5
#endif

static LRESULT CALLBACK WndProc (HWND, UINT, WPARAM, LPARAM);
static int TranslateKey(UINT message, WPARAM wParam, LPARAM lParam, unsigned char *output);
static void cfgtopalette(void);
static void init_palette(void);
static void init_fonts(int);

static int extra_width, extra_height;

static int pending_netevent = 0;
static WPARAM pend_netevent_wParam = 0;
static LPARAM pend_netevent_lParam = 0;
static void enact_pending_netevent(void);

static time_t last_movement = 0;

#define FONT_NORMAL 0
#define FONT_BOLD 1
#define FONT_UNDERLINE 2
#define FONT_BOLDUND 3
#define FONT_OEM 4
#define FONT_OEMBOLD 5
#define FONT_OEMBOLDUND 6
#define FONT_OEMUND 7
static HFONT fonts[8];
static int font_needs_hand_underlining;
static enum {
    BOLD_COLOURS, BOLD_SHADOW, BOLD_FONT
} bold_mode;
static enum {
    UND_LINE, UND_FONT
} und_mode;
static int descent;

#define NCOLOURS 24
static COLORREF colours[NCOLOURS];
static HPALETTE pal;
static LPLOGPALETTE logpal;
static RGBTRIPLE defpal[NCOLOURS];

static HWND hwnd;

static HBITMAP caretbm;

static int dbltime, lasttime, lastact;
static Mouse_Button lastbtn;

static char *window_name, *icon_name;

static int compose_state = 0;

/* Dummy routine, only required in plink. */
void ldisc_update(int echo, int edit) {}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show) {
    static char appname[] = "PuTTY";
    WORD winsock_ver;
    WSADATA wsadata;
    WNDCLASS wndclass;
    MSG msg;
    int guess_width, guess_height;

    hinst = inst;
    flags = FLAG_VERBOSE | FLAG_INTERACTIVE;

    winsock_ver = MAKEWORD(1, 1);
    if (WSAStartup(winsock_ver, &wsadata)) {
	MessageBox(NULL, "Unable to initialise WinSock", "WinSock Error",
		   MB_OK | MB_ICONEXCLAMATION);
	return 1;
    }
    if (LOBYTE(wsadata.wVersion) != 1 || HIBYTE(wsadata.wVersion) != 1) {
	MessageBox(NULL, "WinSock version is incompatible with 1.1",
		   "WinSock Error", MB_OK | MB_ICONEXCLAMATION);
	WSACleanup();
	return 1;
    }
    /* WISHLIST: maybe allow config tweaking even if winsock not present? */
    sk_init();

    InitCommonControls();

    /* Ensure a Maximize setting in Explorer doesn't maximise the
     * config box. */
    defuse_showwindow();

    /*
     * Process the command line.
     */
    {
	char *p;

	default_protocol = DEFAULT_PROTOCOL;
	default_port = DEFAULT_PORT;
	cfg.logtype = LGTYP_NONE;

	do_defaults(NULL, &cfg);

	p = cmdline;
	while (*p && isspace(*p)) p++;

	/*
	 * Process command line options first. Yes, this can be
	 * done better, and it will be as soon as I have the
	 * energy...
	 */
	while (*p == '-') {
	    char *q = p + strcspn(p, " \t");
	    p++;
	    if (q == p + 3 &&
		tolower(p[0]) == 's' &&
		tolower(p[1]) == 's' &&
		tolower(p[2]) == 'h') {
		default_protocol = cfg.protocol = PROT_SSH;
		default_port = cfg.port = 22;
	    } else if (q == p + 7 &&
		tolower(p[0]) == 'c' &&
		tolower(p[1]) == 'l' &&
		tolower(p[2]) == 'e' &&
		tolower(p[3]) == 'a' &&
		tolower(p[4]) == 'n' &&
		tolower(p[5]) == 'u' &&
		tolower(p[6]) == 'p') {
                /*
                 * `putty -cleanup'. Remove all registry entries
                 * associated with PuTTY, and also find and delete
                 * the random seed file.
                 */
                if (MessageBox(NULL,
                               "This procedure will remove ALL Registry\n"
                               "entries associated with PuTTY, and will\n"
                               "also remove the PuTTY random seed file.\n"
                               "\n"
                               "THIS PROCESS WILL DESTROY YOUR SAVED\n"
                               "SESSIONS. Are you really sure you want\n"
                               "to continue?",
                               "PuTTY Warning",
                               MB_YESNO | MB_ICONWARNING) == IDYES) {
                    cleanup_all();
                }
                exit(0);
	    }
	    p = q + strspn(q, " \t");
	}

	/*
	 * An initial @ means to activate a saved session.
	 */
	if (*p == '@') {
	    int i = strlen(p);
	    while (i > 1 && isspace(p[i-1]))
		i--;
	    p[i] = '\0';
	    do_defaults (p+1, &cfg);
	    if (!*cfg.host && !do_config()) {
		WSACleanup();
		return 0;
	    }
	} else if (*p == '&') {
	    /*
	     * An initial & means we've been given a command line
	     * containing the hex value of a HANDLE for a file
	     * mapping object, which we must then extract as a
	     * config.
	     */
	    HANDLE filemap;
	    Config *cp;
	    if (sscanf(p+1, "%p", &filemap) == 1 &&
		(cp = MapViewOfFile(filemap, FILE_MAP_READ,
				    0, 0, sizeof(Config))) != NULL) {
		cfg = *cp;
		UnmapViewOfFile(cp);
		CloseHandle(filemap);
	    } else if (!do_config()) {
		WSACleanup();
		return 0;
	    }
	} else if (*p) {
	    char *q = p;
            /*
             * If the hostname starts with "telnet:", set the
             * protocol to Telnet and process the string as a
             * Telnet URL.
             */
            if (!strncmp(q, "telnet:", 7)) {
                char c;

                q += 7;
		if (q[0] == '/' && q[1] == '/')
		    q += 2;
                cfg.protocol = PROT_TELNET;
                p = q;
                while (*p && *p != ':' && *p != '/') p++;
                c = *p;
                if (*p)
                    *p++ = '\0';
                if (c == ':')
                    cfg.port = atoi(p);
                else
                    cfg.port = -1;
                strncpy (cfg.host, q, sizeof(cfg.host)-1);
                cfg.host[sizeof(cfg.host)-1] = '\0';
            } else {
                while (*p && !isspace(*p)) p++;
                if (*p)
                    *p++ = '\0';
                strncpy (cfg.host, q, sizeof(cfg.host)-1);
                cfg.host[sizeof(cfg.host)-1] = '\0';
                while (*p && isspace(*p)) p++;
                if (*p)
                    cfg.port = atoi(p);
                else
                    cfg.port = -1;
            }
	} else {
	    if (!do_config()) {
		WSACleanup();
		return 0;
	    }
	}

	/* See if host is of the form user@host */
	if (cfg.host[0] != '\0') {
	    char *atsign = strchr(cfg.host, '@');
	    /* Make sure we're not overflowing the user field */
	    if (atsign) {
		if (atsign-cfg.host < sizeof cfg.username) {
		    strncpy (cfg.username, cfg.host, atsign-cfg.host);
		    cfg.username[atsign-cfg.host] = '\0';
		}
		memmove(cfg.host, atsign+1, 1+strlen(atsign+1));
	    }
	}
    }

    /*
     * Select protocol. This is farmed out into a table in a
     * separate file to enable an ssh-free variant.
     */
    {
        int i;
        back = NULL;
        for (i = 0; backends[i].backend != NULL; i++)
            if (backends[i].protocol == cfg.protocol) {
                back = backends[i].backend;
                break;
            }
        if (back == NULL) {
            MessageBox(NULL, "Unsupported protocol number found",
                       "PuTTY Internal Error", MB_OK | MB_ICONEXCLAMATION);
            WSACleanup();
            return 1;
        }
    }

    /* Check for invalid Port number (i.e. zero) */
    if (cfg.port == 0) {
        MessageBox(NULL, "Invalid Port Number",
		   "PuTTY Internal Error", MB_OK |MB_ICONEXCLAMATION);
        WSACleanup();
        return 1;
    }

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
	wndclass.lpszClassName = appname;

	RegisterClass (&wndclass);
    }

    hwnd = NULL;

    savelines = cfg.savelines;
    term_init();

    cfgtopalette();

    /*
     * Guess some defaults for the window size. This all gets
     * updated later, so we don't really care too much. However, we
     * do want the font width/height guesses to correspond to a
     * large font rather than a small one...
     */
    
    font_width = 10;
    font_height = 20;
    extra_width = 25;
    extra_height = 28;
    term_size (cfg.height, cfg.width, cfg.savelines);
    guess_width = extra_width + font_width * cols;
    guess_height = extra_height + font_height * rows;
    {
	RECT r;
	HWND w = GetDesktopWindow();
	GetWindowRect (w, &r);
	if (guess_width > r.right - r.left)
	    guess_width = r.right - r.left;
	if (guess_height > r.bottom - r.top)
	    guess_height = r.bottom - r.top;
    }

    {
	int winmode = WS_OVERLAPPEDWINDOW|WS_VSCROLL;
        int exwinmode = 0;
	if (!cfg.scrollbar)  winmode &= ~(WS_VSCROLL);
	if (cfg.locksize)    winmode &= ~(WS_THICKFRAME|WS_MAXIMIZEBOX);
        if (cfg.alwaysontop) exwinmode = WS_EX_TOPMOST;
        hwnd = CreateWindowEx (exwinmode, appname, appname,
                              winmode, CW_USEDEFAULT, CW_USEDEFAULT,
                              guess_width, guess_height,
                              NULL, NULL, inst, NULL);
     }

    /*
     * Initialise the fonts, simultaneously correcting the guesses
     * for font_{width,height}.
     */
    bold_mode = cfg.bold_colour ? BOLD_COLOURS : BOLD_FONT;
    und_mode = UND_FONT;
    init_fonts(0);

    /*
     * Correct the guesses for extra_{width,height}.
     */
    {
	RECT cr, wr;
	GetWindowRect (hwnd, &wr);
	GetClientRect (hwnd, &cr);
	extra_width = wr.right - wr.left - cr.right + cr.left;
	extra_height = wr.bottom - wr.top - cr.bottom + cr.top;
    }

    /*
     * Resize the window, now we know what size we _really_ want it
     * to be.
     */
    guess_width = extra_width + font_width * cols;
    guess_height = extra_height + font_height * rows;
    SendMessage (hwnd, WM_IGNORE_SIZE, 0, 0);
    SetWindowPos (hwnd, NULL, 0, 0, guess_width, guess_height,
		  SWP_NOMOVE | SWP_NOREDRAW | SWP_NOZORDER);

    /*
     * Set up a caret bitmap, with no content.
     */
    {
        char *bits;
        int size = (font_width+15)/16 * 2 * font_height; 
        bits = smalloc(size);
        memset(bits, 0, size);
        caretbm = CreateBitmap(font_width, font_height, 1, 1, bits);
        sfree(bits);
    }
    CreateCaret(hwnd, caretbm, font_width, font_height);

    /*
     * Initialise the scroll bar.
     */
    {
	SCROLLINFO si;

	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
	si.nMin = 0;
	si.nMax = rows-1;
	si.nPage = rows;
	si.nPos = 0;
	SetScrollInfo (hwnd, SB_VERT, &si, FALSE);
    }

    /*
     * Start up the telnet connection.
     */
    {
	char *error;
	char msg[1024], *title;
	char *realhost;

	error = back->init (cfg.host, cfg.port, &realhost);
	if (error) {
	    sprintf(msg, "Unable to open connection:\n%s", error);
	    MessageBox(NULL, msg, "PuTTY Error", MB_ICONERROR | MB_OK);
	    return 0;
	}
	window_name = icon_name = NULL;
        if (*cfg.wintitle) {
            title = cfg.wintitle;
        } else {
            sprintf(msg, "%s - PuTTY", realhost);
            title = msg;
        }
	set_title (title);
	set_icon (title);
    }

    session_closed = FALSE;

    /*
     * Set up the input and output buffers.
     */
    inbuf_head = 0;
    outbuf_reap = outbuf_head = 0;

    /*
     * Prepare the mouse handler.
     */
    lastact = MA_NOTHING;
    lastbtn = MB_NOTHING;
    dbltime = GetDoubleClickTime();

    /*
     * Set up the session-control options on the system menu.
     */
    {
	HMENU m = GetSystemMenu (hwnd, FALSE);
	HMENU p,s;
	int i;

	AppendMenu (m, MF_SEPARATOR, 0, 0);
	if (cfg.protocol == PROT_TELNET) {
	    p = CreateMenu();
	    AppendMenu (p, MF_ENABLED, IDM_TEL_AYT, "Are You There");
	    AppendMenu (p, MF_ENABLED, IDM_TEL_BRK, "Break");
	    AppendMenu (p, MF_ENABLED, IDM_TEL_SYNCH, "Synch");
	    AppendMenu (p, MF_SEPARATOR, 0, 0);
	    AppendMenu (p, MF_ENABLED, IDM_TEL_EC, "Erase Character");
	    AppendMenu (p, MF_ENABLED, IDM_TEL_EL, "Erase Line");
	    AppendMenu (p, MF_ENABLED, IDM_TEL_GA, "Go Ahead");
	    AppendMenu (p, MF_ENABLED, IDM_TEL_NOP, "No Operation");
	    AppendMenu (p, MF_SEPARATOR, 0, 0);
	    AppendMenu (p, MF_ENABLED, IDM_TEL_ABORT, "Abort Process");
	    AppendMenu (p, MF_ENABLED, IDM_TEL_AO, "Abort Output");
	    AppendMenu (p, MF_ENABLED, IDM_TEL_IP, "Interrupt Process");
	    AppendMenu (p, MF_ENABLED, IDM_TEL_SUSP, "Suspend Process");
	    AppendMenu (p, MF_SEPARATOR, 0, 0);
	    AppendMenu (p, MF_ENABLED, IDM_TEL_EOR, "End Of Record");
	    AppendMenu (p, MF_ENABLED, IDM_TEL_EOF, "End Of File");
	    AppendMenu (m, MF_POPUP | MF_ENABLED, (UINT) p, "Telnet Command");
	    AppendMenu (m, MF_SEPARATOR, 0, 0);
	}
	AppendMenu (m, MF_ENABLED, IDM_SHOWLOG, "&Event Log");
	AppendMenu (m, MF_SEPARATOR, 0, 0);
	AppendMenu (m, MF_ENABLED, IDM_NEWSESS, "Ne&w Session...");
	AppendMenu (m, MF_ENABLED, IDM_DUPSESS, "&Duplicate Session");
	s = CreateMenu();
	get_sesslist(TRUE);
	for (i = 1 ; i < ((nsessions < 256) ? nsessions : 256) ; i++)
	  AppendMenu (s, MF_ENABLED, IDM_SAVED_MIN + (16 * i) , sessions[i]);
	AppendMenu (m, MF_POPUP | MF_ENABLED, (UINT) s, "Sa&ved Sessions");
	AppendMenu (m, MF_ENABLED, IDM_RECONF, "Chan&ge Settings...");
	AppendMenu (m, MF_SEPARATOR, 0, 0);
	AppendMenu (m, MF_ENABLED, IDM_COPYALL, "C&opy All to Clipboard");
	AppendMenu (m, MF_ENABLED, IDM_CLRSB, "C&lear Scrollback");
	AppendMenu (m, MF_ENABLED, IDM_RESET, "Rese&t Terminal");
	AppendMenu (m, MF_SEPARATOR, 0, 0);
	AppendMenu (m, MF_ENABLED, IDM_ABOUT, "&About PuTTY");
    }

    /*
     * Finally show the window!
     */
    ShowWindow (hwnd, show);

    /*
     * Open the initial log file if there is one.
     */
    logfopen();

    /*
     * Set the palette up.
     */
    pal = NULL;
    logpal = NULL;
    init_palette();

    has_focus = (GetForegroundWindow() == hwnd);
    UpdateWindow (hwnd);

    if (GetMessage (&msg, NULL, 0, 0) == 1)
    {
	int timer_id = 0, long_timer = 0;

	while (msg.message != WM_QUIT) {
	    /* Sometimes DispatchMessage calls routines that use their own
	     * GetMessage loop, setup this timer so we get some control back.
	     *
	     * Also call term_update() from the timer so that if the host
	     * is sending data flat out we still do redraws.
	     */
	    if(timer_id && long_timer) {
		KillTimer(hwnd, timer_id);
		long_timer = timer_id = 0;
	    }
	    if(!timer_id)
		timer_id = SetTimer(hwnd, 1, 20, NULL);
            if (!(IsWindow(logbox) && IsDialogMessage(logbox, &msg)))
                DispatchMessage (&msg);

	    /* Make sure we blink everything that needs it. */
	    term_blink(0);

	    /* Send the paste buffer if there's anything to send */
	    term_paste();

	    /* If there's nothing new in the queue then we can do everything
	     * we've delayed, reading the socket, writing, and repainting
	     * the window.
	     */
	    if (PeekMessage (&msg, NULL, 0, 0, PM_REMOVE))
		continue;

	    if (pending_netevent) {
		enact_pending_netevent();

		/* Force the cursor blink on */
		term_blink(1);

		if (PeekMessage (&msg, NULL, 0, 0, PM_REMOVE))
		    continue;
	    }

	    /* Okay there is now nothing to do so we make sure the screen is
	     * completely up to date then tell windows to call us in a little 
	     * while.
	     */
	    if (timer_id) {
		KillTimer(hwnd, timer_id);
		timer_id = 0;
	    }
            HideCaret(hwnd);
	    if (inbuf_head)
		term_out();
	    term_update();
            ShowCaret(hwnd);
	    if (!has_focus)
	       timer_id = SetTimer(hwnd, 1, 59500, NULL);
	    else
	       timer_id = SetTimer(hwnd, 1, 100, NULL);
	    long_timer = 1;
	
	    /* There's no point rescanning everything in the message queue
	     * so we do an apperently unneccesary wait here 
	     */
	    WaitMessage();
	    if (GetMessage (&msg, NULL, 0, 0) != 1)
		break;
	}
    }

    /*
     * Clean up.
     */
    {
	int i;
	for (i=0; i<8; i++)
	    if (fonts[i])
		DeleteObject(fonts[i]);
    }
    sfree(logpal);
    if (pal)
	DeleteObject(pal);
    WSACleanup();

    if (cfg.protocol == PROT_SSH) {
	random_save_seed();
#ifdef MSCRYPTOAPI
	crypto_wrapup();
#endif
    }

    return msg.wParam;
}

/*
 * Set up, or shut down, an AsyncSelect. Called from winnet.c.
 */
char *do_select(SOCKET skt, int startup) {
    int msg, events;
    if (startup) {
	msg = WM_NETEVENT;
	events = FD_READ | FD_WRITE | FD_OOB | FD_CLOSE;
    } else {
	msg = events = 0;
    }
    if (!hwnd)
	return "do_select(): internal error (hwnd==NULL)";
    if (WSAAsyncSelect (skt, hwnd, msg, events) == SOCKET_ERROR) {
        switch (WSAGetLastError()) {
          case WSAENETDOWN: return "Network is down";
          default: return "WSAAsyncSelect(): unknown error";
        }
    }
    return NULL;
}

/*
 * Print a message box and close the connection.
 */
void connection_fatal(char *fmt, ...) {
    va_list ap;
    char stuff[200];

    va_start(ap, fmt);
    vsprintf(stuff, fmt, ap);
    va_end(ap);
    MessageBox(hwnd, stuff, "PuTTY Fatal Error", MB_ICONERROR | MB_OK);
    if (cfg.close_on_exit == COE_ALWAYS)
        PostQuitMessage(1);
    else {
        session_closed = TRUE;
        SetWindowText (hwnd, "PuTTY (inactive)");
    }
}

/*
 * Actually do the job requested by a WM_NETEVENT
 */
static void enact_pending_netevent(void) {
    static int reentering = 0;
    extern int select_result(WPARAM, LPARAM);
    int ret;

    if (reentering)
        return;                        /* don't unpend the pending */

    pending_netevent = FALSE;

    reentering = 1;
    ret = select_result (pend_netevent_wParam, pend_netevent_lParam);
    reentering = 0;

    if (ret == 0 && !session_closed) {
        /* Abnormal exits will already have set session_closed and taken
         * appropriate action. */
	if (cfg.close_on_exit == COE_ALWAYS ||
            cfg.close_on_exit == COE_NORMAL)
	    PostQuitMessage(0);
	else {
            session_closed = TRUE;
            SetWindowText (hwnd, "PuTTY (inactive)");
            MessageBox(hwnd, "Connection closed by remote host",
                       "PuTTY", MB_OK | MB_ICONINFORMATION);
	}
    }
}

/*
 * Copy the colour palette from the configuration data into defpal.
 * This is non-trivial because the colour indices are different.
 */
static void cfgtopalette(void) {
    int i;
    static const int ww[] = {
	6, 7, 8, 9, 10, 11, 12, 13,
	14, 15, 16, 17, 18, 19, 20, 21,
	0, 1, 2, 3, 4, 4, 5, 5
    };

    for (i=0; i<24; i++) {
	int w = ww[i];
	defpal[i].rgbtRed = cfg.colours[w][0];
	defpal[i].rgbtGreen = cfg.colours[w][1];
	defpal[i].rgbtBlue = cfg.colours[w][2];
    }
}

/*
 * Set up the colour palette.
 */
static void init_palette(void) {
    int i;
    HDC hdc = GetDC (hwnd);
    if (hdc) {
	if (cfg.try_palette &&
	    GetDeviceCaps (hdc, RASTERCAPS) & RC_PALETTE) {
	    logpal = smalloc(sizeof(*logpal)
			     - sizeof(logpal->palPalEntry)
			     + NCOLOURS * sizeof(PALETTEENTRY));
	    logpal->palVersion = 0x300;
	    logpal->palNumEntries = NCOLOURS;
	    for (i = 0; i < NCOLOURS; i++) {
		logpal->palPalEntry[i].peRed = defpal[i].rgbtRed;
		logpal->palPalEntry[i].peGreen = defpal[i].rgbtGreen;
		logpal->palPalEntry[i].peBlue = defpal[i].rgbtBlue;
		logpal->palPalEntry[i].peFlags = PC_NOCOLLAPSE;
	    }
	    pal = CreatePalette (logpal);
	    if (pal) {
		SelectPalette (hdc, pal, FALSE);
		RealizePalette (hdc);
		SelectPalette (hdc, GetStockObject (DEFAULT_PALETTE),
			       FALSE);
	    }
	}
	ReleaseDC (hwnd, hdc);
    }
    if (pal)
	for (i=0; i<NCOLOURS; i++)
	    colours[i] = PALETTERGB(defpal[i].rgbtRed,
				    defpal[i].rgbtGreen,
				    defpal[i].rgbtBlue);
    else
	for(i=0; i<NCOLOURS; i++)
	    colours[i] = RGB(defpal[i].rgbtRed,
			     defpal[i].rgbtGreen,
			     defpal[i].rgbtBlue);
}

/*
 * Initialise all the fonts we will need. There may be as many as
 * eight or as few as one. We also:
 *
 * - check the font width and height, correcting our guesses if
 *   necessary.
 *
 * - verify that the bold font is the same width as the ordinary
 *   one, and engage shadow bolding if not.
 * 
 * - verify that the underlined font is the same width as the
 *   ordinary one (manual underlining by means of line drawing can
 *   be done in a pinch).
 */
static void init_fonts(int pick_width) {
    TEXTMETRIC tm;
    int i;
    int fsize[8];
    HDC hdc;
    int fw_dontcare, fw_bold;
    int firstchar = ' ';

#ifdef CHECKOEMFONT
font_messup:
#endif
    for (i=0; i<8; i++)
	fonts[i] = NULL;

    if (cfg.fontisbold) {
	fw_dontcare = FW_BOLD;
	fw_bold = FW_HEAVY;
   } else {
	fw_dontcare = FW_DONTCARE;
	fw_bold = FW_BOLD;
    }

    hdc = GetDC(hwnd);

    font_height = cfg.fontheight;
    if (font_height > 0) {
        font_height = -MulDiv(font_height, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    }
    font_width = pick_width;

#define f(i,c,w,u) \
    fonts[i] = CreateFont (font_height, font_width, 0, 0, w, FALSE, u, FALSE, \
			   c, OUT_DEFAULT_PRECIS, \
		           CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, \
			   FIXED_PITCH | FF_DONTCARE, cfg.font)

    if (cfg.vtmode != VT_OEMONLY) {
	f(FONT_NORMAL, cfg.fontcharset, fw_dontcare, FALSE);

	SelectObject (hdc, fonts[FONT_NORMAL]);
	GetTextMetrics(hdc, &tm); 
	font_height = tm.tmHeight;
	font_width = tm.tmAveCharWidth;

	f(FONT_UNDERLINE, cfg.fontcharset, fw_dontcare, TRUE);

        /*
         * Some fonts, e.g. 9-pt Courier, draw their underlines
         * outside their character cell. We successfully prevent
         * screen corruption by clipping the text output, but then
         * we lose the underline completely. Here we try to work
         * out whether this is such a font, and if it is, we set a
         * flag that causes underlines to be drawn by hand.
         *
         * Having tried other more sophisticated approaches (such
         * as examining the TEXTMETRIC structure or requesting the
         * height of a string), I think we'll do this the brute
         * force way: we create a small bitmap, draw an underlined
         * space on it, and test to see whether any pixels are
         * foreground-coloured. (Since we expect the underline to
         * go all the way across the character cell, we only search
         * down a single column of the bitmap, half way across.)
         */
        {
            HDC und_dc;
            HBITMAP und_bm, und_oldbm;
            int i, gotit;
            COLORREF c;

            und_dc = CreateCompatibleDC(hdc);
            und_bm = CreateCompatibleBitmap(hdc, font_width, font_height);
            und_oldbm = SelectObject(und_dc, und_bm);
            SelectObject(und_dc, fonts[FONT_UNDERLINE]);
            SetTextAlign(und_dc, TA_TOP | TA_LEFT | TA_NOUPDATECP);
            SetTextColor (und_dc, RGB(255,255,255));
            SetBkColor (und_dc, RGB(0,0,0));
            SetBkMode (und_dc, OPAQUE);
            ExtTextOut (und_dc, 0, 0, ETO_OPAQUE, NULL, " ", 1, NULL);
            gotit = FALSE;
            for (i = 0; i < font_height; i++) {
                c = GetPixel(und_dc, font_width/2, i);
                if (c != RGB(0,0,0))
                    gotit = TRUE;
            }
            SelectObject(und_dc, und_oldbm);
            DeleteObject(und_bm);
            DeleteDC(und_dc);
            font_needs_hand_underlining = !gotit;
        }

        if (bold_mode == BOLD_FONT) {
	    f(FONT_BOLD, cfg.fontcharset, fw_bold, FALSE);
	    f(FONT_BOLDUND, cfg.fontcharset, fw_bold, TRUE);
	}

        if (cfg.vtmode == VT_OEMANSI) {
	    f(FONT_OEM, OEM_CHARSET, fw_dontcare, FALSE);
	    f(FONT_OEMUND, OEM_CHARSET, fw_dontcare, TRUE);

	    if (bold_mode == BOLD_FONT) {
		f(FONT_OEMBOLD, OEM_CHARSET, fw_bold, FALSE);
		f(FONT_OEMBOLDUND, OEM_CHARSET, fw_bold, TRUE);
	    }
        }
    }
    else
    {
	f(FONT_OEM, cfg.fontcharset, fw_dontcare, FALSE);

	SelectObject (hdc, fonts[FONT_OEM]);
	GetTextMetrics(hdc, &tm); 
	font_height = tm.tmHeight;
	font_width = tm.tmAveCharWidth;

	f(FONT_OEMUND, cfg.fontcharset, fw_dontcare, TRUE);

	if (bold_mode == BOLD_FONT) {
	    f(FONT_BOLD, cfg.fontcharset, fw_bold, FALSE);
	    f(FONT_BOLDUND, cfg.fontcharset, fw_bold, TRUE);
	}
    }
#undef f

    descent = tm.tmAscent + 1;
    if (descent >= font_height)
	descent = font_height - 1;
    firstchar = tm.tmFirstChar;

    for (i=0; i<8; i++) {
	if (fonts[i]) {
	    if (SelectObject (hdc, fonts[i]) &&
	        GetTextMetrics(hdc, &tm) )
	         fsize[i] = tm.tmAveCharWidth + 256 * tm.tmHeight;
	    else fsize[i] = -i;
	}
	else fsize[i] = -i;
    }

    ReleaseDC (hwnd, hdc);

    /* ... This is wrong in OEM only mode */
    if (fsize[FONT_UNDERLINE] != fsize[FONT_NORMAL] ||
	(bold_mode == BOLD_FONT &&
	 fsize[FONT_BOLDUND] != fsize[FONT_BOLD])) {
	und_mode = UND_LINE;
	DeleteObject (fonts[FONT_UNDERLINE]);
	if (bold_mode == BOLD_FONT)
	    DeleteObject (fonts[FONT_BOLDUND]);
    }

    if (bold_mode == BOLD_FONT &&
	fsize[FONT_BOLD] != fsize[FONT_NORMAL]) {
	bold_mode = BOLD_SHADOW;
	DeleteObject (fonts[FONT_BOLD]);
	if (und_mode == UND_FONT)
	    DeleteObject (fonts[FONT_BOLDUND]);
    }

#ifdef CHECKOEMFONT
    /* With the fascist font painting it doesn't matter if the linedraw font
     * isn't exactly the right size anymore so we don't have to check this.
     */
    if (cfg.vtmode == VT_OEMANSI && fsize[FONT_OEM] != fsize[FONT_NORMAL] ) {
	if( cfg.fontcharset == OEM_CHARSET )
	{
	    MessageBox(NULL, "The OEM and ANSI versions of this font are\n"
		   "different sizes. Using OEM-only mode instead",
		   "Font Size Mismatch", MB_ICONINFORMATION | MB_OK);
	    cfg.vtmode = VT_OEMONLY;
	}
	else if( firstchar < ' ' )
	{
	    MessageBox(NULL, "The OEM and ANSI versions of this font are\n"
		   "different sizes. Using XTerm mode instead",
		   "Font Size Mismatch", MB_ICONINFORMATION | MB_OK);
	    cfg.vtmode = VT_XWINDOWS;
	}
	else
	{
	    MessageBox(NULL, "The OEM and ANSI versions of this font are\n"
		   "different sizes. Using ISO8859-1 mode instead",
		   "Font Size Mismatch", MB_ICONINFORMATION | MB_OK);
	    cfg.vtmode = VT_POORMAN;
	}

	for (i=0; i<8; i++)
	    if (fonts[i])
		DeleteObject (fonts[i]);
	goto font_messup;
    }
#endif
}

void request_resize (int w, int h, int refont) {
    int width, height;

    /* If the window is maximized supress resizing attempts */
    if(IsZoomed(hwnd)) return;
    
#ifdef CHECKOEMFONT
    /* Don't do this in OEMANSI, you may get disable messages */
    if (refont && w != cols && (cols==80 || cols==132)
	  && cfg.vtmode != VT_OEMANSI)
#else
    if (refont && w != cols && (cols==80 || cols==132))
#endif
    {
       /* If font width too big for screen should we shrink the font more ? */
        if (w==132)
            font_width = ((font_width*cols+w/2)/w);
        else
	    font_width = 0;
	{
	    int i;
	    for (i=0; i<8; i++)
		if (fonts[i])
		    DeleteObject(fonts[i]);
	}
        bold_mode = cfg.bold_colour ? BOLD_COLOURS : BOLD_FONT;
        und_mode = UND_FONT;
        init_fonts(font_width);
    }
    else
    {
       static int first_time = 1;
       static RECT ss;

       switch(first_time)
       {
       case 1:
	     /* Get the size of the screen */
	     if (GetClientRect(GetDesktopWindow(),&ss))
		/* first_time = 0 */;
	     else { first_time = 2; break; }
       case 0:
	     /* Make sure the values are sane */
	     width  = (ss.right-ss.left-extra_width  ) / font_width;
	     height = (ss.bottom-ss.top-extra_height ) / font_height;

	     if (w>width)  w=width;
	     if (h>height) h=height;
	     if (w<15) w = 15;
	     if (h<1) w = 1;
       }
    }

    width = extra_width + font_width * w;
    height = extra_height + font_height * h;

    SetWindowPos (hwnd, NULL, 0, 0, width, height,
		  SWP_NOACTIVATE | SWP_NOCOPYBITS |
		  SWP_NOMOVE | SWP_NOZORDER);
}

static void click (Mouse_Button b, int x, int y) {
    int thistime = GetMessageTime();

    if (lastbtn == b && thistime - lasttime < dbltime) {
	lastact = (lastact == MA_CLICK ? MA_2CLK :
		   lastact == MA_2CLK ? MA_3CLK :
		   lastact == MA_3CLK ? MA_CLICK : MA_NOTHING);
    } else {
	lastbtn = b;
	lastact = MA_CLICK;
    }
    if (lastact != MA_NOTHING)
	term_mouse (b, lastact, x, y);
    lasttime = thistime;
}

static void show_mouseptr(int show) {
    static int cursor_visible = 1;
    if (!cfg.hide_mouseptr)            /* override if this feature disabled */
        show = 1;
    if (cursor_visible && !show)
        ShowCursor(FALSE);
    else if (!cursor_visible && show)
        ShowCursor(TRUE);
    cursor_visible = show;
}

static LRESULT CALLBACK WndProc (HWND hwnd, UINT message,
                                 WPARAM wParam, LPARAM lParam) {
    HDC hdc;
    static int ignore_size = FALSE;
    static int ignore_clip = FALSE;
    static int just_reconfigged = FALSE;
    static int resizing = FALSE;
    static int need_backend_resize = FALSE;

    switch (message) {
      case WM_TIMER:
	if (pending_netevent)
	    enact_pending_netevent();
	if (inbuf_head)
	    term_out();
        noise_regular();
        HideCaret(hwnd);
	term_update();
        ShowCaret(hwnd);
	if (cfg.ping_interval > 0)
        {
           time_t now;
           time(&now);
           if (now-last_movement > cfg.ping_interval)
           {
              back->special(TS_PING);
              last_movement = now;
           }
        }
	return 0;
      case WM_CREATE:
	break;
      case WM_CLOSE:
        show_mouseptr(1);
	if (!cfg.warn_on_close || session_closed ||
	    MessageBox(hwnd, "Are you sure you want to close this session?",
		       "PuTTY Exit Confirmation",
		       MB_ICONWARNING | MB_OKCANCEL) == IDOK)
	    DestroyWindow(hwnd);
	return 0;
      case WM_DESTROY:
        show_mouseptr(1);
	PostQuitMessage (0);
	return 0;
      case WM_SYSCOMMAND:
	switch (wParam & ~0xF) {       /* low 4 bits reserved to Windows */
	  case IDM_SHOWLOG:
	    showeventlog(hwnd);
	    break;
	  case IDM_NEWSESS:
	  case IDM_DUPSESS:
	  case IDM_SAVEDSESS:
	    {
		char b[2048];
		char c[30], *cl;
		int freecl = FALSE;
		STARTUPINFO si;
		PROCESS_INFORMATION pi;
		HANDLE filemap = NULL;

		if (wParam == IDM_DUPSESS) {
		    /*
		     * Allocate a file-mapping memory chunk for the
		     * config structure.
		     */
		    SECURITY_ATTRIBUTES sa;
		    Config *p;

		    sa.nLength = sizeof(sa);
		    sa.lpSecurityDescriptor = NULL;
		    sa.bInheritHandle = TRUE;
		    filemap = CreateFileMapping((HANDLE)0xFFFFFFFF,
						&sa,
						PAGE_READWRITE,
						0,
						sizeof(Config),
						NULL);
		    if (filemap) {
			p = (Config *)MapViewOfFile(filemap,
						    FILE_MAP_WRITE,
						    0, 0, sizeof(Config));
			if (p) {
			    *p = cfg;  /* structure copy */
			    UnmapViewOfFile(p);
			}
		    }
		    sprintf(c, "putty &%p", filemap);
		    cl = c;
		} else if (wParam == IDM_SAVEDSESS) {
		    char *session = sessions[(lParam - IDM_SAVED_MIN) / 16];
		    cl = smalloc(16 + strlen(session)); /* 8, but play safe */
		    if (!cl)
			cl = NULL;     /* not a very important failure mode */
		    else {
			sprintf(cl, "putty @%s", session);
			freecl = TRUE;
		    }
		} else
		    cl = NULL;

		GetModuleFileName (NULL, b, sizeof(b)-1);
		si.cb = sizeof(si);
		si.lpReserved = NULL;
		si.lpDesktop = NULL;
		si.lpTitle = NULL;
		si.dwFlags = 0;
		si.cbReserved2 = 0;
		si.lpReserved2 = NULL;
		CreateProcess (b, cl, NULL, NULL, TRUE,
			       NORMAL_PRIORITY_CLASS, NULL, NULL, &si, &pi);

		if (filemap)
		    CloseHandle(filemap);
		if (freecl)
		    sfree(cl);
	    }
	    break;
          case IDM_RECONF:
            {
                int prev_alwaysontop = cfg.alwaysontop;
		char oldlogfile[FILENAME_MAX];
		int oldlogtype;
		int need_setwpos = FALSE;
		int old_fwidth, old_fheight;

		strcpy(oldlogfile, cfg.logfilename);
		oldlogtype = cfg.logtype;
		cfg.width = cols;
		cfg.height = rows;
		old_fwidth = font_width;
		old_fheight = font_height;
                GetWindowText(hwnd, cfg.wintitle, sizeof(cfg.wintitle));

                if (!do_reconfig(hwnd))
                    break;

		if (strcmp(oldlogfile, cfg.logfilename) ||
		    oldlogtype != cfg.logtype) {
		    logfclose();       /* reset logging */
		    logfopen();
		}

                just_reconfigged = TRUE;
                {
                    int i;
                    for (i=0; i<8; i++)
                        if (fonts[i])
                            DeleteObject(fonts[i]);
                }
                bold_mode = cfg.bold_colour ? BOLD_COLOURS : BOLD_FONT;
                und_mode = UND_FONT;
                init_fonts(0);
                sfree(logpal);
                /*
                 * Flush the line discipline's edit buffer in the
                 * case where local editing has just been disabled.
                 */
                ldisc_send(NULL, 0);
                if (pal)
                    DeleteObject(pal);
                logpal = NULL;
                pal = NULL;
                cfgtopalette();
                init_palette();

                /* Enable or disable the scroll bar, etc */
                {
                    LONG nflg, flag = GetWindowLong(hwnd, GWL_STYLE);
                    LONG nexflag, exflag = GetWindowLong(hwnd, GWL_EXSTYLE);

                    nexflag = exflag;
                    if (cfg.alwaysontop != prev_alwaysontop) {
                        if (cfg.alwaysontop) {
                            nexflag = WS_EX_TOPMOST;
                            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                                         SWP_NOMOVE | SWP_NOSIZE);
                        } else {
                            nexflag = 0;
                            SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                                         SWP_NOMOVE | SWP_NOSIZE);
                        }
                    }

                    nflg = flag;
                    if (cfg.scrollbar) nflg |=  WS_VSCROLL;
                    else               nflg &= ~WS_VSCROLL;
                    if (cfg.locksize)
                        nflg &= ~(WS_THICKFRAME|WS_MAXIMIZEBOX);
                    else
                        nflg |= (WS_THICKFRAME|WS_MAXIMIZEBOX);

                    if (nflg != flag || nexflag != exflag)
                    {
                        RECT cr, wr;

                        if (nflg != flag)
                            SetWindowLong(hwnd, GWL_STYLE, nflg);
                        if (nexflag != exflag)
                            SetWindowLong(hwnd, GWL_EXSTYLE, nexflag);

                        SendMessage (hwnd, WM_IGNORE_SIZE, 0, 0);

                        SetWindowPos(hwnd, NULL, 0,0,0,0,
                                     SWP_NOACTIVATE|SWP_NOCOPYBITS|
                                     SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|
                                     SWP_FRAMECHANGED);

                        GetWindowRect (hwnd, &wr);
                        GetClientRect (hwnd, &cr);
                        extra_width = wr.right - wr.left - cr.right + cr.left;
                        extra_height = wr.bottom - wr.top - cr.bottom + cr.top;
                    }
                }

		if (cfg.height != rows ||
		    cfg.width != cols ||
		    old_fwidth != font_width ||
		    old_fheight != font_height ||
		    cfg.savelines != savelines)
		    need_setwpos = TRUE;
                term_size(cfg.height, cfg.width, cfg.savelines);
                InvalidateRect(hwnd, NULL, TRUE);
                if (need_setwpos) {
		    force_normal(hwnd);
		    SetWindowPos (hwnd, NULL, 0, 0,
				  extra_width + font_width * cfg.width,
				  extra_height + font_height * cfg.height,
				  SWP_NOACTIVATE | SWP_NOCOPYBITS |
				  SWP_NOMOVE | SWP_NOZORDER);
		}
                set_title(cfg.wintitle);
                if (IsIconic(hwnd)) {
                    SetWindowText (hwnd,
                                   cfg.win_name_always ? window_name : icon_name);
                }
            }
            break;
	  case IDM_COPYALL:
	    term_copyall();
	    break;
          case IDM_CLRSB:
            term_clrsb();
            break;
          case IDM_RESET:
            term_pwron();
            break;
          case IDM_TEL_AYT: back->special (TS_AYT); break;
	  case IDM_TEL_BRK: back->special (TS_BRK); break;
	  case IDM_TEL_SYNCH: back->special (TS_SYNCH); break;
	  case IDM_TEL_EC: back->special (TS_EC); break;
	  case IDM_TEL_EL: back->special (TS_EL); break;
	  case IDM_TEL_GA: back->special (TS_GA); break;
	  case IDM_TEL_NOP: back->special (TS_NOP); break;
	  case IDM_TEL_ABORT: back->special (TS_ABORT); break;
	  case IDM_TEL_AO: back->special (TS_AO); break;
	  case IDM_TEL_IP: back->special (TS_IP); break;
	  case IDM_TEL_SUSP: back->special (TS_SUSP); break;
	  case IDM_TEL_EOR: back->special (TS_EOR); break;
	  case IDM_TEL_EOF: back->special (TS_EOF); break;
	  case IDM_ABOUT:
	    showabout (hwnd);
	    break;
	default:
	  if (wParam >= IDM_SAVED_MIN && wParam <= IDM_SAVED_MAX) {
	    SendMessage(hwnd, WM_SYSCOMMAND, IDM_SAVEDSESS, wParam);
	  }
	}
	break;

#define X_POS(l) ((int)(short)LOWORD(l))
#define Y_POS(l) ((int)(short)HIWORD(l))

#define TO_CHR_X(x) (((x)<0 ? (x)-font_width+1 : (x)) / font_width)
#define TO_CHR_Y(y) (((y)<0 ? (y)-font_height+1: (y)) / font_height)

      case WM_LBUTTONDOWN:
        show_mouseptr(1);
	click (MB_SELECT, TO_CHR_X(X_POS(lParam)),
	       TO_CHR_Y(Y_POS(lParam)));
        SetCapture(hwnd);
	return 0;
      case WM_LBUTTONUP:
        show_mouseptr(1);
	term_mouse (MB_SELECT, MA_RELEASE, TO_CHR_X(X_POS(lParam)),
		    TO_CHR_Y(Y_POS(lParam)));
        ReleaseCapture();
	return 0;
      case WM_MBUTTONDOWN:
        show_mouseptr(1);
        SetCapture(hwnd);
	click (cfg.mouse_is_xterm ? MB_PASTE : MB_EXTEND,
	       TO_CHR_X(X_POS(lParam)),
	       TO_CHR_Y(Y_POS(lParam)));
	return 0;
      case WM_MBUTTONUP:
        show_mouseptr(1);
	term_mouse (cfg.mouse_is_xterm ? MB_PASTE : MB_EXTEND,
		    MA_RELEASE, TO_CHR_X(X_POS(lParam)),
		    TO_CHR_Y(Y_POS(lParam)));
        ReleaseCapture();
	return 0;
      case WM_RBUTTONDOWN:
        show_mouseptr(1);
        SetCapture(hwnd);
	click (cfg.mouse_is_xterm ? MB_EXTEND : MB_PASTE,
	       TO_CHR_X(X_POS(lParam)),
	       TO_CHR_Y(Y_POS(lParam)));
	return 0;
      case WM_RBUTTONUP:
        show_mouseptr(1);
	term_mouse (cfg.mouse_is_xterm ? MB_EXTEND : MB_PASTE,
		    MA_RELEASE, TO_CHR_X(X_POS(lParam)),
		    TO_CHR_Y(Y_POS(lParam)));
        ReleaseCapture();
	return 0;
      case WM_MOUSEMOVE:
        show_mouseptr(1);
	/*
	 * Add the mouse position and message time to the random
	 * number noise.
	 */
        noise_ultralight(lParam);

	if (wParam & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON)) {
	    Mouse_Button b;
	    if (wParam & MK_LBUTTON)
		b = MB_SELECT;
	    else if (wParam & MK_MBUTTON)
		b = cfg.mouse_is_xterm ? MB_PASTE : MB_EXTEND;
	    else
		b = cfg.mouse_is_xterm ? MB_EXTEND : MB_PASTE;
	    term_mouse (b, MA_DRAG, TO_CHR_X(X_POS(lParam)),
			TO_CHR_Y(Y_POS(lParam)));
	}
	return 0;
      case WM_NCMOUSEMOVE:
	show_mouseptr(1);
        noise_ultralight(lParam);
	return 0;
      case WM_IGNORE_CLIP:
	ignore_clip = wParam;	       /* don't panic on DESTROYCLIPBOARD */
	break;
      case WM_DESTROYCLIPBOARD:
	if (!ignore_clip)
	    term_deselect();
	ignore_clip = FALSE;
	return 0;
      case WM_PAINT:
	{
	    PAINTSTRUCT p;
            HideCaret(hwnd);
	    hdc = BeginPaint (hwnd, &p);
	    if (pal) {
		SelectPalette (hdc, pal, TRUE);
		RealizePalette (hdc);
	    }
	    term_paint (hdc, p.rcPaint.left, p.rcPaint.top,
			p.rcPaint.right, p.rcPaint.bottom);
	    SelectObject (hdc, GetStockObject(SYSTEM_FONT));
	    SelectObject (hdc, GetStockObject(WHITE_PEN));
	    EndPaint (hwnd, &p);
            ShowCaret(hwnd);
	}
	return 0;
      case WM_NETEVENT:
	/* Notice we can get multiple netevents, FD_READ, FD_WRITE etc
	 * but the only one that's likely to try to overload us is FD_READ.
	 * This means buffering just one is fine.
	 */
	if (pending_netevent)
	    enact_pending_netevent();

	pending_netevent = TRUE;
	pend_netevent_wParam=wParam;
	pend_netevent_lParam=lParam;
	time(&last_movement);
	return 0;
      case WM_SETFOCUS:
	has_focus = TRUE;
        CreateCaret(hwnd, caretbm, font_width, font_height);
        ShowCaret(hwnd);
        compose_state = 0;
	term_out();
	term_update();
	break;
      case WM_KILLFOCUS:
        show_mouseptr(1);
	has_focus = FALSE;
        DestroyCaret();
	term_out();
	term_update();
	break;
      case WM_IGNORE_SIZE:
	ignore_size = TRUE;	       /* don't panic on next WM_SIZE msg */
	break;
      case WM_ENTERSIZEMOVE:
        EnableSizeTip(1);
        resizing = TRUE;
	need_backend_resize = FALSE;
        break;
      case WM_EXITSIZEMOVE:
        EnableSizeTip(0);
        resizing = FALSE;
	if (need_backend_resize)
	    back->size();
        break;
      case WM_SIZING:
	{
	    int width, height, w, h, ew, eh;
	    LPRECT r = (LPRECT)lParam;

	    width = r->right - r->left - extra_width;
	    height = r->bottom - r->top - extra_height;
	    w = (width + font_width/2) / font_width; if (w < 1) w = 1;
	    h = (height + font_height/2) / font_height; if (h < 1) h = 1;
            UpdateSizeTip(hwnd, w, h);
	    ew = width - w * font_width;
	    eh = height - h * font_height;
	    if (ew != 0) {
		if (wParam == WMSZ_LEFT ||
		    wParam == WMSZ_BOTTOMLEFT ||
		    wParam == WMSZ_TOPLEFT)
		    r->left += ew;
		else
		    r->right -= ew;
	    }
	    if (eh != 0) {
		if (wParam == WMSZ_TOP ||
		    wParam == WMSZ_TOPRIGHT ||
		    wParam == WMSZ_TOPLEFT)
		    r->top += eh;
		else
		    r->bottom -= eh;
	    }
	    if (ew || eh)
		return 1;
	    else
		return 0;
	}
        /* break;  (never reached) */
      case WM_SIZE:
	if (wParam == SIZE_MINIMIZED) {
	    SetWindowText (hwnd,
			   cfg.win_name_always ? window_name : icon_name);
	    break;
	}
	if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)
	    SetWindowText (hwnd, window_name);
	if (!ignore_size) {
	    int width, height, w, h;
#if 0 /* we have fixed this using WM_SIZING now */
            int ew, eh;
#endif

	    width = LOWORD(lParam);
	    height = HIWORD(lParam);
	    w = width / font_width; if (w < 1) w = 1;
	    h = height / font_height; if (h < 1) h = 1;
#if 0 /* we have fixed this using WM_SIZING now */
	    ew = width - w * font_width;
	    eh = height - h * font_height;
	    if (ew != 0 || eh != 0) {
		RECT r;
		GetWindowRect (hwnd, &r);
		SendMessage (hwnd, WM_IGNORE_SIZE, 0, 0);
		SetWindowPos (hwnd, NULL, 0, 0,
			      r.right - r.left - ew, r.bottom - r.top - eh,
			      SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER);
	    }
#endif
	    if (w != cols || h != rows || just_reconfigged) {
		term_invalidate();
		term_size (h, w, cfg.savelines);
                /*
                 * Don't call back->size in mid-resize. (To prevent
                 * massive numbers of resize events getting sent
                 * down the connection during an NT opaque drag.)
                 */
                if (!resizing)
                    back->size();
		else
		    need_backend_resize = TRUE;
		just_reconfigged = FALSE;
	    }
	}
	ignore_size = FALSE;
	return 0;
      case WM_VSCROLL:
	switch (LOWORD(wParam)) {
	  case SB_BOTTOM: term_scroll(-1, 0); break;
	  case SB_TOP: term_scroll(+1, 0); break;
	  case SB_LINEDOWN: term_scroll (0, +1); break;
	  case SB_LINEUP: term_scroll (0, -1); break;
	  case SB_PAGEDOWN: term_scroll (0, +rows/2); break;
	  case SB_PAGEUP: term_scroll (0, -rows/2); break;
	  case SB_THUMBPOSITION: case SB_THUMBTRACK:
	    term_scroll (1, HIWORD(wParam)); break;
	}
	break; 
     case WM_PALETTECHANGED:
	if ((HWND) wParam != hwnd && pal != NULL) {
	    HDC hdc = get_ctx();
	    if (hdc) {
		if (RealizePalette (hdc) > 0)
		    UpdateColors (hdc);
		free_ctx (hdc);
	    }
	}
	break;
      case WM_QUERYNEWPALETTE:
	if (pal != NULL) {
	    HDC hdc = get_ctx();
	    if (hdc) {
		if (RealizePalette (hdc) > 0)
		    UpdateColors (hdc);
		free_ctx (hdc);
		return TRUE;
	    }
	}
	return FALSE;
      case WM_KEYDOWN:
      case WM_SYSKEYDOWN:
      case WM_KEYUP:
      case WM_SYSKEYUP:
	/*
	 * Add the scan code and keypress timing to the random
	 * number noise.
	 */
        noise_ultralight(lParam);

	/*
	 * We don't do TranslateMessage since it disassociates the
	 * resulting CHAR message from the KEYDOWN that sparked it,
	 * which we occasionally don't want. Instead, we process
	 * KEYDOWN, and call the Win32 translator functions so that
	 * we get the translations under _our_ control.
	 */
	{
	    unsigned char buf[20];
	    int len;

            if (wParam==VK_PROCESSKEY) {
		MSG m;
                m.hwnd = hwnd;
                m.message = WM_KEYDOWN;
                m.wParam = wParam;
                m.lParam = lParam & 0xdfff;
                TranslateMessage(&m);
            } else {
		len = TranslateKey (message, wParam, lParam, buf);
		if (len == -1)
		    return DefWindowProc (hwnd, message, wParam, lParam);
		ldisc_send (buf, len);

                if (len > 0)
                    show_mouseptr(0);
	    }
	}
	return 0;
      case WM_IME_CHAR:
	{
	    unsigned char buf[2];

	    buf[1] = wParam;
	    buf[0] = wParam >> 8;
	    ldisc_send (buf, 2);
	}
      case WM_CHAR:
      case WM_SYSCHAR:
	/*
	 * Nevertheless, we are prepared to deal with WM_CHAR
	 * messages, should they crop up. So if someone wants to
	 * post the things to us as part of a macro manoeuvre,
	 * we're ready to cope.
	 */
	{
	    char c = xlat_kbd2tty((unsigned char)wParam);
	    ldisc_send (&c, 1);
	}
	return 0;
    }

    return DefWindowProc (hwnd, message, wParam, lParam);
}

/*
 * Move the system caret. (We maintain one, even though it's
 * invisible, for the benefit of blind people: apparently some
 * helper software tracks the system caret, so we should arrange to
 * have one.)
 */
void sys_cursor(int x, int y) {
    SetCaretPos(x * font_width, y * font_height);
}

/*
 * Draw a line of text in the window, at given character
 * coordinates, in given attributes.
 *
 * We are allowed to fiddle with the contents of `text'.
 */
void do_text (Context ctx, int x, int y, char *text, int len,
	      unsigned long attr, int lattr) {
    COLORREF fg, bg, t;
    int nfg, nbg, nfont;
    HDC hdc = ctx;
    RECT line_box;
    int force_manual_underline = 0;
    int fnt_width = font_width*(1+(lattr!=LATTR_NORM));
    static int *IpDx = 0, IpDxLEN = 0;;

    if (len>IpDxLEN || IpDx[0] != fnt_width) {
	int i;
	if (len>IpDxLEN) {
	    sfree(IpDx);
	    IpDx = smalloc((len+16)*sizeof(int));
	    IpDxLEN = (len+16);
	}
	for(i=0; i<IpDxLEN; i++)
	    IpDx[i] = fnt_width;
    }

    x *= fnt_width;
    y *= font_height;

    if ((attr & ATTR_ACTCURS) && cfg.cursor_type == 0) {
	attr &= (bold_mode == BOLD_COLOURS ? 0x300200 : 0x300300);
	attr ^= ATTR_CUR_XOR;
    }

    nfont = 0;
    if (cfg.vtmode == VT_OEMONLY)
	nfont |= FONT_OEM;

    /*
     * Map high-half characters in order to approximate ISO using
     * OEM character set. No characters are missing if the OEM codepage
     * is CP850.
     */
    if (nfont & FONT_OEM) {
	int i;
	for (i=0; i<len; i++)
	    if (text[i] >= '\xA0' && text[i] <= '\xFF') {
#if 0
		/* This is CP850 ... perfect translation */
		static const char oemhighhalf[] =
		    "\x20\xAD\xBD\x9C\xCF\xBE\xDD\xF5" /* A0-A7 */
		    "\xF9\xB8\xA6\xAE\xAA\xF0\xA9\xEE" /* A8-AF */
		    "\xF8\xF1\xFD\xFC\xEF\xE6\xF4\xFA" /* B0-B7 */
		    "\xF7\xFB\xA7\xAF\xAC\xAB\xF3\xA8" /* B8-BF */
		    "\xB7\xB5\xB6\xC7\x8E\x8F\x92\x80" /* C0-C7 */
		    "\xD4\x90\xD2\xD3\xDE\xD6\xD7\xD8" /* C8-CF */
		    "\xD1\xA5\xE3\xE0\xE2\xE5\x99\x9E" /* D0-D7 */
		    "\x9D\xEB\xE9\xEA\x9A\xED\xE8\xE1" /* D8-DF */
		    "\x85\xA0\x83\xC6\x84\x86\x91\x87" /* E0-E7 */
		    "\x8A\x82\x88\x89\x8D\xA1\x8C\x8B" /* E8-EF */
		    "\xD0\xA4\x95\xA2\x93\xE4\x94\xF6" /* F0-F7 */
		    "\x9B\x97\xA3\x96\x81\xEC\xE7\x98" /* F8-FF */
		    ;
#endif
		/* This is CP437 ... junk translation */
		static const unsigned char oemhighhalf[] = {
		    0x20, 0xad, 0x9b, 0x9c, 0x6f, 0x9d, 0x7c, 0x15,
		    0x22, 0x43, 0xa6, 0xae, 0xaa, 0x2d, 0x52, 0xc4,
		    0xf8, 0xf1, 0xfd, 0x33, 0x27, 0xe6, 0x14, 0xfa,
		    0x2c, 0x31, 0xa7, 0xaf, 0xac, 0xab, 0x2f, 0xa8,
		    0x41, 0x41, 0x41, 0x41, 0x8e, 0x8f, 0x92, 0x80,
		    0x45, 0x90, 0x45, 0x45, 0x49, 0x49, 0x49, 0x49,
		    0x44, 0xa5, 0x4f, 0x4f, 0x4f, 0x4f, 0x99, 0x78,
		    0xed, 0x55, 0x55, 0x55, 0x9a, 0x59, 0x50, 0xe1,
		    0x85, 0xa0, 0x83, 0x61, 0x84, 0x86, 0x91, 0x87,
		    0x8a, 0x82, 0x88, 0x89, 0x8d, 0xa1, 0x8c, 0x8b,
		    0x0b, 0xa4, 0x95, 0xa2, 0x93, 0x6f, 0x94, 0xf6,
		    0xed, 0x97, 0xa3, 0x96, 0x81, 0x79, 0x70, 0x98
		};

		text[i] = oemhighhalf[(unsigned char)text[i] - 0xA0];
	    }
    }

    if (attr & ATTR_LINEDRW) {
	int i;
	/* ISO 8859-1 */
	static const char poorman[] =
	    "*#****\xB0\xB1**+++++-----++++|****\xA3\xB7";

	/* CP437 */
	static const char oemmap_437[] =
	    "\x04\xB1****\xF8\xF1**\xD9\xBF\xDA\xC0\xC5"
	    "\xC4\xC4\xC4\xC4\xC4\xC3\xB4\xC1\xC2\xB3\xF3\xF2\xE3*\x9C\xFA";

	/* CP850 */
	static const char oemmap_850[] =
	    "\x04\xB1****\xF8\xF1**\xD9\xBF\xDA\xC0\xC5"
	    "\xC4\xC4\xC4\xC4\xC4\xC3\xB4\xC1\xC2\xB3****\x9C\xFA";

	/* Poor windows font ... eg: windows courier */
	static const char oemmap[] =
	    "*\xB1****\xF8\xF1**\xD9\xBF\xDA\xC0\xC5"
	    "\xC4\xC4\xC4\xC4\xC4\xC3\xB4\xC1\xC2\xB3****\x9C\xFA";

	/*
	 * Line drawing mapping: map ` thru ~ (0x60 thru 0x7E) to
	 * VT100 line drawing chars; everything else stays normal.
	 *
	 * Actually '_' maps to space too, but that's done before.
	 */
	switch (cfg.vtmode) {
	  case VT_XWINDOWS:
	    for (i=0; i<len; i++)
		if (text[i] >= '\x60' && text[i] <= '\x7E')
		    text[i] += '\x01' - '\x60';
	    break;
	  case VT_OEMANSI:
	    /* Make sure we actually have an OEM font */
	    if (fonts[nfont|FONT_OEM]) { 
	  case VT_OEMONLY:
	        nfont |= FONT_OEM;
	        for (i=0; i<len; i++)
		    if (text[i] >= '\x60' && text[i] <= '\x7E')
		        text[i] = oemmap[(unsigned char)text[i] - 0x60];
	        break;
	    }
	  case VT_POORMAN:
	    for (i=0; i<len; i++)
		if (text[i] >= '\x60' && text[i] <= '\x7E')
		    text[i] = poorman[(unsigned char)text[i] - 0x60];
	    break;
	}
    }

    nfg = 2 * ((attr & ATTR_FGMASK) >> ATTR_FGSHIFT);
    nbg = 2 * ((attr & ATTR_BGMASK) >> ATTR_BGSHIFT);
    if (bold_mode == BOLD_FONT && (attr & ATTR_BOLD))
	nfont |= FONT_BOLD;
    if (und_mode == UND_FONT && (attr & ATTR_UNDER))
	nfont |= FONT_UNDERLINE;
    if (!fonts[nfont]) 
    {
	if (nfont&FONT_UNDERLINE)
	    force_manual_underline = 1;
	/* Don't do the same for manual bold, it could be bad news. */

	nfont &= ~(FONT_BOLD|FONT_UNDERLINE);
    }
    if (font_needs_hand_underlining && (attr & ATTR_UNDER))
        force_manual_underline = 1;
    if (attr & ATTR_REVERSE) {
	t = nfg; nfg = nbg; nbg = t;
    }
    if (bold_mode == BOLD_COLOURS && (attr & ATTR_BOLD))
	nfg++;
    if (bold_mode == BOLD_COLOURS && (attr & ATTR_BLINK))
	nbg++;
    fg = colours[nfg];
    bg = colours[nbg];
    SelectObject (hdc, fonts[nfont]);
    SetTextColor (hdc, fg);
    SetBkColor (hdc, bg);
    SetBkMode (hdc, OPAQUE);
    line_box.left   = x;
    line_box.top    = y;
    line_box.right  = x+fnt_width*len;
    line_box.bottom = y+font_height;
    ExtTextOut (hdc, x, y, ETO_CLIPPED|ETO_OPAQUE, &line_box, text, len, IpDx);
    if (bold_mode == BOLD_SHADOW && (attr & ATTR_BOLD)) {
	SetBkMode (hdc, TRANSPARENT);

       /* GRR: This draws the character outside it's box and can leave
	* 'droppings' even with the clip box! I suppose I could loop it
	* one character at a time ... yuk. 
	* 
	* Or ... I could do a test print with "W", and use +1 or -1 for this
	* shift depending on if the leftmost column is blank...
	*/
        ExtTextOut (hdc, x-1, y, ETO_CLIPPED, &line_box, text, len, IpDx);
    }
    if (force_manual_underline || 
	    (und_mode == UND_LINE && (attr & ATTR_UNDER))) {
        HPEN oldpen;
	oldpen = SelectObject (hdc, CreatePen(PS_SOLID, 0, fg));
	MoveToEx (hdc, x, y+descent, NULL);
	LineTo (hdc, x+len*fnt_width, y+descent);
        oldpen = SelectObject (hdc, oldpen);
        DeleteObject (oldpen);
    }
    if ((attr & ATTR_PASCURS) && cfg.cursor_type == 0) {
	POINT pts[5];
        HPEN oldpen;
	pts[0].x = pts[1].x = pts[4].x = x;
	pts[2].x = pts[3].x = x+fnt_width-1;
	pts[0].y = pts[3].y = pts[4].y = y;
	pts[1].y = pts[2].y = y+font_height-1;
	oldpen = SelectObject (hdc, CreatePen(PS_SOLID, 0, colours[23]));
	Polyline (hdc, pts, 5);
        oldpen = SelectObject (hdc, oldpen);
        DeleteObject (oldpen);
    }
    if ((attr & (ATTR_ACTCURS | ATTR_PASCURS)) && cfg.cursor_type != 0) {
        int startx, starty, dx, dy, length, i;
	if (cfg.cursor_type == 1) {
            startx = x; starty = y+descent;
            dx = 1; dy = 0; length = fnt_width;
        } else {
	    int xadjust = 0;
	    if (attr & ATTR_RIGHTCURS)
		xadjust = fnt_width-1;
            startx = x+xadjust; starty = y;
            dx = 0; dy = 1; length = font_height;
	}
        if (attr & ATTR_ACTCURS) {
            HPEN oldpen;
            oldpen = SelectObject (hdc, CreatePen(PS_SOLID, 0, colours[23]));
            MoveToEx (hdc, startx, starty, NULL);
            LineTo (hdc, startx+dx*length, starty+dy*length);
            oldpen = SelectObject (hdc, oldpen);
            DeleteObject (oldpen);
        } else {
            for (i = 0; i < length; i++) {
                if (i % 2 == 0) {
                    SetPixel(hdc, startx, starty, colours[23]);
                }
                startx += dx; starty += dy;
            }
        }
    }
}

static int check_compose(int first, int second) {

    static char * composetbl[] = {
       "++#", "AA@", "(([", "//\\", "))]", "(-{", "-)}", "/^|", "!!¡", "C/¢",
       "C|¢", "L-£", "L=£", "XO¤", "X0¤", "Y-¥", "Y=¥", "||¦", "SO§", "S!§",
       "S0§", "\"\"¨", "CO©", "C0©", "A_ª", "<<«", ",-¬", "--­", "RO®",
       "-^¯", "0^°", "+-±", "2^²", "3^³", "''´", "/Uµ", "P!¶", ".^·", ",,¸",
       "1^¹", "O_º", ">>»", "14¼", "12½", "34¾", "??¿", "`AÀ", "'AÁ", "^AÂ",
       "~AÃ", "\"AÄ", "*AÅ", "AEÆ", ",CÇ", "`EÈ", "'EÉ", "^EÊ", "\"EË",
       "`IÌ", "'IÍ", "^IÎ", "\"IÏ", "-DÐ", "~NÑ", "`OÒ", "'OÓ", "^OÔ",
       "~OÕ", "\"OÖ", "XX×", "/OØ", "`UÙ", "'UÚ", "^UÛ", "\"UÜ", "'YÝ",
       "HTÞ", "ssß", "`aà", "'aá", "^aâ", "~aã", "\"aä", "*aå", "aeæ", ",cç",
       "`eè", "'eé", "^eê", "\"eë", "`iì", "'ií", "^iî", "\"iï", "-dð", "~nñ",
       "`oò", "'oó", "^oô", "~oõ", "\"oö", ":-÷", "o/ø", "`uù", "'uú", "^uû",
       "\"uü", "'yý", "htþ", "\"yÿ",
    0};

    char ** c;
    static int recurse = 0;
    int nc = -1;

    for(c=composetbl; *c; c++) {
	if( (*c)[0] == first && (*c)[1] == second)
	{
	    return (*c)[2] & 0xFF;
	}
    }

    if(recurse==0)
    {
	recurse=1;
	nc = check_compose(second, first);
	if(nc == -1)
	    nc = check_compose(toupper(first), toupper(second));
	if(nc == -1)
	    nc = check_compose(toupper(second), toupper(first));
	recurse=0;
    }
    return nc;
}


/*
 * Translate a WM_(SYS)?KEY(UP|DOWN) message into a string of ASCII
 * codes. Returns number of bytes used or zero to drop the message
 * or -1 to forward the message to windows.
 */
static int TranslateKey(UINT message, WPARAM wParam, LPARAM lParam,
			unsigned char *output) {
    BYTE keystate[256];
    int  scan, left_alt = 0, key_down, shift_state;
    int  r, i, code;
    unsigned char * p = output;
    static int alt_state = 0;

    HKL kbd_layout = GetKeyboardLayout(0);

    static WORD keys[3];
    static int compose_char = 0;
    static WPARAM compose_key = 0;
    
    r = GetKeyboardState(keystate);
    if (!r) memset(keystate, 0, sizeof(keystate));
    else
    {
#if 0
       {  /* Tell us all about key events */
	  static BYTE oldstate[256];
	  static int first = 1;
	  static int scan;
	  int ch;
	  if(first) memcpy(oldstate, keystate, sizeof(oldstate));
	  first=0;

	  if ((HIWORD(lParam)&(KF_UP|KF_REPEAT))==KF_REPEAT) {
	     debug(("+"));
	  } else if ((HIWORD(lParam)&KF_UP) && scan==(HIWORD(lParam) & 0xFF) ) {
	     debug((". U"));
	  } else {
	     debug((".\n"));
	     if (wParam >= VK_F1 && wParam <= VK_F20 )
		debug(("K_F%d", wParam+1-VK_F1));
	     else switch(wParam)
	     {
	     case VK_SHIFT:   debug(("SHIFT")); break;
	     case VK_CONTROL: debug(("CTRL")); break;
	     case VK_MENU:    debug(("ALT")); break;
	     default:         debug(("VK_%02x", wParam));
	     }
	     if(message == WM_SYSKEYDOWN || message == WM_SYSKEYUP)
		debug(("*"));
	     debug((", S%02x", scan=(HIWORD(lParam) & 0xFF) ));

	     ch = MapVirtualKeyEx(wParam, 2, kbd_layout);
	     if (ch>=' ' && ch<='~') debug((", '%c'", ch));
	     else if (ch)            debug((", $%02x", ch));

	     if (keys[0]) debug((", KB0=%02x", keys[0]));
	     if (keys[1]) debug((", KB1=%02x", keys[1]));
	     if (keys[2]) debug((", KB2=%02x", keys[2]));

	     if ( (keystate[VK_SHIFT]&0x80)!=0)     debug((", S"));
	     if ( (keystate[VK_CONTROL]&0x80)!=0)   debug((", C"));
	     if ( (HIWORD(lParam)&KF_EXTENDED) )    debug((", E"));
	     if ( (HIWORD(lParam)&KF_UP) )          debug((", U"));
	  }

	  if ((HIWORD(lParam)&(KF_UP|KF_REPEAT))==KF_REPEAT)
	     ;
	  else if ( (HIWORD(lParam)&KF_UP) ) 
	     oldstate[wParam&0xFF] ^= 0x80;
	  else
	     oldstate[wParam&0xFF] ^= 0x81;

	  for(ch=0; ch<256; ch++)
	     if (oldstate[ch] != keystate[ch])
		debug((", M%02x=%02x", ch, keystate[ch]));

	  memcpy(oldstate, keystate, sizeof(oldstate));
       }
#endif

	if (wParam == VK_MENU && (HIWORD(lParam)&KF_EXTENDED)) {
	    keystate[VK_RMENU] = keystate[VK_MENU];
	}


	/* Nastyness with NUMLock - Shift-NUMLock is left alone though */
	if ( (cfg.funky_type == 3 ||
              (cfg.funky_type <= 1 && app_keypad_keys && !cfg.no_applic_k))
	      && wParam==VK_NUMLOCK && !(keystate[VK_SHIFT]&0x80)) {

	    wParam = VK_EXECUTE;

	    /* UnToggle NUMLock */
            if ((HIWORD(lParam)&(KF_UP|KF_REPEAT))==0)
	        keystate[VK_NUMLOCK] ^= 1;
	}

	/* And write back the 'adjusted' state */
	SetKeyboardState (keystate);
    }

    /* Disable Auto repeat if required */
    if (repeat_off && (HIWORD(lParam)&(KF_UP|KF_REPEAT))==KF_REPEAT)
       return 0;

    if ((HIWORD(lParam)&KF_ALTDOWN) && (keystate[VK_RMENU]&0x80) == 0)
	left_alt = 1;

    key_down = ((HIWORD(lParam)&KF_UP)==0);

    /* Make sure Ctrl-ALT is not the same as AltGr for ToAscii unless told. */
    if (left_alt && (keystate[VK_CONTROL]&0x80)) {
	if (cfg.ctrlaltkeys)
	    keystate[VK_MENU] = 0;
	else {
	    keystate[VK_RMENU] = 0x80;
	    left_alt = 0;
	}
    }

    scan = (HIWORD(lParam) & (KF_UP | KF_EXTENDED | 0xFF));
    shift_state = ((keystate[VK_SHIFT]&0x80)!=0)
                + ((keystate[VK_CONTROL]&0x80)!=0)*2;

    /* Note if AltGr was pressed and if it was used as a compose key */
    if (!compose_state) {
	compose_key = -1;
	if (cfg.compose_key) {
	    if (wParam == VK_MENU && (HIWORD(lParam)&KF_EXTENDED))
		compose_key = wParam;
	}
	if (wParam == VK_APPS)
	    compose_key = wParam;
    }

    if (wParam == compose_key)
    {
	if (compose_state == 0 && (HIWORD(lParam)&(KF_UP|KF_REPEAT))==0)
	    compose_state = 1;
	else if (compose_state == 1 && (HIWORD(lParam)&KF_UP))
	    compose_state = 2;
	else
	    compose_state = 0;
    }
    else if (compose_state==1 && wParam != VK_CONTROL)
	compose_state = 0;

    /* 
     * Record that we pressed key so the scroll window can be reset, but
     * be careful to avoid Shift-UP/Down
     */
    if( wParam != VK_SHIFT && wParam != VK_PRIOR && wParam != VK_NEXT ) {
        seen_key_event = 1; 
    }

    /* Make sure we're not pasting */
    if (key_down) term_nopaste();

    if (compose_state>1 && left_alt) compose_state = 0;

    /* Sanitize the number pad if not using a PC NumPad */
    if( left_alt || (app_keypad_keys && !cfg.no_applic_k
                     && cfg.funky_type != 2)
	|| cfg.funky_type == 3 || cfg.nethack_keypad || compose_state )
    {
	if ((HIWORD(lParam)&KF_EXTENDED) == 0)
	{
	    int nParam = 0;
	    switch(wParam)
	    {
	    case VK_INSERT:	nParam = VK_NUMPAD0; break;
	    case VK_END:	nParam = VK_NUMPAD1; break;
	    case VK_DOWN:	nParam = VK_NUMPAD2; break;
	    case VK_NEXT:	nParam = VK_NUMPAD3; break;
	    case VK_LEFT:	nParam = VK_NUMPAD4; break;
	    case VK_CLEAR:	nParam = VK_NUMPAD5; break;
	    case VK_RIGHT:	nParam = VK_NUMPAD6; break;
	    case VK_HOME:	nParam = VK_NUMPAD7; break;
	    case VK_UP:		nParam = VK_NUMPAD8; break;
	    case VK_PRIOR:	nParam = VK_NUMPAD9; break;
	    case VK_DELETE: 	nParam = VK_DECIMAL; break;
	    }
	    if (nParam)
	    {
		if (keystate[VK_NUMLOCK]&1) shift_state |= 1;
		wParam = nParam;
	    }
	}
    }

    /* If a key is pressed and AltGr is not active */
    if (key_down && (keystate[VK_RMENU]&0x80) == 0 && !compose_state)
    {
	/* Okay, prepare for most alts then ...*/
	if (left_alt) *p++ = '\033';

	/* Lets see if it's a pattern we know all about ... */
	if (wParam == VK_PRIOR && shift_state == 1) {
            SendMessage (hwnd, WM_VSCROLL, SB_PAGEUP, 0);
            return 0;
	}
	if (wParam == VK_NEXT && shift_state == 1) {
            SendMessage (hwnd, WM_VSCROLL, SB_PAGEDOWN, 0);
            return 0;
	}
        if (wParam == VK_INSERT && shift_state == 1) {
            term_mouse (MB_PASTE, MA_CLICK, 0, 0);
            term_mouse (MB_PASTE, MA_RELEASE, 0, 0);
            return 0;
        }
	if (left_alt && wParam == VK_F4 && cfg.alt_f4) {
            return -1;
	}
	if (left_alt && wParam == VK_SPACE && cfg.alt_space) {
	    alt_state = 0;
            PostMessage(hwnd, WM_CHAR, ' ', 0);
            SendMessage (hwnd, WM_SYSCOMMAND, SC_KEYMENU, 0);
            return -1;
	}
	/* Control-Numlock for app-keypad mode switch */
	if (wParam == VK_PAUSE && shift_state == 2) {
	    app_keypad_keys ^= 1;
	    return 0;
	}

	/* Nethack keypad */
	if (cfg.nethack_keypad && !left_alt) {
	   switch(wParam) {
	       case VK_NUMPAD1: *p++ = shift_state ? 'B': 'b'; return p-output;
	       case VK_NUMPAD2: *p++ = shift_state ? 'J': 'j'; return p-output;
	       case VK_NUMPAD3: *p++ = shift_state ? 'N': 'n'; return p-output;
	       case VK_NUMPAD4: *p++ = shift_state ? 'H': 'h'; return p-output;
	       case VK_NUMPAD5: *p++ = shift_state ? '.': '.'; return p-output;
	       case VK_NUMPAD6: *p++ = shift_state ? 'L': 'l'; return p-output;
	       case VK_NUMPAD7: *p++ = shift_state ? 'Y': 'y'; return p-output;
	       case VK_NUMPAD8: *p++ = shift_state ? 'K': 'k'; return p-output;
	       case VK_NUMPAD9: *p++ = shift_state ? 'U': 'u'; return p-output;
	   }
	}

	/* Application Keypad */
	if (!left_alt) {
	   int xkey = 0;

	   if ( cfg.funky_type == 3 ||
	      ( cfg.funky_type <= 1 &&
               app_keypad_keys && !cfg.no_applic_k)) switch(wParam) {
	       case VK_EXECUTE: xkey = 'P'; break;
	       case VK_DIVIDE:  xkey = 'Q'; break;
	       case VK_MULTIPLY:xkey = 'R'; break;
	       case VK_SUBTRACT:xkey = 'S'; break;
	   }
	   if(app_keypad_keys && !cfg.no_applic_k) switch(wParam) {
	       case VK_NUMPAD0: xkey = 'p'; break;
	       case VK_NUMPAD1: xkey = 'q'; break;
	       case VK_NUMPAD2: xkey = 'r'; break;
	       case VK_NUMPAD3: xkey = 's'; break;
	       case VK_NUMPAD4: xkey = 't'; break;
	       case VK_NUMPAD5: xkey = 'u'; break;
	       case VK_NUMPAD6: xkey = 'v'; break;
	       case VK_NUMPAD7: xkey = 'w'; break;
	       case VK_NUMPAD8: xkey = 'x'; break;
	       case VK_NUMPAD9: xkey = 'y'; break;

	       case VK_DECIMAL: xkey = 'n'; break;
	       case VK_ADD:     if(cfg.funky_type==2) { 
				    if(shift_state) xkey = 'l';
				    else            xkey = 'k';
				} else if(shift_state)  xkey = 'm'; 
				  else                  xkey = 'l';
				break;

	       case VK_DIVIDE:  if(cfg.funky_type==2) xkey = 'o'; break;
	       case VK_MULTIPLY:if(cfg.funky_type==2) xkey = 'j'; break;
	       case VK_SUBTRACT:if(cfg.funky_type==2) xkey = 'm'; break;

	       case VK_RETURN:
				if (HIWORD(lParam)&KF_EXTENDED)
				    xkey = 'M';
				break;
	    }
	    if(xkey)
	    {
		if (vt52_mode)
		{
		    if (xkey>='P' && xkey<='S')
			p += sprintf((char *)p, "\x1B%c", xkey); 
		    else
			p += sprintf((char *)p, "\x1B?%c", xkey); 
		}
		else 
		    p += sprintf((char *)p, "\x1BO%c", xkey); 
	        return p - output;
	    }
	}

	if (wParam == VK_BACK && shift_state == 0 )	/* Backspace */
	{
	    *p++ = (cfg.bksp_is_delete ? 0x7F : 0x08);
	    return p-output;
	}
	if (wParam == VK_TAB && shift_state == 1 )	/* Shift tab */
	{
	    *p++ = 0x1B; *p++ = '['; *p++ = 'Z'; return p - output;
	}
	if (wParam == VK_SPACE && shift_state == 2 )	/* Ctrl-Space */
	{
	    *p++ = 0; return p - output;
	}
	if (wParam == VK_SPACE && shift_state == 3 )	/* Ctrl-Shift-Space */
	{
	    *p++ = 160; return p - output;
	}
	if (wParam == VK_CANCEL && shift_state == 2 )	/* Ctrl-Break */
	{
	    *p++ = 3; return p - output;
	}
	if (wParam == VK_PAUSE)				/* Break/Pause */
	{
	    *p++ = 26; *p++ = 0; return -2;
	}
	/* Control-2 to Control-8 are special */
	if (shift_state == 2 && wParam >= '2' && wParam <= '8')
	{
	    *p++ = "\000\033\034\035\036\037\177"[wParam-'2'];
	    return p - output;
	}
	if (shift_state == 2 && wParam == 0xBD) {
	    *p++ = 0x1F;
	    return p - output;
	}
	if (shift_state == 2 && wParam == 0xDF) {
	    *p++ = 0x1C;
	    return p - output;
	}
	if (shift_state == 0 && wParam == VK_RETURN && cr_lf_return) {
	    *p++ = '\r'; *p++ = '\n';
	    return p - output;
	}

	/*
	 * Next, all the keys that do tilde codes. (ESC '[' nn '~',
	 * for integer decimal nn.)
	 *
	 * We also deal with the weird ones here. Linux VCs replace F1
	 * to F5 by ESC [ [ A to ESC [ [ E. rxvt doesn't do _that_, but
	 * does replace Home and End (1~ and 4~) by ESC [ H and ESC O w
	 * respectively.
	 */
	code = 0;
	switch (wParam) {
	  case VK_F1: code = (keystate[VK_SHIFT] & 0x80 ? 23 : 11); break;
	  case VK_F2: code = (keystate[VK_SHIFT] & 0x80 ? 24 : 12); break;
	  case VK_F3: code = (keystate[VK_SHIFT] & 0x80 ? 25 : 13); break;
	  case VK_F4: code = (keystate[VK_SHIFT] & 0x80 ? 26 : 14); break;
	  case VK_F5: code = (keystate[VK_SHIFT] & 0x80 ? 28 : 15); break;
	  case VK_F6: code = (keystate[VK_SHIFT] & 0x80 ? 29 : 17); break;
	  case VK_F7: code = (keystate[VK_SHIFT] & 0x80 ? 31 : 18); break;
	  case VK_F8: code = (keystate[VK_SHIFT] & 0x80 ? 32 : 19); break;
	  case VK_F9: code = (keystate[VK_SHIFT] & 0x80 ? 33 : 20); break;
	  case VK_F10: code = (keystate[VK_SHIFT] & 0x80 ? 34 : 21); break;
	  case VK_F11: code = 23; break;
	  case VK_F12: code = 24; break;
	  case VK_F13: code = 25; break;
	  case VK_F14: code = 26; break;
	  case VK_F15: code = 28; break;
	  case VK_F16: code = 29; break;
	  case VK_F17: code = 31; break;
	  case VK_F18: code = 32; break;
	  case VK_F19: code = 33; break;
	  case VK_F20: code = 34; break;
	  case VK_HOME: code = 1; break;
	  case VK_INSERT: code = 2; break;
	  case VK_DELETE: code = 3; break;
	  case VK_END: code = 4; break;
	  case VK_PRIOR: code = 5; break;
	  case VK_NEXT: code = 6; break;
	}
	/* Reorder edit keys to physical order */
	if (cfg.funky_type == 3 && code <= 6 ) code = "\0\2\1\4\5\3\6"[code];

	if (cfg.funky_type == 1 && code >= 11 && code <= 15) {
	    p += sprintf((char *)p, "\x1B[[%c", code + 'A' - 11);
	    return p - output;
	}
	if (cfg.funky_type == 2 && code >= 11 && code <= 14) {
	    if (vt52_mode)
	        p += sprintf((char *)p, "\x1B%c", code + 'P' - 11);
	    else
	        p += sprintf((char *)p, "\x1BO%c", code + 'P' - 11);
	    return p - output;
	}
	if (cfg.rxvt_homeend && (code == 1 || code == 4)) {
	    p += sprintf((char *)p, code == 1 ? "\x1B[H" : "\x1BOw");
	    return p - output;
	}
	if (code) {
	    p += sprintf((char *)p, "\x1B[%d~", code);
	    return p - output;
	}

	/*
	 * Now the remaining keys (arrows and Keypad 5. Keypad 5 for
	 * some reason seems to send VK_CLEAR to Windows...).
	 */
	{
	    char xkey = 0;
	    switch (wParam) {
	        case VK_UP: 	xkey = 'A'; break;
	        case VK_DOWN: 	xkey = 'B'; break;
	        case VK_RIGHT: 	xkey = 'C'; break;
	        case VK_LEFT: 	xkey = 'D'; break;
	        case VK_CLEAR: 	xkey = 'G'; break;
	    }
	    if (xkey)
	    {
		if (vt52_mode)
		    p += sprintf((char *)p, "\x1B%c", xkey);
		else if (app_cursor_keys && !cfg.no_applic_c)
		    p += sprintf((char *)p, "\x1BO%c", xkey);
		else
		    p += sprintf((char *)p, "\x1B[%c", xkey);
		return p - output;
	    }
	}

	/*
	 * Finally, deal with Return ourselves. (Win95 seems to
	 * foul it up when Alt is pressed, for some reason.)
	 */
	if (wParam == VK_RETURN)       /* Return */
	{
	    *p++ = 0x0D;
	    return p-output;
	}
    }

    /* Okay we've done everything interesting; let windows deal with 
     * the boring stuff */
    {
	BOOL capsOn=keystate[VK_CAPITAL] !=0;

	/* helg: clear CAPS LOCK state if caps lock switches to cyrillic */
	if(cfg.xlat_capslockcyr)
	    keystate[VK_CAPITAL] = 0;

	r = ToAsciiEx(wParam, scan, keystate, keys, 0, kbd_layout);
	if(r>0)
	{
	    p = output;
	    for(i=0; i<r; i++)
	    {
		unsigned char ch = (unsigned char)keys[i];

		if (compose_state==2 && (ch&0x80) == 0 && ch>' ') {
		    compose_char = ch;
		    compose_state ++;
		    continue;
		}
		if (compose_state==3 && (ch&0x80) == 0 && ch>' ') {
		    int nc;
		    compose_state = 0;

		    if ((nc=check_compose(compose_char,ch)) == -1)
		    {
			MessageBeep(MB_ICONHAND);
			return 0;
		    }
		    *p++ = xlat_kbd2tty((unsigned char)nc);
		    return p-output;
		}

		compose_state = 0;

		if( left_alt && key_down ) *p++ = '\033';
		if (!key_down)
		    *p++ = ch;
		else
		{
		    if(capsOn)
			ch = xlat_latkbd2win(ch);
	            *p++ = xlat_kbd2tty(ch);
		}
	    }

	    /* This is so the ALT-Numpad and dead keys work correctly. */
	    keys[0] = 0;

	    return p-output;
	}
    }

    /* ALT alone may or may not want to bring up the System menu */
    if (wParam == VK_MENU) {
        if (cfg.alt_only) {
            if (message == WM_SYSKEYDOWN)
                alt_state = 1;
            else if (message == WM_SYSKEYUP && alt_state)
                PostMessage(hwnd, WM_CHAR, ' ', 0);
            if (message == WM_SYSKEYUP)
                alt_state = 0;
        } else
	    return 0;
    }
    else alt_state = 0;

    return -1;
}

void set_title (char *title) {
    sfree (window_name);
    window_name = smalloc(1+strlen(title));
    strcpy (window_name, title);
    if (cfg.win_name_always || !IsIconic(hwnd))
	SetWindowText (hwnd, title);
}

void set_icon (char *title) {
    sfree (icon_name);
    icon_name = smalloc(1+strlen(title));
    strcpy (icon_name, title);
    if (!cfg.win_name_always && IsIconic(hwnd))
	SetWindowText (hwnd, title);
}

void set_sbar (int total, int start, int page) {
    SCROLLINFO si;

    if (!cfg.scrollbar) return;

    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = total - 1;
    si.nPage = page;
    si.nPos = start;
    if (hwnd)
        SetScrollInfo (hwnd, SB_VERT, &si, TRUE);
}

Context get_ctx(void) {
    HDC hdc;
    if (hwnd) {
	hdc = GetDC (hwnd);
	if (hdc && pal)
	    SelectPalette (hdc, pal, FALSE);
	return hdc;
    } else
	return NULL;
}

void free_ctx (Context ctx) {
    SelectPalette (ctx, GetStockObject (DEFAULT_PALETTE), FALSE);
    ReleaseDC (hwnd, ctx);
}

static void real_palette_set (int n, int r, int g, int b) {
    if (pal) {
	logpal->palPalEntry[n].peRed = r;
	logpal->palPalEntry[n].peGreen = g;
	logpal->palPalEntry[n].peBlue = b;
	logpal->palPalEntry[n].peFlags = PC_NOCOLLAPSE;
	colours[n] = PALETTERGB(r, g, b);
	SetPaletteEntries (pal, 0, NCOLOURS, logpal->palPalEntry);
    } else
	colours[n] = RGB(r, g, b);
}

void palette_set (int n, int r, int g, int b) {
    static const int first[21] = {
	0, 2, 4, 6, 8, 10, 12, 14,
	1, 3, 5, 7, 9, 11, 13, 15,
	16, 17, 18, 20, 22
    };
    real_palette_set (first[n], r, g, b);
    if (first[n] >= 18)
	real_palette_set (first[n]+1, r, g, b);
    if (pal) {
	HDC hdc = get_ctx();
	UnrealizeObject (pal);
	RealizePalette (hdc);
	free_ctx (hdc);
    }
}

void palette_reset (void) {
    int i;

    for (i = 0; i < NCOLOURS; i++) {
	if (pal) {
	    logpal->palPalEntry[i].peRed = defpal[i].rgbtRed;
	    logpal->palPalEntry[i].peGreen = defpal[i].rgbtGreen;
	    logpal->palPalEntry[i].peBlue = defpal[i].rgbtBlue;
	    logpal->palPalEntry[i].peFlags = 0;
	    colours[i] = PALETTERGB(defpal[i].rgbtRed,
				    defpal[i].rgbtGreen,
				    defpal[i].rgbtBlue);
	} else
	    colours[i] = RGB(defpal[i].rgbtRed,
			     defpal[i].rgbtGreen,
			     defpal[i].rgbtBlue);
    }

    if (pal) {
	HDC hdc;
	SetPaletteEntries (pal, 0, NCOLOURS, logpal->palPalEntry);
	hdc = get_ctx();
	RealizePalette (hdc);
	free_ctx (hdc);
    }
}

void write_clip (void *data, int len, int must_deselect) {
    HGLOBAL clipdata;
    void *lock;

    clipdata = GlobalAlloc (GMEM_DDESHARE | GMEM_MOVEABLE, len + 1);
    if (!clipdata)
	return;
    lock = GlobalLock (clipdata);
    if (!lock)
	return;
    memcpy (lock, data, len);
    ((unsigned char *) lock) [len] = 0;
    GlobalUnlock (clipdata);

    if (!must_deselect)
        SendMessage (hwnd, WM_IGNORE_CLIP, TRUE, 0);

    if (OpenClipboard (hwnd)) {
	EmptyClipboard();
	SetClipboardData (CF_TEXT, clipdata);
	CloseClipboard();
    } else
	GlobalFree (clipdata);

    if (!must_deselect)
        SendMessage (hwnd, WM_IGNORE_CLIP, FALSE, 0);
}

void get_clip (void **p, int *len) {
    static HGLOBAL clipdata = NULL;

    if (!p) {
	if (clipdata)
	    GlobalUnlock (clipdata);
	clipdata = NULL;
	return;
    } else {
	if (OpenClipboard (NULL)) {
	    clipdata = GetClipboardData (CF_TEXT);
	    CloseClipboard();
	    if (clipdata) {
		*p = GlobalLock (clipdata);
		if (*p) {
		    *len = strlen(*p);
		    return;
		}
	    }
	}
    }

    *p = NULL;
    *len = 0;
}

/*
 * Move `lines' lines from position `from' to position `to' in the
 * window.
 */
void optimised_move (int to, int from, int lines) {
    RECT r;
    int min, max;

    min = (to < from ? to : from);
    max = to + from - min;

    r.left = 0; r.right = cols * font_width;
    r.top = min * font_height; r.bottom = (max+lines) * font_height;
    ScrollWindow (hwnd, 0, (to - from) * font_height, &r, &r);
}

/*
 * Print a message box and perform a fatal exit.
 */
void fatalbox(char *fmt, ...) {
    va_list ap;
    char stuff[200];

    va_start(ap, fmt);
    vsprintf(stuff, fmt, ap);
    va_end(ap);
    MessageBox(hwnd, stuff, "PuTTY Fatal Error", MB_ICONERROR | MB_OK);
    exit(1);
}

/*
 * Beep.
 */
void beep(int errorbeep) {
    static long last_beep = 0;
    long now, beep_diff;

    now = GetTickCount();
    beep_diff = now-last_beep;

    /* Make sure we only respond to one beep per packet or so */
    if (beep_diff>=0 && beep_diff<50)
        return;

    if(errorbeep)
       MessageBeep(MB_ICONHAND);
    else
       MessageBeep(MB_OK);

    last_beep = GetTickCount();
}
