/*
 * 'psusan': Pseudo Ssh for Untappable, Separately Authenticated Networks
 *
 * This is a standalone application that speaks on its standard I/O
 * (or a listening Unix-domain socket) the server end of the bare
 * ssh-connection protocol used by PuTTY's connection sharing.
 *
 * The idea of this tool is that you can use it to communicate across
 * any 8-bit-clean data channel between two inconveniently separated
 * domains, provided the channel is already (as the name suggests)
 * adequately secured against eavesdropping and modification and
 * already authenticated as the right user.
 *
 * If you're sitting at one end of such a channel and want to type
 * commands into the other end, the most obvious thing to do is to run
 * a terminal session directly over it. But if you run psusan at one
 * end, and a PuTTY (or compatible) client at the other end, then you
 * not only get a single terminal session: you get all the other SSH
 * amenities, like the ability to spawn extra terminal sessions,
 * forward ports or X11 connections, even forward an SSH agent.
 *
 * There are a surprising number of channels of that kind; see the man
 * page for some examples.
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
#include "mpint.h"
#include "ssh.h"
#include "ssh/server.h"

void modalfatalbox(const char *p, ...)
{
    va_list ap;
    fprintf(stderr, "FATAL ERROR: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}
void nonfatal(const char *p, ...)
{
    va_list ap;
    fprintf(stderr, "ERROR: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
}

char *platform_default_s(const char *name)
{
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
    return fontspec_new_default();
}

Filename *platform_default_filename(const char *name)
{
    return filename_from_str("");
}

char *x_get_default(const char *key)
{
    return NULL;                       /* this is a stub */
}

void old_keyfile_warning(void) { }

void timer_change_notify(unsigned long next)
{
}

char *platform_get_x_display(void) { return NULL; }

void make_unix_sftp_filehandle_key(void *vdata, size_t size)
{
    /* psusan runs without a random number generator, so we can't make
     * this up by random_read. Fortunately, psusan is also
     * non-adversarial, so it's safe to generate this trivially. */
    unsigned char *data = (unsigned char *)vdata;
    for (size_t i = 0; i < size; i++)
        data[i] = (unsigned)rand() / ((unsigned)RAND_MAX / 256);
}

static bool verbose;

struct server_instance {
    unsigned id;
    LogPolicy logpolicy;
};

static void log_to_stderr(unsigned id, const char *msg)
{
    if (!verbose)
        return;
    if (id != (unsigned)-1)
        fprintf(stderr, "#%u: ", id);
    fputs(msg, stderr);
    fputc('\n', stderr);
    fflush(stderr);
}

static void server_eventlog(LogPolicy *lp, const char *event)
{
    struct server_instance *inst = container_of(
        lp, struct server_instance, logpolicy);
    if (verbose)
        log_to_stderr(inst->id, event);
}

static void server_logging_error(LogPolicy *lp, const char *event)
{
    struct server_instance *inst = container_of(
        lp, struct server_instance, logpolicy);
    log_to_stderr(inst->id, event);    /* unconditional */
}

static int server_askappend(
    LogPolicy *lp, Filename *filename,
    void (*callback)(void *ctx, int result), void *ctx)
{
    return 2; /* always overwrite (FIXME: could make this a cmdline option) */
}

static const LogPolicyVtable server_logpolicy_vt = {
    .eventlog = server_eventlog,
    .askappend = server_askappend,
    .logging_error = server_logging_error,
    .verbose = null_lp_verbose_no,
};

static void show_help(FILE *fp)
{
    fputs("usage:   psusan [options]\n"
          "options: --listen SOCKETPATH  listen for connections on a Unix-domain socket\n"
          "         --listen-once        (with --listen) stop after one connection\n"
          "         --verbose            print log messages to standard error\n"
          "         --sessiondir DIR     cwd for session subprocess (default $HOME)\n"
          "         --sshlog FILE        write ssh-connection packet log to FILE\n"
          "         --sshrawlog FILE     write packets and raw data log to FILE\n"
          "also:    psusan --help        show this text\n"
          "         psusan --version     show version information\n", fp);
}

static void show_version_and_exit(void)
{
    char *buildinfo_text = buildinfo("\n");
    printf("%s: %s\n%s\n", appname, ver, buildinfo_text);
    sfree(buildinfo_text);
    exit(0);
}

const bool buildinfo_gtk_relevant = false;

static bool listening = false, listen_once = false;
static bool finished = false;
void server_instance_terminated(LogPolicy *lp)
{
    struct server_instance *inst = container_of(
        lp, struct server_instance, logpolicy);

    if (listening && !listen_once) {
        log_to_stderr(inst->id, "connection terminated");
    } else {
        finished = true;
    }

    sfree(inst);
}

bool psusan_continue(void *ctx, bool fd, bool cb)
{
    return !finished;
}

static bool longoptarg(const char *arg, const char *expected,
                       const char **val, int *argcp, char ***argvp)
{
    int len = strlen(expected);
    if (memcmp(arg, expected, len))
        return false;
    if (arg[len] == '=') {
        *val = arg + len + 1;
        return true;
    } else if (arg[len] == '\0') {
        if (--*argcp > 0) {
            *val = *++*argvp;
            return true;
        } else {
            fprintf(stderr, "%s: option %s expects an argument\n",
                    appname, expected);
            exit(1);
        }
    }
    return false;
}

static bool longoptnoarg(const char *arg, const char *expected)
{
    int len = strlen(expected);
    if (memcmp(arg, expected, len))
        return false;
    if (arg[len] == '=') {
        fprintf(stderr, "%s: option %s expects no argument\n",
                appname, expected);
        exit(1);
    } else if (arg[len] == '\0') {
        return true;
    }
    return false;
}

struct server_config {
    Conf *conf;
    const SshServerConfig *ssc;

    unsigned next_id;

    Socket *listening_socket;
    Plug listening_plug;
};

static Plug *server_conn_plug(
    struct server_config *cfg, struct server_instance **inst_out)
{
    struct server_instance *inst = snew(struct server_instance);

    memset(inst, 0, sizeof(*inst));

    inst->id = cfg->next_id++;
    inst->logpolicy.vt = &server_logpolicy_vt;

    if (inst_out)
        *inst_out = inst;

    return ssh_server_plug(
        cfg->conf, cfg->ssc, NULL, 0, NULL, NULL,
        &inst->logpolicy, &unix_live_sftpserver_vt);
}

static void server_log(Plug *plug, Socket *s, PlugLogType type, SockAddr *addr,
                       int port, const char *error_msg, int error_code)
{
    log_to_stderr(-1, error_msg);
}

static void server_closing(Plug *plug, PlugCloseType type,
                           const char *error_msg)
{
    if (type != PLUGCLOSE_NORMAL)
        log_to_stderr(-1, error_msg);
}

static int server_accepting(Plug *p, accept_fn_t constructor, accept_ctx_t ctx)
{
    struct server_config *cfg = container_of(
        p, struct server_config, listening_plug);
    Socket *s;
    const char *err;

    struct server_instance *inst;

    if (listen_once) {
        if (!cfg->listening_socket) /* in case of rapid double-accept */
            return 1;
        sk_close(cfg->listening_socket);
        cfg->listening_socket = NULL;
    }

    Plug *plug = server_conn_plug(cfg, &inst);
    s = constructor(ctx, plug);
    if ((err = sk_socket_error(s)) != NULL)
        return 1;

    SocketEndpointInfo *pi = sk_peer_info(s);

    char *msg = dupprintf("new connection from %s", pi->log_text);
    log_to_stderr(inst->id, msg);
    sfree(msg);
    sk_free_endpoint_info(pi);

    sk_set_frozen(s, false);
    ssh_server_start(plug, s);
    return 0;
}

static const PlugVtable server_plugvt = {
    .log = server_log,
    .closing = server_closing,
    .accepting = server_accepting,
};

unsigned auth_methods(AuthPolicy *ap)
{ return 0; }
bool auth_none(AuthPolicy *ap, ptrlen username)
{ return false; }
int auth_password(AuthPolicy *ap, ptrlen username, ptrlen password,
                  ptrlen *new_password_opt)
{ return 0; }
bool auth_publickey(AuthPolicy *ap, ptrlen username, ptrlen public_blob)
{ return false; }
RSAKey *auth_publickey_ssh1(
    AuthPolicy *ap, ptrlen username, mp_int *rsa_modulus)
{ return NULL; }
AuthKbdInt *auth_kbdint_prompts(AuthPolicy *ap, ptrlen username)
{ return NULL; }
int auth_kbdint_responses(AuthPolicy *ap, const ptrlen *responses)
{ return -1; }
char *auth_ssh1int_challenge(AuthPolicy *ap, unsigned method, ptrlen username)
{ return NULL; }
bool auth_ssh1int_response(AuthPolicy *ap, ptrlen response)
{ return false; }
bool auth_successful(AuthPolicy *ap, ptrlen username, unsigned method)
{ return false; }

int main(int argc, char **argv)
{
    const char *listen_socket = NULL;

    SshServerConfig ssc;

    Conf *conf = make_ssh_server_conf();

    memset(&ssc, 0, sizeof(ssc));

    ssc.application_name = "PSUSAN";
    ssc.session_starting_dir = getenv("HOME");
    ssc.bare_connection = true;

    while (--argc > 0) {
        const char *arg = *++argv;
        const char *val;

        if (longoptnoarg(arg, "--help")) {
            show_help(stdout);
            exit(0);
        } else if (longoptnoarg(arg, "--version")) {
            show_version_and_exit();
        } else if (longoptnoarg(arg, "--verbose") || !strcmp(arg, "-v")) {
            verbose = true;
        } else if (longoptarg(arg, "--sessiondir", &val, &argc, &argv)) {
            ssc.session_starting_dir = val;
        } else if (longoptarg(arg, "--sshlog", &val, &argc, &argv) ||
                   longoptarg(arg, "-sshlog", &val, &argc, &argv)) {
            Filename *logfile = filename_from_str(val);
            conf_set_filename(conf, CONF_logfilename, logfile);
            filename_free(logfile);
            conf_set_int(conf, CONF_logtype, LGTYP_PACKETS);
            conf_set_int(conf, CONF_logxfovr, LGXF_OVR);
        } else if (longoptarg(arg, "--sshrawlog", &val, &argc, &argv) ||
                   longoptarg(arg, "-sshrawlog", &val, &argc, &argv)) {
            Filename *logfile = filename_from_str(val);
            conf_set_filename(conf, CONF_logfilename, logfile);
            filename_free(logfile);
            conf_set_int(conf, CONF_logtype, LGTYP_SSHRAW);
            conf_set_int(conf, CONF_logxfovr, LGXF_OVR);
        } else if (longoptarg(arg, "--listen", &val, &argc, &argv)) {
            listen_socket = val;
        } else if (!strcmp(arg, "--listen-once")) {
            listen_once = true;
        } else {
            fprintf(stderr, "%s: unrecognised option '%s'\n", appname, arg);
            exit(1);
        }
    }

    sk_init();
    uxsel_init();

    struct server_config scfg;
    scfg.conf = conf;
    scfg.ssc = &ssc;
    scfg.next_id = 0;

    if (listen_socket) {
        listening = true;
        scfg.listening_plug.vt = &server_plugvt;
        SockAddr *addr = unix_sock_addr(listen_socket);
        scfg.listening_socket = new_unix_listener(addr, &scfg.listening_plug);
        char *msg = dupprintf("listening on Unix socket %s", listen_socket);
        log_to_stderr(-1, msg);
        sfree(msg);
    } else {
        struct server_instance *inst;
        Plug *plug = server_conn_plug(&scfg, &inst);
        ssh_server_start(plug, make_fd_socket(0, 1, -1, NULL, 0, plug));
        log_to_stderr(inst->id, "running directly on stdio");
    }

    cli_main_loop(cliloop_no_pw_setup, cliloop_no_pw_check,
                  psusan_continue, NULL);

    return 0;
}
