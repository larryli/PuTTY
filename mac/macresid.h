/* $Id$ */

/*
 * macresid.h -- Mac resource IDs
 *
 * This file is shared by C and Rez source files
 */

/* TMPL for saved sessions */
#define TMPL_Int	128

/* Menu bar IDs */
#define MBAR_Main	128

/* Open IDs */
#define open_pTTY	128

/* Menu IDs */
#define mApple		128
#define mFile		129
#define mEdit		130
#define mWindow		131

/* Menu Items */
/* Apple menu */
#define iAbout		1
/* File menu */
#define iNew		1
#define iOpen		2
#define iChange         3
#define iClose		5
#define iSave		6
#define iSaveAs		7
#define iDuplicate	8
#define iQuit		10
/* Edit menu */
#define iUndo		1
#define iCut		3
#define iCopy		4
#define iPaste		5
#define iClear		6
#define iSelectAll	7
/* Window menu */
#define iShowEventLog	1

/* Window types (and resource IDs) */
#define wNone		0 /* Dummy value for no window */
#define wDA		1 /* Dummy value for desk accessory */
#define wFatal		128
#define wAbout		129
#define wiAboutLicence		1
#define wiAboutVersion		3
#define wTerminal	130
#define wLicence	131
#define wSettings	132
#define wiSettingsOpen		1
#define wEventLog	133
#define wQuestion	134

/* Controls */
#define cVScroll	128

/* ldes for list box controls */
#define ldes_Default	128

/* xDEFs */
#define CDEF_EditBox	129
#define SYS7_EDITBOX_VARIANT	0
#define SYS7_TEXT_VARIANT	1
#define SYS7_EDITBOX_PROC	((CDEF_EditBox << 4) + SYS7_EDITBOX_VARIANT)
#define SYS7_TEXT_PROC		((CDEF_EditBox << 4) + SYS7_TEXT_VARIANT)
#define CDEF_Default	130
#define SYS7_DEFAULT_PROC	(CDEF_Default << 4)
#define CDEF_ListBox	131
#define SYS7_LISTBOX_PROC	(CDEF_ListBox << 4)
