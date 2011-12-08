/*
 * pterm main program.
 */

#include <stdio.h>
#include <stdlib.h>

#include "putty.h"

const char *const appname = "pterm";
const int use_event_log = 0;	       /* pterm doesn't need it */
const int new_session = 0, saved_sessions = 0;   /* or these */
const int use_pty_argv = TRUE;

Backend *select_backend(Conf *conf)
{
    return &pty_backend;
}

void net_pending_errors(void)
{
    /*
     * Stub version of net_pending_errors(), because gtkwin.c has to
     * be prepared to call it when linked into PuTTY and therefore we
     * have to avoid a link failure when linking gtkwin.c in turn into
     * a non-networked application.
     */
}

int cfgbox(Conf *conf)
{
    /*
     * This is a no-op in pterm, except that we'll ensure the
     * protocol is set to -1 to inhibit the useless Connection
     * panel in the config box.
     */
    conf_set_int(conf, CONF_protocol, -1);
    return 1;
}

void cleanup_exit(int code)
{
    exit(code);
}

int process_nonoption_arg(char *arg, Conf *conf, int *allow_launch)
{
    return 0;                          /* pterm doesn't have any. */
}

char *make_default_wintitle(char *hostname)
{
    return dupstr("pterm");
}

int main(int argc, char **argv)
{
    extern int pt_main(int argc, char **argv);
    extern void pty_pre_init(void);    /* declared in pty.c */

    cmdline_tooltype = TOOLTYPE_NONNETWORK;
    default_protocol = -1;

    pty_pre_init();

    return pt_main(argc, argv);
}
