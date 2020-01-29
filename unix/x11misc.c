/*
 * x11misc.c: miscellaneous stuff for dealing directly with X servers.
 */

#include <ctype.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>

#include "putty.h"

#ifndef NOT_X_WINDOWS

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include "x11misc.h"

/* ----------------------------------------------------------------------
 * Error handling mechanism which permits us to ignore specific X11
 * errors from particular requests. We maintain a list of upcoming
 * potential error events that we want to not treat as fatal errors.
 */

static int (*orig_x11_error_handler)(Display *thisdisp, XErrorEvent *err);

struct x11_err_to_ignore {
    Display *display;
    unsigned char error_code;
    unsigned long serial;
};

static struct x11_err_to_ignore *errs;
static size_t nerrs, errsize;

static int x11_error_handler(Display *thisdisp, XErrorEvent *err)
{
    for (size_t i = 0; i < nerrs; i++) {
        if (thisdisp == errs[i].display &&
            err->serial == errs[i].serial &&
            err->error_code == errs[i].error_code) {
            /* Ok, this is an error we're happy to ignore */
            return 0;
        }
    }

    return (*orig_x11_error_handler)(thisdisp, err);
}

void x11_ignore_error(Display *disp, unsigned char errcode)
{
    /*
     * Install our error handler, if we haven't already.
     */
    if (!orig_x11_error_handler)
        orig_x11_error_handler = XSetErrorHandler(x11_error_handler);

    /*
     * This is as good a moment as any to winnow the ignore list based
     * on requests we know to have been processed.
     */
    {
        unsigned long last = LastKnownRequestProcessed(disp);
        size_t i, j;
        for (i = j = 0; i < nerrs; i++) {
            if (errs[i].display == disp && errs[i].serial <= last)
                continue;
            errs[j++] = errs[i];
        }
        nerrs = j;
    }

    sgrowarray(errs, errsize, nerrs);
    errs[nerrs].display = disp;
    errs[nerrs].error_code = errcode;
    errs[nerrs].serial = NextRequest(disp);
    nerrs++;
}

#endif

