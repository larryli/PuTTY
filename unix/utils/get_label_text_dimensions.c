/*
 * Determine the dimensions of a piece of text in the standard
 * font used in GTK interface elements like labels. We do this by
 * instantiating an actual GtkLabel, and then querying its size.
 */

#include <gtk/gtk.h>
#include "putty.h"
#include "gtkcompat.h"
#include "gtkmisc.h"

void get_label_text_dimensions(const char *text, int *width, int *height)
{
    GtkWidget *label = gtk_label_new(text);

    /*
     * GTK2 and GTK3 require us to query the size completely
     * differently. I'm sure there ought to be an easier approach than
     * the way I'm doing this in GTK3, too!
     */
#if GTK_CHECK_VERSION(3,0,0)
    PangoLayout *layout = gtk_label_get_layout(GTK_LABEL(label));
    PangoRectangle logrect;
    pango_layout_get_extents(layout, NULL, &logrect);
    if (width)
        *width = logrect.width / PANGO_SCALE;
    if (height)
        *height = logrect.height / PANGO_SCALE;
#else
    GtkRequisition req;
    gtk_widget_size_request(label, &req);
    if (width)
        *width = req.width;
    if (height)
        *height = req.height;
#endif

    g_object_ref_sink(G_OBJECT(label));
#if GTK_CHECK_VERSION(2,10,0)
    g_object_unref(label);
#endif
}
