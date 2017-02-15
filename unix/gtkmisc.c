/*
 * Miscellaneous GTK helper functions.
 */

#include <assert.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>

#include <gtk/gtk.h>
#if !GTK_CHECK_VERSION(3,0,0)
#include <gdk/gdkkeysyms.h>
#endif

#include "putty.h"
#include "gtkcompat.h"

void get_label_text_dimensions(const char *text, int *width, int *height)
{
    /*
     * Determine the dimensions of a piece of text in the standard
     * font used in GTK interface elements like labels. We do this by
     * instantiating an actual GtkLabel, and then querying its size.
     *
     * But GTK2 and GTK3 require us to query the size completely
     * differently. I'm sure there ought to be an easier approach than
     * the way I'm doing this in GTK3, too!
     */
    GtkWidget *label = gtk_label_new(text);

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

int string_width(const char *text)
{
    int ret;
    get_label_text_dimensions(text, &ret, NULL);
    return ret;
}

void align_label_left(GtkLabel *label)
{
#if GTK_CHECK_VERSION(3,16,0)
    gtk_label_set_xalign(label, 0.0);
#elif GTK_CHECK_VERSION(3,14,0)
    gtk_widget_set_halign(GTK_WIDGET(label), GTK_ALIGN_START);
#else
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
#endif
}

/* ----------------------------------------------------------------------
 * Functions to arrange controls in a basically dialog-like window.
 *
 * The best method for doing this has varied wildly with versions of
 * GTK, hence the set of wrapper functions here.
 *
 * In GTK 1, a GtkDialog has an 'action_area' at the bottom, which is
 * a GtkHBox which stretches to cover the full width of the dialog. So
 * we can either add buttons or other widgets to that box directly, or
 * alternatively we can fill the hbox with some layout class of our
 * own such as a Columns widget.
 *
 * In GTK 2, the action area has become a GtkHButtonBox, and its
 * layout behaviour seems to be different and not what we want. So
 * instead we abandon the dialog's action area completely: we
 * gtk_widget_hide() it in the below code, and we also call
 * gtk_dialog_set_has_separator() to remove the separator above it. We
 * then insert our own action area into the end of the dialog's main
 * vbox, and add our own separator above that.
 *
 * In GTK 3, we typically don't even want to use GtkDialog at all,
 * because GTK 3 has become a lot more restrictive about what you can
 * sensibly use GtkDialog for - it deprecates direct access to the
 * action area in favour of making you provide nothing but
 * dialog-ending buttons in the form of (text, response code) pairs,
 * so you can't put any other kind of control in there, or fiddle with
 * alignment and positioning, or even have a button that _doesn't_ end
 * the dialog (e.g. 'View Licence' in our About box). So instead of
 * GtkDialog, we use a straight-up GtkWindow and have it contain a
 * vbox as its (unique) child widget; and we implement the action area
 * by adding a separator and another widget at the bottom of that
 * vbox.
 */

GtkWidget *our_dialog_new(void)
{
#if GTK_CHECK_VERSION(3,0,0)
    /*
     * See comment in our_dialog_set_action_area(): in GTK 3, we use
     * GtkWindow in place of GtkDialog for most purposes.
     */
    GtkWidget *w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_add(GTK_CONTAINER(w), vbox);
    gtk_widget_show(vbox);
    return w;
#else
    return gtk_dialog_new();
#endif
}

void our_dialog_set_action_area(GtkWindow *dlg, GtkWidget *w)
{
#if !GTK_CHECK_VERSION(2,0,0)

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->action_area),
                       w, TRUE, TRUE, 0);

#elif !GTK_CHECK_VERSION(3,0,0)

    GtkWidget *align;
    align = gtk_alignment_new(0, 0, 1, 1);
    gtk_container_add(GTK_CONTAINER(align), w);
    /*
     * The purpose of this GtkAlignment is to provide padding
     * around the buttons. The padding we use is twice the padding
     * used in our GtkColumns, because we nest two GtkColumns most
     * of the time (one separating the tree view from the main
     * controls, and another for the main controls themselves).
     */
#if GTK_CHECK_VERSION(2,4,0)
    gtk_alignment_set_padding(GTK_ALIGNMENT(align), 8, 8, 8, 8);
#endif
    gtk_widget_show(align);
    gtk_box_pack_end(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
                     align, FALSE, TRUE, 0);

    w = gtk_hseparator_new();
    gtk_box_pack_end(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
                     w, FALSE, TRUE, 0);
    gtk_widget_show(w);
    gtk_widget_hide(gtk_dialog_get_action_area(GTK_DIALOG(dlg)));
    g_object_set(G_OBJECT(dlg), "has-separator", TRUE, (const char *)NULL);

#else /* GTK 3 */

    /* GtkWindow is a GtkBin, hence contains exactly one child, which
     * here we always expect to be a vbox */
    GtkBox *vbox = GTK_BOX(gtk_bin_get_child(GTK_BIN(dlg)));
    GtkWidget *sep;

    g_object_set(G_OBJECT(w), "margin", 8, (const char *)NULL);
    gtk_box_pack_end(vbox, w, FALSE, TRUE, 0);

    sep = gtk_hseparator_new();
    gtk_box_pack_end(vbox, sep, FALSE, TRUE, 0);
    gtk_widget_show(sep);

#endif
}

GtkBox *our_dialog_make_action_hbox(GtkWindow *dlg)
{
#if GTK_CHECK_VERSION(3,0,0)
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    our_dialog_set_action_area(dlg, hbox);
    g_object_set(G_OBJECT(hbox), "margin", 0, (const char *)NULL);
    g_object_set(G_OBJECT(hbox), "spacing", 8, (const char *)NULL);
    gtk_widget_show(hbox);
    return GTK_BOX(hbox);
#else /* not GTK 3 */
    return GTK_BOX(gtk_dialog_get_action_area(GTK_DIALOG(dlg)));
#endif
}

void our_dialog_add_to_content_area(GtkWindow *dlg, GtkWidget *w,
                                    gboolean expand, gboolean fill,
                                    guint padding)
{
#if GTK_CHECK_VERSION(3,0,0)
    /* GtkWindow is a GtkBin, hence contains exactly one child, which
     * here we always expect to be a vbox */
    GtkBox *vbox = GTK_BOX(gtk_bin_get_child(GTK_BIN(dlg)));

    gtk_box_pack_start(vbox, w, expand, fill, padding);
#else
    gtk_box_pack_start
        (GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
         w, expand, fill, padding);
#endif
}

char *buildinfo_gtk_version(void)
{
    return dupprintf("%d.%d.%d",
                     GTK_MAJOR_VERSION, GTK_MINOR_VERSION, GTK_MICRO_VERSION);
}
