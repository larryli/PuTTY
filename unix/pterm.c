/*
 * pterm - a fusion of the PuTTY terminal emulator with a Unix pty
 * back end, all running as a GTK application. Wish me luck.
 */

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <gtk/gtk.h>

#define CAT2(x,y) x ## y
#define CAT(x,y) CAT2(x,y)
#define ASSERT(x) enum {CAT(assertion_,__LINE__) = 1 / (x)}

#define lenof(x) (sizeof((x))/sizeof(*(x)))

struct gui_data {
    GtkWidget *area;
    GdkFont *fonts[2];                 /* normal and bold (for now!) */
    GdkGC *black_gc, *white_gc;
};

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
    struct gui_data *inst = (struct gui_data *)data;

    /*
     * FIXME: pass the exposed rect to terminal.c which will call
     * us back to do the actual painting.
     */
    return FALSE;
}

#define KEY_PRESSED(k) \
    (inst->keystate[(k) / 32] & (1 << ((k) % 32)))

gint key_event(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;

    /*
     * FIXME: all sorts of fun keyboard handling required here.
     */
}

gint timer_func(gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;

    /*
     * FIXME: we're bound to need this sooner or later!
     */
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
}

int main(int argc, char **argv)
{
    GtkWidget *window;
    struct gui_data the_inst;
    struct gui_data *inst = &the_inst;   /* so we always write `inst->' */

    gtk_init(&argc, &argv);

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

    gtk_main();

    return 0;
}
