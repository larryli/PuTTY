/* $Id: mac.c,v 1.1.2.1 1999/02/19 21:35:12 ben Exp $ */
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
