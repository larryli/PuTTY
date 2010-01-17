/* $Id$ */
/*
 * Copyright (c) 1999, 2002, 2003 Ben Harris
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* PuTTYgen resources */

/*
 * The space after the # for system includes is to stop mkfiles.pl
 * trying to chase them (Rez doesn't support the angle-bracket
 * syntax).
 */

# include "Types.r"
# include "Dialogs.r"
# include "Palettes.r"
# include "Script.r"


/* Get resource IDs we share with C code */
#include "macpgrid.h"

#include "version.r"

/*
 * Finder-related resources
 */

/* 'pGen' is now registered with Apple as PuTTYgen's signature */

type 'pGen' as 'STR ';

resource 'pGen' (0, purgeable) {
    "PuTTYgen experimental Mac port"
};

resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreAppDiedEvents,
    is32BitCompatible,
    isHighLevelEventAware,
    localandRemoteHLEvents,
    isStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
    1024 * 1024,	/* Minimum size */
    1024 * 1024,	/* Preferred size */
};

#define FREF_APPL 128
#define FREF_Seed 132

resource 'FREF' (FREF_APPL, purgeable) {
    /* The application itself */
    'APPL', FREF_APPL, ""
};

resource 'FREF' (FREF_Seed, purgeable) {
    /* Random seed */
    'Seed', FREF_Seed, ""
};

/* "Internal" file types, which can't be opened */
resource 'BNDL' (129, purgeable) {
    'pTTI', 0,
    {
	'ICN#', {
	    FREF_Seed, FREF_Seed,
	},
	'FREF', {
	    FREF_Seed, FREF_Seed,
	};
    };
};

resource 'kind' (129) {
    'pTTI',
    verBritain,
    {
	'Seed', "PuTTY random number seed",
    }
};

#if TARGET_API_MAC_CARBON
/*
 * Mac OS X Info.plist.
 * See Tech Note TN2013 for details.
 * We don't bother with things that Mac OS X seems to be able to get from
 * other resources.
 */
type 'plst' as 'TEXT';

resource 'plst' (0) {
    "<?xml version='1.0' encoding='UTF-8'?>\n"
    "<!DOCTYPE plist PUBLIC '-//Apple Computer//DTD PLIST 1.0//EN'\n"
    " 'http://www.apple.com/DTDs/PropertyList-1.0.dtd'>\n"
    "<plist version='1.0'>\n"
    "  <dict>\n"
    "    <key>CFBundleInfoDictionaryVersion</key> <string>6.0</string>\n"
    "    <key>CFBundleIdentifier</key>\n"
    "      <string>org.tartarus.projects.putty.puttygen</string>\n"
    "    <key>CFBundleName</key>                  <string>PuTTYgen</string>\n"
    "    <key>CFBundlePackageType</key>           <string>APPL</string>\n"
    "    <key>CFBundleSignature</key>             <string>pGen</string>\n"
    "  </dict>\n"
    "</plist>\n"
};

/* Mac OS X doesn't use this, but Mac OS 9 does. */
type 'carb' as 'TEXT';
resource 'carb' (0) { "" };
#endif

/* Icons, courtesy of DeRez */

/* Random seed icon */

resource 'ICN#' (FREF_Seed, purgeable) {
	{	/* array: 2 elements */
		/* [1] */
		$"1FFFFC00 18F36600 161EF500 1CC92C80"
		$"1CF2EC40 10662C20 108E07F0 151F0490"
		$"1E00C4F0 1803BBD0 1FC5BE10 108B5A90"
		$"1B3C4F50 1267AC90 14B60470 1BB791B0"
		$"17F4D2B0 1DC1F830 1B029450 1B753DD0"
		$"145A8170 11390DD0 1E15A8B0 1CC4CD90"
		$"154ECED0 15C9CF30 172CDB50 12617970"
		$"15E45C90 1D4B9890 15CE4430 1FFFFFF0",
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
resource 'icl4' (FREF_Seed) {
	$"000FFFFFFFFFFFFFFFFFFF0000000000"
	$"000FFC0CFFFF0CFF1FFC0FF000000000"
	$"000F0FF0C0CFFFF1FFFFCFCF00000000"
	$"000FFF0CFF0CF11F0CFCFFCCF0000000"
	$"000FFFC0FFFF11F0FFF0FFCCCF000000"
	$"000F0C0C0FF11FFC0CFCFFCCCCF00000"
	$"000FC0C0F111FFF0C0C0CFFFFFFF0000"
	$"000F0F0F111FFFFF0C0C0F0CFC0F0000"
	$"000FFFF111111111FFC0CFC0FFFF0000"
	$"000FF111111111FFFCFFF0FFFF0F0000"
	$"000FFFFFFF111FCFF0FFFFF0C0CF0000"
	$"000F0C0CF111FCFF0F0FFCFCFC0F0000"
	$"000FF0FF11FFFFC0CFC0FFFFCFCF0000"
	$"000F0CF11FFC0FFFFCFCFF0CFC0F0000"
	$"000FCF11F0FFCFF0C0C0CFC0CFFF0000"
	$"000FF1FFFCFF0FFFFC0F0C0FFCFF0000"
	$"000F1FFFFFFFCFC0FFCFC0F0F0FF0000"
	$"000FFF0FFF0C0C0FFFFFFC0C0CFF0000"
	$"000FF0FFC0C0C0F0F0CF0FC0CFCF0000"
	$"000FFCFF0FFF0F0F0CFFFF0FFF0F0000"
	$"000FCFC0CF0FF0F0F0C0C0CFCFFF0000"
	$"000F0C0F0CFFFC0F0C0CFF0FFF0F0000"
	$"000FFFF0C0CFCFCFF0F0F0C0F0FF0000"
	$"000FFF0CFF0C0F0CFF0CFF0FFC0F0000"
	$"000FCFCF0FC0FFF0FFC0FFF0FFCF0000"
	$"000F0F0FFF0CFC0FFF0CFFFF0CFF0000"
	$"000FCFFFC0F0FFC0FFCFF0FFCFCF0000"
	$"000F0CFC0FFC0C0F0FFFFC0F0FFF0000"
	$"000FCFCFFFF0CFC0CFCFFFC0F0CF0000"
	$"000FFF0F0F0CF0FFFC0FFC0CFC0F0000"
	$"000FCFCFFFC0FFF0CFC0CFC0C0FF0000"
	$"000FFFFFFFFFFFFFFFFFFFFFFFFF"
};
resource 'icl8' (FREF_Seed) {
	$"000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF00000000000000000000"
	$"000000FFFFF5F5F5FFFFFFFFF5F5FFFF05FFFFF5F5FFFF000000000000000000"
	$"000000FFF5FFFFF5F5F5F5FFFFFFFF05FFFFFFFFF5FF2BFF0000000000000000"
	$"000000FFFFFFF5F5FFFFF5F5FF0505FF0000FFF5FFFF2B2BFF00000000000000"
	$"000000FFFFFFF5F5FFFFFFFF0505FFF5FFFFFFF5FFFF2B2B2BFF000000000000"
	$"000000FFF5F5F5F5F5FFFF0505FFFFF5F5F5FFF5FFFF2B2B2B2BFF0000000000"
	$"000000FFF5F5F5F5FF050505FFFFFFF5F5F5F5F5F5FFFFFFFFFFFFFF00000000"
	$"000000FFF5FFF5FF050505FFFFFFFFFFF5F5F5F5F5FFF5F5FFF5F5FF00000000"
	$"000000FFFFFFFF050505050505050505FFFFF5F5F5FFF5F5FFFFFFFF00000000"
	$"000000FFFF050505050505050505FFFFFFF5FFFFFFF5FFFFFFFFF5FF00000000"
	$"000000FFFFFFFFFFFFFF050505FFF5FFFFF5FFFFFFFFFFF5F5F5F5FF00000000"
	$"000000FFF5F5F5F5FF050505FFF5FFFFF5FFF5FFFFF5FFF5FFF5F5FF00000000"
	$"000000FFFFF5FFFF0505FFFFFFFFF5F5F5FFF5F5FFFFFFFFF5FFF5FF00000000"
	$"000000FFF5F5FF0505FFFFF5F5FFFFFFFFF5FFF5FFFFF5F5FFF5F5FF00000000"
	$"000000FFF5FF0505FFF5FFFFF5FFFFF5F5F5F5F5F5FFF5F5F5FFFFFF00000000"
	$"000000FFFF05FFFFFFF5FFFFF5FFFFFFFFF5F5FFF5F5F5FFFFF5FFFF00000000"
	$"000000FF05FFFFFFFFFFFFFFF5FFF5F5FFFFF5FFF5F5FFF5FFF5FFFF00000000"
	$"000000FFFFFFF5FFFFFFF5F5F5F5F5FFFFFFFFFFFFF5F5F5F5F5FFFF00000000"
	$"000000FFFFF5FFFFF5F5F5F5F5F5FF00FFF5F5FFF5FFF5F5F5FFF5FF00000000"
	$"000000FFFFF5FFFFF5FFFFFFF5FF00FFF5F5FFFFFFFFF5FFFFFFF5FF00000000"
	$"000000FFF5FFF5F5F5FFF5FFFF00FF00FFF5F5F5F5F5F5FFF5FFFFFF00000000"
	$"000000FFF5F5F5FFF5F5FFFFFF0000FFF5F5F5F5FFFFF5FFFFFF00FF00000000"
	$"000000FFFFFFFFF5F5F5F5FFF5FF00FFFFF5FFF5FFF5F5F5FF00FFFF00000000"
	$"000000FFFFFFF5F5FFFFF5F5F5FF0000FFFFF5F5FFFFF5FFFF0000FF00000000"
	$"000000FFF5FFF5FFF5FFF5F5FFFFFF00FFFFF5F5FFFFFFF5FFFF00FF00000000"
	$"000000FFF5FFF5FFFFFFF5F5FFF5F5FFFFFFF5F5FFFFFFFFF5F5FFFF00000000"
	$"000000FFF5FFFFFFF5F5FFF5FFFFF5F5FFFFF5FFFFF5FFFFF5FFF5FF00000000"
	$"000000FFF5F5FFF5F5FFFFF5F5F5F5FFF5FFFFFFFFF5F5FFF5FFFFFF00000000"
	$"000000FFF5FFF5FFFFFFFFF5F5FFF5F5F5FFF5FFFFFFF5F5FFF5F5FF00000000"
	$"000000FFFFFFF5FFF5FFF5F5FFF5FFFFFFF5F5FFFFF5F5F5FFF5F5FF00000000"
	$"000000FFF5FFF5FFFFFFF5F5FFFFFFF5F5FFF5F5F5FFF5F5F5F5FFFF00000000"
	$"000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
};
resource 'ics#' (FREF_Seed) {
	{	/* array: 2 elements */
		/* [1] */
		$"7FE0 56B0 59A8 637C 51DC 6794 59AC 76EC"
		$"7224 7C6C 743C 71AC 505C 459C 4424 7FFC",
		/* [2] */
		$"7FE0 7FF0 7FF8 7FFC 7FFC 7FFC 7FFC 7FFC"
		$"7FFC 7FFC 7FFC 7FFC 7FFC 7FFC 7FFC 7FFC"
	}
};
resource 'ics4' (FREF_Seed) {
	$"0FFFFFFFFFF00000"
	$"0F0F0FF1FCFF0000"
	$"0FCFF11FF0FCF000"
	$"0FF111FF0FFFFF00"
	$"0FCF111FFFCFFF00"
	$"0FF11FFFFC0F0F00"
	$"0F1FF0CFF0F0FF00"
	$"0FFF0FFCFFFCFF00"
	$"0FFFC0F0C0F0CF00"
	$"0FFFFF0C0FFCFF00"
	$"0FFFCFC0C0FFFF00"
	$"0FFF0C0FFCFCFF00"
	$"0FCFC0C0CFCFFF00"
	$"0F0C0F0FFC0FFF00"
	$"0FC0CFC0C0F0CF00"
	$"0FFFFFFFFFFFFF"
};
resource 'ics8' (FREF_Seed) {
	$"00FFFFFFFFFFFFFFFFFFFF0000000000"
	$"00FFF5FFF5FFFF05FFF5FFFF00000000"
	$"00FFF5FFFF0505FFFFF5FF2BFF000000"
	$"00FFFF050505FFFFF5FFFFFFFFFF0000"
	$"00FFF5FF050505FFFFFFF5FFFFFF0000"
	$"00FFFF0505FFFFFFFFF5F5FFF5FF0000"
	$"00FF05FFFFF5F5FFFFF5FFF5FFFF0000"
	$"00FFFFFFF5FFFFF5FFFFFFF5FFFF0000"
	$"00FFFFFFF5F5FFF5F5F5FFF5F5FF0000"
	$"00FFFFFFFFFFF5F5F5FFFFF5FFFF0000"
	$"00FFFFFFF5FFF5F5F5F5FFFFFFFF0000"
	$"00FFFFFFF5F5F5FFFFF5FFF5FFFF0000"
	$"00FFF5FFF5F5F5F5F5FFF5FFFFFF0000"
	$"00FFF5F5F5FFF5FFFFF5F5FFFFFF0000"
	$"00FFF5F5F5FFF5F5F5F5FFF5F5FF0000"
	$"00FFFFFFFFFFFFFFFFFFFFFFFFFF"
};

/*
 * Application-missing message string, for random seed and host key database
 * files.
 */
resource 'STR ' (-16397, purgeable) {
    "This file is used internally by PuTTY.  It cannot be opened."
};

/* Missing-application name string, for private keys. */
/* XXX Private keys should eventually be owned by Pageant */
resource 'STR ' (-16396, purgeable) {
    "PuTTYgen"
};

/*
 * Internal resources
 */

/* Menu bar */

resource 'MBAR' (MBAR_Main, preload) {
    { mApple, mFile, mEdit, mWindow }
};

resource 'MENU' (mApple, preload) {
    mApple,
    textMenuProc,
    0b11111111111111111111111111111101,
    enabled,
    apple,
    {
	"About PuTTYgen\0xc9",	noicon, nokey, nomark, plain,
	"-",			noicon, nokey, nomark, plain,
    }
};

resource 'MENU' (mFile, preload) {
    mFile,
    textMenuProc,
    0b11111111111111111111111101111011,
    enabled,
    "File",
    {
	"New",			noicon, "N",   nomark, plain,
	"Open\0xc9",		noicon, "O",   nomark, plain,
	"-",			noicon, nokey, nomark, plain,
	"Close",		noicon, "W",   nomark, plain,
	"Save",			noicon, "S",   nomark, plain,
	"Save As\0xc9",		noicon, nokey, nomark, plain,
	"-",			noicon, nokey, nomark, plain,
	"Quit",			noicon, "Q",   nomark, plain,
    }
};

resource 'MENU' (mEdit, preload) {
    mEdit,
    textMenuProc,
    0b11111111111111111111111111111101,
    enabled,
    "Edit",
    {
	"Undo",			noicon, "Z",   nomark, plain,
	"-",			noicon, nokey, nomark, plain,
	"Cut",			noicon, "X",   nomark, plain,
	"Copy",			noicon, "C",   nomark, plain,
	"Paste",		noicon, "V",   nomark, plain,
	"Clear",		noicon, nokey, nomark, plain,
	"Select All",		noicon, "A",   nomark, plain,
    }
};

resource 'MENU' (mWindow, preload) {
    mWindow,
    textMenuProc,
    0b11111111111111111111111111111111,
    enabled,
    "Window",
    {
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

/* "About" box */

resource 'DLOG' (wAbout, "about", purgeable) {
    { 0, 0, 120, 240 },
    noGrowDocProc,
    invisible,
    goAway,
    wAbout,		/* RefCon -- identifies the window to PuTTY */
    wAbout,		/* DITL ID */
    "About PuTTYgen",
    alertPositionMainScreen
};

resource 'dlgx' (wAbout, "about", purgeable) {
    versionZero {
	kDialogFlagsUseThemeBackground | kDialogFlagsUseThemeControls
    }
};

resource 'DITL' (wAbout, "about", purgeable) {
    {
	{ 87, 13, 107, 227 },
	Button { enabled, "View Licence" },
	{ 13, 13, 29, 227 },
	StaticText { disabled, "PuTTYgen"},
	{ 42, 13, 74, 227 },
	StaticText { disabled, "Some version or other\n"
			       "Copyright © 1997-2010 Simon Tatham"},
    }
};

/* Licence box */

resource 'WIND' (wLicence, "licence", purgeable) {
    { 0, 0, 250, 400 },
    noGrowDocProc,
    visible,
    goAway,
    wLicence,
    "PuTTYgen Licence",
    alertPositionParentWindowScreen
};

type 'TEXT' {
    string;
};

resource 'TEXT' (wLicence, "licence", purgeable) {
    "Copyright 1997-2010 Simon Tatham.\n"
    "\n"
    "Portions copyright Robert de Bath, Joris van Rantwijk, Delian "
    "Delchev, Andreas Schultz, Jeroen Massar, Wez Furlong, Nicolas Barry, "
    "Justin Bradford, Ben Harris, Malcolm Smith, Ahmad Khalifa, Markus "
    "Kuhn, Colin Watson, and CORE SDI S.A.\n"
    "\n"    
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

/* Key box */

resource 'DLOG' (wKey, "key", purgeable) {
    { 0, 0, 120, 240 },
    noGrowDocProc,
    invisible,
    goAway,
    wKey,		/* RefCon -- identifies the window to PuTTY */
    wKey,		/* DITL ID */
    "untitled",
    staggerParentWindowScreen
};

resource 'dlgx' (wKey, "key", purgeable) {
    versionZero {
	kDialogFlagsUseThemeBackground | kDialogFlagsUseThemeControls
    }
};

#define cProgress 129

resource 'DITL' (wKey, "key", purgeable) {
    {
	{ 13, 13, 33, 227 },
	Button { enabled, "Generate" },
	{ 46, 13, 12, 227 },
	Control { enabled, cProgress },
    }
};

resource 'CNTL' (cProgress) {
    { 46, 13, 12, 227 },
    0, visible, 0, 0,
    kControlProgressBarProc, 0, ""
};
