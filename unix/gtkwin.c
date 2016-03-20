/*
 * gtkwin.c: the main code that runs a PuTTY terminal emulator and
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

#define PUTTY_DO_GLOBALS	       /* actually _define_ globals */

#define MAY_REFER_TO_GTK_IN_HEADERS

#include "putty.h"
#include "terminal.h"
#include "gtkcompat.h"
#include "gtkfont.h"
#include "gtkmisc.h"

#ifndef NOT_X_WINDOWS
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#endif

#include "x11misc.h"

#define CAT2(x,y) x ## y
#define CAT(x,y) CAT2(x,y)
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

/* Colours come in two flavours: configurable, and xterm-extended. */
#define NEXTCOLOURS 240 /* 216 colour-cube plus 24 shades of grey */
#define NALLCOLOURS (NCFGCOLOURS + NEXTCOLOURS)

GdkAtom compound_text_atom, utf8_string_atom;

extern char **pty_argv;	       /* declared in pty.c */
extern int use_pty_argv;

/*
 * Timers are global across all sessions (even if we were handling
 * multiple sessions, which we aren't), so the current timer ID is
 * a global variable.
 */
static guint timer_id = 0;

struct clipboard_data_instance;

struct gui_data {
    GtkWidget *window, *area, *sbar;
    GtkBox *hbox;
    GtkAdjustment *sbar_adjust;
    GtkWidget *menu, *specialsmenu, *specialsitem1, *specialsitem2,
	*restartitem;
    GtkWidget *sessionsmenu;
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
#if GTK_CHECK_VERSION(2,0,0)
    GtkIMContext *imc;
#endif
    unifont *fonts[4];                 /* normal, bold, wide, widebold */
#if GTK_CHECK_VERSION(2,0,0)
    const char *geometry;
#else
    int xpos, ypos, gotpos, gravity;
#endif
    GdkCursor *rawcursor, *textcursor, *blankcursor, *waitcursor, *currcursor;
    GdkColor cols[NALLCOLOURS];
#if !GTK_CHECK_VERSION(3,0,0)
    GdkColormap *colmap;
#endif
    int direct_to_font;
    wchar_t *pastein_data;
    int pastein_data_len;
#ifdef JUST_USE_GTK_CLIPBOARD_UTF8
    GtkClipboard *clipboard;
    struct clipboard_data_instance *current_cdi;
#else
    char *pasteout_data, *pasteout_data_ctext, *pasteout_data_utf8;
    int pasteout_data_len, pasteout_data_ctext_len, pasteout_data_utf8_len;
#endif
    int font_width, font_height;
    int width, height;
    int ignore_sbar;
    int mouseptr_visible;
    int busy_status;
    guint toplevel_callback_idle_id;
    int idle_fn_scheduled, quit_fn_scheduled;
    int alt_keycode;
    int alt_digits;
    char *wintitle;
    char *icontitle;
    int master_fd, master_func_id;
    void *ldisc;
    Backend *back;
    void *backhandle;
    Terminal *term;
    void *logctx;
    int exited;
    struct unicode_data ucsdata;
    Conf *conf;
    void *eventlogstuff;
    char *progname, **gtkargvstart;
    int ngtkargs;
    guint32 input_event_time; /* Timestamp of the most recent input event. */
    int reconfiguring;
#if GTK_CHECK_VERSION(3,4,0)
    gdouble cumulative_scroll;
#endif
    /* Cached things out of conf that we refer to a lot */
    int bold_style;
    int window_border;
    int cursor_type;
    int drawtype;
    int meta_mod_mask;
};

static void cache_conf_values(struct gui_data *inst)
{
    inst->bold_style = conf_get_int(inst->conf, CONF_bold_style);
    inst->window_border = conf_get_int(inst->conf, CONF_window_border);
    inst->cursor_type = conf_get_int(inst->conf, CONF_cursor_type);
#ifdef OSX_META_KEY_CONFIG
    inst->meta_mod_mask = 0;
    if (conf_get_int(inst->conf, CONF_osx_option_meta))
        inst->meta_mod_mask |= GDK_MOD1_MASK;
    if (conf_get_int(inst->conf, CONF_osx_command_meta))
        inst->meta_mod_mask |= GDK_MOD2_MASK;
#else
    inst->meta_mod_mask = GDK_MOD1_MASK;
#endif
}

struct draw_ctx {
    struct gui_data *inst;
    unifont_drawctx uctx;
};

static int send_raw_mouse;

static const char *app_name = "pterm";

static void start_backend(struct gui_data *inst);
static void exit_callback(void *vinst);

char *x_get_default(const char *key)
{
#ifndef NOT_X_WINDOWS
    return XGetDefault(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()),
                       app_name, key);
#else
    return NULL;
#endif
}

void connection_fatal(void *frontend, const char *p, ...)
{
    struct gui_data *inst = (struct gui_data *)frontend;

    va_list ap;
    char *msg;
    va_start(ap, p);
    msg = dupvprintf(p, ap);
    va_end(ap);
    fatal_message_box(inst->window, msg);
    sfree(msg);

    queue_toplevel_callback(exit_callback, inst);
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

int platform_default_i(const char *name, int def)
{
    if (!strcmp(name, "CloseOnExit"))
	return 2;  /* maps to FORCE_ON after painful rearrangement :-( */
    if (!strcmp(name, "WinNameAlways"))
	return 0;  /* X natively supports icon titles, so use 'em by default */
    return def;
}

/* Dummy routine, only required in plink. */
void frontend_echoedit_update(void *frontend, int echo, int edit)
{
}

char *get_ttymode(void *frontend, const char *mode)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    return term_get_ttymode(inst->term, mode);
}

int from_backend(void *frontend, int is_stderr, const char *data, int len)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    return term_data(inst->term, is_stderr, data, len);
}

int from_backend_untrusted(void *frontend, const char *data, int len)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    return term_data_untrusted(inst->term, data, len);
}

int from_backend_eof(void *frontend)
{
    return TRUE;   /* do respond to incoming EOF with outgoing */
}

int get_userpass_input(prompts_t *p, const unsigned char *in, int inlen)
{
    struct gui_data *inst = (struct gui_data *)p->frontend;
    int ret;
    ret = cmdline_get_passwd_input(p, in, inlen);
    if (ret == -1)
	ret = term_get_userpass_input(inst->term, p, in, inlen);
    return ret;
}

void logevent(void *frontend, const char *string)
{
    struct gui_data *inst = (struct gui_data *)frontend;

    log_eventlog(inst->logctx, string);

    logevent_dlg(inst->eventlogstuff, string);
}

int font_dimension(void *frontend, int which)/* 0 for width, 1 for height */
{
    struct gui_data *inst = (struct gui_data *)frontend;

    if (which)
	return inst->font_height;
    else
	return inst->font_width;
}

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
    /* struct gui_data *inst = (struct gui_data *)frontend; */

    if (button == MBT_LEFT)
	return MBT_SELECT;
    if (button == MBT_MIDDLE)
	return MBT_PASTE;
    if (button == MBT_RIGHT)
	return MBT_EXTEND;
    return 0;			       /* shouldn't happen */
}

/*
 * Return the top-level GtkWindow associated with a particular
 * front end instance.
 */
void *get_window(void *frontend)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    return inst->window;
}

/*
 * Minimise or restore the window in response to a server-side
 * request.
 */
void set_iconic(void *frontend, int iconic)
{
    /*
     * GTK 1.2 doesn't know how to do this.
     */
#if GTK_CHECK_VERSION(2,0,0)
    struct gui_data *inst = (struct gui_data *)frontend;
    if (iconic)
	gtk_window_iconify(GTK_WINDOW(inst->window));
    else
	gtk_window_deiconify(GTK_WINDOW(inst->window));
#endif
}

/*
 * Move the window in response to a server-side request.
 */
void move_window(void *frontend, int x, int y)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    /*
     * I assume that when the GTK version of this call is available
     * we should use it. Not sure how it differs from the GDK one,
     * though.
     */
#if GTK_CHECK_VERSION(2,0,0)
    gtk_window_move(GTK_WINDOW(inst->window), x, y);
#else
    gdk_window_move(gtk_widget_get_window(inst->window), x, y);
#endif
}

/*
 * Move the window to the top or bottom of the z-order in response
 * to a server-side request.
 */
void set_zorder(void *frontend, int top)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    if (top)
	gdk_window_raise(gtk_widget_get_window(inst->window));
    else
	gdk_window_lower(gtk_widget_get_window(inst->window));
}

/*
 * Refresh the window in response to a server-side request.
 */
void refresh_window(void *frontend)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    term_invalidate(inst->term);
}

/*
 * Maximise or restore the window in response to a server-side
 * request.
 */
void set_zoomed(void *frontend, int zoomed)
{
    /*
     * GTK 1.2 doesn't know how to do this.
     */
#if GTK_CHECK_VERSION(2,0,0)
    struct gui_data *inst = (struct gui_data *)frontend;
    if (zoomed)
	gtk_window_maximize(GTK_WINDOW(inst->window));
    else
	gtk_window_unmaximize(GTK_WINDOW(inst->window));
#endif
}

/*
 * Report whether the window is iconic, for terminal reports.
 */
int is_iconic(void *frontend)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    return !gdk_window_is_viewable(gtk_widget_get_window(inst->window));
}

/*
 * Report the window's position, for terminal reports.
 */
void get_window_pos(void *frontend, int *x, int *y)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    /*
     * I assume that when the GTK version of this call is available
     * we should use it. Not sure how it differs from the GDK one,
     * though.
     */
#if GTK_CHECK_VERSION(2,0,0)
    gtk_window_get_position(GTK_WINDOW(inst->window), x, y);
#else
    gdk_window_get_position(gtk_widget_get_window(inst->window), x, y);
#endif
}

/*
 * Report the window's pixel size, for terminal reports.
 */
void get_window_pixels(void *frontend, int *x, int *y)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    /*
     * I assume that when the GTK version of this call is available
     * we should use it. Not sure how it differs from the GDK one,
     * though.
     */
#if GTK_CHECK_VERSION(2,0,0)
    gtk_window_get_size(GTK_WINDOW(inst->window), x, y);
#else
    gdk_window_get_size(gtk_widget_get_window(inst->window), x, y);
#endif
}

/*
 * Return the window or icon title.
 */
char *get_window_title(void *frontend, int icon)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    return icon ? inst->icontitle : inst->wintitle;
}

gint delete_window(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    if (!inst->exited && conf_get_int(inst->conf, CONF_warn_on_close)) {
	if (!reallyclose(inst))
	    return TRUE;
    }
    return FALSE;
}

static void update_mouseptr(struct gui_data *inst)
{
    switch (inst->busy_status) {
      case BUSY_NOT:
	if (!inst->mouseptr_visible) {
	    gdk_window_set_cursor(gtk_widget_get_window(inst->area),
                                  inst->blankcursor);
	} else if (send_raw_mouse) {
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
	assert(0);
    }
}

static void show_mouseptr(struct gui_data *inst, int show)
{
    if (!conf_get_int(inst->conf, CONF_hide_mouseptr))
	show = 1;
    inst->mouseptr_visible = show;
    update_mouseptr(inst);
}

static void draw_backing_rect(struct gui_data *inst);

gint configure_area(GtkWidget *widget, GdkEventConfigure *event, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    int w, h, need_size = 0;

    /*
     * See if the terminal size has changed, in which case we must
     * let the terminal know.
     */
    w = (event->width - 2*inst->window_border) / inst->font_width;
    h = (event->height - 2*inst->window_border) / inst->font_height;
    if (w != inst->width || h != inst->height) {
	inst->width = w;
	inst->height = h;
	conf_set_int(inst->conf, CONF_width, inst->width);
	conf_set_int(inst->conf, CONF_height, inst->height);
	need_size = 1;
    }

    {
        int backing_w = w * inst->font_width + 2*inst->window_border;
        int backing_h = h * inst->font_height + 2*inst->window_border;

#ifndef NO_BACKING_PIXMAPS
        if (inst->pixmap) {
            gdk_pixmap_unref(inst->pixmap);
            inst->pixmap = NULL;
        }

        inst->pixmap = gdk_pixmap_new(gtk_widget_get_window(widget),
                                      backing_w, backing_h, -1);
#endif

#ifdef DRAW_TEXT_CAIRO
        if (inst->surface) {
            cairo_surface_destroy(inst->surface);
            inst->surface = NULL;
        }

        inst->surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
                                                   backing_w, backing_h);
#endif
    }

    draw_backing_rect(inst);

    if (need_size && inst->term) {
	term_size(inst->term, h, w, conf_get_int(inst->conf, CONF_savelines));
    }

    if (inst->term)
	term_invalidate(inst->term);

#if GTK_CHECK_VERSION(2,0,0)
    gtk_im_context_set_client_window(inst->imc, gtk_widget_get_window(widget));
#endif

    return TRUE;
}

#ifdef DRAW_TEXT_CAIRO
static void cairo_setup_dctx(struct draw_ctx *dctx)
{
    cairo_get_matrix(dctx->uctx.u.cairo.cr,
                     &dctx->uctx.u.cairo.origmatrix);
    cairo_set_line_width(dctx->uctx.u.cairo.cr, 1.0);
    cairo_set_line_cap(dctx->uctx.u.cairo.cr, CAIRO_LINE_CAP_SQUARE);
    cairo_set_line_join(dctx->uctx.u.cairo.cr, CAIRO_LINE_JOIN_MITER);
    /* This antialiasing setting appears to be ignored for Pango
     * font rendering but honoured for stroking and filling paths;
     * I don't quite understand the logic of that, but I won't
     * complain since it's exactly what I happen to want */
    cairo_set_antialias(dctx->uctx.u.cairo.cr, CAIRO_ANTIALIAS_NONE);
}
#endif

#if GTK_CHECK_VERSION(3,0,0)
static gint draw_area(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;

    /*
     * GTK3 window redraw: we always expect Cairo to be enabled, so
     * that inst->surface exists, and pixmaps to be disabled, so that
     * inst->pixmap does not exist. Hence, we just blit from
     * inst->surface to the window.
     */
    if (inst->surface) {
        GdkRectangle dirtyrect;

        gdk_cairo_get_clip_rectangle(cr, &dirtyrect);

        cairo_set_source_surface(cr, inst->surface, 0, 0);
        cairo_rectangle(cr, dirtyrect.x, dirtyrect.y,
                        dirtyrect.width, dirtyrect.height);
        cairo_fill(cr);
    }

    return TRUE;
}
#else
gint expose_area(GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;

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

    return TRUE;
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

gint key_event(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    char output[256];
    wchar_t ucsoutput[2];
    int ucsval, start, end, special, output_charset, use_ucsoutput;
    int nethack_mode, app_keypad_mode;

    /* Remember the timestamp. */
    inst->input_event_time = event->time;

    /* By default, nothing is generated. */
    end = start = 0;
    special = use_ucsoutput = FALSE;
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
                                          mod_bits[i].name,
                                          (char *)NULL);
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
            for (i = 0; event->string[i]; i++) {
                char *old = string_string;
                string_string = dupprintf("%s%s%02x", string_string,
                                          string_string[0] ? " " : "",
                                          (unsigned)event->string[i] & 0xFF);
                sfree(old);
            }
        }

        debug(("key_event: type=%s keyval=%s state=%s "
               "hardware_keycode=%d is_modifier=%s string=[%s]\n",
               type_string, keyval_string, state_string,
               (int)event->hardware_keycode,
               event->is_modifier ? "TRUE" : "FALSE",
               string_string));

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
            debug((" - modifier release terminates Alt+numberpad input, "
                   "keycode = %d\n", inst->alt_keycode));
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
        debug((" - key release, passing to IM\n"));
#endif
        if (gtk_im_context_filter_keypress(inst->imc, event)) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug((" - key release accepted by IM\n"));
#endif
            return TRUE;
        } else {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug((" - key release not accepted by IM\n"));
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
            debug((" - modifier press potentially begins Alt+numberpad "
                   "input\n"));
#endif
	    inst->alt_keycode = -1;
            inst->alt_digits = 0;
	    goto done;		       /* this generates nothing else */
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
#ifdef KEY_EVENT_DIAGNOSTICS
                int old_keycode = inst->alt_keycode;
#endif
		if (inst->alt_keycode == -1)
		    inst->alt_keycode = digit;   /* one-digit code */
		else
		    inst->alt_keycode = inst->alt_keycode * 10 + digit;
                inst->alt_digits++;
#ifdef KEY_EVENT_DIAGNOSTICS
                debug((" - Alt+numberpad digit %d added to keycode %d"
                       " gives %d\n", digit, old_keycode, inst->alt_keycode));
#endif
		/* Having used this digit, we now do nothing more with it. */
		goto done;
	    }
	}

	/*
	 * Shift-PgUp and Shift-PgDn don't even generate keystrokes
	 * at all.
	 */
	if (event->keyval == GDK_KEY_Page_Up &&
            (event->state & GDK_SHIFT_MASK)) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug((" - Shift-PgUp scroll\n"));
#endif
	    term_scroll(inst->term, 0, -inst->height/2);
	    return TRUE;
	}
	if (event->keyval == GDK_KEY_Page_Up &&
            (event->state & GDK_CONTROL_MASK)) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug((" - Ctrl-PgUp scroll\n"));
#endif
	    term_scroll(inst->term, 0, -1);
	    return TRUE;
	}
	if (event->keyval == GDK_KEY_Page_Down &&
            (event->state & GDK_SHIFT_MASK)) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug((" - Shift-PgDn scroll\n"));
#endif
	    term_scroll(inst->term, 0, +inst->height/2);
	    return TRUE;
	}
	if (event->keyval == GDK_KEY_Page_Down &&
            (event->state & GDK_CONTROL_MASK)) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug((" - Ctrl-PgDn scroll\n"));
#endif
	    term_scroll(inst->term, 0, +1);
	    return TRUE;
	}

	/*
	 * Neither does Shift-Ins.
	 */
	if (event->keyval == GDK_KEY_Insert &&
            (event->state & GDK_SHIFT_MASK)) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug((" - Shift-Insert paste\n"));
#endif
	    request_paste(inst);
	    return TRUE;
	}

	special = FALSE;
	use_ucsoutput = FALSE;

        nethack_mode = conf_get_int(inst->conf, CONF_nethack_keypad);
        app_keypad_mode = (inst->term->app_keypad_keys &&
                           !conf_get_int(inst->conf, CONF_no_applic_k));

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
	strncpy(output+1, event->string, lenof(output)-1);
#else /* !GTK_CHECK_VERSION(2,0,0) */
        /*
         * Most things can now be passed to
         * gtk_im_context_filter_keypress without breaking anything
         * below this point. An exception is the numeric keypad if
         * we're in Nethack or application mode: the IM will eat
         * numeric keypad presses if Num Lock is on, but we don't want
         * it to.
         */
	if (app_keypad_mode &&
            (event->keyval == GDK_KEY_Num_Lock ||
             event->keyval == GDK_KEY_KP_Divide ||
             event->keyval == GDK_KEY_KP_Multiply ||
             event->keyval == GDK_KEY_KP_Subtract ||
             event->keyval == GDK_KEY_KP_Add ||
             event->keyval == GDK_KEY_KP_Enter ||
             event->keyval == GDK_KEY_KP_0 ||
             event->keyval == GDK_KEY_KP_Insert ||
             event->keyval == GDK_KEY_KP_1 ||
             event->keyval == GDK_KEY_KP_End ||
             event->keyval == GDK_KEY_KP_2 ||
             event->keyval == GDK_KEY_KP_Down ||
             event->keyval == GDK_KEY_KP_3 ||
             event->keyval == GDK_KEY_KP_Page_Down ||
             event->keyval == GDK_KEY_KP_4 ||
             event->keyval == GDK_KEY_KP_Left ||
             event->keyval == GDK_KEY_KP_5 ||
             event->keyval == GDK_KEY_KP_Begin ||
             event->keyval == GDK_KEY_KP_6 ||
             event->keyval == GDK_KEY_KP_Right ||
             event->keyval == GDK_KEY_KP_7 ||
             event->keyval == GDK_KEY_KP_Home ||
             event->keyval == GDK_KEY_KP_8 ||
             event->keyval == GDK_KEY_KP_Up ||
             event->keyval == GDK_KEY_KP_9 ||
             event->keyval == GDK_KEY_KP_Page_Up ||
             event->keyval == GDK_KEY_KP_Decimal ||
             event->keyval == GDK_KEY_KP_Delete)) {
            /* app keypad; do nothing */
        } else if (nethack_mode &&
                   (event->keyval == GDK_KEY_KP_1 ||
                    event->keyval == GDK_KEY_KP_End ||
                    event->keyval == GDK_KEY_KP_2 ||
                    event->keyval == GDK_KEY_KP_Down ||
                    event->keyval == GDK_KEY_KP_3 ||
                    event->keyval == GDK_KEY_KP_Page_Down ||
                    event->keyval == GDK_KEY_KP_4 ||
                    event->keyval == GDK_KEY_KP_Left ||
                    event->keyval == GDK_KEY_KP_5 ||
                    event->keyval == GDK_KEY_KP_Begin ||
                    event->keyval == GDK_KEY_KP_6 ||
                    event->keyval == GDK_KEY_KP_Right ||
                    event->keyval == GDK_KEY_KP_7 ||
                    event->keyval == GDK_KEY_KP_Home ||
                    event->keyval == GDK_KEY_KP_8 ||
                    event->keyval == GDK_KEY_KP_Up ||
                    event->keyval == GDK_KEY_KP_9 ||
                    event->keyval == GDK_KEY_KP_Page_Up)) {
            /* nethack mode; do nothing */
        } else {
            int try_filter = TRUE;

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
                debug((" - Meta modifier requiring manual intervention, "
                       "suppressing IM filtering\n"));
#endif
                try_filter = FALSE;
            }
#endif

            if (try_filter) {
#ifdef KEY_EVENT_DIAGNOSTICS
                debug((" - general key press, passing to IM\n"));
#endif
                if (gtk_im_context_filter_keypress(inst->imc, event)) {
#ifdef KEY_EVENT_DIAGNOSTICS
                    debug((" - key press accepted by IM\n"));
#endif
                    return TRUE;
                } else {
#ifdef KEY_EVENT_DIAGNOSTICS
                    debug((" - key press not accepted by IM\n"));
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
			    event->string, strlen(event->string),
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
                debug((" - string translated into Unicode = [%s]\n",
                       string_string));
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
                debug((" - string translated into UTF-8 = [%s]\n",
                       string_string));
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
            debug((" - keysym_to_unicode gave %04x\n",
                   (unsigned)ucsoutput[1]));
#endif
	    use_ucsoutput = TRUE;
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
                        debug((" - retranslation for manual Meta: "
                               "new keyval = %s, Unicode = %04x\n",
                               keyval_name, (unsigned)ucsoutput[1]));
                        sfree(keyval_name);
                    }
#endif
                    use_ucsoutput = TRUE;
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
            debug((" - Ctrl-` special case, translating as 1c\n"));
#endif
	    output[1] = '\x1C';
	    use_ucsoutput = FALSE;
	    end = 2;
	}

        /* Some GTK backends (e.g. Quartz) do not change event->string
         * in response to the Control modifier. So we do it ourselves
         * here, if it's not already happened.
         *
         * The translations below are in line with X11 policy as far
         * as I know. */
        if ((event->state & GDK_CONTROL_MASK) && end == 2) {
#ifdef KEY_EVENT_DIAGNOSTICS
            int orig = output[1];
#endif

            if (output[1] >= '3' && output[1] <= '7') {
                /* ^3,...,^7 map to 0x1B,...,0x1F */
                output[1] += '\x1B' - '3';
            } else if (output[1] == '2' || output[1] == ' ') {
                /* ^2 and ^Space are both ^@, i.e. \0 */
                output[1] = '\0';
            } else if (output[1] == '8') {
                /* ^8 is DEL */
                output[1] = '\x7F';
            } else if (output[1] == '/') {
                /* ^/ is the same as ^_ */
                output[1] = '\x1F';
            } else if (output[1] >= 0x40 && output[1] < 0x7F) {
                /* Everything anywhere near the alphabetics just gets
                 * masked. */
                output[1] &= 0x1F;
            }
            /* Anything else, e.g. '0', is unchanged. */

#ifdef KEY_EVENT_DIAGNOSTICS
            if (orig == output[1])
                debug((" - manual Ctrl key handling did nothing\n"));
            else
                debug((" - manual Ctrl key handling: %02x -> %02x\n",
                       (unsigned)orig, (unsigned)output[1]));
#endif
        }

	/* Control-Break sends a Break special to the backend */
	if (event->keyval == GDK_KEY_Break &&
	    (event->state & GDK_CONTROL_MASK)) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug((" - Ctrl-Break special case, sending TS_BRK\n"));
#endif
	    if (inst->back)
		inst->back->special(inst->backhandle, TS_BRK);
	    return TRUE;
	}

	/* We handle Return ourselves, because it needs to be flagged as
	 * special to ldisc. */
	if (event->keyval == GDK_KEY_Return) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug((" - Return special case, translating as 0d + special\n"));
#endif
	    output[1] = '\015';
	    use_ucsoutput = FALSE;
	    end = 2;
	    special = TRUE;
	}

	/* Control-2, Control-Space and Control-@ are NUL */
	if (!output[1] &&
	    (event->keyval == ' ' || event->keyval == '2' ||
	     event->keyval == '@') &&
	    (event->state & (GDK_SHIFT_MASK |
			     GDK_CONTROL_MASK)) == GDK_CONTROL_MASK) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug((" - Ctrl-{space,2,@} special case, translating as 00\n"));
#endif
	    output[1] = '\0';
	    use_ucsoutput = FALSE;
	    end = 2;
	}

	/* Control-Shift-Space is 160 (ISO8859 nonbreaking space) */
	if (!output[1] && event->keyval == ' ' &&
	    (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK)) ==
	    (GDK_SHIFT_MASK | GDK_CONTROL_MASK)) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug((" - Ctrl-Shift-space special case, translating as 00a0\n"));
#endif
	    output[1] = '\240';
	    output_charset = CS_ISO8859_1;
	    use_ucsoutput = FALSE;
	    end = 2;
	}

	/* We don't let GTK tell us what Backspace is! We know better. */
	if (event->keyval == GDK_KEY_BackSpace &&
	    !(event->state & GDK_SHIFT_MASK)) {
	    output[1] = conf_get_int(inst->conf, CONF_bksp_is_delete) ?
		'\x7F' : '\x08';
#ifdef KEY_EVENT_DIAGNOSTICS
            debug((" - Backspace, translating as %02x\n",
                   (unsigned)output[1]));
#endif
	    use_ucsoutput = FALSE;
	    end = 2;
	    special = TRUE;
	}
	/* For Shift Backspace, do opposite of what is configured. */
	if (event->keyval == GDK_KEY_BackSpace &&
	    (event->state & GDK_SHIFT_MASK)) {
	    output[1] = conf_get_int(inst->conf, CONF_bksp_is_delete) ?
		'\x08' : '\x7F';
#ifdef KEY_EVENT_DIAGNOSTICS
            debug((" - Shift-Backspace, translating as %02x\n",
                   (unsigned)output[1]));
#endif
	    use_ucsoutput = FALSE;
	    end = 2;
	    special = TRUE;
	}

	/* Shift-Tab is ESC [ Z */
	if (event->keyval == GDK_KEY_ISO_Left_Tab ||
	    (event->keyval == GDK_KEY_Tab &&
             (event->state & GDK_SHIFT_MASK))) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug((" - Shift-Tab, translating as ESC [ Z\n"));
#endif
	    end = 1 + sprintf(output+1, "\033[Z");
	    use_ucsoutput = FALSE;
	}
	/* And normal Tab is Tab, if the keymap hasn't already told us.
	 * (Curiously, at least one version of the MacOS 10.5 X server
	 * doesn't translate Tab for us. */
	if (event->keyval == GDK_KEY_Tab && end <= 1) {
#ifdef KEY_EVENT_DIAGNOSTICS
            debug((" - Tab, translating as 09\n"));
#endif
	    output[1] = '\t';
	    end = 2;
	}

	/*
	 * NetHack keypad mode.
	 */
	if (nethack_mode) {
	    const char *keys = NULL;
	    switch (event->keyval) {
	      case GDK_KEY_KP_1: case GDK_KEY_KP_End:
                keys = "bB\002"; break;
	      case GDK_KEY_KP_2: case GDK_KEY_KP_Down:
                keys = "jJ\012"; break;
	      case GDK_KEY_KP_3: case GDK_KEY_KP_Page_Down:
                keys = "nN\016"; break;
	      case GDK_KEY_KP_4: case GDK_KEY_KP_Left:
                keys = "hH\010"; break;
	      case GDK_KEY_KP_5: case GDK_KEY_KP_Begin:
                keys = "..."; break;
	      case GDK_KEY_KP_6: case GDK_KEY_KP_Right:
                keys = "lL\014"; break;
	      case GDK_KEY_KP_7: case GDK_KEY_KP_Home:
                keys = "yY\031"; break;
	      case GDK_KEY_KP_8: case GDK_KEY_KP_Up:
                keys = "kK\013"; break;
	      case GDK_KEY_KP_9: case GDK_KEY_KP_Page_Up:
                keys = "uU\025"; break;
	    }
	    if (keys) {
		end = 2;
		if (event->state & GDK_CONTROL_MASK)
		    output[1] = keys[2];
		else if (event->state & GDK_SHIFT_MASK)
		    output[1] = keys[1];
		else
		    output[1] = keys[0];
#ifdef KEY_EVENT_DIAGNOSTICS
                debug((" - Nethack-mode key"));
#endif
		use_ucsoutput = FALSE;
		goto done;
	    }
	}

	/*
	 * Application keypad mode.
	 */
	if (app_keypad_mode) {
	    int xkey = 0;
	    switch (event->keyval) {
	      case GDK_KEY_Num_Lock: xkey = 'P'; break;
	      case GDK_KEY_KP_Divide: xkey = 'Q'; break;
	      case GDK_KEY_KP_Multiply: xkey = 'R'; break;
	      case GDK_KEY_KP_Subtract: xkey = 'S'; break;
		/*
		 * Keypad + is tricky. It covers a space that would
		 * be taken up on the VT100 by _two_ keys; so we
		 * let Shift select between the two. Worse still,
		 * in xterm function key mode we change which two...
		 */
	      case GDK_KEY_KP_Add:
		if (conf_get_int(inst->conf, CONF_funky_type) == FUNKY_XTERM) {
		    if (event->state & GDK_SHIFT_MASK)
			xkey = 'l';
		    else
			xkey = 'k';
		} else if (event->state & GDK_SHIFT_MASK)
			xkey = 'm';
		else
		    xkey = 'l';
		break;
	      case GDK_KEY_KP_Enter: xkey = 'M'; break;
	      case GDK_KEY_KP_0: case GDK_KEY_KP_Insert: xkey = 'p'; break;
	      case GDK_KEY_KP_1: case GDK_KEY_KP_End: xkey = 'q'; break;
	      case GDK_KEY_KP_2: case GDK_KEY_KP_Down: xkey = 'r'; break;
	      case GDK_KEY_KP_3: case GDK_KEY_KP_Page_Down: xkey = 's'; break;
	      case GDK_KEY_KP_4: case GDK_KEY_KP_Left: xkey = 't'; break;
	      case GDK_KEY_KP_5: case GDK_KEY_KP_Begin: xkey = 'u'; break;
	      case GDK_KEY_KP_6: case GDK_KEY_KP_Right: xkey = 'v'; break;
	      case GDK_KEY_KP_7: case GDK_KEY_KP_Home: xkey = 'w'; break;
	      case GDK_KEY_KP_8: case GDK_KEY_KP_Up: xkey = 'x'; break;
	      case GDK_KEY_KP_9: case GDK_KEY_KP_Page_Up: xkey = 'y'; break;
	      case GDK_KEY_KP_Decimal: case GDK_KEY_KP_Delete:
                xkey = 'n'; break;
	    }
	    if (xkey) {
		if (inst->term->vt52_mode) {
		    if (xkey >= 'P' && xkey <= 'S')
			end = 1 + sprintf(output+1, "\033%c", xkey);
		    else
			end = 1 + sprintf(output+1, "\033?%c", xkey);
		} else
		    end = 1 + sprintf(output+1, "\033O%c", xkey);
		use_ucsoutput = FALSE;
#ifdef KEY_EVENT_DIAGNOSTICS
                debug((" - Application keypad mode key"));
#endif
		goto done;
	    }
	}

	/*
	 * Next, all the keys that do tilde codes. (ESC '[' nn '~',
	 * for integer decimal nn.)
	 *
	 * We also deal with the weird ones here. Linux VCs replace F1
	 * to F5 by ESC [ [ A to ESC [ [ E. rxvt doesn't do _that_, but
	 * does replace Home and End (1~ and 4~) by ESC [ H and ESC O w
	 * respectively.
	 */
	{
	    int code = 0;
	    int funky_type = conf_get_int(inst->conf, CONF_funky_type);
	    switch (event->keyval) {
	      case GDK_KEY_F1:
		code = (event->state & GDK_SHIFT_MASK ? 23 : 11);
		break;
	      case GDK_KEY_F2:
		code = (event->state & GDK_SHIFT_MASK ? 24 : 12);
		break;
	      case GDK_KEY_F3:
		code = (event->state & GDK_SHIFT_MASK ? 25 : 13);
		break;
	      case GDK_KEY_F4:
		code = (event->state & GDK_SHIFT_MASK ? 26 : 14);
		break;
	      case GDK_KEY_F5:
		code = (event->state & GDK_SHIFT_MASK ? 28 : 15);
		break;
	      case GDK_KEY_F6:
		code = (event->state & GDK_SHIFT_MASK ? 29 : 17);
		break;
	      case GDK_KEY_F7:
		code = (event->state & GDK_SHIFT_MASK ? 31 : 18);
		break;
	      case GDK_KEY_F8:
		code = (event->state & GDK_SHIFT_MASK ? 32 : 19);
		break;
	      case GDK_KEY_F9:
		code = (event->state & GDK_SHIFT_MASK ? 33 : 20);
		break;
	      case GDK_KEY_F10:
		code = (event->state & GDK_SHIFT_MASK ? 34 : 21);
		break;
	      case GDK_KEY_F11:
		code = 23;
		break;
	      case GDK_KEY_F12:
		code = 24;
		break;
	      case GDK_KEY_F13:
		code = 25;
		break;
	      case GDK_KEY_F14:
		code = 26;
		break;
	      case GDK_KEY_F15:
		code = 28;
		break;
	      case GDK_KEY_F16:
		code = 29;
		break;
	      case GDK_KEY_F17:
		code = 31;
		break;
	      case GDK_KEY_F18:
		code = 32;
		break;
	      case GDK_KEY_F19:
		code = 33;
		break;
	      case GDK_KEY_F20:
		code = 34;
		break;
	    }
	    if (!(event->state & GDK_CONTROL_MASK)) switch (event->keyval) {
	      case GDK_KEY_Home: case GDK_KEY_KP_Home:
		code = 1;
		break;
	      case GDK_KEY_Insert: case GDK_KEY_KP_Insert:
		code = 2;
		break;
	      case GDK_KEY_Delete: case GDK_KEY_KP_Delete:
		code = 3;
		break;
	      case GDK_KEY_End: case GDK_KEY_KP_End:
		code = 4;
		break;
	      case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up:
		code = 5;
		break;
	      case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down:
		code = 6;
		break;
	    }
	    /* Reorder edit keys to physical order */
	    if (funky_type == FUNKY_VT400 && code <= 6)
		code = "\0\2\1\4\5\3\6"[code];

	    if (inst->term->vt52_mode && code > 0 && code <= 6) {
		end = 1 + sprintf(output+1, "\x1B%c", " HLMEIG"[code]);
#ifdef KEY_EVENT_DIAGNOSTICS
                debug((" - VT52 mode small keypad key"));
#endif
		use_ucsoutput = FALSE;
		goto done;
	    }

	    if (funky_type == FUNKY_SCO &&     /* SCO function keys */
		code >= 11 && code <= 34) {
		char codes[] = "MNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz@[\\]^_`{";
		int index = 0;
		switch (event->keyval) {
		  case GDK_KEY_F1: index = 0; break;
		  case GDK_KEY_F2: index = 1; break;
		  case GDK_KEY_F3: index = 2; break;
		  case GDK_KEY_F4: index = 3; break;
		  case GDK_KEY_F5: index = 4; break;
		  case GDK_KEY_F6: index = 5; break;
		  case GDK_KEY_F7: index = 6; break;
		  case GDK_KEY_F8: index = 7; break;
		  case GDK_KEY_F9: index = 8; break;
		  case GDK_KEY_F10: index = 9; break;
		  case GDK_KEY_F11: index = 10; break;
		  case GDK_KEY_F12: index = 11; break;
		}
		if (event->state & GDK_SHIFT_MASK) index += 12;
		if (event->state & GDK_CONTROL_MASK) index += 24;
		end = 1 + sprintf(output+1, "\x1B[%c", codes[index]);
#ifdef KEY_EVENT_DIAGNOSTICS
                debug((" - SCO mode function key"));
#endif
		use_ucsoutput = FALSE;
		goto done;
	    }
	    if (funky_type == FUNKY_SCO &&     /* SCO small keypad */
		code >= 1 && code <= 6) {
		char codes[] = "HL.FIG";
		if (code == 3) {
		    output[1] = '\x7F';
		    end = 2;
		} else {
		    end = 1 + sprintf(output+1, "\x1B[%c", codes[code-1]);
		}
#ifdef KEY_EVENT_DIAGNOSTICS
                debug((" - SCO mode small keypad key"));
#endif
		use_ucsoutput = FALSE;
		goto done;
	    }
	    if ((inst->term->vt52_mode || funky_type == FUNKY_VT100P) &&
		code >= 11 && code <= 24) {
		int offt = 0;
		if (code > 15)
		    offt++;
		if (code > 21)
		    offt++;
		if (inst->term->vt52_mode) {
#ifdef KEY_EVENT_DIAGNOSTICS
                    debug((" - VT52 mode function key"));
#endif
		    end = 1 + sprintf(output+1,
				      "\x1B%c", code + 'P' - 11 - offt);
                } else {
#ifdef KEY_EVENT_DIAGNOSTICS
                    debug((" - VT100+ mode function key"));
#endif
		    end = 1 + sprintf(output+1,
				      "\x1BO%c", code + 'P' - 11 - offt);
                }
		use_ucsoutput = FALSE;
		goto done;
	    }
	    if (funky_type == FUNKY_LINUX && code >= 11 && code <= 15) {
		end = 1 + sprintf(output+1, "\x1B[[%c", code + 'A' - 11);
#ifdef KEY_EVENT_DIAGNOSTICS
                debug((" - Linux mode F1-F5 function key"));
#endif
		use_ucsoutput = FALSE;
		goto done;
	    }
	    if (funky_type == FUNKY_XTERM && code >= 11 && code <= 14) {
		if (inst->term->vt52_mode) {
#ifdef KEY_EVENT_DIAGNOSTICS
                    debug((" - VT52 mode (overriding xterm) F1-F4 function"
                           " key"));
#endif
		    end = 1 + sprintf(output+1, "\x1B%c", code + 'P' - 11);
                } else {
#ifdef KEY_EVENT_DIAGNOSTICS
                    debug((" - xterm mode F1-F4 function key"));
#endif
		    end = 1 + sprintf(output+1, "\x1BO%c", code + 'P' - 11);
                }
		use_ucsoutput = FALSE;
		goto done;
	    }
	    if ((code == 1 || code == 4) &&
		conf_get_int(inst->conf, CONF_rxvt_homeend)) {
#ifdef KEY_EVENT_DIAGNOSTICS
                debug((" - rxvt style Home/End"));
#endif
		end = 1 + sprintf(output+1, code == 1 ? "\x1B[H" : "\x1BOw");
		use_ucsoutput = FALSE;
		goto done;
	    }
	    if (code) {
#ifdef KEY_EVENT_DIAGNOSTICS
                debug((" - ordinary function key encoding"));
#endif
		end = 1 + sprintf(output+1, "\x1B[%d~", code);
		use_ucsoutput = FALSE;
		goto done;
	    }
	}

	/*
	 * Cursor keys. (This includes the numberpad cursor keys,
	 * if we haven't already done them due to app keypad mode.)
	 * 
	 * Here we also process un-numlocked un-appkeypadded KP5,
	 * which sends ESC [ G.
	 */
	{
	    int xkey = 0;
	    switch (event->keyval) {
	      case GDK_KEY_Up: case GDK_KEY_KP_Up: xkey = 'A'; break;
	      case GDK_KEY_Down: case GDK_KEY_KP_Down: xkey = 'B'; break;
	      case GDK_KEY_Right: case GDK_KEY_KP_Right: xkey = 'C'; break;
	      case GDK_KEY_Left: case GDK_KEY_KP_Left: xkey = 'D'; break;
	      case GDK_KEY_Begin: case GDK_KEY_KP_Begin: xkey = 'G'; break;
	    }
	    if (xkey) {
		end = 1 + format_arrow_key(output+1, inst->term, xkey,
					   event->state & GDK_CONTROL_MASK);
#ifdef KEY_EVENT_DIAGNOSTICS
                debug((" - arrow key"));
#endif
		use_ucsoutput = FALSE;
		goto done;
	    }
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
            debug((" - final output, special, generic encoding = [%s]\n",
                   charset_to_localenc(output_charset), string_string));
            sfree(string_string);
#endif
	    /*
	     * For special control characters, the character set
	     * should never matter.
	     */
	    output[end] = '\0';	       /* NUL-terminate */
	    if (inst->ldisc)
		ldisc_send(inst->ldisc, output+start, -2, 1);
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
                debug((" - final output in %s = [%s]\n",
                       charset_to_localenc(output_charset), string_string));
                sfree(string_string);
#endif
		if (inst->ldisc)
		    lpage_send(inst->ldisc, output_charset, output+start,
			       end-start, 1);
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
                debug((" - final output in Unicode = [%s]\n",
                       string_string));
                sfree(string_string);
#endif

		/*
		 * We generated our own Unicode key data from the
		 * keysym, so use that instead.
		 */
		if (inst->ldisc)
		    luni_send(inst->ldisc, ucsoutput+start, end-start, 1);
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
            debug((" - final output in direct-to-font encoding = [%s]\n",
                   string_string));
            sfree(string_string);
#endif
	    if (inst->ldisc)
		ldisc_send(inst->ldisc, output+start, end-start, 1);
	}

	show_mouseptr(inst, 0);
	term_seen_key_event(inst->term);
    }

    return TRUE;
}

#if GTK_CHECK_VERSION(2,0,0)
void input_method_commit_event(GtkIMContext *imc, gchar *str, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;

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
    debug((" - IM commit event in UTF-8 = [%s]\n", string_string));
    sfree(string_string);
#endif

    if (inst->ldisc)
        lpage_send(inst->ldisc, CS_UTF8, str, strlen(str), 1);
    show_mouseptr(inst, 0);
    term_seen_key_event(inst->term);
}
#endif

#define SCROLL_INCREMENT_LINES 5

#if GTK_CHECK_VERSION(3,4,0)
gboolean scroll_internal(struct gui_data *inst, gdouble delta, guint state,
			 gdouble ex, gdouble ey)
{
    int shift, ctrl, alt, x, y, raw_mouse_mode;

    show_mouseptr(inst, 1);

    shift = state & GDK_SHIFT_MASK;
    ctrl = state & GDK_CONTROL_MASK;
    alt = state & inst->meta_mod_mask;

    x = (ex - inst->window_border) / inst->font_width;
    y = (ey - inst->window_border) / inst->font_height;

    raw_mouse_mode =
        send_raw_mouse && !(shift && conf_get_int(inst->conf,
                                                  CONF_mouse_override));

    inst->cumulative_scroll += delta * SCROLL_INCREMENT_LINES;

    if (!raw_mouse_mode) {
        int scroll_lines = (int)inst->cumulative_scroll; /* rounds toward 0 */
        if (scroll_lines) {
            term_scroll(inst->term, 0, scroll_lines);
            inst->cumulative_scroll -= scroll_lines;
        }
        return TRUE;
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
        return TRUE;
    }
}
#endif

gboolean button_internal(struct gui_data *inst, guint32 timestamp,
			 GdkEventType type, guint ebutton, guint state,
			 gdouble ex, gdouble ey)
{
    int shift, ctrl, alt, x, y, button, act, raw_mouse_mode;

    /* Remember the timestamp. */
    inst->input_event_time = timestamp;

    show_mouseptr(inst, 1);

    shift = state & GDK_SHIFT_MASK;
    ctrl = state & GDK_CONTROL_MASK;
    alt = state & inst->meta_mod_mask;

    raw_mouse_mode =
        send_raw_mouse && !(shift && conf_get_int(inst->conf,
                                                  CONF_mouse_override));

    if (!raw_mouse_mode) {
        if (ebutton == 4 && type == GDK_BUTTON_PRESS) {
            term_scroll(inst->term, 0, -SCROLL_INCREMENT_LINES);
            return TRUE;
        }
        if (ebutton == 5 && type == GDK_BUTTON_PRESS) {
            term_scroll(inst->term, 0, +SCROLL_INCREMENT_LINES);
            return TRUE;
        }
    }

    if (ebutton == 3 && ctrl) {
	gtk_menu_popup(GTK_MENU(inst->menu), NULL, NULL, NULL, NULL,
		       ebutton, timestamp);
	return TRUE;
    }

    if (ebutton == 1)
	button = MBT_LEFT;
    else if (ebutton == 2)
	button = MBT_MIDDLE;
    else if (ebutton == 3)
	button = MBT_RIGHT;
    else if (ebutton == 4)
	button = MBT_WHEEL_UP;
    else if (ebutton == 5)
	button = MBT_WHEEL_DOWN;
    else
	return FALSE;		       /* don't even know what button! */

    switch (type) {
      case GDK_BUTTON_PRESS: act = MA_CLICK; break;
      case GDK_BUTTON_RELEASE: act = MA_RELEASE; break;
      case GDK_2BUTTON_PRESS: act = MA_2CLK; break;
      case GDK_3BUTTON_PRESS: act = MA_3CLK; break;
      default: return FALSE;	       /* don't know this event type */
    }

    if (raw_mouse_mode && act != MA_CLICK && act != MA_RELEASE)
	return TRUE;		       /* we ignore these in raw mouse mode */

    x = (ex - inst->window_border) / inst->font_width;
    y = (ey - inst->window_border) / inst->font_height;

    term_mouse(inst->term, button, translate_button(button), act,
	       x, y, shift, ctrl, alt);

    return TRUE;
}

gboolean button_event(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    return button_internal(inst, event->time, event->type, event->button,
			   event->state, event->x, event->y);
}

#if GTK_CHECK_VERSION(2,0,0)
/*
 * In GTK 2, mouse wheel events have become a new type of event.
 * This handler translates them back into button-4 and button-5
 * presses so that I don't have to change my old code too much :-)
 */
gboolean scroll_event(GtkWidget *widget, GdkEventScroll *event, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;

#if GTK_CHECK_VERSION(3,4,0)
    gdouble dx, dy;
    if (gdk_event_get_scroll_deltas((GdkEvent *)event, &dx, &dy)) {
        return scroll_internal(inst, dy, event->state, event->x, event->y);
    } else
        return FALSE;
#else
    guint button;
    if (event->direction == GDK_SCROLL_UP)
	button = 4;
    else if (event->direction == GDK_SCROLL_DOWN)
	button = 5;
    else
	return FALSE;

    return button_internal(inst, event->time, GDK_BUTTON_PRESS,
			   button, event->state, event->x, event->y);
#endif
}
#endif

gint motion_event(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    int shift, ctrl, alt, x, y, button;

    /* Remember the timestamp. */
    inst->input_event_time = event->time;

    show_mouseptr(inst, 1);

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
	return FALSE;		       /* don't even know what button! */

    x = (event->x - inst->window_border) / inst->font_width;
    y = (event->y - inst->window_border) / inst->font_height;

    term_mouse(inst->term, button, translate_button(button), MA_DRAG,
	       x, y, shift, ctrl, alt);

    return TRUE;
}

void frontend_keypress(void *handle)
{
    struct gui_data *inst = (struct gui_data *)handle;

    /*
     * If our child process has exited but not closed, terminate on
     * any keypress.
     */
    if (inst->exited)
	cleanup_exit(0);
}

static void exit_callback(void *vinst)
{
    struct gui_data *inst = (struct gui_data *)vinst;
    int exitcode, close_on_exit;

    if (!inst->exited &&
        (exitcode = inst->back->exitcode(inst->backhandle)) >= 0) {
	inst->exited = TRUE;
	close_on_exit = conf_get_int(inst->conf, CONF_close_on_exit);
	if (close_on_exit == FORCE_ON ||
	    (close_on_exit == AUTO && exitcode == 0))
	    gtk_main_quit();	       /* just go */
	if (inst->ldisc) {
	    ldisc_free(inst->ldisc);
	    inst->ldisc = NULL;
	}
        inst->back->free(inst->backhandle);
        inst->backhandle = NULL;
        inst->back = NULL;
        term_provide_resize_fn(inst->term, NULL, NULL);
        update_specials_menu(inst);
	gtk_widget_set_sensitive(inst->restartitem, TRUE);
    }
}

/*
 * Replacement code for the gtk_quit_add() function, which GTK2 - in
 * their unbounded wisdom - deprecated without providing any usable
 * replacement, and which we were using to ensure that our idle
 * function for toplevel callbacks was only run from the outermost
 * gtk_main().
 *
 * We maintain a global variable with a list of 'struct gui_data'
 * instances on which we should call inst_post_main() when a
 * subsidiary gtk_main() terminates; then we must make sure that all
 * our subsidiary calls to gtk_main() are followed by a call to
 * post_main().
 *
 * This is kind of overkill in the sense that at the time of writing
 * we don't actually ever run more than one 'struct gui_data' instance
 * in the same process, but we're _so nearly_ prepared to do that that
 * I want to remain futureproof against the possibility of doing so in
 * future.
 */
struct post_main_context {
    struct post_main_context *next;
    struct gui_data *inst;
};
struct post_main_context *post_main_list_head = NULL;
static void request_post_main(struct gui_data *inst)
{
    struct post_main_context *node = snew(struct post_main_context);
    node->next = post_main_list_head;
    node->inst = inst;
    post_main_list_head = node;
}
static void inst_post_main(struct gui_data *inst);
void post_main(void)
{
    while (post_main_list_head) {
        struct post_main_context *node = post_main_list_head;
        post_main_list_head = node->next;
        inst_post_main(node->inst);
        sfree(node);
    }
}

void notify_remote_exit(void *frontend)
{
    struct gui_data *inst = (struct gui_data *)frontend;

    queue_toplevel_callback(exit_callback, inst);
}

static void notify_toplevel_callback(void *frontend);

static void inst_post_main(struct gui_data *inst)
{
    if (gtk_main_level() == 1) {
        notify_toplevel_callback(inst);
        inst->quit_fn_scheduled = FALSE;
    } else {
        /* Apparently we're _still_ more than one level deep in
         * gtk_main() instances, so we'll need another callback for
         * when we get out of the next one. */
        request_post_main(inst);
    }
}

static gint idle_toplevel_callback_func(gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;

    if (gtk_main_level() > 1) {
        /*
         * We don't run the callbacks if we're in the middle of a
         * subsidiary gtk_main. Instead, ask for a callback when we
         * get back out of the subsidiary main loop (if we haven't
         * already arranged one), so we can reschedule ourself then.
         */
        if (!inst->quit_fn_scheduled) {
            request_post_main(inst);
            inst->quit_fn_scheduled = TRUE;
        }
        /*
         * And unschedule this idle function, since we've now done
         * everything we can until the innermost gtk_main has quit and
         * can reschedule us with a chance of actually taking action.
         */
        if (inst->idle_fn_scheduled) { /* double-check, just in case */
            g_source_remove(inst->toplevel_callback_idle_id);
            inst->idle_fn_scheduled = FALSE;
        }
    } else {
        run_toplevel_callbacks();
    }

    /*
     * If we've emptied our toplevel callback queue, unschedule
     * ourself. Otherwise, leave ourselves pending so we'll be called
     * again to deal with more callbacks after another round of the
     * event loop.
     */
    if (!toplevel_callback_pending() && inst->idle_fn_scheduled) {
        g_source_remove(inst->toplevel_callback_idle_id);
        inst->idle_fn_scheduled = FALSE;
    }

    return TRUE;
}

static void notify_toplevel_callback(void *frontend)
{
    struct gui_data *inst = (struct gui_data *)frontend;

    if (!inst->idle_fn_scheduled) {
        inst->toplevel_callback_idle_id =
            g_idle_add(idle_toplevel_callback_func, inst);
        inst->idle_fn_scheduled = TRUE;
    }
}

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
     * Returning FALSE means 'don't call this timer again', which
     * _should_ be redundant given that we removed it above, but just
     * in case, return FALSE anyway.
     */
    return FALSE;
}

void timer_change_notify(unsigned long next)
{
    long ticks;

    if (timer_id)
	g_source_remove(timer_id);

    ticks = next - GETTICKCOUNT();
    if (ticks <= 0)
	ticks = 1;		       /* just in case */

    timer_id = g_timeout_add(ticks, timer_trigger, LONG_TO_GPOINTER(next));
}

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
        select_result(sourcefd, 4);
    if (condition & G_IO_IN)
        select_result(sourcefd, 1);
    if (condition & G_IO_OUT)
        select_result(sourcefd, 2);

    return TRUE;
}
#else
void fd_input_func(gpointer data, gint sourcefd, GdkInputCondition condition)
{
    if (condition & GDK_INPUT_EXCEPTION)
        select_result(sourcefd, 4);
    if (condition & GDK_INPUT_READ)
        select_result(sourcefd, 1);
    if (condition & GDK_INPUT_WRITE)
        select_result(sourcefd, 2);
}
#endif

void destroy(GtkWidget *widget, gpointer data)
{
    gtk_main_quit();
}

gint focus_event(GtkWidget *widget, GdkEventFocus *event, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    term_set_focus(inst->term, event->in);
    term_update(inst->term);
    show_mouseptr(inst, 1);
    return FALSE;
}

void set_busy_status(void *frontend, int status)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    inst->busy_status = status;
    update_mouseptr(inst);
}

/*
 * set or clear the "raw mouse message" mode
 */
void set_raw_mouse_mode(void *frontend, int activate)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    activate = activate && !conf_get_int(inst->conf, CONF_no_mouse_rep);
    send_raw_mouse = activate;
    update_mouseptr(inst);
}

void request_resize(void *frontend, int w, int h)
{
    struct gui_data *inst = (struct gui_data *)frontend;

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
    get_window_pixels(inst, &large_x, &large_y);
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

    /*
     * In GTK3, we can do this by using gtk_window_resize_to_geometry,
     * which uses the fact that we've already set up the main window's
     * WM hints to reflect the terminal drawing area's resize
     * increment (i.e. character cell) and the fixed amount of stuff
     * round the edges.
     */
    gtk_window_resize_to_geometry(GTK_WINDOW(inst->window), w, h);

#endif

}

static void real_palette_set(struct gui_data *inst, int n, int r, int g, int b)
{
    inst->cols[n].red = r * 0x0101;
    inst->cols[n].green = g * 0x0101;
    inst->cols[n].blue = b * 0x0101;

#if !GTK_CHECK_VERSION(3,0,0)
    {
        gboolean success[1];
        gdk_colormap_free_colors(inst->colmap, inst->cols + n, 1);
        gdk_colormap_alloc_colors(inst->colmap, inst->cols + n, 1,
                                  FALSE, TRUE, success);
        if (!success[0])
            g_error("%s: couldn't allocate colour %d (#%02x%02x%02x)\n",
                    appname, n, r, g, b);
    }
#endif
}

void set_gdk_window_background(GdkWindow *win, const GdkColor *col)
{
#if GTK_CHECK_VERSION(3,0,0)
    /* gdk_window_set_background is deprecated; work around its
     * absence. */
    GdkRGBA rgba;
    rgba.red = col->red / 65535.0;
    rgba.green = col->green / 65535.0;
    rgba.blue = col->blue / 65535.0;
    rgba.alpha = 1.0;
    gdk_window_set_background_rgba(win, &rgba);
#else
    {
        /* For GTK1, which doesn't have a 'const' on
         * gdk_window_set_background's second parameter type. */
        GdkColor col_mutable = *col;
        gdk_window_set_background(win, &col_mutable);
    }
#endif
}

void set_window_background(struct gui_data *inst)
{
    if (inst->area && gtk_widget_get_window(inst->area))
	set_gdk_window_background(gtk_widget_get_window(inst->area),
                                  &inst->cols[258]);
    if (inst->window && gtk_widget_get_window(inst->window))
	set_gdk_window_background(gtk_widget_get_window(inst->window),
                                  &inst->cols[258]);
}

void palette_set(void *frontend, int n, int r, int g, int b)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    if (n >= 16)
	n += 256 - 16;
    if (n >= NALLCOLOURS)
	return;
    real_palette_set(inst, n, r, g, b);
    if (n == 258) {
	/* Default Background changed. Ensure space between text area and
	 * window border is redrawn */
	set_window_background(inst);
	draw_backing_rect(inst);
	gtk_widget_queue_draw(inst->area);
    }
}

void palette_reset(void *frontend)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    /* This maps colour indices in inst->conf to those used in inst->cols. */
    static const int ww[] = {
	256, 257, 258, 259, 260, 261,
	0, 8, 1, 9, 2, 10, 3, 11,
	4, 12, 5, 13, 6, 14, 7, 15
    };
    int i;

    assert(lenof(ww) == NCFGCOLOURS);

#if !GTK_CHECK_VERSION(3,0,0)
    if (!inst->colmap) {
	inst->colmap = gdk_colormap_get_system();
    } else {
	gdk_colormap_free_colors(inst->colmap, inst->cols, NALLCOLOURS);
    }
#endif

    for (i = 0; i < NCFGCOLOURS; i++) {
	inst->cols[ww[i]].red =
	    conf_get_int_int(inst->conf, CONF_colours, i*3+0) * 0x0101;
	inst->cols[ww[i]].green =
	    conf_get_int_int(inst->conf, CONF_colours, i*3+1) * 0x0101;
	inst->cols[ww[i]].blue = 
	    conf_get_int_int(inst->conf, CONF_colours, i*3+2) * 0x0101;
    }

    for (i = 0; i < NEXTCOLOURS; i++) {
	if (i < 216) {
	    int r = i / 36, g = (i / 6) % 6, b = i % 6;
	    inst->cols[i+16].red = r ? r * 0x2828 + 0x3737 : 0;
	    inst->cols[i+16].green = g ? g * 0x2828 + 0x3737 : 0;
	    inst->cols[i+16].blue = b ? b * 0x2828 + 0x3737 : 0;
	} else {
	    int shade = i - 216;
	    shade = shade * 0x0a0a + 0x0808;
	    inst->cols[i+16].red = inst->cols[i+16].green =
		inst->cols[i+16].blue = shade;
	}
    }

#if !GTK_CHECK_VERSION(3,0,0)
    {
        gboolean success[NALLCOLOURS];
        gdk_colormap_alloc_colors(inst->colmap, inst->cols, NALLCOLOURS,
                                  FALSE, TRUE, success);
        for (i = 0; i < NALLCOLOURS; i++) {
            if (!success[i])
                g_error("%s: couldn't allocate colour %d (#%02x%02x%02x)\n",
                        appname, i,
                        conf_get_int_int(inst->conf, CONF_colours, i*3+0),
                        conf_get_int_int(inst->conf, CONF_colours, i*3+1),
                        conf_get_int_int(inst->conf, CONF_colours, i*3+2));
        }
    }
#endif

    /* Since Default Background may have changed, ensure that space
     * between text area and window border is refreshed. */
    set_window_background(inst);
    if (inst->area && gtk_widget_get_window(inst->area)) {
	draw_backing_rect(inst);
	gtk_widget_queue_draw(inst->area);
    }
}

#ifdef JUST_USE_GTK_CLIPBOARD_UTF8

/* ----------------------------------------------------------------------
 * Clipboard handling, using the high-level GtkClipboard interface in
 * as hands-off a way as possible. We write and read the clipboard as
 * UTF-8 text, and let GTK deal with converting to any other text
 * formats it feels like.
 */

void init_clipboard(struct gui_data *inst)
{
    inst->clipboard = gtk_clipboard_get_for_display(gdk_display_get_default(),
                                                    DEFAULT_CLIPBOARD);
}

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
    struct gui_data *inst;
    char *pasteout_data_utf8;
    int pasteout_data_utf8_len;
};

static void clipboard_provide_data(GtkClipboard *clipboard,
                                   GtkSelectionData *selection_data,
                                   guint info, gpointer data)
{
    struct clipboard_data_instance *cdi =
        (struct clipboard_data_instance *)data;
    struct gui_data *inst = cdi->inst;

    if (inst->current_cdi == cdi) {
        gtk_selection_data_set_text(selection_data, cdi->pasteout_data_utf8,
                                    cdi->pasteout_data_utf8_len);
    }
}

static void clipboard_clear(GtkClipboard *clipboard, gpointer data)
{
    struct clipboard_data_instance *cdi =
        (struct clipboard_data_instance *)data;
    struct gui_data *inst = cdi->inst;

    if (inst->current_cdi == cdi) {
        term_deselect(inst->term);
        inst->current_cdi = NULL;
    }
    sfree(cdi->pasteout_data_utf8);
    sfree(cdi);
}

void write_clip(void *frontend, wchar_t *data, int *attr, int len,
                int must_deselect)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    struct clipboard_data_instance *cdi;

    if (inst->direct_to_font) {
        /* In this clipboard mode, we just can't paste if we're in
         * direct-to-font mode. Fortunately, that shouldn't be
         * important, because we'll only use this clipboard handling
         * code on systems where that kind of font doesn't exist
         * anyway. */
        return;
    }

    cdi = snew(struct clipboard_data_instance);
    cdi->inst = inst;
    inst->current_cdi = cdi;
    cdi->pasteout_data_utf8 = snewn(len*6, char);
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
        gtk_clipboard_set_with_data(inst->clipboard, targettable, n_targets,
                                    clipboard_provide_data, clipboard_clear,
                                    cdi);
        gtk_target_table_free(targettable, n_targets);
        gtk_target_list_unref(targetlist);
    }
}

static void clipboard_text_received(GtkClipboard *clipboard,
                                    const gchar *text, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    int length;

    if (!text)
        return;

    length = strlen(text);

    if (inst->pastein_data)
	sfree(inst->pastein_data);

    inst->pastein_data = snewn(length, wchar_t);
    inst->pastein_data_len = mb_to_wc(CS_UTF8, 0, text, length,
                                      inst->pastein_data, length);

    term_do_paste(inst->term);
}

void request_paste(void *frontend)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    gtk_clipboard_request_text(inst->clipboard, clipboard_text_received, inst);
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

/* Store the data in a cut-buffer. */
static void store_cutbuffer(char * ptr, int len)
{
#ifndef NOT_X_WINDOWS
    Display *disp = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    /* ICCCM says we must rotate the buffers before storing to buffer 0. */
    XRotateBuffers(disp, 1);
    XStoreBytes(disp, ptr, len);
#endif
}

/* Retrieve data from a cut-buffer.
 * Returned data needs to be freed with XFree().
 */
static char *retrieve_cutbuffer(int *nbytes)
{
#ifndef NOT_X_WINDOWS
    Display *disp = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    char * ptr;
    ptr = XFetchBytes(disp, nbytes);
    if (*nbytes <= 0 && ptr != 0) {
	XFree(ptr);
	ptr = 0;
    }
    return ptr;
#else
    return NULL;
#endif
}

void write_clip(void *frontend, wchar_t *data, int *attr, int len,
                int must_deselect)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    if (inst->pasteout_data)
	sfree(inst->pasteout_data);
    if (inst->pasteout_data_ctext)
	sfree(inst->pasteout_data_ctext);
    if (inst->pasteout_data_utf8)
	sfree(inst->pasteout_data_utf8);

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
        Display *disp = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
#endif

	inst->pasteout_data_utf8 = snewn(len*6, char);
	inst->pasteout_data_utf8_len = len*6;
	inst->pasteout_data_utf8_len =
	    charset_from_unicode(&tmp, &tmplen, inst->pasteout_data_utf8,
				 inst->pasteout_data_utf8_len,
				 CS_UTF8, NULL, NULL, 0);
	if (inst->pasteout_data_utf8_len == 0) {
	    sfree(inst->pasteout_data_utf8);
	    inst->pasteout_data_utf8 = NULL;
	} else {
	    inst->pasteout_data_utf8 =
		sresize(inst->pasteout_data_utf8,
			inst->pasteout_data_utf8_len + 1, char);
	    inst->pasteout_data_utf8[inst->pasteout_data_utf8_len] = '\0';
	}

	/*
	 * Now let Xlib convert our UTF-8 data into compound text.
	 */
#ifndef NOT_X_WINDOWS
	list[0] = inst->pasteout_data_utf8;
	if (Xutf8TextListToTextProperty(disp, list, 1,
					XCompoundTextStyle, &tp) == 0) {
	    inst->pasteout_data_ctext = snewn(tp.nitems+1, char);
	    memcpy(inst->pasteout_data_ctext, tp.value, tp.nitems);
	    inst->pasteout_data_ctext_len = tp.nitems;
	    XFree(tp.value);
	} else
#endif
        {
            inst->pasteout_data_ctext = NULL;
            inst->pasteout_data_ctext_len = 0;
        }
    } else {
	inst->pasteout_data_utf8 = NULL;
	inst->pasteout_data_utf8_len = 0;
	inst->pasteout_data_ctext = NULL;
	inst->pasteout_data_ctext_len = 0;
    }

    inst->pasteout_data = snewn(len*6, char);
    inst->pasteout_data_len = len*6;
    inst->pasteout_data_len = wc_to_mb(inst->ucsdata.line_codepage, 0,
				       data, len, inst->pasteout_data,
				       inst->pasteout_data_len,
				       NULL, NULL, NULL);
    if (inst->pasteout_data_len == 0) {
	sfree(inst->pasteout_data);
	inst->pasteout_data = NULL;
    } else {
	inst->pasteout_data =
	    sresize(inst->pasteout_data, inst->pasteout_data_len, char);
    }

    store_cutbuffer(inst->pasteout_data, inst->pasteout_data_len);

    if (gtk_selection_owner_set(inst->area, GDK_SELECTION_PRIMARY,
				inst->input_event_time)) {
#if GTK_CHECK_VERSION(2,0,0)
	gtk_selection_clear_targets(inst->area, GDK_SELECTION_PRIMARY);
#endif
	gtk_selection_add_target(inst->area, GDK_SELECTION_PRIMARY,
				 GDK_SELECTION_TYPE_STRING, 1);
	if (inst->pasteout_data_ctext)
	    gtk_selection_add_target(inst->area, GDK_SELECTION_PRIMARY,
				     compound_text_atom, 1);
	if (inst->pasteout_data_utf8)
	    gtk_selection_add_target(inst->area, GDK_SELECTION_PRIMARY,
				     utf8_string_atom, 1);
    }

    if (must_deselect)
	term_deselect(inst->term);
}

static void selection_get(GtkWidget *widget, GtkSelectionData *seldata,
                          guint info, guint time_stamp, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    GdkAtom target = gtk_selection_data_get_target(seldata);
    if (target == utf8_string_atom)
	gtk_selection_data_set(seldata, target, 8,
                               (unsigned char *)inst->pasteout_data_utf8,
			       inst->pasteout_data_utf8_len);
    else if (target == compound_text_atom)
	gtk_selection_data_set(seldata, target, 8,
                               (unsigned char *)inst->pasteout_data_ctext,
			       inst->pasteout_data_ctext_len);
    else
	gtk_selection_data_set(seldata, target, 8,
                               (unsigned char *)inst->pasteout_data,
			       inst->pasteout_data_len);
}

static gint selection_clear(GtkWidget *widget, GdkEventSelection *seldata,
                            gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;

    term_deselect(inst->term);
    if (inst->pasteout_data)
	sfree(inst->pasteout_data);
    if (inst->pasteout_data_ctext)
	sfree(inst->pasteout_data_ctext);
    if (inst->pasteout_data_utf8)
	sfree(inst->pasteout_data_utf8);
    inst->pasteout_data = NULL;
    inst->pasteout_data_len = 0;
    inst->pasteout_data_ctext = NULL;
    inst->pasteout_data_ctext_len = 0;
    inst->pasteout_data_utf8 = NULL;
    inst->pasteout_data_utf8_len = 0;
    return TRUE;
}

void request_paste(void *frontend)
{
    struct gui_data *inst = (struct gui_data *)frontend;
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
	gtk_selection_convert(inst->area, GDK_SELECTION_PRIMARY,
			      utf8_string_atom,
			      inst->input_event_time);
    } else {
	/*
	 * If we're in direct-to-font mode, we disable UTF-8
	 * pasting, and go straight to ordinary string data.
	 */
	gtk_selection_convert(inst->area, GDK_SELECTION_PRIMARY,
			      GDK_SELECTION_TYPE_STRING,
			      inst->input_event_time);
    }
}

static void selection_received(GtkWidget *widget, GtkSelectionData *seldata,
                               guint time, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    char *text;
    int length;
#ifndef NOT_X_WINDOWS
    char **list;
    int free_list_required = 0;
    int free_required = 0;
#endif
    int charset;
    GdkAtom seldata_target = gtk_selection_data_get_target(seldata);
    GdkAtom seldata_type = gtk_selection_data_get_data_type(seldata);
    const guchar *seldata_data = gtk_selection_data_get_data(seldata);
    gint seldata_length = gtk_selection_data_get_length(seldata);

    if (seldata_target == utf8_string_atom && seldata_length <= 0) {
	/*
	 * Failed to get a UTF-8 selection string. Try compound
	 * text next.
	 */
	gtk_selection_convert(inst->area, GDK_SELECTION_PRIMARY,
			      compound_text_atom,
			      inst->input_event_time);
	return;
    }

    if (seldata_target == compound_text_atom && seldata_length <= 0) {
	/*
	 * Failed to get UTF-8 or compound text. Try an ordinary
	 * string.
	 */
	gtk_selection_convert(inst->area, GDK_SELECTION_PRIMARY,
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
	text = retrieve_cutbuffer(&length);
	if (length == 0)
	    return;
	/* Xterm is rumoured to expect Latin-1, though I havn't checked the
	 * source, so use that as a de-facto standard. */
	charset = CS_ISO8859_1;
	free_required = 1;
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
            Display *disp = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());

	    tp.value = (unsigned char *)seldata_data;
	    tp.encoding = (Atom) seldata_type;
	    tp.format = gtk_selection_data_get_format(seldata);
	    tp.nitems = seldata_length;
	    ret = Xutf8TextPropertyToTextList(disp, &tp, &list, &count);
	    if (ret == 0 && count == 1) {
                text = list[0];
                length = strlen(list[0]);
                charset = CS_UTF8;
                free_list_required = 1;
            } else
#endif
            {
		/*
		 * Compound text failed; fall back to STRING.
		 */
		gtk_selection_convert(inst->area, GDK_SELECTION_PRIMARY,
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

    if (inst->pastein_data)
	sfree(inst->pastein_data);

    inst->pastein_data = snewn(length, wchar_t);
    inst->pastein_data_len = length;
    inst->pastein_data_len =
	mb_to_wc(charset, 0, text, length,
		 inst->pastein_data, inst->pastein_data_len);

    term_do_paste(inst->term);

#ifndef NOT_X_WINDOWS
    if (free_list_required)
	XFreeStringList(list);
    if (free_required)
	XFree(text);
#endif
}

void init_clipboard(struct gui_data *inst)
{
#ifndef NOT_X_WINDOWS
    /*
     * Ensure that all the cut buffers exist - according to the ICCCM,
     * we must do this before we start using cut buffers.
     */
    unsigned char empty[] = "";
    Display *disp = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    x11_ignore_error(disp, BadMatch);
    XChangeProperty(disp, GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER0, XA_STRING, 8, PropModeAppend, empty, 0);
    x11_ignore_error(disp, BadMatch);
    XChangeProperty(disp, GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER1, XA_STRING, 8, PropModeAppend, empty, 0);
    x11_ignore_error(disp, BadMatch);
    XChangeProperty(disp, GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER2, XA_STRING, 8, PropModeAppend, empty, 0);
    x11_ignore_error(disp, BadMatch);
    XChangeProperty(disp, GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER3, XA_STRING, 8, PropModeAppend, empty, 0);
    x11_ignore_error(disp, BadMatch);
    XChangeProperty(disp, GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER4, XA_STRING, 8, PropModeAppend, empty, 0);
    x11_ignore_error(disp, BadMatch);
    XChangeProperty(disp, GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER5, XA_STRING, 8, PropModeAppend, empty, 0);
    x11_ignore_error(disp, BadMatch);
    XChangeProperty(disp, GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER6, XA_STRING, 8, PropModeAppend, empty, 0);
    x11_ignore_error(disp, BadMatch);
    XChangeProperty(disp, GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER7, XA_STRING, 8, PropModeAppend, empty, 0);
#endif

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

void get_clip(void *frontend, wchar_t ** p, int *len)
{
    struct gui_data *inst = (struct gui_data *)frontend;

    if (p) {
	*p = inst->pastein_data;
	*len = inst->pastein_data_len;
    }
}

static void set_window_titles(struct gui_data *inst)
{
    /*
     * We must always call set_icon_name after calling set_title,
     * since set_title will write both names. Irritating, but such
     * is life.
     */
    gtk_window_set_title(GTK_WINDOW(inst->window), inst->wintitle);
    if (!conf_get_int(inst->conf, CONF_win_name_always))
	gdk_window_set_icon_name(gtk_widget_get_window(inst->window),
                                 inst->icontitle);
}

void set_title(void *frontend, char *title)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    sfree(inst->wintitle);
    inst->wintitle = dupstr(title);
    set_window_titles(inst);
}

void set_icon(void *frontend, char *title)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    sfree(inst->icontitle);
    inst->icontitle = dupstr(title);
    set_window_titles(inst);
}

void set_title_and_icon(void *frontend, char *title, char *icon)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    sfree(inst->wintitle);
    inst->wintitle = dupstr(title);
    sfree(inst->icontitle);
    inst->icontitle = dupstr(icon);
    set_window_titles(inst);
}

void set_sbar(void *frontend, int total, int start, int page)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    if (!conf_get_int(inst->conf, CONF_scrollbar))
	return;
    gtk_adjustment_set_lower(inst->sbar_adjust, 0);
    gtk_adjustment_set_upper(inst->sbar_adjust, total);
    gtk_adjustment_set_value(inst->sbar_adjust, start);
    gtk_adjustment_set_page_size(inst->sbar_adjust, page);
    gtk_adjustment_set_step_increment(inst->sbar_adjust, 1);
    gtk_adjustment_set_page_increment(inst->sbar_adjust, page/2);
    inst->ignore_sbar = TRUE;
    gtk_adjustment_changed(inst->sbar_adjust);
    inst->ignore_sbar = FALSE;
}

void scrollbar_moved(GtkAdjustment *adj, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;

    if (!conf_get_int(inst->conf, CONF_scrollbar))
	return;
    if (!inst->ignore_sbar)
	term_scroll(inst->term, 1, (int)gtk_adjustment_get_value(adj));
}

void sys_cursor(void *frontend, int x, int y)
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
void do_beep(void *frontend, int mode)
{
    if (mode == BELL_DEFAULT)
	gdk_beep();
}

int char_width(Context ctx, int uc)
{
    /*
     * In this front end, double-width characters are handled using a
     * separate font, so this can safely just return 1 always.
     */
    return 1;
}

Context get_ctx(void *frontend)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    struct draw_ctx *dctx;

    if (!gtk_widget_get_window(inst->area))
	return NULL;

    dctx = snew(struct draw_ctx);
    dctx->inst = inst;
    dctx->uctx.type = inst->drawtype;
#ifdef DRAW_TEXT_GDK
    if (dctx->uctx.type == DRAWTYPE_GDK) {
        /* If we're doing GDK-based drawing, then we also expect
         * inst->pixmap to exist. */
        dctx->uctx.u.gdk.target = inst->pixmap;
        dctx->uctx.u.gdk.gc = gdk_gc_new(gtk_widget_get_window(inst->area));
    }
#endif
#ifdef DRAW_TEXT_CAIRO
    if (dctx->uctx.type == DRAWTYPE_CAIRO) {
        dctx->uctx.u.cairo.widget = GTK_WIDGET(inst->area);
        /* If we're doing Cairo drawing, we expect inst->surface to
         * exist, and we draw to that first, regardless of whether we
         * subsequently copy the results to inst->pixmap. */
        dctx->uctx.u.cairo.cr = cairo_create(inst->surface);
        cairo_setup_dctx(dctx);
    }
#endif
    return dctx;
}

void free_ctx(Context ctx)
{
    struct draw_ctx *dctx = (struct draw_ctx *)ctx;
    /* struct gui_data *inst = dctx->inst; */
#ifdef DRAW_TEXT_GDK
    if (dctx->uctx.type == DRAWTYPE_GDK) {
        gdk_gc_unref(dctx->uctx.u.gdk.gc);
    }
#endif
#ifdef DRAW_TEXT_CAIRO
    if (dctx->uctx.type == DRAWTYPE_CAIRO) {
        cairo_destroy(dctx->uctx.u.cairo.cr);
    }
#endif
    sfree(dctx);
}


static void draw_update(struct draw_ctx *dctx, int x, int y, int w, int h)
{
#if defined DRAW_TEXT_CAIRO && !defined NO_BACKING_PIXMAPS
    if (dctx->uctx.type == DRAWTYPE_CAIRO) {
        /*
         * If inst->surface and inst->pixmap both exist, then we've
         * just drawn new content to the former which we must copy to
         * the latter.
         */
        cairo_t *cr = gdk_cairo_create(dctx->inst->pixmap);
        cairo_set_source_surface(cr, dctx->inst->surface, 0, 0);
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
    gtk_widget_queue_draw_area(dctx->inst->area, x, y, w, h);
}

static void draw_set_colour(struct draw_ctx *dctx, int col)
{
#ifdef DRAW_TEXT_GDK
    if (dctx->uctx.type == DRAWTYPE_GDK) {
        gdk_gc_set_foreground(dctx->uctx.u.gdk.gc, &dctx->inst->cols[col]);
    }
#endif
#ifdef DRAW_TEXT_CAIRO
    if (dctx->uctx.type == DRAWTYPE_CAIRO) {
        cairo_set_source_rgb(dctx->uctx.u.cairo.cr,
                             dctx->inst->cols[col].red / 65535.0,
                             dctx->inst->cols[col].green / 65535.0,
                             dctx->inst->cols[col].blue / 65535.0);
    }
#endif
}

static void draw_rectangle(struct draw_ctx *dctx, int filled,
                           int x, int y, int w, int h)
{
#ifdef DRAW_TEXT_GDK
    if (dctx->uctx.type == DRAWTYPE_GDK) {
        gdk_draw_rectangle(dctx->uctx.u.gdk.target, dctx->uctx.u.gdk.gc,
                           filled, x, y, w, h);
    }
#endif
#ifdef DRAW_TEXT_CAIRO
    if (dctx->uctx.type == DRAWTYPE_CAIRO) {
        cairo_new_path(dctx->uctx.u.cairo.cr);
        if (filled) {
            cairo_rectangle(dctx->uctx.u.cairo.cr, x, y, w, h);
            cairo_fill(dctx->uctx.u.cairo.cr);
        } else {
            cairo_rectangle(dctx->uctx.u.cairo.cr,
                            x + 0.5, y + 0.5, w, h);
            cairo_close_path(dctx->uctx.u.cairo.cr);
            cairo_stroke(dctx->uctx.u.cairo.cr);
        }
    }
#endif
}

static void draw_clip(struct draw_ctx *dctx, int x, int y, int w, int h)
{
#ifdef DRAW_TEXT_GDK
    if (dctx->uctx.type == DRAWTYPE_GDK) {
	GdkRectangle r;

	r.x = x;
	r.y = y;
	r.width = w;
	r.height = h;

        gdk_gc_set_clip_rectangle(dctx->uctx.u.gdk.gc, &r);
    }
#endif
#ifdef DRAW_TEXT_CAIRO
    if (dctx->uctx.type == DRAWTYPE_CAIRO) {
        cairo_reset_clip(dctx->uctx.u.cairo.cr);
        cairo_new_path(dctx->uctx.u.cairo.cr);
        cairo_rectangle(dctx->uctx.u.cairo.cr, x, y, w, h);
        cairo_clip(dctx->uctx.u.cairo.cr);
    }
#endif
}

static void draw_point(struct draw_ctx *dctx, int x, int y)
{
#ifdef DRAW_TEXT_GDK
    if (dctx->uctx.type == DRAWTYPE_GDK) {
        gdk_draw_point(dctx->uctx.u.gdk.target, dctx->uctx.u.gdk.gc, x, y);
    }
#endif
#ifdef DRAW_TEXT_CAIRO
    if (dctx->uctx.type == DRAWTYPE_CAIRO) {
        cairo_new_path(dctx->uctx.u.cairo.cr);
        cairo_rectangle(dctx->uctx.u.cairo.cr, x, y, 1, 1);
        cairo_fill(dctx->uctx.u.cairo.cr);
    }
#endif
}

static void draw_line(struct draw_ctx *dctx, int x0, int y0, int x1, int y1)
{
#ifdef DRAW_TEXT_GDK
    if (dctx->uctx.type == DRAWTYPE_GDK) {
        gdk_draw_line(dctx->uctx.u.gdk.target, dctx->uctx.u.gdk.gc,
                      x0, y0, x1, y1);
    }
#endif
#ifdef DRAW_TEXT_CAIRO
    if (dctx->uctx.type == DRAWTYPE_CAIRO) {
        cairo_new_path(dctx->uctx.u.cairo.cr);
        cairo_move_to(dctx->uctx.u.cairo.cr, x0 + 0.5, y0 + 0.5);
        cairo_line_to(dctx->uctx.u.cairo.cr, x1 + 0.5, y1 + 0.5);
        cairo_stroke(dctx->uctx.u.cairo.cr);
    }
#endif
}

static void draw_stretch_before(struct draw_ctx *dctx, int x, int y,
                                int w, int wdouble,
                                int h, int hdouble, int hbothalf)
{
#ifdef DRAW_TEXT_CAIRO
    if (dctx->uctx.type == DRAWTYPE_CAIRO) {
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
        cairo_transform(dctx->uctx.u.cairo.cr, &matrix);
    }
#endif
}

static void draw_stretch_after(struct draw_ctx *dctx, int x, int y,
                               int w, int wdouble,
                               int h, int hdouble, int hbothalf)
{
#ifdef DRAW_TEXT_GDK
#ifndef NO_BACKING_PIXMAPS
    if (dctx->uctx.type == DRAWTYPE_GDK) {
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
                gdk_draw_pixmap(dctx->uctx.u.gdk.target,
                                dctx->uctx.u.gdk.gc,
                                dctx->uctx.u.gdk.target,
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
		gdk_draw_pixmap(dctx->uctx.u.gdk.target,
                                dctx->uctx.u.gdk.gc,
                                dctx->uctx.u.gdk.target,
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
    if (dctx->uctx.type == DRAWTYPE_CAIRO) {
        cairo_set_matrix(dctx->uctx.u.cairo.cr,
                         &dctx->uctx.u.cairo.origmatrix);
    }
#endif
}

static void draw_backing_rect(struct gui_data *inst)
{
    struct draw_ctx *dctx = get_ctx(inst);
    int w = inst->width * inst->font_width + 2*inst->window_border;
    int h = inst->height * inst->font_height + 2*inst->window_border;
    draw_set_colour(dctx, 258);
    draw_rectangle(dctx, 1, 0, 0, w, h);
    draw_update(dctx, 0, 0, w, h);
    free_ctx(dctx);
}

/*
 * Draw a line of text in the window, at given character
 * coordinates, in given attributes.
 *
 * We are allowed to fiddle with the contents of `text'.
 */
void do_text_internal(Context ctx, int x, int y, wchar_t *text, int len,
		      unsigned long attr, int lattr)
{
    struct draw_ctx *dctx = (struct draw_ctx *)ctx;
    struct gui_data *inst = dctx->inst;
    int ncombining;
    int nfg, nbg, t, fontid, shadow, rlen, widefactor, bold;
    int monochrome =
        gdk_visual_get_depth(gtk_widget_get_visual(inst->area)) == 1;

    if (attr & TATTR_COMBINING) {
	ncombining = len;
	len = 1;
    } else
	ncombining = 1;

    nfg = ((monochrome ? ATTR_DEFFG : (attr & ATTR_FGMASK)) >> ATTR_FGSHIFT);
    nbg = ((monochrome ? ATTR_DEFBG : (attr & ATTR_BGMASK)) >> ATTR_BGSHIFT);
    if (!!(attr & ATTR_REVERSE) ^ (monochrome && (attr & TATTR_ACTCURS))) {
	t = nfg;
	nfg = nbg;
	nbg = t;
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
	nfg = 260;
	nbg = 261;
    }

    fontid = shadow = 0;

    if (attr & ATTR_WIDE) {
	widefactor = 2;
	fontid |= 2;
    } else {
	widefactor = 1;
    }

    if ((attr & ATTR_BOLD) && (inst->bold_style & 1)) {
	bold = 1;
	fontid |= 1;
    } else {
	bold = 0;
    }

    if (!inst->fonts[fontid]) {
	int i;
	/*
	 * Fall back through font ids with subsets of this one's
	 * set bits, in order.
	 */
	for (i = fontid; i-- > 0 ;) {
	    if (i & ~fontid)
		continue;	       /* some other bit is set */
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
	if (x + len*2*widefactor > inst->term->cols)
	    len = (inst->term->cols-x)/2/widefactor;/* trim to LH half */
	rlen = len * 2;
    } else
	rlen = len;

    draw_clip(dctx,
              x*inst->font_width+inst->window_border,
              y*inst->font_height+inst->window_border,
              rlen*widefactor*inst->font_width,
              inst->font_height);

    if ((lattr & LATTR_MODE) != LATTR_NORM) {
        draw_stretch_before(dctx,
                            x*inst->font_width+inst->window_border,
                            y*inst->font_height+inst->window_border,
                            rlen*widefactor*inst->font_width, TRUE,
                            inst->font_height,
                            ((lattr & LATTR_MODE) != LATTR_WIDE),
                            ((lattr & LATTR_MODE) == LATTR_BOT));
    }

    draw_set_colour(dctx, nbg);
    draw_rectangle(dctx, TRUE,
                   x*inst->font_width+inst->window_border,
                   y*inst->font_height+inst->window_border,
                   rlen*widefactor*inst->font_width, inst->font_height);

    draw_set_colour(dctx, nfg);
    if (ncombining > 1) {
        assert(len == 1);
        unifont_draw_combining(&dctx->uctx, inst->fonts[fontid],
                               x*inst->font_width+inst->window_border,
                               (y*inst->font_height+inst->window_border+
                                inst->fonts[0]->ascent),
                               text, ncombining, widefactor > 1,
                               bold, inst->font_width);
    } else {
        unifont_draw_text(&dctx->uctx, inst->fonts[fontid],
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
        draw_line(dctx, x*inst->font_width+inst->window_border,
                  y*inst->font_height + uheight + inst->window_border,
                  (x+len)*widefactor*inst->font_width-1+inst->window_border,
                  y*inst->font_height + uheight + inst->window_border);
    }

    if ((lattr & LATTR_MODE) != LATTR_NORM) {
        draw_stretch_after(dctx,
                           x*inst->font_width+inst->window_border,
                           y*inst->font_height+inst->window_border,
                           rlen*widefactor*inst->font_width, TRUE,
                           inst->font_height,
                           ((lattr & LATTR_MODE) != LATTR_WIDE),
                           ((lattr & LATTR_MODE) == LATTR_BOT));
    }
}

void do_text(Context ctx, int x, int y, wchar_t *text, int len,
	     unsigned long attr, int lattr)
{
    struct draw_ctx *dctx = (struct draw_ctx *)ctx;
    struct gui_data *inst = dctx->inst;
    int widefactor;

    do_text_internal(ctx, x, y, text, len, attr, lattr);

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

    draw_update(dctx,
                x*inst->font_width+inst->window_border,
                y*inst->font_height+inst->window_border,
                len*widefactor*inst->font_width, inst->font_height);
}

void do_cursor(Context ctx, int x, int y, wchar_t *text, int len,
	       unsigned long attr, int lattr)
{
    struct draw_ctx *dctx = (struct draw_ctx *)ctx;
    struct gui_data *inst = dctx->inst;

    int active, passive, widefactor;

    if (attr & TATTR_PASCURS) {
	attr &= ~TATTR_PASCURS;
	passive = 1;
    } else
	passive = 0;
    if ((attr & TATTR_ACTCURS) && inst->cursor_type != 0) {
	attr &= ~TATTR_ACTCURS;
        active = 1;
    } else
        active = 0;
    do_text_internal(ctx, x, y, text, len, attr, lattr);

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
            draw_set_colour(dctx, 261);
            draw_rectangle(dctx, FALSE,
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

        draw_set_colour(dctx, 261);
	if (passive) {
	    for (i = 0; i < length; i++) {
		if (i % 2 == 0) {
		    draw_point(dctx, startx, starty);
		}
		startx += dx;
		starty += dy;
	    }
	} else if (active) {
	    draw_line(dctx, startx, starty,
                      startx + (length-1) * dx, starty + (length-1) * dy);
	} /* else no cursor (e.g., blinked off) */
    }

    draw_update(dctx,
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

GdkCursor *make_mouse_ptr(struct gui_data *inst, int cursor_val)
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

void cmdline_error(const char *p, ...)
{
    va_list ap;
    fprintf(stderr, "%s: ", appname);
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

const char *get_x_display(void *frontend)
{
    return gdk_get_display();
}

#ifndef NOT_X_WINDOWS
long get_windowid(void *frontend)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    return (long)GDK_WINDOW_XID(gtk_widget_get_window(inst->area));
}
#endif

static void help(FILE *fp) {
    if(fprintf(fp,
"pterm option summary:\n"
"\n"
"  --display DISPLAY         Specify X display to use (note '--')\n"
"  -name PREFIX              Prefix when looking up resources (default: pterm)\n"
"  -fn FONT                  Normal text font\n"
"  -fb FONT                  Bold text font\n"
"  -geometry GEOMETRY        Position and size of window (size in characters)\n"
"  -sl LINES                 Number of lines of scrollback\n"
"  -fg COLOUR, -bg COLOUR    Foreground/background colour\n"
"  -bfg COLOUR, -bbg COLOUR  Foreground/background bold colour\n"
"  -cfg COLOUR, -bfg COLOUR  Foreground/background cursor colour\n"
"  -T TITLE                  Window title\n"
"  -ut, +ut                  Do(default) or do not update utmp\n"
"  -ls, +ls                  Do(default) or do not make shell a login shell\n"
"  -sb, +sb                  Do(default) or do not display a scrollbar\n"
"  -log PATH, -sessionlog PATH  Log all output to a file\n"
"  -nethack                  Map numeric keypad to hjklyubn direction keys\n"
"  -xrm RESOURCE-STRING      Set an X resource\n"
"  -e COMMAND [ARGS...]      Execute command (consumes all remaining args)\n"
	 ) < 0 || fflush(fp) < 0) {
	perror("output error");
	exit(1);
    }
}

static void version(FILE *fp) {
    if(fprintf(fp, "%s: %s\n", appname, ver) < 0 || fflush(fp) < 0) {
	perror("output error");
	exit(1);
    }
}

int do_cmdline(int argc, char **argv, int do_everything, int *allow_launch,
               struct gui_data *inst, Conf *conf)
{
    int err = 0;
    char *val;

    /*
     * Macros to make argument handling easier. Note that because
     * they need to call `continue', they cannot be contained in
     * the usual do {...} while (0) wrapper to make them
     * syntactically single statements; hence it is not legal to
     * use one of these macros as an unbraced statement between
     * `if' and `else'.
     */
#define EXPECTS_ARG { \
    if (--argc <= 0) { \
	err = 1; \
	fprintf(stderr, "%s: %s expects an argument\n", appname, p); \
        continue; \
    } else \
	val = *++argv; \
}
#define SECOND_PASS_ONLY { if (!do_everything) continue; }

    while (--argc > 0) {
	const char *p = *++argv;
        int ret;

	/*
	 * Shameless cheating. Debian requires all X terminal
	 * emulators to support `-T title'; but
	 * cmdline_process_param will eat -T (it means no-pty) and
	 * complain that pterm doesn't support it. So, in pterm
	 * only, we convert -T into -title.
	 */
	if ((cmdline_tooltype & TOOLTYPE_NONNETWORK) &&
	    !strcmp(p, "-T"))
	    p = "-title";

        ret = cmdline_process_param(p, (argc > 1 ? argv[1] : NULL),
                                    do_everything ? 1 : -1, conf);

	if (ret == -2) {
	    cmdline_error("option \"%s\" requires an argument", p);
	} else if (ret == 2) {
	    --argc, ++argv;            /* skip next argument */
            continue;
	} else if (ret == 1) {
            continue;
        }

	if (!strcmp(p, "-fn") || !strcmp(p, "-font")) {
	    FontSpec *fs;
	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;
            fs = fontspec_new(val);
	    conf_set_fontspec(conf, CONF_font, fs);
            fontspec_free(fs);

	} else if (!strcmp(p, "-fb")) {
	    FontSpec *fs;
	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;
            fs = fontspec_new(val);
	    conf_set_fontspec(conf, CONF_boldfont, fs);
            fontspec_free(fs);

	} else if (!strcmp(p, "-fw")) {
	    FontSpec *fs;
	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;
            fs = fontspec_new(val);
	    conf_set_fontspec(conf, CONF_widefont, fs);
            fontspec_free(fs);

	} else if (!strcmp(p, "-fwb")) {
	    FontSpec *fs;
	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;
            fs = fontspec_new(val);
	    conf_set_fontspec(conf, CONF_wideboldfont, fs);
            fontspec_free(fs);

	} else if (!strcmp(p, "-cs")) {
	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;
	    conf_set_str(conf, CONF_line_codepage, val);

	} else if (!strcmp(p, "-geometry")) {
	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;

#if GTK_CHECK_VERSION(2,0,0)
            inst->geometry = val;
#else
            /* On GTK 1, we have to do this using raw Xlib */
            {
                int flags, x, y;
                unsigned int w, h;
                flags = XParseGeometry(val, &x, &y, &w, &h);
                if (flags & WidthValue)
                    conf_set_int(conf, CONF_width, w);
                if (flags & HeightValue)
                    conf_set_int(conf, CONF_height, h);

                if (flags & (XValue | YValue)) {
                    inst->xpos = x;
                    inst->ypos = y;
                    inst->gotpos = TRUE;
                    inst->gravity = ((flags & XNegative ? 1 : 0) |
                                     (flags & YNegative ? 2 : 0));
                }
            }
#endif

	} else if (!strcmp(p, "-sl")) {
	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;
	    conf_set_int(conf, CONF_savelines, atoi(val));

	} else if (!strcmp(p, "-fg") || !strcmp(p, "-bg") ||
		   !strcmp(p, "-bfg") || !strcmp(p, "-bbg") ||
		   !strcmp(p, "-cfg") || !strcmp(p, "-cbg")) {
	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;

            {
#if GTK_CHECK_VERSION(3,0,0)
                GdkRGBA rgba;
                int success = gdk_rgba_parse(&rgba, val);
#else
                GdkColor col;
                int success = gdk_color_parse(val, &col);
#endif

                if (!success) {
                    err = 1;
                    fprintf(stderr, "%s: unable to parse colour \"%s\"\n",
                            appname, val);
                } else {
#if GTK_CHECK_VERSION(3,0,0)
                    int r = rgba.red * 255;
                    int g = rgba.green * 255;
                    int b = rgba.blue * 255;
#else
                    int r = col.red / 256;
                    int g = col.green / 256;
                    int b = col.blue / 256;
#endif

                    int index;
                    index = (!strcmp(p, "-fg") ? 0 :
                             !strcmp(p, "-bg") ? 2 :
                             !strcmp(p, "-bfg") ? 1 :
                             !strcmp(p, "-bbg") ? 3 :
                             !strcmp(p, "-cfg") ? 4 :
                             !strcmp(p, "-cbg") ? 5 : -1);
                    assert(index != -1);

                    conf_set_int_int(conf, CONF_colours, index*3+0, r);
                    conf_set_int_int(conf, CONF_colours, index*3+1, g);
                    conf_set_int_int(conf, CONF_colours, index*3+2, b);
                }
            }

	} else if (use_pty_argv && !strcmp(p, "-e")) {
	    /* This option swallows all further arguments. */
	    if (!do_everything)
		break;

	    if (--argc > 0) {
		int i;
		pty_argv = snewn(argc+1, char *);
		++argv;
		for (i = 0; i < argc; i++)
		    pty_argv[i] = argv[i];
		pty_argv[argc] = NULL;
		break;		       /* finished command-line processing */
	    } else
		err = 1, fprintf(stderr, "%s: -e expects an argument\n",
                                 appname);

	} else if (!strcmp(p, "-title")) {
	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;
	    conf_set_str(conf, CONF_wintitle, val);

	} else if (!strcmp(p, "-log")) {
	    Filename *fn;
	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;
            fn = filename_from_str(val);
	    conf_set_filename(conf, CONF_logfilename, fn);
	    conf_set_int(conf, CONF_logtype, LGTYP_DEBUG);
            filename_free(fn);

	} else if (!strcmp(p, "-ut-") || !strcmp(p, "+ut")) {
	    SECOND_PASS_ONLY;
	    conf_set_int(conf, CONF_stamp_utmp, 0);

	} else if (!strcmp(p, "-ut")) {
	    SECOND_PASS_ONLY;
	    conf_set_int(conf, CONF_stamp_utmp, 1);

	} else if (!strcmp(p, "-ls-") || !strcmp(p, "+ls")) {
	    SECOND_PASS_ONLY;
	    conf_set_int(conf, CONF_login_shell, 0);

	} else if (!strcmp(p, "-ls")) {
	    SECOND_PASS_ONLY;
	    conf_set_int(conf, CONF_login_shell, 1);

	} else if (!strcmp(p, "-nethack")) {
	    SECOND_PASS_ONLY;
	    conf_set_int(conf, CONF_nethack_keypad, 1);

	} else if (!strcmp(p, "-sb-") || !strcmp(p, "+sb")) {
	    SECOND_PASS_ONLY;
	    conf_set_int(conf, CONF_scrollbar, 0);

	} else if (!strcmp(p, "-sb")) {
	    SECOND_PASS_ONLY;
	    conf_set_int(conf, CONF_scrollbar, 1);

	} else if (!strcmp(p, "-name")) {
	    EXPECTS_ARG;
	    app_name = val;

	} else if (!strcmp(p, "-xrm")) {
	    EXPECTS_ARG;
	    provide_xrm_string(val);

	} else if(!strcmp(p, "-help") || !strcmp(p, "--help")) {
	    help(stdout);
	    exit(0);

	} else if(!strcmp(p, "-version") || !strcmp(p, "--version")) {
	    version(stdout);
	    exit(0);

        } else if (!strcmp(p, "-pgpfp")) {
            pgp_fingerprints();
            exit(1);

	} else if(p[0] != '-' && (!do_everything ||
                                  process_nonoption_arg(p, conf,
							allow_launch))) {
            /* do nothing */

	} else {
	    err = 1;
	    fprintf(stderr, "%s: unrecognized option '%s'\n", appname, p);
	}
    }

    return err;
}

struct uxsel_id {
#if GTK_CHECK_VERSION(2,0,0)
    GIOChannel *chan;
    guint watch_id;
#else
    int id;
#endif
};

uxsel_id *uxsel_input_add(int fd, int rwx) {
    uxsel_id *id = snew(uxsel_id);

#if GTK_CHECK_VERSION(2,0,0)
    int flags = 0;
    if (rwx & 1) flags |= G_IO_IN;
    if (rwx & 2) flags |= G_IO_OUT;
    if (rwx & 4) flags |= G_IO_PRI;
    id->chan = g_io_channel_unix_new(fd);
    g_io_channel_set_encoding(id->chan, NULL, NULL);
    id->watch_id = g_io_add_watch_full(id->chan, GDK_PRIORITY_REDRAW+1, flags,
                                       fd_input_func, NULL, NULL);
#else
    int flags = 0;
    if (rwx & 1) flags |= GDK_INPUT_READ;
    if (rwx & 2) flags |= GDK_INPUT_WRITE;
    if (rwx & 4) flags |= GDK_INPUT_EXCEPTION;
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

int frontend_is_utf8(void *frontend)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    return inst->ucsdata.line_codepage == CS_UTF8;
}

char *setup_fonts_ucs(struct gui_data *inst)
{
    int shadowbold = conf_get_int(inst->conf, CONF_shadowbold);
    int shadowboldoffset = conf_get_int(inst->conf, CONF_shadowboldoffset);
    FontSpec *fs;
    unifont *fonts[4];
    int i;

    fs = conf_get_fontspec(inst->conf, CONF_font);
    fonts[0] = multifont_create(inst->area, fs->name, FALSE, FALSE,
                                shadowboldoffset, shadowbold);
    if (!fonts[0]) {
        return dupprintf("unable to load font \"%s\"", fs->name);
    }

    fs = conf_get_fontspec(inst->conf, CONF_boldfont);
    if (shadowbold || !fs->name[0]) {
	fonts[1] = NULL;
    } else {
	fonts[1] = multifont_create(inst->area, fs->name, FALSE, TRUE,
                                    shadowboldoffset, shadowbold);
	if (!fonts[1]) {
            if (fonts[0])
                unifont_destroy(fonts[0]);
	    return dupprintf("unable to load bold font \"%s\"", fs->name);
	}
    }

    fs = conf_get_fontspec(inst->conf, CONF_widefont);
    if (fs->name[0]) {
	fonts[2] = multifont_create(inst->area, fs->name, TRUE, FALSE,
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
	fonts[3] = multifont_create(inst->area, fs->name, TRUE, TRUE,
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

    inst->font_width = inst->fonts[0]->width;
    inst->font_height = inst->fonts[0]->height;

    inst->direct_to_font = init_ucs(&inst->ucsdata,
				    conf_get_str(inst->conf, CONF_line_codepage),
				    conf_get_int(inst->conf, CONF_utf8_override),
				    inst->fonts[0]->public_charset,
				    conf_get_int(inst->conf, CONF_vtmode));

    inst->drawtype = inst->fonts[0]->preferred_drawtype;

    return NULL;
}

void set_geom_hints(struct gui_data *inst)
{
    GdkGeometry geom;
    geom.min_width = inst->font_width + 2*inst->window_border;
    geom.min_height = inst->font_height + 2*inst->window_border;
    geom.max_width = geom.max_height = -1;
    geom.base_width = 2*inst->window_border;
    geom.base_height = 2*inst->window_border;
    geom.width_inc = inst->font_width;
    geom.height_inc = inst->font_height;
    geom.min_aspect = geom.max_aspect = 0;
    gtk_window_set_geometry_hints(GTK_WINDOW(inst->window), inst->area, &geom,
                                  GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE |
                                  GDK_HINT_RESIZE_INC);
}

void clear_scrollback_menuitem(GtkMenuItem *item, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    term_clrsb(inst->term);
}

void reset_terminal_menuitem(GtkMenuItem *item, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    term_pwron(inst->term, TRUE);
    if (inst->ldisc)
	ldisc_echoedit_update(inst->ldisc);
}

void copy_all_menuitem(GtkMenuItem *item, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    term_copyall(inst->term);
}

void special_menuitem(GtkMenuItem *item, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    int code = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item),
                                                 "user-data"));

    if (inst->back)
	inst->back->special(inst->backhandle, code);
}

void about_menuitem(GtkMenuItem *item, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    about_box(inst->window);
}

void event_log_menuitem(GtkMenuItem *item, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    showeventlog(inst->eventlogstuff, inst->window);
}

void change_settings_menuitem(GtkMenuItem *item, gpointer data)
{
    /* This maps colour indices in inst->conf to those used in inst->cols. */
    static const int ww[] = {
	256, 257, 258, 259, 260, 261,
	0, 8, 1, 9, 2, 10, 3, 11,
	4, 12, 5, 13, 6, 14, 7, 15
    };
    struct gui_data *inst = (struct gui_data *)data;
    char *title;
    Conf *oldconf, *newconf;
    int i, j, need_size;

    assert(lenof(ww) == NCFGCOLOURS);

    if (inst->reconfiguring)
      return;
    else
      inst->reconfiguring = TRUE;

    title = dupcat(appname, " Reconfiguration", NULL);

    oldconf = inst->conf;
    newconf = conf_copy(inst->conf);

    if (do_config_box(title, newconf, 1,
		      inst->back?inst->back->cfg_info(inst->backhandle):0)) {
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
        /* Pass new config data to the back end */
        if (inst->back)
	    inst->back->reconfig(inst->backhandle, inst->conf);

	cache_conf_values(inst);

        /*
         * Just setting inst->conf is sufficient to cause colour
         * setting changes to appear on the next ESC]R palette
         * reset. But we should also check whether any colour
         * settings have been changed, and revert the ones that have
         * to the new default, on the assumption that the user is
         * most likely to want an immediate update.
         */
        for (i = 0; i < NCFGCOLOURS; i++) {
	    for (j = 0; j < 3; j++)
		if (conf_get_int_int(oldconf, CONF_colours, i*3+j) !=
		    conf_get_int_int(newconf, CONF_colours, i*3+j))
		    break;
	    if (j < 3) {
                real_palette_set(inst, ww[i],
				 conf_get_int_int(newconf,CONF_colours,i*3+0),
				 conf_get_int_int(newconf,CONF_colours,i*3+1),
				 conf_get_int_int(newconf,CONF_colours,i*3+2));

		/*
		 * If the default background has changed, we must
		 * repaint the space in between the window border
		 * and the text area.
		 */
		if (ww[i] == 258) {
		    set_window_background(inst);
		    draw_backing_rect(inst);
		}
	    }
        }

        /*
         * If the scrollbar needs to be shown, hidden, or moved
         * from one end to the other of the window, do so now.
         */
        if (conf_get_int(oldconf, CONF_scrollbar) !=
	    conf_get_int(newconf, CONF_scrollbar)) {
            if (conf_get_int(newconf, CONF_scrollbar))
                gtk_widget_show(inst->sbar);
            else
                gtk_widget_hide(inst->sbar);
        }
        if (conf_get_int(oldconf, CONF_scrollbar_on_left) !=
	    conf_get_int(newconf, CONF_scrollbar_on_left)) {
            gtk_box_reorder_child(inst->hbox, inst->sbar,
                                  conf_get_int(newconf, CONF_scrollbar_on_left)
				  ? 0 : 1);
        }

        /*
         * Change the window title, if required.
         */
        if (strcmp(conf_get_str(oldconf, CONF_wintitle),
		   conf_get_str(newconf, CONF_wintitle)))
            set_title(inst, conf_get_str(newconf, CONF_wintitle));
	set_window_titles(inst);

        /*
         * Redo the whole tangled fonts and Unicode mess if
         * necessary.
         */
        need_size = FALSE;
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
	    conf_get_int(oldconf, CONF_utf8_override) !=
	    conf_get_int(newconf, CONF_utf8_override) ||
	    conf_get_int(oldconf, CONF_vtmode) !=
	    conf_get_int(newconf, CONF_vtmode) ||
	    conf_get_int(oldconf, CONF_shadowbold) !=
	    conf_get_int(newconf, CONF_shadowbold) ||
	    conf_get_int(oldconf, CONF_shadowboldoffset) !=
	    conf_get_int(newconf, CONF_shadowboldoffset)) {
            char *errmsg = setup_fonts_ucs(inst);
            if (errmsg) {
                char *msgboxtext =
                    dupprintf("Could not change fonts in terminal window: %s\n",
                              errmsg);
                messagebox(inst->window, "Font setup error", msgboxtext,
                           string_width("Could not change fonts in terminal window:"),
                           FALSE, "OK", 'o', +1, 1,
                           NULL);
                sfree(msgboxtext);
                sfree(errmsg);
            } else {
                need_size = TRUE;
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
            request_resize(inst, conf_get_int(newconf, CONF_width),
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
    sfree(title);
    inst->reconfiguring = FALSE;
}

void fork_and_exec_self(struct gui_data *inst, int fd_to_close, ...)
{
    /*
     * Re-execing ourself is not an exact science under Unix. I do
     * the best I can by using /proc/self/exe if available and by
     * assuming argv[0] can be found on $PATH if not.
     * 
     * Note that we also have to reconstruct the elements of the
     * original argv which gtk swallowed, since the user wants the
     * new session to appear on the same X display as the old one.
     */
    char **args;
    va_list ap;
    int i, n;
    int pid;

    /*
     * Collect the arguments with which to re-exec ourself.
     */
    va_start(ap, fd_to_close);
    n = 2;			       /* progname and terminating NULL */
    n += inst->ngtkargs;
    while (va_arg(ap, char *) != NULL)
	n++;
    va_end(ap);

    args = snewn(n, char *);
    args[0] = inst->progname;
    args[n-1] = NULL;
    for (i = 0; i < inst->ngtkargs; i++)
	args[i+1] = inst->gtkargvstart[i];

    i++;
    va_start(ap, fd_to_close);
    while ((args[i++] = va_arg(ap, char *)) != NULL);
    va_end(ap);

    assert(i == n);

    /*
     * Do the double fork.
     */
    pid = fork();
    if (pid < 0) {
	perror("fork");
        sfree(args);
	return;
    }

    if (pid == 0) {
	int pid2 = fork();
	if (pid2 < 0) {
	    perror("fork");
	    _exit(1);
	} else if (pid2 > 0) {
	    /*
	     * First child has successfully forked second child. My
	     * Work Here Is Done. Note the use of _exit rather than
	     * exit: the latter appears to cause destroy messages
	     * to be sent to the X server. I suspect gtk uses
	     * atexit.
	     */
	    _exit(0);
	}

	/*
	 * If we reach here, we are the second child, so we now
	 * actually perform the exec.
	 */
	if (fd_to_close >= 0)
	    close(fd_to_close);

	execv("/proc/self/exe", args);
	execvp(inst->progname, args);
	perror("exec");
	_exit(127);

    } else {
	int status;
        sfree(args);
	waitpid(pid, &status, 0);
    }

}

void dup_session_menuitem(GtkMenuItem *item, gpointer gdata)
{
    struct gui_data *inst = (struct gui_data *)gdata;
    /*
     * For this feature we must marshal conf and (possibly) pty_argv
     * into a byte stream, create a pipe, and send this byte stream
     * to the child through the pipe.
     */
    int i, ret, sersize, size;
    char *data;
    char option[80];
    int pipefd[2];

    if (pipe(pipefd) < 0) {
	perror("pipe");
	return;
    }

    size = sersize = conf_serialised_size(inst->conf);
    if (use_pty_argv && pty_argv) {
	for (i = 0; pty_argv[i]; i++)
	    size += strlen(pty_argv[i]) + 1;
    }

    data = snewn(size, char);
    conf_serialise(inst->conf, data);
    if (use_pty_argv && pty_argv) {
	int p = sersize;
	for (i = 0; pty_argv[i]; i++) {
	    strcpy(data + p, pty_argv[i]);
	    p += strlen(pty_argv[i]) + 1;
	}
	assert(p == size);
    }

    sprintf(option, "---[%d,%d]", pipefd[0], size);
    noncloexec(pipefd[0]);
    fork_and_exec_self(inst, pipefd[1], option, NULL);
    close(pipefd[0]);

    i = ret = 0;
    while (i < size && (ret = write(pipefd[1], data + i, size - i)) > 0)
	i += ret;
    if (ret < 0)
	perror("write to pipe");
    close(pipefd[1]);
    sfree(data);
}

int read_dupsession_data(struct gui_data *inst, Conf *conf, char *arg)
{
    int fd, i, ret, size, size_used;
    char *data;

    if (sscanf(arg, "---[%d,%d]", &fd, &size) != 2) {
	fprintf(stderr, "%s: malformed magic argument `%s'\n", appname, arg);
	exit(1);
    }

    data = snewn(size, char);
    i = ret = 0;
    while (i < size && (ret = read(fd, data + i, size - i)) > 0)
	i += ret;
    if (ret < 0) {
	perror("read from pipe");
	exit(1);
    } else if (i < size) {
	fprintf(stderr, "%s: unexpected EOF in Duplicate Session data\n",
		appname);
	exit(1);
    }

    size_used = conf_deserialise(conf, data, size);
    if (use_pty_argv && size > size_used) {
	int n = 0;
	i = size_used;
	while (i < size) {
	    while (i < size && data[i]) i++;
	    if (i >= size) {
		fprintf(stderr, "%s: malformed Duplicate Session data\n",
			appname);
		exit(1);
	    }
	    i++;
	    n++;
	}
	pty_argv = snewn(n+1, char *);
	pty_argv[n] = NULL;
	n = 0;
	i = size_used;
	while (i < size) {
	    char *p = data + i;
	    while (i < size && data[i]) i++;
	    assert(i < size);
	    i++;
	    pty_argv[n++] = dupstr(p);
	}
    }

    sfree(data);

    return 0;
}

void new_session_menuitem(GtkMenuItem *item, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;

    fork_and_exec_self(inst, -1, NULL);
}

void restart_session_menuitem(GtkMenuItem *item, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;

    if (!inst->back) {
	logevent(inst, "----- Session restarted -----");
	term_pwron(inst->term, FALSE);
	start_backend(inst);
	inst->exited = FALSE;
    }
}

void saved_session_menuitem(GtkMenuItem *item, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    char *str = (char *)g_object_get_data(G_OBJECT(item), "user-data");

    fork_and_exec_self(inst, -1, "-load", str, NULL);
}

void saved_session_freedata(GtkMenuItem *item, gpointer data)
{
    char *str = (char *)g_object_get_data(G_OBJECT(item), "user-data");

    sfree(str);
}

static void update_savedsess_menu(GtkMenuItem *menuitem, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    struct sesslist sesslist;
    int i;

    gtk_container_foreach(GTK_CONTAINER(inst->sessionsmenu),
			  (GtkCallback)gtk_widget_destroy, NULL);

    get_sesslist(&sesslist, TRUE);
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
	gtk_widget_set_sensitive(menuitem, FALSE);
	gtk_container_add(GTK_CONTAINER(inst->sessionsmenu), menuitem);
	gtk_widget_show(menuitem);
    }
    get_sesslist(&sesslist, FALSE); /* free up */
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

void update_specials_menu(void *frontend)
{
    struct gui_data *inst = (struct gui_data *)frontend;

    const struct telnet_special *specials;

    if (inst->back)
	specials = inst->back->get_specials(inst->backhandle);
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
	      case TS_SUBMENU:
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
	      case TS_EXITMENU:
		nesting--;
		if (nesting) {
		    menu = saved_menu; /* XXX lame stacking */
		    saved_menu = NULL;
		}
		break;
	      case TS_SEP:
		menuitem = gtk_menu_item_new();
		break;
	      default:
		menuitem = gtk_menu_item_new_with_label(specials[i].name);
                g_object_set_data(G_OBJECT(menuitem), "user-data",
                                  GINT_TO_POINTER(specials[i].code));
                g_signal_connect(G_OBJECT(menuitem), "activate",
                                 G_CALLBACK(special_menuitem), inst);
		break;
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

static void start_backend(struct gui_data *inst)
{
    extern Backend *select_backend(Conf *conf);
    char *realhost;
    const char *error;
    char *s;

    inst->back = select_backend(inst->conf);

    error = inst->back->init((void *)inst, &inst->backhandle,
			     inst->conf,
			     conf_get_str(inst->conf, CONF_host),
			     conf_get_int(inst->conf, CONF_port),
			     &realhost,
			     conf_get_int(inst->conf, CONF_tcp_nodelay),
			     conf_get_int(inst->conf, CONF_tcp_keepalives));

    if (error) {
	char *msg = dupprintf("Unable to open connection to %s:\n%s",
			      conf_get_str(inst->conf, CONF_host), error);
	inst->exited = TRUE;
	fatal_message_box(inst->window, msg);
	sfree(msg);
	exit(0);
    }

    s = conf_get_str(inst->conf, CONF_wintitle);
    if (s[0]) {
	set_title_and_icon(inst, s, s);
    } else {
	char *title = make_default_wintitle(realhost);
	set_title_and_icon(inst, title, title);
	sfree(title);
    }
    sfree(realhost);

    inst->back->provide_logctx(inst->backhandle, inst->logctx);

    term_provide_resize_fn(inst->term, inst->back->size, inst->backhandle);

    inst->ldisc =
	ldisc_create(inst->conf, inst->term, inst->back, inst->backhandle,
		     inst);

    gtk_widget_set_sensitive(inst->restartitem, FALSE);
}

int pt_main(int argc, char **argv)
{
    extern int cfgbox(Conf *conf);
    struct gui_data *inst;

    setlocale(LC_CTYPE, "");

    /*
     * Create an instance structure and initialise to zeroes
     */
    inst = snew(struct gui_data);
    memset(inst, 0, sizeof(*inst));
    inst->alt_keycode = -1;            /* this one needs _not_ to be zero */
    inst->busy_status = BUSY_NOT;
    inst->conf = conf_new();
    inst->wintitle = inst->icontitle = NULL;
    inst->quit_fn_scheduled = FALSE;
    inst->idle_fn_scheduled = FALSE;
    inst->drawtype = DRAWTYPE_DEFAULT;
#if GTK_CHECK_VERSION(3,4,0)
    inst->cumulative_scroll = 0.0;
#endif

    /* defer any child exit handling until we're ready to deal with
     * it */
    block_signal(SIGCHLD, 1);

    inst->progname = argv[0];
    /*
     * Copy the original argv before letting gtk_init fiddle with
     * it. It will be required later.
     */
    {
	int i, oldargc;
	inst->gtkargvstart = snewn(argc-1, char *);
	for (i = 1; i < argc; i++)
	    inst->gtkargvstart[i-1] = dupstr(argv[i]);
	oldargc = argc;
	gtk_init(&argc, &argv);
	inst->ngtkargs = oldargc - argc;
    }

    if (argc > 1 && !strncmp(argv[1], "---", 3)) {
	read_dupsession_data(inst, inst->conf, argv[1]);
	/* Splatter this argument so it doesn't clutter a ps listing */
	smemclr(argv[1], strlen(argv[1]));
    } else {
	/* By default, we bring up the config dialog, rather than launching
	 * a session. This gets set to TRUE if something happens to change
	 * that (e.g., a hostname is specified on the command-line). */
	int allow_launch = FALSE;
	if (do_cmdline(argc, argv, 0, &allow_launch, inst, inst->conf))
	    exit(1);		       /* pre-defaults pass to get -class */
	do_defaults(NULL, inst->conf);
	if (do_cmdline(argc, argv, 1, &allow_launch, inst, inst->conf))
	    exit(1);		       /* post-defaults, do everything */

	cmdline_run_saved(inst->conf);

	if (loaded_session)
	    allow_launch = TRUE;

	if ((!allow_launch || !conf_launchable(inst->conf)) &&
	    !cfgbox(inst->conf))
	    exit(0);		       /* config box hit Cancel */
    }

    if (!compound_text_atom)
        compound_text_atom = gdk_atom_intern("COMPOUND_TEXT", FALSE);
    if (!utf8_string_atom)
        utf8_string_atom = gdk_atom_intern("UTF8_STRING", FALSE);

    inst->area = gtk_drawing_area_new();

#if GTK_CHECK_VERSION(2,0,0)
    inst->imc = gtk_im_multicontext_new();
#endif

    {
        char *errmsg = setup_fonts_ucs(inst);
        if (errmsg) {
            fprintf(stderr, "%s: %s\n", appname, errmsg);
            exit(1);
        }
    }
    inst->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    {
        const char *winclass = conf_get_str(inst->conf, CONF_winclass);
        if (*winclass)
            gtk_window_set_wmclass(GTK_WINDOW(inst->window),
                                   winclass, winclass);
    }

    /*
     * Set up the colour map.
     */
    palette_reset(inst);

    inst->width = conf_get_int(inst->conf, CONF_width);
    inst->height = conf_get_int(inst->conf, CONF_height);
    cache_conf_values(inst);

    init_clipboard(inst);

    set_geom_hints(inst);

#if GTK_CHECK_VERSION(3,0,0)
    gtk_window_set_default_geometry(GTK_WINDOW(inst->window),
                                    inst->width, inst->height);
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

    inst->sbar_adjust = GTK_ADJUSTMENT(gtk_adjustment_new(0,0,0,0,0,0));
    inst->sbar = gtk_vscrollbar_new(inst->sbar_adjust);
    inst->hbox = GTK_BOX(gtk_hbox_new(FALSE, 0));
    /*
     * We always create the scrollbar; it remains invisible if
     * unwanted, so we can pop it up quickly if it suddenly becomes
     * desirable.
     */
    if (conf_get_int(inst->conf, CONF_scrollbar_on_left))
        gtk_box_pack_start(inst->hbox, inst->sbar, FALSE, FALSE, 0);
    gtk_box_pack_start(inst->hbox, inst->area, TRUE, TRUE, 0);
    if (!conf_get_int(inst->conf, CONF_scrollbar_on_left))
        gtk_box_pack_start(inst->hbox, inst->sbar, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(inst->window), GTK_WIDGET(inst->hbox));

    gtk_widget_show(inst->area);
    if (conf_get_int(inst->conf, CONF_scrollbar))
	gtk_widget_show(inst->sbar);
    else
	gtk_widget_hide(inst->sbar);
    gtk_widget_show(GTK_WIDGET(inst->hbox));

#if GTK_CHECK_VERSION(2,0,0)
    if (inst->geometry) {
        gtk_window_parse_geometry(GTK_WINDOW(inst->window), inst->geometry);
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
    g_signal_connect(G_OBJECT(inst->area), "configure_event",
                     G_CALLBACK(configure_area), inst);
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
    if (conf_get_int(inst->conf, CONF_scrollbar))
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

    {
	extern const char *const *const main_icon[];
	extern const int n_main_icon;
	set_window_icon(inst->window, main_icon, n_main_icon);
    }

    gtk_widget_show(inst->window);

    set_window_background(inst);

    /*
     * Set up the Ctrl+rightclick context menu.
     */
    {
	GtkWidget *menuitem;
	char *s;
	extern const int use_event_log, new_session, saved_sessions;

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
	gtk_widget_set_sensitive(inst->restartitem, FALSE);
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
	MKMENUITEM("Copy All", copy_all_menuitem);
	MKSEP();
	s = dupcat("About ", appname, NULL);
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
    show_mouseptr(inst, 1);

    inst->eventlogstuff = eventlogstuff_new();

    request_callback_notifications(notify_toplevel_callback, inst);

    inst->term = term_init(inst->conf, &inst->ucsdata, inst);
    inst->logctx = log_init(inst, inst->conf);
    term_provide_logctx(inst->term, inst->logctx);

    uxsel_init();

    term_size(inst->term, inst->height, inst->width,
	      conf_get_int(inst->conf, CONF_savelines));

    start_backend(inst);

    ldisc_echoedit_update(inst->ldisc);     /* cause ldisc to notice changes */

    /* now we're reday to deal with the child exit handler being
     * called */
    block_signal(SIGCHLD, 0);

    /*
     * Block SIGPIPE: if we attempt Duplicate Session or similar
     * and it falls over in some way, we certainly don't want
     * SIGPIPE terminating the main pterm/PuTTY. Note that we do
     * this _after_ (at least pterm) forks off its child process,
     * since the child wants SIGPIPE handled in the usual way.
     */
    block_signal(SIGPIPE, 1);

    inst->exited = FALSE;

    gtk_main();

    return 0;
}
