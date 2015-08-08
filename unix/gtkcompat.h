/*
 * Header file to make compatibility with older GTK versions less
 * painful, by #defining various things that are pure spelling changes
 * between GTK1 and GTK2.
 */

#if !GTK_CHECK_VERSION(2,0,0)

#include <ctype.h>
#include <X11/X.h>

/* Helper macro used in definitions below. Note that although it
 * *expands* w and flag twice, it does not *evaluate* them twice
 * because the evaluations are in exclusive branches of ?:. So it's
 * a side-effect-safe macro. */
#define gtk1_widget_set_unset_flag(w, flag, b)                    \
    ((b) ? GTK_WIDGET_SET_FLAGS(w, flag) : GTK_WIDGET_UNSET_FLAGS(w, flag))

#define G_CALLBACK(x) GTK_SIGNAL_FUNC(x)
#define G_OBJECT(x) GTK_OBJECT(x)

#define g_ascii_isspace(x) (isspace((unsigned char)(x)))
#define g_signal_connect gtk_signal_connect
#define g_object_set_data gtk_object_set_data

#define GDK_GRAB_SUCCESS GrabSuccess

#define gtk_widget_set_size_request gtk_widget_set_usize

#define gtk_dialog_get_content_area(dlg) ((dlg)->vbox)
#define gtk_dialog_get_action_area(dlg) ((dlg)->action_area)
#define gtk_dialog_set_can_default(dlg) ((dlg)->action_area)
#define gtk_widget_get_window(w) ((w)->window)
#define gtk_widget_get_parent(w) ((w)->parent)
#define gtk_widget_set_allocation(w, a) ((w)->allocation = *(a))
#define gtk_container_get_border_width(c) ((c)->border_width)
#define gtk_bin_get_child(b) ((b)->child)
#define gtk_color_selection_dialog_get_color_selection(cs) ((cs)->colorsel)
#define gtk_selection_data_get_target(sd) ((sd)->target)
#define gtk_selection_data_get_data_type(sd) ((sd)->type)
#define gtk_selection_data_get_data(sd) ((sd)->data)
#define gtk_selection_data_get_length(sd) ((sd)->length)
#define gtk_selection_data_get_format(sd) ((sd)->format)
#define gtk_adjustment_set_lower(a, val) ((a)->lower = (val))
#define gtk_adjustment_set_upper(a, val) ((a)->upper = (val))
#define gtk_adjustment_set_page_size(a, val) ((a)->page_size = (val))
#define gtk_adjustment_set_page_increment(a, val) ((a)->page_increment = (val))
#define gtk_adjustment_set_step_increment(a, val) ((a)->step_increment = (val))
#define gtk_adjustment_get_value(a) ((a)->value)
#define gdk_visual_get_depth(v) ((v)->depth)

#define gtk_widget_set_has_window(w, b)                 \
    gtk1_widget_set_unset_flag(w, GTK_NO_WINDOW, !(b))
#define gtk_widget_set_mapped(w, b)                     \
    gtk1_widget_set_unset_flag(w, GTK_MAPPED, (b))
#define gtk_widget_set_can_default(w, b)                \
    gtk1_widget_set_unset_flag(w, GTK_CAN_DEFAULT, (b))
#define gtk_widget_get_visible(w) GTK_WIDGET_VISIBLE(w)
#define gtk_widget_get_mapped(w) GTK_WIDGET_MAPPED(w)
#define gtk_widget_get_realized(w) GTK_WIDGET_REALIZED(w)
#define gtk_widget_get_state(w) GTK_WIDGET_STATE(w)

#endif

