/*
 * PLink - a command-line (stdin/stdout) variant of PuTTY.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include "putty.h"
#include "ssh.h"
#include "storage.h"
#include "tree234.h"

#define MAX_STDIN_BACKLOG 4096

static LogContext *logctx;

static struct termios orig_termios;

void cmdline_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    console_print_error_msg_fmt_v("plink", fmt, ap);
    va_end(ap);
    exit(1);
}

static bool local_tty = false; /* do we have a local tty? */

static Backend *backend;
static Conf *conf;

/*
 * Default settings that are specific to Unix plink.
 */
char *platform_default_s(const char *name)
{
    if (!strcmp(name, "TermType"))
        return dupstr(getenv("TERM"));
    if (!strcmp(name, "SerialLine"))
        return dupstr("/dev/ttyS0");
    return NULL;
}

bool platform_default_b(const char *name, bool def)
{
    return def;
}

int platform_default_i(const char *name, int def)
{
    return def;
}

FontSpec *platform_default_fontspec(const char *name)
{
    return fontspec_new("");
}

Filename *platform_default_filename(const char *name)
{
    if (!strcmp(name, "LogFileName"))
        return filename_from_str("putty.log");
    else
        return filename_from_str("");
}

char *x_get_default(const char *key)
{
    return NULL;                       /* this is a stub */
}
static void plink_echoedit_update(Seat *seat, bool echo, bool edit)
{
    /* Update stdin read mode to reflect changes in line discipline. */
    struct termios mode;

    if (!local_tty) return;

    mode = orig_termios;

    if (echo)
        mode.c_lflag |= ECHO;
    else
        mode.c_lflag &= ~ECHO;

    if (edit) {
        mode.c_iflag |= ICRNL;
        mode.c_lflag |= ISIG | ICANON;
        mode.c_oflag |= OPOST;
    } else {
        mode.c_iflag &= ~ICRNL;
        mode.c_lflag &= ~(ISIG | ICANON);
        mode.c_oflag &= ~OPOST;
        /* Solaris sets these to unhelpful values */
        mode.c_cc[VMIN] = 1;
        mode.c_cc[VTIME] = 0;
        /* FIXME: perhaps what we do with IXON/IXOFF should be an
         * argument to the echoedit_update() method, to allow
         * implementation of SSH-2 "xon-xoff" and Rlogin's
         * equivalent? */
        mode.c_iflag &= ~IXON;
        mode.c_iflag &= ~IXOFF;
    }
    /*
     * Mark parity errors and (more important) BREAK on input.  This
     * is more complex than it need be because POSIX-2001 suggests
     * that escaping of valid 0xff in the input stream is dependent on
     * IGNPAR being clear even though marking of BREAK isn't.  NetBSD
     * 2.0 goes one worse and makes it dependent on INPCK too.  We
     * deal with this by forcing these flags into a useful state and
     * then faking the state in which we found them in from_tty() if
     * we get passed a parity or framing error.
     */
    mode.c_iflag = (mode.c_iflag | INPCK | PARMRK) & ~IGNPAR;

    tcsetattr(STDIN_FILENO, TCSANOW, &mode);
}

/* Helper function to extract a special character from a termios. */
static char *get_ttychar(struct termios *t, int index)
{
    cc_t c = t->c_cc[index];
#if defined(_POSIX_VDISABLE)
    if (c == _POSIX_VDISABLE)
        return dupstr("");
#endif
    return dupprintf("^<%d>", c);
}

static char *plink_get_ttymode(Seat *seat, const char *mode)
{
    /*
     * Propagate appropriate terminal modes from the local terminal,
     * if any.
     */
    if (!local_tty) return NULL;

#define GET_CHAR(ourname, uxname) \
    do { \
        if (strcmp(mode, ourname) == 0) \
            return get_ttychar(&orig_termios, uxname); \
    } while(0)
#define GET_BOOL(ourname, uxname, uxmemb, transform) \
    do { \
        if (strcmp(mode, ourname) == 0) { \
            bool b = (orig_termios.uxmemb & uxname) != 0; \
            transform; \
            return dupprintf("%d", b); \
        } \
    } while (0)

    /*
     * Modes that want to be the same on all terminal devices involved.
     */
    /* All the special characters supported by SSH */
#if defined(VINTR)
    GET_CHAR("INTR", VINTR);
#endif
#if defined(VQUIT)
    GET_CHAR("QUIT", VQUIT);
#endif
#if defined(VERASE)
    GET_CHAR("ERASE", VERASE);
#endif
#if defined(VKILL)
    GET_CHAR("KILL", VKILL);
#endif
#if defined(VEOF)
    GET_CHAR("EOF", VEOF);
#endif
#if defined(VEOL)
    GET_CHAR("EOL", VEOL);
#endif
#if defined(VEOL2)
    GET_CHAR("EOL2", VEOL2);
#endif
#if defined(VSTART)
    GET_CHAR("START", VSTART);
#endif
#if defined(VSTOP)
    GET_CHAR("STOP", VSTOP);
#endif
#if defined(VSUSP)
    GET_CHAR("SUSP", VSUSP);
#endif
#if defined(VDSUSP)
    GET_CHAR("DSUSP", VDSUSP);
#endif
#if defined(VREPRINT)
    GET_CHAR("REPRINT", VREPRINT);
#endif
#if defined(VWERASE)
    GET_CHAR("WERASE", VWERASE);
#endif
#if defined(VLNEXT)
    GET_CHAR("LNEXT", VLNEXT);
#endif
#if defined(VFLUSH)
    GET_CHAR("FLUSH", VFLUSH);
#endif
#if defined(VSWTCH)
    GET_CHAR("SWTCH", VSWTCH);
#endif
#if defined(VSTATUS)
    GET_CHAR("STATUS", VSTATUS);
#endif
#if defined(VDISCARD)
    GET_CHAR("DISCARD", VDISCARD);
#endif
    /* Modes that "configure" other major modes. These should probably be
     * considered as user preferences. */
    /* Configuration of ICANON */
#if defined(ECHOK)
    GET_BOOL("ECHOK", ECHOK, c_lflag, );
#endif
#if defined(ECHOKE)
    GET_BOOL("ECHOKE", ECHOKE, c_lflag, );
#endif
#if defined(ECHOE)
    GET_BOOL("ECHOE", ECHOE, c_lflag, );
#endif
#if defined(ECHONL)
    GET_BOOL("ECHONL", ECHONL, c_lflag, );
#endif
#if defined(XCASE)
    GET_BOOL("XCASE", XCASE, c_lflag, );
#endif
#if defined(IUTF8)
    GET_BOOL("IUTF8", IUTF8, c_iflag, );
#endif
    /* Configuration of ECHO */
#if defined(ECHOCTL)
    GET_BOOL("ECHOCTL", ECHOCTL, c_lflag, );
#endif
    /* Configuration of IXON/IXOFF */
#if defined(IXANY)
    GET_BOOL("IXANY", IXANY, c_iflag, );
#endif
    /* Configuration of OPOST */
#if defined(OLCUC)
    GET_BOOL("OLCUC", OLCUC, c_oflag, );
#endif
#if defined(ONLCR)
    GET_BOOL("ONLCR", ONLCR, c_oflag, );
#endif
#if defined(OCRNL)
    GET_BOOL("OCRNL", OCRNL, c_oflag, );
#endif
#if defined(ONOCR)
    GET_BOOL("ONOCR", ONOCR, c_oflag, );
#endif
#if defined(ONLRET)
    GET_BOOL("ONLRET", ONLRET, c_oflag, );
#endif

    /*
     * Modes that want to be set in only one place, and that we have
     * squashed locally.
     */
#if defined(ISIG)
    GET_BOOL("ISIG", ISIG, c_lflag, );
#endif
#if defined(ICANON)
    GET_BOOL("ICANON", ICANON, c_lflag, );
#endif
#if defined(ECHO)
    GET_BOOL("ECHO", ECHO, c_lflag, );
#endif
#if defined(IXON)
    GET_BOOL("IXON", IXON, c_iflag, );
#endif
#if defined(IXOFF)
    GET_BOOL("IXOFF", IXOFF, c_iflag, );
#endif
#if defined(OPOST)
    GET_BOOL("OPOST", OPOST, c_oflag, );
#endif

    /*
     * We do not propagate the following modes:
     *  - Parity/serial settings, which are a local affair and don't
     *    make sense propagated over SSH's 8-bit byte-stream.
     *      IGNPAR PARMRK INPCK CS7 CS8 PARENB PARODD
     *  - Things that want to be enabled in one place that we don't
     *    squash locally.
     *      IUCLC
     *  - Status bits.
     *      PENDIN
     *  - Things I don't know what to do with. (FIXME)
     *      ISTRIP IMAXBEL NOFLSH TOSTOP IEXTEN
     *      INLCR IGNCR ICRNL
     */

#undef GET_CHAR
#undef GET_BOOL

    /* Fall through to here for unrecognised names, or ones that are
     * unsupported on this platform */
    return NULL;
}

void cleanup_termios(void)
{
    if (local_tty)
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

static bufchain stdout_data, stderr_data;
static bufchain_sink stdout_bcs, stderr_bcs;
static StripCtrlChars *stdout_scc, *stderr_scc;
static BinarySink *stdout_bs, *stderr_bs;

static enum { EOF_NO, EOF_PENDING, EOF_SENT } outgoingeof;

size_t try_output(bool is_stderr)
{
    bufchain *chain = (is_stderr ? &stderr_data : &stdout_data);
    int fd = (is_stderr ? STDERR_FILENO : STDOUT_FILENO);
    ssize_t ret;

    if (bufchain_size(chain) > 0) {
        bool prev_nonblock = nonblock(fd);
        ptrlen senddata;
        do {
            senddata = bufchain_prefix(chain);
            ret = write(fd, senddata.ptr, senddata.len);
            if (ret > 0)
                bufchain_consume(chain, ret);
        } while (ret == senddata.len && bufchain_size(chain) != 0);
        if (!prev_nonblock)
            no_nonblock(fd);
        if (ret < 0 && errno != EAGAIN) {
            perror(is_stderr ? "stderr: write" : "stdout: write");
            exit(1);
        }
    }
    if (outgoingeof == EOF_PENDING && bufchain_size(&stdout_data) == 0) {
        close(STDOUT_FILENO);
        outgoingeof = EOF_SENT;
    }
    return bufchain_size(&stdout_data) + bufchain_size(&stderr_data);
}

static size_t plink_output(
    Seat *seat, bool is_stderr, const void *data, size_t len)
{
    assert(is_stderr || outgoingeof == EOF_NO);

    BinarySink *bs = is_stderr ? stderr_bs : stdout_bs;
    put_data(bs, data, len);

    return try_output(is_stderr);
}

static bool plink_eof(Seat *seat)
{
    assert(outgoingeof == EOF_NO);
    outgoingeof = EOF_PENDING;
    try_output(false);
    return false;   /* do not respond to incoming EOF with outgoing */
}

static int plink_get_userpass_input(Seat *seat, prompts_t *p, bufchain *input)
{
    int ret;
    ret = cmdline_get_passwd_input(p);
    if (ret == -1)
        ret = console_get_userpass_input(p);
    return ret;
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
    .get_userpass_input = plink_get_userpass_input,
    .notify_remote_exit = nullseat_notify_remote_exit,
    .connection_fatal = console_connection_fatal,
    .update_specials_menu = nullseat_update_specials_menu,
    .get_ttymode = plink_get_ttymode,
    .set_busy_status = nullseat_set_busy_status,
    .verify_ssh_host_key = console_verify_ssh_host_key,
    .confirm_weak_crypto_primitive = console_confirm_weak_crypto_primitive,
    .confirm_weak_cached_hostkey = console_confirm_weak_cached_hostkey,
    .is_utf8 = nullseat_is_never_utf8,
    .echoedit_update = plink_echoedit_update,
    .get_x_display = nullseat_get_x_display,
    .get_windowid = nullseat_get_windowid,
    .get_window_pixel_size = nullseat_get_window_pixel_size,
    .stripctrl_new = console_stripctrl_new,
    .set_trust_status = console_set_trust_status,
    .verbose = cmdline_seat_verbose,
    .interactive = plink_seat_interactive,
    .get_cursor_position = nullseat_get_cursor_position,
};
static Seat plink_seat[1] = {{ &plink_seat_vt }};

/*
 * Handle data from a local tty in PARMRK format.
 */
static void from_tty(void *vbuf, unsigned len)
{
    char *p, *q, *end, *buf = vbuf;
    static enum {NORMAL, FF, FF00} state = NORMAL;

    p = buf; end = buf + len;
    while (p < end) {
        switch (state) {
            case NORMAL:
                if (*p == '\xff') {
                    p++;
                    state = FF;
                } else {
                    q = memchr(p, '\xff', end - p);
                    if (q == NULL) q = end;
                    backend_send(backend, p, q - p);
                    p = q;
                }
                break;
            case FF:
                if (*p == '\xff') {
                    backend_send(backend, p, 1);
                    p++;
                    state = NORMAL;
                } else if (*p == '\0') {
                    p++;
                    state = FF00;
                } else abort();
                break;
            case FF00:
                if (*p == '\0') {
                    backend_special(backend, SS_BRK, 0);
                } else {
                    /*
                     * Pretend that PARMRK wasn't set.  This involves
                     * faking what INPCK and IGNPAR would have done if
                     * we hadn't overridden them.  Unfortunately, we
                     * can't do this entirely correctly because INPCK
                     * distinguishes between framing and parity
                     * errors, but PARMRK format represents both in
                     * the same way.  We assume that parity errors are
                     * more common than framing errors, and hence
                     * treat all input errors as being subject to
                     * INPCK.
                     */
                    if (orig_termios.c_iflag & INPCK) {
                        /* If IGNPAR is set, we throw away the character. */
                        if (!(orig_termios.c_iflag & IGNPAR)) {
                            /* PE/FE get passed on as NUL. */
                            *p = 0;
                            backend_send(backend, p, 1);
                        }
                    } else {
                        /* INPCK not set.  Assume we got a parity error. */
                        backend_send(backend, p, 1);
                    }
                }
                p++;
                state = NORMAL;
        }
    }
}

static int signalpipe[2];

void sigwinch(int signum)
{
    if (write(signalpipe[1], "x", 1) <= 0)
        /* not much we can do about it */;
}

/*
 * Short description of parameters.
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
    printf("  -pw passw login with specified password\n");
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

void frontend_net_error_pending(void) {}

const bool share_can_be_downstream = true;
const bool share_can_be_upstream = true;

const bool buildinfo_gtk_relevant = false;

const unsigned cmdline_tooltype =
    TOOLTYPE_HOST_ARG |
    TOOLTYPE_HOST_ARG_CAN_BE_SESSION |
    TOOLTYPE_HOST_ARG_PROTOCOL_PREFIX |
    TOOLTYPE_HOST_ARG_FROM_LAUNCHABLE_LOAD;

static bool seen_stdin_eof = false;

static bool plink_pw_setup(void *vctx, pollwrapper *pw)
{
    pollwrap_add_fd_rwx(pw, signalpipe[0], SELECT_R);

    if (!seen_stdin_eof &&
        backend_connected(backend) &&
        backend_sendok(backend) &&
        backend_sendbuffer(backend) < MAX_STDIN_BACKLOG) {
        /* If we're OK to send, then try to read from stdin. */
        pollwrap_add_fd_rwx(pw, STDIN_FILENO, SELECT_R);
    }

    if (bufchain_size(&stdout_data) > 0) {
        /* If we have data for stdout, try to write to stdout. */
        pollwrap_add_fd_rwx(pw, STDOUT_FILENO, SELECT_W);
    }

    if (bufchain_size(&stderr_data) > 0) {
        /* If we have data for stderr, try to write to stderr. */
        pollwrap_add_fd_rwx(pw, STDERR_FILENO, SELECT_W);
    }

    return true;
}

static void plink_pw_check(void *vctx, pollwrapper *pw)
{
    if (pollwrap_check_fd_rwx(pw, signalpipe[0], SELECT_R)) {
        char c[1];
        struct winsize size;
        if (read(signalpipe[0], c, 1) <= 0)
            /* ignore error */;
        /* ignore its value; it'll be `x' */
        if (ioctl(STDIN_FILENO, TIOCGWINSZ, (void *)&size) >= 0)
            backend_size(backend, size.ws_col, size.ws_row);
    }

    if (pollwrap_check_fd_rwx(pw, STDIN_FILENO, SELECT_R)) {
        char buf[4096];
        int ret;

        if (backend_connected(backend)) {
            ret = read(STDIN_FILENO, buf, sizeof(buf));
            noise_ultralight(NOISE_SOURCE_IOLEN, ret);
            if (ret < 0) {
                perror("stdin: read");
                exit(1);
            } else if (ret == 0) {
                backend_special(backend, SS_EOF, 0);
                seen_stdin_eof = true;
            } else {
                if (local_tty)
                    from_tty(buf, ret);
                else
                    backend_send(backend, buf, ret);
            }
        }
    }

    if (pollwrap_check_fd_rwx(pw, STDOUT_FILENO, SELECT_W)) {
        backend_unthrottle(backend, try_output(false));
    }

    if (pollwrap_check_fd_rwx(pw, STDERR_FILENO, SELECT_W)) {
        backend_unthrottle(backend, try_output(true));
    }
}

static bool plink_continue(void *vctx, bool found_any_fd,
                           bool ran_any_callback)
{
    if (!backend_connected(backend) &&
        bufchain_size(&stdout_data) == 0 && bufchain_size(&stderr_data) == 0)
        return false;                  /* terminate main loop */
    return true;
}

int main(int argc, char **argv)
{
    int exitcode;
    bool errors;
    enum TriState sanitise_stdout = AUTO, sanitise_stderr = AUTO;
    bool use_subsystem = false;
    bool just_test_share_exists = false;
    struct winsize size;
    const struct BackendVtable *backvt;

    /*
     * Initialise port and protocol to sensible defaults. (These
     * will be overridden by more or less anything.)
     */
    settings_set_default_protocol(PROT_SSH);
    settings_set_default_port(22);

    bufchain_init(&stdout_data);
    bufchain_init(&stderr_data);
    bufchain_sink_init(&stdout_bcs, &stdout_data);
    bufchain_sink_init(&stderr_bcs, &stderr_data);
    stdout_bs = BinarySink_UPCAST(&stdout_bcs);
    stderr_bs = BinarySink_UPCAST(&stderr_bcs);
    outgoingeof = EOF_NO;

    stderr_tty_init();
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
            exit(0);
        } else if (!strcmp(p, "-pgpfp")) {
            pgp_fingerprints();
            exit(1);
        } else if (!strcmp(p, "-o")) {
            if (argc <= 1) {
                fprintf(stderr,
                        "plink: option \"-o\" requires an argument\n");
                errors = true;
            } else {
                --argc;
                /* Explicitly pass "plink" in place of appname for
                 * error reporting purposes. appname will have been
                 * set by be_foo.c to something more generic, probably
                 * "PuTTY". */
                provide_xrm_string(*++argv, "plink");
            }
        } else if (!strcmp(p, "-shareexists")) {
            just_test_share_exists = true;
        } else if (!strcmp(p, "-fuzznet")) {
            conf_set_int(conf, CONF_proxy_type, PROXY_FUZZ);
            conf_set_str(conf, CONF_proxy_telnet_command, "%host");
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
                put_datapl(cmdbuf, ptrlen_from_asciz(p));
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
     * If we have no better ideas for the remote username, use the local
     * one, as 'ssh' does.
     */
    if (conf_get_str(conf, CONF_username)[0] == '\0') {
        char *user = get_username();
        if (user) {
            conf_set_str(conf, CONF_username, user);
            sfree(user);
        }
    }

    /*
     * Apply subsystem status.
     */
    if (use_subsystem)
        conf_set_bool(conf, CONF_ssh_subsys, true);

    /*
     * Select protocol. This is farmed out into a table in a
     * separate file to enable an ssh-free variant.
     */
    backvt = backend_vt_from_proto(conf_get_int(conf, CONF_protocol));
    if (!backvt) {
        fprintf(stderr,
                "Internal fault: Unsupported protocol found\n");
        return 1;
    }

    if (backvt->flags & BACKEND_NEEDS_TERMINAL) {
        fprintf(stderr,
                "Plink doesn't support %s, which needs terminal emulation\n",
                backvt->displayname);
        return 1;
    }

    /*
     * Block SIGPIPE, so that we'll get EPIPE individually on
     * particular network connections that go wrong.
     */
    putty_signal(SIGPIPE, SIG_IGN);

    /*
     * Set up the pipe we'll use to tell us about SIGWINCH.
     */
    if (pipe(signalpipe) < 0) {
        perror("pipe");
        exit(1);
    }
    /* We don't want the signal handler to block if the pipe's full. */
    nonblock(signalpipe[0]);
    nonblock(signalpipe[1]);
    cloexec(signalpipe[0]);
    cloexec(signalpipe[1]);
    putty_signal(SIGWINCH, sigwinch);

    /*
     * Now that we've got the SIGWINCH handler installed, try to find
     * out the initial terminal size.
     */
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &size) >= 0) {
        conf_set_int(conf, CONF_width, size.ws_col);
        conf_set_int(conf, CONF_height, size.ws_row);
    }

    /*
     * Decide whether to sanitise control sequences out of standard
     * output and standard error.
     *
     * If we weren't given a command-line override, we do this if (a)
     * the fd in question is pointing at a terminal, and (b) we aren't
     * trying to allocate a terminal as part of the session.
     *
     * (Rationale: the risk of control sequences is that they cause
     * confusion when sent to a local terminal, so if there isn't one,
     * no problem. Also, if we allocate a remote terminal, then we
     * sent a terminal type, i.e. we told it what kind of escape
     * sequences we _like_, i.e. we were expecting to receive some.)
     */
    if (sanitise_stdout == FORCE_ON ||
        (sanitise_stdout == AUTO && isatty(STDOUT_FILENO) &&
         conf_get_bool(conf, CONF_nopty))) {
        stdout_scc = stripctrl_new(stdout_bs, true, L'\0');
        stdout_bs = BinarySink_UPCAST(stdout_scc);
    }
    if (sanitise_stderr == FORCE_ON ||
        (sanitise_stderr == AUTO && isatty(STDERR_FILENO) &&
         conf_get_bool(conf, CONF_nopty))) {
        stderr_scc = stripctrl_new(stderr_bs, true, L'\0');
        stderr_bs = BinarySink_UPCAST(stderr_scc);
    }

    sk_init();
    uxsel_init();

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

    if (just_test_share_exists) {
        if (!backvt->test_for_upstream) {
            fprintf(stderr, "Connection sharing not supported for this "
                    "connection type (%s)'\n", backvt->displayname);
            return 1;
        }
        if (backvt->test_for_upstream(conf_get_str(conf, CONF_host),
                                      conf_get_int(conf, CONF_port), conf))
            return 0;
        else
            return 1;
    }

    /*
     * Start up the connection.
     */
    logctx = log_init(console_cli_logpolicy, conf);
    {
        char *error, *realhost;
        /* nodelay is only useful if stdin is a terminal device */
        bool nodelay = conf_get_bool(conf, CONF_tcp_nodelay) && isatty(0);

        /* This is a good place for a fuzzer to fork us. */
#ifdef __AFL_HAVE_MANUAL_CONTROL
        __AFL_INIT();
#endif

        error = backend_init(backvt, plink_seat, &backend, logctx, conf,
                             conf_get_str(conf, CONF_host),
                             conf_get_int(conf, CONF_port),
                             &realhost, nodelay,
                             conf_get_bool(conf, CONF_tcp_keepalives));
        if (error) {
            fprintf(stderr, "Unable to open connection:\n%s\n", error);
            sfree(error);
            return 1;
        }
        ldisc_create(conf, NULL, backend, plink_seat);
        sfree(realhost);
    }

    /*
     * Set up the initial console mode. We don't care if this call
     * fails, because we know we aren't necessarily running in a
     * console.
     */
    local_tty = (tcgetattr(STDIN_FILENO, &orig_termios) == 0);
    atexit(cleanup_termios);
    seat_echoedit_update(plink_seat, 1, 1);

    cli_main_loop(plink_pw_setup, plink_pw_check, plink_continue, NULL);

    exitcode = backend_exitcode(backend);
    if (exitcode < 0) {
        fprintf(stderr, "Remote process exit code unavailable\n");
        exitcode = 1;                  /* this is an error condition */
    }
    cleanup_exit(exitcode);
    return exitcode;                   /* shouldn't happen, but placates gcc */
}
