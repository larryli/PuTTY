/* $Id: mac_res.r,v 1.1.2.5 1999/02/28 02:38:40 ben Exp $ */
/* PuTTY resources */

#include "Types.r"
#include "Dialogs.r"
#include "Palettes.r"

/* Get resource IDs we share with C code */
#include "macresid.h"

/*
 * Finder-related resources
 */

/* 'pTTY' is now registered with Apple as PuTTY's signature */

type 'pTTY' as 'STR ';

resource 'pTTY' (0, purgeable) {
    "PuTTY experimental Mac port"
};

resource 'vers' (1, purgeable) {
    0x00, 0x45,		/* Major and minor (BCD) */
    development, 0,	/* Status and pre-release */
    2,			/* Region code (2 = UK) */
    "Mac exp",		/* Short version (list views) */
    "Mac experimental version.  "
    "Copyright Simon Tatham 1997-9",	/* Long version (get info) */
};

resource 'SIZE' (-1) {
    reserved,
    ignoreSuspendResumeEvents,
    reserved,
    cannotBackground,
    needsActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreAppDiedEvents,
    is32BitCompatible,
    notHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    useTextEditServices,
    reserved,
    reserved,
    reserved,
    1024 * 1024,	/* Minimum size */
    1024 * 1024,	/* Preferred size */
};

resource 'FREF' (128, purgeable) {
    /* The application itself */
    'APPL', 128, ""
};

resource 'FREF' (129, purgeable) {
    /* Saved session */
    'Sess', 129, ""
    };

resource 'FREF' (130, purgeable) {
    /* SSH host keys database */
    'HKey', 130, ""
};

resource 'BNDL' (128, purgeable) {
    'pTTY', 0,
    {
	'ICN#', {
	    128, 128,
	    129, 129,
	    130, 130
	},
	'FREF', {
	    128, 128,
	    129, 129,
	    130, 130
	};
    };
};

/* Icons, courtesy of DeRez */

/* Application icon */
resource 'ICN#' (128, purgeable) {
	{	/* array: 2 elements */
		/* [1] */
		$"00003FFE 00004001 00004FF9 00005005"
		$"00005355 00004505 00005A05 00002405"
		$"00004A85 00019005 000223F9 00047C01"
		$"00180201 7FA00C7D 801F1001 9FE22001"
		$"A00CDFFE AA892002 A0123FFE A82C0000"
		$"A0520000 AA6A0000 A00A0000 9FF20000"
		$"80020000 80020000 80FA0000 80020000"
		$"80020000 7FFC0000 40040000 7FFC",
		/* [2] */
		$"00003FFE 00007FFF 0000 7FFF 00007FFF"
		$"00007FFF 00007FFF 0000 7FFF 00007FFF"
		$"00007FFF 00007FFF 0000 7FFF 00007FFF"
		$"00007FFF 7FFC7FFF FFFE 7FFF FFFE7FFF"
		$"FFFE3FFE FFFE3FFE FFFE 3FFE FFFE0000"
		$"FFFE0000 FFFE0000 FFFE 0000 FFFE0000"
		$"FFFE0000 FFFE0000 FFFE 0000 FFFE0000"
		$"FFFE0000 7FFC0000 7FFC 0000 7FFC"
	}
};
resource 'ics#' (128, purgeable) {
	{	/* array: 2 elements */
		/* [1] */
		$"00FF 0081 00BD 00A5 00A5 00BD FF81 818D"
		$"BD81 A57E A500 BD00 8100 8D00 8100 7E",
		/* [2] */
		$"00FF 00FF 00FF 00FF 00FF 00FF FFFF FFFF"
		$"FFFF FF7E FF00 FF00 FF00 FF00 FF00 7E"
	}
};

/* Known hosts icon */
resource 'ICN#' (130, purgeable) {
	{	/* array: 2 elements */
		/* [1] */
		$"1FFFFC00 10000600 10000500 1FFFFC80"
		$"10000440 10000420 1FFFFFF0 10000010"
		$"13FC0F90 1C03F0F0 15FA8090 150A8090"
		$"1D0B80F0 150A8050 15FA8050 1C038070"
		$"143A8050 14028050 1FFFABF0 12048110"
		$"13FCFF10 1AAAAAB0 10000010 17FFFFD0"
		$"14000050 15252250 15555550 15252250"
		$"14000050 17FFFFD0 10000010 1FFFFFF0",
		/* [2] */
		$"1FFFFC00 1FFFFE00 1FFFFF00 1FFFFF80"
		$"1FFFFFC0 1FFFFFE0 1FFFFFF0 1FFFFFF0"
		$"1FFFFFF0 1FFFFFF0 1FFFFFF0 1FFFFFF0"
		$"1FFFFFF0 1FFFFFF0 1FFFFFF0 1FFFFFF0"
		$"1FFFFFF0 1FFFFFF0 1FFFFFF0 1FFFFFF0"
		$"1FFFFFF0 1FFFFFF0 1FFFFFF0 1FFFFFF0"
		$"1FFFFFF0 1FFFFFF0 1FFFFFF0 1FFFFFF0"
		$"1FFFFFF0 1FFFFFF0 1FFFFFF0 1FFFFFF0"
	}
};

resource 'icl4' (130, purgeable) {
	$"000FFFFFFFFFFFFFFFFFFF0000000000"
	$"000F00000000000000000FF000000000"
	$"000F00000000000000000FCF00000000"
	$"000FFFFFFFFFFFFFFFFFFFCCF0000000"
	$"000F00000000000000000FCCCF000000"
	$"000F00000000000000000FCCCCF00000"
	$"000FFFFFFFFFFFFFFFFFFFFFFFFF0000"
	$"000F00000000000000000000000F0000"
	$"000F00FFFFFFFF000000FFFFF00F0000"
	$"000FFFCCCCCCCCFFFFFFCCCCFFFF0000"
	$"000F0FCEEEEECCF0FCCCCCCCF00F0000"
	$"000F0FCE0D0D0CF0FCCCCCCCF00F0000"
	$"000FFFCED0D0CCFFFCCCCCCCFFFF0000"
	$"000F0FCE0D0D0CF0FCCCCCCCCF0F0000"
	$"000F0FCCC0C0CCF0FCCCCCCCCF0F0000"
	$"000FFFCCCCCCCCFFFCCCCCCCCFFF0000"
	$"000F0FCCCCFFFCF0FCCCCCCCCF0F0000"
	$"000F0FCCCCCCCCF0FCCCCCCCCF0F0000"
	$"000FFFFFFFFFFFFFFDDDDDDFFFFF0000"
	$"000F00FCCDDEEF00FDDDDDDF000F0000"
	$"000F00FFFFFFFF00FFFFFFFF000F0000"
	$"000F0C0C0C0C0C0C0C0C0C0C0C0F0000"
	$"000FC0C0C0C0C0C0C0C0C0C0C0CF0000"
	$"000F0FFFFFFFFFFFFFFFFFFFFF0F0000"
	$"000FCF0000000000000000000FCF0000"
	$"000F0F0F00F00F0F00F000F00F0F0000"
	$"000FCF0F0F0F0F0F0F0F0F0F0FCF0000"
	$"000F0F0F00F00F0F00F000F00F0F0000"
	$"000FCF0000000000000000000FCF0000"
	$"000F0FFFFFFFFFFFFFFFFFFFFF0F0000"
	$"000FC0C0C0C0C0C0C0C0C0C0C0CF0000"
	$"000FFFFFFFFFFFFFFFFFFFFFFFFF"
};
resource 'icl8' (130, purgeable) {
	$"000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF00000000000000000000"
	$"000000FF0000000000000000000000000000000000FFFF000000000000000000"
	$"000000FF0000000000000000000000000000000000FFF6FF0000000000000000"
	$"000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF6F6FF00000000000000"
	$"000000FF0000000000000000000000000000000000FFF6F6F6FF000000000000"
	$"000000FF0000000000000000000000000000000000FFF6F6F6F6FF0000000000"
	$"000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF00000000"
	$"000000FF0000000000000000000000000000000000000000000000FF00000000"
	$"000000FF0000FFFFFFFFFFFFFFFF000000000000FFFFFFFFFF0000FF00000000"
	$"000000FFFFFF2B2B2B2B2B2B2B2BFFFFFFFFFFFF2B2B2B2BFFFFFFFF00000000"
	$"000000FF00FF2BFCFCFCFCFCF82BFF00FF2B2B2B2B2B2B2BFF0000FF00000000"
	$"000000FF00FF2BFC2A2A2A2A002BFF00FF2B2B2B2B2B2B2BFF0000FF00000000"
	$"000000FFFFFF2BFC2A2A2A2A002BFFFFFF2B2B2B2B2B2B2BFFFFFFFF00000000"
	$"000000FF00FF2BFC2A2A2A2A002BFF00FF2B2B2B2B2B2B2B2BFF00FF00000000"
	$"000000FF00FF2BF800000000002BFF00FF2B2B2B2B2B2B2B2BFF00FF00000000"
	$"000000FFFFFF2B2B2B2B2B2B2B2BFFFFFF2B2B2B2B2B2B2B2BFFFFFF00000000"
	$"000000FF00FF2B2B2B2BFFFFFF2BFF00FF2B2B2B2B2B2B2B2BFF00FF00000000"
	$"000000FF00FF2B2B2B2B2B2B2B2BFF00FF2B2B2B2B2B2B2B2BFF00FF00000000"
	$"000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFF9F9F9F9F9F9FFFFFFFFFF00000000"
	$"000000FF0000FFF7F8F9FAFBFCFF0000FFF9F9F9F9F9F9FF000000FF00000000"
	$"000000FF0000FFFFFFFFFFFFFFFF0000FFFFFFFFFFFFFFFF000000FF00000000"
	$"000000FFF5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5FF00000000"
	$"000000FFF5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5FF00000000"
	$"000000FFF5FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF5FF00000000"
	$"000000FFF5FF00000000000000000000000000000000000000FFF5FF00000000"
	$"000000FFF5FF00FF0000FF0000FF00FF0000FF000000FF0000FFF5FF00000000"
	$"000000FFF5FF00FF00FF00FF00FF00FF00FF00FF00FF00FF00FFF5FF00000000"
	$"000000FFF5FF00FF0000FF0000FF00FF0000FF000000FF0000FFF5FF00000000"
	$"000000FFF5FF00000000000000000000000000000000000000FFF5FF00000000"
	$"000000FFF5FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF5FF00000000"
	$"000000FFF5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5F5FF00000000"
	$"000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
};


/*
 * Internal resources
 */

/* Menu bar */

resource 'MBAR' (MBAR_Main, preload) {
    { mApple, mFile }
};

resource 'MENU' (mApple, preload) {
    mApple,
    textMenuProc,
    0b11111111111111111111111111111101,
    enabled,
    apple,
    {
	"About PuTTYÉ",		noicon, nokey, nomark, plain,
	"-",			noicon, nokey, nomark, plain,
    }
};

resource 'MENU' (mFile, preload) {
    mFile,
    textMenuProc,
    0b11111111111111111111111111111011,
    enabled,
    "File",
    {
	"New Session",		noicon, "N",   nomark, plain,
	"Close",		noicon, "W",   nomark, plain,
	"-",			noicon, nokey, nomark, plain,
	"Quit",			noicon, "Q",   nomark, plain,
    }
};

/* Fatal error box.  Stolen from the Finder. */

resource 'ALRT' (wFatal, "fatalbox", purgeable) {
	{54, 67, 152, 435},
	wFatal,
	beepStages,
	alertPositionMainScreen
};

resource 'DITL' (wFatal, "fatalbox", purgeable) {
	{	/* array DITLarray: 3 elements */
		/* [1] */
		{68, 299, 88, 358},
		Button {
			enabled,
			"OK"
		},
		/* [2] */
		{68, 227, 88, 286},
		StaticText {
			disabled,
			""
		},
		/* [3] */
		{7, 74, 55, 358},
		StaticText {
			disabled,
			"^0"
		}
	}
};

/* Terminal window */

resource 'WIND' (wTerminal, "terminal", purgeable) {
    { 0, 0, 200, 200 },
    zoomDocProc,
    invisible,
    goAway,
    0x0,
    "untitled",
    staggerParentWindowScreen
};

resource 'CNTL' (cVScroll, "vscroll", purgeable) {
    { 0, 0, 48, 16 },
    0, invisible, 0, 0,
    scrollBarProc, 0, ""
};

/* "About" box */

resource 'DLOG' (wAbout, "about", purgeable) {
    { 0, 0, 120, 186 },
    noGrowDocProc,
    visible,
    goAway,
    wAbout,		/* RefCon -- identifies the window to PuTTY */
    wAbout,		/* DITL ID */
    "About PuTTY",
    alertPositionMainScreen
};

resource 'dlgx' (wAbout, "about", purgeable) {
    versionZero {
	kDialogFlagsUseThemeBackground | kDialogFlagsUseThemeControls
    }
};

resource 'DITL' (wAbout, "about", purgeable) {
    {
	{ 87, 13, 107, 173 },
	Button { enabled, "View Licence" },
	{ 13, 13, 29, 173 },
	StaticText { disabled, "PuTTY"},
	{ 42, 13, 74, 173 },
	StaticText { disabled, "Experimental Mac Port\n"
			       "© 1997-9 Simon Tatham"},
    }
};

/* Licence box */

resource 'WIND' (wLicence, "licence", purgeable) {
    { 0, 0, 300, 300 },
    noGrowDocProc,
    visible,
    goAway,
    wLicence,
    "PuTTY Licence",
    alertPositionParentWindowScreen
};

type 'TEXT' {
    string;
};

resource 'TEXT' (wLicence, "licence", purgeable) {
    "Copyright © 1997-9 Simon Tatham\n"
    "Portions copyright Gary S. Brown and Eric Young\n\n"
    
    "Permission is hereby granted, free of charge, to any person "
    "obtaining a copy of this software and associated documentation "
    "files (the \"Software\"), to deal in the Software without "
    "restriction, including without limitation the rights to use, "
    "copy, modify, merge, publish, distribute, sublicense, and/or "
    "sell copies of the Software, and to permit persons to whom the "
    "Software is furnished to do so, subject to the following "
    "conditions:\n\n"
    
    "The above copyright notice and this permission notice shall be "
    "included in all copies or substantial portions of the Software.\n\n"
    
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, "
    "EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF "
    "MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND "
    "NONINFRINGEMENT.  IN NO EVENT SHALL SIMON TATHAM BE LIABLE FOR "
    "ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF "
    "CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN "
    "CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE "
    "SOFTWARE."
};

/* Default Preferences */

type PREF_wordness_type {
    wide array [256] {
        integer;
    };
};

/*
 * This resource collects together the various short settings we need.
 * Each area of the system gets its own longword for flags, and then
 * we have the other settings.  Strings are stored as two shorts --
 * the id of a STR# or STR resource, and the index if it's a STR# (0
 * for STR).
 */

type 'pSET' {
    /* Basic boolean options */
    boolean dont_close_on_exit, close_on_exit;
    align long;
    /* SSH options */
    boolean use_pty, no_pty;
    align long;
    /* Telnet options */
    boolean bsd_environ, rfc_environ;
    align long;
    /* Keyboard options */
    boolean backspace, delete;
    boolean std_home_end, rxvt_home_end;
    boolean std_funkeys, linux_funkeys;
    boolean normal_cursor, app_cursor;
    boolean normal_keypad, app_keypad;
    align long;
    /* Terminal options */
    boolean no_dec_om, dec_om;
    boolean no_auto_wrap, auto_wrap;
    boolean no_auto_cr, auto_cr;
    boolean use_icon_name, win_name_always;
    align long;
    /* Colour options */
    boolean bold_font, bold_colour;
    align long;
    /* Non-boolean options */
    integer; integer;		/* host */
    longint;			/* port */
    longint prot_telnet = 0, prot_ssh = 1; /* protocol */
    integer; integer;		/* termtype */
    integer; integer;		/* termspeed */
    integer; integer;		/* environmt */
    integer; integer;		/* username */
    longint;			/* width */
    longint;			/* height */
    longint;			/* save_lines */
    integer; unsigned integer;	/* font */
    longint;			/* font_height */
    integer;			/* 'pltt' for colours */
    integer;			/* 'wORD' for wordness */
};

resource 'pSET' (PREF_settings, "settings", purgeable) {
    close_on_exit,
    use_pty,
    rfc_environ,
    delete,
    std_home_end,
    std_funkeys,
    normal_cursor,
    normal_keypad,
    no_dec_om,
    auto_wrap,
    no_auto_cr,
    use_icon_name,
    bold_colour,
#define PREF_strings 1024
    PREF_strings, 1,		/* host 'STR#' */
    23, prot_telnet,		/* port, protocol */
    PREF_strings, 2,		/* termtype 'STR#' */
    PREF_strings, 3,		/* termspeed 'STR#' */
    PREF_strings, 4,		/* environmt 'STR#' */
    PREF_strings, 5,		/* username */
    80, 24, 200,    		/* width, height, save_lines */
    PREF_strings, 6,		/* font 'STR#' */
    9,				/* font_height */
#define PREF_pltt 1024
    PREF_pltt,			/* colours 'pltt' */
#define PREF_wordness 1024
    PREF_wordness,		/* wordness 'wORD */
};

resource 'STR#' (PREF_strings, "strings", purgeable) {
    {
	"nowhere.loopback.edu",
	"xterm",
	"38400,38400",
	"\000",
	"",
	"Monaco",
    }
};

resource PREF_wordness_type (PREF_wordness, "wordness", purgeable) {
    {
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,1,2,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,1,
	1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,2,
	1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2
    }
};

/*
 * and _why_ isn't this provided for us?
 */
type 'TMPL' {
    array {
	pstring;		/* Item name */
	literal longint;	/* Item type */
    };
};

resource 'TMPL' (128, "pSET") {
    {
	"Close on exit", 'BBIT',
	"", 'BBIT', /* Must pad to a multiple of 8 */
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'ALNG',
	"No PTY", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'ALNG',
	"RFC OLD-ENVIRON", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'ALNG',
	"Delete key sends delete", 'BBIT',
	"rxvt home/end", 'BBIT',
	"Linux function keys", 'BBIT',
	"Application cursor keys", 'BBIT',
	"Application keypad", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'ALNG',
	"Use colour for bold", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'BBIT',
	"", 'ALNG',
	"Host STR# ID", 'DWRD',
	"Host STR# index", 'DWRD',
	"Port", 'DLNG',
	"Protocol", 'DLNG',
	"Termspeed STR# ID", 'DWRD',
	"Termspeed STR# index", 'DWRD',
	"Environ STR# ID", 'DWRD',
	"Environ STR# index", 'DWRD',
	"Username STR# ID", 'DWRD',
	"Username STR# index", 'DWRD',
	"Terminal width", 'DLNG',
	"Terminal height", 'DLNG',
	"Save lines", 'DLNG',
	"Font STR# ID", 'DWRD',
	"Font STR# index", 'DWRD',
	"Font size", 'DLNG',
	"pltt ID", 'DWRD',
	"wORD ID", 'DWRD',
    };
};

resource 'pltt' (PREF_pltt, purgeable) {
    {
	0x0000, 0x0000, 0x0000, pmTolerant, 0x2000,	/* black */
	0x5555, 0x5555, 0x5555, pmTolerant, 0x2000,	/* bright black */
	0xbbbb, 0x0000, 0x0000, pmTolerant, 0x2000,	/* red */
	0xffff, 0x5555, 0x5555, pmTolerant, 0x2000,	/* bright red */
	0x0000, 0xbbbb, 0x0000, pmTolerant, 0x2000,	/* green */
	0x5555, 0xffff, 0x5555, pmTolerant, 0x2000,	/* bright green */
	0xbbbb, 0xbbbb, 0x0000, pmTolerant, 0x2000,	/* yellow */
	0xffff, 0xffff, 0x0000, pmTolerant, 0x2000,	/* bright yellow */
	0x0000, 0x0000, 0xbbbb, pmTolerant, 0x2000,	/* blue */
	0x5555, 0x5555, 0xffff, pmTolerant, 0x2000,	/* bright blue */
	0xbbbb, 0x0000, 0xbbbb, pmTolerant, 0x2000,	/* magenta */
	0xffff, 0x5555, 0xffff, pmTolerant, 0x2000,	/* bright magenta */
	0x0000, 0xbbbb, 0xbbbb, pmTolerant, 0x2000,	/* cyan */
	0x5555, 0xffff, 0xffff, pmTolerant, 0x2000,	/* bright cyan */
	0xbbbb, 0xbbbb, 0xbbbb, pmTolerant, 0x2000,	/* white */
	0xffff, 0xffff, 0xffff, pmTolerant, 0x2000,	/* bright white */
	0xbbbb, 0xbbbb, 0xbbbb, pmTolerant, 0x2000,	/* default fg */
	0xffff, 0xffff, 0xffff, pmTolerant, 0x2000,	/* default bold fg */
	0x0000, 0x0000, 0x0000, pmTolerant, 0x2000,	/* default bg */
	0x5555, 0x5555, 0x5555, pmTolerant, 0x2000,	/* default bold bg */
	0x0000, 0x0000, 0x0000, pmTolerant, 0x2000,	/* cursor bg */
	0x0000, 0x0000, 0x0000, pmTolerant, 0x2000,	/* bold cursor bg */
	0x0000, 0xffff, 0x0000, pmTolerant, 0x2000,	/* cursor fg */
	0x0000, 0xffff, 0x0000, pmTolerant, 0x2000,	/* bold cursor fg */
    }
};

