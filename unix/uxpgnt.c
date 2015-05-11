/*
 * Unix Pageant, more or less similar to ssh-agent.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>

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

void fatalbox(char *p, ...)
{
    va_list ap;
    fprintf(stderr, "FATAL ERROR: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}
void modalfatalbox(char *p, ...)
{
    va_list ap;
    fprintf(stderr, "FATAL ERROR: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}
void nonfatal(char *p, ...)
{
    va_list ap;
    fprintf(stderr, "ERROR: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
}
void connection_fatal(void *frontend, char *p, ...)
{
    va_list ap;
    fprintf(stderr, "FATAL ERROR: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}
void cmdline_error(char *p, ...)
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
int uxsel_input_add(int fd, int rwx) { return 0; }
void uxsel_input_remove(int id) { }

/*
 * More stubs.
 */
void logevent(void *frontend, const char *string) {}
void random_save_seed(void) {}
void random_destroy_seed(void) {}
void noise_ultralight(unsigned long data) {}
char *platform_default_s(const char *name) { return NULL; }
int platform_default_i(const char *name, int def) { return def; }
FontSpec *platform_default_fontspec(const char *name) { return fontspec_new(""); }
Filename *platform_default_filename(const char *name) { return filename_from_str(""); }
char *x_get_default(const char *key) { return NULL; }
void old_keyfile_warning(void) {}
void timer_change_notify(unsigned long next) {}

/*
 * Short description of parameters.
 */
static void usage(void)
{
    printf("Pageant: SSH agent\n");
    printf("%s\n", ver);
    printf("FIXME\n");
    exit(1);
}

static void version(void)
{
    printf("pageant: %s\n", ver);
    exit(1);
}

void keylist_update(void)
{
    /* Nothing needs doing in Unix Pageant */
}

#define PAGEANT_DIR_PREFIX "/tmp/pageant"

const char *const appname = "Pageant";

char *platform_get_x_display(void) {
    return dupstr(getenv("DISPLAY"));
}

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
static int x11_receive(Plug plug, int urgent, char *data, int len) {return 0;}
static void x11_sent(Plug plug, int bufsize) {}
static int x11_closing(Plug plug, const char *error_msg, int error_code,
		       int calling_back)
{
    time_to_die = TRUE;
    return 1;
}
struct X11Connection {
    const struct plug_function_table *fn;
};

char *socketname;
void pageant_print_env(int pid)
{
    printf("SSH_AUTH_SOCK=%s; export SSH_AUTH_SOCK;\n"
           "SSH_AGENT_PID=%d; export SSH_AGENT_PID;\n",
           socketname, (int)pid);
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

int main(int argc, char **argv)
{
    int *fdlist;
    int fd;
    int i, fdcount, fdsize, fdstate;
    int errors;
    unsigned long now;
    char *username, *socketdir;
    const char *err;
    struct pageant_listen_state *pl;
    Socket sock;
    enum {
        LIFE_UNSPEC, LIFE_X11, LIFE_TTY, LIFE_DEBUG, LIFE_PERM, LIFE_EXEC
    } life = LIFE_UNSPEC;
    const char *display = NULL;
    int doing_opts = TRUE;
    char **exec_args = NULL;
    int termination_pid = -1;
    Conf *conf;

    fdlist = NULL;
    fdcount = fdsize = 0;
    errors = FALSE;

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
            fprintf(stderr, "pageant: unexpected argument '%s'\n", p);
            exit(1);
        }
    }

    if (errors)
	return 1;

    if (life == LIFE_UNSPEC) {
        fprintf(stderr, "pageant: expected a lifetime option\n");
        exit(1);
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
    pageant_init();
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

        if (!display)
            display = getenv("DISPLAY");
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
            int fd = open("/dev/tty", O_RDONLY);
            if (fd < 0) {
                if (errno != ENXIO) {
                    perror("/dev/tty: open");
                    exit(1);
                }
                time_to_die = TRUE;
                break;
            } else {
                close(fd);
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

    return 0;
}
