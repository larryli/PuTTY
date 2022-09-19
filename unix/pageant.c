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
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

#include "putty.h"
#include "ssh.h"
#include "misc.h"
#include "pageant.h"

void cmdline_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    console_print_error_msg_fmt_v("pageant", fmt, ap);
    va_end(ap);
    exit(1);
}

static void setup_sigchld_handler(void);

typedef enum RuntimePromptType {
    RTPROMPT_UNAVAILABLE,
    RTPROMPT_DEBUG,
    RTPROMPT_GUI,
} RuntimePromptType;

static const char *progname;

struct uxpgnt_client {
    FILE *logfp;
    strbuf *prompt_buf;
    RuntimePromptType prompt_type;
    bool prompt_active;
    PageantClientDialogId *dlgid;
    int passphrase_fd;
    int termination_pid;

    PageantListenerClient plc;
};

static void uxpgnt_log(PageantListenerClient *plc, const char *fmt, va_list ap)
{
    struct uxpgnt_client *upc = container_of(plc, struct uxpgnt_client, plc);

    if (!upc->logfp)
        return;

    fprintf(upc->logfp, "pageant: ");
    vfprintf(upc->logfp, fmt, ap);
    fprintf(upc->logfp, "\n");
}

static int make_pipe_to_askpass(const char *msg)
{
    int pipefds[2];

    setup_sigchld_handler();

    if (pipe(pipefds) < 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefds[0]);
        close(pipefds[1]);
        return -1;
    }

    if (pid == 0) {
        const char *args[5] = {
            progname, "--gui-prompt", "--askpass", msg, NULL
        };

        dup2(pipefds[1], 1);
        cloexec(pipefds[0]);
        cloexec(pipefds[1]);

        /*
         * See comment in fork_and_exec_self() in main-gtk-simple.c.
         */
        execv("/proc/self/exe", (char **)args);
        execvp(progname, (char **)args);
        perror("exec");
        _exit(127);
    }

    close(pipefds[1]);
    return pipefds[0];
}

static bool uxpgnt_ask_passphrase(
    PageantListenerClient *plc, PageantClientDialogId *dlgid,
    const char *comment)
{
    struct uxpgnt_client *upc = container_of(plc, struct uxpgnt_client, plc);

    assert(!upc->dlgid); /* Pageant core should be serialising requests */

    char *msg = dupprintf(
        "A client of Pageant wants to use the following encrypted key:\n"
        "%s\n"
        "If you intended this, enter the passphrase to decrypt the key.",
        comment);

    switch (upc->prompt_type) {
      case RTPROMPT_UNAVAILABLE:
        sfree(msg);
        return false;

      case RTPROMPT_GUI:
        upc->passphrase_fd = make_pipe_to_askpass(msg);
        sfree(msg);
        if (upc->passphrase_fd < 0)
            return false; /* something went wrong */
        break;

      case RTPROMPT_DEBUG:
        fprintf(upc->logfp, "pageant passphrase request: %s\n", msg);
        sfree(msg);
        break;
    }

    upc->prompt_active = true;
    upc->dlgid = dlgid;
    return true;
}

static void passphrase_done(struct uxpgnt_client *upc, bool success)
{
    PageantClientDialogId *dlgid = upc->dlgid;
    upc->dlgid = NULL;
    upc->prompt_active = false;

    if (upc->logfp)
        fprintf(upc->logfp, "pageant passphrase response: %s\n",
                success ? "success" : "failure");

    if (success)
        pageant_passphrase_request_success(
            dlgid, ptrlen_from_strbuf(upc->prompt_buf));
    else
        pageant_passphrase_request_refused(dlgid);

    strbuf_free(upc->prompt_buf);
    upc->prompt_buf = strbuf_new_nm();
}

static const PageantListenerClientVtable uxpgnt_vtable = {
    .log = uxpgnt_log,
    .ask_passphrase = uxpgnt_ask_passphrase,
};

/*
 * More stubs.
 */
void random_save_seed(void) {}
void random_destroy_seed(void) {}
char *platform_default_s(const char *name) { return NULL; }
bool platform_default_b(const char *name, bool def) { return def; }
int platform_default_i(const char *name, int def) { return def; }
FontSpec *platform_default_fontspec(const char *name) { return fontspec_new(""); }
Filename *platform_default_filename(const char *name) { return filename_from_str(""); }
char *x_get_default(const char *key) { return NULL; }

/*
 * Short description of parameters.
 */
static void usage(void)
{
    printf("Pageant: SSH agent\n");
    printf("%s\n", ver);
    printf("Usage: pageant <lifetime> [[--encrypted] key files]\n");
    printf("       pageant [[--encrypted] key files] --exec <command> [args]\n");
    printf("       pageant -a [--encrypted] [key files]\n");
    printf("       pageant -d [key identifiers]\n");
    printf("       pageant -D\n");
    printf("       pageant -r [key identifiers]\n");
    printf("       pageant -R\n");
    printf("       pageant --public [key identifiers]\n");
    printf("       pageant ( --public-openssh | -L ) [key identifiers]\n");
    printf("       pageant -l [-E fptype]\n");
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
    printf("  --public-openssh, -L   print public keys in OpenSSH format\n");
    printf("  -d           delete key(s) from the agent\n");
    printf("  -D           delete all keys from the agent\n");
    printf("  -r           re-encrypt keys in the agent (forget cleartext)\n");
    printf("  -R           re-encrypt all possible keys in the agent\n");
    printf("Other options:\n");
    printf("  -v           verbose mode (in agent mode)\n");
    printf("  -s -c        force POSIX or C shell syntax (in agent mode)\n");
    printf("  --symlink path   create symlink to socket (in agent mode)\n");
    printf("  --encrypted  when adding keys, don't decrypt\n");
    printf("  -E alg, --fptype alg   fingerprint type for -l (sha256, md5)\n");
    printf("  --tty-prompt force tty-based passphrase prompt\n");
    printf("  --gui-prompt force GUI-based passphrase prompt\n");
    printf("  --askpass <prompt>   behave like a standalone askpass program\n");
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

static bool time_to_die = false;

/*
 * These functions are part of the plug for our connection to the X
 * display, so they do get called. They needn't actually do anything,
 * except that x11_closing has to signal back to the main loop that
 * it's time to terminate.
 */
static void x11_log(Plug *p, PlugLogType type, SockAddr *addr, int port,
                    const char *error_msg, int error_code) {}
static void x11_receive(Plug *plug, int urgent, const char *data, size_t len) {}
static void x11_sent(Plug *plug, size_t bufsize) {}
static void x11_closing(Plug *plug, PlugCloseType type, const char *error_msg)
{
    time_to_die = true;
}
struct X11Connection {
    Plug plug;
};

static char *socketname;
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
        unreachable("SHELL_AUTO should have been eliminated by now");
        break;
    }
}

void pageant_fork_and_print_env(bool retain_tty)
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
#if HAVE_NULLARY_SETPGRP
        setpgrp();
#elif HAVE_BINARY_SETPGRP
        setpgrp(0, 0);
#endif
    } else {
        /* Do that, but also leave our entire session and detach from
         * the controlling tty (if any). */
        setsid();
    }
}

static int signalpipe[2] = { -1, -1 };

static void sigchld(int signum)
{
    if (write(signalpipe[1], "x", 1) <= 0)
        /* not much we can do about it */;
}

static void setup_sigchld_handler(void)
{
    if (signalpipe[0] >= 0)
        return;

    /*
     * Set up the pipe we'll use to tell us about SIGCHLD.
     */
    if (pipe(signalpipe) < 0) {
        perror("pipe");
        exit(1);
    }
    putty_signal(SIGCHLD, sigchld);
}

#define TTY_LIFE_POLL_INTERVAL (TICKSPERSEC * 30)
static void *dummy_timer_ctx;
static void tty_life_timer(void *ctx, unsigned long now)
{
    schedule_timer(TTY_LIFE_POLL_INTERVAL, tty_life_timer, &dummy_timer_ctx);
}

typedef enum {
    KEYACT_AGENT_LOAD,
    KEYACT_AGENT_LOAD_ENCRYPTED,
    KEYACT_CLIENT_BASE,
    KEYACT_CLIENT_ADD = KEYACT_CLIENT_BASE,
    KEYACT_CLIENT_ADD_ENCRYPTED,
    KEYACT_CLIENT_DEL,
    KEYACT_CLIENT_DEL_ALL,
    KEYACT_CLIENT_LIST,
    KEYACT_CLIENT_PUBLIC_OPENSSH,
    KEYACT_CLIENT_PUBLIC,
    KEYACT_CLIENT_SIGN,
    KEYACT_CLIENT_REENCRYPT,
    KEYACT_CLIENT_REENCRYPT_ALL,
} keyact;
struct cmdline_key_action {
    struct cmdline_key_action *next;
    keyact action;
    const char *filename;
};

bool is_agent_action(keyact action)
{
    return action < KEYACT_CLIENT_BASE;
}

static struct cmdline_key_action *keyact_head = NULL, *keyact_tail = NULL;
static uint32_t sign_flags = 0;

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

bool have_controlling_tty(void)
{
    int fd = open("/dev/tty", O_RDONLY);
    if (fd < 0) {
        if (errno != ENXIO) {
            perror("/dev/tty: open");
            exit(1);
        }
        return false;
    } else {
        close(fd);
        return true;
    }
}

static char **exec_args = NULL;
static enum {
    LIFE_UNSPEC, LIFE_X11, LIFE_TTY, LIFE_DEBUG, LIFE_PERM, LIFE_EXEC
} life = LIFE_UNSPEC;
static const char *display = NULL;
static enum {
    PROMPT_UNSPEC, PROMPT_TTY, PROMPT_GUI
} prompt_type = PROMPT_UNSPEC;
static FingerprintType key_list_fptype = SSH_FPTYPE_DEFAULT;

static char *askpass_tty(const char *prompt)
{
    prompts_t *p = new_prompts();
    p->to_server = false;
    p->from_server = false;
    p->name = dupstr("Pageant passphrase prompt");
    add_prompt(p, dupcat(prompt, ": "), false);
    SeatPromptResult spr = console_get_userpass_input(p);
    assert(spr.kind != SPRK_INCOMPLETE);

    if (spr.kind == SPRK_USER_ABORT) {
        free_prompts(p);
        return NULL;
    } else if (spr.kind == SPRK_SW_ABORT) {
        free_prompts(p);
        char *err = spr_get_error_message(spr);
        fprintf(stderr, "pageant: unable to read passphrase: %s", err);
        sfree(err);
        return NULL;
    } else {
        char *passphrase = prompt_get_result(p->prompts[0]);
        free_prompts(p);
        return passphrase;
    }
}

static char *askpass_gui(const char *prompt)
{
    char *passphrase;
    bool success;

    passphrase = gtk_askpass_main(
        display, "Pageant passphrase prompt", prompt, &success);
    if (!success) {
        /* return value is error message */
        fprintf(stderr, "%s\n", passphrase);
        sfree(passphrase);
        passphrase = NULL;
    }
    return passphrase;
}

static char *askpass(const char *prompt)
{
    if (prompt_type == PROMPT_TTY) {
        if (!have_controlling_tty()) {
            fprintf(stderr, "no controlling terminal available "
                    "for passphrase prompt\n");
            return NULL;
        }
        return askpass_tty(prompt);
    }

    if (prompt_type == PROMPT_GUI) {
        if (!display) {
            fprintf(stderr, "no graphical display available "
                    "for passphrase prompt\n");
            return NULL;
        }
        return askpass_gui(prompt);
    }

    if (have_controlling_tty()) {
        return askpass_tty(prompt);
    } else if (display) {
        return askpass_gui(prompt);
    } else {
        fprintf(stderr, "no way to read a passphrase without tty or "
                "X display\n");
        return NULL;
    }
}

static bool unix_add_keyfile(const char *filename_str, bool add_encrypted)
{
    Filename *filename = filename_from_str(filename_str);
    int status;
    bool ret;
    char *err;

    ret = true;

    /*
     * Try without a passphrase.
     */
    status = pageant_add_keyfile(filename, NULL, &err, add_encrypted);
    if (status == PAGEANT_ACTION_OK) {
        goto cleanup;
    } else if (status == PAGEANT_ACTION_FAILURE) {
        fprintf(stderr, "pageant: %s: %s\n", filename_str, err);
        ret = false;
        goto cleanup;
    }

    /*
     * And now try prompting for a passphrase.
     */
    while (1) {
        char *prompt = dupprintf(
            "Enter passphrase to load key '%s'", err);
        char *passphrase = askpass(prompt);
        sfree(err);
        sfree(prompt);
        err = NULL;
        if (!passphrase)
            break;

        status = pageant_add_keyfile(filename, passphrase, &err,
                                     add_encrypted);

        smemclr(passphrase, strlen(passphrase));
        sfree(passphrase);
        passphrase = NULL;

        if (status == PAGEANT_ACTION_OK) {
            goto cleanup;
        } else if (status == PAGEANT_ACTION_FAILURE) {
            fprintf(stderr, "pageant: %s: %s\n", filename_str, err);
            ret = false;
            goto cleanup;
        }
    }

  cleanup:
    sfree(err);
    filename_free(filename);
    return ret;
}

void key_list_callback(void *ctx, char **fingerprints, const char *comment,
                       uint32_t ext_flags, struct pageant_pubkey *key)
{
    const char *mode = "";
    if (ext_flags & LIST_EXTENDED_FLAG_HAS_NO_CLEARTEXT_KEY)
        mode = " (encrypted)";
    else if (ext_flags & LIST_EXTENDED_FLAG_HAS_ENCRYPTED_KEY_FILE)
        mode = " (re-encryptable)";

    FingerprintType this_type =
        ssh2_pick_fingerprint(fingerprints, key_list_fptype);
    printf("%s %s%s\n", fingerprints[this_type], comment, mode);
}

struct key_find_ctx {
    const char *string;
    bool match_fp, match_comment;
    bool match_fptypes[SSH_N_FPTYPES];
    struct pageant_pubkey *found;
    int nfound;
};

static bool match_fingerprint_string(
    const char *string_orig, char **fingerprints,
    const struct key_find_ctx *ctx)
{
    const char *hash;

    for (unsigned fptype = 0; fptype < SSH_N_FPTYPES; fptype++) {
        if (!ctx->match_fptypes[fptype])
            continue;

        const char *fingerprint = fingerprints[fptype];
        if (!fingerprint)
            continue;

        /* Find the hash in the fingerprint string. It'll be the word
         * at the end. */
        hash = strrchr(fingerprint, ' ');
        assert(hash);
        hash++;

        const char *string = string_orig;
        bool case_sensitive;
        const char *ignore_chars = "";

        switch (fptype) {
          case SSH_FPTYPE_MD5:
          case SSH_FPTYPE_MD5_CERT:
            /* MD5 fingerprints are in hex, so disregard case differences. */
            case_sensitive = false;
            /* And we don't really need to force the user to type the
             * colons in between the digits, which are always the
             * same. */
            ignore_chars = ":";
            break;
          case SSH_FPTYPE_SHA256:
          case SSH_FPTYPE_SHA256_CERT:
            /* Skip over the "SHA256:" prefix, which we don't really
             * want to force the user to type. On the other hand,
             * tolerate it on the input string. */
            assert(strstartswith(hash, "SHA256:"));
            hash += 7;
            if (strstartswith(string, "SHA256:"))
                string += 7;
            /* SHA256 fingerprints are base64, which is intrinsically
             * case sensitive. */
            case_sensitive = true;
            break;
        }

        /* Now see if the search string is a prefix of the full hash,
         * neglecting colons and (where appropriate) case differences. */
        while (1) {
            string += strspn(string, ignore_chars);
            hash += strspn(hash, ignore_chars);
            if (!*string)
                return true;
            char sc = *string, hc = *hash;
            if (!case_sensitive) {
                sc = tolower((unsigned char)sc);
                hc = tolower((unsigned char)hc);
            }
            if (sc != hc)
                break;
            string++;
            hash++;
        }
    }

    return false;
}

void key_find_callback(void *vctx, char **fingerprints,
                       const char *comment, uint32_t ext_flags,
                       struct pageant_pubkey *key)
{
    struct key_find_ctx *ctx = (struct key_find_ctx *)vctx;

    if ((ctx->match_comment && !strcmp(ctx->string, comment)) ||
        (ctx->match_fp && match_fingerprint_string(ctx->string, fingerprints,
                                                   ctx)))
    {
        if (!ctx->found)
            ctx->found = pageant_pubkey_copy(key);
        ctx->nfound++;
    }
}

struct pageant_pubkey *find_key(const char *string, char **retstr)
{
    struct key_find_ctx ctx[1];
    struct pageant_pubkey key_in, *key_ret;
    bool try_file = true, try_fp = true, try_comment = true;
    bool file_errors = false;
    bool try_all_fptypes = true;
    FingerprintType fptype = SSH_FPTYPE_DEFAULT;

    /*
     * Trim off disambiguating prefixes telling us how to interpret
     * the provided string.
     */
    if (!strncmp(string, "file:", 5)) {
        string += 5;
        try_fp = false;
        try_comment = false;
        file_errors = true; /* also report failure to load the file */
    } else if (!strncmp(string, "comment:", 8)) {
        string += 8;
        try_file = false;
        try_fp = false;
    } else if (!strncmp(string, "fp:", 3)) {
        string += 3;
        try_file = false;
        try_comment = false;
    } else if (!strncmp(string, "fingerprint:", 12)) {
        string += 12;
        try_file = false;
        try_comment = false;
    } else if (!strnicmp(string, "md5:", 4)) {
        string += 4;
        try_file = false;
        try_comment = false;
        try_all_fptypes = false;
        fptype = SSH_FPTYPE_MD5;
    } else if (!strncmp(string, "sha256:", 7)) {
        string += 7;
        try_file = false;
        try_comment = false;
        try_all_fptypes = false;
        fptype = SSH_FPTYPE_SHA256;
    } else if (!strnicmp(string, "md5-cert:", 9)) {
        string += 9;
        try_file = false;
        try_comment = false;
        try_all_fptypes = false;
        fptype = SSH_FPTYPE_MD5_CERT;
    } else if (!strncmp(string, "sha256-cert:", 12)) {
        string += 12;
        try_file = false;
        try_comment = false;
        try_all_fptypes = false;
        fptype = SSH_FPTYPE_SHA256_CERT;
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

            key_in.blob = strbuf_new();
            if (!rsa1_loadpub_f(fn, BinarySink_UPCAST(key_in.blob),
                                NULL, &error)) {
                strbuf_free(key_in.blob);
                key_in.blob = NULL;
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
                strbuf_free(key_in.blob);
                key_in.blob = NULL;
                filename_free(fn);
                return key_ret;
            }
        } else if (keytype == SSH_KEYTYPE_SSH2 ||
                   keytype == SSH_KEYTYPE_SSH2_PUBLIC_RFC4716 ||
                   keytype == SSH_KEYTYPE_SSH2_PUBLIC_OPENSSH) {
            const char *error;

            key_in.blob = strbuf_new();
            if (!ppk_loadpub_f(fn, NULL, BinarySink_UPCAST(key_in.blob),
                               NULL, &error)) {
                strbuf_free(key_in.blob);
                key_in.blob = NULL;
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
                strbuf_free(key_in.blob);
                key_in.blob = NULL;
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
    for (unsigned i = 0; i < SSH_N_FPTYPES; i++)
        ctx->match_fptypes[i] = (try_all_fptypes || i == fptype);
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
    bool errors = false;
    char *retstr;
    LoadedFile *message = lf_new(AGENT_MAX_MSGLEN);
    bool message_loaded = false, message_ok = false;
    strbuf *signature = strbuf_new();

    if (!agent_exists()) {
        fprintf(stderr, "pageant: no agent running to talk to\n");
        exit(1);
    }

    for (act = keyact_head; act; act = act->next) {
        switch (act->action) {
          case KEYACT_CLIENT_ADD:
          case KEYACT_CLIENT_ADD_ENCRYPTED:
            if (!unix_add_keyfile(act->filename,
                                  act->action == KEYACT_CLIENT_ADD_ENCRYPTED))
                errors = true;
            break;
          case KEYACT_CLIENT_LIST:
            if (pageant_enum_keys(key_list_callback, NULL, &retstr) ==
                PAGEANT_ACTION_FAILURE) {
                fprintf(stderr, "pageant: listing keys: %s\n", retstr);
                sfree(retstr);
                errors = true;
            }
            break;
          case KEYACT_CLIENT_DEL:
            key = NULL;
            if (!(key = find_key(act->filename, &retstr)) ||
                pageant_delete_key(key, &retstr) == PAGEANT_ACTION_FAILURE) {
                fprintf(stderr, "pageant: deleting key '%s': %s\n",
                        act->filename, retstr);
                sfree(retstr);
                errors = true;
            }
            if (key)
                pageant_pubkey_free(key);
            break;
          case KEYACT_CLIENT_REENCRYPT:
            key = NULL;
            if (!(key = find_key(act->filename, &retstr)) ||
                pageant_reencrypt_key(key, &retstr) == PAGEANT_ACTION_FAILURE) {
                fprintf(stderr, "pageant: re-encrypting key '%s': %s\n",
                        act->filename, retstr);
                sfree(retstr);
                errors = true;
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
                errors = true;
            } else {
                FILE *fp = stdout;     /* FIXME: add a -o option? */

                if (key->ssh_version == 1) {
                    BinarySource src[1];
                    RSAKey rkey;

                    BinarySource_BARE_INIT(src, key->blob->u, key->blob->len);
                    memset(&rkey, 0, sizeof(rkey));
                    rkey.comment = dupstr(key->comment);
                    get_rsa_ssh1_pub(src, &rkey, RSA_SSH1_EXPONENT_FIRST);
                    ssh1_write_pubkey(fp, &rkey);
                    freersakey(&rkey);
                } else {
                    ssh2_write_pubkey(fp, key->comment,
                                      key->blob->u,
                                      key->blob->len,
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
                errors = true;
            }
            break;
          case KEYACT_CLIENT_REENCRYPT_ALL: {
            int status = pageant_reencrypt_all_keys(&retstr);
            if (status == PAGEANT_ACTION_FAILURE) {
                fprintf(stderr, "pageant: re-encrypting all keys: "
                        "%s\n", retstr);
                sfree(retstr);
                errors = true;
            } else if (status == PAGEANT_ACTION_WARNING) {
                fprintf(stderr, "pageant: re-encrypting all keys: "
                        "warning: %s\n", retstr);
                sfree(retstr);
            }
            break;
          }
          case KEYACT_CLIENT_SIGN:
            key = NULL;
            if (!message_loaded) {
                message_loaded = true;
                switch(lf_load_fp(message, stdin)) {
                  case LF_TOO_BIG:
                    fprintf(stderr, "pageant: message to sign is too big\n");
                    errors = true;
                    break;
                  case LF_ERROR:
                    fprintf(stderr, "pageant: reading message to sign: %s\n",
                            strerror(errno));
                    errors = true;
                    break;
                  case LF_OK:
                    message_ok = true;
                    break;
                }
            }
            if (!message_ok)
                break;
            strbuf_clear(signature);
            if (!(key = find_key(act->filename, &retstr)) ||
                pageant_sign(key, ptrlen_from_lf(message), signature,
                             sign_flags, &retstr) == PAGEANT_ACTION_FAILURE) {
                fprintf(stderr, "pageant: signing with key '%s': %s\n",
                        act->filename, retstr);
                sfree(retstr);
                errors = true;
            } else {
                fwrite(signature->s, 1, signature->len, stdout);
            }
            if (key)
                pageant_pubkey_free(key);
            break;
          default:
            unreachable("Invalid client action found");
        }
    }

    lf_free(message);
    strbuf_free(signature);

    if (errors)
        exit(1);
}

static const PlugVtable X11Connection_plugvt = {
    .log = x11_log,
    .closing = x11_closing,
    .receive = x11_receive,
    .sent = x11_sent,
};


static bool agent_loop_pw_setup(void *vctx, pollwrapper *pw)
{
    struct uxpgnt_client *upc = (struct uxpgnt_client *)vctx;

    if (signalpipe[0] >= 0) {
        pollwrap_add_fd_rwx(pw, signalpipe[0], SELECT_R);
    }

    if (upc->prompt_active)
        pollwrap_add_fd_rwx(pw, upc->passphrase_fd, SELECT_R);

    return true;
}

static void agent_loop_pw_check(void *vctx, pollwrapper *pw)
{
    struct uxpgnt_client *upc = (struct uxpgnt_client *)vctx;

    if (life == LIFE_TTY) {
        /*
         * Every time we wake up (whether it was due to tty_timer
         * elapsing or for any other reason), poll to see if we still
         * have a controlling terminal. If we don't, then our
         * containing tty session has ended, so it's time to clean up
         * and leave.
         */
        if (!have_controlling_tty()) {
            time_to_die = true;
            return;
        }
    }

    if (signalpipe[0] >= 0 &&
        pollwrap_check_fd_rwx(pw, signalpipe[0], SELECT_R)) {
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
            if (pid == upc->termination_pid)
                time_to_die = true;
        }
    }

    if (upc->prompt_active &&
        pollwrap_check_fd_rwx(pw, upc->passphrase_fd, SELECT_R)) {
        char c;
        int retd = read(upc->passphrase_fd, &c, 1);

        switch (upc->prompt_type) {
          case RTPROMPT_GUI:
            if (retd <= 0) {
                close(upc->passphrase_fd);
                upc->passphrase_fd = -1;
                bool ok = (retd == 0);
                if (!strbuf_chomp(upc->prompt_buf, '\n'))
                    ok = false;
                passphrase_done(upc, ok);
            } else {
                put_byte(upc->prompt_buf, c);
            }
            break;
          case RTPROMPT_DEBUG:
            if (retd <= 0) {
                passphrase_done(upc, false);
                /* Now never try to read from stdin again */
                upc->prompt_type = RTPROMPT_UNAVAILABLE;
                break;
            }

            switch (c) {
              case '\n':
              case '\r':
                passphrase_done(upc, true);
                break;
              case '\004':
                passphrase_done(upc, false);
                break;
              case '\b':
              case '\177':
                strbuf_shrink_by(upc->prompt_buf, 1);
                break;
              case '\025':
                strbuf_clear(upc->prompt_buf);
                break;
              default:
                put_byte(upc->prompt_buf, c);
                break;
            }
            break;
          case RTPROMPT_UNAVAILABLE:
            unreachable("Should never have started a prompt at all");
        }
    }
}

static bool agent_loop_continue(void *vctx, bool fd, bool cb)
{
    return !time_to_die;
}

void run_agent(FILE *logfp, const char *symlink_path)
{
    const char *err;
    char *errw;
    struct pageant_listen_state *pl;
    Plug *pl_plug;
    Socket *sock;
    bool errors = false;
    Conf *conf;
    const struct cmdline_key_action *act;

    pageant_init();

    /*
     * Start by loading any keys provided on the command line.
     */
    for (act = keyact_head; act; act = act->next) {
        assert(act->action == KEYACT_AGENT_LOAD ||
               act->action == KEYACT_AGENT_LOAD_ENCRYPTED);
        if (!unix_add_keyfile(act->filename,
                              act->action == KEYACT_AGENT_LOAD_ENCRYPTED))
            errors = true;
    }
    if (errors)
        exit(1);

    /*
     * Set up a listening socket and run Pageant on it.
     */
    struct uxpgnt_client upc[1];
    memset(upc, 0, sizeof(upc));
    upc->plc.vt = &uxpgnt_vtable;
    upc->logfp = logfp;
    upc->passphrase_fd = -1;
    upc->termination_pid = -1;
    upc->prompt_buf = strbuf_new_nm();
    upc->prompt_type = display ? RTPROMPT_GUI : RTPROMPT_UNAVAILABLE;
    pl = pageant_listener_new(&pl_plug, &upc->plc);
    sock = platform_make_agent_socket(pl_plug, PAGEANT_DIR_PREFIX,
                                      &errw, &socketname);
    if (!sock) {
        fprintf(stderr, "pageant: %s\n", errw);
        sfree(errw);
        exit(1);
    }
    pageant_listener_got_socket(pl, sock);

    if (symlink_path) {
        /*
         * Try to make a symlink to the Unix socket, in a location of
         * the user's choosing.
         *
         * If the link already exists, we want to replace it. There
         * are two ways we could do this: either make it under another
         * name and then rename it over the top, or remove the old
         * link first. The former is what 'ln -sf' does, on the
         * grounds that it's more atomic. But I think in this case,
         * where the expected use case is that the previous agent has
         * long since shut down, atomicity isn't a critical concern
         * compared to not accidentally overwriting some non-symlink
         * that might have important data in it!
         */
        struct stat st;
        if (lstat(symlink_path, &st) == 0 && S_ISLNK(st.st_mode))
            unlink(symlink_path);
        if (symlink(socketname, symlink_path) < 0)
            fprintf(stderr, "pageant: making symlink %s: %s\n",
                    symlink_path, strerror(errno));
    }

    conf = conf_new();
    conf_set_int(conf, CONF_proxy_type, PROXY_NONE);

    /*
     * Lifetime preparations.
     */
    if (life == LIFE_X11) {
        struct X11Display *disp;
        void *greeting;
        int greetinglen;
        Socket *s;
        struct X11Connection *conn;
        char *x11_setup_err;

        if (!display) {
            fprintf(stderr, "pageant: no DISPLAY for -X mode\n");
            exit(1);
        }
        disp = x11_setup_display(display, conf, &x11_setup_err);
        if (!disp) {
            fprintf(stderr, "pageant: unable to connect to X server: %s\n",
                    x11_setup_err);
            sfree(x11_setup_err);
            exit(1);
        }

        conn = snew(struct X11Connection);
        conn->plug.vt = &X11Connection_plugvt;
        s = new_connection(sk_addr_dup(disp->addr),
                           disp->realhost, disp->port,
                           false, true, false, false, &conn->plug, conf,
                           NULL);
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

        pageant_fork_and_print_env(false);
    } else if (life == LIFE_TTY) {
        schedule_timer(TTY_LIFE_POLL_INTERVAL,
                       tty_life_timer, &dummy_timer_ctx);
        pageant_fork_and_print_env(true);
    } else if (life == LIFE_PERM) {
        pageant_fork_and_print_env(false);
    } else if (life == LIFE_DEBUG) {
        pageant_print_env(getpid());
        upc->logfp = stdout;

        struct termios orig_termios;
        upc->passphrase_fd = fileno(stdin);
        if (tcgetattr(upc->passphrase_fd, &orig_termios) == 0) {
            struct termios new_termios = orig_termios;
            new_termios.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL | ICANON);

            /*
             * Try to set up a watchdog process that will restore
             * termios if we crash or are killed. If successful, turn
             * off echo, for runtime passphrase prompts.
             */
            int pipefd[2];
            if (pipe(pipefd) == 0) {
                pid_t pid = fork();
                if (pid == 0) {
                    tcsetattr(upc->passphrase_fd, TCSADRAIN, &new_termios);
                    close(pipefd[1]);
                    char buf[4096];
                    while (read(pipefd[0], buf, sizeof(buf)) > 0);
                    tcsetattr(upc->passphrase_fd, TCSADRAIN, &new_termios);
                    _exit(0);
                } else if (pid > 0) {
                    upc->prompt_type = RTPROMPT_DEBUG;
                }

                close(pipefd[0]);
                if (pid < 0)
                    close(pipefd[1]);
            }
        }
    } else if (life == LIFE_EXEC) {
        pid_t agentpid, pid;

        agentpid = getpid();
        setup_sigchld_handler();

        pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        } else if (pid == 0) {
            setenv("SSH_AUTH_SOCK", socketname, true);
            setenv("SSH_AGENT_PID", dupprintf("%d", (int)agentpid), true);
            execvp(exec_args[0], exec_args);
            perror("exec");
            _exit(127);
        } else {
            upc->termination_pid = pid;
        }
    }

    if (!upc->logfp)
        upc->plc.suppress_logging = true;

    cli_main_loop(agent_loop_pw_setup, agent_loop_pw_check,
                  agent_loop_continue, upc);

    /*
     * Before terminating, clean up our Unix socket file if possible.
     */
    if (unlink(socketname) < 0) {
        fprintf(stderr, "pageant: %s: %s\n", socketname, strerror(errno));
        exit(1);
    }

    strbuf_free(upc->prompt_buf);
    conf_free(conf);
}

int main(int argc, char **argv)
{
    bool doing_opts = true;
    keyact curr_keyact = KEYACT_AGENT_LOAD;
    const char *standalone_askpass_prompt = NULL;
    const char *symlink_path = NULL;
    FILE *logfp = NULL;

    progname = argv[0];

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
                logfp = stderr;
            } else if (!strcmp(p, "-a")) {
                curr_keyact = KEYACT_CLIENT_ADD;
            } else if (!strcmp(p, "-d")) {
                curr_keyact = KEYACT_CLIENT_DEL;
            } else if (!strcmp(p, "-r")) {
                curr_keyact = KEYACT_CLIENT_REENCRYPT;
            } else if (!strcmp(p, "-s")) {
                shell_type = SHELL_SH;
            } else if (!strcmp(p, "-c")) {
                shell_type = SHELL_CSH;
            } else if (!strcmp(p, "-D")) {
                add_keyact(KEYACT_CLIENT_DEL_ALL, NULL);
            } else if (!strcmp(p, "-R")) {
                add_keyact(KEYACT_CLIENT_REENCRYPT_ALL, NULL);
            } else if (!strcmp(p, "-l")) {
                add_keyact(KEYACT_CLIENT_LIST, NULL);
            } else if (!strcmp(p, "--public")) {
                curr_keyact = KEYACT_CLIENT_PUBLIC;
            } else if (!strcmp(p, "--public-openssh") || !strcmp(p, "-L")) {
                curr_keyact = KEYACT_CLIENT_PUBLIC_OPENSSH;
            } else if (!strcmp(p, "-X")) {
                life = LIFE_X11;
            } else if (!strcmp(p, "-T")) {
                life = LIFE_TTY;
            } else if (!strcmp(p, "--no-decrypt") ||
                       !strcmp(p, "-no-decrypt") ||
                       !strcmp(p, "--no_decrypt") ||
                       !strcmp(p, "-no_decrypt") ||
                       !strcmp(p, "--nodecrypt") ||
                       !strcmp(p, "-nodecrypt") ||
                       !strcmp(p, "--encrypted") ||
                       !strcmp(p, "-encrypted")) {
                if (curr_keyact == KEYACT_AGENT_LOAD)
                    curr_keyact = KEYACT_AGENT_LOAD_ENCRYPTED;
                else if (curr_keyact == KEYACT_CLIENT_ADD)
                    curr_keyact = KEYACT_CLIENT_ADD_ENCRYPTED;
                else {
                    fprintf(stderr, "pageant: unexpected -E while not adding "
                            "keys\n");
                    exit(1);
                }
            } else if (!strcmp(p, "--debug")) {
                life = LIFE_DEBUG;
            } else if (!strcmp(p, "--test-sign")) {
                curr_keyact = KEYACT_CLIENT_SIGN;
                sign_flags = 0;
            } else if (strstartswith(p, "--test-sign-with-flags=")) {
                curr_keyact = KEYACT_CLIENT_SIGN;
                sign_flags = atoi(p + strlen("--test-sign-with-flags="));
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
            } else if (!strcmp(p, "--tty-prompt")) {
                prompt_type = PROMPT_TTY;
            } else if (!strcmp(p, "--gui-prompt")) {
                prompt_type = PROMPT_GUI;
            } else if (!strcmp(p, "--askpass")) {
                if (--argc > 0) {
                    standalone_askpass_prompt = *++argv;
                } else {
                    fprintf(stderr, "pageant: expected a prompt message "
                            "after --askpass\n");
                    exit(1);
                }
            } else if (!strcmp(p, "--symlink")) {
                if (--argc > 0) {
                    symlink_path = *++argv;
                } else {
                    fprintf(stderr, "pageant: expected a pathname "
                            "after --symlink\n");
                    exit(1);
                }
            } else if (!strcmp(p, "-E") || !strcmp(p, "--fptype")) {
                const char *keyword;
                if (--argc > 0) {
                    keyword = *++argv;
                } else {
                    fprintf(stderr, "pageant: expected a type string "
                            "after %s\n", p);
                    exit(1);
                }
                if (!strcmp(keyword, "md5"))
                    key_list_fptype = SSH_FPTYPE_MD5;
                else if (!strcmp(keyword, "sha256"))
                    key_list_fptype = SSH_FPTYPE_SHA256;
                else if (!strcmp(keyword, "md5-cert"))
                    key_list_fptype = SSH_FPTYPE_MD5_CERT;
                else if (!strcmp(keyword, "sha256-cert"))
                    key_list_fptype = SSH_FPTYPE_SHA256_CERT;
                else {
                    fprintf(stderr, "pageant: unknown fingerprint type `%s'\n",
                            keyword);
                    exit(1);
                }
            } else if (!strcmp(p, "--")) {
                doing_opts = false;
            } else {
                fprintf(stderr, "pageant: unrecognised option '%s'\n", p);
                exit(1);
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

    if (!display) {
        display = getenv("DISPLAY");
        if (display && !*display)
            display = NULL;
    }

    /*
     * Deal with standalone-askpass mode.
     */
    if (standalone_askpass_prompt) {
        char *passphrase = askpass(standalone_askpass_prompt);

        if (!passphrase)
            return 1;

        puts(passphrase);
        fflush(stdout);

        smemclr(passphrase, strlen(passphrase));
        sfree(passphrase);
        return 0;
    }

    /*
     * Block SIGPIPE, so that we'll get EPIPE individually on
     * particular network connections that go wrong.
     */
    putty_signal(SIGPIPE, SIG_IGN);

    sk_init();
    uxsel_init();

    /*
     * Now distinguish our two main running modes. Either we're
     * actually starting up an agent, in which case we should have a
     * lifetime mode, and no key actions of KEYACT_CLIENT_* type; or
     * else we're contacting an existing agent to add or remove keys,
     * in which case we should have no lifetime mode, and no key
     * actions of KEYACT_AGENT_* type.
     */
    {
        bool has_agent_actions = false;
        bool has_client_actions = false;
        bool has_lifetime = false;
        const struct cmdline_key_action *act;

        for (act = keyact_head; act; act = act->next) {
            if (is_agent_action(act->action))
                has_agent_actions = true;
            else
                has_client_actions = true;
        }
        if (life != LIFE_UNSPEC)
            has_lifetime = true;

        if (has_lifetime && has_client_actions) {
            fprintf(stderr, "pageant: client key actions (-a, -d, -D, -r, -R, "
                    "-l, -L) do not go with an agent lifetime option\n");
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
            run_agent(logfp, symlink_path);
        } else if (has_client_actions) {
            run_client();
        }
    }

    return 0;
}
