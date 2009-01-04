/* $Id$ */
/*
 * Copyright (c) 1999, 2003 Ben Harris
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

#include <MacTypes.h>
#include <Dialogs.h>
#include <Files.h>
#include <MacWindows.h>
#include <Processes.h>
#include <Quickdraw.h>
#include <TextUtils.h>

#include <stdarg.h>
#include <stdio.h>

#include "putty.h"
#include "mac.h"
#include "ssh.h"

#if TARGET_API_MAC_CARBON
/*
 * This is used by (I think) CarbonStdCLib, but only exists in
 * CarbonLib 1.1 and later.  Muppets.  Happily, it's documented to be
 * a synonym for NULL.
 */
#include <CFBase.h>
const CFAllocatorRef kCFAllocatorDefault = NULL;
#else
QDGlobals qd;
#endif

/*
 * Like FrontWindow(), but return NULL if we aren't the front process
 * (i.e. the front window isn't one of ours).
 */
WindowPtr mac_frontwindow(void)
{
    ProcessSerialNumber frontpsn;
    ProcessSerialNumber curpsn = { 0, kCurrentProcess };
    Boolean result;

    GetFrontProcess(&frontpsn);
    if (SameProcess(&frontpsn, &curpsn, &result) == noErr && result)
	return FrontWindow();
    return NULL;
}

void fatalbox(char *fmt, ...) {
    va_list ap;
    Str255 stuff;
    
    va_start(ap, fmt);
    /* We'd like stuff to be a Pascal string */
    stuff[0] = vsprintf((char *)(&stuff[1]), fmt, ap);
    va_end(ap);
    ParamText(stuff, NULL, NULL, NULL);
    StopAlert(128, NULL);
    cleanup_exit(1);
}

void modalfatalbox(char *fmt, ...) {
    va_list ap;
    Str255 stuff;
    
    va_start(ap, fmt);
    /* We'd like stuff to be a Pascal string */
    stuff[0] = vsprintf((char *)(&stuff[1]), fmt, ap);
    va_end(ap);
    ParamText(stuff, NULL, NULL, NULL);
    StopAlert(128, NULL);
    cleanup_exit(1);
}

Filename filename_from_str(const char *str)
{
    Filename ret;
    Str255 tmp;

    /* XXX This fails for filenames over 255 characters long. */
    c2pstrcpy(tmp, str);
    FSMakeFSSpec(0, 0, tmp, &ret.fss);
    return ret;
}

/*
 * Convert a filename to a string for display purposes.
 * See pp 2-44--2-46 of IM:Files
 *
 * XXX static storage considered harmful
 */
const char *filename_to_str(const Filename *fn)
{
    CInfoPBRec pb;
    Str255 dirname;
    OSErr err;
    static char *path = NULL;
    char *newpath;

    if (path != NULL) sfree(path);
    path = snewn(fn->fss.name[0], char);
    p2cstrcpy(path, fn->fss.name);
    pb.dirInfo.ioNamePtr = dirname;
    pb.dirInfo.ioVRefNum = fn->fss.vRefNum;
    pb.dirInfo.ioDrParID = fn->fss.parID;
    pb.dirInfo.ioFDirIndex = -1;
    do {
	pb.dirInfo.ioDrDirID = pb.dirInfo.ioDrParID;
	err = PBGetCatInfoSync(&pb);

	/* XXX Assume not A/UX */
	newpath = snewn(strlen(path) + dirname[0] + 2, char);
	p2cstrcpy(newpath, dirname);
	strcat(newpath, ":");
	strcat(newpath, path);
	sfree(path);
	path = newpath;
    } while (pb.dirInfo.ioDrDirID != fsRtDirID);
    return path;
}

int filename_equal(Filename f1, Filename f2)
{

    return f1.fss.vRefNum == f2.fss.vRefNum &&
	f1.fss.parID == f2.fss.parID &&
	f1.fss.name[0] == f2.fss.name[0] &&
	memcmp(f1.fss.name + 1, f2.fss.name + 1, f1.fss.name[0]) == 0;
}

int filename_is_null(Filename fn)
{

    return fn.fss.vRefNum == 0 && fn.fss.parID == 0 && fn.fss.name[0] == 0;
}

FILE *f_open(Filename fn, char const *mode, int is_private)
{
    short savevol;
    long savedir;
    char tmp[256];
    FILE *ret;

    HGetVol(NULL, &savevol, &savedir);
    if (HSetVol(NULL, fn.fss.vRefNum, fn.fss.parID) == noErr) {
	p2cstrcpy(tmp, fn.fss.name);
	ret = fopen(tmp, mode);
    } else
	ret = NULL;
    HSetVol(NULL, savevol, savedir);
    return ret;
}

struct tm ltime(void)
{
    struct tm tm;
    DateTimeRec d;
    GetTime(&d);

    tm.tm_sec=d.second;
    tm.tm_min=d.minute;
    tm.tm_hour=d.hour;
    tm.tm_mday=d.day;
    tm.tm_mon=d.month-1;
    tm.tm_year=d.year-1900;
    tm.tm_wday=d.dayOfWeek;
    tm.tm_yday=1; /* GetTime doesn't tell us */
    tm.tm_isdst=0; /* Have to do DST ourselves */

    /* XXX find out DST adjustment and add it */

    return tm;
}

const int platform_uses_x11_unix_by_default = FALSE;

char *platform_get_x_display(void) {
    return NULL;
}

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */
