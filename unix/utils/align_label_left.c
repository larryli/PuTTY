/*
 * Helper function to align the text in a GtkLabel to the left, which
 * has to be done in several different ways depending on GTK version.
 */

#include <gtk/gtk.h>
#include "putty.h"
#include "gtkcompat.h"
#include "gtkmisc.h"

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
