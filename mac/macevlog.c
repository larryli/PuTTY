/* $Id: macevlog.c,v 1.7 2003/04/12 21:06:34 ben Exp $ */
/*
 * Copyright (c) 2003 Ben Harris
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
#include <Lists.h>
#include <MacWindows.h>
#include <Quickdraw.h>
#include <Script.h>
#include <ToolUtils.h>

#include <limits.h>
#include <string.h>

#include "putty.h"
#include "mac.h"
#include "macresid.h"
#include "terminal.h"

static void mac_draweventloggrowicon(Session *s);
static void mac_adjusteventlogscrollbar(Session *s);
static void mac_clickeventlog(WindowPtr, EventRecord *);
static void mac_activateeventlog(WindowPtr, EventRecord *);
static void mac_groweventlog(WindowPtr, EventRecord *);
static void mac_updateeventlog(WindowPtr);
static void mac_closeeventlog(WindowPtr);

static void mac_createeventlog(Session *s)
{
    Rect view;
    ListBounds bounds = { 0, 0, 0, 1 }; /* 1 column, 0 rows */
    Point csize = { 0, 0 };
    GrafPtr saveport;
    long fondsize;
    WinInfo *wi;

    s->eventlog_window = GetNewWindow(wEventLog, NULL, (WindowPtr)-1);
    wi = snew(WinInfo);
    memset(wi, 0, sizeof(*wi));
    wi->s = s;
    wi->wtype = wEventLog;
    wi->click = &mac_clickeventlog;
    wi->activate = &mac_activateeventlog;
    wi->grow = &mac_groweventlog;
    wi->update = &mac_updateeventlog;
    wi->close = &mac_closeeventlog;
    SetWRefCon(s->eventlog_window, (long)wi);
    GetPort(&saveport);
    SetPort((GrafPtr)GetWindowPort(s->eventlog_window));
    fondsize = GetScriptVariable(smRoman, smScriptSmallFondSize);
    TextFont(HiWord(fondsize));
    TextSize(LoWord(fondsize));
    SetPort(saveport);
#if TARGET_API_MAC_CARBON
    GetPortBounds(GetWindowPort(s->eventlog_window), &view);
#else
    view = s->eventlog_window->portRect;
#endif
    view.right -= 15; /* Scrollbar */
    s->eventlog = LNew(&view, &bounds, csize, 0, s->eventlog_window,
		       TRUE, TRUE, FALSE, TRUE);
    mac_adjusteventlogscrollbar(s);
#if TARGET_API_MAC_CARBON
    SetListSelectionFlags(s->eventlog, lExtendDrag | lNoDisjoint | lNoExtend);
#else
    (*s->eventlog)->selFlags = lExtendDrag | lNoDisjoint | lNoExtend;
#endif
}

void mac_freeeventlog(Session *s)
{

    if (s->eventlog != NULL)
	LDispose(s->eventlog);
    if (s->eventlog_window != NULL) {
	sfree((WinInfo *)GetWRefCon(s->eventlog_window));
	DisposeWindow(s->eventlog_window);
    }
}

void logevent(void *frontend, char *str)
{
    Session *s = frontend;
    ListBounds bounds, visible;
    Cell cell = { 0, 0 };

    if (s->eventlog == NULL)
	mac_createeventlog(s);
    if (s->eventlog == NULL)
	return;

#if TARGET_API_MAC_CARBON
    GetListDataBounds(s->eventlog, &bounds);
    GetListVisibleCells(s->eventlog, &visible);
#else
    bounds = (*s->eventlog)->dataBounds;
    visible = (*s->eventlog)->visible;
#endif

    cell.v = bounds.bottom;
    LAddRow(1, cell.v, s->eventlog);
    LSetCell(str, strlen(str), cell, s->eventlog);
    /* ">=" and "2" because there can be a blank cell below the last one. */
    if (visible.bottom >= bounds.bottom)
	LScroll(0, 2, s->eventlog);
}

static void mac_draweventloggrowicon(Session *s)
{
    Rect clip;
    RgnHandle savergn;

    SetPort((GrafPtr)GetWindowPort(s->eventlog_window));
    /*
     * Stop DrawGrowIcon giving us space for a horizontal scrollbar
     * See Tech Note TB575 for details.
     */
#if TARGET_API_MAC_CARBON
    GetPortBounds(GetWindowPort(s->eventlog_window), &clip);
#else
    clip = s->eventlog_window->portRect;
#endif
    clip.left = clip.right - 15;
    savergn = NewRgn();
    GetClip(savergn);
    ClipRect(&clip);
    DrawGrowIcon(s->eventlog_window);
    SetClip(savergn);
    DisposeRgn(savergn);
}

/*
 * For some reason, LNew doesn't seem to respect the hasGrow
 * parameter, so we hammer the scrollbar into shape ourselves.
 */
static void mac_adjusteventlogscrollbar(Session *s)
{
#if TARGET_API_MAC_CARBON
    Rect winrect;

    GetPortBounds(GetWindowPort(s->eventlog_window), &winrect);
    SizeControl(GetListVerticalScrollBar(s->eventlog),
		16, winrect.bottom - 13);
#else
    SizeControl((*s->eventlog)->vScroll,
		16, s->eventlog_window->portRect.bottom - 13);
#endif
}

void mac_clickeventlog(WindowPtr window, EventRecord *event)
{
    Session *s = mac_windowsession(window);
    Point mouse;
    GrafPtr saveport;

    GetPort(&saveport);
    SetPort((GrafPtr)GetWindowPort(window));
    mouse = event->where;
    GlobalToLocal(&mouse);
    LClick(mouse, event->modifiers, s->eventlog);
    SetPort(saveport);
}

static void mac_groweventlog(WindowPtr window, EventRecord *event)
{
    Session *s = mac_windowsession(window);
    Rect limits;
    long grow_result;
#if TARGET_API_MAC_CARBON
    Rect rect;
    Point cellsize;
#else
    GrafPtr saveport;
#endif

    SetRect(&limits, 15, 0, SHRT_MAX, SHRT_MAX);
    grow_result = GrowWindow(window, event->where, &limits);
    if (grow_result == 0) return;
    SizeWindow(window, LoWord(grow_result), HiWord(grow_result), TRUE);
    LSize(LoWord(grow_result) - 15, HiWord(grow_result), s->eventlog);
    mac_adjusteventlogscrollbar(s);
    /* We would use SetListCellSize in Carbon, but it's missing. */
    (*s->eventlog)->cellSize.h = LoWord(grow_result) - 15;
#if TARGET_API_MAC_CARBON
    cellsize.h = LoWord(grow_result) - 15;
    GetListViewBounds(s->eventlog, &rect);
    InvalWindowRect(window, &rect);
#else
    GetPort(&saveport);
    SetPort(window);
    InvalRect(&(*s->eventlog)->rView);
    SetPort(saveport);
#endif
}

static void mac_activateeventlog(WindowPtr window, EventRecord *event)
{
    Session *s = mac_windowsession(window);
    int active = (event->modifiers & activeFlag) != 0;

    LActivate(active, s->eventlog);
    mac_draweventloggrowicon(s);
}

static void mac_updateeventlog(WindowPtr window)
{
    Session *s = mac_windowsession(window);
#if TARGET_API_MAC_CARBON
    RgnHandle visrgn;
#endif

    SetPort((GrafPtr)GetWindowPort(window));
    BeginUpdate(window);
#if TARGET_API_MAC_CARBON
    visrgn = NewRgn();
    GetPortVisibleRegion(GetWindowPort(window), visrgn);
    LUpdate(visrgn, s->eventlog);
    DisposeRgn(visrgn);
#else
    LUpdate(window->visRgn, s->eventlog);
#endif
    mac_draweventloggrowicon(s);
    EndUpdate(window);
}

static void mac_closeeventlog(WindowPtr window)
{

    HideWindow(window);
}

void mac_showeventlog(Session *s)
{

    SelectWindow(s->eventlog_window);
    ShowWindow(s->eventlog_window);
}

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */
