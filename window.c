#include <windows.h>
#include <commctrl.h>
#include <winsock.h>
#include <stdio.h>
#include <stdlib.h>

#define PUTTY_DO_GLOBALS		       /* actually _define_ globals */
#include "putty.h"
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

#define IDM_SAVED_MIN 0x1000
#define IDM_SAVED_MAX 0x2000

#define WM_IGNORE_SIZE (WM_USER + 2)
#define WM_IGNORE_CLIP (WM_USER + 3)

static int WINAPI WndProc (HWND, UINT, WPARAM, LPARAM);
static int TranslateKey(WPARAM wParam, LPARAM lParam, unsigned char *output);
static void cfgtopalette(void);
static void init_palette(void);
static void init_fonts(void);

static int extra_width, extra_height;

#define FONT_NORMAL 0
#define FONT_BOLD 1
#define FONT_UNDERLINE 2
#define FONT_BOLDUND 3
#define FONT_OEM 4
#define FONT_OEMBOLD 5
#define FONT_OEMBOLDUND 6
#define FONT_OEMUND 7
static HFONT fonts[8];
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

static int dbltime, lasttime, lastact;
static Mouse_Button lastbtn;

static char *window_name, *icon_name;

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show) {
    static char appname[] = "PuTTY";
    WORD winsock_ver;
    WSADATA wsadata;
    WNDCLASS wndclass;
    MSG msg;
    int guess_width, guess_height;

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

    InitCommonControls();

    /*
     * Process the command line.
     */
    {
	char *p;

	default_protocol = DEFAULT_PROTOCOL;
	default_port = DEFAULT_PORT;

	do_defaults(NULL);

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
	    }
	    p = q + strspn(q, " \t");
	}

	/*
	 * An initial @ means to activate a saved session.
	 */
	if (*p == '@') {
	    do_defaults (p+1);
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
	    if (sscanf(p+1, "%x", &filemap) == 1 &&
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
	} else {
	    if (!do_config()) {
		WSACleanup();
		return 0;
	    }
	}
    }

    back = (cfg.protocol == PROT_SSH ? &ssh_backend : 
			cfg.protocol == PROT_TELNET ? &telnet_backend : &raw_backend );

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

    hwnd = CreateWindow (appname, appname,
			 WS_OVERLAPPEDWINDOW | WS_VSCROLL,
			 CW_USEDEFAULT, CW_USEDEFAULT,
			 guess_width, guess_height,
			 NULL, NULL, inst, NULL);

    /*
     * Initialise the fonts, simultaneously correcting the guesses
     * for font_{width,height}.
     */
    bold_mode = cfg.bold_colour ? BOLD_COLOURS : BOLD_FONT;
    und_mode = UND_FONT;
    init_fonts();

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
     * Initialise the scroll bar.
     */
    {
	SCROLLINFO si;

	si.cbSize = sizeof(si);
	si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
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
	char msg[1024];
	char *realhost;

	error = back->init (hwnd, cfg.host, cfg.port, &realhost);
	if (error) {
	    sprintf(msg, "Unable to open connection:\n%s", error);
	    MessageBox(NULL, msg, "PuTTY Error", MB_ICONERROR | MB_OK);
	    return 0;
	}
	window_name = icon_name = NULL;
	sprintf(msg, "%s - PuTTY", realhost);
	set_title (msg);
	set_icon (msg);
    }

    /*
     * Set up the input and output buffers.
     */
    inbuf_reap = inbuf_head = 0;
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
	    AppendMenu (m, MF_ENABLED, IDM_SHOWLOG, "Show Negotiation");
	    AppendMenu (m, MF_SEPARATOR, 0, 0);
	}
	AppendMenu (m, MF_ENABLED, IDM_NEWSESS, "New Session");
	AppendMenu (m, MF_ENABLED, IDM_DUPSESS, "Duplicate Session");
	s = CreateMenu();
	get_sesslist(TRUE);
	for (i = 1 ; i < ((nsessions < 256) ? nsessions : 256) ; i++)
	  AppendMenu (s, MF_ENABLED, IDM_SAVED_MIN + (16 * i) , sessions[i]);
	AppendMenu (m, MF_POPUP | MF_ENABLED, (UINT) s, "Saved Sessions");
	AppendMenu (m, MF_ENABLED, IDM_RECONF, "Change Settings");
	AppendMenu (m, MF_SEPARATOR, 0, 0);
	AppendMenu (m, MF_ENABLED, IDM_CLRSB, "Clear Scrollback");
	AppendMenu (m, MF_ENABLED, IDM_RESET, "Reset Terminal");
	AppendMenu (m, MF_SEPARATOR, 0, 0);
	AppendMenu (m, MF_ENABLED, IDM_ABOUT, "About PuTTY");
    }

    /*
     * Finally show the window!
     */
    ShowWindow (hwnd, show);

    /*
     * Set the palette up.
     */
    pal = NULL;
    logpal = NULL;
    init_palette();

    has_focus = (GetForegroundWindow() == hwnd);
    UpdateWindow (hwnd);

    while (GetMessage (&msg, NULL, 0, 0)) {
	DispatchMessage (&msg);
	if (inbuf_reap != inbuf_head)
	    term_out();
	/* In idle moments, do a full screen update */
	if (!PeekMessage (&msg, NULL, 0, 0, PM_NOREMOVE))
	    term_update();
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

    if (cfg.protocol == PROT_SSH)
	random_save_seed();

    return msg.wParam;
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
 *
 * - verify, in OEM/ANSI combined mode, that the OEM and ANSI base
 *   fonts are the same size, and shift to OEM-only mode if not.
 */
static void init_fonts(void) {
    TEXTMETRIC tm;
    int i, j;
    int widths[5];
    HDC hdc;
    int fw_dontcare, fw_bold;

    for (i=0; i<8; i++)
	fonts[i] = NULL;

    if (cfg.fontisbold) {
	fw_dontcare = FW_BOLD;
	fw_bold = FW_BLACK;
   } else {
	fw_dontcare = FW_DONTCARE;
	fw_bold = FW_BOLD;
    }

#define f(i,c,w,u) \
    fonts[i] = CreateFont (cfg.fontheight, 0, 0, 0, w, FALSE, u, FALSE, \
			   c, OUT_DEFAULT_PRECIS, \
		           CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, \
			   FIXED_PITCH | FF_DONTCARE, cfg.font)
    if (cfg.vtmode != VT_OEMONLY) {
	f(FONT_NORMAL, ANSI_CHARSET, fw_dontcare, FALSE);
	f(FONT_UNDERLINE, ANSI_CHARSET, fw_dontcare, TRUE);
    }
    if (cfg.vtmode == VT_OEMANSI || cfg.vtmode == VT_OEMONLY) {
	f(FONT_OEM, OEM_CHARSET, fw_dontcare, FALSE);
	f(FONT_OEMUND, OEM_CHARSET, fw_dontcare, TRUE);
    }
    if (bold_mode == BOLD_FONT) {
	if (cfg.vtmode != VT_OEMONLY) {
	    f(FONT_BOLD, ANSI_CHARSET, fw_bold, FALSE);
	    f(FONT_BOLDUND, ANSI_CHARSET, fw_bold, TRUE);
	}
	if (cfg.vtmode == VT_OEMANSI || cfg.vtmode == VT_OEMONLY) {
	    f(FONT_OEMBOLD, OEM_CHARSET, fw_bold, FALSE);
	    f(FONT_OEMBOLDUND, OEM_CHARSET, fw_bold, TRUE);
	}
    } else {
	fonts[FONT_BOLD] = fonts[FONT_BOLDUND] = NULL;
	fonts[FONT_OEMBOLD] = fonts[FONT_OEMBOLDUND] = NULL;
    }
#undef f

    hdc = GetDC(hwnd);

    if (cfg.vtmode == VT_OEMONLY)
	j = 4;
    else
	j = 0;

    for (i=0; i<(cfg.vtmode == VT_OEMANSI ? 5 : 4); i++) {
	if (fonts[i+j]) {
	    SelectObject (hdc, fonts[i+j]);
	    GetTextMetrics(hdc, &tm);
	    if (i == 0 || i == 4) {
		font_height = tm.tmHeight;
		font_width = tm.tmAveCharWidth;
		descent = tm.tmAscent + 1;
		if (descent >= font_height)
		    descent = font_height - 1;
	    }
	    widths[i] = tm.tmAveCharWidth;
	}
    }

    ReleaseDC (hwnd, hdc);

    if (widths[FONT_UNDERLINE] != widths[FONT_NORMAL] ||
	(bold_mode == BOLD_FONT &&
	 widths[FONT_BOLDUND] != widths[FONT_BOLD])) {
	und_mode = UND_LINE;
	DeleteObject (fonts[FONT_UNDERLINE]);
	if (bold_mode == BOLD_FONT)
	    DeleteObject (fonts[FONT_BOLDUND]);
    }

    if (bold_mode == BOLD_FONT &&
	widths[FONT_BOLD] != widths[FONT_NORMAL]) {
	bold_mode = BOLD_SHADOW;
	DeleteObject (fonts[FONT_BOLD]);
	if (und_mode == UND_FONT)
	    DeleteObject (fonts[FONT_BOLDUND]);
    }

    if (cfg.vtmode == VT_OEMANSI && widths[FONT_OEM] != widths[FONT_NORMAL]) {
	MessageBox(NULL, "The OEM and ANSI versions of this font are\n"
		   "different sizes. Using OEM-only mode instead",
		   "Font Size Mismatch", MB_ICONINFORMATION | MB_OK);
	cfg.vtmode = VT_OEMONLY;
	for (i=0; i<4; i++)
	    if (fonts[i])
		DeleteObject (fonts[i]);
    }
}

void request_resize (int w, int h) {
    int width = extra_width + font_width * w;
    int height = extra_height + font_height * h;

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

static int WINAPI WndProc (HWND hwnd, UINT message,
			   WPARAM wParam, LPARAM lParam) {
    HDC hdc;
    static int ignore_size = FALSE;
    static int ignore_clip = FALSE;
    static int just_reconfigged = FALSE;

    switch (message) {
      case WM_CREATE:
	break;
      case WM_DESTROY:
	PostQuitMessage (0);
	return 0;
      case WM_SYSCOMMAND:
	switch (wParam & ~0xF) {       /* low 4 bits reserved to Windows */
	  case IDM_SHOWLOG:
	    shownegot(hwnd);
	    break;
	  case IDM_NEWSESS:
	  case IDM_DUPSESS:
	  case IDM_SAVEDSESS:
	    {
		char b[2048];
		char c[30], *cl;
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
		    sprintf(c, "putty &%08x", filemap);
		    cl = c;
		} else if (wParam == IDM_SAVEDSESS) {
		    sprintf(c, "putty @%s",
			    sessions[(lParam - IDM_SAVED_MIN) / 16]);
		    cl = c;
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
	    }
	    break;
	  case IDM_RECONF:
	    if (!do_reconfig(hwnd))
		break;
	    just_reconfigged = TRUE;
	    {
		int i;
		for (i=0; i<8; i++)
		    if (fonts[i])
			DeleteObject(fonts[i]);
	    }
	    bold_mode = cfg.bold_colour ? BOLD_COLOURS : BOLD_FONT;
	    und_mode = UND_FONT;
	    init_fonts();
	    sfree(logpal);
	    if (pal)
		DeleteObject(pal);
	    logpal = NULL;
	    pal = NULL;
	    cfgtopalette();
	    init_palette();
	    term_size(cfg.height, cfg.width, cfg.savelines);
	    InvalidateRect(hwnd, NULL, TRUE);
	    SetWindowPos (hwnd, NULL, 0, 0,
			  extra_width + font_width * cfg.width,
			  extra_height + font_height * cfg.height,
			  SWP_NOACTIVATE | SWP_NOCOPYBITS |
			  SWP_NOMOVE | SWP_NOZORDER);
	    if (IsIconic(hwnd)) {
		SetWindowText (hwnd,
			       cfg.win_name_always ? window_name : icon_name);
	    }
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
	click (MB_SELECT, TO_CHR_X(X_POS(lParam)),
	       TO_CHR_Y(Y_POS(lParam)));
        SetCapture(hwnd);
	return 0;
      case WM_LBUTTONUP:
	term_mouse (MB_SELECT, MA_RELEASE, TO_CHR_X(X_POS(lParam)),
		    TO_CHR_Y(Y_POS(lParam)));
        ReleaseCapture();
	return 0;
      case WM_MBUTTONDOWN:
        SetCapture(hwnd);
	click (cfg.mouse_is_xterm ? MB_PASTE : MB_EXTEND,
	       TO_CHR_X(X_POS(lParam)),
	       TO_CHR_Y(Y_POS(lParam)));
	return 0;
      case WM_MBUTTONUP:
	term_mouse (cfg.mouse_is_xterm ? MB_PASTE : MB_EXTEND,
		    MA_RELEASE, TO_CHR_X(X_POS(lParam)),
		    TO_CHR_Y(Y_POS(lParam)));
        ReleaseCapture();
	return 0;
      case WM_RBUTTONDOWN:
        SetCapture(hwnd);
	click (cfg.mouse_is_xterm ? MB_EXTEND : MB_PASTE,
	       TO_CHR_X(X_POS(lParam)),
	       TO_CHR_Y(Y_POS(lParam)));
	return 0;
      case WM_RBUTTONUP:
	term_mouse (cfg.mouse_is_xterm ? MB_EXTEND : MB_PASTE,
		    MA_RELEASE, TO_CHR_X(X_POS(lParam)),
		    TO_CHR_Y(Y_POS(lParam)));
        ReleaseCapture();
	return 0;
      case WM_MOUSEMOVE:
	/*
	 * Add the mouse position and message time to the random
	 * number noise, if we're using ssh.
	 */
	if (cfg.protocol == PROT_SSH)
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
	}
	return 0;
      case WM_NETEVENT:
	{
	    int i = back->msg (wParam, lParam);
	    if (i < 0) {
		char buf[1024];
		switch (WSABASEERR + (-i) % 10000) {
		  case WSAECONNRESET:
		    sprintf(buf, "Connection reset by peer");
		    break;
		  default:
		    sprintf(buf, "Unexpected network error %d", -i);
		    break;
		}
		MessageBox(hwnd, buf, "PuTTY Fatal Error",
			   MB_ICONERROR | MB_OK);
		PostQuitMessage(1);
	    } else if (i == 0) {
		if (cfg.close_on_exit)
		    PostQuitMessage(0);
		else {
		    MessageBox(hwnd, "Connection closed by remote host",
			       "PuTTY", MB_OK | MB_ICONINFORMATION);
		    SetWindowText (hwnd, "PuTTY (inactive)");
		}
	    }
	}
	return 0;
      case WM_SETFOCUS:
	has_focus = TRUE;
	term_out();
	term_update();
	break;
      case WM_KILLFOCUS:
	has_focus = FALSE;
	term_out();
	term_update();
	break;
      case WM_IGNORE_SIZE:
	ignore_size = TRUE;	       /* don't panic on next WM_SIZE msg */
	break;
      case WM_SIZING:
	{
	    int width, height, w, h, ew, eh;
	    LPRECT r = (LPRECT)lParam;

	    width = r->right - r->left - extra_width;
	    height = r->bottom - r->top - extra_height;
	    w = (width + font_width/2) / font_width; if (w < 1) w = 1;
	    h = (height + font_height/2) / font_height; if (h < 1) h = 1;
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
	break;
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
		back->size();
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
	/*
	 * Add the scan code and keypress timing to the random
	 * number noise, if we're using ssh.
	 */
	if (cfg.protocol == PROT_SSH)
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

	    len = TranslateKey (wParam, lParam, buf);
	    back->send (buf, len);
	}
	return 0;
      case WM_KEYUP:
      case WM_SYSKEYUP:
	/*
	 * We handle KEYUP ourselves in order to distinghish left
	 * and right Alt or Control keys, which Windows won't do
	 * right if left to itself. See also the special processing
	 * at the top of TranslateKey.
	 */
	{
            BYTE keystate[256];
            int ret = GetKeyboardState(keystate);
            if (ret && wParam == VK_MENU) {
		if (lParam & 0x1000000) keystate[VK_RMENU] = 0;
		else keystate[VK_LMENU] = 0;
                SetKeyboardState (keystate);
            }
            if (ret && wParam == VK_CONTROL) {
		if (lParam & 0x1000000) keystate[VK_RCONTROL] = 0;
		else keystate[VK_LCONTROL] = 0;
                SetKeyboardState (keystate);
            }
	}
        /*
         * We don't return here, in order to allow Windows to do
         * its own KEYUP processing as well.
         */
	break;
      case WM_CHAR:
      case WM_SYSCHAR:
	/*
	 * Nevertheless, we are prepared to deal with WM_CHAR
	 * messages, should they crop up. So if someone wants to
	 * post the things to us as part of a macro manoeuvre,
	 * we're ready to cope.
	 */
	{
	    char c = wParam;
	    back->send (&c, 1);
	}
	return 0;
    }

    return DefWindowProc (hwnd, message, wParam, lParam);
}

/*
 * Draw a line of text in the window, at given character
 * coordinates, in given attributes.
 *
 * We are allowed to fiddle with the contents of `text'.
 */
void do_text (Context ctx, int x, int y, char *text, int len,
	      unsigned long attr) {
    COLORREF fg, bg, t;
    int nfg, nbg, nfont;
    HDC hdc = ctx;

    x *= font_width;
    y *= font_height;

    if (attr & ATTR_ACTCURS) {
	attr &= (bold_mode == BOLD_COLOURS ? 0x200 : 0x300);
	attr ^= ATTR_CUR_XOR;
    }

    nfont = 0;
    if (cfg.vtmode == VT_OEMONLY)
	nfont |= FONT_OEM;

    /*
     * Map high-half characters in order to approximate ISO using
     * OEM character set. Characters missing are 0xC3 (Atilde) and
     * 0xCC (Igrave).
     */
    if (nfont & FONT_OEM) {
	int i;
	for (i=0; i<len; i++)
	    if (text[i] >= '\xA0' && text[i] <= '\xFF') {
		static const char oemhighhalf[] =
		    "\x20\xAD\xBD\x9C\xCF\xBE\xDD\xF5" /* A0-A7 */
		    "\xF9\xB8\xA6\xAE\xAA\xF0\xA9\xEE" /* A8-AF */
		    "\xF8\xF1\xFD\xFC\xEF\xE6\xF4\xFA" /* B0-B7 */
		    "\xF7\xFB\xA7\xAF\xAC\xAB\xF3\xA8" /* B8-BF */
		    "\xB7\xB5\xB6\x41\x8E\x8F\x92\x80" /* C0-C7 */
		    "\xD4\x90\xD2\xD3\x49\xD6\xD7\xD8" /* C8-CF */
		    "\xD1\xA5\xE3\xE0\xE2\xE5\x99\x9E" /* D0-D7 */
		    "\x9D\xEB\xE9\xEA\x9A\xED\xE8\xE1" /* D8-DF */
		    "\x85\xA0\x83\xC6\x84\x86\x91\x87" /* E0-E7 */
		    "\x8A\x82\x88\x89\x8D\xA1\x8C\x8B" /* E8-EF */
		    "\xD0\xA4\x95\xA2\x93\xE4\x94\xF6" /* F0-F7 */
		    "\x9B\x97\xA3\x96\x81\xEC\xE7\x98" /* F8-FF */
		    ;
		text[i] = oemhighhalf[(unsigned char)text[i] - 0xA0];
	    }
    }

    if (attr & ATTR_GBCHR) {
	int i;
	/*
	 * GB mapping: map # to pound, and everything else stays
	 * normal.
	 */
	for (i=0; i<len; i++)
	    if (text[i] == '#')
		text[i] = cfg.vtmode == VT_OEMONLY ? '\x9C' : '\xA3';
    } else if (attr & ATTR_LINEDRW) {
	int i;
	static const char poorman[] =
	    "*#****\xB0\xB1**+++++-----++++|****\xA3\xB7";
	static const char oemmap[] =
	    "*\xB1****\xF8\xF1**\xD9\xBF\xDA\xC0\xC5"
	    "\xC4\xC4\xC4\xC4\xC4\xC3\xB4\xC1\xC2\xB3****\x9C\xFA";

	/*
	 * Line drawing mapping: map ` thru ~ (0x60 thru 0x7E) to
	 * VT100 line drawing chars; everything else stays normal.
	 */
	switch (cfg.vtmode) {
	  case VT_XWINDOWS:
	    for (i=0; i<len; i++)
		if (text[i] >= '\x60' && text[i] <= '\x7E')
		    text[i] += '\x01' - '\x60';
	    break;
	  case VT_OEMANSI:
	  case VT_OEMONLY:
	    nfont |= FONT_OEM;
	    for (i=0; i<len; i++)
		if (text[i] >= '\x60' && text[i] <= '\x7E')
		    text[i] = oemmap[(unsigned char)text[i] - 0x60];
	    break;
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
    if (attr & ATTR_REVERSE) {
	t = nfg; nfg = nbg; nbg = t;
    }
    if (bold_mode == BOLD_COLOURS && (attr & ATTR_BOLD))
	nfg++;
    fg = colours[nfg];
    bg = colours[nbg];
    SelectObject (hdc, fonts[nfont]);
    SetTextColor (hdc, fg);
    SetBkColor (hdc, bg);
    SetBkMode (hdc, OPAQUE);
    TextOut (hdc, x, y, text, len);
    if (bold_mode == BOLD_SHADOW && (attr & ATTR_BOLD)) {
	SetBkMode (hdc, TRANSPARENT);
	TextOut (hdc, x-1, y, text, len);
    }
    if (und_mode == UND_LINE && (attr & ATTR_UNDER)) {
        HPEN oldpen;
	oldpen = SelectObject (hdc, CreatePen(PS_SOLID, 0, fg));
	MoveToEx (hdc, x, y+descent, NULL);
	LineTo (hdc, x+len*font_width, y+descent);
        oldpen = SelectObject (hdc, oldpen);
        DeleteObject (oldpen);
    }
    if (attr & ATTR_PASCURS) {
	POINT pts[5];
        HPEN oldpen;
	pts[0].x = pts[1].x = pts[4].x = x;
	pts[2].x = pts[3].x = x+font_width-1;
	pts[0].y = pts[3].y = pts[4].y = y;
	pts[1].y = pts[2].y = y+font_height-1;
	oldpen = SelectObject (hdc, CreatePen(PS_SOLID, 0, colours[23]));
	Polyline (hdc, pts, 5);
        oldpen = SelectObject (hdc, oldpen);
        DeleteObject (oldpen);
    }
}

/*
 * Translate a WM_(SYS)?KEYDOWN message into a string of ASCII
 * codes. Returns number of bytes used.
 */
static int TranslateKey(WPARAM wParam, LPARAM lParam, unsigned char *output) {
    unsigned char *p = output;
    BYTE keystate[256];
    int ret, code;
    int cancel_alt = FALSE;

    /*
     * Get hold of the keyboard state, because we'll need it a few
     * times shortly.
     */
    ret = GetKeyboardState(keystate);

    /* 
     * Windows does not always want to distinguish left and right
     * Alt or Control keys. Thus we keep track of them ourselves.
     * See also the WM_KEYUP handler.
     */
    if (wParam == VK_MENU) {
        if (lParam & 0x1000000) keystate[VK_RMENU] = 0x80;
        else keystate[VK_LMENU] = 0x80;
        SetKeyboardState (keystate);
        return 0;
    }
    if (wParam == VK_CONTROL) {
        if (lParam & 0x1000000) keystate[VK_RCONTROL] = 0x80;
        else keystate[VK_LCONTROL] = 0x80;
        SetKeyboardState (keystate);
        return 0;
    }

    /*
     * Prepend ESC, and cancel ALT, if ALT was pressed at the time
     * and it wasn't AltGr.
     */
    if (lParam & 0x20000000 && (keystate[VK_LMENU] & 0x80)) {
        *p++ = 0x1B;
        cancel_alt = TRUE;
    }

    /*
     * Shift-PgUp, Shift-PgDn, and Alt-F4 all produce window
     * events: we'll deal with those now.
     */
    if (ret && (keystate[VK_SHIFT] & 0x80) && wParam == VK_PRIOR) {
	SendMessage (hwnd, WM_VSCROLL, SB_PAGEUP, 0);
	return 0;
    }
    if (ret && (keystate[VK_SHIFT] & 0x80) && wParam == VK_NEXT) {
	SendMessage (hwnd, WM_VSCROLL, SB_PAGEDOWN, 0);
	return 0;
    }
    if ((lParam & 0x20000000) && wParam == VK_F4) {
	SendMessage (hwnd, WM_DESTROY, 0, 0);
	return 0;
    }

    /*
     * In general, the strategy is to see what the Windows keymap
     * translation has to say for itself, and then process function
     * keys and suchlike ourselves if that fails. But first we must
     * deal with the small number of special cases which the
     * Windows keymap translator thinks it can do but gets wrong.
     *
     * First special case: we might want the Backspace key to send
     * 0x7F not 0x08.
     */
    if (wParam == VK_BACK) {
	*p++ = (cfg.bksp_is_delete ? 0x7F : 0x08);
	return p - output;
    }

    /*
     * Control-Space should send ^@ (0x00), not Space.
     */
    if (ret && (keystate[VK_CONTROL] & 0x80) && wParam == VK_SPACE) {
	*p++ = 0x00;
	return p - output;
    }

    /*
     * If we're in applications keypad mode, we have to process it
     * before char-map translation, because it will pre-empt lots
     * of stuff, even if NumLock is off.
     */
    if (app_keypad_keys) {
	if (ret) {
	    /*
	     * Hack to ensure NumLock doesn't interfere with
	     * perception of Shift for Keypad Plus. I don't pretend
	     * to understand this, but it seems to work as is.
	     * Leave it alone, or die.
	     */
	    keystate[VK_NUMLOCK] = 0;
	    SetKeyboardState (keystate);
	    GetKeyboardState (keystate);
	}
	switch ( (lParam >> 16) & 0x1FF ) {
	  case 0x145: p += sprintf((char *)p, "\x1BOP"); return p - output;
	  case 0x135: p += sprintf((char *)p, "\x1BOQ"); return p - output;
	  case 0x037: p += sprintf((char *)p, "\x1BOR"); return p - output;
	  case 0x047: p += sprintf((char *)p, "\x1BOw"); return p - output;
	  case 0x048: p += sprintf((char *)p, "\x1BOx"); return p - output;
	  case 0x049: p += sprintf((char *)p, "\x1BOy"); return p - output;
	  case 0x04A: p += sprintf((char *)p, "\x1BOS"); return p - output;
	  case 0x04B: p += sprintf((char *)p, "\x1BOt"); return p - output;
	  case 0x04C: p += sprintf((char *)p, "\x1BOu"); return p - output;
	  case 0x04D: p += sprintf((char *)p, "\x1BOv"); return p - output;
	  case 0x04E: /* keypad + is ^[Ol, but ^[Om with Shift */
	    p += sprintf((char *)p,
			 (ret && (keystate[VK_SHIFT] & 0x80)) ?
			 "\x1BOm" : "\x1BOl");
	    return p - output;
	  case 0x04F: p += sprintf((char *)p, "\x1BOq"); return p - output;
	  case 0x050: p += sprintf((char *)p, "\x1BOr"); return p - output;
	  case 0x051: p += sprintf((char *)p, "\x1BOs"); return p - output;
	  case 0x052: p += sprintf((char *)p, "\x1BOp"); return p - output;
	  case 0x053: p += sprintf((char *)p, "\x1BOn"); return p - output;
	  case 0x11C: p += sprintf((char *)p, "\x1BOM"); return p - output;
	}
    }

    /*
     * Before doing Windows charmap translation, remove LeftALT
     * from the keymap, since its sole effect should be to prepend
     * ESC, which we've already done. Note that removal of LeftALT
     * has to happen _after_ the above call to SetKeyboardState, or
     * dire things will befall.
     */
    if (cancel_alt) {
        keystate[VK_MENU] = keystate[VK_RMENU];
        keystate[VK_LMENU] = 0;
    }

    /*
     * Attempt the Windows char-map translation.
     */
    if (ret) {
	WORD chr;
	int r = ToAscii (wParam, (lParam >> 16) & 0xFF,
			 keystate, &chr, 0);
	if (r == 1) {
	    *p++ = chr & 0xFF;
	    return p - output;
	}
    }

    /*
     * OK, we haven't had a key code from the keymap translation.
     * We'll try our various special cases and function keys, and
     * then give up. (There's nothing wrong with giving up:
     * Scrollock, Pause/Break, and of course the various buckybit
     * keys all produce KEYDOWN events that we really _do_ want to
     * ignore.)
     */

    /*
     * Control-2 should return ^@ (0x00), Control-6 should return
     * ^^ (0x1E), and Control-Minus should return ^_ (0x1F). Since
     * the DOS keyboard handling did it, and we have nothing better
     * to do with the key combo in question, we'll also map
     * Control-Backquote to ^\ (0x1C).
     */
    if (ret && (keystate[VK_CONTROL] & 0x80) && wParam == '2') {
	*p++ = 0x00;
	return p - output;
    }
    if (ret && (keystate[VK_CONTROL] & 0x80) && wParam == '6') {
	*p++ = 0x1E;
	return p - output;
    }
    if (ret && (keystate[VK_CONTROL] & 0x80) && wParam == 0xBD) {
	*p++ = 0x1F;
	return p - output;
    }
    if (ret && (keystate[VK_CONTROL] & 0x80) && wParam == 0xDF) {
	*p++ = 0x1C;
	return p - output;
    }

    /*
     * First, all the keys that do tilde codes. (ESC '[' nn '~',
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
      case VK_HOME: code = 1; break;
      case VK_INSERT: code = 2; break;
      case VK_DELETE: code = 3; break;
      case VK_END: code = 4; break;
      case VK_PRIOR: code = 5; break;
      case VK_NEXT: code = 6; break;
    }
    if (cfg.linux_funkeys && code >= 11 && code <= 15) {
	p += sprintf((char *)p, "\x1B[[%c", code + 'A' - 11);
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
    switch (wParam) {
      case VK_UP:
	p += sprintf((char *)p, app_cursor_keys ? "\x1BOA" : "\x1B[A");
	return p - output;
      case VK_DOWN:
	p += sprintf((char *)p, app_cursor_keys ? "\x1BOB" : "\x1B[B");
	return p - output;
      case VK_RIGHT:
	p += sprintf((char *)p, app_cursor_keys ? "\x1BOC" : "\x1B[C");
	return p - output;
      case VK_LEFT:
	p += sprintf((char *)p, app_cursor_keys ? "\x1BOD" : "\x1B[D");
	return p - output;
      case VK_CLEAR: p += sprintf((char *)p, "\x1B[G"); return p - output;
    }

    return 0;
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
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
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

void write_clip (void *data, int len) {
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

    SendMessage (hwnd, WM_IGNORE_CLIP, TRUE, 0);
    if (OpenClipboard (hwnd)) {
	EmptyClipboard();
	SetClipboardData (CF_TEXT, clipdata);
	CloseClipboard();
    } else
	GlobalFree (clipdata);
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
void beep(void) {
    MessageBeep(MB_OK);
}
