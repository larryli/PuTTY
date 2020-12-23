/*
 * SSH server for Unix: main program.
 *
 * ======================================================================
 *
 * This server is NOT SECURE!
 *
 * DO NOT DEPLOY IT IN A HOSTILE-FACING ENVIRONMENT!
 *
 * Its purpose is to speak the server end of everything PuTTY speaks
 * on the client side, so that I can test that I haven't broken PuTTY
 * when I reorganise its code, even things like RSA key exchange or
 * chained auth methods which it's hard to find a server that speaks
 * at all.
 *
 * It has no interaction with the OS's authentication system: the
 * authentications it will accept are configurable by command-line
 * option, and once you authenticate, it will run the connection
 * protocol - including all subprocesses and shells - under the same
 * Unix user id you started it under.
 *
 * It really is only suitable for testing the actual SSH protocol.
 * Don't use it for anything more serious!
 *
 * ======================================================================
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
#include "sshserver.h"

const char *const appname = "uppity";

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
    return fontspec_new("");
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

void make_unix_sftp_filehandle_key(void *data, size_t size)
{
    random_read(data, size);
}

static bool verbose;

struct AuthPolicyShared {
    struct AuthPolicy_ssh1_pubkey *ssh1keys;
    struct AuthPolicy_ssh2_pubkey *ssh2keys;
};

struct AuthPolicy {
    struct AuthPolicyShared *shared;
    int kbdint_state;
};

struct server_instance {
    unsigned id;
    AuthPolicy ap;
    LogPolicy logpolicy;
};

static void log_to_stderr(unsigned id, const char *msg)
{
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

struct AuthPolicy_ssh1_pubkey {
    RSAKey key;
    struct AuthPolicy_ssh1_pubkey *next;
};
struct AuthPolicy_ssh2_pubkey {
    ptrlen public_blob;
    struct AuthPolicy_ssh2_pubkey *next;
};

unsigned auth_methods(AuthPolicy *ap)
{
    return (AUTHMETHOD_PUBLICKEY | AUTHMETHOD_PASSWORD | AUTHMETHOD_KBDINT |
            AUTHMETHOD_TIS | AUTHMETHOD_CRYPTOCARD);
}
bool auth_none(AuthPolicy *ap, ptrlen username)
{
    return false;
}
int auth_password(AuthPolicy *ap, ptrlen username, ptrlen password,
                  ptrlen *new_password_opt)
{
    const char *PHONY_GOOD_PASSWORD = "weasel";
    const char *PHONY_BAD_PASSWORD = "ferret";

    if (!new_password_opt) {
        /* Accept login with our preconfigured good password */
        if (ptrlen_eq_string(password, PHONY_GOOD_PASSWORD))
            return 1;
        /* Don't outright reject the bad password, but insist on a change */
        if (ptrlen_eq_string(password, PHONY_BAD_PASSWORD))
            return 2;
        /* Reject anything else */
        return 0;
    } else {
        /* In a password-change request, expect the bad password as input */
        if (!ptrlen_eq_string(password, PHONY_BAD_PASSWORD))
            return 0;
        /* Accept a request to change it to the good password */
        if (ptrlen_eq_string(*new_password_opt, PHONY_GOOD_PASSWORD))
            return 1;
        /* Outright reject a request to change it to the same password
         * as it already 'was' */
        if (ptrlen_eq_string(*new_password_opt, PHONY_BAD_PASSWORD))
            return 0;
        /* Anything else, pretend the new pw wasn't good enough, and
         * re-request a change */
        return 2;
    }
}
bool auth_publickey(AuthPolicy *ap, ptrlen username, ptrlen public_blob)
{
    struct AuthPolicy_ssh2_pubkey *iter;
    for (iter = ap->shared->ssh2keys; iter; iter = iter->next) {
        if (ptrlen_eq_ptrlen(public_blob, iter->public_blob))
            return true;
    }
    return false;
}
RSAKey *auth_publickey_ssh1(
    AuthPolicy *ap, ptrlen username, mp_int *rsa_modulus)
{
    struct AuthPolicy_ssh1_pubkey *iter;
    for (iter = ap->shared->ssh1keys; iter; iter = iter->next) {
        if (mp_cmp_eq(rsa_modulus, iter->key.modulus))
            return &iter->key;
    }
    return NULL;
}
AuthKbdInt *auth_kbdint_prompts(AuthPolicy *ap, ptrlen username)
{
    AuthKbdInt *aki;

    switch (ap->kbdint_state) {
      case 0:
        aki = snew(AuthKbdInt);
        aki->title = dupstr("Initial double prompt");
        aki->instruction =
            dupstr("First prompt should echo, second should not");
        aki->nprompts = 2;
        aki->prompts = snewn(aki->nprompts, AuthKbdIntPrompt);
        aki->prompts[0].prompt = dupstr("Echoey prompt: ");
        aki->prompts[0].echo = true;
        aki->prompts[1].prompt = dupstr("Silent prompt: ");
        aki->prompts[1].echo = false;
        return aki;
      case 1:
        aki = snew(AuthKbdInt);
        aki->title = dupstr("Zero-prompt step");
        aki->instruction = dupstr("Shouldn't see any prompts this time");
        aki->nprompts = 0;
        aki->prompts = NULL;
        return aki;
      default:
        ap->kbdint_state = 0;
        return NULL;
    }
}
int auth_kbdint_responses(AuthPolicy *ap, const ptrlen *responses)
{
    switch (ap->kbdint_state) {
      case 0:
        if (ptrlen_eq_string(responses[0], "stoat") &&
            ptrlen_eq_string(responses[1], "weasel")) {
            ap->kbdint_state++;
            return 0;                  /* those are the expected responses */
        } else {
            ap->kbdint_state = 0;
            return -1;
        }
        break;
      case 1:
        return +1;                     /* succeed after the zero-prompt step */
      default:
        ap->kbdint_state = 0;
        return -1;
    }
}
char *auth_ssh1int_challenge(AuthPolicy *ap, unsigned method, ptrlen username)
{
    /* FIXME: test returning a challenge string without \n, and ensure
     * it gets printed as a prompt in its own right, without PuTTY
     * making up a "Response: " prompt to follow it */
    return dupprintf("This is a dummy %s challenge!\n",
                     (method == AUTHMETHOD_TIS ? "TIS" : "CryptoCard"));
}
bool auth_ssh1int_response(AuthPolicy *ap, ptrlen response)
{
    return ptrlen_eq_string(response, "otter");
}
bool auth_successful(AuthPolicy *ap, ptrlen username, unsigned method)
{
    return true;
}

static void safety_warning(FILE *fp)
{
    fputs("  =================================================\n"
          "     THIS SSH SERVER IS NOT WRITTEN TO BE SECURE!\n"
          "  DO NOT DEPLOY IT IN A HOSTILE-FACING ENVIRONMENT!\n"
          "  =================================================\n", fp);
}

static void show_help(FILE *fp)
{
    safety_warning(fp);
    fputs("\n"
          "usage:   uppity [options]\n"
          "options: --listen [PORT|PATH] listen to a port on localhost, or Unix socket\n"
          "         --listen-once        (with --listen) stop after one "
          "connection\n"
          "         --hostkey KEY        SSH host key (need at least one)\n"
          "         --rsakexkey KEY      key for SSH-2 RSA key exchange "
          "(in SSH-1 format)\n"
          "         --userkey KEY        public key"
           " acceptable for user authentication\n"
          "         --sessiondir DIR     cwd for session subprocess (default $HOME)\n"
          "         --bannertext TEXT    send TEXT as SSH-2 auth banner\n"
          "         --bannerfile FILE    send contents of FILE as SSH-2 auth "
          "banner\n"
          "         --kexinit-kex STR    override list of SSH-2 KEX methods\n"
          "         --kexinit-hostkey STR  override list of SSH-2 host key "
          "types\n"
          "         --kexinit-cscipher STR override list of SSH-2 "
          "client->server ciphers\n"
          "         --kexinit-sccipher STR override list of SSH-2 "
          "server->client ciphers\n"
          "         --kexinit-csmac STR    override list of SSH-2 "
          "client->server MACs\n"
          "         --kexinit-scmac STR    override list of SSH-2 "
          "server->client MACs\n"
          "         --kexinit-cscomp STR   override list of SSH-2 "
          "c->s compression types\n"
          "         --kexinit-sccomp STR   override list of SSH-2 "
          "s->c compression types\n"
          "         --ssh1-ciphers STR     override list of SSH-1 ciphers\n"
          "         --ssh1-no-compression  forbid compression in SSH-1\n"
          "         --exitsignum         send buggy numeric \"exit-signal\" "
          "message\n"
          "         --verbose            print event log messages to standard "
          "error\n"
          "         --sshlog FILE        write SSH packet log to FILE\n"
          "         --sshrawlog FILE     write SSH packets + raw data log"
          " to FILE\n"
          "also:    uppity --help        show this text\n"
          "         uppity --version     show version information\n"
          "\n", fp);
    safety_warning(fp);
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

    ssh_key **hostkeys;
    int nhostkeys;

    RSAKey *hostkey1;

    struct AuthPolicyShared *ap_shared;

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
    inst->ap.shared = cfg->ap_shared;
    inst->logpolicy.vt = &server_logpolicy_vt;

    if (inst_out)
        *inst_out = inst;

    return ssh_server_plug(
        cfg->conf, cfg->ssc, cfg->hostkeys, cfg->nhostkeys, cfg->hostkey1,
        &inst->ap, &inst->logpolicy, &unix_live_sftpserver_vt);
}

static void server_log(Plug *plug, PlugLogType type, SockAddr *addr, int port,
                       const char *error_msg, int error_code)
{
    log_to_stderr((unsigned)-1, error_msg);
}

static void server_closing(Plug *plug, const char *error_msg, int error_code,
                           bool calling_back)
{
    log_to_stderr((unsigned)-1, error_msg);
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

    unsigned old_next_id = cfg->next_id;

    Plug *plug = server_conn_plug(cfg, &inst);
    s = constructor(ctx, plug);
    if ((err = sk_socket_error(s)) != NULL)
        return 1;

    SocketPeerInfo *pi = sk_peer_info(s);

    if (pi->addressfamily != ADDRTYPE_LOCAL && !sk_peer_trusted(s)) {
        fprintf(stderr, "rejected connection from %s (untrustworthy peer)\n",
                pi->log_text);
        sk_free_peer_info(pi);
        sk_close(s);
        cfg->next_id = old_next_id;
        return 1;
    }

    char *msg = dupprintf("new connection from %s", pi->log_text);
    log_to_stderr(inst->id, msg);
    sfree(msg);
    sk_free_peer_info(pi);

    sk_set_frozen(s, false);
    ssh_server_start(plug, s);
    return 0;
}

static const PlugVtable server_plugvt = {
    .log = server_log,
    .closing = server_closing,
    .accepting = server_accepting,
};

int main(int argc, char **argv)
{
    int listen_port = -1;
    const char *listen_socket = NULL;

    ssh_key **hostkeys = NULL;
    size_t nhostkeys = 0, hostkeysize = 0;
    RSAKey *hostkey1 = NULL;

    struct AuthPolicyShared aps;
    SshServerConfig ssc;

    Conf *conf = make_ssh_server_conf();

    aps.ssh1keys = NULL;
    aps.ssh2keys = NULL;

    memset(&ssc, 0, sizeof(ssc));

    ssc.application_name = "Uppity";
    ssc.session_starting_dir = getenv("HOME");
    ssc.ssh1_cipher_mask = SSH1_SUPPORTED_CIPHER_MASK;
    ssc.ssh1_allow_compression = true;

    if (argc <= 1) {
        /*
         * We're going to terminate with an error message below,
         * because there are no host keys. But we'll display the help
         * as additional standard-error output, if nothing else so
         * that people see the giant safety warning.
         */
        show_help(stderr);
        fputc('\n', stderr);
    }

    while (--argc > 0) {
        const char *arg = *++argv;
        const char *val;

        if (!strcmp(arg, "--help")) {
            show_help(stdout);
            exit(0);
        } else if (longoptnoarg(arg, "--version")) {
            show_version_and_exit();
        } else if (longoptnoarg(arg, "--verbose") || !strcmp(arg, "-v")) {
            verbose = true;
        } else if (longoptarg(arg, "--listen", &val, &argc, &argv)) {
            if (val[0] == '/') {
                listen_port = -1;
                listen_socket = val;
            } else {
                listen_port = atoi(val);
                listen_socket = NULL;
            }
        } else if (!strcmp(arg, "--listen-once")) {
            listen_once = true;
        } else if (longoptarg(arg, "--hostkey", &val, &argc, &argv)) {
            Filename *keyfile;
            int keytype;
            const char *error;

            keyfile = filename_from_str(val);
            keytype = key_type(keyfile);

            if (keytype == SSH_KEYTYPE_SSH2) {
                ssh2_userkey *uk;
                ssh_key *key;
                uk = ppk_load_f(keyfile, NULL, &error);
                filename_free(keyfile);
                if (!uk || !uk->key) {
                    fprintf(stderr, "%s: unable to load host key '%s': "
                            "%s\n", appname, val, error);
                    exit(1);
                }
                char *invalid = ssh_key_invalid(uk->key, 0);
                if (invalid) {
                    fprintf(stderr, "%s: host key '%s' is unusable: "
                            "%s\n", appname, val, invalid);
                    exit(1);
                }
                key = uk->key;
                sfree(uk->comment);
                sfree(uk);

                for (int i = 0; i < nhostkeys; i++)
                    if (ssh_key_alg(hostkeys[i]) == ssh_key_alg(key)) {
                        fprintf(stderr, "%s: host key '%s' duplicates key "
                                "type %s\n", appname, val,
                                ssh_key_alg(key)->ssh_id);
                        exit(1);
                    }

                sgrowarray(hostkeys, hostkeysize, nhostkeys);
                hostkeys[nhostkeys++] = key;
            } else if (keytype == SSH_KEYTYPE_SSH1) {
                if (hostkey1) {
                    fprintf(stderr, "%s: host key '%s' is a redundant "
                            "SSH-1 host key\n", appname, val);
                    exit(1);
                }
                hostkey1 = snew(RSAKey);
                if (!rsa1_load_f(keyfile, hostkey1, NULL, &error)) {
                    fprintf(stderr, "%s: unable to load host key '%s': "
                            "%s\n", appname, val, error);
                    exit(1);
                }
            } else {
                fprintf(stderr, "%s: '%s' is not loadable as a "
                        "private key (%s)", appname, val,
                        key_type_to_str(keytype));
                exit(1);
            }
        } else if (longoptarg(arg, "--rsakexkey", &val, &argc, &argv)) {
            Filename *keyfile;
            int keytype;
            const char *error;

            keyfile = filename_from_str(val);
            keytype = key_type(keyfile);

            if (keytype != SSH_KEYTYPE_SSH1) {
                fprintf(stderr, "%s: '%s' is not loadable as an SSH-1 format "
                        "private key (%s)", appname, val,
                        key_type_to_str(keytype));
                exit(1);
            }

            if (ssc.rsa_kex_key) {
                freersakey(ssc.rsa_kex_key);
            } else {
                ssc.rsa_kex_key = snew(RSAKey);
            }

            if (!rsa1_load_f(keyfile, ssc.rsa_kex_key, NULL, &error)) {
                fprintf(stderr, "%s: unable to load RSA kex key '%s': "
                        "%s\n", appname, val, error);
                exit(1);
            }

            ssc.rsa_kex_key->sshk.vt = &ssh_rsa;
        } else if (longoptarg(arg, "--userkey", &val, &argc, &argv)) {
            Filename *keyfile;
            int keytype;
            const char *error;

            keyfile = filename_from_str(val);
            keytype = key_type(keyfile);

            if (keytype == SSH_KEYTYPE_SSH2_PUBLIC_RFC4716 ||
                keytype == SSH_KEYTYPE_SSH2_PUBLIC_OPENSSH) {
                strbuf *sb = strbuf_new();
                struct AuthPolicy_ssh2_pubkey *node;
                void *blob;

                if (!ppk_loadpub_f(keyfile, NULL, BinarySink_UPCAST(sb),
                                   NULL, &error)) {
                    fprintf(stderr, "%s: unable to load user key '%s': "
                            "%s\n", appname, val, error);
                    exit(1);
                }

                node = snew_plus(struct AuthPolicy_ssh2_pubkey, sb->len);
                blob = snew_plus_get_aux(node);
                memcpy(blob, sb->u, sb->len);
                node->public_blob = make_ptrlen(blob, sb->len);

                node->next = aps.ssh2keys;
                aps.ssh2keys = node;

                strbuf_free(sb);
            } else if (keytype == SSH_KEYTYPE_SSH1_PUBLIC) {
                strbuf *sb = strbuf_new();
                BinarySource src[1];
                struct AuthPolicy_ssh1_pubkey *node;

                if (!rsa1_loadpub_f(keyfile, BinarySink_UPCAST(sb),
                                    NULL, &error)) {
                    fprintf(stderr, "%s: unable to load user key '%s': "
                            "%s\n", appname, val, error);
                    exit(1);
                }

                node = snew(struct AuthPolicy_ssh1_pubkey);
                BinarySource_BARE_INIT(src, sb->u, sb->len);
                get_rsa_ssh1_pub(src, &node->key, RSA_SSH1_EXPONENT_FIRST);

                node->next = aps.ssh1keys;
                aps.ssh1keys = node;

                strbuf_free(sb);
            } else {
                fprintf(stderr, "%s: '%s' is not loadable as a public key "
                        "(%s)\n", appname, val, key_type_to_str(keytype));
                exit(1);
            }
        } else if (longoptarg(arg, "--bannerfile", &val, &argc, &argv)) {
            FILE *fp = fopen(val, "r");
            if (!fp) {
                fprintf(stderr, "%s: %s: open: %s\n", appname,
                        val, strerror(errno));
                exit(1);
            }
            strbuf *sb = strbuf_new();
            if (!read_file_into(BinarySink_UPCAST(sb), fp)) {
                fprintf(stderr, "%s: %s: read: %s\n", appname,
                        val, strerror(errno));
                exit(1);
            }
            fclose(fp);
            ssc.banner = ptrlen_from_strbuf(sb);
        } else if (longoptarg(arg, "--bannertext", &val, &argc, &argv)) {
            ssc.banner = ptrlen_from_asciz(val);
        } else if (longoptarg(arg, "--sessiondir", &val, &argc, &argv)) {
            ssc.session_starting_dir = val;
        } else if (longoptarg(arg, "--kexinit-kex", &val, &argc, &argv)) {
            ssc.kex_override[KEXLIST_KEX] = ptrlen_from_asciz(val);
        } else if (longoptarg(arg, "--kexinit-hostkey", &val, &argc, &argv)) {
            ssc.kex_override[KEXLIST_HOSTKEY] = ptrlen_from_asciz(val);
        } else if (longoptarg(arg, "--kexinit-cscipher", &val, &argc, &argv)) {
            ssc.kex_override[KEXLIST_CSCIPHER] = ptrlen_from_asciz(val);
        } else if (longoptarg(arg, "--kexinit-csmac", &val, &argc, &argv)) {
            ssc.kex_override[KEXLIST_CSMAC] = ptrlen_from_asciz(val);
        } else if (longoptarg(arg, "--kexinit-cscomp", &val, &argc, &argv)) {
            ssc.kex_override[KEXLIST_CSCOMP] = ptrlen_from_asciz(val);
        } else if (longoptarg(arg, "--kexinit-sccipher", &val, &argc, &argv)) {
            ssc.kex_override[KEXLIST_SCCIPHER] = ptrlen_from_asciz(val);
        } else if (longoptarg(arg, "--kexinit-scmac", &val, &argc, &argv)) {
            ssc.kex_override[KEXLIST_SCMAC] = ptrlen_from_asciz(val);
        } else if (longoptarg(arg, "--kexinit-sccomp", &val, &argc, &argv)) {
            ssc.kex_override[KEXLIST_SCCOMP] = ptrlen_from_asciz(val);
        } else if (longoptarg(arg, "--ssh1-ciphers", &val, &argc, &argv)) {
            ptrlen list = ptrlen_from_asciz(val);
            ptrlen word;
            unsigned long mask = 0;
            while (word = ptrlen_get_word(&list, ","), word.len != 0) {

#define SSH1_CIPHER_CASE(bitpos, name)                  \
                if (ptrlen_eq_string(word, name)) {     \
                    mask |= 1U << bitpos;               \
                    continue;                           \
                }
                SSH1_SUPPORTED_CIPHER_LIST(SSH1_CIPHER_CASE);
#undef SSH1_CIPHER_CASE

                fprintf(stderr, "%s: unrecognised SSH-1 cipher '%.*s'\n",
                        appname, PTRLEN_PRINTF(word));
                exit(1);
            }
            ssc.ssh1_cipher_mask = mask;
        } else if (longoptnoarg(arg, "--ssh1-no-compression")) {
            ssc.ssh1_allow_compression = false;
        } else if (longoptnoarg(arg, "--exitsignum")) {
            ssc.exit_signal_numeric = true;
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
        } else if (!strcmp(arg, "--pretend-to-accept-any-pubkey")) {
            ssc.stunt_pretend_to_accept_any_pubkey = true;
        } else if (!strcmp(arg, "--open-unconditional-agent-socket")) {
            ssc.stunt_open_unconditional_agent_socket = true;
        } else {
            fprintf(stderr, "%s: unrecognised option '%s'\n", appname, arg);
            exit(1);
        }
    }

    if (nhostkeys == 0 && !hostkey1) {
        fprintf(stderr, "%s: specify at least one host key\n", appname);
        exit(1);
    }

    random_ref();

    /*
     * Block SIGPIPE, so that we'll get EPIPE individually on
     * particular network connections that go wrong.
     */
    putty_signal(SIGPIPE, SIG_IGN);

    sk_init();
    uxsel_init();

    struct server_config scfg;
    scfg.conf = conf;
    scfg.ssc = &ssc;
    scfg.hostkeys = hostkeys;
    scfg.nhostkeys = nhostkeys;
    scfg.hostkey1 = hostkey1;
    scfg.ap_shared = &aps;
    scfg.next_id = 0;

    if (listen_port >= 0 || listen_socket) {
        listening = true;
        scfg.listening_plug.vt = &server_plugvt;
        char *msg;
        if (listen_port >= 0) {
            scfg.listening_socket = sk_newlistener(
                NULL, listen_port, &scfg.listening_plug, true,
                ADDRTYPE_UNSPEC);
            msg = dupprintf("%s: listening on port %d",
                            appname, listen_port);
        } else {
            SockAddr *addr = unix_sock_addr(listen_socket);
            scfg.listening_socket = new_unix_listener(
                addr, &scfg.listening_plug);
            msg = dupprintf("%s: listening on Unix socket %s",
                            appname, listen_socket);
        }

        log_to_stderr(-1, msg);
        sfree(msg);
    } else {
        struct server_instance *inst;
        Plug *plug = server_conn_plug(&scfg, &inst);
        ssh_server_start(plug, make_fd_socket(0, 1, -1, plug));
        log_to_stderr(inst->id, "speaking SSH on stdio");
    }

    cli_main_loop(cliloop_no_pw_setup, cliloop_no_pw_check,
                  cliloop_always_continue, NULL);

    return 0;
}
