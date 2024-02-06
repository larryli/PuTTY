/*
 * Return the version of GTK we were compiled against, for buildinfo.
 */

#include <gtk/gtk.h>
#include "putty.h"
#include "gtkcompat.h"
#include "gtkmisc.h"

char *buildinfo_gtk_version(void)
{
    return dupprintf("%d.%d.%d",
                     GTK_MAJOR_VERSION, GTK_MINOR_VERSION, GTK_MICRO_VERSION);
}
