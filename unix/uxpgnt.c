/*
 * Unix Pageant, more or less similar to ssh-agent.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#define PUTTY_DO_GLOBALS	       /* actually _define_ globals */
#include "putty.h"
#include "ssh.h"
#include "misc.h"
#include "pageant.h"

SockAddr unix_sock_addr(const char *path);
Socket new_unix_listener(SockAddr listenaddr, Plug plug);

void fatalbox(const char *p, ...)
{
    va_list ap;
    fprintf(stderr, "FATAL ERROR: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}
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
void connection_fatal(void *frontend, const char *p, ...)
{
    va_list ap;
    fprintf(stderr, "FATAL ERROR: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}
void cmdline_error(const char *p, ...)
{
    va_list ap;
    fprintf(stderr, "pageant: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

FILE *pageant_logfp = NULL;
void pageant_log(void *ctx, const char *fmt, va_list ap)
{
    if (!pageant_logfp)
        return;

    fprintf(pageant_logfp, "pageant: ");
    vfprintf(pageant_logfp, fmt, ap);
    fprintf(pageant_logfp, "\n");
}

/*
 * In Pageant our selects are synchronous, so these functions are
 * empty stubs.
 */
uxsel_id *uxsel_input_add(int fd, int rwx) { return NULL; }
void uxsel_input_remove(uxsel_id *id) { }

/*
 * More stubs.
 */
void random_save_seed(void) {}
void random_destroy_seed(void) {}
void noise_ultralight(unsigned long data) {}
char *platform_default_s(const char *name) { return NULL; }
int platform_default_i(const char *name, int def) { return def; }
FontSpec *platform_default_fontspec(const char *name) { return fontspec_new(""); }
Filename *platform_default_filename(const char *name) { return filename_from_str(""); }
char *x_get_default(const char *key) { return NULL; }
void log_eventlog(void *handle, const char *event) {}
int from_backend(void *frontend, int is_stderr, const char *data, int datalen)
{ assert(!"only here to satisfy notional call from backend_socket_log"); }

/*
 * Short description of parameters.
 */
static void usage(void)
{
    printf("Pageant: SSH agent\n");
    printf("%s\n", ver);
    printf("Usage: pageant <lifetime> [key files]\n");
    printf("       pageant [key files] --exec <command> [args]\n");
    printf("       pageant -a [key files]\n");
    printf("       pageant -d [key identifiers]\n");
    printf("       pageant --public [key identifiers]\n");
    printf("       pageant --public-openssh [key identifiers]\n");
    printf("       pageant -l\n");
    printf("       pageant -D\n");
    printf("Lifetime options, for running Pageant as an agent:\n");
    printf("  -X           run with the lifetime of the X server\n");
    printf("  -T           run with the lifetime of the controlling tty\n");
    printf("  --permanent  run permanently\n");
    printf("  --debug      run in debugging mode, without forking\n");
    printf("  --exec <command>   run with the lifetime of that command\n");
    printf("Client options, for talking to an existing agent:\n");
    printf("  -a           add key(s) to the existing agent\n");
    printf("  -l           list currently loaded key fingerprints and comments\n");
    printf("  --public     print public keys in RFC 4716 format\n");
    printf("  --public-openssh   print public keys in OpenSSH format\n");
    printf("  -d           delete key(s) from the agent\n");
    printf("  -D           delete all keys from the agent\n");
    printf("Other options:\n");
    printf("  -v           verbose mode (in agent mode)\n");
    printf("  -s -c        force POSIX or C shell syntax (in agent mode)\n");
    exit(1);
}

static void version(void)
{
    char *buildinfo_text = buildinfo("\n");
    printf("pageant: %s\n%s\n", ver, buildinfo_text);
    sfree(buildinfo_text);
    exit(0);
}

void keylist_update(void)
{
    /* Nothing needs doing in Unix Pageant */
}

#define PAGEANT_DIR_PREFIX "/tmp/pageant"

const char *const appname = "Pageant";

static int time_to_die = FALSE;

/* Stub functions to permit linking against x11fwd.c. These never get
 * used, because in LIFE_X11 mode we connect to the X server using a
 * straightforward Socket and don't try to create an ersatz SSH
 * forwarding too. */
int sshfwd_write(struct ssh_channel *c, char *data, int len) { return 0; }
void sshfwd_write_eof(struct ssh_channel *c) { }
void sshfwd_unclean_close(struct ssh_channel *c, const char *err) { }
void sshfwd_unthrottle(struct ssh_channel *c, int bufsize) {}
Conf *sshfwd_get_conf(struct ssh_channel *c) { return NULL; }
void sshfwd_x11_sharing_handover(struct ssh_channel *c,
                                 void *share_cs, void *share_chan,
                                 const char *peer_addr, int peer_port,
                                 int endian, int protomajor, int protominor,
                                 const void *initial_data, int initial_len) {}
void sshfwd_x11_is_local(struct ssh_channel *c) {}

/*
 * These functions are part of the plug for our connection to the X
 * display, so they do get called. They needn't actually do anything,
 * except that x11_closing has to signal back to the main loop that
 * it's time to terminate.
 */
static void x11_log(Plug p, int type, SockAddr addr, int port,
		    const char *error_msg, int error_code) {}
static void x11_receive(Plug plug, int urgent, char *data, int len) {}
static void x11_sent(Plug plug, int bufsize) {}
static void x11_closing(Plug plug, const char *error_msg, int error_code,
			int calling_back)
{
    time_to_die = TRUE;
}
struct X11Connection {
    const struct plug_function_table *fn;
};

char *socketname;
static enum { SHELL_AUTO, SHELL_SH, SHELL_CSH } shell_type = SHELL_AUTO;
void pageant_print_env(int pid)
{
    if (shell_type == SHELL_AUTO) {
        /* Same policy as OpenSSH: if $SHELL ends in "csh" then assume
         * it's csh-shaped. */
        const char *shell = getenv("SHELL");
        if (shell && strlen(shell) >= 3 &&
            !strcmp(shell + strlen(shell) - 3, "csh"))
            shell_type = SHELL_CSH;
        else
            shell_type = SHELL_SH;
    }

    /*
     * These shell snippets could usefully pay some attention to
     * escaping of interesting characters. I don't think it causes a
     * problem at the moment, because the pathnames we use are so
     * utterly boring, but it's a lurking bug waiting to happen once
     * a bit more flexibility turns up.
     */

    switch (shell_type) {
      case SHELL_SH:
        printf("SSH_AUTH_SOCK=%s; export SSH_AUTH_SOCK;\n"
               "SSH_AGENT_PID=%d; export SSH_AGENT_PID;\n",
               socketname, pid);
        break;
      case SHELL_CSH:
        printf("setenv SSH_AUTH_SOCK %s;\n"
               "setenv SSH_AGENT_PID %d;\n",
               socketname, pid);
        break;
      case SHELL_AUTO:
        assert(0 && "Can't get here");
        break;
    }
}

void pageant_fork_and_print_env(int retain_tty)
{
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(1);
    } else if (pid != 0) {
        pageant_print_env(pid);
        exit(0);
    }

    /*
     * Having forked off, we now daemonise ourselves as best we can.
     * It's good practice in general to setsid() ourself out of any
     * process group we didn't want to be part of, and to chdir("/")
     * to avoid holding any directories open that we don't need in
     * case someone wants to umount them; also, we should definitely
     * close standard output (because it will very likely be pointing
     * at a pipe from which some parent process is trying to read our
     * environment variable dump, so if we hold open another copy of
     * it then that process will never finish reading). We close
     * standard input too on general principles, but not standard
     * error, since we might need to shout a panicky error message
     * down that one.
     */
    if (chdir("/") < 0) {
        /* should there be an error condition, nothing we can do about
         * it anyway */
    }
    close(0);
    close(1);
    if (retain_tty) {
        /* Get out of our previous process group, to avoid being
         * blasted by passing signals. But keep our controlling tty,
         * so we can keep checking to see if we still have one. */
        setpgrp();
    } else {
        /* Do that, but also leave our entire session and detach from
         * the controlling tty (if any). */
        setsid();
    }
}

int signalpipe[2];

void sigchld(int signum)
{
    if (write(signalpipe[1], "x", 1) <= 0)
        /* not much we can do about it */;
}

#define TTY_LIFE_POLL_INTERVAL (TICKSPERSEC * 30)
void *dummy_timer_ctx;
static void tty_life_timer(void *ctx, unsigned long now)
{
    schedule_timer(TTY_LIFE_POLL_INTERVAL, tty_life_timer, &dummy_timer_ctx);
}

typedef enum {
    KEYACT_AGENT_LOAD,
    KEYACT_CLIENT_ADD,
    KEYACT_CLIENT_DEL,
    KEYACT_CLIENT_DEL_ALL,
    KEYACT_CLIENT_LIST,
    KEYACT_CLIENT_PUBLIC_OPENSSH,
    KEYACT_CLIENT_PUBLIC
} keyact;
struct cmdline_key_action {
    struct cmdline_key_action *next;
    keyact action;
    const char *filename;
};

int is_agent_action(keyact action)
{
    return action == KEYACT_AGENT_LOAD;
}

struct cmdline_key_action *keyact_head = NULL, *keyact_tail = NULL;

void add_keyact(keyact action, const char *filename)
{
    struct cmdline_key_action *a = snew(struct cmdline_key_action);
    a->action = action;
    a->filename = filename;
    a->next = NULL;
    if (keyact_tail)
        keyact_tail->next = a;
    else
        keyact_head = a;
    keyact_tail = a;
}

int have_controlling_tty(void)
{
    int fd = open("/dev/tty", O_RDONLY);
    if (fd < 0) {
        if (errno != ENXIO) {
            perror("/dev/tty: open");
            exit(1);
        }
        return FALSE;
    } else {
        close(fd);
        return TRUE;
    }
}

char **exec_args = NULL;
enum {
    LIFE_UNSPEC, LIFE_X11, LIFE_TTY, LIFE_DEBUG, LIFE_PERM, LIFE_EXEC
} life = LIFE_UNSPEC;
const char *display = NULL;

static char *askpass(const char *comment)
{
    if (have_controlling_tty()) {
        int ret;
        prompts_t *p = new_prompts(NULL);
        p->to_server = FALSE;
        p->name = dupstr("Pageant passphrase prompt");
        add_prompt(p,
                   dupprintf("Enter passphrase to load key '%s': ", comment),
                   FALSE);
        ret = console_get_userpass_input(p, NULL, 0);
        assert(ret >= 0);

        if (!ret) {
            perror("pageant: unable to read passphrase");
            free_prompts(p);
            return NULL;
        } else {
            char *passphrase = dupstr(p->prompts[0]->result);
            free_prompts(p);
            return passphrase;
        }
    } else if (display) {
        char *prompt, *passphrase;
        int success;

        /* in gtkask.c */
        char *gtk_askpass_main(const char *display, const char *wintitle,
                               const char *prompt, int *success);

        prompt = dupprintf("Enter passphrase to load key '%s': ", comment);
        passphrase = gtk_askpass_main(display,
                                      "Pageant passphrase prompt",
                                      prompt, &success);
        sfree(prompt);
        if (!success) {
            /* return value is error message */
            fprintf(stderr, "%s\n", passphrase);
            sfree(passphrase);
            passphrase = NULL;
        }
        return passphrase;
    } else {
        fprintf(stderr, "no way to read a passphrase without tty or "
                "X display\n");
        return NULL;
    }
}

static int unix_add_keyfile(const char *filename_str)
{
    Filename *filename = filename_from_str(filename_str);
    int status, ret;
    char *err;

    ret = TRUE;

    /*
     * Try without a passphrase.
     */
    status = pageant_add_keyfile(filename, NULL, &err);
    if (status == PAGEANT_ACTION_OK) {
        goto cleanup;
    } else if (status == PAGEANT_ACTION_FAILURE) {
        fprintf(stderr, "pageant: %s: %s\n", filename_str, err);
        ret = FALSE;
        goto cleanup;
    }

    /*
     * And now try prompting for a passphrase.
     */
    while (1) {
        char *passphrase = askpass(err);
        sfree(err);
        err = NULL;
        if (!passphrase)
            break;

        status = pageant_add_keyfile(filename, passphrase, &err);

        smemclr(passphrase, strlen(passphrase));
        sfree(passphrase);
        passphrase = NULL;

        if (status == PAGEANT_ACTION_OK) {
            goto cleanup;
        } else if (status == PAGEANT_ACTION_FAILURE) {
            fprintf(stderr, "pageant: %s: %s\n", filename_str, err);
            ret = FALSE;
            goto cleanup;
        }
    }

  cleanup:
    sfree(err);
    filename_free(filename);
    return ret;
}

void key_list_callback(void *ctx, const char *fingerprint,
                       const char *comment, struct pageant_pubkey *key)
{
    printf("%s %s\n", fingerprint, comment);
}

struct key_find_ctx {
    const char *string;
    int match_fp, match_comment;
    struct pageant_pubkey *found;
    int nfound;
};

int match_fingerprint_string(const char *string, const char *fingerprint)
{
    const char *hash;

    /* Find the hash in the fingerprint string. It'll be the word at the end. */
    hash = strrchr(fingerprint, ' ');
    assert(hash);
    hash++;

    /* Now see if the search string is a prefix of the full hash,
     * neglecting colons and case differences. */
    while (1) {
        while (*string == ':') string++;
        while (*hash == ':') hash++;
        if (!*string)
            return TRUE;
        if (tolower((unsigned char)*string) != tolower((unsigned char)*hash))
            return FALSE;
        string++;
        hash++;
    }
}

void key_find_callback(void *vctx, const char *fingerprint,
                       const char *comment, struct pageant_pubkey *key)
{
    struct key_find_ctx *ctx = (struct key_find_ctx *)vctx;

    if ((ctx->match_comment && !strcmp(ctx->string, comment)) ||
        (ctx->match_fp && match_fingerprint_string(ctx->string, fingerprint)))
    {
        if (!ctx->found)
            ctx->found = pageant_pubkey_copy(key);
        ctx->nfound++;
    }
}

struct pageant_pubkey *find_key(const char *string, char **retstr)
{
    struct key_find_ctx actx, *ctx = &actx;
    struct pageant_pubkey key_in, *key_ret;
    int try_file = TRUE, try_fp = TRUE, try_comment = TRUE;
    int file_errors = FALSE;

    /*
     * Trim off disambiguating prefixes telling us how to interpret
     * the provided string.
     */
    if (!strncmp(string, "file:", 5)) {
        string += 5;
        try_fp = try_comment = FALSE;
        file_errors = TRUE; /* also report failure to load the file */
    } else if (!strncmp(string, "comment:", 8)) {
        string += 8;
        try_file = try_fp = FALSE;
    } else if (!strncmp(string, "fp:", 3)) {
        string += 3;
        try_file = try_comment = FALSE;
    } else if (!strncmp(string, "fingerprint:", 12)) {
        string += 12;
        try_file = try_comment = FALSE;
    }

    /*
     * Try interpreting the string as a key file name.
     */
    if (try_file) {
        Filename *fn = filename_from_str(string);
        int keytype = key_type(fn);
        if (keytype == SSH_KEYTYPE_SSH1 ||
            keytype == SSH_KEYTYPE_SSH1_PUBLIC) {
            const char *error;

            if (!rsakey_pubblob(fn, &key_in.blob, &key_in.bloblen,
                                NULL, &error)) {
                if (file_errors) {
                    *retstr = dupprintf("unable to load file '%s': %s",
                                        string, error);
                    filename_free(fn);
                    return NULL;
                }
            } else {
                /*
                 * If we've successfully loaded the file, stop here - we
                 * already have a key blob and need not go to the agent to
                 * list things.
                 */
                key_in.ssh_version = 1;
                key_in.comment = NULL;
                key_ret = pageant_pubkey_copy(&key_in);
                sfree(key_in.blob);
                filename_free(fn);
                return key_ret;
            }
        } else if (keytype == SSH_KEYTYPE_SSH2 ||
                   keytype == SSH_KEYTYPE_SSH2_PUBLIC_RFC4716 ||
                   keytype == SSH_KEYTYPE_SSH2_PUBLIC_OPENSSH) {
            const char *error;

            if ((key_in.blob = ssh2_userkey_loadpub(fn, NULL,
                                                    &key_in.bloblen,
                                                    NULL, &error)) == NULL) {
                if (file_errors) {
                    *retstr = dupprintf("unable to load file '%s': %s",
                                        string, error);
                    filename_free(fn);
                    return NULL;
                }
            } else {
                /*
                 * If we've successfully loaded the file, stop here - we
                 * already have a key blob and need not go to the agent to
                 * list things.
                 */
                key_in.ssh_version = 2;
                key_in.comment = NULL;
                key_ret = pageant_pubkey_copy(&key_in);
                sfree(key_in.blob);
                filename_free(fn);
                return key_ret;
            }
        } else {
            if (file_errors) {
                *retstr = dupprintf("unable to load key file '%s': %s",
                                    string, key_type_to_str(keytype));
                filename_free(fn);
                return NULL;
            }
        }
        filename_free(fn);
    }

    /*
     * Failing that, go through the keys in the agent, and match
     * against fingerprints and comments as appropriate.
     */
    ctx->string = string;
    ctx->match_fp = try_fp;
    ctx->match_comment = try_comment;
    ctx->found = NULL;
    ctx->nfound = 0;
    if (pageant_enum_keys(key_find_callback, ctx, retstr) ==
        PAGEANT_ACTION_FAILURE)
        return NULL;

    if (ctx->nfound == 0) {
        *retstr = dupstr("no key matched");
        assert(!ctx->found);
        return NULL;
    } else if (ctx->nfound > 1) {
        *retstr = dupstr("multiple keys matched");
        assert(ctx->found);
        pageant_pubkey_free(ctx->found);
        return NULL;
    }

    assert(ctx->found);
    return ctx->found;
}

void run_client(void)
{
    const struct cmdline_key_action *act;
    struct pageant_pubkey *key;
    int errors = FALSE;
    char *retstr;

    if (!agent_exists()) {
        fprintf(stderr, "pageant: no agent running to talk to\n");
        exit(1);
    }

    for (act = keyact_head; act; act = act->next) {
        switch (act->action) {
          case KEYACT_CLIENT_ADD:
            if (!unix_add_keyfile(act->filename))
                errors = TRUE;
            break;
          case KEYACT_CLIENT_LIST:
            if (pageant_enum_keys(key_list_callback, NULL, &retstr) ==
                PAGEANT_ACTION_FAILURE) {
                fprintf(stderr, "pageant: listing keys: %s\n", retstr);
                sfree(retstr);
                errors = TRUE;
            }
            break;
          case KEYACT_CLIENT_DEL:
            key = NULL;
            if (!(key = find_key(act->filename, &retstr)) ||
                pageant_delete_key(key, &retstr) == PAGEANT_ACTION_FAILURE) {
                fprintf(stderr, "pageant: deleting key '%s': %s\n",
                        act->filename, retstr);
                sfree(retstr);
                errors = TRUE;
            }
            if (key)
                pageant_pubkey_free(key);
            break;
          case KEYACT_CLIENT_PUBLIC_OPENSSH:
          case KEYACT_CLIENT_PUBLIC:
            key = NULL;
            if (!(key = find_key(act->filename, &retstr))) {
                fprintf(stderr, "pageant: finding key '%s': %s\n",
                        act->filename, retstr);
                sfree(retstr);
                errors = TRUE;
            } else {
                FILE *fp = stdout;     /* FIXME: add a -o option? */

                if (key->ssh_version == 1) {
                    struct RSAKey rkey;
                    memset(&rkey, 0, sizeof(rkey));
                    rkey.comment = dupstr(key->comment);
                    makekey(key->blob, key->bloblen, &rkey, NULL, 0);
                    ssh1_write_pubkey(fp, &rkey);
                    freersakey(&rkey);
                } else {
                    ssh2_write_pubkey(fp, key->comment, key->blob,key->bloblen,
                                      (act->action == KEYACT_CLIENT_PUBLIC ?
                                       SSH_KEYTYPE_SSH2_PUBLIC_RFC4716 :
                                       SSH_KEYTYPE_SSH2_PUBLIC_OPENSSH));
                }
                pageant_pubkey_free(key);
            }
            break;
          case KEYACT_CLIENT_DEL_ALL:
            if (pageant_delete_all_keys(&retstr) == PAGEANT_ACTION_FAILURE) {
                fprintf(stderr, "pageant: deleting all keys: %s\n", retstr);
                sfree(retstr);
                errors = TRUE;
            }
            break;
          default:
            assert(0 && "Invalid client action found");
        }
    }

    if (errors)
        exit(1);
}

void run_agent(void)
{
    const char *err;
    char *username, *socketdir;
    struct pageant_listen_state *pl;
    Socket sock;
    unsigned long now;
    int *fdlist;
    int fd;
    int i, fdcount, fdsize, fdstate;
    int termination_pid = -1;
    int errors = FALSE;
    Conf *conf;
    const struct cmdline_key_action *act;

    fdlist = NULL;
    fdcount = fdsize = 0;

    pageant_init();

    /*
     * Start by loading any keys provided on the command line.
     */
    for (act = keyact_head; act; act = act->next) {
        assert(act->action == KEYACT_AGENT_LOAD);
        if (!unix_add_keyfile(act->filename))
            errors = TRUE;
    }
    if (errors)
        exit(1);

    /*
     * Set up a listening socket and run Pageant on it.
     */
    username = get_username();
    socketdir = dupprintf("%s.%s", PAGEANT_DIR_PREFIX, username);
    sfree(username);
    assert(*socketdir == '/');
    if ((err = make_dir_and_check_ours(socketdir)) != NULL) {
        fprintf(stderr, "pageant: %s: %s\n", socketdir, err);
        exit(1);
    }
    socketname = dupprintf("%s/pageant.%d", socketdir, (int)getpid());
    pl = pageant_listener_new();
    sock = new_unix_listener(unix_sock_addr(socketname), (Plug)pl);
    if ((err = sk_socket_error(sock)) != NULL) {
        fprintf(stderr, "pageant: %s: %s\n", socketname, err);
        exit(1);
    }
    pageant_listener_got_socket(pl, sock);

    conf = conf_new();
    conf_set_int(conf, CONF_proxy_type, PROXY_NONE);

    /*
     * Lifetime preparations.
     */
    signalpipe[0] = signalpipe[1] = -1;
    if (life == LIFE_X11) {
        struct X11Display *disp;
        void *greeting;
        int greetinglen;
        Socket s;
        struct X11Connection *conn;

        static const struct plug_function_table fn_table = {
            x11_log,
            x11_closing,
            x11_receive,
            x11_sent,
            NULL
        };

        if (!display) {
            fprintf(stderr, "pageant: no DISPLAY for -X mode\n");
            exit(1);
        }
        disp = x11_setup_display(display, conf);

        conn = snew(struct X11Connection);
        conn->fn = &fn_table;
        s = new_connection(sk_addr_dup(disp->addr),
                           disp->realhost, disp->port,
                           0, 1, 0, 0, (Plug)conn, conf);
        if ((err = sk_socket_error(s)) != NULL) {
            fprintf(stderr, "pageant: unable to connect to X server: %s", err);
            exit(1);
        }
        greeting = x11_make_greeting('B', 11, 0, disp->localauthproto,
                                     disp->localauthdata,
                                     disp->localauthdatalen,
                                     NULL, 0, &greetinglen);
        sk_write(s, greeting, greetinglen);
        smemclr(greeting, greetinglen);
        sfree(greeting);

        pageant_fork_and_print_env(FALSE);
    } else if (life == LIFE_TTY) {
        schedule_timer(TTY_LIFE_POLL_INTERVAL,
                       tty_life_timer, &dummy_timer_ctx);
        pageant_fork_and_print_env(TRUE);
    } else if (life == LIFE_PERM) {
        pageant_fork_and_print_env(FALSE);
    } else if (life == LIFE_DEBUG) {
        pageant_print_env(getpid());
        pageant_logfp = stdout;
    } else if (life == LIFE_EXEC) {
        pid_t agentpid, pid;

        agentpid = getpid();

        /*
         * Set up the pipe we'll use to tell us about SIGCHLD.
         */
        if (pipe(signalpipe) < 0) {
            perror("pipe");
            exit(1);
        }
        putty_signal(SIGCHLD, sigchld);

        pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        } else if (pid == 0) {
            setenv("SSH_AUTH_SOCK", socketname, TRUE);
            setenv("SSH_AGENT_PID", dupprintf("%d", (int)agentpid), TRUE);
            execvp(exec_args[0], exec_args);
            perror("exec");
            _exit(127);
        } else {
            termination_pid = pid;
        }
    }

    /*
     * Now we've decided on our logging arrangements, pass them on to
     * pageant.c.
     */
    pageant_listener_set_logfn(pl, NULL, pageant_logfp ? pageant_log : NULL);

    now = GETTICKCOUNT();

    while (!time_to_die) {
	fd_set rset, wset, xset;
	int maxfd;
	int rwx;
	int ret;
        unsigned long next;

	FD_ZERO(&rset);
	FD_ZERO(&wset);
	FD_ZERO(&xset);
	maxfd = 0;

        if (signalpipe[0] >= 0) {
            FD_SET_MAX(signalpipe[0], maxfd, rset);
        }

	/* Count the currently active fds. */
	i = 0;
	for (fd = first_fd(&fdstate, &rwx); fd >= 0;
	     fd = next_fd(&fdstate, &rwx)) i++;

	/* Expand the fdlist buffer if necessary. */
	if (i > fdsize) {
	    fdsize = i + 16;
	    fdlist = sresize(fdlist, fdsize, int);
	}

	/*
	 * Add all currently open fds to the select sets, and store
	 * them in fdlist as well.
	 */
	fdcount = 0;
	for (fd = first_fd(&fdstate, &rwx); fd >= 0;
	     fd = next_fd(&fdstate, &rwx)) {
	    fdlist[fdcount++] = fd;
	    if (rwx & 1)
		FD_SET_MAX(fd, maxfd, rset);
	    if (rwx & 2)
		FD_SET_MAX(fd, maxfd, wset);
	    if (rwx & 4)
		FD_SET_MAX(fd, maxfd, xset);
	}

        if (toplevel_callback_pending()) {
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 0;
            ret = select(maxfd, &rset, &wset, &xset, &tv);
        } else if (run_timers(now, &next)) {
            unsigned long then;
            long ticks;
            struct timeval tv;

            then = now;
            now = GETTICKCOUNT();
            if (now - then > next - then)
                ticks = 0;
            else
                ticks = next - now;
            tv.tv_sec = ticks / 1000;
            tv.tv_usec = ticks % 1000 * 1000;
            ret = select(maxfd, &rset, &wset, &xset, &tv);
            if (ret == 0)
                now = next;
            else
                now = GETTICKCOUNT();
        } else {
            ret = select(maxfd, &rset, &wset, &xset, NULL);
        }

        if (ret < 0 && errno == EINTR)
            continue;

	if (ret < 0) {
	    perror("select");
	    exit(1);
	}

        if (life == LIFE_TTY) {
            /*
             * Every time we wake up (whether it was due to tty_timer
             * elapsing or for any other reason), poll to see if we
             * still have a controlling terminal. If we don't, then
             * our containing tty session has ended, so it's time to
             * clean up and leave.
             */
            if (!have_controlling_tty()) {
                time_to_die = TRUE;
                break;
            }
        }

	for (i = 0; i < fdcount; i++) {
	    fd = fdlist[i];
            /*
             * We must process exceptional notifications before
             * ordinary readability ones, or we may go straight
             * past the urgent marker.
             */
	    if (FD_ISSET(fd, &xset))
		select_result(fd, 4);
	    if (FD_ISSET(fd, &rset))
		select_result(fd, 1);
	    if (FD_ISSET(fd, &wset))
		select_result(fd, 2);
	}

        if (signalpipe[0] >= 0 && FD_ISSET(signalpipe[0], &rset)) {
            char c[1];
            if (read(signalpipe[0], c, 1) <= 0)
                /* ignore error */;
            /* ignore its value; it'll be `x' */
            while (1) {
                int status;
                pid_t pid;
                pid = waitpid(-1, &status, WNOHANG);
                if (pid <= 0)
                    break;
                if (pid == termination_pid)
                    time_to_die = TRUE;
            }
        }

        run_toplevel_callbacks();
    }

    /*
     * When we come here, we're terminating, and should clean up our
     * Unix socket file if possible.
     */
    if (unlink(socketname) < 0) {
        fprintf(stderr, "pageant: %s: %s\n", socketname, strerror(errno));
        exit(1);
    }

    conf_free(conf);
}

int main(int argc, char **argv)
{
    int doing_opts = TRUE;
    keyact curr_keyact = KEYACT_AGENT_LOAD;

    /*
     * Process the command line.
     */
    while (--argc > 0) {
	char *p = *++argv;
	if (*p == '-' && doing_opts) {
            if (!strcmp(p, "-V") || !strcmp(p, "--version")) {
                version();
	    } else if (!strcmp(p, "--help")) {
                usage();
                exit(0);
            } else if (!strcmp(p, "-v")) {
                pageant_logfp = stderr;
            } else if (!strcmp(p, "-a")) {
                curr_keyact = KEYACT_CLIENT_ADD;
            } else if (!strcmp(p, "-d")) {
                curr_keyact = KEYACT_CLIENT_DEL;
            } else if (!strcmp(p, "-s")) {
                shell_type = SHELL_SH;
            } else if (!strcmp(p, "-c")) {
                shell_type = SHELL_CSH;
            } else if (!strcmp(p, "-D")) {
                add_keyact(KEYACT_CLIENT_DEL_ALL, NULL);
            } else if (!strcmp(p, "-l")) {
                add_keyact(KEYACT_CLIENT_LIST, NULL);
            } else if (!strcmp(p, "--public")) {
                curr_keyact = KEYACT_CLIENT_PUBLIC;
            } else if (!strcmp(p, "--public-openssh")) {
                curr_keyact = KEYACT_CLIENT_PUBLIC_OPENSSH;
            } else if (!strcmp(p, "-X")) {
                life = LIFE_X11;
            } else if (!strcmp(p, "-T")) {
                life = LIFE_TTY;
            } else if (!strcmp(p, "--debug")) {
                life = LIFE_DEBUG;
            } else if (!strcmp(p, "--permanent")) {
                life = LIFE_PERM;
            } else if (!strcmp(p, "--exec")) {
                life = LIFE_EXEC;
                /* Now all subsequent arguments go to the exec command. */
                if (--argc > 0) {
                    exec_args = ++argv;
                    argc = 0;          /* force end of option processing */
                } else {
                    fprintf(stderr, "pageant: expected a command "
                            "after --exec\n");
                    exit(1);
                }
            } else if (!strcmp(p, "--")) {
                doing_opts = FALSE;
            }
        } else {
            /*
             * Non-option arguments (apart from those after --exec,
             * which are treated specially above) are interpreted as
             * the names of private key files to either add or delete
             * from an agent.
             */
            add_keyact(curr_keyact, p);
        }
    }

    if (life == LIFE_EXEC && !exec_args) {
        fprintf(stderr, "pageant: expected a command with --exec\n");
        exit(1);
    }

    /*
     * Block SIGPIPE, so that we'll get EPIPE individually on
     * particular network connections that go wrong.
     */
    putty_signal(SIGPIPE, SIG_IGN);

    sk_init();
    uxsel_init();

    if (!display) {
        display = getenv("DISPLAY");
        if (display && !*display)
            display = NULL;
    }

    /*
     * Now distinguish our two main running modes. Either we're
     * actually starting up an agent, in which case we should have a
     * lifetime mode, and no key actions of KEYACT_CLIENT_* type; or
     * else we're contacting an existing agent to add or remove keys,
     * in which case we should have no lifetime mode, and no key
     * actions of KEYACT_AGENT_* type.
     */
    {
        int has_agent_actions = FALSE;
        int has_client_actions = FALSE;
        int has_lifetime = FALSE;
        const struct cmdline_key_action *act;

        for (act = keyact_head; act; act = act->next) {
            if (is_agent_action(act->action))
                has_agent_actions = TRUE;
            else
                has_client_actions = TRUE;
        }
        if (life != LIFE_UNSPEC)
            has_lifetime = TRUE;

        if (has_lifetime && has_client_actions) {
            fprintf(stderr, "pageant: client key actions (-a, -d, -D, -l, -L)"
                    " do not go with an agent lifetime option\n");
            exit(1);
        }
        if (!has_lifetime && has_agent_actions) {
            fprintf(stderr, "pageant: expected an agent lifetime option with"
                    " bare key file arguments\n");
            exit(1);
        }
        if (!has_lifetime && !has_client_actions) {
            fprintf(stderr, "pageant: expected an agent lifetime option"
                    " or a client key action\n");
            exit(1);
        }

        if (has_lifetime) {
            run_agent();
        } else if (has_client_actions) {
            run_client();
        }
    }

    return 0;
}
