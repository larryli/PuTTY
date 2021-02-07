/*
 * pterm main program.
 */

#include <stdio.h>
#include <stdlib.h>

#include "putty.h"

const char *const appname = "pterm";
const bool use_event_log = false;      /* pterm doesn't need it */
const bool new_session = false, saved_sessions = false; /* or these */
const bool dup_check_launchable = false; /* no need to check host name
                                          * in conf */
const bool use_pty_argv = true;

const unsigned cmdline_tooltype = TOOLTYPE_NONNETWORK;

/* gtkwin.c will call this, and in pterm it's not needed */
void noise_ultralight(NoiseSourceId id, unsigned long data) { }

const struct BackendVtable *select_backend(Conf *conf)
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

void setup(bool single)
{
    settings_set_default_protocol(-1);

    if (single)
        pty_pre_init();
}
