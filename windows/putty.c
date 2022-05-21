#include "putty.h"
#include "storage.h"

extern bool sesslist_demo_mode;
extern const char *dialog_box_demo_screenshot_filename;
static strbuf *demo_terminal_data = NULL;
static const char *terminal_demo_screenshot_filename;

const unsigned cmdline_tooltype =
    TOOLTYPE_HOST_ARG |
    TOOLTYPE_PORT_ARG |
    TOOLTYPE_NO_VERBOSE_OPTION;

void gui_term_process_cmdline(Conf *conf, char *cmdline)
{
    char *p;
    bool special_launchable_argument = false;
    bool demo_config_box = false;

    settings_set_default_protocol(be_default_protocol);
    /* Find the appropriate default port. */
    {
        const struct BackendVtable *vt =
            backend_vt_from_proto(be_default_protocol);
        settings_set_default_port(0); /* illegal */
        if (vt)
            settings_set_default_port(vt->default_port);
    }
    conf_set_int(conf, CONF_logtype, LGTYP_NONE);

    do_defaults(NULL, conf);

    p = handle_restrict_acl_cmdline_prefix(cmdline);

    if (handle_special_sessionname_cmdline(p, conf)) {
        if (!conf_launchable(conf) && !do_config(conf)) {
            cleanup_exit(0);
        }
        special_launchable_argument = true;
    } else if (handle_special_filemapping_cmdline(p, conf)) {
        special_launchable_argument = true;
    } else if (!*p) {
        /* Do-nothing case for an empty command line - or rather,
         * for a command line that's empty _after_ we strip off
         * the &R prefix. */
    } else {
        /*
         * Otherwise, break up the command line and deal with
         * it sensibly.
         */
        int argc, i;
        char **argv;

        split_into_argv(cmdline, &argc, &argv, NULL);

        for (i = 0; i < argc; i++) {
            char *p = argv[i];
            int ret;

            ret = cmdline_process_param(p, i+1<argc?argv[i+1]:NULL,
                                        1, conf);
            if (ret == -2) {
                cmdline_error("option \"%s\" requires an argument", p);
            } else if (ret == 2) {
                i++;               /* skip next argument */
            } else if (ret == 1) {
                continue;          /* nothing further needs doing */
            } else if (!strcmp(p, "-cleanup")) {
                /*
                 * `putty -cleanup'. Remove all registry
                 * entries associated with PuTTY, and also find
                 * and delete the random seed file.
                 */
                char *s1, *s2;
                s1 = dupprintf("This procedure will remove ALL Registry entries\n"
                               "associated with %s, and will also remove\n"
                               "the random seed file. (This only affects the\n"
                               "currently logged-in user.)\n"
                               "\n"
                               "THIS PROCESS WILL DESTROY YOUR SAVED SESSIONS.\n"
                               "Are you really sure you want to continue?",
                               appname);
                s2 = dupprintf("%s Warning", appname);
                if (message_box(NULL, s1, s2,
                                MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2,
                                HELPCTXID(option_cleanup)) == IDYES) {
                    cleanup_all();
                }
                sfree(s1);
                sfree(s2);
                exit(0);
            } else if (!strcmp(p, "-pgpfp")) {
                pgp_fingerprints_msgbox(NULL);
                exit(1);
            } else if (has_ca_config_box &&
                       (!strcmp(p, "-host-ca") || !strcmp(p, "--host-ca") ||
                        !strcmp(p, "-host_ca") || !strcmp(p, "--host_ca"))) {
                show_ca_config_box(NULL);
                exit(0);
            } else if (!strcmp(p, "-demo-config-box")) {
                if (i+1 >= argc) {
                    cmdline_error("%s expects an output filename", p);
                } else {
                    demo_config_box = true;
                    dialog_box_demo_screenshot_filename = argv[++i];
                }
            } else if (!strcmp(p, "-demo-terminal")) {
                if (i+2 >= argc) {
                    cmdline_error("%s expects input and output filenames", p);
                } else {
                    const char *infile = argv[++i];
                    terminal_demo_screenshot_filename = argv[++i];
                    FILE *fp = fopen(infile, "rb");
                    if (!fp)
                        cmdline_error("can't open input file '%s'", infile);
                    demo_terminal_data = strbuf_new();
                    char buf[4096];
                    int retd;
                    while ((retd = fread(buf, 1, sizeof(buf), fp)) > 0)
                        put_data(demo_terminal_data, buf, retd);
                    fclose(fp);
                }
            } else if (*p != '-') {
                cmdline_error("unexpected argument \"%s\"", p);
            } else {
                cmdline_error("unknown option \"%s\"", p);
            }
        }
    }

    cmdline_run_saved(conf);

    if (demo_config_box) {
        sesslist_demo_mode = true;
        load_open_settings(NULL, conf);
        conf_set_str(conf, CONF_host, "demo-server.example.com");
        do_config(conf);
        cleanup_exit(0);
    } else if (demo_terminal_data) {
        /* Ensure conf will cause an immediate session launch */
        load_open_settings(NULL, conf);
        conf_set_str(conf, CONF_host, "demo-server.example.com");
        conf_set_int(conf, CONF_close_on_exit, FORCE_OFF);
    } else {
        /*
         * Bring up the config dialog if the command line hasn't
         * (explicitly) specified a launchable configuration.
         */
        if (!(special_launchable_argument || cmdline_host_ok(conf))) {
            if (!do_config(conf))
                cleanup_exit(0);
        }
    }

    prepare_session(conf);
}

const struct BackendVtable *backend_vt_from_conf(Conf *conf)
{
    if (demo_terminal_data) {
        return &null_backend;
    }

    /*
     * Select protocol. This is farmed out into a table in a
     * separate file to enable an ssh-free variant.
     */
    const struct BackendVtable *vt = backend_vt_from_proto(
        conf_get_int(conf, CONF_protocol));
    if (!vt) {
        char *str = dupprintf("%s Internal Error", appname);
        MessageBox(NULL, "Unsupported protocol number found",
                   str, MB_OK | MB_ICONEXCLAMATION);
        sfree(str);
        cleanup_exit(1);
    }
    return vt;
}

const wchar_t *get_app_user_model_id(void)
{
    return L"SimonTatham.PuTTY";
}

static void demo_terminal_screenshot(void *ctx, unsigned long now)
{
    HWND hwnd = (HWND)ctx;
    char *err = save_screenshot(hwnd, terminal_demo_screenshot_filename);
    if (err) {
        MessageBox(hwnd, err, "Demo screenshot failure", MB_OK | MB_ICONERROR);
        sfree(err);
    }
    cleanup_exit(0);
}

void gui_terminal_ready(HWND hwnd, Seat *seat, Backend *backend)
{
    if (demo_terminal_data) {
        ptrlen data = ptrlen_from_strbuf(demo_terminal_data);
        seat_stdout(seat, data.ptr, data.len);
        schedule_timer(TICKSPERSEC, demo_terminal_screenshot, (void *)hwnd);
    }
}
