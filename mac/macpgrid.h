/* $Id: macpgrid.h,v 1.4 2003/02/20 22:55:09 ben Exp $ */

/*
 * macpgrid.h -- Mac resource IDs for PuTTYgen
 *
 * This file is shared by C and Rez source files
 */

/* Menu bar IDs */
#define MBAR_Main	128

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
#define iClose		4
#define iSave		5
#define iSaveAs		6
#define iQuit		8
/* Edit menu */
#define iUndo		1
#define iCut		3
#define iCopy		4
#define iPaste		5
#define iClear		6
#define iSelectAll	7
/* Window menu */

/* Window types (and resource IDs) */
#define wNone		0 /* Dummy value for no window */
#define wDA		1 /* Dummy value for desk accessory */
#define wFatal		128
#define wAbout		129
#define wiAboutLicence		1
#define wiAboutVersion		3
#define wLicence	131
#define wKey		134
#define wiKeyGenerate		1
#define wiKeyProgress		2

