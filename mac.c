/* $Id: mac.c,v 1.1.2.2 1999/02/19 23:03:29 ben Exp $ */
/*
 * mac.c -- miscellaneous Mac-specific routines
 */

#include <MacTypes.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <MacWindows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>

#include <stdlib.h>		/* putty.h needs size_t */
#include <stdarg.h>

#include "putty.h"

QDGlobals qd;

int cold = 1;

int main (int argc, char **argv) {
    Handle menuBar;

    /* Init QuickDraw */
    InitGraf(&qd.thePort);
    /* Init Font Manager */
    InitFonts();
    /* Init Window Manager */
    InitWindows();
    /* Init Menu Manager */
    InitMenus();
    /* Init TextEdit */
    TEInit();
    /* Init Dialog Manager */
    InitDialogs(nil);
    InitCursor();
    cold = 0;
    
    menuBar = GetNewMBar(128);
    if (menuBar == NULL)
    	fatalbox("Unable to create menu bar.");
    SetMenuBar(menuBar);
    AppendResMenu(GetMenuHandle(128), 'DRVR');
    /* adjustmenus */
    DrawMenuBar();
    
    fatalbox("Init complete");
}

void fatalbox(const char *fmt, ...) {
    va_list ap;
    Str255 stuff;
    
    va_start(ap, fmt);
    /* We'd like stuff to be a Pascal string */
    stuff[0] = vsprintf((char *)(&stuff[1]), fmt, ap);
    va_end(ap);
    ParamText(stuff, NULL, NULL, NULL);
    StopAlert(128, nil);
    exit(1);
}
