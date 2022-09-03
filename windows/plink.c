/*
 * PLink - a Windows command-line (stdin/stdout) variant of PuTTY.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>

#include "putty.h"
#include "ssh.h"
#include "storage.h"
#include "tree234.h"
#include "security-api.h"

void cmdline_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    console_print_error_msg_fmt_v("plink", fmt, ap);
    va_end(ap);
    exit(1);
}

static HANDLE inhandle, outhandle, errhandle;
static struct handle *stdin_handle, *stdout_handle, *stderr_handle;
static handle_sink stdout_hs, stderr_hs;
static StripCtrlChars *stdout_scc, *stderr_scc;
static BinarySink *stdout_bs, *stderr_bs;
static DWORD orig_console_mode;

static Backend *backend;
static LogContext *logctx;
static Conf *conf;

static void plink_echoedit_update(Seat *seat, bool echo, bool edit)
{
    /* Update stdin read mode to reflect changes in line discipline. */
    DWORD mode;

    mode = ENABLE_PROCESSED_INPUT;
    if (echo)
        mode = mode | ENABLE_ECHO_INPUT;
    else
        mode = mode & ~ENABLE_ECHO_INPUT;
    if (edit)
        mode = mode | ENABLE_LINE_INPUT;
    else
        mode = mode & ~ENABLE_LINE_INPUT;
    SetConsoleMode(inhandle, mode);
}

static size_t plink_output(
    Seat *seat, SeatOutputType type, const void *data, size_t len)
{
    bool is_stderr = type != SEAT_OUTPUT_STDOUT;
    BinarySink *bs = is_stderr ? stderr_bs : stdout_bs;
    put_data(bs, data, len);

    return handle_backlog(stdout_handle) + handle_backlog(stderr_handle);
}

static bool plink_eof(Seat *seat)
{
    handle_write_eof(stdout_handle);
    return false;   /* do not respond to incoming EOF with outgoing */
}

static SeatPromptResult plink_get_userpass_input(Seat *seat, prompts_t *p)
{
    /* Plink doesn't support Restart Session, so we can just have a
     * single static cmdline_get_passwd_input_state that's never reset */
    static cmdline_get_passwd_input_state cmdline_state =
        CMDLINE_GET_PASSWD_INPUT_STATE_INIT;

    SeatPromptResult spr;
    spr = cmdline_get_passwd_input(p, &cmdline_state, false);
    if (spr.kind == SPRK_INCOMPLETE)
        spr = console_get_userpass_input(p);
    return spr;
}

static bool plink_seat_interactive(Seat *seat)
{
    return (!*conf_get_str(conf, CONF_remote_cmd) &&
            !*conf_get_str(conf, CONF_remote_cmd2) &&
            !*conf_get_str(conf, CONF_ssh_nc_host));
}

static const SeatVtable plink_seat_vt = {
    .output = plink_output,
    .eof = plink_eof,
    .sent = nullseat_sent,
    .banner = nullseat_banner_to_stderr,
    .get_userpass_input = plink_get_userpass_input,
    .notify_session_started = nullseat_notify_session_started,
    .notify_remote_exit = nullseat_notify_remote_exit,
    .notify_remote_disconnect = nullseat_notify_remote_disconnect,
    .connection_fatal = console_connection_fatal,
    .update_specials_menu = nullseat_update_specials_menu,
    .get_ttymode = nullseat_get_ttymode,
    .set_busy_status = nullseat_set_busy_status,
    .confirm_ssh_host_key = console_confirm_ssh_host_key,
    .confirm_weak_crypto_primitive = console_confirm_weak_crypto_primitive,
    .confirm_weak_cached_hostkey = console_confirm_weak_cached_hostkey,
    .prompt_descriptions = console_prompt_descriptions,
    .is_utf8 = nullseat_is_never_utf8,
    .echoedit_update = plink_echoedit_update,
    .get_x_display = nullseat_get_x_display,
    .get_windowid = nullseat_get_windowid,
    .get_window_pixel_size = nullseat_get_window_pixel_size,
    .stripctrl_new = console_stripctrl_new,
    .set_trust_status = console_set_trust_status,
    .can_set_trust_status = console_can_set_trust_status,
    .has_mixed_input_stream = console_has_mixed_input_stream,
    .verbose = cmdline_seat_verbose,
    .interactive = plink_seat_interactive,
    .get_cursor_position = nullseat_get_cursor_position,
};
static Seat plink_seat[1] = {{ &plink_seat_vt }};

static DWORD main_thread_id;

/*
 *  Short description of parameters.
 */
static void usage(void)
{
    printf("Plink: command-line connection utility\n");
    printf("%s\n", ver);
    printf("Usage: plink [options] [user@]host [command]\n");
    printf("       (\"host\" can also be a PuTTY saved session name)\n");
    printf("Options:\n");
    printf("  -V        print version information and exit\n");
    printf("  -pgpfp    print PGP key fingerprints and exit\n");
    printf("  -v        show verbose messages\n");
    printf("  -load sessname  Load settings from saved session\n");
    printf("  -ssh -telnet -rlogin -raw -serial\n");
    printf("            force use of a particular protocol\n");
    printf("  -ssh-connection\n");
    printf("            force use of the bare ssh-connection protocol\n");
    printf("  -P port   connect to specified port\n");
    printf("  -l user   connect with specified username\n");
    printf("  -batch    disable all interactive prompts\n");
    printf("  -proxycmd command\n");
    printf("            use 'command' as local proxy\n");
    printf("  -sercfg configuration-string (e.g. 19200,8,n,1,X)\n");
    printf("            Specify the serial configuration (serial only)\n");
    printf("The following options only apply to SSH connections:\n");
    printf("  -pwfile file   login with password read from specified file\n");
    printf("  -D [listen-IP:]listen-port\n");
    printf("            Dynamic SOCKS-based port forwarding\n");
    printf("  -L [listen-IP:]listen-port:host:port\n");
    printf("            Forward local port to remote address\n");
    printf("  -R [listen-IP:]listen-port:host:port\n");
    printf("            Forward remote port to local address\n");
    printf("  -X -x     enable / disable X11 forwarding\n");
    printf("  -A -a     enable / disable agent forwarding\n");
    printf("  -t -T     enable / disable pty allocation\n");
    printf("  -1 -2     force use of particular SSH protocol version\n");
    printf("  -4 -6     force use of IPv4 or IPv6\n");
    printf("  -C        enable compression\n");
    printf("  -i key    private key file for user authentication\n");
    printf("  -noagent  disable use of Pageant\n");
    printf("  -agent    enable use of Pageant\n");
    printf("  -no-trivial-auth\n");
    printf("            disconnect if SSH authentication succeeds trivially\n");
    printf("  -noshare  disable use of connection sharing\n");
    printf("  -share    enable use of connection sharing\n");
    printf("  -hostkey keyid\n");
    printf("            manually specify a host key (may be repeated)\n");
    printf("  -sanitise-stderr, -sanitise-stdout, "
           "-no-sanitise-stderr, -no-sanitise-stdout\n");
    printf("            do/don't strip control chars from standard "
           "output/error\n");
    printf("  -no-antispoof   omit anti-spoofing prompt after "
           "authentication\n");
    printf("  -m file   read remote command(s) from file\n");
    printf("  -s        remote command is an SSH subsystem (SSH-2 only)\n");
    printf("  -N        don't start a shell/command (SSH-2 only)\n");
    printf("  -nc host:port\n");
    printf("            open tunnel in place of session (SSH-2 only)\n");
    printf("  -sshlog file\n");
    printf("  -sshrawlog file\n");
    printf("            log protocol details to a file\n");
    printf("  -logoverwrite\n");
    printf("  -logappend\n");
    printf("            control what happens when a log file already exists\n");
    printf("  -shareexists\n");
    printf("            test whether a connection-sharing upstream exists\n");
    exit(1);
}

static void version(void)
{
    char *buildinfo_text = buildinfo("\n");
    printf("plink: %s\n%s\n", ver, buildinfo_text);
    sfree(buildinfo_text);
    exit(0);
}

size_t stdin_gotdata(struct handle *h, const void *data, size_t len, int err)
{
    if (err) {
        char buf[4096];
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, 0,
                      buf, lenof(buf), NULL);
        buf[lenof(buf)-1] = '\0';
        if (buf[strlen(buf)-1] == '\n')
            buf[strlen(buf)-1] = '\0';
        fprintf(stderr, "Unable to read from standard input: %s\n", buf);
        cleanup_exit(0);
    }

    noise_ultralight(NOISE_SOURCE_IOLEN, len);
    if (backend_connected(backend)) {
        if (len > 0) {
            backend_send(backend, data, len);
            return backend_sendbuffer(backend);
        } else {
            backend_special(backend, SS_EOF, 0);
            return 0;
        }
    } else
        return 0;
}

void stdouterr_sent(struct handle *h, size_t new_backlog, int err, bool close)
{
    if (close) {
        CloseHandle(outhandle);
        CloseHandle(errhandle);
        outhandle = errhandle = INVALID_HANDLE_VALUE;
    }

    if (err) {
        char buf[4096];
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, 0,
                      buf, lenof(buf), NULL);
        buf[lenof(buf)-1] = '\0';
        if (buf[strlen(buf)-1] == '\n')
            buf[strlen(buf)-1] = '\0';
        fprintf(stderr, "Unable to write to standard %s: %s\n",
                (h == stdout_handle ? "output" : "error"), buf);
        cleanup_exit(0);
    }

    if (backend_connected(backend)) {
        backend_unthrottle(backend, (handle_backlog(stdout_handle) +
                                     handle_backlog(stderr_handle)));
    }
}

const bool share_can_be_downstream = true;
const bool share_can_be_upstream = true;

const unsigned cmdline_tooltype =
    TOOLTYPE_HOST_ARG |
    TOOLTYPE_HOST_ARG_CAN_BE_SESSION |
    TOOLTYPE_HOST_ARG_PROTOCOL_PREFIX |
    TOOLTYPE_HOST_ARG_FROM_LAUNCHABLE_LOAD;

static bool sending;

static bool plink_mainloop_pre(void *vctx, const HANDLE **extra_handles,
                               size_t *n_extra_handles)
{
    if (!sending && backend_sendok(backend)) {
        stdin_handle = handle_input_new(inhandle, stdin_gotdata, NULL,
                                        0);
        sending = true;
    }

    return true;
}

static bool plink_mainloop_post(void *vctx, size_t extra_handle_index)
{
    if (sending)
        handle_unthrottle(stdin_handle, backend_sendbuffer(backend));

    if (!backend_connected(backend) &&
        handle_backlog(stdout_handle) + handle_backlog(stderr_handle) == 0)
        return false; /* we closed the connection */

    return true;
}

int main(int argc, char **argv)
{
    int exitcode;
    bool errors;
    bool use_subsystem = false;
    bool just_test_share_exists = false;
    enum TriState sanitise_stdout = AUTO, sanitise_stderr = AUTO;
    const struct BackendVtable *vt;

    dll_hijacking_protection();

    /*
     * Initialise port and protocol to sensible defaults. (These
     * will be overridden by more or less anything.)
     */
    settings_set_default_protocol(PROT_SSH);
    settings_set_default_port(22);

    /*
     * Process the command line.
     */
    conf = conf_new();
    do_defaults(NULL, conf);
    settings_set_default_protocol(conf_get_int(conf, CONF_protocol));
    settings_set_default_port(conf_get_int(conf, CONF_port));
    errors = false;
    {
        /*
         * Override the default protocol if PLINK_PROTOCOL is set.
         */
        char *p = getenv("PLINK_PROTOCOL");
        if (p) {
            const struct BackendVtable *vt = backend_vt_from_name(p);
            if (vt) {
                settings_set_default_protocol(vt->protocol);
                settings_set_default_port(vt->default_port);
                conf_set_int(conf, CONF_protocol, vt->protocol);
                conf_set_int(conf, CONF_port, vt->default_port);
            }
        }
    }
    while (--argc) {
        char *p = *++argv;
        int ret = cmdline_process_param(p, (argc > 1 ? argv[1] : NULL),
                                        1, conf);
        if (ret == -2) {
            fprintf(stderr,
                    "plink: option \"%s\" requires an argument\n", p);
            errors = true;
        } else if (ret == 2) {
            --argc, ++argv;
        } else if (ret == 1) {
            continue;
        } else if (!strcmp(p, "-batch")) {
            console_batch_mode = true;
        } else if (!strcmp(p, "-s")) {
            /* Save status to write to conf later. */
            use_subsystem = true;
        } else if (!strcmp(p, "-V") || !strcmp(p, "--version")) {
            version();
        } else if (!strcmp(p, "--help")) {
            usage();
        } else if (!strcmp(p, "-pgpfp")) {
            pgp_fingerprints();
            exit(1);
        } else if (!strcmp(p, "-shareexists")) {
            just_test_share_exists = true;
        } else if (!strcmp(p, "-sanitise-stdout") ||
                   !strcmp(p, "-sanitize-stdout")) {
            sanitise_stdout = FORCE_ON;
        } else if (!strcmp(p, "-no-sanitise-stdout") ||
                   !strcmp(p, "-no-sanitize-stdout")) {
            sanitise_stdout = FORCE_OFF;
        } else if (!strcmp(p, "-sanitise-stderr") ||
                   !strcmp(p, "-sanitize-stderr")) {
            sanitise_stderr = FORCE_ON;
        } else if (!strcmp(p, "-no-sanitise-stderr") ||
                   !strcmp(p, "-no-sanitize-stderr")) {
            sanitise_stderr = FORCE_OFF;
        } else if (!strcmp(p, "-no-antispoof")) {
            console_antispoof_prompt = false;
        } else if (*p != '-') {
            strbuf *cmdbuf = strbuf_new();

            while (argc > 0) {
                if (cmdbuf->len > 0)
                    put_byte(cmdbuf, ' '); /* add space separator */
                put_dataz(cmdbuf, p);
                if (--argc > 0)
                    p = *++argv;
            }

            conf_set_str(conf, CONF_remote_cmd, cmdbuf->s);
            conf_set_str(conf, CONF_remote_cmd2, "");
            conf_set_bool(conf, CONF_nopty, true);  /* command => no tty */

            strbuf_free(cmdbuf);
            break;                     /* done with cmdline */
        } else {
            fprintf(stderr, "plink: unknown option \"%s\"\n", p);
            errors = true;
        }
    }

    if (errors)
        return 1;

    if (!cmdline_host_ok(conf)) {
        usage();
    }

    prepare_session(conf);

    /*
     * Perform command-line overrides on session configuration.
     */
    cmdline_run_saved(conf);

    /*
     * Apply subsystem status.
     */
    if (use_subsystem)
        conf_set_bool(conf, CONF_ssh_subsys, true);

    /*
     * Select protocol. This is farmed out into a table in a
     * separate file to enable an ssh-free variant.
     */
    vt = backend_vt_from_proto(conf_get_int(conf, CONF_protocol));
    if (vt == NULL) {
        fprintf(stderr,
                "Internal fault: Unsupported protocol found\n");
        return 1;
    }

    if (vt->flags & BACKEND_NEEDS_TERMINAL) {
        fprintf(stderr,
                "Plink doesn't support %s, which needs terminal emulation\n",
                vt->displayname_lc);
        return 1;
    }

    sk_init();
    if (p_WSAEventSelect == NULL) {
        fprintf(stderr, "Plink requires WinSock 2\n");
        return 1;
    }

    /*
     * Plink doesn't provide any way to add forwardings after the
     * connection is set up, so if there are none now, we can safely set
     * the "simple" flag.
     */
    if (conf_get_int(conf, CONF_protocol) == PROT_SSH &&
        !conf_get_bool(conf, CONF_x11_forward) &&
        !conf_get_bool(conf, CONF_agentfwd) &&
        !conf_get_str_nthstrkey(conf, CONF_portfwd, 0))
        conf_set_bool(conf, CONF_ssh_simple, true);

    logctx = log_init(console_cli_logpolicy, conf);

    if (just_test_share_exists) {
        if (!vt->test_for_upstream) {
            fprintf(stderr, "Connection sharing not supported for this "
                    "connection type (%s)'\n", vt->displayname_lc);
            return 1;
        }
        if (vt->test_for_upstream(conf_get_str(conf, CONF_host),
                                  conf_get_int(conf, CONF_port), conf))
            return 0;
        else
            return 1;
    }

    if (restricted_acl()) {
        lp_eventlog(console_cli_logpolicy,
                    "Running with restricted process ACL");
    }

    inhandle = GetStdHandle(STD_INPUT_HANDLE);
    outhandle = GetStdHandle(STD_OUTPUT_HANDLE);
    errhandle = GetStdHandle(STD_ERROR_HANDLE);

    /*
     * Turn off ECHO and LINE input modes. We don't care if this
     * call fails, because we know we aren't necessarily running in
     * a console.
     */
    GetConsoleMode(inhandle, &orig_console_mode);
    SetConsoleMode(inhandle, ENABLE_PROCESSED_INPUT);

    /*
     * Pass the output handles to the handle-handling subsystem.
     * (The input one we leave until we're through the
     * authentication process.)
     */
    stdout_handle = handle_output_new(outhandle, stdouterr_sent, NULL, 0);
    stderr_handle = handle_output_new(errhandle, stdouterr_sent, NULL, 0);
    handle_sink_init(&stdout_hs, stdout_handle);
    handle_sink_init(&stderr_hs, stderr_handle);
    stdout_bs = BinarySink_UPCAST(&stdout_hs);
    stderr_bs = BinarySink_UPCAST(&stderr_hs);

    /*
     * Decide whether to sanitise control sequences out of standard
     * output and standard error.
     *
     * If we weren't given a command-line override, we do this if (a)
     * the fd in question is pointing at a console, and (b) we aren't
     * trying to allocate a terminal as part of the session.
     *
     * (Rationale: the risk of control sequences is that they cause
     * confusion when sent to a local console, so if there isn't one,
     * no problem. Also, if we allocate a remote terminal, then we
     * sent a terminal type, i.e. we told it what kind of escape
     * sequences we _like_, i.e. we were expecting to receive some.)
     */
    if (sanitise_stdout == FORCE_ON ||
        (sanitise_stdout == AUTO && is_console_handle(outhandle) &&
         conf_get_bool(conf, CONF_nopty))) {
        stdout_scc = stripctrl_new(stdout_bs, true, L'\0');
        stdout_bs = BinarySink_UPCAST(stdout_scc);
    }
    if (sanitise_stderr == FORCE_ON ||
        (sanitise_stderr == AUTO && is_console_handle(errhandle) &&
         conf_get_bool(conf, CONF_nopty))) {
        stderr_scc = stripctrl_new(stderr_bs, true, L'\0');
        stderr_bs = BinarySink_UPCAST(stderr_scc);
    }

    /*
     * Start up the connection.
     */
    winselcli_setup();                 /* ensure event object exists */
    {
        char *error, *realhost;
        /* nodelay is only useful if stdin is a character device (console) */
        bool nodelay = conf_get_bool(conf, CONF_tcp_nodelay) &&
            (GetFileType(GetStdHandle(STD_INPUT_HANDLE)) == FILE_TYPE_CHAR);

        error = backend_init(vt, plink_seat, &backend, logctx, conf,
                             conf_get_str(conf, CONF_host),
                             conf_get_int(conf, CONF_port),
                             &realhost, nodelay,
                             conf_get_bool(conf, CONF_tcp_keepalives));
        if (error) {
            fprintf(stderr, "Unable to open connection:\n%s", error);
            sfree(error);
            return 1;
        }
        ldisc_create(conf, NULL, backend, plink_seat);
        sfree(realhost);
    }

    main_thread_id = GetCurrentThreadId();

    sending = false;

    cli_main_loop(plink_mainloop_pre, plink_mainloop_post, NULL);

    exitcode = backend_exitcode(backend);
    if (exitcode < 0) {
        fprintf(stderr, "Remote process exit code unavailable\n");
        exitcode = 1;                  /* this is an error condition */
    }
    cleanup_exit(exitcode);
    return 0;                          /* placate compiler warning */
}
