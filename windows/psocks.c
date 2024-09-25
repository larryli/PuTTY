/*
 * Main program for Windows psocks.
 */

#include "putty.h"
#include "ssh.h"
#include "psocks.h"

static const PsocksPlatform platform = {
    NULL /* open_pipes */,
    NULL /* found_subcommand */,
    NULL /* start_subcommand */,
};

int main(int argc, char **argv)
{
    psocks_state *ps = psocks_new(&platform);
    CmdlineArgList *arglist = cmdline_arg_list_from_GetCommandLineW();
    psocks_cmdline(ps, arglist);

    sk_init();
    winselcli_setup();
    psocks_start(ps);

    cli_main_loop(cliloop_null_pre, cliloop_null_post, NULL);
}
