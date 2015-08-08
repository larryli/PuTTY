/*
 * Header file to make compatibility with older GTK versions less
 * painful, by #defining various things that are pure spelling changes
 * between GTK1 and GTK2.
 */

#if !GTK_CHECK_VERSION(2,0,0)

#include <ctype.h>
#include <X11/X.h>

#define G_CALLBACK(x) GTK_SIGNAL_FUNC(x)
#define G_OBJECT(x) GTK_OBJECT(x)
#define g_ascii_isspace(x) (isspace((unsigned char)(x)))
#define g_signal_connect gtk_signal_connect
#define GDK_GRAB_SUCCESS GrabSuccess
#define gtk_dialog_get_content_area(dlg) ((dlg)->vbox)
#define gtk_dialog_get_action_area(dlg) ((dlg)->action_area)
#define gtk_widget_set_size_request gtk_widget_set_usize

#endif

