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

#define PUTTY_DO_GLOBALS	       /* actually _define_ globals */
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
    return NULL;		       /* this is a stub */
}

/*
 * Our selects are synchronous, so these functions are empty stubs.
 */
uxsel_id *uxsel_input_add(int fd, int rwx) { return NULL; }
void uxsel_input_remove(uxsel_id *id) { }

void old_keyfile_warning(void) { }

void timer_change_notify(unsigned long next)
{
}

char *platform_get_x_display(void) { return NULL; }

static bool verbose;

static void log_to_stderr(const char *msg)
{
    /*
     * FIXME: in multi-connection proper-socket mode, prefix this with
     * a connection id of some kind. We'll surely pass this in to
     * sshserver.c by way of constructing a distinct LogPolicy per
     * instance and making its containing structure contain the id -
     * but we'll also have to arrange for those LogPolicy structs to
     * be freed when the server instance terminates.
     *
     * For now, though, we only have one server instance, so no need.
     */

    fputs(msg, stderr);
    fputc('\n', stderr);
    fflush(stderr);
}

static void server_eventlog(LogPolicy *lp, const char *event)
{
    if (verbose)
        log_to_stderr(event);
}

static void server_logging_error(LogPolicy *lp, const char *event)
{
    log_to_stderr(event);              /* unconditional */
}

static int server_askappend(
    LogPolicy *lp, Filename *filename,
    void (*callback)(void *ctx, int result), void *ctx)
{
    return 2; /* always overwrite (FIXME: could make this a cmdline option) */
}

static const LogPolicyVtable server_logpolicy_vt = {
    server_eventlog,
    server_askappend,
    server_logging_error,
};
LogPolicy server_logpolicy[1] = {{ &server_logpolicy_vt }};

struct AuthPolicy_ssh1_pubkey {
    RSAKey key;
    struct AuthPolicy_ssh1_pubkey *next;
};
struct AuthPolicy_ssh2_pubkey {
    ptrlen public_blob;
    struct AuthPolicy_ssh2_pubkey *next;
};

struct AuthPolicy {
    struct AuthPolicy_ssh1_pubkey *ssh1keys;
    struct AuthPolicy_ssh2_pubkey *ssh2keys;
    int kbdint_state;
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
    for (iter = ap->ssh2keys; iter; iter = iter->next) {
        if (ptrlen_eq_ptrlen(public_blob, iter->public_blob))
            return true;
    }
    return false;
}
RSAKey *auth_publickey_ssh1(
    AuthPolicy *ap, ptrlen username, mp_int *rsa_modulus)
{
    struct AuthPolicy_ssh1_pubkey *iter;
    for (iter = ap->ssh1keys; iter; iter = iter->next) {
        if (mp_cmp_eq(rsa_modulus, iter->key.modulus))
            return &iter->key;
    }
    return NULL;
}
AuthKbdInt *auth_kbdint_prompts(AuthPolicy *ap, ptrlen username)
{
    AuthKbdInt *aki = snew(AuthKbdInt);

    switch (ap->kbdint_state) {
      case 0:
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
          "options: --hostkey KEY        SSH host key (need at least one)\n"
          "         --userkey KEY        public key"
           " acceptable for user authentication\n"
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

static bool finished = false;
void server_instance_terminated(void)
{
    /* FIXME: change policy here if we're running in a listening loop */
    finished = true;
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

int main(int argc, char **argv)
{
    int *fdlist;
    int fd;
    int i, fdstate;
    size_t fdsize;
    unsigned long now;

    ssh_key **hostkeys = NULL;
    size_t nhostkeys = 0, hostkeysize = 0;
    RSAKey *hostkey1 = NULL;

    AuthPolicy ap;

    Conf *conf = conf_new();
    load_open_settings(NULL, conf);

    ap.kbdint_state = 0;
    ap.ssh1keys = NULL;
    ap.ssh2keys = NULL;

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
        } else if (!strcmp(arg, "--version")) {
            show_version_and_exit();
        } else if (!strcmp(arg, "--verbose") || !strcmp(arg, "-v")) {
            verbose = true;
        } else if (longoptarg(arg, "--hostkey", &val, &argc, &argv)) {
            Filename *keyfile;
            int keytype;
            const char *error;

            keyfile = filename_from_str(val);
            keytype = key_type(keyfile);

            if (keytype == SSH_KEYTYPE_SSH2) {
                ssh2_userkey *uk;
                ssh_key *key;
                uk = ssh2_load_userkey(keyfile, NULL, &error);
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

                for (i = 0; i < nhostkeys; i++)
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
                if (!rsa_ssh1_loadkey(keyfile, hostkey1, NULL, &error)) {
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

                if (!ssh2_userkey_loadpub(keyfile, NULL, BinarySink_UPCAST(sb),
                                          NULL, &error)) {
                    fprintf(stderr, "%s: unable to load user key '%s': "
                            "%s\n", appname, val, error);
                    exit(1);
                }

                node = snew_plus(struct AuthPolicy_ssh2_pubkey, sb->len);
                blob = snew_plus_get_aux(node);
                memcpy(blob, sb->u, sb->len);
                node->public_blob = make_ptrlen(blob, sb->len);

                node->next = ap.ssh2keys;
                ap.ssh2keys = node;

                strbuf_free(sb);
            } else if (keytype == SSH_KEYTYPE_SSH1_PUBLIC) {
                strbuf *sb = strbuf_new();
                BinarySource src[1];
                struct AuthPolicy_ssh1_pubkey *node;

                if (!rsa_ssh1_loadpub(keyfile, BinarySink_UPCAST(sb),
                                      NULL, &error)) {
                    fprintf(stderr, "%s: unable to load user key '%s': "
                            "%s\n", appname, val, error);
                    exit(1);
                }

                node = snew(struct AuthPolicy_ssh1_pubkey);
                BinarySource_BARE_INIT(src, sb->u, sb->len);
                get_rsa_ssh1_pub(src, &node->key, RSA_SSH1_EXPONENT_FIRST);

                node->next = ap.ssh1keys;
                ap.ssh1keys = node;

                strbuf_free(sb);
            } else {
                fprintf(stderr, "%s: '%s' is not loadable as a public key "
                        "(%s)\n", appname, val, key_type_to_str(keytype));
                exit(1);
            }
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
        } else {
            fprintf(stderr, "%s: unrecognised option '%s'\n", appname, arg);
            exit(1);
        }
    }

    if (nhostkeys == 0 && !hostkey1) {
        fprintf(stderr, "%s: specify at least one host key\n", appname);
        exit(1);
    }

    fdlist = NULL;
    fdsize = 0;

    random_ref();

    /*
     * Block SIGPIPE, so that we'll get EPIPE individually on
     * particular network connections that go wrong.
     */
    putty_signal(SIGPIPE, SIG_IGN);

    sk_init();
    uxsel_init();

    {
        Plug *plug = ssh_server_plug(
            conf, hostkeys, nhostkeys, hostkey1, &ap, server_logpolicy,
            &unix_live_sftpserver_vt);
        ssh_server_start(plug, make_fd_socket(0, 1, -1, plug));
    }

    now = GETTICKCOUNT();

    pollwrapper *pw = pollwrap_new();
    while (!finished) {
	int rwx;
	int ret;
        unsigned long next;

        pollwrap_clear(pw);

	/* Count the currently active fds. */
	i = 0;
	for (fd = first_fd(&fdstate, &rwx); fd >= 0;
	     fd = next_fd(&fdstate, &rwx)) i++;

	/* Expand the fdlist buffer if necessary. */
        sgrowarray(fdlist, fdsize, i);

	/*
	 * Add all currently open fds to the select sets, and store
	 * them in fdlist as well.
	 */
	int fdcount = 0;
	for (fd = first_fd(&fdstate, &rwx); fd >= 0;
	     fd = next_fd(&fdstate, &rwx)) {
	    fdlist[fdcount++] = fd;
            pollwrap_add_fd_rwx(pw, fd, rwx);
	}

        if (toplevel_callback_pending()) {
            ret = pollwrap_poll_instant(pw);
        } else if (run_timers(now, &next)) {
            do {
                unsigned long then;
                long ticks;

		then = now;
		now = GETTICKCOUNT();
		if (now - then > next - then)
		    ticks = 0;
		else
		    ticks = next - now;

                bool overflow = false;
                if (ticks > INT_MAX) {
                    ticks = INT_MAX;
                    overflow = true;
                }

                ret = pollwrap_poll_timeout(pw, ticks);
                if (ret == 0 && !overflow)
                    now = next;
                else
                    now = GETTICKCOUNT();
            } while (ret < 0 && errno == EINTR);
        } else {
            ret = pollwrap_poll_endless(pw);
        }

        if (ret < 0 && errno == EINTR)
            continue;

	if (ret < 0) {
	    perror("poll");
	    exit(1);
	}

	for (i = 0; i < fdcount; i++) {
	    fd = fdlist[i];
            int rwx = pollwrap_get_fd_rwx(pw, fd);
            /*
             * We must process exceptional notifications before
             * ordinary readability ones, or we may go straight
             * past the urgent marker.
             */
	    if (rwx & SELECT_X)
		select_result(fd, SELECT_X);
	    if (rwx & SELECT_R)
		select_result(fd, SELECT_R);
	    if (rwx & SELECT_W)
		select_result(fd, SELECT_W);
	}

        run_toplevel_callbacks();
    }
    exit(0);
}
