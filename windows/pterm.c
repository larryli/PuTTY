#include "putty.h"
#include "storage.h"

void gui_term_process_cmdline(Conf *conf, char *cmdline)
{
    do_defaults(NULL, conf);
    conf_set_str(conf, CONF_remote_cmd, "");

    cmdline = handle_restrict_acl_cmdline_prefix(cmdline);
    if (handle_special_sessionname_cmdline(cmdline, conf) ||
        handle_special_filemapping_cmdline(cmdline, conf))
        return;

    int argc;
    char **argv, **argstart;
    split_into_argv(cmdline, &argc, &argv, &argstart);

    for (int i = 0; i < argc; i++) {
        const char *arg = argv[i];
        if (!strcmp(arg, "-e")) {
            if (i+1 < argc) {
                /* The command to execute is taken to be the unparsed
                 * version of the whole remainder of the command line. */
                conf_set_str(conf, CONF_remote_cmd, argstart[i+1]);
                return;
            } else {
                cmdline_error("option \"%s\" requires an argument", arg);
            }
        } else if (arg[0] == '-') {
            cmdline_error("unrecognised option \"%s\"", arg);
        } else {
            cmdline_error("unexpected non-option argument \"%s\"", arg);
        }
    }

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
