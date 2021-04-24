/*
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

#include <gtk/gtk.h>
#include "putty.h"
#include "gtkcompat.h"
#include "gtkmisc.h"

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
                       w, true, true, 0);

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
                     align, false, true, 0);

    w = gtk_hseparator_new();
    gtk_box_pack_end(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
                     w, false, true, 0);
    gtk_widget_show(w);
    gtk_widget_hide(gtk_dialog_get_action_area(GTK_DIALOG(dlg)));
    g_object_set(G_OBJECT(dlg), "has-separator", true, (const char *)NULL);

#else /* GTK 3 */

    /* GtkWindow is a GtkBin, hence contains exactly one child, which
     * here we always expect to be a vbox */
    GtkBox *vbox = GTK_BOX(gtk_bin_get_child(GTK_BIN(dlg)));
    GtkWidget *sep;

    g_object_set(G_OBJECT(w), "margin", 8, (const char *)NULL);
    gtk_box_pack_end(vbox, w, false, true, 0);

    sep = gtk_hseparator_new();
    gtk_box_pack_end(vbox, sep, false, true, 0);
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
                                    bool expand, bool fill, guint padding)
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
