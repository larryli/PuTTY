/* $Id: putty.r,v 1.1.2.6 1999/02/20 23:55:55 ben Exp $ */
/* PuTTY resources */

#define PICT_RezTemplateVersion 1

#include "Types.r"

/* Get resource IDs we share with C code */
#include "macresid.h"

/*
 * Finder-related resources
 */

/* For now, PuTTY uses the signature "pTTY" */

type 'pTTY' as 'STR ';

resource 'pTTY' (0, purgeable) {
    "PuTTY experimental Mac port"
};

resource 'vers' (1, purgeable) {
    0x00, 0x45,		/* Major and minor (BCD) */
    development, 0,	/* Status and pre-release */
    2,			/* Region code (2 = UK) */
    "Mac exp",		/* Short version (list views) */
    "Mac experimental",	/* Long version (get info) */
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
    65536,		/* Minimum size */
    65536,		/* Preferred size */
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
		$"0000 3FFE 0000 4001 0000 4FF9 0000 5005"
		$"0000 5545 0000 5005 0000 5405 0000 5005"
		$"0000 5505 0000 5005 0000 4FF9 0000 4001"
		$"0000 4001 7FFC 407D 8002 4001 9FF2 4001"
		$"A00A 3FFE AA8A 2002 A00A 3FFE A80A 0000"
		$"A00A 0000 AA0A 0000 A00A 0000 9FF2 0000"
		$"8002 0000 8002 0000 80FA 0000 8002 0000"
		$"8002 0000 7FFC 0000 4004 0000 7FFC",
		/* [2] */
		$"0000 3FFE 0000 7FFF 0000 7FFF 0000 7FFF"
		$"0000 7FFF 0000 7FFF 0000 7FFF 0000 7FFF"
		$"0000 7FFF 0000 7FFF 0000 7FFF 0000 7FFF"
		$"0000 7FFF 7FFC 7FFF FFFE 7FFF FFFE 7FFF"
		$"FFFE 3FFE FFFE 3FFE FFFE 3FFE FFFE 0000"
		$"FFFE 0000 FFFE 0000 FFFE 0000 FFFE 0000"
		$"FFFE 0000 FFFE 0000 FFFE 0000 FFFE 0000"
		$"FFFE 0000 7FFC 0000 7FFC 0000 7FFC"
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
		$"1FFF FC00 1000 0600 1000 0500 1FFF FC80"
		$"1000 0440 1000 0420 1FFF FFF0 1000 0010"
		$"13FC 0F90 1C03 F0F0 15FA 8090 150A 8090"
		$"1D0B 80F0 150A 8050 15FA 8050 1C03 8070"
		$"143A 8050 1402 8050 1FFF ABF0 1204 8110"
		$"13FC FF10 1AAA AAB0 1000 0010 17FF FFD0"
		$"1400 0050 1525 2250 1555 5550 1525 2250"
		$"1400 0050 17FF FFD0 1000 0010 1FFF FFF0",
		/* [2] */
		$"1FFF FC00 1FFF FE00 1FFF FF00 1FFF FF80"
		$"1FFF FFC0 1FFF FFE0 1FFF FFF0 1FFF FFF0"
		$"1FFF FFF0 1FFF FFF0 1FFF FFF0 1FFF FFF0"
		$"1FFF FFF0 1FFF FFF0 1FFF FFF0 1FFF FFF0"
		$"1FFF FFF0 1FFF FFF0 1FFF FFF0 1FFF FFF0"
		$"1FFF FFF0 1FFF FFF0 1FFF FFF0 1FFF FFF0"
		$"1FFF FFF0 1FFF FFF0 1FFF FFF0 1FFF FFF0"
		$"1FFF FFF0 1FFF FFF0 1FFF FFF0 1FFF FFF0"
	}
};
resource 'icl4' (130, purgeable) {
	$"000F FFFF FFFF FFFF FFFF FF00 0000 0000"
	$"000F 0000 0000 0000 0000 0FF0 0000 0000"
	$"000F 0000 0000 0000 0000 0FCF 0000 0000"
	$"000F FFFF FFFF FFFF FFFF FFCC F000 0000"
	$"000F 0000 0000 0000 0000 0FCC CF00 0000"
	$"000F 0000 0000 0000 0000 0FCC CCF0 0000"
	$"000F FFFF FFFF FFFF FFFF FFFF FFFF 0000"
	$"000F 0000 0000 0000 0000 0000 000F 0000"
	$"000F 00FF FFFF FF00 0000 FFFF F00F 0000"
	$"000F FFCC CCCC CCFF FFFF CCCC FFFF 0000"
	$"000F 0FCE EEEE CCF0 FCCC CCCC F00F 0000"
	$"000F 0FCE 0D0D 0CF0 FCCC CCCC F00F 0000"
	$"000F FFCE D0D0 CCFF FCCC CCCC FFFF 0000"
	$"000F 0FCE 0D0D 0CF0 FCCC CCCC CF0F 0000"
	$"000F 0FCC C0C0 CCF0 FCCC CCCC CF0F 0000"
	$"000F FFCC CCCC CCFF FCCC CCCC CFFF 0000"
	$"000F 0FCC CCFF FCF0 FCCC CCCC CF0F 0000"
	$"000F 0FCC CCCC CCF0 FCCC CCCC CF0F 0000"
	$"000F FFFF FFFF FFFF FDDD DDDF FFFF 0000"
	$"000F 00FC CDDE EF00 FDDD DDDF 000F 0000"
	$"000F 00FF FFFF FF00 FFFF FFFF 000F 0000"
	$"000F 0C0C 0C0C 0C0C 0C0C 0C0C 0C0F 0000"
	$"000F C0C0 C0C0 C0C0 C0C0 C0C0 C0CF 0000"
	$"000F 0FFF FFFF FFFF FFFF FFFF FF0F 0000"
	$"000F CF00 0000 0000 0000 0000 0FCF 0000"
	$"000F 0F0F 00F0 0F0F 00F0 00F0 0F0F 0000"
	$"000F CF0F 0F0F 0F0F 0F0F 0F0F 0FCF 0000"
	$"000F 0F0F 00F0 0F0F 00F0 00F0 0F0F 0000"
	$"000F CF00 0000 0000 0000 0000 0FCF 0000"
	$"000F 0FFF FFFF FFFF FFFF FFFF FF0F 0000"
	$"000F C0C0 C0C0 C0C0 C0C0 C0C0 C0CF 0000"
	$"000F FFFF FFFF FFFF FFFF FFFF FFFF"
};
resource 'icl8' (130, purgeable) {
	$"0000 00FF FFFF FFFF FFFF FFFF FFFF FFFF"
	$"FFFF FFFF FFFF 0000 0000 0000 0000 0000"
	$"0000 00FF 0000 0000 0000 0000 0000 0000"
	$"0000 0000 00FF FF00 0000 0000 0000 0000"
	$"0000 00FF 0000 0000 0000 0000 0000 0000"
	$"0000 0000 00FF F6FF 0000 0000 0000 0000"
	$"0000 00FF FFFF FFFF FFFF FFFF FFFF FFFF"
	$"FFFF FFFF FFFF F6F6 FF00 0000 0000 0000"
	$"0000 00FF 0000 0000 0000 0000 0000 0000"
	$"0000 0000 00FF F6F6 F6FF 0000 0000 0000"
	$"0000 00FF 0000 0000 0000 0000 0000 0000"
	$"0000 0000 00FF F6F6 F6F6 FF00 0000 0000"
	$"0000 00FF FFFF FFFF FFFF FFFF FFFF FFFF"
	$"FFFF FFFF FFFF FFFF FFFF FFFF 0000 0000"
	$"0000 00FF 0000 0000 0000 0000 0000 0000"
	$"0000 0000 0000 0000 0000 00FF 0000 0000"
	$"0000 00FF 0000 FFFF FFFF FFFF FFFF 0000"
	$"0000 0000 FFFF FFFF FF00 00FF 0000 0000"
	$"0000 00FF FFFF 2B2B 2B2B 2B2B 2B2B FFFF"
	$"FFFF FFFF 2B2B 2B2B FFFF FFFF 0000 0000"
	$"0000 00FF 00FF 2BFC FCFC FCFC F82B FF00"
	$"FF2B 2B2B 2B2B 2B2B FF00 00FF 0000 0000"
	$"0000 00FF 00FF 2BFC 2A2A 2A2A 002B FF00"
	$"FF2B 2B2B 2B2B 2B2B FF00 00FF 0000 0000"
	$"0000 00FF FFFF 2BFC 2A2A 2A2A 002B FFFF"
	$"FF2B 2B2B 2B2B 2B2B FFFF FFFF 0000 0000"
	$"0000 00FF 00FF 2BFC 2A2A 2A2A 002B FF00"
	$"FF2B 2B2B 2B2B 2B2B 2BFF 00FF 0000 0000"
	$"0000 00FF 00FF 2BF8 0000 0000 002B FF00"
	$"FF2B 2B2B 2B2B 2B2B 2BFF 00FF 0000 0000"
	$"0000 00FF FFFF 2B2B 2B2B 2B2B 2B2B FFFF"
	$"FF2B 2B2B 2B2B 2B2B 2BFF FFFF 0000 0000"
	$"0000 00FF 00FF 2B2B 2B2B FFFF FF2B FF00"
	$"FF2B 2B2B 2B2B 2B2B 2BFF 00FF 0000 0000"
	$"0000 00FF 00FF 2B2B 2B2B 2B2B 2B2B FF00"
	$"FF2B 2B2B 2B2B 2B2B 2BFF 00FF 0000 0000"
	$"0000 00FF FFFF FFFF FFFF FFFF FFFF FFFF"
	$"FFF9 F9F9 F9F9 F9FF FFFF FFFF 0000 0000"
	$"0000 00FF 0000 FFF7 F8F9 FAFB FCFF 0000"
	$"FFF9 F9F9 F9F9 F9FF 0000 00FF 0000 0000"
	$"0000 00FF 0000 FFFF FFFF FFFF FFFF 0000"
	$"FFFF FFFF FFFF FFFF 0000 00FF 0000 0000"
	$"0000 00FF F5F5 F5F5 F5F5 F5F5 F5F5 F5F5"
	$"F5F5 F5F5 F5F5 F5F5 F5F5 F5FF 0000 0000"
	$"0000 00FF F5F5 F5F5 F5F5 F5F5 F5F5 F5F5"
	$"F5F5 F5F5 F5F5 F5F5 F5F5 F5FF 0000 0000"
	$"0000 00FF F5FF FFFF FFFF FFFF FFFF FFFF"
	$"FFFF FFFF FFFF FFFF FFFF F5FF 0000 0000"
	$"0000 00FF F5FF 0000 0000 0000 0000 0000"
	$"0000 0000 0000 0000 00FF F5FF 0000 0000"
	$"0000 00FF F5FF 00FF 0000 FF00 00FF 00FF"
	$"0000 FF00 0000 FF00 00FF F5FF 0000 0000"
	$"0000 00FF F5FF 00FF 00FF 00FF 00FF 00FF"
	$"00FF 00FF 00FF 00FF 00FF F5FF 0000 0000"
	$"0000 00FF F5FF 00FF 0000 FF00 00FF 00FF"
	$"0000 FF00 0000 FF00 00FF F5FF 0000 0000"
	$"0000 00FF F5FF 0000 0000 0000 0000 0000"
	$"0000 0000 0000 0000 00FF F5FF 0000 0000"
	$"0000 00FF F5FF FFFF FFFF FFFF FFFF FFFF"
	$"FFFF FFFF FFFF FFFF FFFF F5FF 0000 0000"
	$"0000 00FF F5F5 F5F5 F5F5 F5F5 F5F5 F5F5"
	$"F5F5 F5F5 F5F5 F5F5 F5F5 F5FF 0000 0000"
	$"0000 00FF FFFF FFFF FFFF FFFF FFFF FFFF"
	$"FFFF FFFF FFFF FFFF FFFF FFFF"
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
    0b11111111111111111111111111111101,
    enabled,
    "File",
    {
	"New Session"		noicon, "N",   nomark, plain,
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
    { 0, 0, 0, 0 },
    zoomDocProc,
    invisible,
    goAway,
    0x0,
    "untitled"
    staggerParentWindowScreen
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

resource 'DITL' (wAbout, "about", purgeable) {
    {
	{ 87, 116, 107, 173 },
	Button { enabled, "Close" },
	{ 87, 13, 107, 103 },
	Button { enabled, "View Licence" },
	{ 13, 13, 29, 173 },
	StaticText { disabled, "PuTTY"},
	{ 42, 13, 74, 173 },
	StaticText { disabled, "Mac Development\n© 1997-9 Simon Tatham"},
    }
};

/* Licence box */

resource 'DLOG' (wLicence, "licence", purgeable) {
    { 0, 0, 300, 300 },
    noGrowDocProc,
    visible,
    goAway,
    wLicence,
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

#if 0
resource 'DITL' (wLicence, "licence", purgeable) {
    {
	{ 13, 23, 287, 277 },
	Picture { enabled, wLicence }
    }
};

resource 'PICT' (wLicence, "licence", purgeable) {
    { 0, 0, 274, 254 },
     VersionTwo {
 	{
	    LongText { { 16, 0 }, "Copyright © 1997-9 Simon Tatham" },
 	    LongText { { 32, 0 }, "Portions copyright Gary S. Brown and Eric Young" },
 	}
    }
};
#endif
