/*
 * gtk-common.c: machinery in the GTK front end which is common to all
 * programs that run a session in a terminal window, and also common
 * across all _sessions_ rather than specific to one session. (Timers,
 * uxsel etc.)
 */

#define _GNU_SOURCE

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <locale.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <gtk/gtk.h>
#if !GTK_CHECK_VERSION(3,0,0)
#include <gdk/gdkkeysyms.h>
#endif

#if GTK_CHECK_VERSION(2,0,0)
#include <gtk/gtkimmodule.h>
#endif

#define MAY_REFER_TO_GTK_IN_HEADERS

#include "putty.h"
#include "terminal.h"
#include "gtkcompat.h"
#include "unifont.h"
#include "gtkmisc.h"

#ifndef NOT_X_WINDOWS
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#endif

#define ASSERT(x) enum {CAT(assertion_,__LINE__) = 1 / (x)}

#if GTK_CHECK_VERSION(2,0,0)
ASSERT(sizeof(long) <= sizeof(gsize));
#define LONG_TO_GPOINTER(l) GSIZE_TO_POINTER(l)
#define GPOINTER_TO_LONG(p) GPOINTER_TO_SIZE(p)
#else /* Gtk 1.2 */
ASSERT(sizeof(long) <= sizeof(gpointer));
#define LONG_TO_GPOINTER(l) ((gpointer)(long)(l))
#define GPOINTER_TO_LONG(p) ((long)(p))
#endif

/* ----------------------------------------------------------------------
 * File descriptors and uxsel.
 */

struct uxsel_id {
#if GTK_CHECK_VERSION(2,0,0)
    GIOChannel *chan;
    guint watch_id;
#else
    int id;
#endif
};

#if GTK_CHECK_VERSION(2,0,0)
gboolean fd_input_func(GIOChannel *source, GIOCondition condition,
                       gpointer data)
{
    int sourcefd = g_io_channel_unix_get_fd(source);
    /*
     * We must process exceptional notifications before ordinary
     * readability ones, or we may go straight past the urgent
     * marker.
     */
    if (condition & G_IO_PRI)
        select_result(sourcefd, SELECT_X);
    if (condition & (G_IO_IN | G_IO_HUP))
        select_result(sourcefd, SELECT_R);
    if (condition & G_IO_OUT)
        select_result(sourcefd, SELECT_W);

    run_toplevel_callbacks();

    return true;
}
#else
void fd_input_func(gpointer data, gint sourcefd, GdkInputCondition condition)
{
    if (condition & GDK_INPUT_EXCEPTION)
        select_result(sourcefd, SELECT_X);
    if (condition & GDK_INPUT_READ)
        select_result(sourcefd, SELECT_R);
    if (condition & GDK_INPUT_WRITE)
        select_result(sourcefd, SELECT_W);

    run_toplevel_callbacks();
}
#endif

uxsel_id *uxsel_input_add(int fd, int rwx) {
    uxsel_id *id = snew(uxsel_id);

#if GTK_CHECK_VERSION(2,0,0)
    int flags = 0;
    if (rwx & SELECT_R) flags |= G_IO_IN | G_IO_HUP;
    if (rwx & SELECT_W) flags |= G_IO_OUT;
    if (rwx & SELECT_X) flags |= G_IO_PRI;
    id->chan = g_io_channel_unix_new(fd);
    g_io_channel_set_encoding(id->chan, NULL, NULL);
    id->watch_id = g_io_add_watch_full(id->chan, GDK_PRIORITY_REDRAW+1, flags,
                                       fd_input_func, NULL, NULL);
#else
    int flags = 0;
    if (rwx & SELECT_R) flags |= GDK_INPUT_READ;
    if (rwx & SELECT_W) flags |= GDK_INPUT_WRITE;
    if (rwx & SELECT_X) flags |= GDK_INPUT_EXCEPTION;
    assert(flags);
    id->id = gdk_input_add(fd, flags, fd_input_func, NULL);
#endif

    return id;
}

void uxsel_input_remove(uxsel_id *id) {
#if GTK_CHECK_VERSION(2,0,0)
    g_source_remove(id->watch_id);
    g_io_channel_unref(id->chan);
#else
    gdk_input_remove(id->id);
#endif
    sfree(id);
}

/* ----------------------------------------------------------------------
 * Timers.
 */

static guint timer_id = 0;

static gint timer_trigger(gpointer data)
{
    unsigned long now = GPOINTER_TO_LONG(data);
    unsigned long next, then;
    long ticks;

    /*
     * Destroy the timer we got here on.
     */
    if (timer_id) {
        g_source_remove(timer_id);
        timer_id = 0;
    }

    /*
     * run_timers() may cause a call to timer_change_notify, in which
     * case a new timer will already have been set up and left in
     * timer_id. If it hasn't, and run_timers reports that some timing
     * still needs to be done, we do it ourselves.
     */
    if (run_timers(now, &next) && !timer_id) {
        then = now;
        now = GETTICKCOUNT();
        if (now - then > next - then)
            ticks = 0;
        else
            ticks = next - now;
        timer_id = g_timeout_add(ticks, timer_trigger, LONG_TO_GPOINTER(next));
    }

    /*
     * Returning false means 'don't call this timer again', which
     * _should_ be redundant given that we removed it above, but just
     * in case, return false anyway.
     */
    return false;
}

void timer_change_notify(unsigned long next)
{
    long ticks;

    if (timer_id)
        g_source_remove(timer_id);

    ticks = next - GETTICKCOUNT();
    if (ticks <= 0)
        ticks = 1;                     /* just in case */

    timer_id = g_timeout_add(ticks, timer_trigger, LONG_TO_GPOINTER(next));
}

/* ----------------------------------------------------------------------
 * Toplevel callbacks.
 */

static guint toplevel_callback_idle_id;
static bool idle_fn_scheduled;

static void notify_toplevel_callback(void *);

static gint idle_toplevel_callback_func(gpointer data)
{
    run_toplevel_callbacks();

    /*
     * If we've emptied our toplevel callback queue, unschedule
     * ourself. Otherwise, leave ourselves pending so we'll be called
     * again to deal with more callbacks after another round of the
     * event loop.
     */
    if (!toplevel_callback_pending() && idle_fn_scheduled) {
        g_source_remove(toplevel_callback_idle_id);
        idle_fn_scheduled = false;
    }

    return true;
}

static void notify_toplevel_callback(void *vctx)
{
    if (!idle_fn_scheduled) {
        toplevel_callback_idle_id =
            g_idle_add(idle_toplevel_callback_func, NULL);
        idle_fn_scheduled = true;
    }
}

/* ----------------------------------------------------------------------
 * Setup function. The real main program must call this.
 */

void gtkcomm_setup(void)
{
    uxsel_init();
    request_callback_notifications(notify_toplevel_callback, NULL);
}
