/*
 * pterm - a fusion of the PuTTY terminal emulator with a Unix pty
 * back end, all running as a GTK application. Wish me luck.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <gtk/gtk.h>

#define PUTTY_DO_GLOBALS	       /* actually _define_ globals */
#include "putty.h"

#define CAT2(x,y) x ## y
#define CAT(x,y) CAT2(x,y)
#define ASSERT(x) enum {CAT(assertion_,__LINE__) = 1 / (x)}

void ldisc_update(int echo, int edit)
{
    /*
     * This is a stub in pterm. If I ever produce a Unix
     * command-line ssh/telnet/rlogin client (i.e. a port of plink)
     * then it will require some termios manoeuvring analogous to
     * that in the Windows plink.c, but here it's meaningless.
     */
}

int askappend(char *filename)
{
    /*
     * FIXME: for the moment we just wipe the log file. Since I
     * haven't yet enabled logging, this shouldn't matter yet!
     */
    return 2;
}

void logevent(char *string)
{
    /*
     * FIXME: event log entries are currently ignored.
     */
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
Mouse_Button translate_button(Mouse_Button button)
{
    if (button == MBT_LEFT)
	return MBT_SELECT;
    if (button == MBT_MIDDLE)
	return MBT_PASTE;
    if (button == MBT_RIGHT)
	return MBT_EXTEND;
    return 0;			       /* shouldn't happen */
}

/*
 * Minimise or restore the window in response to a server-side
 * request.
 */
void set_iconic(int iconic)
{
    /* FIXME: currently ignored */
}

/*
 * Move the window in response to a server-side request.
 */
void move_window(int x, int y)
{
    /* FIXME: currently ignored */
}

/*
 * Move the window to the top or bottom of the z-order in response
 * to a server-side request.
 */
void set_zorder(int top)
{
    /* FIXME: currently ignored */
}

/*
 * Refresh the window in response to a server-side request.
 */
void refresh_window(void)
{
    /* FIXME: currently ignored */
}

/*
 * Maximise or restore the window in response to a server-side
 * request.
 */
void set_zoomed(int zoomed)
{
    /* FIXME: currently ignored */
}

/*
 * Report whether the window is iconic, for terminal reports.
 */
int is_iconic(void)
{
    return 0;			       /* FIXME */
}

/*
 * Report the window's position, for terminal reports.
 */
void get_window_pos(int *x, int *y)
{
    *x = 3; *y = 4;		       /* FIXME */
}

/*
 * Report the window's pixel size, for terminal reports.
 */
void get_window_pixels(int *x, int *y)
{
    *x = 1; *y = 2;		       /* FIXME */
}

/*
 * Return the window or icon title.
 */
char *get_window_title(int icon)
{
    return "FIXME: window title retrieval not yet implemented";
}

struct gui_data {
    GtkWidget *area;
    GdkFont *fonts[2];                 /* normal and bold (for now!) */
    GdkGC *black_gc, *white_gc;
};

static struct gui_data the_inst;
static struct gui_data *inst = &the_inst;   /* so we always write `inst->' */

gint delete_window(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    /*
     * FIXME: warn on close?
     */
    return FALSE;
}

gint configure_area(GtkWidget *widget, GdkEventConfigure *event, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;

    inst->fonts[0] = gdk_font_load("9x15t");   /* XXCONFIG */
    inst->fonts[1] = NULL;             /* XXCONFIG */
    inst->black_gc = widget->style->black_gc;
    inst->white_gc = widget->style->white_gc;

#if 0                                  /* FIXME: get cmap from settings */
    /*
     * Set up the colour map.
     */
    inst->colmap = gdk_colormap_get_system();
    {
	char *colours[] = {
	    "#cc0000", "#880000", "#ff0000",
	    "#cc6600", "#884400", "#ff7f00",
	    "#cc9900", "#886600", "#ffbf00",
	    "#cccc00", "#888800", "#ffff00",
	    "#00cc00", "#008800", "#00ff00",
	    "#008400", "#005800", "#00b000",
	    "#008484", "#005858", "#00b0b0",
	    "#00cccc", "#008888", "#00ffff",
	    "#0066cc", "#004488", "#007fff",
	    "#9900cc", "#660088", "#bf00ff",
	    "#cc00cc", "#880088", "#ff00ff",
	    "#cc9999", "#886666", "#ffbfbf",
	    "#cccc99", "#888866", "#ffffbf",
	    "#99cc99", "#668866", "#bfffbf",
	    "#9999cc", "#666688", "#bfbfff",
	    "#757575", "#4e4e4e", "#9c9c9c",
	    "#999999", "#666666", "#bfbfbf",
	    "#cccccc", "#888888", "#ffffff",
	};
	ASSERT(sizeof(colours)/sizeof(*colours)==3*NCOLOURS);
	gboolean success[3*NCOLOURS];
	int i;

	for (i = 0; i < 3*NCOLOURS; i++) {
	    if (!gdk_color_parse(colours[i], &inst->cols[i]))
		g_error("4tris: couldn't parse colour \"%s\"\n", colours[i]);
	}

	gdk_colormap_alloc_colors(inst->colmap, inst->cols, 3*NCOLOURS,
				  FALSE, FALSE, success);
	for (i = 0; i < 3*NCOLOURS; i++) {
	    if (!success[i])
		g_error("4tris: couldn't allocate colour \"%s\"\n", colours[i]);
	}
    }
#endif

    return TRUE;
}

gint expose_area(GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
    /* struct gui_data *inst = (struct gui_data *)data; */

    /*
     * Pass the exposed rectangle to terminal.c, which will call us
     * back to do the actual painting.
     */
    term_paint(NULL, 
	       event->area.x / 9, event->area.y / 15,
	       (event->area.x + event->area.width - 1) / 9,
	       (event->area.y + event->area.height - 1) / 15);
    return TRUE;
}

#define KEY_PRESSED(k) \
    (inst->keystate[(k) / 32] & (1 << ((k) % 32)))

gint key_event(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    /* struct gui_data *inst = (struct gui_data *)data; */

    if (event->type == GDK_KEY_PRESS) {
	char c[1];
	c[0] = event->keyval;
	ldisc_send(c, 1, 1);
	term_out();
    }

    return TRUE;
}

gint timer_func(gpointer data)
{
    /* struct gui_data *inst = (struct gui_data *)data; */

    term_update();
    return TRUE;
}

void destroy(GtkWidget *widget, gpointer data)
{
    gtk_main_quit();
}

gint focus_event(GtkWidget *widget, GdkEventFocus *event, gpointer data)
{
    /*
     * FIXME: need to faff with the cursor shape.
     */
    return FALSE;
}

/*
 * set or clear the "raw mouse message" mode
 */
void set_raw_mouse_mode(int activate)
{
    /* FIXME: currently ignored */
}

void request_resize(int w, int h)
{
    /* FIXME: currently ignored */
}

void palette_set(int n, int r, int g, int b)
{
    /* FIXME: currently ignored */
}
void palette_reset(void)
{
    /* FIXME: currently ignored */
}

void write_clip(wchar_t * data, int len, int must_deselect)
{
    /* FIXME: currently ignored */
}

void get_clip(wchar_t ** p, int *len)
{
    if (p) {
	/* FIXME: currently nonfunctional */
	*p = NULL;
	*len = 0;
    }
}

void set_title(char *title)
{
    /* FIXME: currently ignored */
}

void set_icon(char *title)
{
    /* FIXME: currently ignored */
}

void set_sbar(int total, int start, int page)
{
    /* FIXME: currently ignored */
}

void sys_cursor(int x, int y)
{
    /*
     * This is meaningless under X.
     */
}

void beep(int mode)
{
    gdk_beep();
}

int CharWidth(Context ctx, int uc)
{
    /*
     * Under X, any fixed-width font really _is_ fixed-width.
     * Double-width characters will be dealt with using a separate
     * font. For the moment we can simply return 1.
     */
    return 1;
}

Context get_ctx(void)
{
    GdkGC *gc = gdk_gc_new(inst->area->window);
    return gc;
}

void free_ctx(Context ctx)
{
    GdkGC *gc = (GdkGC *)ctx;
    gdk_gc_unref(gc);
}

/*
 * Draw a line of text in the window, at given character
 * coordinates, in given attributes.
 *
 * We are allowed to fiddle with the contents of `text'.
 */
void do_text(Context ctx, int x, int y, char *text, int len,
	     unsigned long attr, int lattr)
{
    GdkColor fg, bg;

    GdkGC *gc = (GdkGC *)ctx;
    fg.red = fg.green = fg.blue = 65535;
    bg.red = bg.green = bg.blue = 65535;
    gdk_gc_set_foreground(gc, &fg);
    gdk_gc_set_background(gc, &bg);

    gdk_draw_text(inst->area->window, inst->fonts[0], inst->white_gc,
		  x*9, y*15 + inst->fonts[0]->ascent, text, len);
}

void do_cursor(Context ctx, int x, int y, char *text, int len,
	       unsigned long attr, int lattr)
{
    /* FIXME: passive cursor NYI */
    if (attr & TATTR_PASCURS) {
	attr &= ~TATTR_PASCURS;
	attr |= TATTR_ACTCURS;
    }
    do_text(ctx, x, y, text, len, attr, lattr);
}

void modalfatalbox(char *p, ...)
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
    GtkWidget *window;

    gtk_init(&argc, &argv);

    do_defaults(NULL, &cfg);

    init_ucs();

    back = &pty_backend;
    back->init(NULL, 0, NULL, 0);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    inst->area = gtk_drawing_area_new();
    gtk_drawing_area_size(GTK_DRAWING_AREA(inst->area),
			  9*80, 15*24);/* FIXME: proper resizing stuff */

    gtk_container_add(GTK_CONTAINER(window), inst->area);

    gtk_signal_connect(GTK_OBJECT(window), "destroy",
		       GTK_SIGNAL_FUNC(destroy), inst);
    gtk_signal_connect(GTK_OBJECT(window), "delete_event",
		       GTK_SIGNAL_FUNC(delete_window), inst);
    gtk_signal_connect(GTK_OBJECT(window), "key_press_event",
		       GTK_SIGNAL_FUNC(key_event), inst);
    gtk_signal_connect(GTK_OBJECT(window), "key_release_event",
		       GTK_SIGNAL_FUNC(key_event), inst);
    gtk_signal_connect(GTK_OBJECT(window), "focus_in_event",
		       GTK_SIGNAL_FUNC(focus_event), inst);
    gtk_signal_connect(GTK_OBJECT(window), "focus_out_event",
		       GTK_SIGNAL_FUNC(focus_event), inst);
    gtk_signal_connect(GTK_OBJECT(inst->area), "configure_event",
		       GTK_SIGNAL_FUNC(configure_area), inst);
    gtk_signal_connect(GTK_OBJECT(inst->area), "expose_event",
		       GTK_SIGNAL_FUNC(expose_area), inst);
    gtk_timeout_add(20, timer_func, inst);
    gtk_widget_add_events(GTK_WIDGET(inst->area),
			  GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK);

    gtk_widget_show(inst->area);
    gtk_widget_show(window);

    term_init();
    term_size(24, 80, 2000);

    gtk_main();

    return 0;
}
