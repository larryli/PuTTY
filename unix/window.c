/*
 * window.c: the main code that runs a PuTTY terminal emulator and
 * backend in a GTK window.
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

#include "x11misc.h"

GdkAtom compound_text_atom, utf8_string_atom;
static GdkAtom clipboard_atom
#if GTK_CHECK_VERSION(2,0,0) /* GTK1 will have to fill this in at startup */
    = GDK_SELECTION_CLIPBOARD
#endif
    ;

#ifdef JUST_USE_GTK_CLIPBOARD_UTF8
/*
 * Because calling gtk_clipboard_set_with_data triggers a call to the
 * clipboard_clear function from the last time, we need to arrange a
 * way to distinguish a real call to clipboard_clear for the _new_
 * instance of the clipboard data from the leftover call for the
 * outgoing one. We do this by setting the user data field in our
 * gtk_clipboard_set_with_data() call, instead of the obvious pointer
 * to 'inst', to one of these.
 */
struct clipboard_data_instance {
    char *pasteout_data_utf8;
    int pasteout_data_utf8_len;
    struct clipboard_state *state;
    struct clipboard_data_instance *next, *prev;
};
#endif

struct clipboard_state {
    GtkFrontend *inst;
    int clipboard;
    GdkAtom atom;
#ifdef JUST_USE_GTK_CLIPBOARD_UTF8
    GtkClipboard *gtkclipboard;
    struct clipboard_data_instance *current_cdi;
#else
    char *pasteout_data, *pasteout_data_ctext, *pasteout_data_utf8;
    int pasteout_data_len, pasteout_data_ctext_len, pasteout_data_utf8_len;
#endif
};

typedef struct XpmHolder XpmHolder;    /* only used for GTK 1 */

struct GtkFrontend {
    GtkWidget *window, *area, *sbar;
    gboolean sbar_visible;
    gboolean drawing_area_got_size, drawing_area_realised;
    gboolean drawing_area_setup_needed;
    bool drawing_area_setup_called;
    GtkBox *hbox;
    GtkAdjustment *sbar_adjust;
    GtkWidget *menu, *specialsmenu, *specialsitem1, *specialsitem2,
        *restartitem;
    GtkWidget *sessionsmenu;
#ifndef NOT_X_WINDOWS
    Display *disp;
#endif
#ifndef NO_BACKING_PIXMAPS
    /*
     * Server-side pixmap which we use to cache the terminal window's
     * contents. When we draw text in the terminal, we draw it to this
     * pixmap first, and then blit from there to the actual window;
     * this way, X expose events can be handled with an absolute
     * minimum of network traffic, by just sending a command to
     * re-blit an appropriate rectangle from this pixmap.
     */
    GdkPixmap *pixmap;
#endif
#ifdef DRAW_TEXT_CAIRO
    /*
     * If we're drawing using Cairo, we cache the same image on the
     * client side in a Cairo surface.
     *
     * In GTK2+Cairo, this happens _as well_ as having the server-side
     * pixmap cache above; in GTK3+Cairo, server-side pixmaps are
     * deprecated, so we _just_ have this client-side cache. In the
     * latter case that means we have to transmit a big wodge of
     * bitmap data over the X connection on every expose event; but
     * GTK3 apparently deliberately provides no way to avoid that
     * inefficiency, and at least this way we don't _also_ have to
     * redo any font rendering just because the window was temporarily
     * covered.
     */
    cairo_surface_t *surface;
#endif
    int backing_w, backing_h;
#if GTK_CHECK_VERSION(2,0,0)
    GtkIMContext *imc;
#endif
    unifont *fonts[4];                 /* normal, bold, wide, widebold */
    int xpos, ypos, gravity;
    bool gotpos;
    GdkCursor *rawcursor, *textcursor, *blankcursor, *waitcursor, *currcursor;
    GdkColor cols[OSC4_NCOLOURS];        /* indexed by xterm colour indices */
#if !GTK_CHECK_VERSION(3,0,0)
    GdkColormap *colmap;
#endif
    bool direct_to_font;
    struct clipboard_state clipstates[N_CLIPBOARDS];
#ifdef JUST_USE_GTK_CLIPBOARD_UTF8
    /* Remember all clipboard_data_instance structures currently
     * associated with this GtkFrontend, in case they're still around
     * when it gets destroyed */
    struct clipboard_data_instance cdi_headtail;
#endif
    int clipboard_ctrlshiftins, clipboard_ctrlshiftcv;
    int font_width, font_height;
    int width, height, scale;
    bool ignore_sbar;
    bool mouseptr_visible;
    BusyStatus busy_status;
    int alt_keycode;
    int alt_digits;
    char *wintitle;
    char *icontitle;
    int master_fd, master_func_id;
    Ldisc *ldisc;
    Backend *backend;
    Terminal *term;
    LogContext *logctx;
    bool exited;
    struct unicode_data ucsdata;
    Conf *conf;
    eventlog_stuff *eventlogstuff;
    guint32 input_event_time; /* Timestamp of the most recent input event. */
    GtkWidget *dialogs[DIALOG_SLOT_LIMIT];
#if GTK_CHECK_VERSION(3,4,0)
    gdouble cumulative_scroll;
#endif
    /* Cached things out of conf that we refer to a lot */
    int bold_style;
    int window_border;
    int cursor_type;
    int drawtype;
    int meta_mod_mask;
#ifdef OSX_META_KEY_CONFIG
    int system_mod_mask;
#endif
    bool send_raw_mouse;
    bool pointer_indicates_raw_mouse;
    unifont_drawctx uctx;
#if GTK_CHECK_VERSION(2,0,0)
    GdkPixbuf *trust_sigil_pb;
#else
    GdkPixmap *trust_sigil_pm;
#endif
    int trust_sigil_w, trust_sigil_h;

    Seat seat;
    TermWin termwin;
    LogPolicy logpolicy;
};

static void cache_conf_values(GtkFrontend *inst)
{
    inst->bold_style = conf_get_int(inst->conf, CONF_bold_style);
    inst->window_border = conf_get_int(inst->conf, CONF_window_border);
    inst->cursor_type = conf_get_int(inst->conf, CONF_cursor_type);
#ifdef OSX_META_KEY_CONFIG
    inst->meta_mod_mask = 0;
    if (conf_get_bool(inst->conf, CONF_osx_option_meta))
        inst->meta_mod_mask |= GDK_MOD1_MASK;
    if (conf_get_bool(inst->conf, CONF_osx_command_meta))
        inst->meta_mod_mask |= GDK_MOD2_MASK;
    inst->system_mod_mask = GDK_MOD2_MASK & ~inst->meta_mod_mask;
#else
    inst->meta_mod_mask = GDK_MOD1_MASK;
#endif
}

static void start_backend(GtkFrontend *inst);
static void exit_callback(void *vinst);
static void destroy_inst_connection(GtkFrontend *inst);
static void delete_inst(GtkFrontend *inst);

static void post_fatal_message_box_toplevel(void *vctx)
{
    GtkFrontend *inst = (GtkFrontend *)vctx;
    gtk_widget_destroy(inst->window);
}

static void post_fatal_message_box(void *vctx, int result)
{
    GtkFrontend *inst = (GtkFrontend *)vctx;
    unregister_dialog(&inst->seat, DIALOG_SLOT_CONNECTION_FATAL);
    queue_toplevel_callback(post_fatal_message_box_toplevel, inst);
}

static void common_connfatal_message_box(
    GtkFrontend *inst, const char *msg, post_dialog_fn_t postfn)
{
    char *title = dupcat(appname, " Fatal Error");
    GtkWidget *dialog = create_message_box(
        inst->window, title, msg,
        string_width("REASONABLY LONG LINE OF TEXT FOR BASIC SANITY"),
        false, &buttons_ok, postfn, inst);
    register_dialog(&inst->seat, DIALOG_SLOT_CONNECTION_FATAL, dialog);
    sfree(title);
}

void fatal_message_box(GtkFrontend *inst, const char *msg)
{
    common_connfatal_message_box(inst, msg, post_fatal_message_box);
}

static void connection_fatal_callback(void *vctx)
{
    GtkFrontend *inst = (GtkFrontend *)vctx;
    destroy_inst_connection(inst);
}

static void post_nonfatal_message_box(void *vctx, int result)
{
    GtkFrontend *inst = (GtkFrontend *)vctx;
    unregister_dialog(&inst->seat, DIALOG_SLOT_CONNECTION_FATAL);
}

static void gtk_seat_connection_fatal(Seat *seat, const char *msg)
{
    GtkFrontend *inst = container_of(seat, GtkFrontend, seat);
    if (conf_get_int(inst->conf, CONF_close_on_exit) == FORCE_ON) {
        fatal_message_box(inst, msg);
    } else {
        common_connfatal_message_box(inst, msg, post_nonfatal_message_box);
    }

    inst->exited = true;   /* suppress normal exit handling */
    queue_toplevel_callback(connection_fatal_callback, inst);
}

/*
 * Default settings that are specific to pterm.
 */
FontSpec *platform_default_fontspec(const char *name)
{
    if (!strcmp(name, "Font"))
        return fontspec_new(DEFAULT_GTK_FONT);
    else
        return fontspec_new("");
}

Filename *platform_default_filename(const char *name)
{
    if (!strcmp(name, "LogFileName"))
        return filename_from_str("putty.log");
    else
        return filename_from_str("");
}

char *platform_default_s(const char *name)
{
    if (!strcmp(name, "SerialLine"))
        return dupstr("/dev/ttyS0");
    return NULL;
}

bool platform_default_b(const char *name, bool def)
{
    if (!strcmp(name, "WinNameAlways")) {
        /* X natively supports icon titles, so use 'em by default */
        return false;
    }
    return def;
}

int platform_default_i(const char *name, int def)
{
    if (!strcmp(name, "CloseOnExit"))
        return 2;  /* maps to FORCE_ON after painful rearrangement :-( */
    return def;
}

static char *gtk_seat_get_ttymode(Seat *seat, const char *mode)
{
    GtkFrontend *inst = container_of(seat, GtkFrontend, seat);
    return term_get_ttymode(inst->term, mode);
}

static size_t gtk_seat_output(Seat *seat, SeatOutputType type,
                              const void *data, size_t len)
{
    GtkFrontend *inst = container_of(seat, GtkFrontend, seat);
    return term_data(inst->term, data, len);
}

static void gtkwin_unthrottle(TermWin *win, size_t bufsize)
{
    GtkFrontend *inst = container_of(win, GtkFrontend, termwin);
    if (inst->backend)
        backend_unthrottle(inst->backend, bufsize);
}

static bool gtk_seat_eof(Seat *seat)
{
    /* GtkFrontend *inst = container_of(seat, GtkFrontend, seat); */
    return true;   /* do respond to incoming EOF with outgoing */
}

static SeatPromptResult gtk_seat_get_userpass_input(Seat *seat, prompts_t *p)
{
    GtkFrontend *inst = container_of(seat, GtkFrontend, seat);
    SeatPromptResult spr;
    spr = cmdline_get_passwd_input(p);
    if (spr.kind == SPRK_INCOMPLETE)
        spr = term_get_userpass_input(inst->term, p);
    return spr;
}

static bool gtk_seat_is_utf8(Seat *seat)
{
    GtkFrontend *inst = container_of(seat, GtkFrontend, seat);
    return inst->ucsdata.line_codepage == CS_UTF8;
}

static void get_window_pixel_size(GtkFrontend *inst, int *w, int *h)
{
    /*
     * I assume that when the GTK version of this call is available
     * we should use it. Not sure how it differs from the GDK one,
     * though.
     */
#if GTK_CHECK_VERSION(2,0,0)
    gtk_window_get_size(GTK_WINDOW(inst->window), w, h);
#else
    gdk_window_get_size(gtk_widget_get_window(inst->window), w, h);
#endif
}

static bool gtk_seat_get_window_pixel_size(Seat *seat, int *w, int *h)
{
    GtkFrontend *inst = container_of(seat, GtkFrontend, seat);
    get_window_pixel_size(inst, w, h);
    return true;
}

StripCtrlChars *gtk_seat_stripctrl_new(
    Seat *seat, BinarySink *bs_out, SeatInteractionContext sic)
{
    GtkFrontend *inst = container_of(seat, GtkFrontend, seat);
    return stripctrl_new_term(bs_out, false, 0, inst->term);
}

static void gtk_seat_notify_remote_exit(Seat *seat);
static void gtk_seat_update_specials_menu(Seat *seat);
static void gtk_seat_set_busy_status(Seat *seat, BusyStatus status);
static const char *gtk_seat_get_x_display(Seat *seat);
#ifndef NOT_X_WINDOWS
static bool gtk_seat_get_windowid(Seat *seat, long *id);
#endif
static void gtk_seat_set_trust_status(Seat *seat, bool trusted);
static bool gtk_seat_can_set_trust_status(Seat *seat);
static bool gtk_seat_get_cursor_position(Seat *seat, int *x, int *y);

static const SeatVtable gtk_seat_vt = {
    .output = gtk_seat_output,
    .eof = gtk_seat_eof,
    .sent = nullseat_sent,
    .banner = nullseat_banner_to_stderr,
    .get_userpass_input = gtk_seat_get_userpass_input,
    .notify_session_started = nullseat_notify_session_started,
    .notify_remote_exit = gtk_seat_notify_remote_exit,
    .notify_remote_disconnect = nullseat_notify_remote_disconnect,
    .connection_fatal = gtk_seat_connection_fatal,
    .update_specials_menu = gtk_seat_update_specials_menu,
    .get_ttymode = gtk_seat_get_ttymode,
    .set_busy_status = gtk_seat_set_busy_status,
    .confirm_ssh_host_key = gtk_seat_confirm_ssh_host_key,
    .confirm_weak_crypto_primitive = gtk_seat_confirm_weak_crypto_primitive,
    .confirm_weak_cached_hostkey = gtk_seat_confirm_weak_cached_hostkey,
    .is_utf8 = gtk_seat_is_utf8,
    .echoedit_update = nullseat_echoedit_update,
    .get_x_display = gtk_seat_get_x_display,
#ifdef NOT_X_WINDOWS
    .get_windowid = nullseat_get_windowid,
#else
    .get_windowid = gtk_seat_get_windowid,
#endif
    .get_window_pixel_size = gtk_seat_get_window_pixel_size,
    .stripctrl_new = gtk_seat_stripctrl_new,
    .set_trust_status = gtk_seat_set_trust_status,
    .can_set_trust_status = gtk_seat_can_set_trust_status,
    .has_mixed_input_stream = nullseat_has_mixed_input_stream_yes,
    .verbose = nullseat_verbose_yes,
    .interactive = nullseat_interactive_yes,
    .get_cursor_position = gtk_seat_get_cursor_position,
};

static void gtk_eventlog(LogPolicy *lp, const char *string)
{
    GtkFrontend *inst = container_of(lp, GtkFrontend, logpolicy);
    logevent_dlg(inst->eventlogstuff, string);
}

static int gtk_askappend(LogPolicy *lp, Filename *filename,
                         void (*callback)(void *ctx, int result), void *ctx)
{
    GtkFrontend *inst = container_of(lp, GtkFrontend, logpolicy);
    return gtkdlg_askappend(&inst->seat, filename, callback, ctx);
}

static void gtk_logging_error(LogPolicy *lp, const char *event)
{
    GtkFrontend *inst = container_of(lp, GtkFrontend, logpolicy);

    /* Send 'can't open log file' errors to the terminal window.
     * (Marked as stderr, although terminal.c won't care.) */
    seat_stderr_pl(&inst->seat, ptrlen_from_asciz(event));
    seat_stderr_pl(&inst->seat, PTRLEN_LITERAL("\r\n"));
}

static const LogPolicyVtable gtk_logpolicy_vt = {
    .eventlog = gtk_eventlog,
    .askappend = gtk_askappend,
    .logging_error = gtk_logging_error,
    .verbose = null_lp_verbose_yes,
};

/*
 * Translate a raw mouse button designation (LEFT, MIDDLE, RIGHT)
 * into a cooked one (SELECT, EXTEND, PASTE).
 *
 * In Unix, this is not configurable; the X button arrangement is
 * rock-solid across all applications, everyone has a three-button
 * mouse or a means of faking it, and there is no need to switch
 * buttons around at all.
 */
static Mouse_Button translate_button(Mouse_Button button)
{
    if (button == MBT_LEFT)
        return MBT_SELECT;
    if (button == MBT_MIDDLE)
        return MBT_PASTE;
    if (button == MBT_RIGHT)
        return MBT_EXTEND;
    return 0;                          /* shouldn't happen */
}

/*
 * Return the top-level GtkWindow associated with a particular
 * front end instance.
 */
GtkWidget *gtk_seat_get_window(Seat *seat)
{
    GtkFrontend *inst = container_of(seat, GtkFrontend, seat);
    return inst->window;
}

/*
 * Set and clear a pointer to a dialog box created as a result of the
 * network code wanting to ask an asynchronous user question (e.g.
 * 'what about this dodgy host key, then?').
 */
void register_dialog(Seat *seat, enum DialogSlot slot, GtkWidget *dialog)
{
    GtkFrontend *inst;
    assert(seat->vt == &gtk_seat_vt);
    inst = container_of(seat, GtkFrontend, seat);
    assert(slot < DIALOG_SLOT_LIMIT);
    assert(!inst->dialogs[slot]);
    inst->dialogs[slot] = dialog;
}
void unregister_dialog(Seat *seat, enum DialogSlot slot)
{
    GtkFrontend *inst;
    assert(seat->vt == &gtk_seat_vt);
    inst = container_of(seat, GtkFrontend, seat);
    assert(slot < DIALOG_SLOT_LIMIT);
    assert(inst->dialogs[slot]);
    inst->dialogs[slot] = NULL;
}

/*
 * Minimise or restore the window in response to a server-side
 * request.
 */
static void gtkwin_set_minimised(TermWin *tw, bool minimised)
{
    /*
     * GTK 1.2 doesn't know how to do this.
     */
#if GTK_CHECK_VERSION(2,0,0)
    GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);
    if (minimised)
        gtk_window_iconify(GTK_WINDOW(inst->window));
    else
        gtk_window_deiconify(GTK_WINDOW(inst->window));
#endif
}

/*
 * Move the window in response to a server-side request.
 */
static void gtkwin_move(TermWin *tw, int x, int y)
{
    GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);
    /*
     * I assume that when the GTK version of this call is available
     * we should use it. Not sure how it differs from the GDK one,
     * though.
     */
#if GTK_CHECK_VERSION(2,0,0)
    /* in case we reset this at startup due to a geometry string */
    gtk_window_set_gravity(GTK_WINDOW(inst->window), GDK_GRAVITY_NORTH_EAST);
    gtk_window_move(GTK_WINDOW(inst->window), x, y);
#else
    gdk_window_move(gtk_widget_get_window(inst->window), x, y);
#endif
}

/*
 * Move the window to the top or bottom of the z-order in response
 * to a server-side request.
 */
static void gtkwin_set_zorder(TermWin *tw, bool top)
{
    GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);
    if (top)
        gdk_window_raise(gtk_widget_get_window(inst->window));
    else
        gdk_window_lower(gtk_widget_get_window(inst->window));
}

/*
 * Refresh the window in response to a server-side request.
 */
static void gtkwin_refresh(TermWin *tw)
{
    GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);
    term_invalidate(inst->term);
}

/*
 * Maximise or restore the window in response to a server-side
 * request.
 */
static void gtkwin_set_maximised(TermWin *tw, bool maximised)
{
    /*
     * GTK 1.2 doesn't know how to do this.
     */
#if GTK_CHECK_VERSION(2,0,0)
    GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);
    if (maximised)
        gtk_window_maximize(GTK_WINDOW(inst->window));
    else
        gtk_window_unmaximize(GTK_WINDOW(inst->window));
#endif
}

/*
 * Find out whether a dialog box already exists for this window in a
 * particular DialogSlot. If it does, uniconify it (if we can) and
 * raise it, so that the user realises they've already been asked this
 * question.
 */
static bool find_and_raise_dialog(GtkFrontend *inst, enum DialogSlot slot)
{
    GtkWidget *dialog = inst->dialogs[slot];
    if (!dialog)
        return false;

#if GTK_CHECK_VERSION(2,0,0)
    gtk_window_deiconify(GTK_WINDOW(dialog));
#endif
    gdk_window_raise(gtk_widget_get_window(dialog));
    return true;
}

static void warn_on_close_callback(void *vctx, int result)
{
    GtkFrontend *inst = (GtkFrontend *)vctx;
    unregister_dialog(&inst->seat, DIALOG_SLOT_WARN_ON_CLOSE);
    if (result)
        gtk_widget_destroy(inst->window);
}

/*
 * Handle the 'delete window' event (e.g. user clicking the WM close
 * button). The return value false means the window should close, and
 * true means it shouldn't.
 *
 * (That's counterintuitive, but really, in GTK terms, true means 'I
 * have done everything necessary to handle this event, so the default
 * handler need not do anything', i.e. 'suppress default handler',
 * i.e. 'do not close the window'.)
 */
gint delete_window(GtkWidget *widget, GdkEvent *event, GtkFrontend *inst)
{
    if (!inst->exited && conf_get_bool(inst->conf, CONF_warn_on_close)) {
        /*
         * We're not going to exit right now. We must put up a
         * warn-on-close dialog, unless one already exists, in which
         * case we'll just re-emphasise that one.
         */
        if (!find_and_raise_dialog(inst, DIALOG_SLOT_WARN_ON_CLOSE)) {
            char *title = dupcat(appname, " Exit Confirmation");
            char *msg, *additional = NULL;
            if (inst->backend && inst->backend->vt->close_warn_text) {
                additional = inst->backend->vt->close_warn_text(inst->backend);
            }
            msg = dupprintf("Are you sure you want to close this session?%s%s",
                            additional ? "\n" : "",
                            additional ? additional : "");
            GtkWidget *dialog = create_message_box(
                inst->window, title, msg,
                string_width("Most of the width of the above text"),
                false, &buttons_yn, warn_on_close_callback, inst);
            register_dialog(&inst->seat, DIALOG_SLOT_WARN_ON_CLOSE, dialog);
            sfree(title);
            sfree(msg);
            sfree(additional);
        }
        return true;
    }
    return false;
}

#if GTK_CHECK_VERSION(2,0,0)
static gboolean window_state_event(
    GtkWidget *widget, GdkEventWindowState *event, gpointer user_data)
{
    GtkFrontend *inst = (GtkFrontend *)user_data;
    term_notify_minimised(
        inst->term, event->new_window_state & GDK_WINDOW_STATE_ICONIFIED);
    return false;
}
#endif

static void update_mouseptr(GtkFrontend *inst)
{
    switch (inst->busy_status) {
      case BUSY_NOT:
        if (!inst->mouseptr_visible) {
            gdk_window_set_cursor(gtk_widget_get_window(inst->area),
                                  inst->blankcursor);
        } else if (inst->pointer_indicates_raw_mouse) {
            gdk_window_set_cursor(gtk_widget_get_window(inst->area),
                                  inst->rawcursor);
        } else {
            gdk_window_set_cursor(gtk_widget_get_window(inst->area),
                                  inst->textcursor);
        }
        break;
      case BUSY_WAITING:    /* XXX can we do better? */
      case BUSY_CPU:
        /* We always display these cursors. */
        gdk_window_set_cursor(gtk_widget_get_window(inst->area),
                              inst->waitcursor);
        break;
      default:
        unreachable("Bad busy_status");
    }
}

static void show_mouseptr(GtkFrontend *inst, bool show)
{
    if (!conf_get_bool(inst->conf, CONF_hide_mouseptr))
        show = true;
    inst->mouseptr_visible = show;
    update_mouseptr(inst);
}

static void draw_backing_rect(GtkFrontend *inst);

static void drawing_area_setup(GtkFrontend *inst, int width, int height)
{
    int w, h, new_scale;

    /*
     * See if the terminal size has changed.
     */
    w = (width - 2*inst->window_border) / inst->font_width;
    h = (height - 2*inst->window_border) / inst->font_height;
    if (w != inst->width || h != inst->height) {
        /*
         * Update conf.
         */
        inst->width = w;
        inst->height = h;
        conf_set_int(inst->conf, CONF_width, inst->width);
        conf_set_int(inst->conf, CONF_height, inst->height);
        /*
         * We must refresh the window's backing image.
         */
        inst->drawing_area_setup_needed = true;
    }

#if GTK_CHECK_VERSION(3,10,0)
    new_scale = gtk_widget_get_scale_factor(inst->area);
    if (new_scale != inst->scale)
        inst->drawing_area_setup_needed = true;
#else
    new_scale = 1;
#endif

    int new_backing_w = w * inst->font_width + 2*inst->window_border;
    int new_backing_h = h * inst->font_height + 2*inst->window_border;
    new_backing_w *= new_scale;
    new_backing_h *= new_scale;

    if (inst->backing_w != new_backing_w || inst->backing_h != new_backing_h)
        inst->drawing_area_setup_needed = true;

    /*
     * GTK will sometimes send us configure events when nothing about
     * the window size has actually changed. In some situations this
     * can happen quite often, so it's a worthwhile optimisation to
     * detect that situation and avoid the expensive reinitialisation
     * of the backing surface / image, and so on.
     *
     * However, we must still communicate to the terminal that we
     * received a resize event, because sometimes a trivial resize
     * event (to the same size we already were) is a signal from the
     * window system that a _nontrivial_ resize we recently asked for
     * has failed to happen.
     */

    inst->drawing_area_setup_called = true;
    if (inst->term)
        term_size(inst->term, h, w, conf_get_int(inst->conf, CONF_savelines));

    if (!inst->drawing_area_setup_needed)
        return;

    inst->drawing_area_setup_needed = false;
    inst->scale = new_scale;
    inst->backing_w = new_backing_w;
    inst->backing_h = new_backing_h;

#ifndef NO_BACKING_PIXMAPS
    if (inst->pixmap) {
        gdk_pixmap_unref(inst->pixmap);
        inst->pixmap = NULL;
    }

    inst->pixmap = gdk_pixmap_new(gtk_widget_get_window(inst->area),
                                  inst->backing_w, inst->backing_h, -1);
#endif

#ifdef DRAW_TEXT_CAIRO
    if (inst->surface) {
        cairo_surface_destroy(inst->surface);
        inst->surface = NULL;
    }

    inst->surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, inst->backing_w, inst->backing_h);
#endif

    draw_backing_rect(inst);

    if (inst->term)
        term_invalidate(inst->term);

#if GTK_CHECK_VERSION(2,0,0)
    gtk_im_context_set_client_window(
        inst->imc, gtk_widget_get_window(inst->area));
#endif
}

static void drawing_area_setup_simple(GtkFrontend *inst)
{
    /*
     * Wrapper on drawing_area_setup which fetches the width and
     * height of the drawing area. We go directly to the inner version
     * in the case where a new size allocation comes in (just in case
     * GTK hasn't installed it in the normal place yet).
     */
#if GTK_CHECK_VERSION(2,0,0)
    GdkRectangle alloc;
    gtk_widget_get_allocation(inst->area, &alloc);
#else
    GtkAllocation alloc = inst->area->allocation;
#endif
    drawing_area_setup(inst, alloc.width, alloc.height);
}

static void drawing_area_setup_cb(void *vctx)
{
    GtkFrontend *inst = (GtkFrontend *)vctx;

    if (!inst->drawing_area_setup_called)
        drawing_area_setup_simple(inst);
}

static void area_realised(GtkWidget *widget, GtkFrontend *inst)
{
    inst->drawing_area_realised = true;
    if (inst->drawing_area_realised && inst->drawing_area_got_size &&
        inst->drawing_area_setup_needed)
        drawing_area_setup_simple(inst);
}

static void area_size_allocate(
    GtkWidget *widget, GdkRectangle *alloc, GtkFrontend *inst)
{
    inst->drawing_area_got_size = true;
    if (inst->drawing_area_realised && inst->drawing_area_got_size)
        drawing_area_setup(inst, alloc->width, alloc->height);
}

#if GTK_CHECK_VERSION(3,10,0)
static void area_check_scale(GtkFrontend *inst)
{
    if (!inst->drawing_area_setup_needed &&
        inst->scale != gtk_widget_get_scale_factor(inst->area)) {
        drawing_area_setup_simple(inst);
        if (inst->term) {
            term_invalidate(inst->term);
            term_update(inst->term);
        }
    }
}
#endif

static gboolean window_configured(
    GtkWidget *widget, GdkEventConfigure *event, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    if (inst->term) {
        term_notify_window_pos(inst->term, event->x, event->y);
        term_notify_window_size_pixels(
            inst->term, event->width, event->height);
        if (inst->drawing_area_realised && inst->drawing_area_got_size) {
            inst->drawing_area_setup_called = false;
            queue_toplevel_callback(drawing_area_setup_cb, inst);
        }
    }
    return false;
}

#if GTK_CHECK_VERSION(3,10,0)
static gboolean area_configured(
    GtkWidget *widget, GdkEventConfigure *event, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    area_check_scale(inst);
    return false;
}
#endif

#ifdef DRAW_TEXT_CAIRO
static void cairo_setup_draw_ctx(GtkFrontend *inst)
{
    cairo_get_matrix(inst->uctx.u.cairo.cr,
                     &inst->uctx.u.cairo.origmatrix);
    cairo_set_line_width(inst->uctx.u.cairo.cr, 1.0);
    cairo_set_line_cap(inst->uctx.u.cairo.cr, CAIRO_LINE_CAP_SQUARE);
    cairo_set_line_join(inst->uctx.u.cairo.cr, CAIRO_LINE_JOIN_MITER);
    /* This antialiasing setting appears to be ignored for Pango
     * font rendering but honoured for stroking and filling paths;
     * I don't quite understand the logic of that, but I won't
     * complain since it's exactly what I happen to want */
    cairo_set_antialias(inst->uctx.u.cairo.cr, CAIRO_ANTIALIAS_NONE);
}
#endif

#if GTK_CHECK_VERSION(3,0,0)
static gint draw_area(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;

#if GTK_CHECK_VERSION(3,10,0)
    /*
     * This may be the first we hear of the window scale having
     * changed, in which case we must hastily reconstruct our backing
     * surface before we copy the wrong one into the newly resized
     * real window.
     */
    area_check_scale(inst);
#endif

    /*
     * GTK3 window redraw: we always expect Cairo to be enabled, so
     * that inst->surface exists, and pixmaps to be disabled, so that
     * inst->pixmap does not exist. Hence, we just blit from
     * inst->surface to the window.
     */
    if (inst->surface) {
        GdkRectangle dirtyrect;
        cairo_surface_t *target_surface;
        double orig_sx, orig_sy;
        cairo_matrix_t m;

        /*
         * Furtle around in the Cairo setup to force the device scale
         * back to 1, so that when we blit a collection of pixels from
         * our backing surface into the window, they really are
         * _pixels_ and not some confusing antialiased slightly-offset
         * 2x2 rectangle of pixeloids.
         *
         * I have no idea whether GTK expects me not to mess with the
         * device scale in the cairo_surface_t backing its window, so
         * I carefully put it back when I've finished.
         *
         * In some GTK setups, the Cairo context we're given may not
         * have a zero translation offset in its matrix, in which case
         * we have to adjust that to compensate for the change of
         * scale, or else the old translation offset (designed for the
         * old scale) will be multiplied by the new scale instead and
         * put everything in the wrong place.
         */
        target_surface = cairo_get_target(cr);
        cairo_get_matrix(cr, &m);
        cairo_surface_get_device_scale(target_surface, &orig_sx, &orig_sy);
        cairo_surface_set_device_scale(target_surface, 1.0, 1.0);
        cairo_translate(cr, m.x0 * (orig_sx - 1.0), m.y0 * (orig_sy - 1.0));

        gdk_cairo_get_clip_rectangle(cr, &dirtyrect);

        cairo_set_source_surface(cr, inst->surface, 0, 0);
        cairo_rectangle(cr, dirtyrect.x, dirtyrect.y,
                        dirtyrect.width, dirtyrect.height);
        cairo_fill(cr);

        cairo_surface_set_device_scale(target_surface, orig_sx, orig_sy);
    }

    return true;
}
#else
gint expose_area(GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;

#ifndef NO_BACKING_PIXMAPS
    /*
     * Draw to the exposed part of the window from the server-side
     * backing pixmap.
     */
    if (inst->pixmap) {
        gdk_draw_pixmap(gtk_widget_get_window(widget),
                        (gtk_widget_get_style(widget)->fg_gc
                         [gtk_widget_get_state(widget)]),
                        inst->pixmap,
                        event->area.x, event->area.y,
                        event->area.x, event->area.y,
                        event->area.width, event->area.height);
    }
#else
    /*
     * Failing that, draw from the client-side Cairo surface. (We
     * should never be compiled in a context where we have _neither_
     * inst->surface nor inst->pixmap.)
     */
    if (inst->surface) {
        cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(widget));
        cairo_set_source_surface(cr, inst->surface, 0, 0);
        cairo_rectangle(cr, event->area.x, event->area.y,
                        event->area.width, event->area.height);
        cairo_fill(cr);
        cairo_destroy(cr);
    }
#endif

    return true;
}
#endif

#define KEY_PRESSED(k) \
    (inst->keystate[(k) / 32] & (1 << ((k) % 32)))

#ifdef KEY_EVENT_DIAGNOSTICS
char *dup_keyval_name(guint keyval)
{
    const char *name = gdk_keyval_name(keyval);
    if (name)
        return dupstr(name);
    else
        return dupprintf("UNKNOWN[%u]", (unsigned)keyval);
}
#endif

static void change_font_size(GtkFrontend *inst, int increment);
static void key_pressed(GtkFrontend *inst);

/* Subroutine used in key_event */
static int return_key(GtkFrontend *inst, char *output, bool *special)
{
    int end;

    /* Ugly label so we can come here as a fallback from
     * numeric keypad Enter handling */
    if (inst->term->cr_lf_return) {
#ifdef KEY_EVENT_DIAGNOSTICS
        debug(" - Return in cr_lf_return mode, translating as 0d 0a\n");
#endif
        output[1] = '\015';
        output[2] = '\012';
        end = 3;
    } else {
#ifdef KEY_EVENT_DIAGNOSTICS
        debug(" - Return special case, translating as 0d + special\n");
#endif
        output[1] = '\015';
        end = 2;
        *special = true;
    }

    return end;
}

gint key_event(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    char output[256];
    wchar_t ucsoutput[2];
    int ucsval, start, end, output_charset;
    bool special, use_ucsoutput;
    bool force_format_numeric_keypad = false;
    bool generated_something = false;
    char num_keypad_key = '\0';
    const char *event_string = event->string ? event->string : "";

    noise_ultralight(NOISE_SOURCE_KEY, event->keyval);

#ifdef OSX_META_KEY_CONFIG
    if (event->state & inst->system_mod_mask)
        return false;                  /* let GTK process OS X Command key */
#endif

    /* Remember the timestamp. */
    inst->input_event_time = event->time;

    /* By default, nothing is generated. */
    end = start = 0;
    special = use_ucsoutput = false;
    output_charset = CS_ISO8859_1;

#ifdef KEY_EVENT_DIAGNOSTICS
    {
        char *type_string, *state_string, *keyval_string, *string_string;

        type_string = (event->type == GDK_KEY_PRESS ? dupstr("PRESS") :
                       event->type == GDK_KEY_RELEASE ? dupstr("RELEASE") :
                       dupprintf("UNKNOWN[%d]", (int)event->type));

        {
            static const struct {
                int mod_bit;
                const char *name;
            } mod_bits[] = {
                {GDK_SHIFT_MASK, "SHIFT"},
                {GDK_LOCK_MASK, "LOCK"},
                {GDK_CONTROL_MASK, "CONTROL"},
                {GDK_MOD1_MASK, "MOD1"},
                {GDK_MOD2_MASK, "MOD2"},
                {GDK_MOD3_MASK, "MOD3"},
                {GDK_MOD4_MASK, "MOD4"},
                {GDK_MOD5_MASK, "MOD5"},
                {GDK_SUPER_MASK, "SUPER"},
                {GDK_HYPER_MASK, "HYPER"},
                {GDK_META_MASK, "META"},
            };
            int i;
            int val = event->state;

            state_string = dupstr("");

            for (i = 0; i < lenof(mod_bits); i++) {
                if (val & mod_bits[i].mod_bit) {
                    char *old = state_string;
                    state_string = dupcat(state_string,
                                          state_string[0] ? "|" : "",
                                          mod_bits[i].name);
                    sfree(old);

                    val &= ~mod_bits[i].mod_bit;
                }
            }

            if (val || !state_string[0]) {
                char *old = state_string;
                state_string = dupprintf("%s%s%d", state_string,
                                         state_string[0] ? "|" : "", val);
                sfree(old);
            }
        }

        keyval_string = dup_keyval_name(event->keyval);

        string_string = dupstr("");
        {
            int i;
            for (i = 0; event_string[i]; i++) {
                char *old = string_string;
                string_string = dupprintf("%s%s%02x", string_string,
                                          string_string[0] ? " " : "",
                                          (unsigned)event_string[i] & 0xFF);
                sfree(old);
            }
        }

        debug("key_event: type=%s keyval=%s state=%s "
              "hardware_keycode=%d is_modifier=%s string=[%s]\n",
              type_string, keyval_string, state_string,
              (int)event->hardware_keycode,
              event->is_modifier ? "true" : "false",
              string_string);

        sfree(type_string);
        sfree(state_string);
        sfree(keyval_string);
        sfree(string_string);
    }
#endif /* KEY_EVENT_DIAGNOSTICS */

    /*
     * If Alt is being released after typing an Alt+numberpad
     * sequence, we should generate the code that was typed.
     *
     * Note that we only do this if more than one key was actually
     * pressed - I don't think Alt+NumPad4 should be ^D or that
     * Alt+NumPad3 should be ^C, for example. There's no serious
     * inconvenience in having to type a zero before a single-digit
     * character code.
     */
    if (event->type == GDK_KEY_RELEASE) {
        if ((event->keyval == GDK_KEY_Meta_L ||
             event->keyval == GDK_KEY_Meta_R ||
             event->keyval == GDK_KEY_Alt_L ||
             event->keyval == GDK_KEY_Alt_R) &&
            inst->alt_keycode >= 0 && inst->alt_digits > 1) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - modifier release terminates Alt+numberpad input, "
                  "keycode = %d\n", inst->alt_keycode);
#endif
            /*
             * FIXME: we might usefully try to do something clever here
             * about interpreting the generated key code in a way that's
             * appropriate to the line code page.
             */
            output[0] = inst->alt_keycode;
            end = 1;
            goto done;
        }
#if GTK_CHECK_VERSION(2,0,0)
#ifdef KEY_EVENT_DIAGNOSTICS
        debug(" - key release, passing to IM\n");
#endif
        if (gtk_im_context_filter_keypress(inst->imc, event)) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - key release accepted by IM\n");
#endif
            return true;
        } else {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - key release not accepted by IM\n");
#endif
        }
#endif
    }

    if (event->type == GDK_KEY_PRESS) {
        /*
         * If Alt has just been pressed, we start potentially
         * accumulating an Alt+numberpad code. We do this by
         * setting alt_keycode to -1 (nothing yet but plausible).
         */
        if ((event->keyval == GDK_KEY_Meta_L ||
             event->keyval == GDK_KEY_Meta_R ||
             event->keyval == GDK_KEY_Alt_L ||
             event->keyval == GDK_KEY_Alt_R)) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - modifier press potentially begins Alt+numberpad "
                  "input\n");
#endif
            inst->alt_keycode = -1;
            inst->alt_digits = 0;
            goto done;                 /* this generates nothing else */
        }

        /*
         * If we're seeing a numberpad key press with Meta down,
         * consider adding it to alt_keycode if that's sensible.
         * Anything _else_ with Meta down cancels any possibility
         * of an ALT keycode: we set alt_keycode to -2.
         */
        if ((event->state & inst->meta_mod_mask) && inst->alt_keycode != -2) {
            int digit = -1;
            switch (event->keyval) {
              case GDK_KEY_KP_0: case GDK_KEY_KP_Insert: digit = 0; break;
              case GDK_KEY_KP_1: case GDK_KEY_KP_End: digit = 1; break;
              case GDK_KEY_KP_2: case GDK_KEY_KP_Down: digit = 2; break;
              case GDK_KEY_KP_3: case GDK_KEY_KP_Page_Down: digit = 3; break;
              case GDK_KEY_KP_4: case GDK_KEY_KP_Left: digit = 4; break;
              case GDK_KEY_KP_5: case GDK_KEY_KP_Begin: digit = 5; break;
              case GDK_KEY_KP_6: case GDK_KEY_KP_Right: digit = 6; break;
              case GDK_KEY_KP_7: case GDK_KEY_KP_Home: digit = 7; break;
              case GDK_KEY_KP_8: case GDK_KEY_KP_Up: digit = 8; break;
              case GDK_KEY_KP_9: case GDK_KEY_KP_Page_Up: digit = 9; break;
            }
            if (digit < 0)
                inst->alt_keycode = -2;   /* it's invalid */
            else {
#if defined(DEBUG) && defined(KEY_EVENT_DIAGNOSTICS)
                int old_keycode = inst->alt_keycode;
#endif
                if (inst->alt_keycode == -1)
                    inst->alt_keycode = digit;   /* one-digit code */
                else
                    inst->alt_keycode = inst->alt_keycode * 10 + digit;
                inst->alt_digits++;
#ifdef KEY_EVENT_DIAGNOSTICS
                debug(" - Alt+numberpad digit %d added to keycode %d"
                      " gives %d\n", digit, old_keycode, inst->alt_keycode);
#endif
                /* Having used this digit, we now do nothing more with it. */
                goto done;
            }
        }

        if (event->keyval == GDK_KEY_greater &&
            (event->state & GDK_CONTROL_MASK)) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - Ctrl->: increase font size\n");
#endif
            change_font_size(inst, +1);
            return true;
        }
        if (event->keyval == GDK_KEY_less &&
            (event->state & GDK_CONTROL_MASK)) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - Ctrl-<: increase font size\n");
#endif
            change_font_size(inst, -1);
            return true;
        }

        /*
         * Shift-PgUp and Shift-PgDn don't even generate keystrokes
         * at all.
         */
        if (event->keyval == GDK_KEY_Page_Up &&
            ((event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) ==
             (GDK_CONTROL_MASK | GDK_SHIFT_MASK))) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - Ctrl-Shift-PgUp scroll\n");
#endif
            term_scroll(inst->term, 1, 0);
            return true;
        }
        if (event->keyval == GDK_KEY_Page_Up &&
            (event->state & GDK_SHIFT_MASK)) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - Shift-PgUp scroll\n");
#endif
            term_scroll(inst->term, 0, -inst->height/2);
            return true;
        }
        if (event->keyval == GDK_KEY_Page_Up &&
            (event->state & GDK_CONTROL_MASK)) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - Ctrl-PgUp scroll\n");
#endif
            term_scroll(inst->term, 0, -1);
            return true;
        }
        if (event->keyval == GDK_KEY_Page_Down &&
            ((event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) ==
             (GDK_CONTROL_MASK | GDK_SHIFT_MASK))) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - Ctrl-shift-PgDn scroll\n");
#endif
            term_scroll(inst->term, -1, 0);
            return true;
        }
        if (event->keyval == GDK_KEY_Page_Down &&
            (event->state & GDK_SHIFT_MASK)) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - Shift-PgDn scroll\n");
#endif
            term_scroll(inst->term, 0, +inst->height/2);
            return true;
        }
        if (event->keyval == GDK_KEY_Page_Down &&
            (event->state & GDK_CONTROL_MASK)) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - Ctrl-PgDn scroll\n");
#endif
            term_scroll(inst->term, 0, +1);
            return true;
        }

        /*
         * Neither do Shift-Ins or Ctrl-Ins (if enabled).
         */
        if (event->keyval == GDK_KEY_Insert &&
            (event->state & GDK_SHIFT_MASK)) {
            int cfgval = conf_get_int(inst->conf, CONF_ctrlshiftins);

            switch (cfgval) {
              case CLIPUI_IMPLICIT:
#ifdef KEY_EVENT_DIAGNOSTICS
                debug(" - Shift-Insert: paste from PRIMARY\n");
#endif
                term_request_paste(inst->term, CLIP_PRIMARY);
                return true;
              case CLIPUI_EXPLICIT:
#ifdef KEY_EVENT_DIAGNOSTICS
                debug(" - Shift-Insert: paste from CLIPBOARD\n");
#endif
                term_request_paste(inst->term, CLIP_CLIPBOARD);
                return true;
              case CLIPUI_CUSTOM:
#ifdef KEY_EVENT_DIAGNOSTICS
                debug(" - Shift-Insert: paste from custom clipboard\n");
#endif
                term_request_paste(inst->term, inst->clipboard_ctrlshiftins);
                return true;
              default:
#ifdef KEY_EVENT_DIAGNOSTICS
                debug(" - Shift-Insert: no paste action\n");
#endif
                break;
            }
        }
        if (event->keyval == GDK_KEY_Insert &&
            (event->state & GDK_CONTROL_MASK)) {
            static const int clips_clipboard[] = { CLIP_CLIPBOARD };
            int cfgval = conf_get_int(inst->conf, CONF_ctrlshiftins);

            switch (cfgval) {
              case CLIPUI_IMPLICIT:
                /* do nothing; re-copy to PRIMARY is not needed */
#ifdef KEY_EVENT_DIAGNOSTICS
                debug(" - Ctrl-Insert: non-copy to PRIMARY\n");
#endif
                return true;
              case CLIPUI_EXPLICIT:
#ifdef KEY_EVENT_DIAGNOSTICS
                debug(" - Ctrl-Insert: copy to CLIPBOARD\n");
#endif
                term_request_copy(inst->term,
                                  clips_clipboard, lenof(clips_clipboard));
                return true;
              case CLIPUI_CUSTOM:
#ifdef KEY_EVENT_DIAGNOSTICS
                debug(" - Ctrl-Insert: copy to custom clipboard\n");
#endif
                term_request_copy(inst->term,
                                  &inst->clipboard_ctrlshiftins, 1);
                return true;
              default:
#ifdef KEY_EVENT_DIAGNOSTICS
                debug(" - Ctrl-Insert: no copy action\n");
#endif
                break;
            }
        }

        /*
         * Another pair of copy-paste keys.
         */
        if ((event->state & GDK_SHIFT_MASK) &&
            (event->state & GDK_CONTROL_MASK) &&
            (event->keyval == GDK_KEY_C || event->keyval == GDK_KEY_c ||
             event->keyval == GDK_KEY_V || event->keyval == GDK_KEY_v)) {
            int cfgval = conf_get_int(inst->conf, CONF_ctrlshiftcv);
            bool paste = (event->keyval == GDK_KEY_V ||
                          event->keyval == GDK_KEY_v);

            switch (cfgval) {
              case CLIPUI_IMPLICIT:
                if (paste) {
#ifdef KEY_EVENT_DIAGNOSTICS
                    debug(" - Ctrl-Shift-V: paste from PRIMARY\n");
#endif
                    term_request_paste(inst->term, CLIP_PRIMARY);
                } else {
#ifdef KEY_EVENT_DIAGNOSTICS
                    debug(" - Ctrl-Shift-C: non-copy to PRIMARY\n");
#endif
                }
                return true;
              case CLIPUI_EXPLICIT:
                if (paste) {
#ifdef KEY_EVENT_DIAGNOSTICS
                    debug(" - Ctrl-Shift-V: paste from CLIPBOARD\n");
#endif
                    term_request_paste(inst->term, CLIP_CLIPBOARD);
                } else {
                    static const int clips[] = { CLIP_CLIPBOARD };
#ifdef KEY_EVENT_DIAGNOSTICS
                    debug(" - Ctrl-Shift-C: copy to CLIPBOARD\n");
#endif
                    term_request_copy(inst->term, clips, lenof(clips));
                }
                return true;
              case CLIPUI_CUSTOM:
                if (paste) {
#ifdef KEY_EVENT_DIAGNOSTICS
                    debug(" - Ctrl-Shift-V: paste from custom clipboard\n");
#endif
                    term_request_paste(inst->term,
                                       inst->clipboard_ctrlshiftcv);
                } else {
#ifdef KEY_EVENT_DIAGNOSTICS
                    debug(" - Ctrl-Shift-C: copy to custom clipboard\n");
#endif
                    term_request_copy(inst->term,
                                      &inst->clipboard_ctrlshiftcv, 1);
                }
                return true;
            }
        }

        special = false;
        use_ucsoutput = false;

        /* ALT+things gives leading Escape. */
        output[0] = '\033';
#if !GTK_CHECK_VERSION(2,0,0)
        /*
         * In vanilla X, and hence also GDK 1.2, the string received
         * as part of a keyboard event is assumed to be in
         * ISO-8859-1. (Seems woefully shortsighted in i18n terms,
         * but it's true: see the man page for XLookupString(3) for
         * confirmation.)
         */
        output_charset = CS_ISO8859_1;
        strncpy(output+1, event_string, lenof(output)-1);
#else /* !GTK_CHECK_VERSION(2,0,0) */
        /*
         * Most things can now be passed to
         * gtk_im_context_filter_keypress without breaking anything
         * below this point. An exception is the numeric keypad if
         * we're in Nethack or application mode: the IM will eat
         * numeric keypad presses if Num Lock is on, but we don't want
         * it to.
         */
        bool numeric = false;
        bool nethack_mode = conf_get_bool(inst->conf, CONF_nethack_keypad);
        bool app_keypad_mode = (inst->term->app_keypad_keys &&
                                !conf_get_bool(inst->conf, CONF_no_applic_k));

        switch (event->keyval) {
          case GDK_KEY_Num_Lock: num_keypad_key = 'G'; break;
          case GDK_KEY_KP_Divide: num_keypad_key = '/'; break;
          case GDK_KEY_KP_Multiply: num_keypad_key = '*'; break;
          case GDK_KEY_KP_Subtract: num_keypad_key = '-'; break;
          case GDK_KEY_KP_Add: num_keypad_key = '+'; break;
          case GDK_KEY_KP_Enter: num_keypad_key = '\r'; break;
          case GDK_KEY_KP_0: num_keypad_key = '0'; numeric = true; break;
          case GDK_KEY_KP_Insert: num_keypad_key = '0'; break;
          case GDK_KEY_KP_1: num_keypad_key = '1'; numeric = true; break;
          case GDK_KEY_KP_End: num_keypad_key = '1'; break;
          case GDK_KEY_KP_2: num_keypad_key = '2'; numeric = true; break;
          case GDK_KEY_KP_Down: num_keypad_key = '2'; break;
          case GDK_KEY_KP_3: num_keypad_key = '3'; numeric = true; break;
          case GDK_KEY_KP_Page_Down: num_keypad_key = '3'; break;
          case GDK_KEY_KP_4: num_keypad_key = '4'; numeric = true; break;
          case GDK_KEY_KP_Left: num_keypad_key = '4'; break;
          case GDK_KEY_KP_5: num_keypad_key = '5'; numeric = true; break;
          case GDK_KEY_KP_Begin: num_keypad_key = '5'; break;
          case GDK_KEY_KP_6: num_keypad_key = '6'; numeric = true; break;
          case GDK_KEY_KP_Right: num_keypad_key = '6'; break;
          case GDK_KEY_KP_7: num_keypad_key = '7'; numeric = true; break;
          case GDK_KEY_KP_Home: num_keypad_key = '7'; break;
          case GDK_KEY_KP_8: num_keypad_key = '8'; numeric = true; break;
          case GDK_KEY_KP_Up: num_keypad_key = '8'; break;
          case GDK_KEY_KP_9: num_keypad_key = '9'; numeric = true; break;
          case GDK_KEY_KP_Page_Up: num_keypad_key = '9'; break;
          case GDK_KEY_KP_Decimal: num_keypad_key = '.'; numeric = true; break;
          case GDK_KEY_KP_Delete: num_keypad_key = '.'; break;
        }
        if ((app_keypad_mode && num_keypad_key &&
             (numeric || inst->term->funky_type != FUNKY_XTERM)) ||
            (nethack_mode && num_keypad_key >= '1' && num_keypad_key <= '9')) {
            /* In these modes, we override the keypad handling:
             * regardless of Num Lock, the keys are handled by
             * format_numeric_keypad_key below. */
            force_format_numeric_keypad = true;
        } else {
            bool try_filter = true;

#ifdef META_MANUAL_MASK
            if (event->state & META_MANUAL_MASK & inst->meta_mod_mask) {
                /*
                 * If this key event had a Meta modifier bit set which
                 * is also in META_MANUAL_MASK, that means passing
                 * such an event to the GtkIMContext will be unhelpful
                 * (it will eat the keystroke and turn it into
                 * something not what we wanted).
                 */
#ifdef KEY_EVENT_DIAGNOSTICS
                debug(" - Meta modifier requiring manual intervention, "
                      "suppressing IM filtering\n");
#endif
                try_filter = false;
            }
#endif

            if (try_filter) {
#ifdef KEY_EVENT_DIAGNOSTICS
                debug(" - general key press, passing to IM\n");
#endif
                if (gtk_im_context_filter_keypress(inst->imc, event)) {
#ifdef KEY_EVENT_DIAGNOSTICS
                    debug(" - key press accepted by IM\n");
#endif
                    return true;
                } else {
#ifdef KEY_EVENT_DIAGNOSTICS
                    debug(" - key press not accepted by IM\n");
#endif
                }
            }
        }

        /*
         * GDK 2.0 arranges to have done some translation for us: in
         * GDK 2.0, event->string is encoded in the current locale.
         *
         * So we use the standard C library function mbstowcs() to
         * convert from the current locale into Unicode; from there
         * we can convert to whatever PuTTY is currently working in.
         * (In fact I convert straight back to UTF-8 from
         * wide-character Unicode, for the sake of simplicity: that
         * way we can still use exactly the same code to manipulate
         * the string, such as prefixing ESC.)
         */
        output_charset = CS_UTF8;
        {
            wchar_t widedata[32];
            const wchar_t *wp;
            int wlen;
            int ulen;

            wlen = mb_to_wc(DEFAULT_CODEPAGE, 0,
                            event_string, strlen(event_string),
                            widedata, lenof(widedata)-1);

#ifdef KEY_EVENT_DIAGNOSTICS
            {
                char *string_string = dupstr("");
                int i;

                for (i = 0; i < wlen; i++) {
                    char *old = string_string;
                    string_string = dupprintf("%s%s%04x", string_string,
                                              string_string[0] ? " " : "",
                                              (unsigned)widedata[i]);
                    sfree(old);
                }
                debug(" - string translated into Unicode = [%s]\n",
                      string_string);
                sfree(string_string);
            }
#endif

            wp = widedata;
            ulen = charset_from_unicode(&wp, &wlen, output+1, lenof(output)-2,
                                        CS_UTF8, NULL, NULL, 0);

#ifdef KEY_EVENT_DIAGNOSTICS
            {
                char *string_string = dupstr("");
                int i;

                for (i = 0; i < ulen; i++) {
                    char *old = string_string;
                    string_string = dupprintf("%s%s%02x", string_string,
                                              string_string[0] ? " " : "",
                                              (unsigned)output[i+1] & 0xFF);
                    sfree(old);
                }
                debug(" - string translated into UTF-8 = [%s]\n",
                      string_string);
                sfree(string_string);
            }
#endif

            output[1+ulen] = '\0';
        }
#endif /* !GTK_CHECK_VERSION(2,0,0) */

        if (!output[1] &&
            (ucsval = keysym_to_unicode(event->keyval)) >= 0) {
            ucsoutput[0] = '\033';
            ucsoutput[1] = ucsval;
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - keysym_to_unicode gave %04x\n",
                  (unsigned)ucsoutput[1]);
#endif
            use_ucsoutput = true;
            end = 2;
        } else {
            output[lenof(output)-1] = '\0';
            end = strlen(output);
        }
        if (event->state & inst->meta_mod_mask) {
            start = 0;
            if (end == 1) end = 0;

#ifdef META_MANUAL_MASK
            if (event->state & META_MANUAL_MASK) {
                /*
                 * Key events which have a META_MANUAL_MASK meta bit
                 * set may have a keyval reflecting that, e.g. on OS X
                 * the Option key acts as an AltGr-like modifier and
                 * causes different Unicode characters to be output.
                 *
                 * To work around this, we clear the dangerous
                 * modifier bit and retranslate from the hardware
                 * keycode as if the key had been pressed without that
                 * modifier. Then we prefix Esc to *that*.
                 */
                guint new_keyval;
                GdkModifierType consumed;
                if (gdk_keymap_translate_keyboard_state
                    (gdk_keymap_get_for_display(gdk_display_get_default()),
                     event->hardware_keycode, event->state & ~META_MANUAL_MASK,
                     0, &new_keyval, NULL, NULL, &consumed)) {
                    ucsoutput[0] = '\033';
                    ucsoutput[1] = gdk_keyval_to_unicode(new_keyval);
#ifdef KEY_EVENT_DIAGNOSTICS
                    {
                        char *keyval_name = dup_keyval_name(new_keyval);
                        debug(" - retranslation for manual Meta: "
                              "new keyval = %s, Unicode = %04x\n",
                              keyval_name, (unsigned)ucsoutput[1]);
                        sfree(keyval_name);
                    }
#endif
                    use_ucsoutput = true;
                    end = 2;
                }
            }
#endif
        } else
            start = 1;

        /* Control-` is the same as Control-\ (unless gtk has a better idea) */
        if (!output[1] && event->keyval == '`' &&
            (event->state & GDK_CONTROL_MASK)) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - Ctrl-` special case, translating as 1c\n");
#endif
            output[1] = '\x1C';
            use_ucsoutput = false;
            end = 2;
        }

        /* Some GTK backends (e.g. Quartz) do not change event->string
         * in response to the Control modifier. So we do it ourselves
         * here, if it's not already happened.
         *
         * The translations below are in line with X11 policy as far
         * as I know. */
        if ((event->state & GDK_CONTROL_MASK) && end == 2) {
            int orig = use_ucsoutput ? ucsoutput[1] : output[1];
            int new = orig;

            if (new >= '3' && new <= '7') {
                /* ^3,...,^7 map to 0x1B,...,0x1F */
                new += '\x1B' - '3';
            } else if (new == '2' || new == ' ') {
                /* ^2 and ^Space are both ^@, i.e. \0 */
                new = '\0';
            } else if (new == '8') {
                /* ^8 is DEL */
                new = '\x7F';
            } else if (new == '/') {
                /* ^/ is the same as ^_ */
                new = '\x1F';
            } else if (new >= 0x40 && new < 0x7F) {
                /* Everything anywhere near the alphabetics just gets
                 * masked. */
                new &= 0x1F;
            }
            /* Anything else, e.g. '0', is unchanged. */

            if (orig == new) {
#ifdef KEY_EVENT_DIAGNOSTICS
                debug(" - manual Ctrl key handling did nothing\n");
#endif
            } else {
#ifdef KEY_EVENT_DIAGNOSTICS
                debug(" - manual Ctrl key handling: %02x -> %02x\n",
                      (unsigned)orig, (unsigned)new);
#endif
                output[1] = new;
                use_ucsoutput = false;
            }
        }

        /* Control-Break sends a Break special to the backend */
        if (event->keyval == GDK_KEY_Break &&
            (event->state & GDK_CONTROL_MASK)) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - Ctrl-Break special case, sending SS_BRK\n");
#endif
            if (inst->backend)
                backend_special(inst->backend, SS_BRK, 0);
            return true;
        }

        /* We handle Return ourselves, because it needs to be flagged as
         * special to ldisc. */
        if (event->keyval == GDK_KEY_Return) {
            end = return_key(inst, output, &special);
            use_ucsoutput = false;
        }

        /* Control-2, Control-Space and Control-@ are NUL */
        if (!output[1] &&
            (event->keyval == ' ' || event->keyval == '2' ||
             event->keyval == '@') &&
            (event->state & (GDK_SHIFT_MASK |
                             GDK_CONTROL_MASK)) == GDK_CONTROL_MASK) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - Ctrl-{space,2,@} special case, translating as 00\n");
#endif
            output[1] = '\0';
            use_ucsoutput = false;
            end = 2;
        }

        /* Control-Shift-Space is 160 (ISO8859 nonbreaking space) */
        if (!output[1] && event->keyval == ' ' &&
            (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK)) ==
            (GDK_SHIFT_MASK | GDK_CONTROL_MASK)) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - Ctrl-Shift-space special case, translating as 00a0\n");
#endif
            output[1] = '\240';
            output_charset = CS_ISO8859_1;
            use_ucsoutput = false;
            end = 2;
        }

        /* We don't let GTK tell us what Backspace is! We know better. */
        if (event->keyval == GDK_KEY_BackSpace &&
            !(event->state & GDK_SHIFT_MASK)) {
            output[1] = conf_get_bool(inst->conf, CONF_bksp_is_delete) ?
                '\x7F' : '\x08';
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - Backspace, translating as %02x\n",
                  (unsigned)output[1]);
#endif
            use_ucsoutput = false;
            end = 2;
            special = true;
        }
        /* For Shift Backspace, do opposite of what is configured. */
        if (event->keyval == GDK_KEY_BackSpace &&
            (event->state & GDK_SHIFT_MASK)) {
            output[1] = conf_get_bool(inst->conf, CONF_bksp_is_delete) ?
                '\x08' : '\x7F';
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - Shift-Backspace, translating as %02x\n",
                  (unsigned)output[1]);
#endif
            use_ucsoutput = false;
            end = 2;
            special = true;
        }

        /* Shift-Tab is ESC [ Z */
        if (event->keyval == GDK_KEY_ISO_Left_Tab ||
            (event->keyval == GDK_KEY_Tab &&
             (event->state & GDK_SHIFT_MASK))) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - Shift-Tab, translating as ESC [ Z\n");
#endif
            end = 1 + sprintf(output+1, "\033[Z");
            use_ucsoutput = false;
        }
        /* And normal Tab is Tab, if the keymap hasn't already told us.
         * (Curiously, at least one version of the MacOS 10.5 X server
         * doesn't translate Tab for us. */
        if (event->keyval == GDK_KEY_Tab && end <= 1) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - Tab, translating as 09\n");
#endif
            output[1] = '\t';
            end = 2;
        }

        if (num_keypad_key && force_format_numeric_keypad) {
            end = 1 + format_numeric_keypad_key(
                output+1, inst->term, num_keypad_key,
                event->state & GDK_SHIFT_MASK,
                event->state & GDK_CONTROL_MASK);
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - numeric keypad key");
#endif
            use_ucsoutput = false;
            goto done;
        }

        switch (event->keyval) {
            int fkey_number;
            bool consumed_meta_key;

          case GDK_KEY_F1: fkey_number = 1; goto numbered_function_key;
          case GDK_KEY_F2: fkey_number = 2; goto numbered_function_key;
          case GDK_KEY_F3: fkey_number = 3; goto numbered_function_key;
          case GDK_KEY_F4: fkey_number = 4; goto numbered_function_key;
          case GDK_KEY_F5: fkey_number = 5; goto numbered_function_key;
          case GDK_KEY_F6: fkey_number = 6; goto numbered_function_key;
          case GDK_KEY_F7: fkey_number = 7; goto numbered_function_key;
          case GDK_KEY_F8: fkey_number = 8; goto numbered_function_key;
          case GDK_KEY_F9: fkey_number = 9; goto numbered_function_key;
          case GDK_KEY_F10: fkey_number = 10; goto numbered_function_key;
          case GDK_KEY_F11: fkey_number = 11; goto numbered_function_key;
          case GDK_KEY_F12: fkey_number = 12; goto numbered_function_key;
          case GDK_KEY_F13: fkey_number = 13; goto numbered_function_key;
          case GDK_KEY_F14: fkey_number = 14; goto numbered_function_key;
          case GDK_KEY_F15: fkey_number = 15; goto numbered_function_key;
          case GDK_KEY_F16: fkey_number = 16; goto numbered_function_key;
          case GDK_KEY_F17: fkey_number = 17; goto numbered_function_key;
          case GDK_KEY_F18: fkey_number = 18; goto numbered_function_key;
          case GDK_KEY_F19: fkey_number = 19; goto numbered_function_key;
          case GDK_KEY_F20: fkey_number = 20; goto numbered_function_key;
          numbered_function_key:
            consumed_meta_key = false;
            end = 1 + format_function_key(output+1, inst->term, fkey_number,
                                          event->state & GDK_SHIFT_MASK,
                                          event->state & GDK_CONTROL_MASK,
                                          event->state & inst->meta_mod_mask,
                                          &consumed_meta_key);
            if (consumed_meta_key)
                start = 1; /* supersedes the usual prefixing of Esc */
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - function key F%d", fkey_number);
#endif
            use_ucsoutput = false;
            goto done;

            SmallKeypadKey sk_key;
          case GDK_KEY_Home: case GDK_KEY_KP_Home:
            sk_key = SKK_HOME; goto small_keypad_key;
          case GDK_KEY_Insert: case GDK_KEY_KP_Insert:
            sk_key = SKK_INSERT; goto small_keypad_key;
          case GDK_KEY_Delete: case GDK_KEY_KP_Delete:
            sk_key = SKK_DELETE; goto small_keypad_key;
          case GDK_KEY_End: case GDK_KEY_KP_End:
            sk_key = SKK_END; goto small_keypad_key;
          case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up:
            sk_key = SKK_PGUP; goto small_keypad_key;
          case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down:
            sk_key = SKK_PGDN; goto small_keypad_key;
          small_keypad_key:
            /* These keys don't generate terminal input with Ctrl */
            if (event->state & GDK_CONTROL_MASK)
                break;

            end = 1 + format_small_keypad_key(output+1, inst->term, sk_key);
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - small keypad key");
#endif
            use_ucsoutput = false;
            goto done;

            int xkey;
          case GDK_KEY_Up: case GDK_KEY_KP_Up:
            xkey = 'A'; goto arrow_key;
          case GDK_KEY_Down: case GDK_KEY_KP_Down:
            xkey = 'B'; goto arrow_key;
          case GDK_KEY_Right: case GDK_KEY_KP_Right:
            xkey = 'C'; goto arrow_key;
          case GDK_KEY_Left: case GDK_KEY_KP_Left:
            xkey = 'D'; goto arrow_key;
          case GDK_KEY_Begin: case GDK_KEY_KP_Begin:
            xkey = 'G'; goto arrow_key;
          arrow_key:
            consumed_meta_key = false;
            end = 1 + format_arrow_key(output+1, inst->term, xkey,
                                       event->state & GDK_SHIFT_MASK,
                                       event->state & GDK_CONTROL_MASK,
                                       event->state & inst->meta_mod_mask,
                                       &consumed_meta_key);
            if (consumed_meta_key)
                start = 1; /* supersedes the usual prefixing of Esc */
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - arrow key");
#endif
            use_ucsoutput = false;
            goto done;
        }

        if (num_keypad_key) {
            end = 1 + format_numeric_keypad_key(
                output+1, inst->term, num_keypad_key,
                event->state & GDK_SHIFT_MASK,
                event->state & GDK_CONTROL_MASK);
#ifdef KEY_EVENT_DIAGNOSTICS
            debug(" - numeric keypad key");
#endif

            if (end == 1 && num_keypad_key == '\r') {
                /* Keypad Enter, lacking any other translation,
                 * becomes the same special Return code as normal
                 * Return. */
                end = return_key(inst, output, &special);
                use_ucsoutput = false;
            }

            use_ucsoutput = false;
            goto done;
        }

        goto done;
    }

    done:

    if (end-start > 0) {
        if (special) {
#ifdef KEY_EVENT_DIAGNOSTICS
            char *string_string = dupstr("");
            int i;

            for (i = start; i < end; i++) {
                char *old = string_string;
                string_string = dupprintf("%s%s%02x", string_string,
                                          string_string[0] ? " " : "",
                                          (unsigned)output[i] & 0xFF);
                sfree(old);
            }
            debug(" - final output, special, generic encoding = [%s]\n",
                  string_string);
            sfree(string_string);
#endif
            /*
             * For special control characters, the character set
             * should never matter.
             */
            output[end] = '\0';        /* NUL-terminate */
            generated_something = true;
            term_keyinput(inst->term, -1, output+start, -2);
        } else if (!inst->direct_to_font) {
            if (!use_ucsoutput) {
#ifdef KEY_EVENT_DIAGNOSTICS
                char *string_string = dupstr("");
                int i;

                for (i = start; i < end; i++) {
                    char *old = string_string;
                    string_string = dupprintf("%s%s%02x", string_string,
                                              string_string[0] ? " " : "",
                                              (unsigned)output[i] & 0xFF);
                    sfree(old);
                }
                debug(" - final output in %s = [%s]\n",
                      charset_to_localenc(output_charset), string_string);
                sfree(string_string);
#endif
                generated_something = true;
                term_keyinput(inst->term, output_charset,
                              output+start, end-start);
            } else {
#ifdef KEY_EVENT_DIAGNOSTICS
                char *string_string = dupstr("");
                int i;

                for (i = start; i < end; i++) {
                    char *old = string_string;
                    string_string = dupprintf("%s%s%04x", string_string,
                                              string_string[0] ? " " : "",
                                              (unsigned)ucsoutput[i]);
                    sfree(old);
                }
                debug(" - final output in Unicode = [%s]\n",
                      string_string);
                sfree(string_string);
#endif

                /*
                 * We generated our own Unicode key data from the
                 * keysym, so use that instead.
                 */
                generated_something = true;
                term_keyinputw(inst->term, ucsoutput+start, end-start);
            }
        } else {
            /*
             * In direct-to-font mode, we just send the string
             * exactly as we received it.
             */
#ifdef KEY_EVENT_DIAGNOSTICS
            char *string_string = dupstr("");
            int i;

            for (i = start; i < end; i++) {
                char *old = string_string;
                string_string = dupprintf("%s%s%02x", string_string,
                                          string_string[0] ? " " : "",
                                          (unsigned)output[i] & 0xFF);
                sfree(old);
            }
            debug(" - final output in direct-to-font encoding = [%s]\n",
                  string_string);
            sfree(string_string);
#endif
            generated_something = true;
            term_keyinput(inst->term, -1, output+start, end-start);
        }

        show_mouseptr(inst, false);
    }

    if (generated_something)
        key_pressed(inst);
    return true;
}

#if GTK_CHECK_VERSION(2,0,0)
void input_method_commit_event(GtkIMContext *imc, gchar *str, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;

#ifdef KEY_EVENT_DIAGNOSTICS
    char *string_string = dupstr("");
    int i;

    for (i = 0; str[i]; i++) {
        char *old = string_string;
        string_string = dupprintf("%s%s%02x", string_string,
                                  string_string[0] ? " " : "",
                                  (unsigned)str[i] & 0xFF);
        sfree(old);
    }
    debug(" - IM commit event in UTF-8 = [%s]\n", string_string);
    sfree(string_string);
#endif

    term_keyinput(inst->term, CS_UTF8, str, strlen(str));
    show_mouseptr(inst, false);
    key_pressed(inst);
}
#endif

#define SCROLL_INCREMENT_LINES 5

#if GTK_CHECK_VERSION(3,4,0)
gboolean scroll_internal(GtkFrontend *inst, gdouble delta, guint state,
                         gdouble ex, gdouble ey)
{
    int x, y;
    bool shift, ctrl, alt, raw_mouse_mode;

    show_mouseptr(inst, true);

    shift = state & GDK_SHIFT_MASK;
    ctrl = state & GDK_CONTROL_MASK;
    alt = state & inst->meta_mod_mask;

    x = (ex - inst->window_border) / inst->font_width;
    y = (ey - inst->window_border) / inst->font_height;

    raw_mouse_mode = (inst->send_raw_mouse &&
                      !(shift && conf_get_bool(inst->conf,
                                               CONF_mouse_override)));

    inst->cumulative_scroll += delta * SCROLL_INCREMENT_LINES;

    if (!raw_mouse_mode) {
        int scroll_lines = (int)inst->cumulative_scroll; /* rounds toward 0 */
        if (scroll_lines) {
            term_scroll(inst->term, 0, scroll_lines);
            inst->cumulative_scroll -= scroll_lines;
        }
        return true;
    } else {
        int scroll_events = (int)(inst->cumulative_scroll /
                                  SCROLL_INCREMENT_LINES);
        if (scroll_events) {
            int button;

            inst->cumulative_scroll -= scroll_events * SCROLL_INCREMENT_LINES;

            if (scroll_events > 0) {
                button = MBT_WHEEL_DOWN;
            } else {
                button = MBT_WHEEL_UP;
                scroll_events = -scroll_events;
            }

            while (scroll_events-- > 0) {
                term_mouse(inst->term, button, translate_button(button),
                           MA_CLICK, x, y, shift, ctrl, alt);
            }
        }
        return true;
    }
}
#endif

static gboolean button_internal(GtkFrontend *inst, GdkEventButton *event)
{
    bool shift, ctrl, alt, raw_mouse_mode;
    int x, y, button, act;

    /* Remember the timestamp. */
    inst->input_event_time = event->time;

    noise_ultralight(NOISE_SOURCE_MOUSEBUTTON, event->button);

    show_mouseptr(inst, true);

    shift = event->state & GDK_SHIFT_MASK;
    ctrl = event->state & GDK_CONTROL_MASK;
    alt = event->state & inst->meta_mod_mask;

    raw_mouse_mode = (inst->send_raw_mouse &&
                      !(shift && conf_get_bool(inst->conf,
                                               CONF_mouse_override)));

    if (!raw_mouse_mode) {
        if (event->button == 4 && event->type == GDK_BUTTON_PRESS) {
            term_scroll(inst->term, 0, -SCROLL_INCREMENT_LINES);
            return true;
        }
        if (event->button == 5 && event->type == GDK_BUTTON_PRESS) {
            term_scroll(inst->term, 0, +SCROLL_INCREMENT_LINES);
            return true;
        }
    }

    if (event->button == 3 && ctrl) {
#if GTK_CHECK_VERSION(3,22,0)
        gtk_menu_popup_at_pointer(GTK_MENU(inst->menu), (GdkEvent *)event);
#else
        gtk_menu_popup(GTK_MENU(inst->menu), NULL, NULL, NULL, NULL,
                       event->button, event->time);
#endif
        return true;
    }

    if (event->button == 1)
        button = MBT_LEFT;
    else if (event->button == 2)
        button = MBT_MIDDLE;
    else if (event->button == 3)
        button = MBT_RIGHT;
    else if (event->button == 4)
        button = MBT_WHEEL_UP;
    else if (event->button == 5)
        button = MBT_WHEEL_DOWN;
    else
        return false;                  /* don't even know what button! */

    switch (event->type) {
      case GDK_BUTTON_PRESS: act = MA_CLICK; break;
      case GDK_BUTTON_RELEASE: act = MA_RELEASE; break;
      case GDK_2BUTTON_PRESS: act = MA_2CLK; break;
      case GDK_3BUTTON_PRESS: act = MA_3CLK; break;
      default: return false;           /* don't know this event type */
    }

    if (raw_mouse_mode && act != MA_CLICK && act != MA_RELEASE)
        return true;                   /* we ignore these in raw mouse mode */

    x = (event->x - inst->window_border) / inst->font_width;
    y = (event->y - inst->window_border) / inst->font_height;

    term_mouse(inst->term, button, translate_button(button), act,
               x, y, shift, ctrl, alt);

    return true;
}

gboolean button_event(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    return button_internal(inst, event);
}

#if GTK_CHECK_VERSION(2,0,0)
/*
 * In GTK 2, mouse wheel events have become a new type of event.
 * This handler translates them back into button-4 and button-5
 * presses so that I don't have to change my old code too much :-)
 */
gboolean scroll_event(GtkWidget *widget, GdkEventScroll *event, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    GdkScrollDirection dir;

#if GTK_CHECK_VERSION(3,4,0)
    gdouble dx, dy;
    if (gdk_event_get_scroll_deltas((GdkEvent *)event, &dx, &dy)) {
        return scroll_internal(inst, dy, event->state, event->x, event->y);
    } else if (!gdk_event_get_scroll_direction((GdkEvent *)event, &dir)) {
        return false;
    }
#else
    dir = event->direction;
#endif

    guint button;
    GdkEventButton *event_button;
    gboolean ret;

    if (dir == GDK_SCROLL_UP)
        button = 4;
    else if (dir == GDK_SCROLL_DOWN)
        button = 5;
    else
        return false;

    event_button = (GdkEventButton *)gdk_event_new(GDK_BUTTON_PRESS);
    event_button->window = g_object_ref(event->window);
    event_button->send_event = event->send_event;
    event_button->time = event->time;
    event_button->x = event->x;
    event_button->y = event->y;
    event_button->axes = NULL;
    event_button->state = event->state;
    event_button->button = button;
    event_button->device = g_object_ref(event->device);
    event_button->x_root = event->x_root;
    event_button->y_root = event->y_root;
    ret = button_internal(inst, event_button);
    gdk_event_free((GdkEvent *)event_button);
    return ret;
}
#endif

gint motion_event(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    bool shift, ctrl, alt;
    int x, y, button;

    /* Remember the timestamp. */
    inst->input_event_time = event->time;

    noise_ultralight(NOISE_SOURCE_MOUSEPOS,
                     ((uint32_t)event->x << 16) | (uint32_t)event->y);

    show_mouseptr(inst, true);

    shift = event->state & GDK_SHIFT_MASK;
    ctrl = event->state & GDK_CONTROL_MASK;
    alt = event->state & inst->meta_mod_mask;
    if (event->state & GDK_BUTTON1_MASK)
        button = MBT_LEFT;
    else if (event->state & GDK_BUTTON2_MASK)
        button = MBT_MIDDLE;
    else if (event->state & GDK_BUTTON3_MASK)
        button = MBT_RIGHT;
    else
        return false;                  /* don't even know what button! */

    x = (event->x - inst->window_border) / inst->font_width;
    y = (event->y - inst->window_border) / inst->font_height;

    term_mouse(inst->term, button, translate_button(button), MA_DRAG,
               x, y, shift, ctrl, alt);

    return true;
}

static void key_pressed(GtkFrontend *inst)
{
    /*
     * If our child process has exited but not closed, terminate on
     * any keypress.
     *
     * This is a UI feature specific to GTK PuTTY, because GTK PuTTY
     * will (at least sometimes) be running under X, and under X the
     * window manager is sometimes absent (very occasionally on
     * purpose, more usually temporarily because it's crashed). So
     * it's useful to have a way to close an application window
     * without depending on protocols like WM_DELETE_WINDOW that are
     * typically generated by the WM (e.g. in response to a close
     * button in the window frame).
     */
    if (inst->exited)
        gtk_widget_destroy(inst->window);
}

static void exit_callback(void *vctx)
{
    GtkFrontend *inst = (GtkFrontend *)vctx;
    int exitcode, close_on_exit;

    if (!inst->exited &&
        (exitcode = backend_exitcode(inst->backend)) >= 0) {
        destroy_inst_connection(inst);

        close_on_exit = conf_get_int(inst->conf, CONF_close_on_exit);
        if (close_on_exit == FORCE_ON ||
            (close_on_exit == AUTO && exitcode == 0)) {
            gtk_widget_destroy(inst->window);
        }
    }
}

static void gtk_seat_notify_remote_exit(Seat *seat)
{
    GtkFrontend *inst = container_of(seat, GtkFrontend, seat);
    queue_toplevel_callback(exit_callback, inst);
}

static void destroy_inst_connection(GtkFrontend *inst)
{
    inst->exited = true;
    if (inst->ldisc) {
        ldisc_free(inst->ldisc);
        inst->ldisc = NULL;
    }
    if (inst->backend) {
        backend_free(inst->backend);
        inst->backend = NULL;
    }
    if (inst->term)
        term_provide_backend(inst->term, NULL);
    if (inst->menu) {
        seat_update_specials_menu(&inst->seat);
        gtk_widget_set_sensitive(inst->restartitem, true);
    }
}

static void delete_inst(GtkFrontend *inst)
{
    int dialog_slot;
    for (dialog_slot = 0; dialog_slot < DIALOG_SLOT_LIMIT; dialog_slot++) {
        if (inst->dialogs[dialog_slot]) {
            gtk_widget_destroy(inst->dialogs[dialog_slot]);
            inst->dialogs[dialog_slot] = NULL;
        }
    }
    if (inst->window) {
        gtk_widget_destroy(inst->window);
        inst->window = NULL;
    }
    if (inst->menu) {
        gtk_widget_destroy(inst->menu);
        inst->menu = NULL;
    }
    destroy_inst_connection(inst);
    if (inst->term) {
        term_free(inst->term);
        inst->term = NULL;
    }
    if (inst->conf) {
        conf_free(inst->conf);
        inst->conf = NULL;
    }
    if (inst->logctx) {
        log_free(inst->logctx);
        inst->logctx = NULL;
    }
#if GTK_CHECK_VERSION(2,0,0)
    if (inst->trust_sigil_pb) {
        g_object_unref(G_OBJECT(inst->trust_sigil_pb));
        inst->trust_sigil_pb = NULL;
    }
#else
    if (inst->trust_sigil_pm) {
        gdk_pixmap_unref(inst->trust_sigil_pm);
        inst->trust_sigil_pm = NULL;
    }
#endif

#ifdef JUST_USE_GTK_CLIPBOARD_UTF8
    /*
     * Clear up any in-flight clipboard_data_instances. We can't
     * actually _free_ them, but we detach them from the inst that's
     * about to be destroyed.
     */
    while (inst->cdi_headtail.next != &inst->cdi_headtail) {
        struct clipboard_data_instance *cdi = inst->cdi_headtail.next;
        cdi->state = NULL;
        cdi->next->prev = cdi->prev;
        cdi->prev->next = cdi->next;
        cdi->next = cdi->prev = cdi;
    }
#endif

    /*
     * Delete any top-level callbacks associated with inst, which
     * would otherwise become stale-pointer dereferences waiting to
     * happen. We do this last, because some of the above cleanups
     * (notably shutting down the backend) might themelves queue such
     * callbacks, so we need to make sure they don't do that _after_
     * we're supposed to have cleaned everything up.
     */
    delete_callbacks_for_context(inst);

    eventlogstuff_free(inst->eventlogstuff);

    sfree(inst);
}

void destroy(GtkWidget *widget, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    inst->window = NULL;
    delete_inst(inst);
    session_window_closed();
}

gint focus_event(GtkWidget *widget, GdkEventFocus *event, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    term_set_focus(inst->term, event->in);
    term_update(inst->term);
    show_mouseptr(inst, true);
    return false;
}

static void gtk_seat_set_busy_status(Seat *seat, BusyStatus status)
{
    GtkFrontend *inst = container_of(seat, GtkFrontend, seat);
    inst->busy_status = status;
    update_mouseptr(inst);
}

static void gtkwin_set_raw_mouse_mode(TermWin *tw, bool activate)
{
    GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);
    inst->send_raw_mouse = activate;
}

static void gtkwin_set_raw_mouse_mode_pointer(TermWin *tw, bool activate)
{
    GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);
    inst->pointer_indicates_raw_mouse = activate;
    update_mouseptr(inst);
}

#if GTK_CHECK_VERSION(2,0,0)
static void compute_whole_window_size(GtkFrontend *inst,
                                      int wchars, int hchars,
                                      int *wpix, int *hpix);
#endif

static void gtkwin_deny_term_resize(void *vctx)
{
    GtkFrontend *inst = (GtkFrontend *)vctx;
    if (inst->term)
        term_size(inst->term, inst->term->rows, inst->term->cols,
                  inst->term->savelines);
}

static void gtkwin_request_resize(TermWin *tw, int w, int h)
{
    GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);

#if GTK_CHECK_VERSION(2,0,0)
    /*
     * Initial check: don't even try to resize a window if it's in one
     * of the states that make that impossible. This includes being
     * maximised; being full-screen (if we ever implement that); or
     * being in various tiled states.
     *
     * On X11, the effect of trying to resize the window when it can't
     * be resized should be that the window manager sends us a
     * synthetic ConfigureNotify event restating our existing size
     * (ICCCM section 4.1.5 "Configuring the Window"). That's awkward
     * to deal with, but not impossible; so for X11 alone, we might
     * not bother with this check.
     *
     * (In any case, X11 has other reasons why a window resize might
     * be rejected, which this enumeration can't be aware of in any
     * case. For example, if your window manager is a tiling one, then
     * all your windows are _de facto_ tiled, but not _de jure_ in a
     * way that GDK will know about. So we have to handle those
     * synthetic ConfigureNotifies in any case.)
     *
     * On Wayland, as of GTK 3.24.20, the effects are much worse: it
     * looks to me as if GTK stops ever sending us "draw" events, or
     * even a size_allocate, so the display locks up completely until
     * you toggle the maximised state of the window by some other
     * means. So it's worth checking!
     */
    GdkWindow *gdkwin = gtk_widget_get_window(inst->window);
    if (gdkwin) {
        GdkWindowState state = gdk_window_get_state(gdkwin);
        if (state & (GDK_WINDOW_STATE_MAXIMIZED |
                     GDK_WINDOW_STATE_FULLSCREEN |
#if GTK_CHECK_VERSION(3,0,0)
                     GDK_WINDOW_STATE_TILED |
                     GDK_WINDOW_STATE_TOP_TILED |
                     GDK_WINDOW_STATE_RIGHT_TILED |
                     GDK_WINDOW_STATE_BOTTOM_TILED |
                     GDK_WINDOW_STATE_LEFT_TILED |
#endif
                     0)) {
            queue_toplevel_callback(gtkwin_deny_term_resize, inst);
            return;
        }
    }
#endif

#if !GTK_CHECK_VERSION(3,0,0)

    int large_x, large_y;
    int offset_x, offset_y;
    int area_x, area_y;
    GtkRequisition inner, outer;

    /*
     * This is a heinous hack dreamed up by the gnome-terminal
     * people to get around a limitation in gtk. The problem is
     * that in order to set the size correctly we really need to be
     * calling gtk_window_resize - but that needs to know the size
     * of the _whole window_, not the drawing area. So what we do
     * is to set an artificially huge size request on the drawing
     * area, recompute the resulting size request on the window,
     * and look at the difference between the two. That gives us
     * the x and y offsets we need to translate drawing area size
     * into window size for real, and then we call
     * gtk_window_resize.
     */

    /*
     * We start by retrieving the current size of the whole window.
     * Adding a bit to _that_ will give us a value we can use as a
     * bogus size request which guarantees to be bigger than the
     * current size of the drawing area.
     */
    get_window_pixel_size(inst, &large_x, &large_y);
    large_x += 32;
    large_y += 32;

    gtk_widget_set_size_request(inst->area, large_x, large_y);
    gtk_widget_size_request(inst->area, &inner);
    gtk_widget_size_request(inst->window, &outer);

    offset_x = outer.width - inner.width;
    offset_y = outer.height - inner.height;

    area_x = inst->font_width * w + 2*inst->window_border;
    area_y = inst->font_height * h + 2*inst->window_border;

    /*
     * Now we must set the size request on the drawing area back to
     * something sensible before we commit the real resize. Best
     * way to do this, I think, is to set it to what the size is
     * really going to end up being.
     */
    gtk_widget_set_size_request(inst->area, area_x, area_y);
#if GTK_CHECK_VERSION(2,0,0)
    gtk_window_resize(GTK_WINDOW(inst->window),
                      area_x + offset_x, area_y + offset_y);
#else
    gtk_drawing_area_size(GTK_DRAWING_AREA(inst->area), area_x, area_y);
    /*
     * I can no longer remember what this call to
     * gtk_container_dequeue_resize_handler is for. It was
     * introduced in r3092 with no comment, and the commit log
     * message was uninformative. I'm _guessing_ its purpose is to
     * prevent gratuitous resize processing on the window given
     * that we're about to resize it anyway, but I have no idea
     * why that's so incredibly vital.
     *
     * I've tried removing the call, and nothing seems to go
     * wrong. I've backtracked to r3092 and tried removing the
     * call there, and still nothing goes wrong. So I'm going to
     * adopt the working hypothesis that it's superfluous; I won't
     * actually remove it from the GTK 1.2 code, but I won't
     * attempt to replicate its functionality in the GTK 2 code
     * above.
     */
    gtk_container_dequeue_resize_handler(GTK_CONTAINER(inst->window));
    gdk_window_resize(gtk_widget_get_window(inst->window),
                      area_x + offset_x, area_y + offset_y);
#endif

#else /* GTK_CHECK_VERSION(3,0,0) */

    int wp, hp;
    compute_whole_window_size(inst, w, h, &wp, &hp);
    gtk_window_resize(GTK_WINDOW(inst->window), wp, hp);

#endif

}

#if GTK_CHECK_VERSION(3,0,0)
char *colour_to_css(const GdkColor *col)
{
    GdkRGBA rgba;
    rgba.red = col->red / 65535.0;
    rgba.green = col->green / 65535.0;
    rgba.blue = col->blue / 65535.0;
    rgba.alpha = 1.0;
    return gdk_rgba_to_string(&rgba);
}
#endif

void set_gtk_widget_background(GtkWidget *widget, const GdkColor *col)
{
#if GTK_CHECK_VERSION(3,0,0)
    GtkCssProvider *provider = gtk_css_provider_new();
    char *col_css = colour_to_css(col);
    char *data = dupprintf(
        "#drawing-area, #top-level { background-color: %s; }\n", col_css);
    gtk_css_provider_load_from_data(provider, data, -1, NULL);
    GtkStyleContext *context = gtk_widget_get_style_context(widget);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    free(data);
    free(col_css);
#else
    if (gtk_widget_get_window(widget)) {
        /* For GTK1, which doesn't have a 'const' on
         * gdk_window_set_background's second parameter type. */
        GdkColor col_mutable = *col;
        gdk_window_set_background(gtk_widget_get_window(widget), &col_mutable);
    }
#endif
}

void set_window_background(GtkFrontend *inst)
{
    if (inst->area)
        set_gtk_widget_background(GTK_WIDGET(inst->area), &inst->cols[258]);
    if (inst->window)
        set_gtk_widget_background(GTK_WIDGET(inst->window), &inst->cols[258]);
}

static void gtkwin_palette_set(TermWin *tw, unsigned start, unsigned ncolours,
                               const rgb *colours)
{
    GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);

    assert(start <= OSC4_NCOLOURS);
    assert(ncolours <= OSC4_NCOLOURS - start);

#if !GTK_CHECK_VERSION(3,0,0)
    if (!inst->colmap) {
        inst->colmap = gdk_colormap_get_system();
    } else {
        gdk_colormap_free_colors(inst->colmap, inst->cols, OSC4_NCOLOURS);
    }
#endif

    for (unsigned i = 0; i < ncolours; i++) {
        const rgb *in = &colours[i];
        GdkColor *out = &inst->cols[start + i];

        out->red = in->r * 0x0101;
        out->green = in->g * 0x0101;
        out->blue = in->b * 0x0101;
    }

#if !GTK_CHECK_VERSION(3,0,0)
    {
        gboolean success[OSC4_NCOLOURS];
        gdk_colormap_alloc_colors(inst->colmap, inst->cols + start,
                                  ncolours, false, true, success);
        for (unsigned i = 0; i < ncolours; i++) {
            if (!success[i])
                g_error("%s: couldn't allocate colour %d (#%02x%02x%02x)\n",
                        appname, start + i,
                        conf_get_int_int(inst->conf, CONF_colours, i*3+0),
                        conf_get_int_int(inst->conf, CONF_colours, i*3+1),
                        conf_get_int_int(inst->conf, CONF_colours, i*3+2));
        }
    }
#endif

    if (start <= OSC4_COLOUR_bg && OSC4_COLOUR_bg < start + ncolours) {
        /* Default Background has changed, so ensure that space between text
         * area and window border is refreshed. */
        set_window_background(inst);
        if (inst->area && gtk_widget_get_window(inst->area)) {
            draw_backing_rect(inst);
            gtk_widget_queue_draw(inst->area);
        }
    }
}

static void gtkwin_palette_get_overrides(TermWin *tw, Terminal *term)
{
    /* GTK has no analogue of Windows's 'standard system colours', so GTK PuTTY
     * has no config option to override the normally configured colours from
     * it */
}

static struct clipboard_state *clipboard_from_atom(
    GtkFrontend *inst, GdkAtom atom)
{
    int i;

    for (i = 0; i < N_CLIPBOARDS; i++) {
        struct clipboard_state *state = &inst->clipstates[i];
        if (state->inst == inst && state->atom == atom)
            return state;
    }

    return NULL;
}

#ifdef JUST_USE_GTK_CLIPBOARD_UTF8

/* ----------------------------------------------------------------------
 * Clipboard handling, using the high-level GtkClipboard interface in
 * as hands-off a way as possible. We write and read the clipboard as
 * UTF-8 text, and let GTK deal with converting to any other text
 * formats it feels like.
 */

void set_clipboard_atom(GtkFrontend *inst, int clipboard, GdkAtom atom)
{
    struct clipboard_state *state = &inst->clipstates[clipboard];

    state->inst = inst;
    state->clipboard = clipboard;
    state->atom = atom;

    if (state->atom != GDK_NONE) {
        state->gtkclipboard = gtk_clipboard_get_for_display(
            gdk_display_get_default(), state->atom);
        g_object_set_data(G_OBJECT(state->gtkclipboard), "user-data", state);
    } else {
        state->gtkclipboard = NULL;
    }
}

int init_clipboard(GtkFrontend *inst)
{
    set_clipboard_atom(inst, CLIP_PRIMARY, GDK_SELECTION_PRIMARY);
    set_clipboard_atom(inst, CLIP_CLIPBOARD, clipboard_atom);
    return true;
}

static void clipboard_provide_data(GtkClipboard *clipboard,
                                   GtkSelectionData *selection_data,
                                   guint info, gpointer data)
{
    struct clipboard_data_instance *cdi =
        (struct clipboard_data_instance *)data;

    if (cdi->state && cdi->state->current_cdi == cdi) {
        gtk_selection_data_set_text(selection_data, cdi->pasteout_data_utf8,
                                    cdi->pasteout_data_utf8_len);
    }
}

static void clipboard_clear(GtkClipboard *clipboard, gpointer data)
{
    struct clipboard_data_instance *cdi =
        (struct clipboard_data_instance *)data;

    if (cdi->state && cdi->state->current_cdi == cdi) {
        if (cdi->state->inst && cdi->state->inst->term) {
            term_lost_clipboard_ownership(cdi->state->inst->term,
                                          cdi->state->clipboard);
        }
        cdi->state->current_cdi = NULL;
    }
    sfree(cdi->pasteout_data_utf8);
    cdi->next->prev = cdi->prev;
    cdi->prev->next = cdi->next;
    sfree(cdi);
}

static void gtkwin_clip_write(
    TermWin *tw, int clipboard, wchar_t *data, int *attr,
    truecolour *truecolour, int len, bool must_deselect)
{
    GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);
    struct clipboard_state *state = &inst->clipstates[clipboard];
    struct clipboard_data_instance *cdi;

    if (inst->direct_to_font) {
        /* In this clipboard mode, we just can't paste if we're in
         * direct-to-font mode. Fortunately, that shouldn't be
         * important, because we'll only use this clipboard handling
         * code on systems where that kind of font doesn't exist
         * anyway. */
        return;
    }

    if (!state->gtkclipboard)
        return;

    cdi = snew(struct clipboard_data_instance);
    cdi->state = state;
    state->current_cdi = cdi;
    cdi->pasteout_data_utf8 = snewn(len*6, char);
    cdi->prev = inst->cdi_headtail.prev;
    cdi->next = &inst->cdi_headtail;
    cdi->next->prev = cdi;
    cdi->prev->next = cdi;
    {
        const wchar_t *tmp = data;
        int tmplen = len;
        cdi->pasteout_data_utf8_len =
            charset_from_unicode(&tmp, &tmplen, cdi->pasteout_data_utf8,
                                 len*6, CS_UTF8, NULL, NULL, 0);
    }

    /*
     * It would be nice to just call gtk_clipboard_set_text() in place
     * of all of the faffing below. Unfortunately, that won't give me
     * access to the clipboard-clear event, which we use to visually
     * deselect text in the terminal.
     */
    {
        GtkTargetList *targetlist;
        GtkTargetEntry *targettable;
        gint n_targets;

        targetlist = gtk_target_list_new(NULL, 0);
        gtk_target_list_add_text_targets(targetlist, 0);
        targettable = gtk_target_table_new_from_list(targetlist, &n_targets);
        gtk_clipboard_set_with_data(state->gtkclipboard, targettable,
                                    n_targets, clipboard_provide_data,
                                    clipboard_clear, cdi);
        gtk_target_table_free(targettable, n_targets);
        gtk_target_list_unref(targetlist);
    }
}

static void clipboard_text_received(GtkClipboard *clipboard,
                                    const gchar *text, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    wchar_t *paste;
    int paste_len;
    int length;

    if (!text)
        return;

    length = strlen(text);

    paste = snewn(length, wchar_t);
    paste_len = mb_to_wc(CS_UTF8, 0, text, length, paste, length);

    term_do_paste(inst->term, paste, paste_len);

    sfree(paste);
}

static void gtkwin_clip_request_paste(TermWin *tw, int clipboard)
{
    GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);
    struct clipboard_state *state = &inst->clipstates[clipboard];

    if (!state->gtkclipboard)
        return;

    gtk_clipboard_request_text(state->gtkclipboard,
                               clipboard_text_received, inst);
}

#else /* JUST_USE_GTK_CLIPBOARD_UTF8 */

/* ----------------------------------------------------------------------
 * Clipboard handling for X, using the low-level gtk_selection_*
 * interface, handling conversions to fiddly things like compound text
 * ourselves, and storing in X cut buffers too.
 *
 * This version of the clipboard code has to be kept around for GTK1,
 * which doesn't have the higher-level GtkClipboard interface at all.
 * And since it works on GTK2 and GTK3 too and has had a good few
 * years of shakedown and bug fixing, we might as well keep using it
 * where it's applicable.
 *
 * It's _possible_ that we might be able to replicate all the
 * important wrinkles of this code in GtkClipboard. (In particular,
 * cut buffers or local analogue look as if they might be accessible
 * via gtk_clipboard_set_can_store(), and delivering text in
 * non-Unicode formats only in the direct-to-font case ought to be
 * possible if we can figure out the right set of things to put in the
 * GtkTargetList.) But that work can wait until there's a need for it!
 */

#ifndef NOT_X_WINDOWS

/* Store the data in a cut-buffer. */
static void store_cutbuffer(GtkFrontend *inst, char *ptr, int len)
{
    if (inst->disp) {
        /* ICCCM says we must rotate the buffers before storing to buffer 0. */
        XRotateBuffers(inst->disp, 1);
        XStoreBytes(inst->disp, ptr, len);
    }
}

/* Retrieve data from a cut-buffer.
 * Returned data needs to be freed with XFree().
 */
static char *retrieve_cutbuffer(GtkFrontend *inst, int *nbytes)
{
    char *ptr;
    if (!inst->disp) {
        *nbytes = 0;
        return NULL;
    }
    ptr = XFetchBytes(inst->disp, nbytes);
    if (*nbytes <= 0 && ptr != 0) {
        XFree(ptr);
        ptr = 0;
    }
    return ptr;
}

#endif /* NOT_X_WINDOWS */

static void gtkwin_clip_write(
    TermWin *tw, int clipboard, wchar_t *data, int *attr,
    truecolour *truecolour, int len, bool must_deselect)
{
    GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);
    struct clipboard_state *state = &inst->clipstates[clipboard];

    if (state->pasteout_data)
        sfree(state->pasteout_data);
    if (state->pasteout_data_ctext)
        sfree(state->pasteout_data_ctext);
    if (state->pasteout_data_utf8)
        sfree(state->pasteout_data_utf8);

    /*
     * Set up UTF-8 and compound text paste data. This only happens
     * if we aren't in direct-to-font mode using the D800 hack.
     */
    if (!inst->direct_to_font) {
        const wchar_t *tmp = data;
        int tmplen = len;
#ifndef NOT_X_WINDOWS
        XTextProperty tp;
        char *list[1];
#endif

        state->pasteout_data_utf8 = snewn(len*6, char);
        state->pasteout_data_utf8_len = len*6;
        state->pasteout_data_utf8_len =
            charset_from_unicode(&tmp, &tmplen, state->pasteout_data_utf8,
                                 state->pasteout_data_utf8_len,
                                 CS_UTF8, NULL, NULL, 0);
        if (state->pasteout_data_utf8_len == 0) {
            sfree(state->pasteout_data_utf8);
            state->pasteout_data_utf8 = NULL;
        } else {
            state->pasteout_data_utf8 =
                sresize(state->pasteout_data_utf8,
                        state->pasteout_data_utf8_len + 1, char);
            state->pasteout_data_utf8[state->pasteout_data_utf8_len] = '\0';
        }

        /*
         * Now let Xlib convert our UTF-8 data into compound text.
         */
#ifndef NOT_X_WINDOWS
        list[0] = state->pasteout_data_utf8;
        if (inst->disp && Xutf8TextListToTextProperty(
                inst->disp, list, 1, XCompoundTextStyle, &tp) == 0) {
            state->pasteout_data_ctext = snewn(tp.nitems+1, char);
            memcpy(state->pasteout_data_ctext, tp.value, tp.nitems);
            state->pasteout_data_ctext_len = tp.nitems;
            XFree(tp.value);
        } else
#endif
        {
            state->pasteout_data_ctext = NULL;
            state->pasteout_data_ctext_len = 0;
        }
    } else {
        state->pasteout_data_utf8 = NULL;
        state->pasteout_data_utf8_len = 0;
        state->pasteout_data_ctext = NULL;
        state->pasteout_data_ctext_len = 0;
    }

    state->pasteout_data = snewn(len*6, char);
    state->pasteout_data_len = len*6;
    state->pasteout_data_len = wc_to_mb(inst->ucsdata.line_codepage, 0,
                                       data, len, state->pasteout_data,
                                       state->pasteout_data_len,
                                       NULL, NULL);
    if (state->pasteout_data_len == 0) {
        sfree(state->pasteout_data);
        state->pasteout_data = NULL;
    } else {
        state->pasteout_data =
            sresize(state->pasteout_data, state->pasteout_data_len, char);
    }

#ifndef NOT_X_WINDOWS
    /* The legacy X cut buffers go with PRIMARY, not any other clipboard */
    if (state->atom == GDK_SELECTION_PRIMARY)
        store_cutbuffer(inst, state->pasteout_data, state->pasteout_data_len);
#endif

    if (gtk_selection_owner_set(inst->area, state->atom,
                                inst->input_event_time)) {
#if GTK_CHECK_VERSION(2,0,0)
        gtk_selection_clear_targets(inst->area, state->atom);
#endif
        gtk_selection_add_target(inst->area, state->atom,
                                 GDK_SELECTION_TYPE_STRING, 1);
        if (state->pasteout_data_ctext)
            gtk_selection_add_target(inst->area, state->atom,
                                     compound_text_atom, 1);
        if (state->pasteout_data_utf8)
            gtk_selection_add_target(inst->area, state->atom,
                                     utf8_string_atom, 1);
    }

    if (must_deselect)
        term_lost_clipboard_ownership(inst->term, clipboard);
}

static void selection_get(GtkWidget *widget, GtkSelectionData *seldata,
                          guint info, guint time_stamp, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    GdkAtom target = gtk_selection_data_get_target(seldata);
    struct clipboard_state *state = clipboard_from_atom(
        inst, gtk_selection_data_get_selection(seldata));

    if (!state)
        return;

    if (target == utf8_string_atom)
        gtk_selection_data_set(seldata, target, 8,
                               (unsigned char *)state->pasteout_data_utf8,
                               state->pasteout_data_utf8_len);
    else if (target == compound_text_atom)
        gtk_selection_data_set(seldata, target, 8,
                               (unsigned char *)state->pasteout_data_ctext,
                               state->pasteout_data_ctext_len);
    else
        gtk_selection_data_set(seldata, target, 8,
                               (unsigned char *)state->pasteout_data,
                               state->pasteout_data_len);
}

static gint selection_clear(GtkWidget *widget, GdkEventSelection *seldata,
                            gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    struct clipboard_state *state = clipboard_from_atom(
        inst, seldata->selection);

    if (!state)
        return true;

    term_lost_clipboard_ownership(inst->term, state->clipboard);
    if (state->pasteout_data)
        sfree(state->pasteout_data);
    if (state->pasteout_data_ctext)
        sfree(state->pasteout_data_ctext);
    if (state->pasteout_data_utf8)
        sfree(state->pasteout_data_utf8);
    state->pasteout_data = NULL;
    state->pasteout_data_len = 0;
    state->pasteout_data_ctext = NULL;
    state->pasteout_data_ctext_len = 0;
    state->pasteout_data_utf8 = NULL;
    state->pasteout_data_utf8_len = 0;
    return true;
}

static void gtkwin_clip_request_paste(TermWin *tw, int clipboard)
{
    GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);
    struct clipboard_state *state = &inst->clipstates[clipboard];

    /*
     * In Unix, pasting is asynchronous: all we can do at the
     * moment is to call gtk_selection_convert(), and when the data
     * comes back _then_ we can call term_do_paste().
     */

    if (!inst->direct_to_font) {
        /*
         * First we attempt to retrieve the selection as a UTF-8
         * string (which we will convert to the correct code page
         * before sending to the session, of course). If that
         * fails, selection_received() will be informed and will
         * fall back to an ordinary string.
         */
        gtk_selection_convert(inst->area, state->atom, utf8_string_atom,
                              inst->input_event_time);
    } else {
        /*
         * If we're in direct-to-font mode, we disable UTF-8
         * pasting, and go straight to ordinary string data.
         */
        gtk_selection_convert(inst->area, state->atom,
                              GDK_SELECTION_TYPE_STRING,
                              inst->input_event_time);
    }
}

static void selection_received(GtkWidget *widget, GtkSelectionData *seldata,
                               guint time, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    char *text;
    int length;
#ifndef NOT_X_WINDOWS
    char **list;
    bool free_list_required = false;
    bool free_required = false;
#endif
    int charset;
    GdkAtom seldata_target = gtk_selection_data_get_target(seldata);
    GdkAtom seldata_type = gtk_selection_data_get_data_type(seldata);
    const guchar *seldata_data = gtk_selection_data_get_data(seldata);
    gint seldata_length = gtk_selection_data_get_length(seldata);
    wchar_t *paste;
    int paste_len;
    struct clipboard_state *state = clipboard_from_atom(
        inst, gtk_selection_data_get_selection(seldata));

    if (!state)
        return;

    if (seldata_target == utf8_string_atom && seldata_length <= 0) {
        /*
         * Failed to get a UTF-8 selection string. Try compound
         * text next.
         */
        gtk_selection_convert(inst->area, state->atom,
                              compound_text_atom,
                              inst->input_event_time);
        return;
    }

    if (seldata_target == compound_text_atom && seldata_length <= 0) {
        /*
         * Failed to get UTF-8 or compound text. Try an ordinary
         * string.
         */
        gtk_selection_convert(inst->area, state->atom,
                              GDK_SELECTION_TYPE_STRING,
                              inst->input_event_time);
        return;
    }

    /*
     * If we have data, but it's not of a type we can deal with,
     * we have to ignore the data.
     */
    if (seldata_length > 0 &&
        seldata_type != GDK_SELECTION_TYPE_STRING &&
        seldata_type != compound_text_atom &&
        seldata_type != utf8_string_atom)
        return;

    /*
     * If we have no data, try looking in a cut buffer.
     */
    if (seldata_length <= 0) {
#ifndef NOT_X_WINDOWS
        text = retrieve_cutbuffer(inst, &length);
        if (length == 0)
            return;
        /* Xterm is rumoured to expect Latin-1, though I haven't checked the
         * source, so use that as a de-facto standard. */
        charset = CS_ISO8859_1;
        free_required = true;
#else
        return;
#endif
    } else {
        /*
         * Convert COMPOUND_TEXT into UTF-8.
         */
        if (seldata_type == compound_text_atom) {
#ifndef NOT_X_WINDOWS
            XTextProperty tp;
            int ret, count;

            tp.value = (unsigned char *)seldata_data;
            tp.encoding = (Atom) seldata_type;
            tp.format = gtk_selection_data_get_format(seldata);
            tp.nitems = seldata_length;
            ret = inst->disp == NULL ? -1 :
                Xutf8TextPropertyToTextList(inst->disp, &tp, &list, &count);
            if (ret == 0 && count == 1) {
                text = list[0];
                length = strlen(list[0]);
                charset = CS_UTF8;
                free_list_required = true;
            } else
#endif
            {
                /*
                 * Compound text failed; fall back to STRING.
                 */
                gtk_selection_convert(inst->area, state->atom,
                                      GDK_SELECTION_TYPE_STRING,
                                      inst->input_event_time);
                return;
            }
        } else {
            text = (char *)seldata_data;
            length = seldata_length;
            charset = (seldata_type == utf8_string_atom ?
                       CS_UTF8 : inst->ucsdata.line_codepage);
        }
    }

    paste = snewn(length, wchar_t);
    paste_len = mb_to_wc(charset, 0, text, length, paste, length);

    term_do_paste(inst->term, paste, paste_len);

    sfree(paste);

#ifndef NOT_X_WINDOWS
    if (free_list_required)
        XFreeStringList(list);
    if (free_required)
        XFree(text);
#endif
}

static void init_one_clipboard(GtkFrontend *inst, int clipboard)
{
    struct clipboard_state *state = &inst->clipstates[clipboard];

    state->inst = inst;
    state->clipboard = clipboard;
}

void set_clipboard_atom(GtkFrontend *inst, int clipboard, GdkAtom atom)
{
    struct clipboard_state *state = &inst->clipstates[clipboard];

    state->inst = inst;
    state->clipboard = clipboard;

    state->atom = atom;
}

void init_clipboard(GtkFrontend *inst)
{
#ifndef NOT_X_WINDOWS
    /*
     * Ensure that all the cut buffers exist - according to the ICCCM,
     * we must do this before we start using cut buffers.
     */
    if (inst->disp) {
        unsigned char empty[] = "";
        x11_ignore_error(inst->disp, BadMatch);
        XChangeProperty(inst->disp, GDK_ROOT_WINDOW(), XA_CUT_BUFFER0,
                        XA_STRING, 8, PropModeAppend, empty, 0);
        x11_ignore_error(inst->disp, BadMatch);
        XChangeProperty(inst->disp, GDK_ROOT_WINDOW(), XA_CUT_BUFFER1,
                        XA_STRING, 8, PropModeAppend, empty, 0);
        x11_ignore_error(inst->disp, BadMatch);
        XChangeProperty(inst->disp, GDK_ROOT_WINDOW(), XA_CUT_BUFFER2,
                        XA_STRING, 8, PropModeAppend, empty, 0);
        x11_ignore_error(inst->disp, BadMatch);
        XChangeProperty(inst->disp, GDK_ROOT_WINDOW(), XA_CUT_BUFFER3,
                        XA_STRING, 8, PropModeAppend, empty, 0);
        x11_ignore_error(inst->disp, BadMatch);
        XChangeProperty(inst->disp, GDK_ROOT_WINDOW(), XA_CUT_BUFFER4,
                        XA_STRING, 8, PropModeAppend, empty, 0);
        x11_ignore_error(inst->disp, BadMatch);
        XChangeProperty(inst->disp, GDK_ROOT_WINDOW(), XA_CUT_BUFFER5,
                        XA_STRING, 8, PropModeAppend, empty, 0);
        x11_ignore_error(inst->disp, BadMatch);
        XChangeProperty(inst->disp, GDK_ROOT_WINDOW(), XA_CUT_BUFFER6,
                        XA_STRING, 8, PropModeAppend, empty, 0);
        x11_ignore_error(inst->disp, BadMatch);
        XChangeProperty(inst->disp, GDK_ROOT_WINDOW(), XA_CUT_BUFFER7,
                        XA_STRING, 8, PropModeAppend, empty, 0);
    }
#endif

    inst->clipstates[CLIP_PRIMARY].atom = GDK_SELECTION_PRIMARY;
    inst->clipstates[CLIP_CLIPBOARD].atom = clipboard_atom;
    init_one_clipboard(inst, CLIP_PRIMARY);
    init_one_clipboard(inst, CLIP_CLIPBOARD);

    g_signal_connect(G_OBJECT(inst->area), "selection_received",
                     G_CALLBACK(selection_received), inst);
    g_signal_connect(G_OBJECT(inst->area), "selection_get",
                     G_CALLBACK(selection_get), inst);
    g_signal_connect(G_OBJECT(inst->area), "selection_clear_event",
                     G_CALLBACK(selection_clear), inst);
}

/*
 * End of selection/clipboard handling.
 * ----------------------------------------------------------------------
 */

#endif /* JUST_USE_GTK_CLIPBOARD_UTF8 */

static void set_window_titles(GtkFrontend *inst)
{
    /*
     * We must always call set_icon_name after calling set_title,
     * since set_title will write both names. Irritating, but such
     * is life.
     */
    gtk_window_set_title(GTK_WINDOW(inst->window), inst->wintitle);
    if (!conf_get_bool(inst->conf, CONF_win_name_always))
        gdk_window_set_icon_name(gtk_widget_get_window(inst->window),
                                 inst->icontitle);
}

static void gtkwin_set_title(TermWin *tw, const char *title, int codepage)
{
    GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);
    sfree(inst->wintitle);
    if (codepage != CP_UTF8) {
        wchar_t *title_w = dup_mb_to_wc(codepage, 0, title);
        inst->wintitle = encode_wide_string_as_utf8(title_w);
        sfree(title_w);
    } else {
        inst->wintitle = dupstr(title);
    }
    set_window_titles(inst);
}

static void gtkwin_set_icon_title(TermWin *tw, const char *title, int codepage)
{
    GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);
    sfree(inst->icontitle);
    if (codepage != CP_UTF8) {
        wchar_t *title_w = dup_mb_to_wc(codepage, 0, title);
        inst->icontitle = encode_wide_string_as_utf8(title_w);
        sfree(title_w);
    } else {
        inst->icontitle = dupstr(title);
    }
    set_window_titles(inst);
}

static void gtkwin_set_scrollbar(TermWin *tw, int total, int start, int page)
{
    GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);
    if (!conf_get_bool(inst->conf, CONF_scrollbar))
        return;
    inst->ignore_sbar = true;
    gtk_adjustment_set_lower(inst->sbar_adjust, 0);
    gtk_adjustment_set_upper(inst->sbar_adjust, total);
    gtk_adjustment_set_value(inst->sbar_adjust, start);
    gtk_adjustment_set_page_size(inst->sbar_adjust, page);
    gtk_adjustment_set_step_increment(inst->sbar_adjust, 1);
    gtk_adjustment_set_page_increment(inst->sbar_adjust, page/2);
#if !GTK_CHECK_VERSION(3,18,0)
    gtk_adjustment_changed(inst->sbar_adjust);
#endif
    inst->ignore_sbar = false;
}

void scrollbar_moved(GtkAdjustment *adj, GtkFrontend *inst)
{
    if (!conf_get_bool(inst->conf, CONF_scrollbar))
        return;
    if (!inst->ignore_sbar)
        term_scroll(inst->term, 1, (int)gtk_adjustment_get_value(adj));
}

static void show_scrollbar(GtkFrontend *inst, gboolean visible)
{
    inst->sbar_visible = visible;
    if (visible)
        gtk_widget_show(inst->sbar);
    else
        gtk_widget_hide(inst->sbar);
}

static void gtkwin_set_cursor_pos(TermWin *tw, int x, int y)
{
    /*
     * This is meaningless under X.
     */
}

/*
 * This is still called when mode==BELL_VISUAL, even though the
 * visual bell is handled entirely within terminal.c, because we
 * may want to perform additional actions on any kind of bell (for
 * example, taskbar flashing in Windows).
 */
static void gtkwin_bell(TermWin *tw, int mode)
{
    /* GtkFrontend *inst = container_of(tw, GtkFrontend, termwin); */
    if (mode == BELL_DEFAULT)
        gdk_display_beep(gdk_display_get_default());
}

static int gtkwin_char_width(TermWin *tw, int uc)
{
    /*
     * In this front end, double-width characters are handled using a
     * separate font, so this can safely just return 1 always.
     */
    return 1;
}

static bool gtkwin_setup_draw_ctx(TermWin *tw)
{
    GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);

    if (!gtk_widget_get_window(inst->area))
        return false;

    inst->uctx.type = inst->drawtype;
#ifdef DRAW_TEXT_GDK
    if (inst->uctx.type == DRAWTYPE_GDK) {
        /* If we're doing GDK-based drawing, then we also expect
         * inst->pixmap to exist. */
        inst->uctx.u.gdk.target = inst->pixmap;
        inst->uctx.u.gdk.gc = gdk_gc_new(gtk_widget_get_window(inst->area));
    }
#endif
#ifdef DRAW_TEXT_CAIRO
    if (inst->uctx.type == DRAWTYPE_CAIRO) {
        inst->uctx.u.cairo.widget = GTK_WIDGET(inst->area);
        /* If we're doing Cairo drawing, we expect inst->surface to
         * exist, and we draw to that first, regardless of whether we
         * subsequently copy the results to inst->pixmap. */
        inst->uctx.u.cairo.cr = cairo_create(inst->surface);
        cairo_scale(inst->uctx.u.cairo.cr, inst->scale, inst->scale);
        cairo_setup_draw_ctx(inst);
    }
#endif
    return true;
}

static void gtkwin_free_draw_ctx(TermWin *tw)
{
    GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);
#ifdef DRAW_TEXT_GDK
    if (inst->uctx.type == DRAWTYPE_GDK) {
        gdk_gc_unref(inst->uctx.u.gdk.gc);
    }
#endif
#ifdef DRAW_TEXT_CAIRO
    if (inst->uctx.type == DRAWTYPE_CAIRO) {
        cairo_destroy(inst->uctx.u.cairo.cr);
    }
#endif
}


static void draw_update(GtkFrontend *inst, int x, int y, int w, int h)
{
#if defined DRAW_TEXT_CAIRO && !defined NO_BACKING_PIXMAPS
    if (inst->uctx.type == DRAWTYPE_CAIRO) {
        /*
         * If inst->surface and inst->pixmap both exist, then we've
         * just drawn new content to the former which we must copy to
         * the latter.
         */
        cairo_t *cr = gdk_cairo_create(inst->pixmap);
        cairo_set_source_surface(cr, inst->surface, 0, 0);
        cairo_rectangle(cr, x, y, w, h);
        cairo_fill(cr);
        cairo_destroy(cr);
    }
#endif

    /*
     * Now we just queue a window redraw, which will cause
     * inst->surface or inst->pixmap (whichever is appropriate for our
     * compile mode) to be copied to the real window when we receive
     * the resulting "expose" or "draw" event.
     *
     * Amazingly, this one API call is actually valid in all versions
     * of GTK :-)
     */
    gtk_widget_queue_draw_area(inst->area, x, y, w, h);
}

#ifdef DRAW_TEXT_CAIRO
static void cairo_set_source_rgb_dim(cairo_t *cr, double r, double g, double b,
                                     bool dim)
{
    if (dim)
        cairo_set_source_rgb(cr, r * 2 / 3, g * 2 / 3, b * 2 / 3);
    else
        cairo_set_source_rgb(cr, r, g, b);
}
#endif

static void draw_set_colour(GtkFrontend *inst, int col, bool dim)
{
#ifdef DRAW_TEXT_GDK
    if (inst->uctx.type == DRAWTYPE_GDK) {
        if (dim) {
#if GTK_CHECK_VERSION(2,0,0)
            GdkColor color;
            color.red =   inst->cols[col].red   * 2 / 3;
            color.green = inst->cols[col].green * 2 / 3;
            color.blue =  inst->cols[col].blue  * 2 / 3;
            gdk_gc_set_rgb_fg_color(inst->uctx.u.gdk.gc, &color);
#else
            /* Poor GTK1 fallback */
            gdk_gc_set_foreground(inst->uctx.u.gdk.gc, &inst->cols[col]);
#endif
        } else {
            gdk_gc_set_foreground(inst->uctx.u.gdk.gc, &inst->cols[col]);
        }
    }
#endif
#ifdef DRAW_TEXT_CAIRO
    if (inst->uctx.type == DRAWTYPE_CAIRO) {
        cairo_set_source_rgb_dim(inst->uctx.u.cairo.cr,
                                 inst->cols[col].red / 65535.0,
                                 inst->cols[col].green / 65535.0,
                                 inst->cols[col].blue / 65535.0, dim);
    }
#endif
}

static void draw_set_colour_rgb(GtkFrontend *inst, optionalrgb orgb, bool dim)
{
#ifdef DRAW_TEXT_GDK
    if (inst->uctx.type == DRAWTYPE_GDK) {
#if GTK_CHECK_VERSION(2,0,0)
        GdkColor color;
        color.red =   orgb.r * 256;
        color.green = orgb.g * 256;
        color.blue =  orgb.b * 256;
        if (dim) {
            color.red   = color.red   * 2 / 3;
            color.green = color.green * 2 / 3;
            color.blue  = color.blue  * 2 / 3;
        }
        gdk_gc_set_rgb_fg_color(inst->uctx.u.gdk.gc, &color);
#else
        /* Poor GTK1 fallback */
        gdk_gc_set_foreground(inst->uctx.u.gdk.gc, &inst->cols[256]);
#endif
    }
#endif
#ifdef DRAW_TEXT_CAIRO
    if (inst->uctx.type == DRAWTYPE_CAIRO) {
        cairo_set_source_rgb_dim(inst->uctx.u.cairo.cr, orgb.r / 255.0,
                                 orgb.g / 255.0, orgb.b / 255.0, dim);
    }
#endif
}

static void draw_rectangle(GtkFrontend *inst, bool filled,
                           int x, int y, int w, int h)
{
#ifdef DRAW_TEXT_GDK
    if (inst->uctx.type == DRAWTYPE_GDK) {
        gdk_draw_rectangle(inst->uctx.u.gdk.target, inst->uctx.u.gdk.gc,
                           filled, x, y, w, h);
    }
#endif
#ifdef DRAW_TEXT_CAIRO
    if (inst->uctx.type == DRAWTYPE_CAIRO) {
        cairo_new_path(inst->uctx.u.cairo.cr);
        if (filled) {
            cairo_rectangle(inst->uctx.u.cairo.cr, x, y, w, h);
            cairo_fill(inst->uctx.u.cairo.cr);
        } else {
            cairo_rectangle(inst->uctx.u.cairo.cr,
                            x + 0.5, y + 0.5, w, h);
            cairo_close_path(inst->uctx.u.cairo.cr);
            cairo_stroke(inst->uctx.u.cairo.cr);
        }
    }
#endif
}

static void draw_clip(GtkFrontend *inst, int x, int y, int w, int h)
{
#ifdef DRAW_TEXT_GDK
    if (inst->uctx.type == DRAWTYPE_GDK) {
        GdkRectangle r;

        r.x = x;
        r.y = y;
        r.width = w;
        r.height = h;

        gdk_gc_set_clip_rectangle(inst->uctx.u.gdk.gc, &r);
    }
#endif
#ifdef DRAW_TEXT_CAIRO
    if (inst->uctx.type == DRAWTYPE_CAIRO) {
        cairo_reset_clip(inst->uctx.u.cairo.cr);
        cairo_new_path(inst->uctx.u.cairo.cr);
        cairo_rectangle(inst->uctx.u.cairo.cr, x, y, w, h);
        cairo_clip(inst->uctx.u.cairo.cr);
    }
#endif
}

static void draw_point(GtkFrontend *inst, int x, int y)
{
#ifdef DRAW_TEXT_GDK
    if (inst->uctx.type == DRAWTYPE_GDK) {
        gdk_draw_point(inst->uctx.u.gdk.target, inst->uctx.u.gdk.gc, x, y);
    }
#endif
#ifdef DRAW_TEXT_CAIRO
    if (inst->uctx.type == DRAWTYPE_CAIRO) {
        cairo_new_path(inst->uctx.u.cairo.cr);
        cairo_rectangle(inst->uctx.u.cairo.cr, x, y, 1, 1);
        cairo_fill(inst->uctx.u.cairo.cr);
    }
#endif
}

static void draw_line(GtkFrontend *inst, int x0, int y0, int x1, int y1)
{
#ifdef DRAW_TEXT_GDK
    if (inst->uctx.type == DRAWTYPE_GDK) {
        gdk_draw_line(inst->uctx.u.gdk.target, inst->uctx.u.gdk.gc,
                      x0, y0, x1, y1);
    }
#endif
#ifdef DRAW_TEXT_CAIRO
    if (inst->uctx.type == DRAWTYPE_CAIRO) {
        cairo_new_path(inst->uctx.u.cairo.cr);
        cairo_move_to(inst->uctx.u.cairo.cr, x0 + 0.5, y0 + 0.5);
        cairo_line_to(inst->uctx.u.cairo.cr, x1 + 0.5, y1 + 0.5);
        cairo_stroke(inst->uctx.u.cairo.cr);
    }
#endif
}

static void draw_stretch_before(GtkFrontend *inst, int x, int y,
                                int w, bool wdouble,
                                int h, bool hdouble, bool hbothalf)
{
#ifdef DRAW_TEXT_CAIRO
    if (inst->uctx.type == DRAWTYPE_CAIRO) {
        cairo_matrix_t matrix;

        matrix.xy = 0;
        matrix.yx = 0;

        if (wdouble) {
            matrix.xx = 2;
            matrix.x0 = -x;
        } else {
            matrix.xx = 1;
            matrix.x0 = 0;
        }

        if (hdouble) {
            matrix.yy = 2;
            if (hbothalf) {
                matrix.y0 = -(y+h);
            } else {
                matrix.y0 = -y;
            }
        } else {
            matrix.yy = 1;
            matrix.y0 = 0;
        }
        cairo_transform(inst->uctx.u.cairo.cr, &matrix);
    }
#endif
}

static void draw_stretch_after(GtkFrontend *inst, int x, int y,
                               int w, bool wdouble,
                               int h, bool hdouble, bool hbothalf)
{
#ifdef DRAW_TEXT_GDK
#ifndef NO_BACKING_PIXMAPS
    if (inst->uctx.type == DRAWTYPE_GDK) {
        /*
         * I can't find any plausible StretchBlt equivalent in the X
         * server, so I'm going to do this the slow and painful way.
         * This will involve repeated calls to gdk_draw_pixmap() to
         * stretch the text horizontally. It's O(N^2) in time and O(N)
         * in network bandwidth, but you try thinking of a better way.
         * :-(
         */
        int i;
        if (wdouble) {
            for (i = 0; i < w; i++) {
                gdk_draw_pixmap(inst->uctx.u.gdk.target,
                                inst->uctx.u.gdk.gc,
                                inst->uctx.u.gdk.target,
                                x + 2*i, y,
                                x + 2*i+1, y,
                                w - i, h);
            }
            w *= 2;
        }

        if (hdouble) {
            int dt, db;
            /* Now stretch vertically, in the same way. */
            if (hbothalf)
                dt = 0, db = 1;
            else
                dt = 1, db = 0;
            for (i = 0; i < h; i += 2) {
                gdk_draw_pixmap(inst->uctx.u.gdk.target,
                                inst->uctx.u.gdk.gc,
                                inst->uctx.u.gdk.target,
                                x, y + dt*i + db,
                                x, y + dt*(i+1),
                                w, h-i-1);
            }
        }
    }
#else
#error No way to implement stretching in GDK without a reliable backing pixmap
#endif
#endif /* DRAW_TEXT_GDK */
#ifdef DRAW_TEXT_CAIRO
    if (inst->uctx.type == DRAWTYPE_CAIRO) {
        cairo_set_matrix(inst->uctx.u.cairo.cr,
                         &inst->uctx.u.cairo.origmatrix);
    }
#endif
}

static void draw_backing_rect(GtkFrontend *inst)
{
    int w, h;

    if (!win_setup_draw_ctx(&inst->termwin))
        return;

    w = inst->width * inst->font_width + 2*inst->window_border;
    h = inst->height * inst->font_height + 2*inst->window_border;
    draw_set_colour(inst, 258, false);
    draw_rectangle(inst, true, 0, 0, w, h);
    draw_update(inst, 0, 0, w, h);
    win_free_draw_ctx(&inst->termwin);
}

/*
 * Draw a line of text in the window, at given character
 * coordinates, in given attributes.
 *
 * We are allowed to fiddle with the contents of `text'.
 */
static void do_text_internal(
    GtkFrontend *inst, int x, int y, wchar_t *text, int len,
    unsigned long attr, int lattr, truecolour truecolour)
{
    int ncombining;
    int nfg, nbg, t, fontid, rlen, widefactor;
    bool bold;
    bool monochrome =
        gdk_visual_get_depth(gtk_widget_get_visual(inst->area)) == 1;

    if (attr & TATTR_COMBINING) {
        ncombining = len;
        len = 1;
    } else
        ncombining = 1;

    if (monochrome)
        truecolour.fg = truecolour.bg = optionalrgb_none;

    nfg = ((monochrome ? ATTR_DEFFG : (attr & ATTR_FGMASK)) >> ATTR_FGSHIFT);
    nbg = ((monochrome ? ATTR_DEFBG : (attr & ATTR_BGMASK)) >> ATTR_BGSHIFT);
    if (!!(attr & ATTR_REVERSE) ^ (monochrome && (attr & TATTR_ACTCURS))) {
        struct optionalrgb trgb;

        t = nfg;
        nfg = nbg;
        nbg = t;

        trgb = truecolour.fg;
        truecolour.fg = truecolour.bg;
        truecolour.bg = trgb;
    }
    if ((inst->bold_style & 2) && (attr & ATTR_BOLD)) {
        if (nfg < 16) nfg |= 8;
        else if (nfg >= 256) nfg |= 1;
    }
    if ((inst->bold_style & 2) && (attr & ATTR_BLINK)) {
        if (nbg < 16) nbg |= 8;
        else if (nbg >= 256) nbg |= 1;
    }
    if ((attr & TATTR_ACTCURS) && !monochrome) {
        truecolour.fg = truecolour.bg = optionalrgb_none;
        nfg = 260;
        nbg = 261;
        attr &= ~ATTR_DIM;             /* don't dim the cursor */
    }

    fontid = 0;

    if (attr & ATTR_WIDE) {
        widefactor = 2;
        fontid |= 2;
    } else {
        widefactor = 1;
    }

    if ((attr & ATTR_BOLD) && (inst->bold_style & 1)) {
        bold = true;
        fontid |= 1;
    } else {
        bold = false;
    }

    if (!inst->fonts[fontid]) {
        int i;
        /*
         * Fall back through font ids with subsets of this one's
         * set bits, in order.
         */
        for (i = fontid; i-- > 0 ;) {
            if (i & ~fontid)
                continue;              /* some other bit is set */
            if (inst->fonts[i]) {
                fontid = i;
                break;
            }
        }
        assert(inst->fonts[fontid]);   /* we should at least have hit zero */
    }

    if ((lattr & LATTR_MODE) != LATTR_NORM) {
        x *= 2;
        if (x >= inst->term->cols)
            return;
        if (x + len*2*widefactor > inst->term->cols) {
            len = (inst->term->cols-x)/2/widefactor;/* trim to LH half */
            if (len == 0)
                return; /* rounded down half a double-width char to zero */
        }
        rlen = len * 2;
    } else
        rlen = len;

    draw_clip(inst,
              x*inst->font_width+inst->window_border,
              y*inst->font_height+inst->window_border,
              rlen*widefactor*inst->font_width,
              inst->font_height);

    if ((lattr & LATTR_MODE) != LATTR_NORM) {
        draw_stretch_before(inst,
                            x*inst->font_width+inst->window_border,
                            y*inst->font_height+inst->window_border,
                            rlen*widefactor*inst->font_width, true,
                            inst->font_height,
                            ((lattr & LATTR_MODE) != LATTR_WIDE),
                            ((lattr & LATTR_MODE) == LATTR_BOT));
    }

    if (truecolour.bg.enabled)
        draw_set_colour_rgb(inst, truecolour.bg, attr & ATTR_DIM);
    else
        draw_set_colour(inst, nbg, attr & ATTR_DIM);
    draw_rectangle(inst, true,
                   x*inst->font_width+inst->window_border,
                   y*inst->font_height+inst->window_border,
                   rlen*widefactor*inst->font_width, inst->font_height);

    if (truecolour.fg.enabled)
        draw_set_colour_rgb(inst, truecolour.fg, attr & ATTR_DIM);
    else
        draw_set_colour(inst, nfg, attr & ATTR_DIM);
    if (ncombining > 1) {
        assert(len == 1);
        unifont_draw_combining(&inst->uctx, inst->fonts[fontid],
                               x*inst->font_width+inst->window_border,
                               (y*inst->font_height+inst->window_border+
                                inst->fonts[0]->ascent),
                               text, ncombining, widefactor > 1,
                               bold, inst->font_width);
    } else {
        unifont_draw_text(&inst->uctx, inst->fonts[fontid],
                          x*inst->font_width+inst->window_border,
                          (y*inst->font_height+inst->window_border+
                           inst->fonts[0]->ascent),
                          text, len, widefactor > 1,
                          bold, inst->font_width);
    }

    if (attr & ATTR_UNDER) {
        int uheight = inst->fonts[0]->ascent + 1;
        if (uheight >= inst->font_height)
            uheight = inst->font_height - 1;
        draw_line(inst, x*inst->font_width+inst->window_border,
                  y*inst->font_height + uheight + inst->window_border,
                  (x+len)*widefactor*inst->font_width-1+inst->window_border,
                  y*inst->font_height + uheight + inst->window_border);
    }

    if (attr & ATTR_STRIKE) {
        int sheight = inst->fonts[fontid]->strikethrough_y;
        draw_line(inst, x*inst->font_width+inst->window_border,
                  y*inst->font_height + sheight + inst->window_border,
                  (x+len)*widefactor*inst->font_width-1+inst->window_border,
                  y*inst->font_height + sheight + inst->window_border);
    }

    if ((lattr & LATTR_MODE) != LATTR_NORM) {
        draw_stretch_after(inst,
                           x*inst->font_width+inst->window_border,
                           y*inst->font_height+inst->window_border,
                           rlen*widefactor*inst->font_width, true,
                           inst->font_height,
                           ((lattr & LATTR_MODE) != LATTR_WIDE),
                           ((lattr & LATTR_MODE) == LATTR_BOT));
    }
}

static void gtkwin_draw_text(
    TermWin *tw, int x, int y, wchar_t *text, int len,
    unsigned long attr, int lattr, truecolour truecolour)
{
    GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);
    int widefactor;

    do_text_internal(inst, x, y, text, len, attr, lattr, truecolour);

    if (attr & ATTR_WIDE) {
        widefactor = 2;
    } else {
        widefactor = 1;
    }

    if ((lattr & LATTR_MODE) != LATTR_NORM) {
        x *= 2;
        if (x >= inst->term->cols)
            return;
        if (x + len*2*widefactor > inst->term->cols)
            len = (inst->term->cols-x)/2/widefactor;/* trim to LH half */
        len *= 2;
    }

    draw_update(inst,
                x*inst->font_width+inst->window_border,
                y*inst->font_height+inst->window_border,
                len*widefactor*inst->font_width, inst->font_height);
}

static void gtkwin_draw_cursor(
    TermWin *tw, int x, int y, wchar_t *text, int len,
    unsigned long attr, int lattr, truecolour truecolour)
{
    GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);
    bool active, passive;
    int widefactor;

    if (attr & TATTR_PASCURS) {
        attr &= ~TATTR_PASCURS;
        passive = true;
    } else
        passive = false;
    if ((attr & TATTR_ACTCURS) && inst->cursor_type != 0) {
        attr &= ~TATTR_ACTCURS;
        active = true;
    } else
        active = false;
    do_text_internal(inst, x, y, text, len, attr, lattr, truecolour);

    if (attr & TATTR_COMBINING)
        len = 1;

    if (attr & ATTR_WIDE) {
        widefactor = 2;
    } else {
        widefactor = 1;
    }

    if ((lattr & LATTR_MODE) != LATTR_NORM) {
        x *= 2;
        if (x >= inst->term->cols)
            return;
        if (x + len*2*widefactor > inst->term->cols)
            len = (inst->term->cols-x)/2/widefactor;/* trim to LH half */
        len *= 2;
    }

    if (inst->cursor_type == 0) {
        /*
         * An active block cursor will already have been done by
         * the above do_text call, so we only need to do anything
         * if it's passive.
         */
        if (passive) {
            draw_set_colour(inst, 261, false);
            draw_rectangle(inst, false,
                           x*inst->font_width+inst->window_border,
                           y*inst->font_height+inst->window_border,
                           len*widefactor*inst->font_width-1,
                           inst->font_height-1);
        }
    } else {
        int uheight;
        int startx, starty, dx, dy, length, i;

        int char_width;

        if ((attr & ATTR_WIDE) || (lattr & LATTR_MODE) != LATTR_NORM)
            char_width = 2*inst->font_width;
        else
            char_width = inst->font_width;

        if (inst->cursor_type == 1) {
            uheight = inst->fonts[0]->ascent + 1;
            if (uheight >= inst->font_height)
                uheight = inst->font_height - 1;

            startx = x * inst->font_width + inst->window_border;
            starty = y * inst->font_height + inst->window_border + uheight;
            dx = 1;
            dy = 0;
            length = len * widefactor * char_width;
        } else {
            int xadjust = 0;
            if (attr & TATTR_RIGHTCURS)
                xadjust = char_width - 1;
            startx = x * inst->font_width + inst->window_border + xadjust;
            starty = y * inst->font_height + inst->window_border;
            dx = 0;
            dy = 1;
            length = inst->font_height;
        }

        draw_set_colour(inst, 261, false);
        if (passive) {
            for (i = 0; i < length; i++) {
                if (i % 2 == 0) {
                    draw_point(inst, startx, starty);
                }
                startx += dx;
                starty += dy;
            }
        } else if (active) {
            draw_line(inst, startx, starty,
                      startx + (length-1) * dx, starty + (length-1) * dy);
        } /* else no cursor (e.g., blinked off) */
    }

    draw_update(inst,
                x*inst->font_width+inst->window_border,
                y*inst->font_height+inst->window_border,
                len*widefactor*inst->font_width, inst->font_height);

#if GTK_CHECK_VERSION(2,0,0)
    {
        GdkRectangle cursorrect;
        cursorrect.x = x*inst->font_width+inst->window_border;
        cursorrect.y = y*inst->font_height+inst->window_border;
        cursorrect.width = len*widefactor*inst->font_width;
        cursorrect.height = inst->font_height;
        gtk_im_context_set_cursor_location(inst->imc, &cursorrect);
    }
#endif
}

#if !GTK_CHECK_VERSION(2,0,0)
/*
 * For GTK 1, manual code to scale an in-memory XPM, producing a new
 * one as output. It will be ugly, but good enough to use as a trust
 * sigil.
 */
struct XpmHolder {
    char **strings;
    size_t nstrings;
};

static void xpmholder_free(XpmHolder *xh)
{
    for (size_t i = 0; i < xh->nstrings; i++)
        sfree(xh->strings[i]);
    sfree(xh->strings);
    sfree(xh);
}

static XpmHolder *xpm_scale(const char *const *xpm, int wo, int ho)
{
    /* Get image dimensions, # colours, and chars-per-pixel */
    int wi = 0, hi = 0, nc = 0, cpp = 0;
    int retd = sscanf(xpm[0], "%d %d %d %d", &wi, &hi, &nc, &cpp);
    assert(retd == 4);

    /* Make output XpmHolder */
    XpmHolder *xh = snew(XpmHolder);
    xh->nstrings = 1 + nc + ho;
    xh->strings = snewn(xh->nstrings, char *);

    /* Set up header */
    xh->strings[0] = dupprintf("%d %d %d %d", wo, ho, nc, cpp);
    for (int i = 0; i < nc; i++)
        xh->strings[1 + i] = dupstr(xpm[1 + i]);

    /* Scale image */
    for (int yo = 0; yo < ho; yo++) {
        int yi = yo * hi / ho;
        char *ro = snewn(cpp * wo + 1, char);
        ro[cpp * wo] = '\0';
        xh->strings[1 + nc + yo] = ro;
        const char *ri = xpm[1 + nc + yi];

        for (int xo = 0; xo < wo; xo++) {
            int xi = xo * wi / wo;
            memcpy(ro + cpp * xo, ri + cpp * xi, cpp);
        }
    }

    return xh;
}
#endif /* !GTK_CHECK_VERSION(2,0,0) */

static void gtkwin_draw_trust_sigil(TermWin *tw, int cx, int cy)
{
    GtkFrontend *inst = container_of(tw, GtkFrontend, termwin);

    int x = cx * inst->font_width + inst->window_border;
    int y = cy * inst->font_height + inst->window_border;
    int w = 2*inst->font_width, h = inst->font_height;

    if (inst->trust_sigil_w != w || inst->trust_sigil_h != h ||
#if GTK_CHECK_VERSION(2,0,0)
        !inst->trust_sigil_pb
#else
        !inst->trust_sigil_pm
#endif
        ) {

#if GTK_CHECK_VERSION(2,0,0)
        if (inst->trust_sigil_pb)
            g_object_unref(G_OBJECT(inst->trust_sigil_pb));
#else
        if (inst->trust_sigil_pm)
            gdk_pixmap_unref(inst->trust_sigil_pm);
#endif

        int best_icon_index = 0;
        unsigned score = UINT_MAX;
        for (int i = 0; i < n_main_icon; i++) {
            int iw, ih;
            if (sscanf(main_icon[i][0], "%d %d", &iw, &ih) == 2) {
                int this_excess = (iw + ih) - (w + h);
                unsigned this_score = (abs(this_excess) |
                                       (this_excess > 0 ? 0 : 0x80000000U));
                if (this_score < score) {
                    best_icon_index = i;
                    score = this_score;
                }
            }
        }

#if GTK_CHECK_VERSION(2,0,0)
        GdkPixbuf *icon_unscaled = gdk_pixbuf_new_from_xpm_data(
            (const gchar **)main_icon[best_icon_index]);
        inst->trust_sigil_pb = gdk_pixbuf_scale_simple(
            icon_unscaled, w, h, GDK_INTERP_BILINEAR);
        g_object_unref(G_OBJECT(icon_unscaled));
#else
        XpmHolder *xh = xpm_scale(main_icon[best_icon_index], w, h);
        inst->trust_sigil_pm = gdk_pixmap_create_from_xpm_d(
            gtk_widget_get_window(inst->window), NULL,
            &inst->cols[258], xh->strings);
        xpmholder_free(xh);
#endif

        inst->trust_sigil_w = w;
        inst->trust_sigil_h = h;
    }

#ifdef DRAW_TEXT_GDK
    if (inst->uctx.type == DRAWTYPE_GDK) {
#if GTK_CHECK_VERSION(2,0,0)
        gdk_draw_pixbuf(inst->uctx.u.gdk.target, inst->uctx.u.gdk.gc,
                        inst->trust_sigil_pb, 0, 0, x, y, w, h,
                        GDK_RGB_DITHER_NORMAL, 0, 0);
#else
        gdk_draw_pixmap(inst->uctx.u.gdk.target, inst->uctx.u.gdk.gc,
                        inst->trust_sigil_pm, 0, 0, x, y, w, h);
#endif
    }
#endif
#ifdef DRAW_TEXT_CAIRO
    if (inst->uctx.type == DRAWTYPE_CAIRO) {
        inst->uctx.u.cairo.widget = GTK_WIDGET(inst->area);
        cairo_save(inst->uctx.u.cairo.cr);
        cairo_translate(inst->uctx.u.cairo.cr, x, y);
        gdk_cairo_set_source_pixbuf(inst->uctx.u.cairo.cr,
                                    inst->trust_sigil_pb, 0, 0);
        cairo_rectangle(inst->uctx.u.cairo.cr, 0, 0, w, h);
        cairo_fill(inst->uctx.u.cairo.cr);
        cairo_restore(inst->uctx.u.cairo.cr);
    }
#endif

    draw_update(inst, x, y, w, h);
}

GdkCursor *make_mouse_ptr(GtkFrontend *inst, int cursor_val)
{
    if (cursor_val == -1) {
#if GTK_CHECK_VERSION(2,16,0)
        cursor_val = GDK_BLANK_CURSOR;
#else
        /*
         * Work around absence of GDK_BLANK_CURSOR by inventing a
         * blank pixmap.
         */
        GdkCursor *ret;
        GdkColor bg = { 0, 0, 0, 0 };
        GdkPixmap *pm = gdk_pixmap_new(NULL, 1, 1, 1);
        GdkGC *gc = gdk_gc_new(pm);
        gdk_gc_set_foreground(gc, &bg);
        gdk_draw_rectangle(pm, gc, 1, 0, 0, 1, 1);
        gdk_gc_unref(gc);
        ret = gdk_cursor_new_from_pixmap(pm, pm, &bg, &bg, 1, 1);
        gdk_pixmap_unref(pm);
        return ret;
#endif
    }

    return gdk_cursor_new(cursor_val);
}

void modalfatalbox(const char *p, ...)
{
    va_list ap;
    fprintf(stderr, "FATAL ERROR: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static const char *gtk_seat_get_x_display(Seat *seat)
{
    return gdk_get_display();
}

#ifndef NOT_X_WINDOWS
static bool gtk_seat_get_windowid(Seat *seat, long *id)
{
    GtkFrontend *inst = container_of(seat, GtkFrontend, seat);
    GdkWindow *window = gtk_widget_get_window(inst->area);
    if (!GDK_IS_X11_WINDOW(window))
        return false;
    *id = GDK_WINDOW_XID(window);
    return true;
}
#endif

char *setup_fonts_ucs(GtkFrontend *inst)
{
    bool shadowbold = conf_get_bool(inst->conf, CONF_shadowbold);
    int shadowboldoffset = conf_get_int(inst->conf, CONF_shadowboldoffset);
    FontSpec *fs;
    unifont *fonts[4];
    int i;

    fs = conf_get_fontspec(inst->conf, CONF_font);
    fonts[0] = multifont_create(inst->area, fs->name, false, false,
                                shadowboldoffset, shadowbold);
    if (!fonts[0]) {
        return dupprintf("unable to load font \"%s\"", fs->name);
    }

    fs = conf_get_fontspec(inst->conf, CONF_boldfont);
    if (shadowbold || !fs->name[0]) {
        fonts[1] = NULL;
    } else {
        fonts[1] = multifont_create(inst->area, fs->name, false, true,
                                    shadowboldoffset, shadowbold);
        if (!fonts[1]) {
            if (fonts[0])
                unifont_destroy(fonts[0]);
            return dupprintf("unable to load bold font \"%s\"", fs->name);
        }
    }

    fs = conf_get_fontspec(inst->conf, CONF_widefont);
    if (fs->name[0]) {
        fonts[2] = multifont_create(inst->area, fs->name, true, false,
                                    shadowboldoffset, shadowbold);
        if (!fonts[2]) {
            for (i = 0; i < 2; i++)
                if (fonts[i])
                    unifont_destroy(fonts[i]);
            return dupprintf("unable to load wide font \"%s\"", fs->name);
        }
    } else {
        fonts[2] = NULL;
    }

    fs = conf_get_fontspec(inst->conf, CONF_wideboldfont);
    if (shadowbold || !fs->name[0]) {
        fonts[3] = NULL;
    } else {
        fonts[3] = multifont_create(inst->area, fs->name, true, true,
                                    shadowboldoffset, shadowbold);
        if (!fonts[3]) {
            for (i = 0; i < 3; i++)
                if (fonts[i])
                    unifont_destroy(fonts[i]);
            return dupprintf("unable to load wide bold font \"%s\"", fs->name);
        }
    }

    /*
     * Now we've got past all the possible error conditions, we can
     * actually update our state.
     */

    for (i = 0; i < 4; i++) {
        if (inst->fonts[i])
            unifont_destroy(inst->fonts[i]);
        inst->fonts[i] = fonts[i];
    }

    if (inst->font_width != inst->fonts[0]->width ||
        inst->font_height != inst->fonts[0]->height) {

        inst->font_width = inst->fonts[0]->width;
        inst->font_height = inst->fonts[0]->height;

        /*
         * The font size has changed, so force the next call to
         * drawing_area_setup to regenerate the backing surface.
         */
        inst->drawing_area_setup_needed = true;
    }

    inst->direct_to_font = init_ucs(&inst->ucsdata,
                                    conf_get_str(inst->conf, CONF_line_codepage),
                                    conf_get_bool(inst->conf, CONF_utf8_override),
                                    inst->fonts[0]->public_charset,
                                    conf_get_int(inst->conf, CONF_vtmode));

    inst->drawtype = inst->fonts[0]->preferred_drawtype;

    return NULL;
}

#if GTK_CHECK_VERSION(3,0,0)
struct find_app_menu_bar_ctx {
    GtkWidget *area, *menubar;
};
static void find_app_menu_bar(GtkWidget *widget, gpointer data)
{
    struct find_app_menu_bar_ctx *ctx = (struct find_app_menu_bar_ctx *)data;
    if (widget != ctx->area && GTK_IS_MENU_BAR(widget))
        ctx->menubar = widget;
}
#endif

static void compute_geom_hints(GtkFrontend *inst, GdkGeometry *geom)
{
    /*
     * Unused fields in geom.
     */
    geom->max_width = geom->max_height = -1;
    geom->min_aspect = geom->max_aspect = 0;

    /*
     * Set up the geometry fields we care about, with reference to
     * just the drawing area. We'll correct for other widgets in a
     * moment.
     */
    geom->min_width = inst->font_width + 2*inst->window_border;
    geom->min_height = inst->font_height + 2*inst->window_border;
    geom->base_width = 2*inst->window_border;
    geom->base_height = 2*inst->window_border;
    geom->width_inc = inst->font_width;
    geom->height_inc = inst->font_height;

    /*
     * If we've got a scrollbar visible, then we must include its
     * width as part of the base and min width, and also ensure that
     * our window's minimum height is at least the height required by
     * the scrollbar.
     *
     * In the latter case, we must also take care to arrange that
     * (geom->min_height - geom->base_height) is an integer multiple of
     * geom->height_inc, because if it's not, then some window managers
     * (we know of xfwm4) get confused, with the effect that they
     * resize our window to a height based on min_height instead of
     * base_height, which we then round down and the window ends up
     * too short.
     */
    if (inst->sbar_visible) {
        GtkRequisition req;
        int min_sb_height;

#if GTK_CHECK_VERSION(3,0,0)
        gtk_widget_get_preferred_size(inst->sbar, &req, NULL);
#else
        gtk_widget_size_request(inst->sbar, &req);
#endif

        /* Compute rounded-up scrollbar height. */
        min_sb_height = req.height;
        min_sb_height += geom->height_inc - 1;
        min_sb_height -= ((min_sb_height - geom->base_height%geom->height_inc)
                          % geom->height_inc);

        geom->min_width += req.width;
        geom->base_width += req.width;
        if (geom->min_height < min_sb_height)
            geom->min_height = min_sb_height;
    }

#if GTK_CHECK_VERSION(3,0,0)
    /*
     * And if we're running a main-gtk-application.c based program and
     * GtkApplicationWindow has given us a menu bar inside the window,
     * then we must take that into account as well.
     *
     * In its unbounded wisdom, GtkApplicationWindow doesn't actually
     * give us a direct function call to _find_ the menu bar widget.
     * Fortunately, we can find it by enumerating the children of the
     * top-level window and looking for one we didn't put there
     * ourselves.
     */
    {
        struct find_app_menu_bar_ctx ctx[1];
        ctx->area = inst->area;
        ctx->menubar = NULL;
        gtk_container_foreach(GTK_CONTAINER(inst->window),
                              find_app_menu_bar, ctx);

        if (ctx->menubar) {
            GtkRequisition req;
            int min_menu_width;
            gtk_widget_get_preferred_size(ctx->menubar, NULL, &req);

            /*
             * This time, the height adjustment is easy (the menu bar
             * sits above everything), but we have to take care with
             * the _width_ to ensure we keep min_width and base_width
             * congruent modulo width_inc.
             */
            geom->min_height += req.height;
            geom->base_height += req.height;

            min_menu_width = req.width;
            min_menu_width += geom->width_inc - 1;
            min_menu_width -=
                ((min_menu_width - geom->base_width%geom->width_inc)
                 % geom->width_inc);
            if (geom->min_width < min_menu_width)
                geom->min_width = min_menu_width;
        }
    }
#endif
}

void set_geom_hints(GtkFrontend *inst)
{
    /*
     * 2021-12-20: I've found that on Ubuntu 20.04 Wayland (using GTK
     * 3.24.20), setting geometry hints causes the window size to come
     * out wrong. As far as I can tell, that's because the GDK Wayland
     * backend internally considers windows to be a lot larger than
     * their obvious display size (*even* considering visible window
     * furniture like title bars), with an extra margin on every side
     * to account for surrounding effects like shadows. And the
     * geometry hints like base size and resize increment are applied
     * to that larger size rather than the more obvious 'client area'
     * size. So when we ask for a window of exactly the size we want,
     * it gets modified by GDK based on the geometry hints, but
     * applying this extra margin, which causes the size to be a
     * little bit too small.
     *
     * I don't know how you can sensibly find out the size of that
     * margin. If I did, I could account for it in the geometry hints.
     * But I also see that gtk_window_set_geometry_hints is removed in
     * GTK 4, which suggests that probably doing a lot of hard work to
     * fix this is not the way forward.
     *
     * So instead, I simply avoid setting geometry hints at all on any
     * GDK backend other than X11, and hopefully that's a workaround.
     */
#if GTK_CHECK_VERSION(3,0,0)
    if (!GDK_IS_X11_DISPLAY(gdk_display_get_default()))
        return;
#endif

    const struct BackendVtable *vt;
    GdkGeometry geom;
    gint flags = GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE | GDK_HINT_RESIZE_INC;
    compute_geom_hints(inst, &geom);
#if GTK_CHECK_VERSION(2,0,0)
    if (inst->gotpos)
        flags |= GDK_HINT_USER_POS;
#endif
    vt = backend_vt_from_proto(conf_get_int(inst->conf, CONF_protocol));
    if (vt && vt->flags & BACKEND_RESIZE_FORBIDDEN) {
        /* Window resizing forbidden.  Set both minimum and maximum
         * dimensions to be the initial size. */
        geom.min_width = geom.base_width + geom.width_inc * inst->width;
        geom.min_height = geom.base_height + geom.height_inc * inst->height;
        geom.max_width = geom.min_width;
        geom.max_height = geom.min_height;
        flags |= GDK_HINT_MAX_SIZE;
    }
    gtk_window_set_geometry_hints(GTK_WINDOW(inst->window),
                                  NULL, &geom, flags);
}

#if GTK_CHECK_VERSION(2,0,0)
static void compute_whole_window_size(GtkFrontend *inst,
                                      int wchars, int hchars,
                                      int *wpix, int *hpix)
{
    GdkGeometry geom;
    compute_geom_hints(inst, &geom);
    if (wpix) *wpix = geom.base_width + wchars * geom.width_inc;
    if (hpix) *hpix = geom.base_height + hchars * geom.height_inc;
}
#endif

void clear_scrollback_menuitem(GtkMenuItem *item, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    term_clrsb(inst->term);
}

void reset_terminal_menuitem(GtkMenuItem *item, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    term_pwron(inst->term, true);
    if (inst->ldisc)
        ldisc_echoedit_update(inst->ldisc);
}

void copy_clipboard_menuitem(GtkMenuItem *item, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    static const int clips[] = { MENU_CLIPBOARD };
    term_request_copy(inst->term, clips, lenof(clips));
}

void paste_clipboard_menuitem(GtkMenuItem *item, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    term_request_paste(inst->term, MENU_CLIPBOARD);
}

void copy_all_menuitem(GtkMenuItem *item, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    static const int clips[] = { COPYALL_CLIPBOARDS };
    term_copyall(inst->term, clips, lenof(clips));
}

void special_menuitem(GtkMenuItem *item, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    SessionSpecial *sc = g_object_get_data(G_OBJECT(item), "user-data");

    if (inst->backend)
        backend_special(inst->backend, sc->code, sc->arg);
}

void about_menuitem(GtkMenuItem *item, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    about_box(inst->window);
}

void event_log_menuitem(GtkMenuItem *item, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    showeventlog(inst->eventlogstuff, inst->window);
}

void setup_clipboards(GtkFrontend *inst, Terminal *term, Conf *conf)
{
    assert(term->mouse_select_clipboards[0] == CLIP_LOCAL);

    term->n_mouse_select_clipboards = 1;
    term->mouse_select_clipboards[
        term->n_mouse_select_clipboards++] = MOUSE_SELECT_CLIPBOARD;

    if (conf_get_bool(conf, CONF_mouseautocopy)) {
        term->mouse_select_clipboards[
            term->n_mouse_select_clipboards++] = CLIP_CLIPBOARD;
    }

    set_clipboard_atom(inst, CLIP_CUSTOM_1, GDK_NONE);
    set_clipboard_atom(inst, CLIP_CUSTOM_2, GDK_NONE);
    set_clipboard_atom(inst, CLIP_CUSTOM_3, GDK_NONE);

    switch (conf_get_int(conf, CONF_mousepaste)) {
      case CLIPUI_IMPLICIT:
        term->mouse_paste_clipboard = MOUSE_PASTE_CLIPBOARD;
        break;
      case CLIPUI_EXPLICIT:
        term->mouse_paste_clipboard = CLIP_CLIPBOARD;
        break;
      case CLIPUI_CUSTOM:
        term->mouse_paste_clipboard = CLIP_CUSTOM_1;
        set_clipboard_atom(inst, CLIP_CUSTOM_1,
                           gdk_atom_intern(
                               conf_get_str(conf, CONF_mousepaste_custom),
                               false));
        break;
      default:
        term->mouse_paste_clipboard = CLIP_NULL;
        break;
    }

    if (conf_get_int(conf, CONF_ctrlshiftins) == CLIPUI_CUSTOM) {
        GdkAtom atom = gdk_atom_intern(
            conf_get_str(conf, CONF_ctrlshiftins_custom), false);
        struct clipboard_state *state = clipboard_from_atom(inst, atom);
        if (state) {
            inst->clipboard_ctrlshiftins = state->clipboard;
        } else {
            inst->clipboard_ctrlshiftins = CLIP_CUSTOM_2;
            set_clipboard_atom(inst, CLIP_CUSTOM_2, atom);
        }
    }

    if (conf_get_int(conf, CONF_ctrlshiftcv) == CLIPUI_CUSTOM) {
        GdkAtom atom = gdk_atom_intern(
            conf_get_str(conf, CONF_ctrlshiftcv_custom), false);
        struct clipboard_state *state = clipboard_from_atom(inst, atom);
        if (state) {
            inst->clipboard_ctrlshiftins = state->clipboard;
        } else {
            inst->clipboard_ctrlshiftcv = CLIP_CUSTOM_3;
            set_clipboard_atom(inst, CLIP_CUSTOM_3, atom);
        }
    }
}

struct after_change_settings_dialog_ctx {
    GtkFrontend *inst;
    Conf *newconf;
};

static void after_change_settings_dialog(void *vctx, int retval);

void change_settings_menuitem(GtkMenuItem *item, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    struct after_change_settings_dialog_ctx *ctx;
    GtkWidget *dialog;
    char *title;

    if (find_and_raise_dialog(inst, DIALOG_SLOT_RECONFIGURE))
        return;

    title = dupcat(appname, " Reconfiguration");

    ctx = snew(struct after_change_settings_dialog_ctx);
    ctx->inst = inst;
    ctx->newconf = conf_copy(inst->conf);

    term_pre_reconfig(inst->term, ctx->newconf);

    dialog = create_config_box(
        title, ctx->newconf, true,
        inst->backend ? backend_cfg_info(inst->backend) : 0,
        after_change_settings_dialog, ctx);
    register_dialog(&inst->seat, DIALOG_SLOT_RECONFIGURE, dialog);

    sfree(title);
}

static void after_change_settings_dialog(void *vctx, int retval)
{
    struct after_change_settings_dialog_ctx ctx =
        *(struct after_change_settings_dialog_ctx *)vctx;
    GtkFrontend *inst = ctx.inst;
    Conf *oldconf = inst->conf, *newconf = ctx.newconf;
    bool need_size;

    sfree(vctx); /* we've copied this already */

    unregister_dialog(&inst->seat, DIALOG_SLOT_RECONFIGURE);

    if (retval > 0) {
        inst->conf = newconf;

        /* Pass new config data to the logging module */
        log_reconfig(inst->logctx, inst->conf);
        /*
         * Flush the line discipline's edit buffer in the case
         * where local editing has just been disabled.
         */
        if (inst->ldisc) {
            ldisc_configure(inst->ldisc, inst->conf);
            ldisc_echoedit_update(inst->ldisc);
        }
        /* Pass new config data to the terminal */
        term_reconfig(inst->term, inst->conf);
        setup_clipboards(inst, inst->term, inst->conf);
        /* Pass new config data to the back end */
        if (inst->backend)
            backend_reconfig(inst->backend, inst->conf);

        cache_conf_values(inst);

        need_size = false;

        /*
         * If the scrollbar needs to be shown, hidden, or moved
         * from one end to the other of the window, do so now.
         */
        if (conf_get_bool(oldconf, CONF_scrollbar) !=
            conf_get_bool(newconf, CONF_scrollbar)) {
            show_scrollbar(inst, conf_get_bool(newconf, CONF_scrollbar));
            need_size = true;
        }
        if (conf_get_bool(oldconf, CONF_scrollbar_on_left) !=
            conf_get_bool(newconf, CONF_scrollbar_on_left)) {
            gtk_box_reorder_child(inst->hbox, inst->sbar,
                                  conf_get_bool(newconf, CONF_scrollbar_on_left)
                                  ? 0 : 1);
        }

        /*
         * Redo the whole tangled fonts and Unicode mess if
         * necessary.
         */
        if (strcmp(conf_get_fontspec(oldconf, CONF_font)->name,
                   conf_get_fontspec(newconf, CONF_font)->name) ||
            strcmp(conf_get_fontspec(oldconf, CONF_boldfont)->name,
                   conf_get_fontspec(newconf, CONF_boldfont)->name) ||
            strcmp(conf_get_fontspec(oldconf, CONF_widefont)->name,
                   conf_get_fontspec(newconf, CONF_widefont)->name) ||
            strcmp(conf_get_fontspec(oldconf, CONF_wideboldfont)->name,
                   conf_get_fontspec(newconf, CONF_wideboldfont)->name) ||
            strcmp(conf_get_str(oldconf, CONF_line_codepage),
                   conf_get_str(newconf, CONF_line_codepage)) ||
            conf_get_bool(oldconf, CONF_utf8_override) !=
            conf_get_bool(newconf, CONF_utf8_override) ||
            conf_get_int(oldconf, CONF_vtmode) !=
            conf_get_int(newconf, CONF_vtmode) ||
            conf_get_bool(oldconf, CONF_shadowbold) !=
            conf_get_bool(newconf, CONF_shadowbold) ||
            conf_get_int(oldconf, CONF_shadowboldoffset) !=
            conf_get_int(newconf, CONF_shadowboldoffset)) {
            char *errmsg = setup_fonts_ucs(inst);
            if (errmsg) {
                char *msgboxtext =
                    dupprintf("Could not change fonts in terminal window: %s\n",
                              errmsg);
                create_message_box(
                    inst->window, "Font setup error", msgboxtext,
                    string_width("Could not change fonts in terminal window:"),
                    false, &buttons_ok, trivial_post_dialog_fn, NULL);
                sfree(msgboxtext);
                sfree(errmsg);
            } else {
                need_size = true;
            }
        }

        /*
         * Resize the window.
         */
        if (conf_get_int(oldconf, CONF_width) !=
            conf_get_int(newconf, CONF_width) ||
            conf_get_int(oldconf, CONF_height) !=
            conf_get_int(newconf, CONF_height) ||
            conf_get_int(oldconf, CONF_window_border) !=
            conf_get_int(newconf, CONF_window_border) ||
            need_size) {
            set_geom_hints(inst);
            win_request_resize(&inst->termwin,
                               conf_get_int(newconf, CONF_width),
                               conf_get_int(newconf, CONF_height));
        } else {
            /*
             * The above will have caused a call to term_size() for
             * us if it happened. If the user has fiddled with only
             * the scrollback size, the above will not have
             * happened and we will need an explicit term_size()
             * here.
             */
            if (conf_get_int(oldconf, CONF_savelines) !=
                conf_get_int(newconf, CONF_savelines))
                term_size(inst->term, inst->term->rows, inst->term->cols,
                          conf_get_int(newconf, CONF_savelines));
        }

        term_invalidate(inst->term);

        /*
         * We do an explicit full redraw here to ensure the window
         * border has been redrawn as well as the text area.
         */
        gtk_widget_queue_draw(inst->area);

        conf_free(oldconf);
    } else {
        conf_free(newconf);
    }
}

static void change_font_size(GtkFrontend *inst, int increment)
{
    static const int conf_keys[lenof(inst->fonts)] = {
        CONF_font, CONF_boldfont, CONF_widefont, CONF_wideboldfont,
    };
    FontSpec *oldfonts[lenof(inst->fonts)];
    FontSpec *newfonts[lenof(inst->fonts)];
    char *errmsg = NULL;
    int i;

    for (i = 0; i < lenof(newfonts); i++)
        oldfonts[i] = newfonts[i] = NULL;

    for (i = 0; i < lenof(inst->fonts); i++) {
        if (inst->fonts[i]) {
            char *newname = unifont_size_increment(inst->fonts[i], increment);
            if (!newname)
                goto cleanup;
            newfonts[i] = fontspec_new(newname);
            sfree(newname);
        }
    }

    for (i = 0; i < lenof(newfonts); i++) {
        if (newfonts[i]) {
            oldfonts[i] = fontspec_copy(
                conf_get_fontspec(inst->conf, conf_keys[i]));
            conf_set_fontspec(inst->conf, conf_keys[i], newfonts[i]);
        }
    }

    errmsg = setup_fonts_ucs(inst);
    if (errmsg)
        goto cleanup;

    /* Success, so suppress putting everything back */
    for (i = 0; i < lenof(newfonts); i++) {
        if (oldfonts[i]) {
            fontspec_free(oldfonts[i]);
            oldfonts[i] = NULL;
        }
    }

    set_geom_hints(inst);
    win_request_resize(&inst->termwin, conf_get_int(inst->conf, CONF_width),
                       conf_get_int(inst->conf, CONF_height));
    term_invalidate(inst->term);
    gtk_widget_queue_draw(inst->area);

  cleanup:
    for (i = 0; i < lenof(oldfonts); i++) {
        if (oldfonts[i]) {
            conf_set_fontspec(inst->conf, conf_keys[i], oldfonts[i]);
            fontspec_free(oldfonts[i]);
        }
        if (newfonts[i])
            fontspec_free(newfonts[i]);
    }
    sfree(errmsg);
}

void dup_session_menuitem(GtkMenuItem *item, gpointer gdata)
{
    GtkFrontend *inst = (GtkFrontend *)gdata;

    launch_duplicate_session(inst->conf);
}

void new_session_menuitem(GtkMenuItem *item, gpointer data)
{
    launch_new_session();
}

void restart_session_menuitem(GtkMenuItem *item, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;

    if (!inst->backend) {
        logevent(inst->logctx, "----- Session restarted -----");
        term_pwron(inst->term, false);
        start_backend(inst);
        inst->exited = false;
    }
}

void saved_session_menuitem(GtkMenuItem *item, gpointer data)
{
    char *str = (char *)g_object_get_data(G_OBJECT(item), "user-data");

    launch_saved_session(str);
}

void saved_session_freedata(GtkMenuItem *item, gpointer data)
{
    char *str = (char *)g_object_get_data(G_OBJECT(item), "user-data");

    sfree(str);
}

void app_menu_action(GtkFrontend *frontend, enum MenuAction action)
{
    GtkFrontend *inst = (GtkFrontend *)frontend;
    switch (action) {
      case MA_COPY:
        copy_clipboard_menuitem(NULL, inst);
        break;
      case MA_PASTE:
        paste_clipboard_menuitem(NULL, inst);
        break;
      case MA_COPY_ALL:
        copy_all_menuitem(NULL, inst);
        break;
      case MA_DUPLICATE_SESSION:
        dup_session_menuitem(NULL, inst);
        break;
      case MA_RESTART_SESSION:
        restart_session_menuitem(NULL, inst);
        break;
      case MA_CHANGE_SETTINGS:
        change_settings_menuitem(NULL, inst);
        break;
      case MA_CLEAR_SCROLLBACK:
        clear_scrollback_menuitem(NULL, inst);
        break;
      case MA_RESET_TERMINAL:
        reset_terminal_menuitem(NULL, inst);
        break;
      case MA_EVENT_LOG:
        event_log_menuitem(NULL, inst);
        break;
    }
}

static void update_savedsess_menu(GtkMenuItem *menuitem, gpointer data)
{
    GtkFrontend *inst = (GtkFrontend *)data;
    struct sesslist sesslist;
    int i;

    gtk_container_foreach(GTK_CONTAINER(inst->sessionsmenu),
                          (GtkCallback)gtk_widget_destroy, NULL);

    get_sesslist(&sesslist, true);
    /* skip sesslist.sessions[0] == Default Settings */
    for (i = 1; i < sesslist.nsessions; i++) {
        GtkWidget *menuitem =
            gtk_menu_item_new_with_label(sesslist.sessions[i]);
        gtk_container_add(GTK_CONTAINER(inst->sessionsmenu), menuitem);
        gtk_widget_show(menuitem);
        g_object_set_data(G_OBJECT(menuitem), "user-data",
                          dupstr(sesslist.sessions[i]));
        g_signal_connect(G_OBJECT(menuitem), "activate",
                         G_CALLBACK(saved_session_menuitem),
                         inst);
        g_signal_connect(G_OBJECT(menuitem), "destroy",
                         G_CALLBACK(saved_session_freedata),
                         inst);
    }
    if (sesslist.nsessions <= 1) {
        GtkWidget *menuitem =
            gtk_menu_item_new_with_label("(No sessions)");
        gtk_widget_set_sensitive(menuitem, false);
        gtk_container_add(GTK_CONTAINER(inst->sessionsmenu), menuitem);
        gtk_widget_show(menuitem);
    }
    get_sesslist(&sesslist, false); /* free up */
}

void set_window_icon(GtkWidget *window, const char *const *const *icon,
                     int n_icon)
{
#if GTK_CHECK_VERSION(2,0,0)
    GList *iconlist;
    int n;
#else
    GdkPixmap *iconpm;
    GdkBitmap *iconmask;
#endif

    if (!n_icon)
        return;

    gtk_widget_realize(window);
#if GTK_CHECK_VERSION(2,0,0)
    gtk_window_set_icon(GTK_WINDOW(window),
                        gdk_pixbuf_new_from_xpm_data((const gchar **)icon[0]));
#else
    iconpm = gdk_pixmap_create_from_xpm_d(gtk_widget_get_window(window),
                                          &iconmask, NULL, (gchar **)icon[0]);
    gdk_window_set_icon(gtk_widget_get_window(window), NULL, iconpm, iconmask);
#endif

#if GTK_CHECK_VERSION(2,0,0)
    iconlist = NULL;
    for (n = 0; n < n_icon; n++) {
        iconlist =
            g_list_append(iconlist,
                          gdk_pixbuf_new_from_xpm_data((const gchar **)
                                                       icon[n]));
    }
    gtk_window_set_icon_list(GTK_WINDOW(window), iconlist);
#endif
}

static void free_special_cmd(gpointer data) { sfree(data); }

static void gtk_seat_update_specials_menu(Seat *seat)
{
    GtkFrontend *inst = container_of(seat, GtkFrontend, seat);
    const SessionSpecial *specials;

    if (inst->backend)
        specials = backend_get_specials(inst->backend);
    else
        specials = NULL;

    /* I believe this disposes of submenus too. */
    gtk_container_foreach(GTK_CONTAINER(inst->specialsmenu),
                          (GtkCallback)gtk_widget_destroy, NULL);
    if (specials) {
        int i;
        GtkWidget *menu = inst->specialsmenu;
        /* A lame "stack" for submenus that will do for now. */
        GtkWidget *saved_menu = NULL;
        int nesting = 1;
        for (i = 0; nesting > 0; i++) {
            GtkWidget *menuitem = NULL;
            switch (specials[i].code) {
              case SS_SUBMENU:
                assert (nesting < 2);
                saved_menu = menu; /* XXX lame stacking */
                menu = gtk_menu_new();
                menuitem = gtk_menu_item_new_with_label(specials[i].name);
                gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), menu);
                gtk_container_add(GTK_CONTAINER(saved_menu), menuitem);
                gtk_widget_show(menuitem);
                menuitem = NULL;
                nesting++;
                break;
              case SS_EXITMENU:
                nesting--;
                if (nesting) {
                    menu = saved_menu; /* XXX lame stacking */
                    saved_menu = NULL;
                }
                break;
              case SS_SEP:
                menuitem = gtk_menu_item_new();
                break;
              default: {
                menuitem = gtk_menu_item_new_with_label(specials[i].name);
                SessionSpecial *sc = snew(SessionSpecial);
                *sc = specials[i]; /* structure copy */
                g_object_set_data_full(G_OBJECT(menuitem), "user-data",
                                       sc, free_special_cmd);
                g_signal_connect(G_OBJECT(menuitem), "activate",
                                 G_CALLBACK(special_menuitem), inst);
                break;
              }
            }
            if (menuitem) {
                gtk_container_add(GTK_CONTAINER(menu), menuitem);
                gtk_widget_show(menuitem);
            }
        }
        gtk_widget_show(inst->specialsitem1);
        gtk_widget_show(inst->specialsitem2);
    } else {
        gtk_widget_hide(inst->specialsitem1);
        gtk_widget_hide(inst->specialsitem2);
    }
}

static void start_backend(GtkFrontend *inst)
{
    const struct BackendVtable *vt;
    char *error, *realhost;

    vt = select_backend(inst->conf);

    seat_set_trust_status(&inst->seat, true);
    error = backend_init(vt, &inst->seat, &inst->backend,
                         inst->logctx, inst->conf,
                         conf_get_str(inst->conf, CONF_host),
                         conf_get_int(inst->conf, CONF_port),
                         &realhost,
                         conf_get_bool(inst->conf, CONF_tcp_nodelay),
                         conf_get_bool(inst->conf, CONF_tcp_keepalives));

    if (error) {
        seat_connection_fatal(&inst->seat,
                              "Unable to open connection to %s:\n%s",
                              conf_dest(inst->conf), error);
        sfree(error);
        inst->exited = true;
        return;
    }

    term_setup_window_titles(inst->term, realhost);
    sfree(realhost);

    term_provide_backend(inst->term, inst->backend);

    inst->ldisc = ldisc_create(inst->conf, inst->term, inst->backend,
                               &inst->seat);

    gtk_widget_set_sensitive(inst->restartitem, false);
}

#if GTK_CHECK_VERSION(2,0,0)
static void get_monitor_geometry(GtkWidget *widget, GdkRectangle *geometry)
{
#if GTK_CHECK_VERSION(3,4,0)
    GdkDisplay *display = gtk_widget_get_display(widget);
    GdkWindow *gdkwindow = gtk_widget_get_window(widget);
# if GTK_CHECK_VERSION(3,22,0)
    GdkMonitor *monitor;
    if (gdkwindow)
        monitor = gdk_display_get_monitor_at_window(display, gdkwindow);
    else
        monitor = gdk_display_get_monitor(display, 0);
    gdk_monitor_get_geometry(monitor, geometry);
# else
    GdkScreen *screen = gdk_display_get_default_screen(display);
    gint monitor_num = gdk_screen_get_monitor_at_window(screen, gdkwindow);
    gdk_screen_get_monitor_geometry(screen, monitor_num, geometry);
# endif
#else
    geometry->x = geometry->y = 0;
    geometry->width = gdk_screen_width();
    geometry->height = gdk_screen_height();
#endif
}
#endif

static const TermWinVtable gtk_termwin_vt = {
    .setup_draw_ctx = gtkwin_setup_draw_ctx,
    .draw_text = gtkwin_draw_text,
    .draw_cursor = gtkwin_draw_cursor,
    .draw_trust_sigil = gtkwin_draw_trust_sigil,
    .char_width = gtkwin_char_width,
    .free_draw_ctx = gtkwin_free_draw_ctx,
    .set_cursor_pos = gtkwin_set_cursor_pos,
    .set_raw_mouse_mode = gtkwin_set_raw_mouse_mode,
    .set_raw_mouse_mode_pointer = gtkwin_set_raw_mouse_mode_pointer,
    .set_scrollbar = gtkwin_set_scrollbar,
    .bell = gtkwin_bell,
    .clip_write = gtkwin_clip_write,
    .clip_request_paste = gtkwin_clip_request_paste,
    .refresh = gtkwin_refresh,
    .request_resize = gtkwin_request_resize,
    .set_title = gtkwin_set_title,
    .set_icon_title = gtkwin_set_icon_title,
    .set_minimised = gtkwin_set_minimised,
    .set_maximised = gtkwin_set_maximised,
    .move = gtkwin_move,
    .set_zorder = gtkwin_set_zorder,
    .palette_set = gtkwin_palette_set,
    .palette_get_overrides = gtkwin_palette_get_overrides,
    .unthrottle = gtkwin_unthrottle,
};

void new_session_window(Conf *conf, const char *geometry_string)
{
    GtkFrontend *inst;

    prepare_session(conf);

    /*
     * Create an instance structure and initialise to zeroes
     */
    inst = snew(GtkFrontend);
    memset(inst, 0, sizeof(*inst));
#ifdef JUST_USE_GTK_CLIPBOARD_UTF8
    inst->cdi_headtail.next = inst->cdi_headtail.prev = &inst->cdi_headtail;
#endif
    inst->alt_keycode = -1;            /* this one needs _not_ to be zero */
    inst->busy_status = BUSY_NOT;
    inst->conf = conf;
    inst->wintitle = inst->icontitle = NULL;
    inst->drawtype = DRAWTYPE_DEFAULT;
#if GTK_CHECK_VERSION(3,4,0)
    inst->cumulative_scroll = 0.0;
#endif
    inst->drawing_area_setup_needed = true;

    inst->termwin.vt = &gtk_termwin_vt;
    inst->seat.vt = &gtk_seat_vt;
    inst->logpolicy.vt = &gtk_logpolicy_vt;

#ifndef NOT_X_WINDOWS
    inst->disp = get_x11_display();
    if (geometry_string) {
        int flags, x, y;
        unsigned int w, h;
        flags = XParseGeometry(geometry_string, &x, &y, &w, &h);
        if (flags & WidthValue)
            conf_set_int(conf, CONF_width, w);
        if (flags & HeightValue)
            conf_set_int(conf, CONF_height, h);

        if (flags & (XValue | YValue)) {
            inst->xpos = x;
            inst->ypos = y;
            inst->gotpos = true;
            inst->gravity = ((flags & XNegative ? 1 : 0) |
                             (flags & YNegative ? 2 : 0));
        }
    }
#endif

    if (!compound_text_atom)
        compound_text_atom = gdk_atom_intern("COMPOUND_TEXT", false);
    if (!utf8_string_atom)
        utf8_string_atom = gdk_atom_intern("UTF8_STRING", false);
    if (!clipboard_atom)
        clipboard_atom = gdk_atom_intern("CLIPBOARD", false);

    inst->area = gtk_drawing_area_new();
    gtk_widget_set_name(GTK_WIDGET(inst->area), "drawing-area");

    {
        char *errmsg = setup_fonts_ucs(inst);
        if (errmsg) {
            window_setup_error(errmsg);
            sfree(errmsg);
            gtk_widget_destroy(inst->area);
            sfree(inst);
            return;
        }
    }

#if GTK_CHECK_VERSION(2,0,0)
    inst->imc = gtk_im_multicontext_new();
#endif

    inst->window = make_gtk_toplevel_window(inst);
    gtk_widget_set_name(GTK_WIDGET(inst->window), "top-level");
    {
        const char *winclass = conf_get_str(inst->conf, CONF_winclass);
        if (*winclass) {
#if GTK_CHECK_VERSION(3,22,0)
#ifndef NOT_X_WINDOWS
            GdkWindow *gdkwin;
            gtk_widget_realize(GTK_WIDGET(inst->window));
            gdkwin = gtk_widget_get_window(GTK_WIDGET(inst->window));
            if (inst->disp && gdk_window_ensure_native(gdkwin)) {
                XClassHint *xch = XAllocClassHint();
                xch->res_name = (char *)winclass;
                xch->res_class = (char *)winclass;
                XSetClassHint(inst->disp, GDK_WINDOW_XID(gdkwin), xch);
                XFree(xch);
            }
#endif
            /*
             * If we do have NOT_X_WINDOWS set, then we don't have any
             * function in GTK 3.22 equivalent to the above. But then,
             * surely in that situation the deprecated
             * gtk_window_set_wmclass wouldn't have done anything
             * meaningful in previous GTKs either.
             */
#else
            gtk_window_set_wmclass(GTK_WINDOW(inst->window),
                                   winclass, winclass);
#endif
        }
    }

#if GTK_CHECK_VERSION(2,0,0)
    {
        const BackendVtable *vt = select_backend(inst->conf);
        if (vt && vt->flags & BACKEND_RESIZE_FORBIDDEN)
            gtk_window_set_resizable(GTK_WINDOW(inst->window), false);
    }
#endif

    inst->width = conf_get_int(inst->conf, CONF_width);
    inst->height = conf_get_int(inst->conf, CONF_height);
    cache_conf_values(inst);

    init_clipboard(inst);

    inst->sbar_adjust = GTK_ADJUSTMENT(gtk_adjustment_new(0,0,0,0,0,0));
    inst->sbar = gtk_vscrollbar_new(inst->sbar_adjust);
    inst->hbox = GTK_BOX(gtk_hbox_new(false, 0));
    /*
     * We always create the scrollbar; it remains invisible if
     * unwanted, so we can pop it up quickly if it suddenly becomes
     * desirable.
     */
    if (conf_get_bool(inst->conf, CONF_scrollbar_on_left))
        gtk_box_pack_start(inst->hbox, inst->sbar, false, false, 0);
    gtk_box_pack_start(inst->hbox, inst->area, true, true, 0);
    if (!conf_get_bool(inst->conf, CONF_scrollbar_on_left))
        gtk_box_pack_start(inst->hbox, inst->sbar, false, false, 0);

    gtk_container_add(GTK_CONTAINER(inst->window), GTK_WIDGET(inst->hbox));

    gtk_widget_show(inst->area);
    show_scrollbar(inst, conf_get_bool(inst->conf, CONF_scrollbar));
    gtk_widget_show(GTK_WIDGET(inst->hbox));

    /*
     * We must call gtk_widget_realize before setting up the geometry
     * hints, so that GtkApplicationWindow will have actually created
     * its menu bar (if it's going to) and hence compute_geom_hints
     * can find it to take its size into account.
     */
    gtk_widget_realize(inst->window);
    set_geom_hints(inst);

#if GTK_CHECK_VERSION(3,0,0)
    {
        int wp, hp;
        compute_whole_window_size(inst, inst->width, inst->height, &wp, &hp);
        gtk_window_set_default_size(GTK_WINDOW(inst->window), wp, hp);
    }
#else
    {
        int w = inst->font_width * inst->width + 2*inst->window_border;
        int h = inst->font_height * inst->height + 2*inst->window_border;
#if GTK_CHECK_VERSION(2,0,0)
        gtk_widget_set_size_request(inst->area, w, h);
#else
        gtk_drawing_area_size(GTK_DRAWING_AREA(inst->area), w, h);
#endif
    }
#endif

#if GTK_CHECK_VERSION(2,0,0)
    if (inst->gotpos) {
        static const GdkGravity gravities[] = {
            GDK_GRAVITY_NORTH_WEST,
            GDK_GRAVITY_NORTH_EAST,
            GDK_GRAVITY_SOUTH_WEST,
            GDK_GRAVITY_SOUTH_EAST,
        };
        int x = inst->xpos, y = inst->ypos;
        int wp, hp;
        GdkRectangle monitor_geometry;
        compute_whole_window_size(inst, inst->width, inst->height, &wp, &hp);
        get_monitor_geometry(GTK_WIDGET(inst->window), &monitor_geometry);
        if (inst->gravity & 1) x += (monitor_geometry.width - wp);
        if (inst->gravity & 2) y += (monitor_geometry.height - hp);
        gtk_window_set_gravity(GTK_WINDOW(inst->window),
                               gravities[inst->gravity & 3]);
        gtk_window_move(GTK_WINDOW(inst->window), x, y);
    }
#else
    if (inst->gotpos) {
        int x = inst->xpos, y = inst->ypos;
        GtkRequisition req;
        gtk_widget_size_request(GTK_WIDGET(inst->window), &req);
        if (inst->gravity & 1) x += gdk_screen_width() - req.width;
        if (inst->gravity & 2) y += gdk_screen_height() - req.height;
        gtk_window_set_position(GTK_WINDOW(inst->window), GTK_WIN_POS_NONE);
        gtk_widget_set_uposition(GTK_WIDGET(inst->window), x, y);
    }
#endif

    g_signal_connect(G_OBJECT(inst->window), "destroy",
                     G_CALLBACK(destroy), inst);
    g_signal_connect(G_OBJECT(inst->window), "delete_event",
                     G_CALLBACK(delete_window), inst);
    g_signal_connect(G_OBJECT(inst->window), "key_press_event",
                     G_CALLBACK(key_event), inst);
    g_signal_connect(G_OBJECT(inst->window), "key_release_event",
                     G_CALLBACK(key_event), inst);
    g_signal_connect(G_OBJECT(inst->window), "focus_in_event",
                     G_CALLBACK(focus_event), inst);
    g_signal_connect(G_OBJECT(inst->window), "focus_out_event",
                     G_CALLBACK(focus_event), inst);
    g_signal_connect(G_OBJECT(inst->area), "realize",
                     G_CALLBACK(area_realised), inst);
    g_signal_connect(G_OBJECT(inst->area), "size_allocate",
                     G_CALLBACK(area_size_allocate), inst);
    g_signal_connect(G_OBJECT(inst->window), "configure_event",
                     G_CALLBACK(window_configured), inst);
#if GTK_CHECK_VERSION(3,10,0)
    g_signal_connect(G_OBJECT(inst->area), "configure_event",
                     G_CALLBACK(area_configured), inst);
#endif
#if GTK_CHECK_VERSION(3,0,0)
    g_signal_connect(G_OBJECT(inst->area), "draw",
                     G_CALLBACK(draw_area), inst);
#else
    g_signal_connect(G_OBJECT(inst->area), "expose_event",
                     G_CALLBACK(expose_area), inst);
#endif
    g_signal_connect(G_OBJECT(inst->area), "button_press_event",
                     G_CALLBACK(button_event), inst);
    g_signal_connect(G_OBJECT(inst->area), "button_release_event",
                     G_CALLBACK(button_event), inst);
#if GTK_CHECK_VERSION(2,0,0)
    g_signal_connect(G_OBJECT(inst->area), "scroll_event",
                     G_CALLBACK(scroll_event), inst);
#endif
    g_signal_connect(G_OBJECT(inst->area), "motion_notify_event",
                     G_CALLBACK(motion_event), inst);
#if GTK_CHECK_VERSION(2,0,0)
    g_signal_connect(G_OBJECT(inst->imc), "commit",
                     G_CALLBACK(input_method_commit_event), inst);
#endif
    if (conf_get_bool(inst->conf, CONF_scrollbar))
        g_signal_connect(G_OBJECT(inst->sbar_adjust), "value_changed",
                         G_CALLBACK(scrollbar_moved), inst);
    gtk_widget_add_events(GTK_WIDGET(inst->area),
                          GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK |
                          GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                          GDK_POINTER_MOTION_MASK | GDK_BUTTON_MOTION_MASK
#if GTK_CHECK_VERSION(3,4,0)
                          | GDK_SMOOTH_SCROLL_MASK
#endif
        );

    set_window_icon(inst->window, main_icon, n_main_icon);

    gtk_widget_show(inst->window);

    set_window_background(inst);

    /*
     * Set up the Ctrl+rightclick context menu.
     */
    {
        GtkWidget *menuitem;
        char *s;

        inst->menu = gtk_menu_new();

#define MKMENUITEM(title, func) do                                      \
        {                                                               \
            menuitem = gtk_menu_item_new_with_label(title);             \
            gtk_container_add(GTK_CONTAINER(inst->menu), menuitem);     \
            gtk_widget_show(menuitem);                                  \
            g_signal_connect(G_OBJECT(menuitem), "activate",            \
                             G_CALLBACK(func), inst);                   \
        } while (0)

#define MKSUBMENU(title) do                                             \
        {                                                               \
            menuitem = gtk_menu_item_new_with_label(title);             \
            gtk_container_add(GTK_CONTAINER(inst->menu), menuitem);     \
            gtk_widget_show(menuitem);                                  \
        } while (0)

#define MKSEP() do                                                      \
        {                                                               \
            menuitem = gtk_menu_item_new();                             \
            gtk_container_add(GTK_CONTAINER(inst->menu), menuitem);     \
            gtk_widget_show(menuitem);                                  \
        } while (0)

        if (new_session)
            MKMENUITEM("New Session...", new_session_menuitem);
        MKMENUITEM("Restart Session", restart_session_menuitem);
        inst->restartitem = menuitem;
        gtk_widget_set_sensitive(inst->restartitem, false);
        MKMENUITEM("Duplicate Session", dup_session_menuitem);
        if (saved_sessions) {
            inst->sessionsmenu = gtk_menu_new();
            /* sessionsmenu will be updated when it's invoked */
            /* XXX is this the right way to do dynamic menus in Gtk? */
            MKMENUITEM("Saved Sessions", update_savedsess_menu);
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem),
                                      inst->sessionsmenu);
        }
        MKSEP();
        MKMENUITEM("Change Settings...", change_settings_menuitem);
        MKSEP();
        if (use_event_log)
            MKMENUITEM("Event Log", event_log_menuitem);
        MKSUBMENU("Special Commands");
        inst->specialsmenu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), inst->specialsmenu);
        inst->specialsitem1 = menuitem;
        MKSEP();
        inst->specialsitem2 = menuitem;
        gtk_widget_hide(inst->specialsitem1);
        gtk_widget_hide(inst->specialsitem2);
        MKMENUITEM("Clear Scrollback", clear_scrollback_menuitem);
        MKMENUITEM("Reset Terminal", reset_terminal_menuitem);
        MKSEP();
        MKMENUITEM("Copy to " CLIPNAME_EXPLICIT_OBJECT,
                   copy_clipboard_menuitem);
        MKMENUITEM("Paste from " CLIPNAME_EXPLICIT_OBJECT,
                   paste_clipboard_menuitem);
        MKMENUITEM("Copy All", copy_all_menuitem);
        MKSEP();
        s = dupcat("About ", appname);
        MKMENUITEM(s, about_menuitem);
        sfree(s);
#undef MKMENUITEM
#undef MKSUBMENU
#undef MKSEP
    }

    inst->textcursor = make_mouse_ptr(inst, GDK_XTERM);
    inst->rawcursor = make_mouse_ptr(inst, GDK_LEFT_PTR);
    inst->waitcursor = make_mouse_ptr(inst, GDK_WATCH);
    inst->blankcursor = make_mouse_ptr(inst, -1);
    inst->currcursor = inst->textcursor;
    show_mouseptr(inst, true);

    inst->eventlogstuff = eventlogstuff_new();

    inst->term = term_init(inst->conf, &inst->ucsdata, &inst->termwin);
    setup_clipboards(inst, inst->term, inst->conf);
    inst->logctx = log_init(&inst->logpolicy, inst->conf);
    term_provide_logctx(inst->term, inst->logctx);

    term_size(inst->term, inst->height, inst->width,
              conf_get_int(inst->conf, CONF_savelines));

#if GTK_CHECK_VERSION(2,0,0)
    /* Delay this signal connection until after inst->term exists */
    g_signal_connect(G_OBJECT(inst->window), "window_state_event",
                     G_CALLBACK(window_state_event), inst);
#endif

    inst->exited = false;

    start_backend(inst);

    if (inst->ldisc) /* early backend failure might make this NULL already */
        ldisc_echoedit_update(inst->ldisc); /* cause ldisc to notice changes */
}

static void gtk_seat_set_trust_status(Seat *seat, bool trusted)
{
    GtkFrontend *inst = container_of(seat, GtkFrontend, seat);
    term_set_trust_status(inst->term, trusted);
}

static bool gtk_seat_can_set_trust_status(Seat *seat)
{
    return true;
}

static bool gtk_seat_get_cursor_position(Seat *seat, int *x, int *y)
{
    GtkFrontend *inst = container_of(seat, GtkFrontend, seat);
    if (inst->term) {
        term_get_cursor_position(inst->term, x, y);
        return true;
    }
    return false;
}
