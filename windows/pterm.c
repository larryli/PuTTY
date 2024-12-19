#include "putty.h"
#include "storage.h"

const unsigned cmdline_tooltype =
    TOOLTYPE_NONNETWORK |
    TOOLTYPE_NO_VERBOSE_OPTION;

void gui_term_process_cmdline(Conf *conf, char *cmdline)
{
    do_defaults(NULL, conf);
    conf_set_str(conf, CONF_remote_cmd, "");

    cmdline = handle_restrict_acl_cmdline_prefix(cmdline);
    if (handle_special_sessionname_cmdline(cmdline, conf) ||
        handle_special_filemapping_cmdline(cmdline, conf))
        return;

    CmdlineArgList *arglist = cmdline_arg_list_from_GetCommandLineW();
    size_t arglistpos = 0;
    while (arglist->args[arglistpos]) {
        CmdlineArg *arg = arglist->args[arglistpos++];
        CmdlineArg *nextarg = arglist->args[arglistpos];
        const char *argstr = cmdline_arg_to_str(arg);
        int retd = cmdline_process_param(arg, nextarg, 1, conf);
        if (retd == -2) {
            cmdline_error("option \"%s\" requires an argument", argstr);
        } else if (retd == 2) {
            arglistpos++;              /* skip next argument */
        } else if (retd == 1) {
            continue;          /* nothing further needs doing */
        } else if (!strcmp(argstr, "-e")) {
            if (nextarg) {
                /* The command to execute is taken to be the unparsed
                 * version of the whole remainder of the command line. */
                char *cmd = cmdline_arg_remainder_utf8(nextarg);
                conf_set_utf8(conf, CONF_remote_cmd, cmd);
                sfree(cmd);
                return;
            } else {
                cmdline_error("option \"%s\" requires an argument", argstr);
            }
        } else if (argstr[0] == '-') {
            cmdline_error("unrecognised option \"%s\"", argstr);
        } else {
            cmdline_error("unexpected non-option argument \"%s\"", argstr);
        }
    }

    cmdline_run_saved(conf);

    conf_set_int(conf, CONF_sharrow_type, SHARROW_BITMAP);
}

const struct BackendVtable *backend_vt_from_conf(Conf *conf)
{
    return &conpty_backend;
}

const wchar_t *get_app_user_model_id(void)
{
    return L"SimonTatham.Pterm";
}

void gui_terminal_ready(HWND hwnd, Seat *seat, Backend *backend)
{
}
