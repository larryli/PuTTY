/*
 * GTK implementation of a GUI password/passphrase prompt.
 */

#include <assert.h>
#include <time.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "misc.h"

#define N_DRAWING_AREAS 3

struct drawing_area_ctx {
    GtkWidget *area;
    GdkColor *cols;
    int width, height, current;
};

struct askpass_ctx {
    GtkWidget *dialog, *promptlabel;
    struct drawing_area_ctx drawingareas[N_DRAWING_AREAS];
    int active_area;
    GtkIMContext *imc;
    GdkColormap *colmap;
    GdkColor cols[2];
    char *passphrase;
    int passlen, passsize;
};

static void visually_acknowledge_keypress(struct askpass_ctx *ctx)
{
    int new_active;
    new_active = rand() % (N_DRAWING_AREAS - 1);
    if (new_active >= ctx->active_area)
        new_active++;
    ctx->drawingareas[ctx->active_area].current = 0;
    gtk_widget_queue_draw(ctx->drawingareas[ctx->active_area].area);
    ctx->drawingareas[new_active].current = 1;
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

static gint key_event(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    struct askpass_ctx *ctx = (struct askpass_ctx *)data;
    if (event->keyval == GDK_Return && event->type == GDK_KEY_PRESS) {
        gtk_main_quit();
    } else if (event->keyval == GDK_Escape && event->type == GDK_KEY_PRESS) {
        smemclr(ctx->passphrase, ctx->passsize);
        ctx->passphrase = NULL;
        gtk_main_quit();
    } else {
        if (gtk_im_context_filter_keypress(ctx->imc, event))
            return TRUE;

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
            } else if (event->keyval == GDK_BackSpace) {
                /* Backspace. Delete one character. */
                if (ctx->passlen > 0)
                    ctx->passlen -= last_char_len(ctx);
                visually_acknowledge_keypress(ctx);
            }
        }
    }
    return TRUE;
}

static void input_method_commit_event(GtkIMContext *imc, gchar *str,
                                      gpointer data)
{
    struct askpass_ctx *ctx = (struct askpass_ctx *)data;
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

static gint configure_area(GtkWidget *widget, GdkEventConfigure *event,
                           gpointer data)
{
    struct drawing_area_ctx *ctx = (struct drawing_area_ctx *)data;
    ctx->width = event->width;
    ctx->height = event->height;
    gtk_widget_queue_draw(widget);
    return TRUE;
}

static gint expose_area(GtkWidget *widget, GdkEventExpose *event,
                        gpointer data)
{
    struct drawing_area_ctx *ctx = (struct drawing_area_ctx *)data;

    GdkGC *gc = gdk_gc_new(ctx->area->window);
    gdk_gc_set_foreground(gc, &ctx->cols[ctx->current]);
    gdk_draw_rectangle(widget->window, gc, TRUE,
                       0, 0, ctx->width, ctx->height);
    gdk_gc_unref(gc);
    return TRUE;
}

static int try_grab_keyboard(struct askpass_ctx *ctx)
{
    int ret = gdk_keyboard_grab(ctx->dialog->window, FALSE, GDK_CURRENT_TIME);
    return ret == GDK_GRAB_SUCCESS;
}

typedef int (try_grab_fn_t)(struct askpass_ctx *ctx);

static int repeatedly_try_grab(struct askpass_ctx *ctx, try_grab_fn_t fn)
{
    /*
     * Repeatedly try to grab some aspect of the X server. We have to
     * do this rather than just trying once, because there is at least
     * one important situation in which the grab may fail the first
     * time: any user who is launching an add-key operation off some
     * kind of window manager hotkey will almost by definition be
     * running this script with a keyboard grab already active, namely
     * the one-key grab that the WM (or whatever) uses to detect
     * presses of the hotkey. So at the very least we have to give the
     * user time to release that key.
     */
    const useconds_t ms_limit = 5*1000000;  /* try for 5 seconds */
    const useconds_t ms_step = 1000000/8;   /* at 1/8 second intervals */
    useconds_t ms;

    for (ms = 0; ms < ms_limit; ms += ms_step) {
        if (fn(ctx))
            return TRUE;
        usleep(ms_step);
    }
    return FALSE;
}

static const char *gtk_askpass_setup(struct askpass_ctx *ctx,
                                     const char *window_title,
                                     const char *prompt_text)
{
    int i;
    gboolean success[2];

    ctx->passlen = 0;
    ctx->passsize = 2048;
    ctx->passphrase = snewn(ctx->passsize, char);

    /*
     * Create widgets.
     */
    ctx->dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(ctx->dialog), window_title);
    ctx->promptlabel = gtk_label_new(prompt_text);
    gtk_label_set_line_wrap(GTK_LABEL(ctx->promptlabel), TRUE);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area
                                    (GTK_DIALOG(ctx->dialog))),
                      ctx->promptlabel);
    ctx->imc = gtk_im_multicontext_new();
    ctx->colmap = gdk_colormap_get_system();
    ctx->cols[0].red = ctx->cols[0].green = ctx->cols[0].blue = 0xFFFF;
    ctx->cols[1].red = ctx->cols[1].green = ctx->cols[1].blue = 0;
    gdk_colormap_alloc_colors(ctx->colmap, ctx->cols, 2,
                              FALSE, TRUE, success);
    if (!success[0] | !success[1])
        return "unable to allocate colours";
    for (i = 0; i < N_DRAWING_AREAS; i++) {
        ctx->drawingareas[i].area = gtk_drawing_area_new();
        ctx->drawingareas[i].cols = ctx->cols;
        ctx->drawingareas[i].current = 0;
        ctx->drawingareas[i].width = ctx->drawingareas[i].height = 0;
        /* It would be nice to choose this size in some more
         * context-sensitive way, like measuring the size of some
         * piece of template text. */
        gtk_widget_set_size_request(ctx->drawingareas[i].area, 32, 32);
        gtk_container_add(GTK_CONTAINER(gtk_dialog_get_action_area
                                        (GTK_DIALOG(ctx->dialog))),
                          ctx->drawingareas[i].area);
        gtk_signal_connect(GTK_OBJECT(ctx->drawingareas[i].area),
                           "configure_event",
                           GTK_SIGNAL_FUNC(configure_area),
                           &ctx->drawingareas[i]);
        gtk_signal_connect(GTK_OBJECT(ctx->drawingareas[i].area),
                           "expose_event",
                           GTK_SIGNAL_FUNC(expose_area),
                           &ctx->drawingareas[i]);
        gtk_widget_show(ctx->drawingareas[i].area);
    }
    ctx->active_area = rand() % N_DRAWING_AREAS;
    ctx->drawingareas[ctx->active_area].current = 1;

    /*
     * Arrange to receive key events. We don't really need to worry
     * from a UI perspective about which widget gets the events, as
     * long as we know which it is so we can catch them. So we'll pick
     * the prompt label at random, and we'll use gtk_grab_add to
     * ensure key events go to it.
     */
    gtk_widget_set_sensitive(ctx->promptlabel, TRUE);
    gtk_window_set_keep_above(GTK_WINDOW(ctx->dialog), TRUE);

    /*
     * Actually show the window, and wait for it to be shown.
     */
    gtk_widget_show_now(ctx->dialog);

    /*
     * Now that the window is displayed, make it grab the input focus.
     */
    gtk_grab_add(ctx->promptlabel);
    if (!repeatedly_try_grab(ctx, try_grab_keyboard))
        return "unable to grab keyboard";

    /*
     * And now that we've got the keyboard grab, connect up our
     * keyboard handlers, and display the prompt.
     */
    g_signal_connect(G_OBJECT(ctx->imc), "commit",
                     G_CALLBACK(input_method_commit_event), ctx);
    gtk_signal_connect(GTK_OBJECT(ctx->promptlabel), "key_press_event",
		       GTK_SIGNAL_FUNC(key_event), ctx);
    gtk_signal_connect(GTK_OBJECT(ctx->promptlabel), "key_release_event",
		       GTK_SIGNAL_FUNC(key_event), ctx);
    gtk_im_context_set_client_window(ctx->imc, ctx->dialog->window);
    gtk_widget_show(ctx->promptlabel);

    return NULL;
}

static void gtk_askpass_cleanup(struct askpass_ctx *ctx)
{
    gdk_keyboard_ungrab(GDK_CURRENT_TIME);
    gtk_grab_remove(ctx->promptlabel);

    if (ctx->passphrase) {
        assert(ctx->passlen < ctx->passsize);
        ctx->passphrase[ctx->passlen] = '\0';
    }

    gtk_widget_destroy(ctx->dialog);
}

static int setup_gtk(const char *display)
{
    static int gtk_initialised = FALSE;
    int argc;
    char *real_argv[3];
    char **argv = real_argv;
    int ret;

    if (gtk_initialised)
        return TRUE;

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

char *gtk_askpass_main(const char *display, const char *wintitle,
                       const char *prompt, int *success)
{
    struct askpass_ctx actx, *ctx = &actx;
    const char *err;

    /* In case gtk_init hasn't been called yet by the program */
    if (!setup_gtk(display)) {
        *success = FALSE;
        return dupstr("unable to initialise GTK");
    }

    if ((err = gtk_askpass_setup(ctx, wintitle, prompt)) != NULL) {
        *success = FALSE;
        return dupprintf("%s", err);
    }
    gtk_main();
    gtk_askpass_cleanup(ctx);

    if (ctx->passphrase) {
        *success = TRUE;
        return ctx->passphrase;
    } else {
        *success = FALSE;
        return dupstr("passphrase input cancelled");
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
    int success, exitcode;
    char *ret;

    gtk_init(&argc, &argv);

    if (argc != 2) {
        success = FALSE;
        ret = dupprintf("usage: %s <prompt text>", argv[0]);
    } else {
        srand(time(NULL));
        ret = gtk_askpass_main(argv[1], &success);
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
