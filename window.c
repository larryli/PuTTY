#include <windows.h>
#include <imm.h>
#include <commctrl.h>
#include <mmsystem.h>
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

#define PUTTY_DO_GLOBALS	       /* actually _define_ globals */
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

#define IDM_SESSLGP   0x0250	       /* log type printable */
#define IDM_SESSLGA   0x0260	       /* log type all chars */
#define IDM_SESSLGE   0x0270	       /* log end */
#define IDM_SAVED_MIN 0x1000
#define IDM_SAVED_MAX 0x2000

#define WM_IGNORE_SIZE (WM_XUSER + 1)
#define WM_IGNORE_CLIP (WM_XUSER + 2)

/* Needed for Chinese support and apparently not always defined. */
#ifndef VK_PROCESSKEY
#define VK_PROCESSKEY 0xE5
#endif

static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static int TranslateKey(UINT message, WPARAM wParam, LPARAM lParam,
			unsigned char *output);
static void cfgtopalette(void);
static void init_palette(void);
static void init_fonts(int);
static void another_font(int);
static void deinit_fonts(void);

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
#define FONT_WIDE	0x04
#define FONT_HIGH	0x08
#define FONT_NARROW	0x10
#define FONT_OEM 	0x20
#define FONT_OEMBOLD 	0x21
#define FONT_OEMUND 	0x22
#define FONT_OEMBOLDUND 0x23
#define FONT_MSGOTHIC 	0x40
#define FONT_MINGLIU 	0x60
#define FONT_GULIMCHE 	0x80
#define FONT_MAXNO 	0x8F
#define FONT_SHIFT	5
static HFONT fonts[FONT_MAXNO];
static int fontflag[FONT_MAXNO];
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

/* this allows xterm-style mouse handling. */
static int send_raw_mouse = 0;
static int wheel_accumulator = 0;

static char *window_name, *icon_name;

static int compose_state = 0;

/* Dummy routine, only required in plink. */
void ldisc_update(int echo, int edit)
{
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
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
	while (*p && isspace(*p))
	    p++;

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
		tolower(p[1]) == 's' && tolower(p[2]) == 'h') {
		default_protocol = cfg.protocol = PROT_SSH;
		default_port = cfg.port = 22;
	    } else if (q == p + 7 &&
		       tolower(p[0]) == 'c' &&
		       tolower(p[1]) == 'l' &&
		       tolower(p[2]) == 'e' &&
		       tolower(p[3]) == 'a' &&
		       tolower(p[4]) == 'n' &&
		       tolower(p[5]) == 'u' && tolower(p[6]) == 'p') {
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
	    while (i > 1 && isspace(p[i - 1]))
		i--;
	    p[i] = '\0';
	    do_defaults(p + 1, &cfg);
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
	    if (sscanf(p + 1, "%p", &filemap) == 1 &&
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
		while (*p && *p != ':' && *p != '/')
		    p++;
		c = *p;
		if (*p)
		    *p++ = '\0';
		if (c == ':')
		    cfg.port = atoi(p);
		else
		    cfg.port = -1;
		strncpy(cfg.host, q, sizeof(cfg.host) - 1);
		cfg.host[sizeof(cfg.host) - 1] = '\0';
	    } else {
		while (*p && !isspace(*p))
		    p++;
		if (*p)
		    *p++ = '\0';
		strncpy(cfg.host, q, sizeof(cfg.host) - 1);
		cfg.host[sizeof(cfg.host) - 1] = '\0';
		while (*p && isspace(*p))
		    p++;
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
		if (atsign - cfg.host < sizeof cfg.username) {
		    strncpy(cfg.username, cfg.host, atsign - cfg.host);
		    cfg.username[atsign - cfg.host] = '\0';
		}
		memmove(cfg.host, atsign + 1, 1 + strlen(atsign + 1));
	    }
	}

	/*
	 * Trim a colon suffix off the hostname if it's there.
	 */
	cfg.host[strcspn(cfg.host, ":")] = '\0';
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
		   "PuTTY Internal Error", MB_OK | MB_ICONEXCLAMATION);
	WSACleanup();
	return 1;
    }

    if (!prev) {
	wndclass.style = 0;
	wndclass.lpfnWndProc = WndProc;
	wndclass.cbClsExtra = 0;
	wndclass.cbWndExtra = 0;
	wndclass.hInstance = inst;
	wndclass.hIcon = LoadIcon(inst, MAKEINTRESOURCE(IDI_MAINICON));
	wndclass.hCursor = LoadCursor(NULL, IDC_IBEAM);
	wndclass.hbrBackground = GetStockObject(BLACK_BRUSH);
	wndclass.lpszMenuName = NULL;
	wndclass.lpszClassName = appname;

	RegisterClass(&wndclass);
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
    term_size(cfg.height, cfg.width, cfg.savelines);
    guess_width = extra_width + font_width * cols;
    guess_height = extra_height + font_height * rows;
    {
	RECT r;
	HWND w = GetDesktopWindow();
	GetWindowRect(w, &r);
	if (guess_width > r.right - r.left)
	    guess_width = r.right - r.left;
	if (guess_height > r.bottom - r.top)
	    guess_height = r.bottom - r.top;
    }

    {
	int winmode = WS_OVERLAPPEDWINDOW | WS_VSCROLL;
	int exwinmode = 0;
	if (!cfg.scrollbar)
	    winmode &= ~(WS_VSCROLL);
	if (cfg.locksize)
	    winmode &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
	if (cfg.alwaysontop)
	    exwinmode |= WS_EX_TOPMOST;
	if (cfg.sunken_edge)
	    exwinmode |= WS_EX_CLIENTEDGE;
	hwnd = CreateWindowEx(exwinmode, appname, appname,
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
	GetWindowRect(hwnd, &wr);
	GetClientRect(hwnd, &cr);
	extra_width = wr.right - wr.left - cr.right + cr.left;
	extra_height = wr.bottom - wr.top - cr.bottom + cr.top;
    }

    /*
     * Resize the window, now we know what size we _really_ want it
     * to be.
     */
    guess_width = extra_width + font_width * cols;
    guess_height = extra_height + font_height * rows;
    SendMessage(hwnd, WM_IGNORE_SIZE, 0, 0);
    SetWindowPos(hwnd, NULL, 0, 0, guess_width, guess_height,
		 SWP_NOMOVE | SWP_NOREDRAW | SWP_NOZORDER);

    /*
     * Set up a caret bitmap, with no content.
     */
    {
	char *bits;
	int size = (font_width + 15) / 16 * 2 * font_height;
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
	si.nMax = rows - 1;
	si.nPage = rows;
	si.nPos = 0;
	SetScrollInfo(hwnd, SB_VERT, &si, FALSE);
    }

    /*
     * Start up the telnet connection.
     */
    {
	char *error;
	char msg[1024], *title;
	char *realhost;

	error = back->init(cfg.host, cfg.port, &realhost);
	if (error) {
	    sprintf(msg, "Unable to open connection to\n"
		    "%.800s\n" "%s", cfg.host, error);
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
	sfree(realhost);
	set_title(title);
	set_icon(title);
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
    lastbtn = MBT_NOTHING;
    dbltime = GetDoubleClickTime();

    /*
     * Set up the session-control options on the system menu.
     */
    {
	HMENU m = GetSystemMenu(hwnd, FALSE);
	HMENU p, s;
	int i;

	AppendMenu(m, MF_SEPARATOR, 0, 0);
	if (cfg.protocol == PROT_TELNET) {
	    p = CreateMenu();
	    AppendMenu(p, MF_ENABLED, IDM_TEL_AYT, "Are You There");
	    AppendMenu(p, MF_ENABLED, IDM_TEL_BRK, "Break");
	    AppendMenu(p, MF_ENABLED, IDM_TEL_SYNCH, "Synch");
	    AppendMenu(p, MF_SEPARATOR, 0, 0);
	    AppendMenu(p, MF_ENABLED, IDM_TEL_EC, "Erase Character");
	    AppendMenu(p, MF_ENABLED, IDM_TEL_EL, "Erase Line");
	    AppendMenu(p, MF_ENABLED, IDM_TEL_GA, "Go Ahead");
	    AppendMenu(p, MF_ENABLED, IDM_TEL_NOP, "No Operation");
	    AppendMenu(p, MF_SEPARATOR, 0, 0);
	    AppendMenu(p, MF_ENABLED, IDM_TEL_ABORT, "Abort Process");
	    AppendMenu(p, MF_ENABLED, IDM_TEL_AO, "Abort Output");
	    AppendMenu(p, MF_ENABLED, IDM_TEL_IP, "Interrupt Process");
	    AppendMenu(p, MF_ENABLED, IDM_TEL_SUSP, "Suspend Process");
	    AppendMenu(p, MF_SEPARATOR, 0, 0);
	    AppendMenu(p, MF_ENABLED, IDM_TEL_EOR, "End Of Record");
	    AppendMenu(p, MF_ENABLED, IDM_TEL_EOF, "End Of File");
	    AppendMenu(m, MF_POPUP | MF_ENABLED, (UINT) p,
		       "Telnet Command");
	    AppendMenu(m, MF_SEPARATOR, 0, 0);
	}
	AppendMenu(m, MF_ENABLED, IDM_SHOWLOG, "&Event Log");
	AppendMenu(m, MF_SEPARATOR, 0, 0);
	AppendMenu(m, MF_ENABLED, IDM_NEWSESS, "Ne&w Session...");
	AppendMenu(m, MF_ENABLED, IDM_DUPSESS, "&Duplicate Session");
	s = CreateMenu();
	get_sesslist(TRUE);
	for (i = 1; i < ((nsessions < 256) ? nsessions : 256); i++)
	    AppendMenu(s, MF_ENABLED, IDM_SAVED_MIN + (16 * i),
		       sessions[i]);
	AppendMenu(m, MF_POPUP | MF_ENABLED, (UINT) s, "Sa&ved Sessions");
	AppendMenu(m, MF_ENABLED, IDM_RECONF, "Chan&ge Settings...");
	AppendMenu(m, MF_SEPARATOR, 0, 0);
	AppendMenu(m, MF_ENABLED, IDM_COPYALL, "C&opy All to Clipboard");
	AppendMenu(m, MF_ENABLED, IDM_CLRSB, "C&lear Scrollback");
	AppendMenu(m, MF_ENABLED, IDM_RESET, "Rese&t Terminal");
	AppendMenu(m, MF_SEPARATOR, 0, 0);
	AppendMenu(m, MF_ENABLED, IDM_ABOUT, "&About PuTTY");
    }

    /*
     * Finally show the window!
     */
    ShowWindow(hwnd, show);

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
    UpdateWindow(hwnd);

    if (GetMessage(&msg, NULL, 0, 0) == 1) {
	int timer_id = 0, long_timer = 0;

	while (msg.message != WM_QUIT) {
	    /* Sometimes DispatchMessage calls routines that use their own
	     * GetMessage loop, setup this timer so we get some control back.
	     *
	     * Also call term_update() from the timer so that if the host
	     * is sending data flat out we still do redraws.
	     */
	    if (timer_id && long_timer) {
		KillTimer(hwnd, timer_id);
		long_timer = timer_id = 0;
	    }
	    if (!timer_id)
		timer_id = SetTimer(hwnd, 1, 20, NULL);
	    if (!(IsWindow(logbox) && IsDialogMessage(logbox, &msg)))
		DispatchMessage(&msg);

	    /* Make sure we blink everything that needs it. */
	    term_blink(0);

	    /* Send the paste buffer if there's anything to send */
	    term_paste();

	    /* If there's nothing new in the queue then we can do everything
	     * we've delayed, reading the socket, writing, and repainting
	     * the window.
	     */
	    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		continue;

	    if (pending_netevent) {
		enact_pending_netevent();

		/* Force the cursor blink on */
		term_blink(1);

		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
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
	    if (in_vbell)
		/* Hmm, term_update didn't want to do an update too soon ... */
		timer_id = SetTimer(hwnd, 1, 50, NULL);
	    else if (!has_focus)
		timer_id = SetTimer(hwnd, 1, 2000, NULL);
	    else
		timer_id = SetTimer(hwnd, 1, 100, NULL);
	    long_timer = 1;

	    /* There's no point rescanning everything in the message queue
	     * so we do an apparently unnecessary wait here
	     */
	    WaitMessage();
	    if (GetMessage(&msg, NULL, 0, 0) != 1)
		break;
	}
    }

    /*
     * Clean up.
     */
    deinit_fonts();
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
char *do_select(SOCKET skt, int startup)
{
    int msg, events;
    if (startup) {
	msg = WM_NETEVENT;
	events = FD_READ | FD_WRITE | FD_OOB | FD_CLOSE;
    } else {
	msg = events = 0;
    }
    if (!hwnd)
	return "do_select(): internal error (hwnd==NULL)";
    if (WSAAsyncSelect(skt, hwnd, msg, events) == SOCKET_ERROR) {
	switch (WSAGetLastError()) {
	  case WSAENETDOWN:
	    return "Network is down";
	  default:
	    return "WSAAsyncSelect(): unknown error";
	}
    }
    return NULL;
}

/*
 * set or clear the "raw mouse message" mode
 */
void set_raw_mouse_mode(int activate)
{
    send_raw_mouse = activate;
    SetCursor(LoadCursor(NULL, activate ? IDC_ARROW : IDC_IBEAM));
}

/*
 * Print a message box and close the connection.
 */
void connection_fatal(char *fmt, ...)
{
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
	SetWindowText(hwnd, "PuTTY (inactive)");
    }
}

/*
 * Actually do the job requested by a WM_NETEVENT
 */
static void enact_pending_netevent(void)
{
    static int reentering = 0;
    extern int select_result(WPARAM, LPARAM);
    int ret;

    if (reentering)
	return;			       /* don't unpend the pending */

    pending_netevent = FALSE;

    reentering = 1;
    ret = select_result(pend_netevent_wParam, pend_netevent_lParam);
    reentering = 0;

    if (ret == 0 && !session_closed) {
	/* Abnormal exits will already have set session_closed and taken
	 * appropriate action. */
	if (cfg.close_on_exit == COE_ALWAYS ||
	    cfg.close_on_exit == COE_NORMAL) PostQuitMessage(0);
	else {
	    session_closed = TRUE;
	    SetWindowText(hwnd, "PuTTY (inactive)");
	    MessageBox(hwnd, "Connection closed by remote host",
		       "PuTTY", MB_OK | MB_ICONINFORMATION);
	}
    }
}

/*
 * Copy the colour palette from the configuration data into defpal.
 * This is non-trivial because the colour indices are different.
 */
static void cfgtopalette(void)
{
    int i;
    static const int ww[] = {
	6, 7, 8, 9, 10, 11, 12, 13,
	14, 15, 16, 17, 18, 19, 20, 21,
	0, 1, 2, 3, 4, 4, 5, 5
    };

    for (i = 0; i < 24; i++) {
	int w = ww[i];
	defpal[i].rgbtRed = cfg.colours[w][0];
	defpal[i].rgbtGreen = cfg.colours[w][1];
	defpal[i].rgbtBlue = cfg.colours[w][2];
    }
}

/*
 * Set up the colour palette.
 */
static void init_palette(void)
{
    int i;
    HDC hdc = GetDC(hwnd);
    if (hdc) {
	if (cfg.try_palette && GetDeviceCaps(hdc, RASTERCAPS) & RC_PALETTE) {
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
	    pal = CreatePalette(logpal);
	    if (pal) {
		SelectPalette(hdc, pal, FALSE);
		RealizePalette(hdc);
		SelectPalette(hdc, GetStockObject(DEFAULT_PALETTE), FALSE);
	    }
	}
	ReleaseDC(hwnd, hdc);
    }
    if (pal)
	for (i = 0; i < NCOLOURS; i++)
	    colours[i] = PALETTERGB(defpal[i].rgbtRed,
				    defpal[i].rgbtGreen,
				    defpal[i].rgbtBlue);
    else
	for (i = 0; i < NCOLOURS; i++)
	    colours[i] = RGB(defpal[i].rgbtRed,
			     defpal[i].rgbtGreen, defpal[i].rgbtBlue);
}

/*
 * Initialise all the fonts we will need initially. There may be as many as
 * three or as few as one.  The other (poentially) twentyone fonts are done
 * if/when they are needed.
 *
 * We also:
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
static void init_fonts(int pick_width)
{
    TEXTMETRIC tm;
    CPINFO cpinfo;
    int fontsize[3];
    int i;
    HDC hdc;
    int fw_dontcare, fw_bold;

    for (i = 0; i < FONT_MAXNO; i++)
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
	font_height =
	    -MulDiv(font_height, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    }
    font_width = pick_width;

#define f(i,c,w,u) \
    fonts[i] = CreateFont (font_height, font_width, 0, 0, w, FALSE, u, FALSE, \
			   c, OUT_DEFAULT_PRECIS, \
		           CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, \
			   FIXED_PITCH | FF_DONTCARE, cfg.font)

    f(FONT_NORMAL, cfg.fontcharset, fw_dontcare, FALSE);

    SelectObject(hdc, fonts[FONT_NORMAL]);
    GetTextMetrics(hdc, &tm);
    font_height = tm.tmHeight;
    font_width = tm.tmAveCharWidth;

    {
	CHARSETINFO info;
	DWORD cset = tm.tmCharSet;
	memset(&info, 0xFF, sizeof(info));

	/* !!! Yes the next line is right */
	if (cset == OEM_CHARSET)
	    font_codepage = GetOEMCP();
	else
	    if (TranslateCharsetInfo
		((DWORD *) cset, &info, TCI_SRCCHARSET)) font_codepage =
		info.ciACP;
	else
	    font_codepage = -1;

	GetCPInfo(font_codepage, &cpinfo);
	dbcs_screenfont = (cpinfo.MaxCharSize > 1);
    }

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
	SetTextColor(und_dc, RGB(255, 255, 255));
	SetBkColor(und_dc, RGB(0, 0, 0));
	SetBkMode(und_dc, OPAQUE);
	ExtTextOut(und_dc, 0, 0, ETO_OPAQUE, NULL, " ", 1, NULL);
	gotit = FALSE;
	for (i = 0; i < font_height; i++) {
	    c = GetPixel(und_dc, font_width / 2, i);
	    if (c != RGB(0, 0, 0))
		gotit = TRUE;
	}
	SelectObject(und_dc, und_oldbm);
	DeleteObject(und_bm);
	DeleteDC(und_dc);
	if (!gotit) {
	    und_mode = UND_LINE;
	    DeleteObject(fonts[FONT_UNDERLINE]);
	    fonts[FONT_UNDERLINE] = 0;
	}
    }

    if (bold_mode == BOLD_FONT) {
	f(FONT_BOLD, cfg.fontcharset, fw_bold, FALSE);
    }
#undef f

    descent = tm.tmAscent + 1;
    if (descent >= font_height)
	descent = font_height - 1;

    for (i = 0; i < 3; i++) {
	if (fonts[i]) {
	    if (SelectObject(hdc, fonts[i]) && GetTextMetrics(hdc, &tm))
		fontsize[i] = tm.tmAveCharWidth + 256 * tm.tmHeight;
	    else
		fontsize[i] = -i;
	} else
	    fontsize[i] = -i;
    }

    ReleaseDC(hwnd, hdc);

    if (fontsize[FONT_UNDERLINE] != fontsize[FONT_NORMAL]) {
	und_mode = UND_LINE;
	DeleteObject(fonts[FONT_UNDERLINE]);
	fonts[FONT_UNDERLINE] = 0;
    }

    if (bold_mode == BOLD_FONT &&
	fontsize[FONT_BOLD] != fontsize[FONT_NORMAL]) {
	bold_mode = BOLD_SHADOW;
	DeleteObject(fonts[FONT_BOLD]);
	fonts[FONT_BOLD] = 0;
    }
    fontflag[0] = fontflag[1] = fontflag[2] = 1;

    init_ucs_tables();
}

static void another_font(int fontno)
{
    int basefont;
    int fw_dontcare, fw_bold;
    int c, u, w, x;
    char *s;

    if (fontno < 0 || fontno >= FONT_MAXNO || fontflag[fontno])
	return;

    basefont = (fontno & ~(FONT_BOLDUND));
    if (basefont != fontno && !fontflag[basefont])
	another_font(basefont);

    if (cfg.fontisbold) {
	fw_dontcare = FW_BOLD;
	fw_bold = FW_HEAVY;
    } else {
	fw_dontcare = FW_DONTCARE;
	fw_bold = FW_BOLD;
    }

    c = cfg.fontcharset;
    w = fw_dontcare;
    u = FALSE;
    s = cfg.font;
    x = font_width;

    if (fontno & FONT_WIDE)
	x *= 2;
    if (fontno & FONT_NARROW)
	x /= 2;
    if (fontno & FONT_OEM)
	c = OEM_CHARSET;
    if (fontno & FONT_BOLD)
	w = fw_bold;
    if (fontno & FONT_UNDERLINE)
	u = TRUE;

    fonts[fontno] =
	CreateFont(font_height * (1 + !!(fontno & FONT_HIGH)), x, 0, 0, w,
		   FALSE, u, FALSE, c, OUT_DEFAULT_PRECIS,
		   CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
		   FIXED_PITCH | FF_DONTCARE, s);

    fontflag[fontno] = 1;
}

static void deinit_fonts(void)
{
    int i;
    for (i = 0; i < FONT_MAXNO; i++) {
	if (fonts[i])
	    DeleteObject(fonts[i]);
	fonts[i] = 0;
	fontflag[i] = 0;
    }
}

void request_resize(int w, int h, int refont)
{
    int width, height;

    /* If the window is maximized supress resizing attempts */
    if (IsZoomed(hwnd))
	return;

    if (refont && w != cols && (cols == 80 || cols == 132)) {
	/* If font width too big for screen should we shrink the font more ? */
	if (w == 132)
	    font_width = ((font_width * cols + w / 2) / w);
	else
	    font_width = 0;
	deinit_fonts();
	bold_mode = cfg.bold_colour ? BOLD_COLOURS : BOLD_FONT;
	und_mode = UND_FONT;
	init_fonts(font_width);
    } else {
	static int first_time = 1;
	static RECT ss;

	switch (first_time) {
	  case 1:
	    /* Get the size of the screen */
	    if (GetClientRect(GetDesktopWindow(), &ss))
		/* first_time = 0 */ ;
	    else {
		first_time = 2;
		break;
	    }
	  case 0:
	    /* Make sure the values are sane */
	    width = (ss.right - ss.left - extra_width) / font_width;
	    height = (ss.bottom - ss.top - extra_height) / font_height;

	    if (w > width)
		w = width;
	    if (h > height)
		h = height;
	    if (w < 15)
		w = 15;
	    if (h < 1)
		w = 1;
	}
    }

    width = extra_width + font_width * w;
    height = extra_height + font_height * h;

    SetWindowPos(hwnd, NULL, 0, 0, width, height,
		 SWP_NOACTIVATE | SWP_NOCOPYBITS |
		 SWP_NOMOVE | SWP_NOZORDER);
}

static void click(Mouse_Button b, int x, int y, int shift, int ctrl)
{
    int thistime = GetMessageTime();

    if (send_raw_mouse) {
	term_mouse(b, MA_CLICK, x, y, shift, ctrl);
	return;
    }

    if (lastbtn == b && thistime - lasttime < dbltime) {
	lastact = (lastact == MA_CLICK ? MA_2CLK :
		   lastact == MA_2CLK ? MA_3CLK :
		   lastact == MA_3CLK ? MA_CLICK : MA_NOTHING);
    } else {
	lastbtn = b;
	lastact = MA_CLICK;
    }
    if (lastact != MA_NOTHING)
	term_mouse(b, lastact, x, y, shift, ctrl);
    lasttime = thistime;
}

/*
 * Translate a raw mouse button designation (LEFT, MIDDLE, RIGHT)
 * into a cooked one (SELECT, EXTEND, PASTE).
 */
Mouse_Button translate_button(Mouse_Button button)
{
    if (button == MBT_LEFT)
	return MBT_SELECT;
    if (button == MBT_MIDDLE)
	return cfg.mouse_is_xterm ? MBT_PASTE : MBT_EXTEND;
    if (button == MBT_RIGHT)
	return cfg.mouse_is_xterm ? MBT_EXTEND : MBT_PASTE;
}

static void show_mouseptr(int show)
{
    static int cursor_visible = 1;
    if (!cfg.hide_mouseptr)	       /* override if this feature disabled */
	show = 1;
    if (cursor_visible && !show)
	ShowCursor(FALSE);
    else if (!cursor_visible && show)
	ShowCursor(TRUE);
    cursor_visible = show;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    HDC hdc;
    static int ignore_size = FALSE;
    static int ignore_clip = FALSE;
    static int just_reconfigged = FALSE;
    static int resizing = FALSE;
    static int need_backend_resize = FALSE;
    static int defered_resize = FALSE;

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
	if (cfg.ping_interval > 0) {
	    time_t now;
	    time(&now);
	    if (now - last_movement > cfg.ping_interval) {
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
	    MessageBox(hwnd,
		       "Are you sure you want to close this session?",
		       "PuTTY Exit Confirmation",
		       MB_ICONWARNING | MB_OKCANCEL) == IDOK)
	    DestroyWindow(hwnd);
	return 0;
      case WM_DESTROY:
	show_mouseptr(1);
	PostQuitMessage(0);
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
		    filemap = CreateFileMapping((HANDLE) 0xFFFFFFFF,
						&sa,
						PAGE_READWRITE,
						0, sizeof(Config), NULL);
		    if (filemap) {
			p = (Config *) MapViewOfFile(filemap,
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
		    char *session =
			sessions[(lParam - IDM_SAVED_MIN) / 16];
		    cl = smalloc(16 + strlen(session));	/* 8, but play safe */
		    if (!cl)
			cl = NULL;     /* not a very important failure mode */
		    else {
			sprintf(cl, "putty @%s", session);
			freecl = TRUE;
		    }
		} else
		    cl = NULL;

		GetModuleFileName(NULL, b, sizeof(b) - 1);
		si.cb = sizeof(si);
		si.lpReserved = NULL;
		si.lpDesktop = NULL;
		si.lpTitle = NULL;
		si.dwFlags = 0;
		si.cbReserved2 = 0;
		si.lpReserved2 = NULL;
		CreateProcess(b, cl, NULL, NULL, TRUE,
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
		int prev_sunken_edge = cfg.sunken_edge;
		char oldlogfile[FILENAME_MAX];
		int oldlogtype;
		int need_setwpos = FALSE;
		int old_fwidth, old_fheight;

		strcpy(oldlogfile, cfg.logfilename);
		oldlogtype = cfg.logtype;
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
		deinit_fonts();
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
		    LONG nexflag, exflag =
			GetWindowLong(hwnd, GWL_EXSTYLE);

		    nexflag = exflag;
		    if (cfg.alwaysontop != prev_alwaysontop) {
			if (cfg.alwaysontop) {
			    nexflag |= WS_EX_TOPMOST;
			    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
					 SWP_NOMOVE | SWP_NOSIZE);
			} else {
			    nexflag &= ~(WS_EX_TOPMOST);
			    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
					 SWP_NOMOVE | SWP_NOSIZE);
			}
		    }
		    if (cfg.sunken_edge)
			nexflag |= WS_EX_CLIENTEDGE;
		    else
			nexflag &= ~(WS_EX_CLIENTEDGE);

		    nflg = flag;
		    if (cfg.scrollbar)
			nflg |= WS_VSCROLL;
		    else
			nflg &= ~WS_VSCROLL;
		    if (cfg.locksize)
			nflg &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
		    else
			nflg |= (WS_THICKFRAME | WS_MAXIMIZEBOX);

		    if (nflg != flag || nexflag != exflag) {
			RECT cr, wr;

			if (nflg != flag)
			    SetWindowLong(hwnd, GWL_STYLE, nflg);
			if (nexflag != exflag)
			    SetWindowLong(hwnd, GWL_EXSTYLE, nexflag);

			SendMessage(hwnd, WM_IGNORE_SIZE, 0, 0);

			SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
				     SWP_NOACTIVATE | SWP_NOCOPYBITS |
				     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER
				     | SWP_FRAMECHANGED);

			GetWindowRect(hwnd, &wr);
			GetClientRect(hwnd, &cr);
			extra_width =
			    wr.right - wr.left - cr.right + cr.left;
			extra_height =
			    wr.bottom - wr.top - cr.bottom + cr.top;
			need_setwpos = TRUE;
		    }
		}

		if (cfg.height != rows ||
		    cfg.width != cols ||
		    old_fwidth != font_width ||
		    old_fheight != font_height ||
		    cfg.savelines != savelines ||
		    cfg.sunken_edge != prev_sunken_edge)
			need_setwpos = TRUE;

		if (IsZoomed(hwnd)) {
		    int w, h;
		    RECT cr;
		    if (need_setwpos)
			defered_resize = TRUE;

		    GetClientRect(hwnd, &cr);
		    w = cr.right - cr.left;
		    h = cr.bottom - cr.top;
		    w = w / font_width;
		    if (w < 1)
			w = 1;
		    h = h / font_height;
		    if (h < 1)
			h = 1;

		    term_size(h, w, cfg.savelines);
		    InvalidateRect(hwnd, NULL, TRUE);
		    back->size();
		} else {
		    term_size(cfg.height, cfg.width, cfg.savelines);
		    InvalidateRect(hwnd, NULL, TRUE);
		    if (need_setwpos) {
			SetWindowPos(hwnd, NULL, 0, 0,
				     extra_width + font_width * cfg.width,
				     extra_height +
				     font_height * cfg.height,
				     SWP_NOACTIVATE | SWP_NOCOPYBITS |
				     SWP_NOMOVE | SWP_NOZORDER);
		    }
		}
		/* Oops */
		if (cfg.locksize && IsZoomed(hwnd))
		    force_normal(hwnd);
		set_title(cfg.wintitle);
		if (IsIconic(hwnd)) {
		    SetWindowText(hwnd,
				  cfg.win_name_always ? window_name :
				  icon_name);
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
	  case IDM_TEL_AYT:
	    back->special(TS_AYT);
	    break;
	  case IDM_TEL_BRK:
	    back->special(TS_BRK);
	    break;
	  case IDM_TEL_SYNCH:
	    back->special(TS_SYNCH);
	    break;
	  case IDM_TEL_EC:
	    back->special(TS_EC);
	    break;
	  case IDM_TEL_EL:
	    back->special(TS_EL);
	    break;
	  case IDM_TEL_GA:
	    back->special(TS_GA);
	    break;
	  case IDM_TEL_NOP:
	    back->special(TS_NOP);
	    break;
	  case IDM_TEL_ABORT:
	    back->special(TS_ABORT);
	    break;
	  case IDM_TEL_AO:
	    back->special(TS_AO);
	    break;
	  case IDM_TEL_IP:
	    back->special(TS_IP);
	    break;
	  case IDM_TEL_SUSP:
	    back->special(TS_SUSP);
	    break;
	  case IDM_TEL_EOR:
	    back->special(TS_EOR);
	    break;
	  case IDM_TEL_EOF:
	    back->special(TS_EOF);
	    break;
	  case IDM_ABOUT:
	    showabout(hwnd);
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
#define WHEEL_DELTA 120
      case WM_MOUSEWHEEL:
	{
	    wheel_accumulator += (short) HIWORD(wParam);
	    wParam = LOWORD(wParam);

	    /* process events when the threshold is reached */
	    while (abs(wheel_accumulator) >= WHEEL_DELTA) {
		int b;

		/* reduce amount for next time */
		if (wheel_accumulator > 0) {
		    b = MBT_WHEEL_UP;
		    wheel_accumulator -= WHEEL_DELTA;
		} else if (wheel_accumulator < 0) {
		    b = MBT_WHEEL_DOWN;
		    wheel_accumulator += WHEEL_DELTA;
		} else
		    break;

		if (send_raw_mouse) {
		    /* send a mouse-down followed by a mouse up */
		    term_mouse(b,
			       MA_CLICK,
			       TO_CHR_X(X_POS(lParam)),
			       TO_CHR_Y(Y_POS(lParam)), wParam & MK_SHIFT,
			       wParam & MK_CONTROL);
		    term_mouse(b, MA_RELEASE, TO_CHR_X(X_POS(lParam)),
			       TO_CHR_Y(Y_POS(lParam)), wParam & MK_SHIFT,
			       wParam & MK_CONTROL);
		} else {
		    /* trigger a scroll */
		    term_scroll(0,
				b == MBT_WHEEL_UP ? -rows / 2 : rows / 2);
		}
	    }
	    return 0;
	}
      case WM_LBUTTONDOWN:
      case WM_MBUTTONDOWN:
      case WM_RBUTTONDOWN:
      case WM_LBUTTONUP:
      case WM_MBUTTONUP:
      case WM_RBUTTONUP:
	{
	    int button, press;
	    switch (message) {
	      case WM_LBUTTONDOWN:
		button = MBT_LEFT;
		press = 1;
		break;
	      case WM_MBUTTONDOWN:
		button = MBT_MIDDLE;
		press = 1;
		break;
	      case WM_RBUTTONDOWN:
		button = MBT_RIGHT;
		press = 1;
		break;
	      case WM_LBUTTONUP:
		button = MBT_LEFT;
		press = 0;
		break;
	      case WM_MBUTTONUP:
		button = MBT_MIDDLE;
		press = 0;
		break;
	      case WM_RBUTTONUP:
		button = MBT_RIGHT;
		press = 0;
		break;
	    }
	    show_mouseptr(1);
	    if (press) {
		click(button,
		      TO_CHR_X(X_POS(lParam)), TO_CHR_Y(Y_POS(lParam)),
		      wParam & MK_SHIFT, wParam & MK_CONTROL);
		SetCapture(hwnd);
	    } else {
		term_mouse(button, MA_RELEASE,
			   TO_CHR_X(X_POS(lParam)),
			   TO_CHR_Y(Y_POS(lParam)), wParam & MK_SHIFT,
			   wParam & MK_CONTROL);
		ReleaseCapture();
	    }
	}
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
		b = MBT_SELECT;
	    else if (wParam & MK_MBUTTON)
		b = cfg.mouse_is_xterm ? MBT_PASTE : MBT_EXTEND;
	    else
		b = cfg.mouse_is_xterm ? MBT_EXTEND : MBT_PASTE;
	    term_mouse(b, MA_DRAG, TO_CHR_X(X_POS(lParam)),
		       TO_CHR_Y(Y_POS(lParam)), wParam & MK_SHIFT,
		       wParam & MK_CONTROL);
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
	    hdc = BeginPaint(hwnd, &p);
	    if (pal) {
		SelectPalette(hdc, pal, TRUE);
		RealizePalette(hdc);
	    }
	    term_paint(hdc, p.rcPaint.left, p.rcPaint.top,
		       p.rcPaint.right, p.rcPaint.bottom);
	    SelectObject(hdc, GetStockObject(SYSTEM_FONT));
	    SelectObject(hdc, GetStockObject(WHITE_PEN));
	    EndPaint(hwnd, &p);
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
	pend_netevent_wParam = wParam;
	pend_netevent_lParam = lParam;
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
	    LPRECT r = (LPRECT) lParam;

	    width = r->right - r->left - extra_width;
	    height = r->bottom - r->top - extra_height;
	    w = (width + font_width / 2) / font_width;
	    if (w < 1)
		w = 1;
	    h = (height + font_height / 2) / font_height;
	    if (h < 1)
		h = 1;
	    UpdateSizeTip(hwnd, w, h);
	    ew = width - w * font_width;
	    eh = height - h * font_height;
	    if (ew != 0) {
		if (wParam == WMSZ_LEFT ||
		    wParam == WMSZ_BOTTOMLEFT || wParam == WMSZ_TOPLEFT)
		    r->left += ew;
		else
		    r->right -= ew;
	    }
	    if (eh != 0) {
		if (wParam == WMSZ_TOP ||
		    wParam == WMSZ_TOPRIGHT || wParam == WMSZ_TOPLEFT)
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
	    SetWindowText(hwnd,
			  cfg.win_name_always ? window_name : icon_name);
	    break;
	}
	if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)
	    SetWindowText(hwnd, window_name);
	if (!ignore_size) {
	    int width, height, w, h;
#if 0				       /* we have fixed this using WM_SIZING now */
	    int ew, eh;
#endif

	    width = LOWORD(lParam);
	    height = HIWORD(lParam);
	    w = width / font_width;
	    if (w < 1)
		w = 1;
	    h = height / font_height;
	    if (h < 1)
		h = 1;
#if 0				       /* we have fixed this using WM_SIZING now */
	    ew = width - w * font_width;
	    eh = height - h * font_height;
	    if (ew != 0 || eh != 0) {
		RECT r;
		GetWindowRect(hwnd, &r);
		SendMessage(hwnd, WM_IGNORE_SIZE, 0, 0);
		SetWindowPos(hwnd, NULL, 0, 0,
			     r.right - r.left - ew, r.bottom - r.top - eh,
			     SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER);
	    }
#endif
	    if (w != cols || h != rows || just_reconfigged) {
		term_invalidate();
		term_size(h, w, cfg.savelines);
		/*
		 * Don't call back->size in mid-resize. (To prevent
		 * massive numbers of resize events getting sent
		 * down the connection during an NT opaque drag.)
		 */
		if (!resizing)
		    back->size();
		else {
		    need_backend_resize = TRUE;
		    cfg.height = h;
		    cfg.width = w;
		}
		just_reconfigged = FALSE;
	    }
	}
	if (wParam == SIZE_RESTORED && defered_resize) {
	    defered_resize = FALSE;
	    SetWindowPos(hwnd, NULL, 0, 0,
			 extra_width + font_width * cfg.width,
			 extra_height + font_height * cfg.height,
			 SWP_NOACTIVATE | SWP_NOCOPYBITS |
			 SWP_NOMOVE | SWP_NOZORDER);
	}
	ignore_size = FALSE;
	return 0;
      case WM_VSCROLL:
	switch (LOWORD(wParam)) {
	  case SB_BOTTOM:
	    term_scroll(-1, 0);
	    break;
	  case SB_TOP:
	    term_scroll(+1, 0);
	    break;
	  case SB_LINEDOWN:
	    term_scroll(0, +1);
	    break;
	  case SB_LINEUP:
	    term_scroll(0, -1);
	    break;
	  case SB_PAGEDOWN:
	    term_scroll(0, +rows / 2);
	    break;
	  case SB_PAGEUP:
	    term_scroll(0, -rows / 2);
	    break;
	  case SB_THUMBPOSITION:
	  case SB_THUMBTRACK:
	    term_scroll(1, HIWORD(wParam));
	    break;
	}
	break;
      case WM_PALETTECHANGED:
	if ((HWND) wParam != hwnd && pal != NULL) {
	    HDC hdc = get_ctx();
	    if (hdc) {
		if (RealizePalette(hdc) > 0)
		    UpdateColors(hdc);
		free_ctx(hdc);
	    }
	}
	break;
      case WM_QUERYNEWPALETTE:
	if (pal != NULL) {
	    HDC hdc = get_ctx();
	    if (hdc) {
		if (RealizePalette(hdc) > 0)
		    UpdateColors(hdc);
		free_ctx(hdc);
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

	    if (wParam == VK_PROCESSKEY) {
		MSG m;
		m.hwnd = hwnd;
		m.message = WM_KEYDOWN;
		m.wParam = wParam;
		m.lParam = lParam & 0xdfff;
		TranslateMessage(&m);
	    } else {
		len = TranslateKey(message, wParam, lParam, buf);
		if (len == -1)
		    return DefWindowProc(hwnd, message, wParam, lParam);
		ldisc_send(buf, len);

		if (len > 0)
		    show_mouseptr(0);
	    }
	}
	return 0;
      case WM_INPUTLANGCHANGE:
	{
	    /* wParam == Font number */
	    /* lParam == Locale */
	    char lbuf[20];
	    HKL NewInputLocale = (HKL) lParam;

	    // lParam == GetKeyboardLayout(0);

	    GetLocaleInfo(LOWORD(NewInputLocale),
			  LOCALE_IDEFAULTANSICODEPAGE, lbuf, sizeof(lbuf));

	    kbd_codepage = atoi(lbuf);
	}
	break;
      case WM_IME_CHAR:
	if (wParam & 0xFF00) {
	    unsigned char buf[2];

	    buf[1] = wParam;
	    buf[0] = wParam >> 8;
	    lpage_send(kbd_codepage, buf, 2);
	} else {
	    char c = (unsigned char) wParam;
	    lpage_send(kbd_codepage, &c, 1);
	}
	return (0);
      case WM_CHAR:
      case WM_SYSCHAR:
	/*
	 * Nevertheless, we are prepared to deal with WM_CHAR
	 * messages, should they crop up. So if someone wants to
	 * post the things to us as part of a macro manoeuvre,
	 * we're ready to cope.
	 */
	{
	    char c = (unsigned char)wParam;
	    lpage_send(CP_ACP, &c, 1);
	}
	return 0;
      case WM_SETCURSOR:
	if (send_raw_mouse) {
	    SetCursor(LoadCursor(NULL, IDC_ARROW));
	    return TRUE;
	}
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

/*
 * Move the system caret. (We maintain one, even though it's
 * invisible, for the benefit of blind people: apparently some
 * helper software tracks the system caret, so we should arrange to
 * have one.)
 */
void sys_cursor(int x, int y)
{
    if (has_focus)
	SetCaretPos(x * font_width, y * font_height);
}

/*
 * Draw a line of text in the window, at given character
 * coordinates, in given attributes.
 *
 * We are allowed to fiddle with the contents of `text'.
 */
void do_text(Context ctx, int x, int y, char *text, int len,
	     unsigned long attr, int lattr)
{
    COLORREF fg, bg, t;
    int nfg, nbg, nfont;
    HDC hdc = ctx;
    RECT line_box;
    int force_manual_underline = 0;
    int fnt_width = font_width * (1 + (lattr != LATTR_NORM));
    int char_width = fnt_width;
    int text_adjust = 0;
    static int *IpDx = 0, IpDxLEN = 0;

    if (attr & ATTR_WIDE)
	char_width *= 2;

    if (len > IpDxLEN || IpDx[0] != char_width) {
	int i;
	if (len > IpDxLEN) {
	    sfree(IpDx);
	    IpDx = smalloc((len + 16) * sizeof(int));
	    IpDxLEN = (len + 16);
	}
	for (i = 0; i < IpDxLEN; i++)
	    IpDx[i] = char_width;
    }

    x *= fnt_width;
    y *= font_height;

    if ((attr & TATTR_ACTCURS) && (cfg.cursor_type == 0 || big_cursor)) {
	attr &= ATTR_CUR_AND | (bold_mode != BOLD_COLOURS ? ATTR_BOLD : 0);
	attr ^= ATTR_CUR_XOR;
    }

    nfont = 0;
    if (cfg.vtmode == VT_POORMAN && lattr != LATTR_NORM) {
	/* Assume a poorman font is borken in other ways too. */
	lattr = LATTR_WIDE;
    } else
	switch (lattr) {
	  case LATTR_NORM:
	    break;
	  case LATTR_WIDE:
	    nfont |= FONT_WIDE;
	    break;
	  default:
	    nfont |= FONT_WIDE + FONT_HIGH;
	    break;
	}

    /* Special hack for the VT100 linedraw glyphs. */
    if ((attr & CSET_MASK) == 0x2300) {
	if (!dbcs_screenfont &&
	    text[0] >= (char) 0xBA && text[0] <= (char) 0xBD) {
	    switch ((unsigned char) (text[0])) {
	      case 0xBA:
		text_adjust = -2 * font_height / 5;
		break;
	      case 0xBB:
		text_adjust = -1 * font_height / 5;
		break;
	      case 0xBC:
		text_adjust = font_height / 5;
		break;
	      case 0xBD:
		text_adjust = 2 * font_height / 5;
		break;
	    }
	    if (lattr == LATTR_TOP || lattr == LATTR_BOT)
		text_adjust *= 2;
	    attr &= ~CSET_MASK;
	    text[0] = (char) (unitab_xterm['q'] & CHAR_MASK);
	    attr |= (unitab_xterm['q'] & CSET_MASK);
	    if (attr & ATTR_UNDER) {
		attr &= ~ATTR_UNDER;
		force_manual_underline = 1;
	    }
	}
    }

    /* Anything left as an original character set is unprintable. */
    if (DIRECT_CHAR(attr)) {
	attr &= ~CSET_MASK;
	attr |= 0xFF00;
	memset(text, 0xFF, len);
    }

    /* OEM CP */
    if ((attr & CSET_MASK) == ATTR_OEMCP)
	nfont |= FONT_OEM;

    nfg = 2 * ((attr & ATTR_FGMASK) >> ATTR_FGSHIFT);
    nbg = 2 * ((attr & ATTR_BGMASK) >> ATTR_BGSHIFT);
    if (bold_mode == BOLD_FONT && (attr & ATTR_BOLD))
	nfont |= FONT_BOLD;
    if (und_mode == UND_FONT && (attr & ATTR_UNDER))
	nfont |= FONT_UNDERLINE;
    another_font(nfont);
    if (!fonts[nfont]) {
	if (nfont & FONT_UNDERLINE)
	    force_manual_underline = 1;
	/* Don't do the same for manual bold, it could be bad news. */

	nfont &= ~(FONT_BOLD | FONT_UNDERLINE);
    }
    another_font(nfont);
    if (!fonts[nfont])
	nfont = FONT_NORMAL;
    if (attr & ATTR_REVERSE) {
	t = nfg;
	nfg = nbg;
	nbg = t;
    }
    if (bold_mode == BOLD_COLOURS && (attr & ATTR_BOLD))
	nfg++;
    if (bold_mode == BOLD_COLOURS && (attr & ATTR_BLINK))
	nbg++;
    fg = colours[nfg];
    bg = colours[nbg];
    SelectObject(hdc, fonts[nfont]);
    SetTextColor(hdc, fg);
    SetBkColor(hdc, bg);
    SetBkMode(hdc, OPAQUE);
    line_box.left = x;
    line_box.top = y;
    line_box.right = x + char_width * len;
    line_box.bottom = y + font_height;

    /* We're using a private area for direct to font. (512 chars.) */
    if (dbcs_screenfont && (attr & CSET_MASK) == ATTR_ACP) {
	/* Ho Hum, dbcs fonts are a PITA! */
	/* To display on W9x I have to convert to UCS */
	static wchar_t *uni_buf = 0;
	static int uni_len = 0;
	int nlen;
	if (len > uni_len) {
	    sfree(uni_buf);
	    uni_buf = smalloc((uni_len = len) * sizeof(wchar_t));
	}
	nlen = MultiByteToWideChar(font_codepage, MB_USEGLYPHCHARS,
				   text, len, uni_buf, uni_len);

	if (nlen <= 0)
	    return;		       /* Eeek! */

	ExtTextOutW(hdc, x,
		    y - font_height * (lattr == LATTR_BOT) + text_adjust,
		    ETO_CLIPPED | ETO_OPAQUE, &line_box, uni_buf, nlen, 0);
	if (bold_mode == BOLD_SHADOW && (attr & ATTR_BOLD)) {
	    SetBkMode(hdc, TRANSPARENT);
	    ExtTextOutW(hdc, x - 1,
			y - font_height * (lattr ==
					   LATTR_BOT) + text_adjust,
			ETO_CLIPPED, &line_box, uni_buf, nlen, 0);
	}
    } else if (DIRECT_FONT(attr)) {
	ExtTextOut(hdc, x,
		   y - font_height * (lattr == LATTR_BOT) + text_adjust,
		   ETO_CLIPPED | ETO_OPAQUE, &line_box, text, len, IpDx);
	if (bold_mode == BOLD_SHADOW && (attr & ATTR_BOLD)) {
	    SetBkMode(hdc, TRANSPARENT);

	    /* GRR: This draws the character outside it's box and can leave
	     * 'droppings' even with the clip box! I suppose I could loop it
	     * one character at a time ... yuk. 
	     * 
	     * Or ... I could do a test print with "W", and use +1 or -1 for this
	     * shift depending on if the leftmost column is blank...
	     */
	    ExtTextOut(hdc, x - 1,
		       y - font_height * (lattr ==
					  LATTR_BOT) + text_adjust,
		       ETO_CLIPPED, &line_box, text, len, IpDx);
	}
    } else {
	/* And 'normal' unicode characters */
	static WCHAR *wbuf = NULL;
	static int wlen = 0;
	int i;
	if (wlen < len) {
	    sfree(wbuf);
	    wlen = len;
	    wbuf = smalloc(wlen * sizeof(WCHAR));
	}
	for (i = 0; i < len; i++)
	    wbuf[i] = (WCHAR) ((attr & CSET_MASK) + (text[i] & CHAR_MASK));

	ExtTextOutW(hdc, x,
		    y - font_height * (lattr == LATTR_BOT) + text_adjust,
		    ETO_CLIPPED | ETO_OPAQUE, &line_box, wbuf, len, IpDx);

	/* And the shadow bold hack. */
	if (bold_mode == BOLD_SHADOW) {
	    SetBkMode(hdc, TRANSPARENT);
	    ExtTextOutW(hdc, x - 1,
			y - font_height * (lattr ==
					   LATTR_BOT) + text_adjust,
			ETO_CLIPPED, &line_box, wbuf, len, IpDx);
	}
    }
    if (lattr != LATTR_TOP && (force_manual_underline ||
			       (und_mode == UND_LINE
				&& (attr & ATTR_UNDER)))) {
	HPEN oldpen;
	int dec = descent;
	if (lattr == LATTR_BOT)
	    dec = dec * 2 - font_height;

	oldpen = SelectObject(hdc, CreatePen(PS_SOLID, 0, fg));
	MoveToEx(hdc, x, y + dec, NULL);
	LineTo(hdc, x + len * char_width, y + dec);
	oldpen = SelectObject(hdc, oldpen);
	DeleteObject(oldpen);
    }
}

void do_cursor(Context ctx, int x, int y, char *text, int len,
	       unsigned long attr, int lattr)
{

    int fnt_width;
    int char_width;
    HDC hdc = ctx;
    int ctype = cfg.cursor_type;

    if ((attr & TATTR_ACTCURS) && (ctype == 0 || big_cursor)) {
	if (((attr & CSET_MASK) | (unsigned char) *text) != UCSWIDE) {
	    do_text(ctx, x, y, text, len, attr, lattr);
	    return;
	}
	ctype = 2;
	attr |= TATTR_RIGHTCURS;
    }

    fnt_width = char_width = font_width * (1 + (lattr != LATTR_NORM));
    if (attr & ATTR_WIDE)
	char_width *= 2;
    x *= fnt_width;
    y *= font_height;

    if ((attr & TATTR_PASCURS) && (ctype == 0 || big_cursor)) {
	POINT pts[5];
	HPEN oldpen;
	pts[0].x = pts[1].x = pts[4].x = x;
	pts[2].x = pts[3].x = x + char_width - 1;
	pts[0].y = pts[3].y = pts[4].y = y;
	pts[1].y = pts[2].y = y + font_height - 1;
	oldpen = SelectObject(hdc, CreatePen(PS_SOLID, 0, colours[23]));
	Polyline(hdc, pts, 5);
	oldpen = SelectObject(hdc, oldpen);
	DeleteObject(oldpen);
    } else if ((attr & (TATTR_ACTCURS | TATTR_PASCURS)) && ctype != 0) {
	int startx, starty, dx, dy, length, i;
	if (ctype == 1) {
	    startx = x;
	    starty = y + descent;
	    dx = 1;
	    dy = 0;
	    length = char_width;
	} else {
	    int xadjust = 0;
	    if (attr & TATTR_RIGHTCURS)
		xadjust = char_width - 1;
	    startx = x + xadjust;
	    starty = y;
	    dx = 0;
	    dy = 1;
	    length = font_height;
	}
	if (attr & TATTR_ACTCURS) {
	    HPEN oldpen;
	    oldpen =
		SelectObject(hdc, CreatePen(PS_SOLID, 0, colours[23]));
	    MoveToEx(hdc, startx, starty, NULL);
	    LineTo(hdc, startx + dx * length, starty + dy * length);
	    oldpen = SelectObject(hdc, oldpen);
	    DeleteObject(oldpen);
	} else {
	    for (i = 0; i < length; i++) {
		if (i % 2 == 0) {
		    SetPixel(hdc, startx, starty, colours[23]);
		}
		startx += dx;
		starty += dy;
	    }
	}
    }
}

/*
 * Translate a WM_(SYS)?KEY(UP|DOWN) message into a string of ASCII
 * codes. Returns number of bytes used or zero to drop the message
 * or -1 to forward the message to windows.
 */
static int TranslateKey(UINT message, WPARAM wParam, LPARAM lParam,
			unsigned char *output)
{
    BYTE keystate[256];
    int scan, left_alt = 0, key_down, shift_state;
    int r, i, code;
    unsigned char *p = output;
    static int alt_state = 0;
    static int alt_sum = 0;

    HKL kbd_layout = GetKeyboardLayout(0);

    static WORD keys[3];
    static int compose_char = 0;
    static WPARAM compose_key = 0;

    r = GetKeyboardState(keystate);
    if (!r)
	memset(keystate, 0, sizeof(keystate));
    else {
#if 0
#define SHOW_TOASCII_RESULT
	{			       /* Tell us all about key events */
	    static BYTE oldstate[256];
	    static int first = 1;
	    static int scan;
	    int ch;
	    if (first)
		memcpy(oldstate, keystate, sizeof(oldstate));
	    first = 0;

	    if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) == KF_REPEAT) {
		debug(("+"));
	    } else if ((HIWORD(lParam) & KF_UP)
		       && scan == (HIWORD(lParam) & 0xFF)) {
		debug((". U"));
	    } else {
		debug((".\n"));
		if (wParam >= VK_F1 && wParam <= VK_F20)
		    debug(("K_F%d", wParam + 1 - VK_F1));
		else
		    switch (wParam) {
		      case VK_SHIFT:
			debug(("SHIFT"));
			break;
		      case VK_CONTROL:
			debug(("CTRL"));
			break;
		      case VK_MENU:
			debug(("ALT"));
			break;
		      default:
			debug(("VK_%02x", wParam));
		    }
		if (message == WM_SYSKEYDOWN || message == WM_SYSKEYUP)
		    debug(("*"));
		debug((", S%02x", scan = (HIWORD(lParam) & 0xFF)));

		ch = MapVirtualKeyEx(wParam, 2, kbd_layout);
		if (ch >= ' ' && ch <= '~')
		    debug((", '%c'", ch));
		else if (ch)
		    debug((", $%02x", ch));

		if (keys[0])
		    debug((", KB0=%02x", keys[0]));
		if (keys[1])
		    debug((", KB1=%02x", keys[1]));
		if (keys[2])
		    debug((", KB2=%02x", keys[2]));

		if ((keystate[VK_SHIFT] & 0x80) != 0)
		    debug((", S"));
		if ((keystate[VK_CONTROL] & 0x80) != 0)
		    debug((", C"));
		if ((HIWORD(lParam) & KF_EXTENDED))
		    debug((", E"));
		if ((HIWORD(lParam) & KF_UP))
		    debug((", U"));
	    }

	    if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) == KF_REPEAT);
	    else if ((HIWORD(lParam) & KF_UP))
		oldstate[wParam & 0xFF] ^= 0x80;
	    else
		oldstate[wParam & 0xFF] ^= 0x81;

	    for (ch = 0; ch < 256; ch++)
		if (oldstate[ch] != keystate[ch])
		    debug((", M%02x=%02x", ch, keystate[ch]));

	    memcpy(oldstate, keystate, sizeof(oldstate));
	}
#endif

	if (wParam == VK_MENU && (HIWORD(lParam) & KF_EXTENDED)) {
	    keystate[VK_RMENU] = keystate[VK_MENU];
	}


	/* Nastyness with NUMLock - Shift-NUMLock is left alone though */
	if ((cfg.funky_type == 3 ||
	     (cfg.funky_type <= 1 && app_keypad_keys && !cfg.no_applic_k))
	    && wParam == VK_NUMLOCK && !(keystate[VK_SHIFT] & 0x80)) {

	    wParam = VK_EXECUTE;

	    /* UnToggle NUMLock */
	    if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) == 0)
		keystate[VK_NUMLOCK] ^= 1;
	}

	/* And write back the 'adjusted' state */
	SetKeyboardState(keystate);
    }

    /* Disable Auto repeat if required */
    if (repeat_off && (HIWORD(lParam) & (KF_UP | KF_REPEAT)) == KF_REPEAT)
	return 0;

    if ((HIWORD(lParam) & KF_ALTDOWN) && (keystate[VK_RMENU] & 0x80) == 0)
	left_alt = 1;

    key_down = ((HIWORD(lParam) & KF_UP) == 0);

    /* Make sure Ctrl-ALT is not the same as AltGr for ToAscii unless told. */
    if (left_alt && (keystate[VK_CONTROL] & 0x80)) {
	if (cfg.ctrlaltkeys)
	    keystate[VK_MENU] = 0;
	else {
	    keystate[VK_RMENU] = 0x80;
	    left_alt = 0;
	}
    }

    scan = (HIWORD(lParam) & (KF_UP | KF_EXTENDED | 0xFF));
    shift_state = ((keystate[VK_SHIFT] & 0x80) != 0)
	+ ((keystate[VK_CONTROL] & 0x80) != 0) * 2;

    /* Note if AltGr was pressed and if it was used as a compose key */
    if (!compose_state) {
	compose_key = 0x100;
	if (cfg.compose_key) {
	    if (wParam == VK_MENU && (HIWORD(lParam) & KF_EXTENDED))
		compose_key = wParam;
	}
	if (wParam == VK_APPS)
	    compose_key = wParam;
    }

    if (wParam == compose_key) {
	if (compose_state == 0
	    && (HIWORD(lParam) & (KF_UP | KF_REPEAT)) == 0) compose_state =
		1;
	else if (compose_state == 1 && (HIWORD(lParam) & KF_UP))
	    compose_state = 2;
	else
	    compose_state = 0;
    } else if (compose_state == 1 && wParam != VK_CONTROL)
	compose_state = 0;

    /* 
     * Record that we pressed key so the scroll window can be reset, but
     * be careful to avoid Shift-UP/Down
     */
    if (wParam != VK_SHIFT && wParam != VK_PRIOR && wParam != VK_NEXT) {
	seen_key_event = 1;
    }

    /* Make sure we're not pasting */
    if (key_down)
	term_nopaste();

    if (compose_state > 1 && left_alt)
	compose_state = 0;

    /* Sanitize the number pad if not using a PC NumPad */
    if (left_alt || (app_keypad_keys && !cfg.no_applic_k
		     && cfg.funky_type != 2)
	|| cfg.funky_type == 3 || cfg.nethack_keypad || compose_state) {
	if ((HIWORD(lParam) & KF_EXTENDED) == 0) {
	    int nParam = 0;
	    switch (wParam) {
	      case VK_INSERT:
		nParam = VK_NUMPAD0;
		break;
	      case VK_END:
		nParam = VK_NUMPAD1;
		break;
	      case VK_DOWN:
		nParam = VK_NUMPAD2;
		break;
	      case VK_NEXT:
		nParam = VK_NUMPAD3;
		break;
	      case VK_LEFT:
		nParam = VK_NUMPAD4;
		break;
	      case VK_CLEAR:
		nParam = VK_NUMPAD5;
		break;
	      case VK_RIGHT:
		nParam = VK_NUMPAD6;
		break;
	      case VK_HOME:
		nParam = VK_NUMPAD7;
		break;
	      case VK_UP:
		nParam = VK_NUMPAD8;
		break;
	      case VK_PRIOR:
		nParam = VK_NUMPAD9;
		break;
	      case VK_DELETE:
		nParam = VK_DECIMAL;
		break;
	    }
	    if (nParam) {
		if (keystate[VK_NUMLOCK] & 1)
		    shift_state |= 1;
		wParam = nParam;
	    }
	}
    }

    /* If a key is pressed and AltGr is not active */
    if (key_down && (keystate[VK_RMENU] & 0x80) == 0 && !compose_state) {
	/* Okay, prepare for most alts then ... */
	if (left_alt)
	    *p++ = '\033';

	/* Lets see if it's a pattern we know all about ... */
	if (wParam == VK_PRIOR && shift_state == 1) {
	    SendMessage(hwnd, WM_VSCROLL, SB_PAGEUP, 0);
	    return 0;
	}
	if (wParam == VK_NEXT && shift_state == 1) {
	    SendMessage(hwnd, WM_VSCROLL, SB_PAGEDOWN, 0);
	    return 0;
	}
	if (wParam == VK_INSERT && shift_state == 1) {
	    term_mouse(MBT_PASTE, MA_CLICK, 0, 0, 0, 0);
	    term_mouse(MBT_PASTE, MA_RELEASE, 0, 0, 0, 0);
	    return 0;
	}
	if (left_alt && wParam == VK_F4 && cfg.alt_f4) {
	    return -1;
	}
	if (left_alt && wParam == VK_SPACE && cfg.alt_space) {
	    alt_state = 0;
	    PostMessage(hwnd, WM_CHAR, ' ', 0);
	    SendMessage(hwnd, WM_SYSCOMMAND, SC_KEYMENU, 0);
	    return -1;
	}
	/* Control-Numlock for app-keypad mode switch */
	if (wParam == VK_PAUSE && shift_state == 2) {
	    app_keypad_keys ^= 1;
	    return 0;
	}

	/* Nethack keypad */
	if (cfg.nethack_keypad && !left_alt) {
	    switch (wParam) {
	      case VK_NUMPAD1:
		*p++ = shift_state ? 'B' : 'b';
		return p - output;
	      case VK_NUMPAD2:
		*p++ = shift_state ? 'J' : 'j';
		return p - output;
	      case VK_NUMPAD3:
		*p++ = shift_state ? 'N' : 'n';
		return p - output;
	      case VK_NUMPAD4:
		*p++ = shift_state ? 'H' : 'h';
		return p - output;
	      case VK_NUMPAD5:
		*p++ = shift_state ? '.' : '.';
		return p - output;
	      case VK_NUMPAD6:
		*p++ = shift_state ? 'L' : 'l';
		return p - output;
	      case VK_NUMPAD7:
		*p++ = shift_state ? 'Y' : 'y';
		return p - output;
	      case VK_NUMPAD8:
		*p++ = shift_state ? 'K' : 'k';
		return p - output;
	      case VK_NUMPAD9:
		*p++ = shift_state ? 'U' : 'u';
		return p - output;
	    }
	}

	/* Application Keypad */
	if (!left_alt) {
	    int xkey = 0;

	    if (cfg.funky_type == 3 ||
		(cfg.funky_type <= 1 &&
		 app_keypad_keys && !cfg.no_applic_k)) switch (wParam) {
		  case VK_EXECUTE:
		    xkey = 'P';
		    break;
		  case VK_DIVIDE:
		    xkey = 'Q';
		    break;
		  case VK_MULTIPLY:
		    xkey = 'R';
		    break;
		  case VK_SUBTRACT:
		    xkey = 'S';
		    break;
		}
	    if (app_keypad_keys && !cfg.no_applic_k)
		switch (wParam) {
		  case VK_NUMPAD0:
		    xkey = 'p';
		    break;
		  case VK_NUMPAD1:
		    xkey = 'q';
		    break;
		  case VK_NUMPAD2:
		    xkey = 'r';
		    break;
		  case VK_NUMPAD3:
		    xkey = 's';
		    break;
		  case VK_NUMPAD4:
		    xkey = 't';
		    break;
		  case VK_NUMPAD5:
		    xkey = 'u';
		    break;
		  case VK_NUMPAD6:
		    xkey = 'v';
		    break;
		  case VK_NUMPAD7:
		    xkey = 'w';
		    break;
		  case VK_NUMPAD8:
		    xkey = 'x';
		    break;
		  case VK_NUMPAD9:
		    xkey = 'y';
		    break;

		  case VK_DECIMAL:
		    xkey = 'n';
		    break;
		  case VK_ADD:
		    if (cfg.funky_type == 2) {
			if (shift_state)
			    xkey = 'l';
			else
			    xkey = 'k';
		    } else if (shift_state)
			xkey = 'm';
		    else
			xkey = 'l';
		    break;

		  case VK_DIVIDE:
		    if (cfg.funky_type == 2)
			xkey = 'o';
		    break;
		  case VK_MULTIPLY:
		    if (cfg.funky_type == 2)
			xkey = 'j';
		    break;
		  case VK_SUBTRACT:
		    if (cfg.funky_type == 2)
			xkey = 'm';
		    break;

		  case VK_RETURN:
		    if (HIWORD(lParam) & KF_EXTENDED)
			xkey = 'M';
		    break;
		}
	    if (xkey) {
		if (vt52_mode) {
		    if (xkey >= 'P' && xkey <= 'S')
			p += sprintf((char *) p, "\x1B%c", xkey);
		    else
			p += sprintf((char *) p, "\x1B?%c", xkey);
		} else
		    p += sprintf((char *) p, "\x1BO%c", xkey);
		return p - output;
	    }
	}

	if (wParam == VK_BACK && shift_state == 0) {	/* Backspace */
	    *p++ = (cfg.bksp_is_delete ? 0x7F : 0x08);
	    *p++ = 0;
	    return -2;
	}
	if (wParam == VK_TAB && shift_state == 1) {	/* Shift tab */
	    *p++ = 0x1B;
	    *p++ = '[';
	    *p++ = 'Z';
	    return p - output;
	}
	if (wParam == VK_SPACE && shift_state == 2) {	/* Ctrl-Space */
	    *p++ = 0;
	    return p - output;
	}
	if (wParam == VK_SPACE && shift_state == 3) {	/* Ctrl-Shift-Space */
	    *p++ = 160;
	    return p - output;
	}
	if (wParam == VK_CANCEL && shift_state == 2) {	/* Ctrl-Break */
	    *p++ = 3;
	    *p++ = 0;
	    return -2;
	}
	if (wParam == VK_PAUSE) {      /* Break/Pause */
	    *p++ = 26;
	    *p++ = 0;
	    return -2;
	}
	/* Control-2 to Control-8 are special */
	if (shift_state == 2 && wParam >= '2' && wParam <= '8') {
	    *p++ = "\000\033\034\035\036\037\177"[wParam - '2'];
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
	    *p++ = '\r';
	    *p++ = '\n';
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
	  case VK_F1:
	    code = (keystate[VK_SHIFT] & 0x80 ? 23 : 11);
	    break;
	  case VK_F2:
	    code = (keystate[VK_SHIFT] & 0x80 ? 24 : 12);
	    break;
	  case VK_F3:
	    code = (keystate[VK_SHIFT] & 0x80 ? 25 : 13);
	    break;
	  case VK_F4:
	    code = (keystate[VK_SHIFT] & 0x80 ? 26 : 14);
	    break;
	  case VK_F5:
	    code = (keystate[VK_SHIFT] & 0x80 ? 28 : 15);
	    break;
	  case VK_F6:
	    code = (keystate[VK_SHIFT] & 0x80 ? 29 : 17);
	    break;
	  case VK_F7:
	    code = (keystate[VK_SHIFT] & 0x80 ? 31 : 18);
	    break;
	  case VK_F8:
	    code = (keystate[VK_SHIFT] & 0x80 ? 32 : 19);
	    break;
	  case VK_F9:
	    code = (keystate[VK_SHIFT] & 0x80 ? 33 : 20);
	    break;
	  case VK_F10:
	    code = (keystate[VK_SHIFT] & 0x80 ? 34 : 21);
	    break;
	  case VK_F11:
	    code = 23;
	    break;
	  case VK_F12:
	    code = 24;
	    break;
	  case VK_F13:
	    code = 25;
	    break;
	  case VK_F14:
	    code = 26;
	    break;
	  case VK_F15:
	    code = 28;
	    break;
	  case VK_F16:
	    code = 29;
	    break;
	  case VK_F17:
	    code = 31;
	    break;
	  case VK_F18:
	    code = 32;
	    break;
	  case VK_F19:
	    code = 33;
	    break;
	  case VK_F20:
	    code = 34;
	    break;
	  case VK_HOME:
	    code = 1;
	    break;
	  case VK_INSERT:
	    code = 2;
	    break;
	  case VK_DELETE:
	    code = 3;
	    break;
	  case VK_END:
	    code = 4;
	    break;
	  case VK_PRIOR:
	    code = 5;
	    break;
	  case VK_NEXT:
	    code = 6;
	    break;
	}
	/* Reorder edit keys to physical order */
	if (cfg.funky_type == 3 && code <= 6)
	    code = "\0\2\1\4\5\3\6"[code];

	if (vt52_mode && code > 0 && code <= 6) {
	    p += sprintf((char *) p, "\x1B%c", " HLMEIG"[code]);
	    return p - output;
	}

	if (cfg.funky_type == 5 && code >= 11 && code <= 34) {
	    char codes[] = "MNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz@[\\]^_`{";
	    int index = 0;
	    switch (wParam) {
	      case VK_F1: index = 0; break;
	      case VK_F2: index = 1; break;
	      case VK_F3: index = 2; break;
	      case VK_F4: index = 3; break;
	      case VK_F5: index = 4; break;
	      case VK_F6: index = 5; break;
	      case VK_F7: index = 6; break;
	      case VK_F8: index = 7; break;
	      case VK_F9: index = 8; break;
	      case VK_F10: index = 9; break;
	      case VK_F11: index = 10; break;
	      case VK_F12: index = 11; break;
	    }
	    if (keystate[VK_SHIFT] & 0x80) index += 12;
	    if (keystate[VK_CONTROL] & 0x80) index += 24;
	    p += sprintf((char *) p, "\x1B[%c", codes[index]);
	    return p - output;
	}
	if ((vt52_mode || cfg.funky_type == 4) && code >= 11 && code <= 24) {
	    int offt = 0;
	    if (code > 15)
		offt++;
	    if (code > 21)
		offt++;
	    if (vt52_mode)
		p += sprintf((char *) p, "\x1B%c", code + 'P' - 11 - offt);
	    else
		p +=
		    sprintf((char *) p, "\x1BO%c", code + 'P' - 11 - offt);
	    return p - output;
	}
	if (cfg.funky_type == 1 && code >= 11 && code <= 15) {
	    p += sprintf((char *) p, "\x1B[[%c", code + 'A' - 11);
	    return p - output;
	}
	if (cfg.funky_type == 2 && code >= 11 && code <= 14) {
	    if (vt52_mode)
		p += sprintf((char *) p, "\x1B%c", code + 'P' - 11);
	    else
		p += sprintf((char *) p, "\x1BO%c", code + 'P' - 11);
	    return p - output;
	}
	if (cfg.rxvt_homeend && (code == 1 || code == 4)) {
	    p += sprintf((char *) p, code == 1 ? "\x1B[H" : "\x1BOw");
	    return p - output;
	}
	if (code) {
	    p += sprintf((char *) p, "\x1B[%d~", code);
	    return p - output;
	}

	/*
	 * Now the remaining keys (arrows and Keypad 5. Keypad 5 for
	 * some reason seems to send VK_CLEAR to Windows...).
	 */
	{
	    char xkey = 0;
	    switch (wParam) {
	      case VK_UP:
		xkey = 'A';
		break;
	      case VK_DOWN:
		xkey = 'B';
		break;
	      case VK_RIGHT:
		xkey = 'C';
		break;
	      case VK_LEFT:
		xkey = 'D';
		break;
	      case VK_CLEAR:
		xkey = 'G';
		break;
	    }
	    if (xkey) {
		if (vt52_mode)
		    p += sprintf((char *) p, "\x1B%c", xkey);
		else {
		    int app_flg = (app_cursor_keys && !cfg.no_applic_c);
		    /* VT100 & VT102 manuals both state the app cursor keys
		     * only work if the app keypad is on.
		     */
		    if (!app_keypad_keys)
			app_flg = 0;
		    /* Useful mapping of Ctrl-arrows */
		    if (shift_state == 2)
			app_flg = !app_flg;

		    if (app_flg)
			p += sprintf((char *) p, "\x1BO%c", xkey);
		    else
			p += sprintf((char *) p, "\x1B[%c", xkey);
		}
		return p - output;
	    }
	}

	/*
	 * Finally, deal with Return ourselves. (Win95 seems to
	 * foul it up when Alt is pressed, for some reason.)
	 */
	if (wParam == VK_RETURN) {     /* Return */
	    *p++ = 0x0D;
	    *p++ = 0;
	    return -2;
	}

	if (left_alt && wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)
	    alt_sum = alt_sum * 10 + wParam - VK_NUMPAD0;
	else
	    alt_sum = 0;
    }

    /* Okay we've done everything interesting; let windows deal with 
     * the boring stuff */
    {
	r = ToAsciiEx(wParam, scan, keystate, keys, 0, kbd_layout);
#ifdef SHOW_TOASCII_RESULT
	if (r == 1 && !key_down) {
	    if (alt_sum) {
		if (utf || dbcs_screenfont)
		    debug((", (U+%04x)", alt_sum));
		else
		    debug((", LCH(%d)", alt_sum));
	    } else {
		debug((", ACH(%d)", keys[0]));
	    }
	} else if (r > 0) {
	    int r1;
	    debug((", ASC("));
	    for (r1 = 0; r1 < r; r1++) {
		debug(("%s%d", r1 ? "," : "", keys[r1]));
	    }
	    debug((")"));
	}
#endif
	if (r > 0) {
	    WCHAR keybuf;
	    p = output;
	    for (i = 0; i < r; i++) {
		unsigned char ch = (unsigned char) keys[i];

		if (compose_state == 2 && (ch & 0x80) == 0 && ch > ' ') {
		    compose_char = ch;
		    compose_state++;
		    continue;
		}
		if (compose_state == 3 && (ch & 0x80) == 0 && ch > ' ') {
		    int nc;
		    compose_state = 0;

		    if ((nc = check_compose(compose_char, ch)) == -1) {
			MessageBeep(MB_ICONHAND);
			return 0;
		    }
		    keybuf = nc;
		    luni_send(&keybuf, 1);
		    continue;
		}

		compose_state = 0;

		if (!key_down) {
		    if (alt_sum) {
			if (utf || dbcs_screenfont) {
			    keybuf = alt_sum;
			    luni_send(&keybuf, 1);
			} else {
			    ch = (char) alt_sum;
			    ldisc_send(&ch, 1);
			}
			alt_sum = 0;
		    } else
			lpage_send(kbd_codepage, &ch, 1);
		} else {
		    static char cbuf[] = "\033 ";
		    cbuf[1] = ch;
		    lpage_send(kbd_codepage, cbuf + !left_alt,
			       1 + !!left_alt);
		}
	    }

	    /* This is so the ALT-Numpad and dead keys work correctly. */
	    keys[0] = 0;

	    return p - output;
	}
	/* If we're definitly not building up an ALT-54321 then clear it */
	if (!left_alt)
	    keys[0] = 0;
	/* If we will be using alt_sum fix the 256s */
	else if (keys[0] && (utf || dbcs_screenfont))
	    keys[0] = 10;
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
    } else
	alt_state = 0;

    return -1;
}

void set_title(char *title)
{
    sfree(window_name);
    window_name = smalloc(1 + strlen(title));
    strcpy(window_name, title);
    if (cfg.win_name_always || !IsIconic(hwnd))
	SetWindowText(hwnd, title);
}

void set_icon(char *title)
{
    sfree(icon_name);
    icon_name = smalloc(1 + strlen(title));
    strcpy(icon_name, title);
    if (!cfg.win_name_always && IsIconic(hwnd))
	SetWindowText(hwnd, title);
}

void set_sbar(int total, int start, int page)
{
    SCROLLINFO si;

    if (!cfg.scrollbar)
	return;

    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = total - 1;
    si.nPage = page;
    si.nPos = start;
    if (hwnd)
	SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

Context get_ctx(void)
{
    HDC hdc;
    if (hwnd) {
	hdc = GetDC(hwnd);
	if (hdc && pal)
	    SelectPalette(hdc, pal, FALSE);
	return hdc;
    } else
	return NULL;
}

void free_ctx(Context ctx)
{
    SelectPalette(ctx, GetStockObject(DEFAULT_PALETTE), FALSE);
    ReleaseDC(hwnd, ctx);
}

static void real_palette_set(int n, int r, int g, int b)
{
    if (pal) {
	logpal->palPalEntry[n].peRed = r;
	logpal->palPalEntry[n].peGreen = g;
	logpal->palPalEntry[n].peBlue = b;
	logpal->palPalEntry[n].peFlags = PC_NOCOLLAPSE;
	colours[n] = PALETTERGB(r, g, b);
	SetPaletteEntries(pal, 0, NCOLOURS, logpal->palPalEntry);
    } else
	colours[n] = RGB(r, g, b);
}

void palette_set(int n, int r, int g, int b)
{
    static const int first[21] = {
	0, 2, 4, 6, 8, 10, 12, 14,
	1, 3, 5, 7, 9, 11, 13, 15,
	16, 17, 18, 20, 22
    };
    real_palette_set(first[n], r, g, b);
    if (first[n] >= 18)
	real_palette_set(first[n] + 1, r, g, b);
    if (pal) {
	HDC hdc = get_ctx();
	UnrealizeObject(pal);
	RealizePalette(hdc);
	free_ctx(hdc);
    }
}

void palette_reset(void)
{
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
			     defpal[i].rgbtGreen, defpal[i].rgbtBlue);
    }

    if (pal) {
	HDC hdc;
	SetPaletteEntries(pal, 0, NCOLOURS, logpal->palPalEntry);
	hdc = get_ctx();
	RealizePalette(hdc);
	free_ctx(hdc);
    }
}

void write_aclip(char *data, int len, int must_deselect)
{
    HGLOBAL clipdata;
    void *lock;

    clipdata = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, len + 1);
    if (!clipdata)
	return;
    lock = GlobalLock(clipdata);
    if (!lock)
	return;
    memcpy(lock, data, len);
    ((unsigned char *) lock)[len] = 0;
    GlobalUnlock(clipdata);

    if (!must_deselect)
	SendMessage(hwnd, WM_IGNORE_CLIP, TRUE, 0);

    if (OpenClipboard(hwnd)) {
	EmptyClipboard();
	SetClipboardData(CF_TEXT, clipdata);
	CloseClipboard();
    } else
	GlobalFree(clipdata);

    if (!must_deselect)
	SendMessage(hwnd, WM_IGNORE_CLIP, FALSE, 0);
}

/*
 * Note: unlike write_aclip() this will not append a nul.
 */
void write_clip(wchar_t * data, int len, int must_deselect)
{
    HGLOBAL clipdata;
    HGLOBAL clipdata2;
    int len2;
    void *lock, *lock2;

    len2 = WideCharToMultiByte(CP_ACP, 0, data, len, 0, 0, NULL, NULL);

    clipdata = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE,
			   len * sizeof(wchar_t));
    clipdata2 = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, len2);

    if (!clipdata || !clipdata2) {
	if (clipdata)
	    GlobalFree(clipdata);
	if (clipdata2)
	    GlobalFree(clipdata2);
	return;
    }
    if (!(lock = GlobalLock(clipdata)))
	return;
    if (!(lock2 = GlobalLock(clipdata2)))
	return;

    memcpy(lock, data, len * sizeof(wchar_t));
    WideCharToMultiByte(CP_ACP, 0, data, len, lock2, len2, NULL, NULL);

    GlobalUnlock(clipdata);
    GlobalUnlock(clipdata2);

    if (!must_deselect)
	SendMessage(hwnd, WM_IGNORE_CLIP, TRUE, 0);

    if (OpenClipboard(hwnd)) {
	EmptyClipboard();
	SetClipboardData(CF_UNICODETEXT, clipdata);
	SetClipboardData(CF_TEXT, clipdata2);
	CloseClipboard();
    } else {
	GlobalFree(clipdata);
	GlobalFree(clipdata2);
    }

    if (!must_deselect)
	SendMessage(hwnd, WM_IGNORE_CLIP, FALSE, 0);
}

void get_clip(wchar_t ** p, int *len)
{
    static HGLOBAL clipdata = NULL;
    static wchar_t *converted = 0;
    wchar_t *p2;

    if (converted) {
	sfree(converted);
	converted = 0;
    }
    if (!p) {
	if (clipdata)
	    GlobalUnlock(clipdata);
	clipdata = NULL;
	return;
    } else if (OpenClipboard(NULL)) {
	if (clipdata = GetClipboardData(CF_UNICODETEXT)) {
	    CloseClipboard();
	    *p = GlobalLock(clipdata);
	    if (*p) {
		for (p2 = *p; *p2; p2++);
		*len = p2 - *p;
		return;
	    }
	} else if (clipdata = GetClipboardData(CF_TEXT)) {
	    char *s;
	    int i;
	    CloseClipboard();
	    s = GlobalLock(clipdata);
	    i = MultiByteToWideChar(CP_ACP, 0, s, strlen(s) + 1, 0, 0);
	    *p = converted = smalloc(i * sizeof(wchar_t));
	    MultiByteToWideChar(CP_ACP, 0, s, strlen(s) + 1, converted, i);
	    *len = i - 1;
	    return;
	} else
	    CloseClipboard();
    }

    *p = NULL;
    *len = 0;
}

#if 0
/*
 * Move `lines' lines from position `from' to position `to' in the
 * window.
 */
void optimised_move(int to, int from, int lines)
{
    RECT r;
    int min, max;

    min = (to < from ? to : from);
    max = to + from - min;

    r.left = 0;
    r.right = cols * font_width;
    r.top = min * font_height;
    r.bottom = (max + lines) * font_height;
    ScrollWindow(hwnd, 0, (to - from) * font_height, &r, &r);
}
#endif

/*
 * Print a message box and perform a fatal exit.
 */
void fatalbox(char *fmt, ...)
{
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
void beep(int mode)
{
    if (mode == BELL_DEFAULT) {
	/*
	 * For MessageBeep style bells, we want to be careful of
	 * timing, because they don't have the nice property of
	 * PlaySound bells that each one cancels the previous
	 * active one. So we limit the rate to one per 50ms or so.
	 */
	static long lastbeep = 0;
	long beepdiff;

	beepdiff = GetTickCount() - lastbeep;
	if (beepdiff >= 0 && beepdiff < 50)
	    return;
	MessageBeep(MB_OK);
	/*
	 * The above MessageBeep call takes time, so we record the
	 * time _after_ it finishes rather than before it starts.
	 */
	lastbeep = GetTickCount();
    } else if (mode == BELL_WAVEFILE) {
	if (!PlaySound(cfg.bell_wavefile, NULL, SND_ASYNC | SND_FILENAME)) {
	    char buf[sizeof(cfg.bell_wavefile) + 80];
	    sprintf(buf, "Unable to play sound file\n%s\n"
		    "Using default sound instead", cfg.bell_wavefile);
	    MessageBox(hwnd, buf, "PuTTY Sound Error",
		       MB_OK | MB_ICONEXCLAMATION);
	    cfg.beep = BELL_DEFAULT;
	}
    }
}
