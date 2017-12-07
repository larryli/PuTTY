/*
 * pterm main program.
 */

#include <stdio.h>
#include <stdlib.h>

#include "putty.h"

const char *const appname = "pterm";
const int use_event_log = 0;	       /* pterm doesn't need it */
const int new_session = 0, saved_sessions = 0;   /* or these */
const int dup_check_launchable = 0; /* no need to check host name in conf */
const int use_pty_argv = TRUE;

Backend *select_backend(Conf *conf)
{
    return &pty_backend;
}

void initial_config_box(Conf *conf, post_dialog_fn_t after, void *afterctx)
{
    /*
     * This is a no-op in pterm, except that we'll ensure the protocol
     * is set to -1 to inhibit the useless Connection panel in the
     * config box. So we do that and then just immediately call the
     * post-dialog function with a positive result.
     */
    conf_set_int(conf, CONF_protocol, -1);
    after(afterctx, 1);
}

void cleanup_exit(int code)
{
    exit(code);
}

char *make_default_wintitle(char *hostname)
{
    return dupstr("pterm");
}

void setup(int single)
{
    extern void pty_pre_init(void);    /* declared in pty.c */

    cmdline_tooltype = TOOLTYPE_NONNETWORK;
    default_protocol = -1;

    if (single)
        pty_pre_init();
}
