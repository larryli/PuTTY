/*
 * no-gtk.c: link into non-GUI Unix programs so that they can tell
 * buildinfo about a lack of GTK.
 */

#include "putty.h"

char *buildinfo_gtk_version(void)
{
    return NULL;
}
