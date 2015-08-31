/*
 * Miscellaneous helper functions for GTK.
 */

#ifndef PUTTY_GTKMISC_H
#define PUTTY_GTKMISC_H

GtkWidget *our_dialog_new(void);
void our_dialog_add_to_content_area(GtkWindow *dlg, GtkWidget *w,
                                    gboolean expand, gboolean fill,
                                    guint padding);
void our_dialog_set_action_area(GtkWindow *dlg, GtkWidget *w);
GtkBox *our_dialog_make_action_hbox(GtkWindow *dlg);

#endif /* PUTTY_GTKMISC_H */
