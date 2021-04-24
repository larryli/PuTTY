/*
 * Return the Xlib 'Display *' underlying our GTK environment, if any.
 */

#include <gtk/gtk.h>
#include "putty.h"
#include "gtkcompat.h"
#include "gtkmisc.h"

#ifndef NOT_X_WINDOWS

#include <gdk/gdkx.h>
#include <X11/Xlib.h>

Display *get_x11_display(void)
{
#if GTK_CHECK_VERSION(3,0,0)
    if (!GDK_IS_X11_DISPLAY(gdk_display_get_default()))
        return NULL;
#endif
    return GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
}

#else

/*
 * Include _something_ in this file to prevent an annoying compiler
 * warning, and to avoid having to condition out this file in
 * CMakeLists. It's in a library, so this variable shouldn't end up in
 * any actual program, because nothing will refer to it.
 */
const int get_x11_display_dummy_variable = 0;

#endif
