/*
 * pterm - a fusion of the PuTTY terminal emulator with a Unix pty
 * back end, all running as a GTK application. Wish me luck.
 */

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <gtk/gtk.h>

#define PUTTY_DO_GLOBALS	       /* actually _define_ globals */
#include "putty.h"

#define CAT2(x,y) x ## y
#define CAT(x,y) CAT2(x,y)
#define ASSERT(x) enum {CAT(assertion_,__LINE__) = 1 / (x)}

#define NCOLOURS (lenof(((Config *)0)->colours))

struct gui_data {
    GtkWidget *area;
    GdkPixmap *pixmap;
    GdkFont *fonts[2];                 /* normal and bold (for now!) */
    GdkCursor *rawcursor, *textcursor;
    GdkColor cols[NCOLOURS];
    GdkColormap *colmap;
    GdkGC *black_gc, *white_gc;
};

static struct gui_data the_inst;
static struct gui_data *inst = &the_inst;   /* so we always write `inst->' */
static int send_raw_mouse;

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

    if (inst->pixmap)
	gdk_pixmap_unref(inst->pixmap);

    inst->pixmap = gdk_pixmap_new(widget->window, 9*80, 15*24, -1);

    inst->fonts[0] = gdk_font_load("9x15t");   /* XXCONFIG */
    inst->fonts[1] = NULL;             /* XXCONFIG */
    inst->black_gc = widget->style->black_gc;
    inst->white_gc = widget->style->white_gc;

    /*
     * Set up the colour map.
     */
    inst->colmap = gdk_colormap_get_system();
    {
	static const int ww[] = {
	    6, 7, 8, 9, 10, 11, 12, 13,
	    14, 15, 16, 17, 18, 19, 20, 21,
	    0, 1, 2, 3, 4, 5
	};
	gboolean success[NCOLOURS];
	int i;

	assert(lenof(ww) == NCOLOURS);

	for (i = 0; i < NCOLOURS; i++) {
	    inst->cols[i].red = cfg.colours[ww[i]][0] * 0x0101;
	    inst->cols[i].green = cfg.colours[ww[i]][1] * 0x0101;
	    inst->cols[i].blue = cfg.colours[ww[i]][2] * 0x0101;
	}

	gdk_colormap_alloc_colors(inst->colmap, inst->cols, NCOLOURS,
				  FALSE, FALSE, success);
	for (i = 0; i < NCOLOURS; i++) {
	    if (!success[i])
		g_error("pterm: couldn't allocate colour %d (#%02x%02x%02x)\n",
			i, cfg.colours[i][0], cfg.colours[i][1], cfg.colours[i][2]);
	}
    }

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
	ldisc_send(event->string, strlen(event->string), 1);
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
    has_focus = event->in;
    term_out();
    term_update();
    return FALSE;
}

/*
 * set or clear the "raw mouse message" mode
 */
void set_raw_mouse_mode(int activate)
{
    activate = activate && !cfg.no_mouse_rep;
    send_raw_mouse = activate;
    if (send_raw_mouse)
	gdk_window_set_cursor(inst->area->window, inst->rawcursor);
    else
	gdk_window_set_cursor(inst->area->window, inst->textcursor);
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
    GdkGC *gc;
    if (!inst->area->window)
	return NULL;
    gc = gdk_gc_new(inst->area->window);
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
    int nfg, nbg, t;
    GdkGC *gc = (GdkGC *)ctx;

    /*
     * NYI:
     *  - ATTR_WIDE (is this for Unicode CJK? I hope so)
     *  - LATTR_* (ESC # 4 double-width and double-height stuff)
     *  - cursor shapes other than block
     *  - VT100 line drawing stuff; code pages in general!
     *  - shadow bolding
     *  - underline
     */

    nfg = 2 * ((attr & ATTR_FGMASK) >> ATTR_FGSHIFT);
    nbg = 2 * ((attr & ATTR_BGMASK) >> ATTR_BGSHIFT);
    if (attr & ATTR_REVERSE) {
	t = nfg;
	nfg = nbg;
	nbg = t;
    }
    if (cfg.bold_colour && (attr & ATTR_BOLD))
	nfg++;
    if (cfg.bold_colour && (attr & ATTR_BLINK))
	nbg++;
    if (attr & TATTR_ACTCURS) {
	nfg = NCOLOURS-2;
	nbg = NCOLOURS-1;
    }

    gdk_gc_set_foreground(gc, &inst->cols[nbg]);
    gdk_draw_rectangle(inst->pixmap, gc, 1, x*9, y*15, len*9, 15);
    
    gdk_gc_set_foreground(gc, &inst->cols[nfg]);
    gdk_draw_text(inst->pixmap, inst->fonts[0], gc,
		  x*9, y*15 + inst->fonts[0]->ascent, text, len);

    if (attr & ATTR_UNDER) {
	int uheight = inst->fonts[0]->ascent + 1;
	if (uheight >= 15)
	    uheight = 14;
	gdk_draw_line(inst->pixmap, gc, x*9, y*15 + uheight,
		      (x+len)*9-1, y*15+uheight);
    }

    gdk_draw_pixmap(inst->area->window, gc, inst->pixmap,
		    x*9, y*15, x*9, y*15, len*9, 15);
}

void do_cursor(Context ctx, int x, int y, char *text, int len,
	       unsigned long attr, int lattr)
{
    int passive;
    GdkGC *gc = (GdkGC *)ctx;

    /*
     * NYI: cursor shapes other than block
     */
    if (attr & TATTR_PASCURS) {
	attr &= ~TATTR_PASCURS;
	passive = 1;
    } else
	passive = 0;
    do_text(ctx, x, y, text, len, attr, lattr);
    if (passive) {
	gdk_gc_set_foreground(gc, &inst->cols[NCOLOURS-1]);
	gdk_draw_rectangle(inst->pixmap, gc, 0, x*9, y*15, len*9-1, 15-1);
	gdk_draw_pixmap(inst->area->window, gc, inst->pixmap,
			x*9, y*15, x*9, y*15, len*9, 15);
    }
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

    inst->textcursor = gdk_cursor_new(GDK_XTERM);
    inst->rawcursor = gdk_cursor_new(GDK_ARROW);
    gdk_window_set_cursor(inst->area->window, inst->textcursor);

    term_init();
    term_size(24, 80, 2000);

    gtk_main();

    return 0;
}
