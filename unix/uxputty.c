/*
 * Unix PuTTY main program.
 */

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>

#define MAY_REFER_TO_GTK_IN_HEADERS

#include "putty.h"
#include "storage.h"

#include "gtkcompat.h"

/*
 * Stubs to avoid uxpty.c needing to be linked in.
 */
const int use_pty_argv = FALSE;
char **pty_argv;		       /* never used */
char *pty_osx_envrestore_prefix;

/*
 * Clean up and exit.
 */
void cleanup_exit(int code)
{
    /*
     * Clean up.
     */
    sk_cleanup();
    random_save_seed();
    exit(code);
}

Backend *select_backend(Conf *conf)
{
    Backend *back = backend_from_proto(conf_get_int(conf, CONF_protocol));
    assert(back != NULL);
    return back;
}

void initial_config_box(Conf *conf, post_dialog_fn_t after, void *afterctx)
{
    char *title = dupcat(appname, " Configuration", NULL);
    create_config_box(title, conf, FALSE, 0, after, afterctx);
    sfree(title);
}

const int use_event_log = 1, new_session = 1, saved_sessions = 1;
const int dup_check_launchable = 1;

char *make_default_wintitle(char *hostname)
{
    return dupcat(hostname, " - ", appname, NULL);
}

/*
 * X11-forwarding-related things suitable for Gtk app.
 */

char *platform_get_x_display(void) {
    const char *display;
    /* Try to take account of --display and what have you. */
    if (!(display = gdk_get_display()))
	/* fall back to traditional method */
	display = getenv("DISPLAY");
    return dupstr(display);
}

const int share_can_be_downstream = TRUE;
const int share_can_be_upstream = TRUE;

void setup(int single)
{
    sk_init();
    flags = FLAG_VERBOSE | FLAG_INTERACTIVE;
    cmdline_tooltype |= TOOLTYPE_HOST_ARG | TOOLTYPE_PORT_ARG;
    default_protocol = be_default_protocol;
    /* Find the appropriate default port. */
    {
	Backend *b = backend_from_proto(default_protocol);
	default_port = 0; /* illegal */
	if (b)
	    default_port = b->default_port;
    }
}
