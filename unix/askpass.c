/*
 * GTK implementation of a GUI password/passphrase prompt.
 */

#include <assert.h>
#include <time.h>
#include <stdlib.h>

#include <unistd.h>

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#if !GTK_CHECK_VERSION(3,0,0)
#include <gdk/gdkkeysyms.h>
#endif

#include "defs.h"
#include "unifont.h"
#include "gtkcompat.h"
#include "gtkmisc.h"

#include "putty.h"
#include "ssh.h"
#include "misc.h"

#define N_DRAWING_AREAS 3

struct drawing_area_ctx {
    GtkWidget *area;
#ifndef DRAW_DEFAULT_CAIRO
    GdkColor *cols;
#endif
    int width, height;
    enum { NOT_CURRENT, CURRENT, GREYED_OUT } state;
};

struct askpass_ctx {
    GtkWidget *dialog, *promptlabel;
    struct drawing_area_ctx drawingareas[N_DRAWING_AREAS];
    int active_area;
#if GTK_CHECK_VERSION(2,0,0)
    GtkIMContext *imc;
#endif
#ifndef DRAW_DEFAULT_CAIRO
    GdkColormap *colmap;
    GdkColor cols[3];
#endif
    char *error_message;               /* if we finish without a passphrase */
    char *passphrase;                  /* if we finish with one */
    int passlen, passsize;
#if GTK_CHECK_VERSION(3,20,0)
    GdkSeat *seat;                     /* for gdk_seat_grab */
#elif GTK_CHECK_VERSION(3,0,0)
    GdkDevice *keyboard;               /* for gdk_device_grab */
#endif

    int nattempts;
};

static prng *keypress_prng = NULL;
static void feed_keypress_prng(void *data, int size)
{
    put_data(keypress_prng, data, size);
}
void random_add_noise(NoiseSourceId source, const void *noise, int length)
{
    if (keypress_prng)
        prng_add_entropy(keypress_prng, source, make_ptrlen(noise, length));
}
static void setup_keypress_prng(void)
{
    keypress_prng = prng_new(&ssh_sha256);
    prng_seed_begin(keypress_prng);
    noise_get_heavy(feed_keypress_prng);
    prng_seed_finish(keypress_prng);
}
static void cleanup_keypress_prng(void)
{
    prng_free(keypress_prng);
}
static uint64_t keypress_prng_value(void)
{
    /*
     * Don't actually put the passphrase keystrokes themselves into
     * the PRNG; that doesn't seem like the course of wisdom when
     * that's precisely what the information displayed on the screen
     * is trying _not_ to be correlated to.
     */
    noise_ultralight(NOISE_SOURCE_KEY, 0);
    uint8_t data[8];
    prng_read(keypress_prng, data, 8);
    return GET_64BIT_MSB_FIRST(data);
}
static int choose_new_area(int prev_area)
{
    int reduced = keypress_prng_value() % (N_DRAWING_AREAS - 1);
    return (prev_area + 1 + reduced) % N_DRAWING_AREAS;
}

static void visually_acknowledge_keypress(struct askpass_ctx *ctx)
{
    int new_active = choose_new_area(ctx->active_area);
    ctx->drawingareas[ctx->active_area].state = NOT_CURRENT;
    gtk_widget_queue_draw(ctx->drawingareas[ctx->active_area].area);
    ctx->drawingareas[new_active].state = CURRENT;
    gtk_widget_queue_draw(ctx->drawingareas[new_active].area);
    ctx->active_area = new_active;
}

static int last_char_len(struct askpass_ctx *ctx)
{
    /*
     * GTK always encodes in UTF-8, so we can do this in a fixed way.
     */
    int i;
    assert(ctx->passlen > 0);
    i = ctx->passlen - 1;
    while ((unsigned)((unsigned char)ctx->passphrase[i] - 0x80) < 0x40) {
        if (i == 0)
            break;
        i--;
    }
    return ctx->passlen - i;
}

static void add_text_to_passphrase(struct askpass_ctx *ctx, gchar *str)
{
    int len = strlen(str);
    if (ctx->passlen + len >= ctx->passsize) {
        /* Take some care with buffer expansion, because there are
         * pieces of passphrase in the old buffer so we should ensure
         * realloc doesn't leave a copy lying around in the address
         * space. */
        int oldsize = ctx->passsize;
        char *newbuf;

        ctx->passsize = (ctx->passlen + len) * 5 / 4 + 1024;
        newbuf = snewn(ctx->passsize, char);
        memcpy(newbuf, ctx->passphrase, oldsize);
        smemclr(ctx->passphrase, oldsize);
        sfree(ctx->passphrase);
        ctx->passphrase = newbuf;
    }
    strcpy(ctx->passphrase + ctx->passlen, str);
    ctx->passlen += len;
    visually_acknowledge_keypress(ctx);
}

static void cancel_askpass(struct askpass_ctx *ctx, const char *msg)
{
    smemclr(ctx->passphrase, ctx->passsize);
    ctx->passphrase = NULL;
    ctx->error_message = dupstr(msg);
    gtk_main_quit();
}

static gboolean askpass_dialog_closed(GtkWidget *widget, GdkEvent *event,
                                      gpointer data)
{
    struct askpass_ctx *ctx = (struct askpass_ctx *)data;
    cancel_askpass(ctx, "passphrase input cancelled");
    /* Don't destroy dialog yet, so gtk_askpass_cleanup() can do its work */
    return true;
}

static gint key_event(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    struct askpass_ctx *ctx = (struct askpass_ctx *)data;

    if (event->keyval == GDK_KEY_Return &&
        event->type == GDK_KEY_PRESS) {
        gtk_main_quit();
    } else if (event->keyval == GDK_KEY_Escape &&
               event->type == GDK_KEY_PRESS) {
        cancel_askpass(ctx, "passphrase input cancelled");
    } else {
#if GTK_CHECK_VERSION(2,0,0)
        if (gtk_im_context_filter_keypress(ctx->imc, event))
            return true;
#endif

        if (event->type == GDK_KEY_PRESS) {
            if (!strcmp(event->string, "\x15")) {
                /* Ctrl-U. Wipe out the whole line */
                ctx->passlen = 0;
                visually_acknowledge_keypress(ctx);
            } else if (!strcmp(event->string, "\x17")) {
                /* Ctrl-W. Delete back to the last space->nonspace
                 * boundary. We interpret 'space' in a really simple
                 * way (mimicking terminal drivers), and don't attempt
                 * to second-guess exciting Unicode space
                 * characters. */
                while (ctx->passlen > 0) {
                    char deleted, prior;
                    ctx->passlen -= last_char_len(ctx);
                    deleted = ctx->passphrase[ctx->passlen];
                    prior = (ctx->passlen == 0 ? ' ' :
                             ctx->passphrase[ctx->passlen-1]);
                    if (!g_ascii_isspace(deleted) && g_ascii_isspace(prior))
                        break;
                }
                visually_acknowledge_keypress(ctx);
            } else if (event->keyval == GDK_KEY_BackSpace) {
                /* Backspace. Delete one character. */
                if (ctx->passlen > 0)
                    ctx->passlen -= last_char_len(ctx);
                visually_acknowledge_keypress(ctx);
#if !GTK_CHECK_VERSION(2,0,0)
            } else if (event->string[0]) {
                add_text_to_passphrase(ctx, event->string);
#endif
            }
        }
    }
    return true;
}

#if GTK_CHECK_VERSION(2,0,0)
static void input_method_commit_event(GtkIMContext *imc, gchar *str,
                                      gpointer data)
{
    struct askpass_ctx *ctx = (struct askpass_ctx *)data;
    add_text_to_passphrase(ctx, str);
}
#endif

static gint configure_area(GtkWidget *widget, GdkEventConfigure *event,
                           gpointer data)
{
    struct drawing_area_ctx *ctx = (struct drawing_area_ctx *)data;
    ctx->width = event->width;
    ctx->height = event->height;
    gtk_widget_queue_draw(widget);
    return true;
}

#ifdef DRAW_DEFAULT_CAIRO
static void askpass_redraw_cairo(cairo_t *cr, struct drawing_area_ctx *ctx)
{
    double rgbval = (ctx->state == CURRENT ? 0 :
                     ctx->state == NOT_CURRENT ? 1 : 0.5);
    cairo_set_source_rgb(cr, rgbval, rgbval, rgbval);
    cairo_paint(cr);
}
#else
static void askpass_redraw_gdk(GdkWindow *win, struct drawing_area_ctx *ctx)
{
    GdkGC *gc = gdk_gc_new(win);
    gdk_gc_set_foreground(gc, &ctx->cols[ctx->state]);
    gdk_draw_rectangle(win, gc, true, 0, 0, ctx->width, ctx->height);
    gdk_gc_unref(gc);
}
#endif

#if GTK_CHECK_VERSION(3,0,0)
static gint draw_area(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    struct drawing_area_ctx *ctx = (struct drawing_area_ctx *)data;
    askpass_redraw_cairo(cr, ctx);
    return true;
}
#else
static gint expose_area(GtkWidget *widget, GdkEventExpose *event,
                        gpointer data)
{
    struct drawing_area_ctx *ctx = (struct drawing_area_ctx *)data;

#ifdef DRAW_DEFAULT_CAIRO
    cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(ctx->area));
    askpass_redraw_cairo(cr, ctx);
    cairo_destroy(cr);
#else
    askpass_redraw_gdk(gtk_widget_get_window(ctx->area), ctx);
#endif

    return true;
}
#endif

static gboolean try_grab_keyboard(gpointer vctx)
{
    struct askpass_ctx *ctx = (struct askpass_ctx *)vctx;
    int i, ret;

#if GTK_CHECK_VERSION(3,20,0)
    /*
     * Grabbing the keyboard in GTK 3.20 requires the new notion of
     * GdkSeat.
     */
    GdkSeat *seat;
    GdkWindow *gdkw = gtk_widget_get_window(ctx->dialog);
    if (!GDK_IS_WINDOW(gdkw) || !gdk_window_is_visible(gdkw))
        goto fail;

    seat = gdk_display_get_default_seat(
        gtk_widget_get_display(ctx->dialog));
    if (!seat)
        goto fail;

    ctx->seat = seat;
    ret = gdk_seat_grab(seat, gdkw, GDK_SEAT_CAPABILITY_KEYBOARD,
                        true, NULL, NULL, NULL, NULL);

    /*
     * For some reason GDK 3.22 hides the GDK window as a side effect
     * of a failed grab. I've no idea why. But if we're going to retry
     * the grab, then we need to unhide it again or else we'll just
     * get GDK_GRAB_NOT_VIEWABLE on every subsequent attempt.
     */
    if (ret != GDK_GRAB_SUCCESS)
        gdk_window_show(gdkw);

#elif GTK_CHECK_VERSION(3,0,0)
    /*
     * And it has to be done differently again prior to GTK 3.20.
     */
    GdkDeviceManager *dm;
    GdkDevice *pointer, *keyboard;

    dm = gdk_display_get_device_manager(
        gtk_widget_get_display(ctx->dialog));
    if (!dm)
        goto fail;

    pointer = gdk_device_manager_get_client_pointer(dm);
    if (!pointer)
        goto fail;
    keyboard = gdk_device_get_associated_device(pointer);
    if (!keyboard)
        goto fail;
    if (gdk_device_get_source(keyboard) != GDK_SOURCE_KEYBOARD)
        goto fail;

    ctx->keyboard = keyboard;
    ret = gdk_device_grab(ctx->keyboard,
                          gtk_widget_get_window(ctx->dialog),
                          GDK_OWNERSHIP_NONE,
                          true,
                          GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK,
                          NULL,
                          GDK_CURRENT_TIME);
#else
    /*
     * It's much simpler in GTK 1 and 2!
     */
    ret = gdk_keyboard_grab(gtk_widget_get_window(ctx->dialog),
                            false, GDK_CURRENT_TIME);
#endif
    if (ret != GDK_GRAB_SUCCESS)
        goto fail;

    /*
     * Now that we've got the keyboard grab, connect up our keyboard
     * handlers.
     */
#if GTK_CHECK_VERSION(2,0,0)
    g_signal_connect(G_OBJECT(ctx->imc), "commit",
                     G_CALLBACK(input_method_commit_event), ctx);
#endif
    g_signal_connect(G_OBJECT(ctx->dialog), "key_press_event",
                     G_CALLBACK(key_event), ctx);
    g_signal_connect(G_OBJECT(ctx->dialog), "key_release_event",
                     G_CALLBACK(key_event), ctx);
#if GTK_CHECK_VERSION(2,0,0)
    gtk_im_context_set_client_window(ctx->imc,
                                     gtk_widget_get_window(ctx->dialog));
#endif

    /*
     * And repaint the key-acknowledgment drawing areas as not greyed
     * out.
     */
    ctx->active_area = keypress_prng_value() % N_DRAWING_AREAS;
    for (i = 0; i < N_DRAWING_AREAS; i++) {
        ctx->drawingareas[i].state =
            (i == ctx->active_area ? CURRENT : NOT_CURRENT);
        gtk_widget_queue_draw(ctx->drawingareas[i].area);
    }

    return false;

  fail:
    /*
     * If we didn't get the grab, reschedule ourself on a timer to try
     * again later.
     *
     * We have to do this rather than just trying once, because there
     * is at least one important situation in which the grab may fail
     * the first time: any user who is launching an add-key operation
     * off some kind of window manager hotkey will almost by
     * definition be running this script with a keyboard grab already
     * active, namely the one-key grab that the WM (or whatever) uses
     * to detect presses of the hotkey. So at the very least we have
     * to give the user time to release that key.
     */
    if (++ctx->nattempts >= 4) {
        cancel_askpass(ctx, "unable to grab keyboard after 5 seconds");
    } else {
        g_timeout_add(1000/8, try_grab_keyboard, ctx);
    }
    return false;
}

void realize(GtkWidget *widget, gpointer vctx)
{
    struct askpass_ctx *ctx = (struct askpass_ctx *)vctx;

    gtk_grab_add(ctx->dialog);

    /*
     * Schedule the first attempt at the keyboard grab.
     */
    ctx->nattempts = 0;
#if GTK_CHECK_VERSION(3,20,0)
    ctx->seat = NULL;
#elif GTK_CHECK_VERSION(3,0,0)
    ctx->keyboard = NULL;
#endif

    g_idle_add(try_grab_keyboard, ctx);
}

static const char *gtk_askpass_setup(struct askpass_ctx *ctx,
                                     const char *window_title,
                                     const char *prompt_text)
{
    int i;
    GtkBox *action_area;

    ctx->passlen = 0;
    ctx->passsize = 2048;
    ctx->passphrase = snewn(ctx->passsize, char);

    /*
     * Create widgets.
     */
    ctx->dialog = our_dialog_new();
    gtk_window_set_title(GTK_WINDOW(ctx->dialog), window_title);
    gtk_window_set_position(GTK_WINDOW(ctx->dialog), GTK_WIN_POS_CENTER);
    g_signal_connect(G_OBJECT(ctx->dialog), "delete-event",
                     G_CALLBACK(askpass_dialog_closed), ctx);
    ctx->promptlabel = gtk_label_new(prompt_text);
    align_label_left(GTK_LABEL(ctx->promptlabel));
    gtk_widget_show(ctx->promptlabel);
    gtk_label_set_line_wrap(GTK_LABEL(ctx->promptlabel), true);
#if GTK_CHECK_VERSION(3,0,0)
    gtk_label_set_width_chars(GTK_LABEL(ctx->promptlabel), 48);
#endif
    int margin = string_width("MM");
#if GTK_CHECK_VERSION(3,12,0)
    gtk_widget_set_margin_start(ctx->promptlabel, margin);
    gtk_widget_set_margin_end(ctx->promptlabel, margin);
#else
    gtk_misc_set_padding(GTK_MISC(ctx->promptlabel), margin, 0);
#endif
    our_dialog_add_to_content_area(GTK_WINDOW(ctx->dialog),
                                   ctx->promptlabel, true, true, 0);
#if GTK_CHECK_VERSION(2,0,0)
    ctx->imc = gtk_im_multicontext_new();
#endif
#ifndef DRAW_DEFAULT_CAIRO
    {
        gboolean success[2];
        ctx->colmap = gdk_colormap_get_system();
        ctx->cols[0].red = ctx->cols[0].green = ctx->cols[0].blue = 0xFFFF;
        ctx->cols[1].red = ctx->cols[1].green = ctx->cols[1].blue = 0;
        ctx->cols[2].red = ctx->cols[2].green = ctx->cols[2].blue = 0x8000;
        gdk_colormap_alloc_colors(ctx->colmap, ctx->cols, 2,
                                  false, true, success);
        if (!success[0] || !success[1])
            return "unable to allocate colours";
    }
#endif

    action_area = our_dialog_make_action_hbox(GTK_WINDOW(ctx->dialog));

    for (i = 0; i < N_DRAWING_AREAS; i++) {
        ctx->drawingareas[i].area = gtk_drawing_area_new();
#ifndef DRAW_DEFAULT_CAIRO
        ctx->drawingareas[i].cols = ctx->cols;
#endif
        ctx->drawingareas[i].state = GREYED_OUT;
        ctx->drawingareas[i].width = ctx->drawingareas[i].height = 0;
        /* It would be nice to choose this size in some more
         * context-sensitive way, like measuring the size of some
         * piece of template text. */
        gtk_widget_set_size_request(ctx->drawingareas[i].area, 32, 32);
        gtk_box_pack_end(action_area, ctx->drawingareas[i].area,
                         true, true, 5);
        g_signal_connect(G_OBJECT(ctx->drawingareas[i].area),
                         "configure_event",
                         G_CALLBACK(configure_area),
                         &ctx->drawingareas[i]);
#if GTK_CHECK_VERSION(3,0,0)
        g_signal_connect(G_OBJECT(ctx->drawingareas[i].area),
                         "draw",
                         G_CALLBACK(draw_area),
                         &ctx->drawingareas[i]);
#else
        g_signal_connect(G_OBJECT(ctx->drawingareas[i].area),
                         "expose_event",
                         G_CALLBACK(expose_area),
                         &ctx->drawingareas[i]);
#endif

#if GTK_CHECK_VERSION(3,0,0)
        g_object_set(G_OBJECT(ctx->drawingareas[i].area),
                     "margin-bottom", 8, (const char *)NULL);
#endif

        gtk_widget_show(ctx->drawingareas[i].area);
    }
    ctx->active_area = -1;

    /*
     * Arrange to receive key events. We don't really need to worry
     * from a UI perspective about which widget gets the events, as
     * long as we know which it is so we can catch them. So we'll pick
     * the prompt label at random, and we'll use gtk_grab_add to
     * ensure key events go to it.
     */
    gtk_widget_set_sensitive(ctx->dialog, true);

#if GTK_CHECK_VERSION(2,0,0)
    gtk_window_set_keep_above(GTK_WINDOW(ctx->dialog), true);
#endif

    /*
     * Wait for the key-receiving widget to actually be created, in
     * order to call gtk_grab_add on it.
     */
    g_signal_connect(G_OBJECT(ctx->dialog), "realize",
                     G_CALLBACK(realize), ctx);

    /*
     * Show the window.
     */
    gtk_widget_show(ctx->dialog);

    return NULL;
}

static void gtk_askpass_cleanup(struct askpass_ctx *ctx)
{
#if GTK_CHECK_VERSION(3,20,0)
    if (ctx->seat)
        gdk_seat_ungrab(ctx->seat);
#elif GTK_CHECK_VERSION(3,0,0)
    if (ctx->keyboard)
        gdk_device_ungrab(ctx->keyboard, GDK_CURRENT_TIME);
#else
    gdk_keyboard_ungrab(GDK_CURRENT_TIME);
#endif
    gtk_grab_remove(ctx->promptlabel);

    if (ctx->passphrase) {
        assert(ctx->passlen < ctx->passsize);
        ctx->passphrase[ctx->passlen] = '\0';
    }

    gtk_widget_destroy(ctx->dialog);
}

static bool setup_gtk(const char *display)
{
    static bool gtk_initialised = false;
    int argc;
    char *real_argv[3];
    char **argv = real_argv;
    bool ret;

    if (gtk_initialised)
        return true;

    argc = 0;
    argv[argc++] = dupstr("dummy");
    argv[argc++] = dupprintf("--display=%s", display);
    argv[argc] = NULL;
    ret = gtk_init_check(&argc, &argv);
    while (argc > 0)
        sfree(argv[--argc]);

    gtk_initialised = ret;
    return ret;
}

const bool buildinfo_gtk_relevant = true;

char *gtk_askpass_main(const char *display, const char *wintitle,
                       const char *prompt, bool *success)
{
    struct askpass_ctx ctx[1];
    const char *err;

    ctx->passphrase = NULL;
    ctx->error_message = NULL;

    /* In case gtk_init hasn't been called yet by the program */
    if (!setup_gtk(display)) {
        *success = false;
        return dupstr("unable to initialise GTK");
    }

    if ((err = gtk_askpass_setup(ctx, wintitle, prompt)) != NULL) {
        *success = false;
        return dupprintf("%s", err);
    }
    setup_keypress_prng();
    gtk_main();
    cleanup_keypress_prng();
    gtk_askpass_cleanup(ctx);

    if (ctx->passphrase) {
        *success = true;
        return ctx->passphrase;
    } else {
        *success = false;
        return ctx->error_message;
    }
}

#ifdef TEST_ASKPASS
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

int main(int argc, char **argv)
{
    bool success;
    int exitcode;
    char *ret;

    gtk_init(&argc, &argv);

    if (argc != 2) {
        success = false;
        ret = dupprintf("usage: %s <prompt text>", argv[0]);
    } else {
        ret = gtk_askpass_main(NULL, "Enter passphrase", argv[1], &success);
    }

    if (!success) {
        fputs(ret, stderr);
        fputc('\n', stderr);
        exitcode = 1;
    } else {
        fputs(ret, stdout);
        fputc('\n', stdout);
        exitcode = 0;
    }

    smemclr(ret, strlen(ret));
    return exitcode;
}
#endif
