/*
 * Pseudo-tty backend for pterm.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>
#include <fcntl.h>
#include <termios.h>
#include <grp.h>
#if HAVE_UTMP_H
#include <utmp.h>
#endif
#include <pwd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <termios.h>

#include "putty.h"
#include "ssh.h"
#include "ssh/server.h" /* to check the prototypes of server-needed things */
#include "tree234.h"

#ifndef OMIT_UTMP
#include <utmpx.h>
#endif

/* updwtmpx() needs the name of the wtmp file.  Try to find it. */
#ifndef WTMPX_FILE
#ifdef _PATH_WTMPX
#define WTMPX_FILE _PATH_WTMPX
#else
#define WTMPX_FILE "/var/log/wtmpx"
#endif
#endif

#ifndef LASTLOG_FILE
#ifdef _PATH_LASTLOG
#define LASTLOG_FILE _PATH_LASTLOG
#else
#define LASTLOG_FILE "/var/log/lastlog"
#endif
#endif

typedef struct Pty Pty;

/*
 * The pty_signal_pipe, along with the SIGCHLD handler, must be
 * process-global rather than session-specific.
 */
static int pty_signal_pipe[2] = { -1, -1 };   /* obviously bogus initial val */

typedef struct PtyFd {
    int fd;
    Pty *pty;
} PtyFd;

struct Pty {
    Conf *conf;

    int master_fd, slave_fd;
    int pipefds[6];
    PtyFd fds[3];
    int master_i, master_o, master_e;

    Seat *seat;
    size_t output_backlog;
    char name[FILENAME_MAX];
    pid_t child_pid;
    int term_width, term_height;
    bool child_dead, finished;
    int exit_code;
    bufchain output_data;
    bool pending_eof;
    Backend backend;
};

#define PTY_MAX_BACKLOG 32768

/*
 * We store all the (active) PtyFd structures in a tree sorted by fd,
 * so that when we get an uxsel notification we know which backend
 * instance is the owner of the pty that caused it, and then we can
 * find out which fd is the relevant one too.
 */
static int ptyfd_compare(void *av, void *bv)
{
    PtyFd *a = (PtyFd *)av;
    PtyFd *b = (PtyFd *)bv;

    if (a->fd < b->fd)
        return -1;
    else if (a->fd > b->fd)
        return +1;
    return 0;
}

static int ptyfd_find(void *av, void *bv)
{
    int a = *(int *)av;
    PtyFd *b = (PtyFd *)bv;

    if (a < b->fd)
        return -1;
    else if (a > b->fd)
        return +1;
    return 0;
}

static tree234 *ptyfds = NULL;

/*
 * We also have a tree of Pty structures themselves, sorted by child
 * pid, so that when we wait() in response to the signal we know which
 * backend instance is the owner of the process that caused the
 * signal.
 */
static int pty_compare_by_pid(void *av, void *bv)
{
    Pty *a = (Pty *)av;
    Pty *b = (Pty *)bv;

    if (a->child_pid < b->child_pid)
        return -1;
    else if (a->child_pid > b->child_pid)
        return +1;
    return 0;
}

static int pty_find_by_pid(void *av, void *bv)
{
    pid_t a = *(pid_t *)av;
    Pty *b = (Pty *)bv;

    if (a < b->child_pid)
        return -1;
    else if (a > b->child_pid)
        return +1;
    return 0;
}

static tree234 *ptys_by_pid = NULL;

/*
 * If we are using pty_pre_init(), it will need to have already
 * allocated a pty structure, which we must then return from
 * pty_init() rather than allocating a new one. Here we store that
 * structure between allocation and use.
 *
 * Note that although most of this module is entirely capable of
 * handling multiple ptys in a single process, pty_pre_init() is
 * fundamentally _dependent_ on there being at most one pty per
 * process, so the normal static-data constraints don't apply.
 *
 * Likewise, since utmp is only used via pty_pre_init, it too must
 * be single-instance, so we can declare utmp-related variables
 * here.
 */
static Pty *single_pty = NULL;

#ifndef OMIT_UTMP
static pid_t pty_utmp_helper_pid = -1;
static int pty_utmp_helper_pipe = -1;
static bool pty_stamped_utmp;
static struct utmpx utmp_entry;
#endif

/*
 * pty_argv is a grievous hack to allow a proper argv to be passed
 * through from the Unix command line. Again, it doesn't really
 * make sense outside a one-pty-per-process setup.
 */
char **pty_argv;

char *pty_osx_envrestore_prefix;

static void pty_close(Pty *pty);
static void pty_try_write(Pty *pty);

#ifndef OMIT_UTMP
static void setup_utmp(char *ttyname, char *location)
{
#if HAVE_LASTLOG
    struct lastlog lastlog_entry;
    FILE *lastlog;
#endif
    struct passwd *pw;
    struct timeval tv;

    pw = getpwuid(getuid());
    if (!pw)
        return; /* can't stamp utmp if we don't have a username */
    memset(&utmp_entry, 0, sizeof(utmp_entry));
    utmp_entry.ut_type = USER_PROCESS;
    utmp_entry.ut_pid = getpid();
#if __GNUC__ >= 8
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wstringop-truncation"
#endif // __GNUC__ >= 8
    strncpy(utmp_entry.ut_line, ttyname+5, lenof(utmp_entry.ut_line));
    strncpy(utmp_entry.ut_id, ttyname+8, lenof(utmp_entry.ut_id));
    strncpy(utmp_entry.ut_user, pw->pw_name, lenof(utmp_entry.ut_user));
    strncpy(utmp_entry.ut_host, location, lenof(utmp_entry.ut_host));
#if __GNUC__ >= 8
#   pragma GCC diagnostic pop
#endif // __GNUC__ >= 8
    /*
     * Apparently there are some architectures where (struct
     * utmpx).ut_tv is not essentially struct timeval (e.g. Linux
     * amd64). Hence the temporary.
     */
    gettimeofday(&tv, NULL);
    utmp_entry.ut_tv.tv_sec = tv.tv_sec;
    utmp_entry.ut_tv.tv_usec = tv.tv_usec;

    setutxent();
    pututxline(&utmp_entry);
    endutxent();

#if HAVE_UPDWTMPX
    updwtmpx(WTMPX_FILE, &utmp_entry);
#endif

#if HAVE_LASTLOG
    memset(&lastlog_entry, 0, sizeof(lastlog_entry));
    strncpy(lastlog_entry.ll_line, ttyname+5, lenof(lastlog_entry.ll_line));
    strncpy(lastlog_entry.ll_host, location, lenof(lastlog_entry.ll_host));
    time(&lastlog_entry.ll_time);
    if ((lastlog = fopen(LASTLOG_FILE, "r+")) != NULL) {
        fseek(lastlog, sizeof(lastlog_entry) * getuid(), SEEK_SET);
        fwrite(&lastlog_entry, 1, sizeof(lastlog_entry), lastlog);
        fclose(lastlog);
    }
#endif

    pty_stamped_utmp = true;

}

static void cleanup_utmp(void)
{
    struct timeval tv;

    if (!pty_stamped_utmp)
        return;

    utmp_entry.ut_type = DEAD_PROCESS;
    memset(utmp_entry.ut_user, 0, lenof(utmp_entry.ut_user));
    gettimeofday(&tv, NULL);
    utmp_entry.ut_tv.tv_sec = tv.tv_sec;
    utmp_entry.ut_tv.tv_usec = tv.tv_usec;

#if HAVE_UPDWTMPX
    updwtmpx(WTMPX_FILE, &utmp_entry);
#endif

    memset(utmp_entry.ut_line, 0, lenof(utmp_entry.ut_line));
    utmp_entry.ut_tv.tv_sec = 0;
    utmp_entry.ut_tv.tv_usec = 0;

    setutxent();
    pututxline(&utmp_entry);
    endutxent();

    pty_stamped_utmp = false;     /* ensure we never double-cleanup */
}
#endif

static void sigchld_handler(int signum)
{
    if (write(pty_signal_pipe[1], "x", 1) <= 0)
        /* not much we can do about it */;
}

static void pty_setup_sigchld_handler(void)
{
    static bool setup = false;
    if (!setup) {
        putty_signal(SIGCHLD, sigchld_handler);
        setup = true;
    }
}

#ifndef OMIT_UTMP
static void fatal_sig_handler(int signum)
{
    putty_signal(signum, SIG_DFL);
    cleanup_utmp();
    raise(signum);
}
#endif

static int pty_open_slave(Pty *pty)
{
    if (pty->slave_fd < 0) {
        pty->slave_fd = open(pty->name, O_RDWR);
        cloexec(pty->slave_fd);
    }

    return pty->slave_fd;
}

static void pty_open_master(Pty *pty)
{
#ifdef BSD_PTYS
    const char chars1[] = "pqrstuvwxyz";
    const char chars2[] = "0123456789abcdef";
    const char *p1, *p2;
    char master_name[20];
    struct group *gp;

    for (p1 = chars1; *p1; p1++)
        for (p2 = chars2; *p2; p2++) {
            sprintf(master_name, "/dev/pty%c%c", *p1, *p2);
            pty->master_fd = open(master_name, O_RDWR);
            if (pty->master_fd >= 0) {
                if (geteuid() == 0 ||
                    access(master_name, R_OK | W_OK) == 0) {
                    /*
                     * We must also check at this point that we are
                     * able to open the slave side of the pty. We
                     * wouldn't want to allocate the wrong master,
                     * get all the way down to forking, and _then_
                     * find we're unable to open the slave.
                     */
                    strcpy(pty->name, master_name);
                    pty->name[5] = 't'; /* /dev/ptyXX -> /dev/ttyXX */

                    cloexec(pty->master_fd);

                    if (pty_open_slave(pty) >= 0 &&
                        access(pty->name, R_OK | W_OK) == 0)
                        goto got_one;
                    if (pty->slave_fd > 0)
                        close(pty->slave_fd);
                    pty->slave_fd = -1;
                }
                close(pty->master_fd);
            }
        }

    /* If we get here, we couldn't get a tty at all. */
    fprintf(stderr, "pterm: unable to open a pseudo-terminal device\n");
    exit(1);

    got_one:

    /* We need to chown/chmod the /dev/ttyXX device. */
    gp = getgrnam("tty");
    chown(pty->name, getuid(), gp ? gp->gr_gid : -1);
    chmod(pty->name, 0600);
#else

    const int flags = O_RDWR
#ifdef O_NOCTTY
        | O_NOCTTY
#endif
        ;

#if HAVE_POSIX_OPENPT
#ifdef SET_NONBLOCK_VIA_OPENPT
    /*
     * OS X, as of 10.10 at least, doesn't permit me to set O_NONBLOCK
     * on pty master fds via the usual fcntl mechanism. Fortunately,
     * it does let me work around this by adding O_NONBLOCK to the
     * posix_openpt flags parameter, which isn't a documented use of
     * the API but seems to work. So we'll do that for now.
     */
    pty->master_fd = posix_openpt(flags | O_NONBLOCK);
#else
    pty->master_fd = posix_openpt(flags);
#endif

    if (pty->master_fd < 0) {
        perror("posix_openpt");
        exit(1);
    }
#else
    pty->master_fd = open("/dev/ptmx", flags);

    if (pty->master_fd < 0) {
        perror("/dev/ptmx: open");
        exit(1);
    }
#endif

    if (grantpt(pty->master_fd) < 0) {
        perror("grantpt");
        exit(1);
    }

    if (unlockpt(pty->master_fd) < 0) {
        perror("unlockpt");
        exit(1);
    }

    cloexec(pty->master_fd);

    pty->name[FILENAME_MAX-1] = '\0';
    strncpy(pty->name, ptsname(pty->master_fd), FILENAME_MAX-1);
#endif

#ifndef SET_NONBLOCK_VIA_OPENPT
    nonblock(pty->master_fd);
#endif
}

static Pty *new_pty_struct(void)
{
    Pty *pty = snew(Pty);
    memset(pty, 0, sizeof(Pty));
    pty->conf = NULL;
    pty->pending_eof = false;
    bufchain_init(&pty->output_data);
    return pty;
}

/*
 * Pre-initialisation. This is here to get around the fact that GTK
 * doesn't like being run in setuid/setgid programs (probably
 * sensibly). So before we initialise GTK - and therefore before we
 * even process the command line - we check to see if we're running
 * set[ug]id. If so, we open our pty master _now_, chown it as
 * necessary, and drop privileges. We can always close it again
 * later. If we're potentially going to be doing utmp as well, we
 * also fork off a utmp helper process and communicate with it by
 * means of a pipe; the utmp helper will keep privileges in order
 * to clean up utmp when we exit (i.e. when its end of our pipe
 * closes).
 */
void pty_pre_init(void)
{
#ifndef NO_PTY_PRE_INIT

    Pty *pty;

#ifndef OMIT_UTMP
    pid_t pid;
    int pipefd[2];
#endif

    pty = single_pty = new_pty_struct();

    /* set the child signal handler straight away; it needs to be set
     * before we ever fork. */
    pty_setup_sigchld_handler();
    pty->master_fd = pty->slave_fd = -1;
#ifndef OMIT_UTMP
    pty_stamped_utmp = false;
#endif

    if (geteuid() != getuid() || getegid() != getgid()) {
        pty_open_master(pty);

#ifndef OMIT_UTMP
        /*
         * Fork off the utmp helper.
         */
        if (pipe(pipefd) < 0) {
            perror("pterm: pipe");
            exit(1);
        }
        cloexec(pipefd[0]);
        cloexec(pipefd[1]);
        pid = fork();
        if (pid < 0) {
            perror("pterm: fork");
            exit(1);
        } else if (pid == 0) {
            char display[128], buffer[128];
            int dlen, ret;

            close(pipefd[1]);
            /*
             * Now sit here until we receive a display name from the
             * other end of the pipe, and then stamp utmp. Unstamp utmp
             * again, and exit, when the pipe closes.
             */

            dlen = 0;
            while (1) {

                ret = read(pipefd[0], buffer, lenof(buffer));
                if (ret <= 0) {
                    cleanup_utmp();
                    _exit(0);
                } else if (!pty_stamped_utmp) {
                    if (dlen < lenof(display))
                        memcpy(display+dlen, buffer,
                               min(ret, lenof(display)-dlen));
                    if (buffer[ret-1] == '\0') {
                        /*
                         * Now we have a display name. NUL-terminate
                         * it, and stamp utmp.
                         */
                        display[lenof(display)-1] = '\0';
                        /*
                         * Trap as many fatal signals as we can in the
                         * hope of having the best possible chance to
                         * clean up utmp before termination. We are
                         * unfortunately unprotected against SIGKILL,
                         * but that's life.
                         */
                        putty_signal(SIGHUP, fatal_sig_handler);
                        putty_signal(SIGINT, fatal_sig_handler);
                        putty_signal(SIGQUIT, fatal_sig_handler);
                        putty_signal(SIGILL, fatal_sig_handler);
                        putty_signal(SIGABRT, fatal_sig_handler);
                        putty_signal(SIGFPE, fatal_sig_handler);
                        putty_signal(SIGPIPE, fatal_sig_handler);
                        putty_signal(SIGALRM, fatal_sig_handler);
                        putty_signal(SIGTERM, fatal_sig_handler);
                        putty_signal(SIGSEGV, fatal_sig_handler);
                        putty_signal(SIGUSR1, fatal_sig_handler);
                        putty_signal(SIGUSR2, fatal_sig_handler);
#ifdef SIGBUS
                        putty_signal(SIGBUS, fatal_sig_handler);
#endif
#ifdef SIGPOLL
                        putty_signal(SIGPOLL, fatal_sig_handler);
#endif
#ifdef SIGPROF
                        putty_signal(SIGPROF, fatal_sig_handler);
#endif
#ifdef SIGSYS
                        putty_signal(SIGSYS, fatal_sig_handler);
#endif
#ifdef SIGTRAP
                        putty_signal(SIGTRAP, fatal_sig_handler);
#endif
#ifdef SIGVTALRM
                        putty_signal(SIGVTALRM, fatal_sig_handler);
#endif
#ifdef SIGXCPU
                        putty_signal(SIGXCPU, fatal_sig_handler);
#endif
#ifdef SIGXFSZ
                        putty_signal(SIGXFSZ, fatal_sig_handler);
#endif
#ifdef SIGIO
                        putty_signal(SIGIO, fatal_sig_handler);
#endif
                        setup_utmp(pty->name, display);
                    }
                }
            }
        } else {
            close(pipefd[0]);
            pty_utmp_helper_pid = pid;
            pty_utmp_helper_pipe = pipefd[1];
        }
#endif
    }

    /* Drop privs. */
    {
#if HAVE_SETRESUID && HAVE_SETRESGID
        int gid = getgid(), uid = getuid();
        int setresgid(gid_t, gid_t, gid_t);
        int setresuid(uid_t, uid_t, uid_t);
        if (setresgid(gid, gid, gid) < 0) {
            perror("setresgid");
            exit(1);
        }
        if (setresuid(uid, uid, uid) < 0) {
            perror("setresuid");
            exit(1);
        }
#else
        if (setgid(getgid()) < 0) {
            perror("setgid");
            exit(1);
        }
        if (setuid(getuid()) < 0) {
            perror("setuid");
            exit(1);
        }
#endif
    }

#endif /* NO_PTY_PRE_INIT */

}

static void pty_try_wait(void);
static void pty_uxsel_setup(Pty *pty);

static void pty_real_select_result(Pty *pty, int fd, int event, int status)
{
    char buf[4096];
    int ret;
    bool finished = false;

    if (event < 0) {
        /*
         * We've been called because our child process did
         * something. `status' tells us what.
         */
        if ((WIFEXITED(status) || WIFSIGNALED(status))) {
            /*
             * The primary child process died.
             */
            pty->child_dead = true;
            del234(ptys_by_pid, pty);
            pty->exit_code = status;

            /*
             * If this is an ordinary pty session, this is also the
             * moment to terminate the whole backend.
             *
             * We _could_ instead keep the terminal open for remaining
             * subprocesses to output to, but conventional wisdom
             * seems to feel that that's the Wrong Thing for an
             * xterm-alike, so we bail out now (though we don't
             * necessarily _close_ the window, depending on the state
             * of Close On Exit). This would be easy enough to change
             * or make configurable if necessary.
             */
            if (pty->master_fd >= 0)
                finished = true;
        }
    } else {
        if (event == SELECT_R) {
            bool is_stdout = (fd == pty->master_o);

            ret = read(fd, buf, sizeof(buf));

            /*
             * Treat EIO on a pty master as equivalent to EOF (because
             * that's how the kernel seems to report the event where
             * the last process connected to the other end of the pty
             * went away).
             */
            if (fd == pty->master_fd && ret < 0 && errno == EIO)
                ret = 0;

            if (ret == 0) {
                /*
                 * EOF on this input fd, so to begin with, we may as
                 * well close it, and remove all references to it in
                 * the pty's fd fields.
                 */
                uxsel_del(fd);
                close(fd);
                if (pty->master_fd == fd)
                    pty->master_fd = -1;
                if (pty->master_o == fd)
                    pty->master_o = -1;
                if (pty->master_e == fd)
                    pty->master_e = -1;

                if (is_stdout) {
                    /*
                     * We assume a clean exit if the pty (or stdout
                     * pipe) has closed, but the actual child process
                     * hasn't. The only way I can imagine this
                     * happening is if it detaches itself from the pty
                     * and goes daemonic - in which case the expected
                     * usage model would precisely _not_ be for the
                     * pterm window to hang around!
                     */
                    finished = true;
                    pty_try_wait(); /* one last effort to collect exit code */
                    if (!pty->child_dead)
                        pty->exit_code = 0;
                }
            } else if (ret < 0) {
                perror("read pty master");
                exit(1);
            } else if (ret > 0) {
                pty->output_backlog = seat_output(
                    pty->seat, !is_stdout, buf, ret);
                pty_uxsel_setup(pty);
            }
        } else if (event == SELECT_W) {
            /*
             * Attempt to send data down the pty.
             */
            pty_try_write(pty);
        }
    }

    if (finished && !pty->finished) {
        int close_on_exit;
        int i;

        for (i = 0; i < 3; i++)
            if (pty->fds[i].fd >= 0)
                uxsel_del(pty->fds[i].fd);

        pty_close(pty);

        pty->finished = true;

        /*
         * This is a slight layering-violation sort of hack: only
         * if we're not closing on exit (COE is set to Never, or to
         * Only On Clean and it wasn't a clean exit) do we output a
         * `terminated' message.
         */
        close_on_exit = conf_get_int(pty->conf, CONF_close_on_exit);
        if (close_on_exit == FORCE_OFF ||
            (close_on_exit == AUTO && pty->exit_code != 0)) {
            char *message;
            if (WIFEXITED(pty->exit_code)) {
                message = dupprintf(
                    "\r\n[pterm: process terminated with exit code %d]\r\n",
                    WEXITSTATUS(pty->exit_code));
            } else if (WIFSIGNALED(pty->exit_code)) {
#if !HAVE_STRSIGNAL
                message = dupprintf(
                    "\r\n[pterm: process terminated on signal %d]\r\n",
                    WTERMSIG(pty->exit_code));
#else
                message = dupprintf(
                    "\r\n[pterm: process terminated on signal %d (%s)]\r\n",
                    WTERMSIG(pty->exit_code),
                    strsignal(WTERMSIG(pty->exit_code)));
#endif
            } else {
                /* _Shouldn't_ happen, but if it does, a vague message
                 * is better than no message at all */
                message = dupprintf("\r\n[pterm: process terminated]\r\n");
            }
            seat_stdout_pl(pty->seat, ptrlen_from_asciz(message));
            sfree(message);
        }

        seat_eof(pty->seat);
        seat_notify_remote_exit(pty->seat);
    }
}

static void pty_try_wait(void)
{
    Pty *pty;
    pid_t pid;
    int status;

    do {
        pid = waitpid(-1, &status, WNOHANG);

        pty = find234(ptys_by_pid, &pid, pty_find_by_pid);

        if (pty)
            pty_real_select_result(pty, -1, -1, status);
    } while (pid > 0);
}

void pty_select_result(int fd, int event)
{
    if (fd == pty_signal_pipe[0]) {
        char c[1];

        if (read(pty_signal_pipe[0], c, 1) <= 0)
            /* ignore error */;
        /* ignore its value; it'll be `x' */

        pty_try_wait();
    } else {
        PtyFd *ptyfd = find234(ptyfds, &fd, ptyfd_find);

        if (ptyfd)
            pty_real_select_result(ptyfd->pty, fd, event, 0);
    }
}

static void pty_uxsel_setup_fd(Pty *pty, int fd)
{
    int rwx = 0;

    if (fd < 0)
        return;

    /* read from standard output and standard error pipes, assuming
     * we're not too backlogged */
    if ((pty->master_o == fd || pty->master_e == fd) &&
        pty->output_backlog < PTY_MAX_BACKLOG)
        rwx |= SELECT_R;
    /* write to standard input pipe if we have any data */
    if (pty->master_i == fd && bufchain_size(&pty->output_data))
        rwx |= SELECT_W;

    uxsel_set(fd, rwx, pty_select_result);
}

static void pty_uxsel_setup(Pty *pty)
{
    /*
     * We potentially have three separate fds here, but on the other
     * hand, some of them might be the same (if they're a pty master).
     * So we can't just call uxsel_set(master_o, SELECT_R) and then
     * uxsel_set(master_i, SELECT_W), without the latter potentially
     * undoing the work of the former if master_o == master_i.
     *
     * Instead, here we call a single uxsel on each one of these fds
     * (if it exists at all), and for each one, check it against all
     * three to see which bits to set.
     */
    pty_uxsel_setup_fd(pty, pty->master_o);
    pty_uxsel_setup_fd(pty, pty->master_e);
    pty_uxsel_setup_fd(pty, pty->master_i);

    /*
     * In principle this only needs calling once for all pty
     * backend instances, but it's simplest just to call it every
     * time; uxsel won't mind.
     */
    uxsel_set(pty_signal_pipe[0], SELECT_R, pty_select_result);
}

static void copy_ttymodes_into_termios(
    struct termios *attrs, struct ssh_ttymodes modes)
{
#define TTYMODE_CHAR(name, ssh_opcode, cc_index) {                      \
        if (modes.have_mode[ssh_opcode]) {                              \
            unsigned value = modes.mode_val[ssh_opcode];                \
            /* normalise wire value of 255 to local _POSIX_VDISABLE */  \
            attrs->c_cc[cc_index] = (value == 255 ?                     \
                                     _POSIX_VDISABLE : value);          \
        }                                                               \
    }

#define TTYMODE_FLAG(flagval, ssh_opcode, field, flagmask) {    \
        if (modes.have_mode[ssh_opcode]) {                      \
            attrs->c_##field##flag &= ~flagmask;                \
            if (modes.mode_val[ssh_opcode])                     \
                attrs->c_##field##flag |= flagval;              \
        }                                                       \
    }

#define TTYMODES_LOCAL_ONLY   /* omit any that this platform doesn't know */
#include "ssh/ttymode-list.h"

#undef TTYMODES_LOCAL_ONLY
#undef TTYMODE_CHAR
#undef TTYMODE_FLAG

    if (modes.have_mode[TTYMODE_ISPEED])
        cfsetispeed(attrs, modes.mode_val[TTYMODE_ISPEED]);
    if (modes.have_mode[TTYMODE_OSPEED])
        cfsetospeed(attrs, modes.mode_val[TTYMODE_OSPEED]);
}

/*
 * The main setup function for the pty back end. This doesn't match
 * the signature of backend_init(), partly because it has to be able
 * to take extra arguments such as an argv array, and also because
 * once we're changing the type signature _anyway_ we can discard the
 * stuff that's not really applicable to this backend like host names
 * and port numbers.
 */
Backend *pty_backend_create(
    Seat *seat, LogContext *logctx, Conf *conf, char **argv, const char *cmd,
    struct ssh_ttymodes ttymodes, bool pipes_instead, const char *dir,
    const char *const *env_vars_to_unset)
{
    int slavefd;
    pid_t pid, pgrp;
#ifndef NOT_X_WINDOWS                  /* for Mac OS X native compilation */
    bool got_windowid;
    long windowid;
#endif
    Pty *pty;
    int i;

    /* No local authentication phase in this protocol */
    seat_set_trust_status(seat, false);

    if (single_pty) {
        pty = single_pty;
        assert(pty->conf == NULL);
    } else {
        pty = new_pty_struct();
        pty->master_fd = pty->slave_fd = -1;
#ifndef OMIT_UTMP
        pty_stamped_utmp = false;
#endif
    }
    for (i = 0; i < 6; i++)
        pty->pipefds[i] = -1;
    for (i = 0; i < 3; i++) {
        pty->fds[i].fd = -1;
        pty->fds[i].pty = pty;
    }

    if (pty_signal_pipe[0] < 0) {
        if (pipe(pty_signal_pipe) < 0) {
            perror("pipe");
            exit(1);
        }
        cloexec(pty_signal_pipe[0]);
        cloexec(pty_signal_pipe[1]);
    }

    pty->seat = seat;
    pty->backend.vt = &pty_backend;

    pty->conf = conf_copy(conf);
    pty->term_width = conf_get_int(conf, CONF_width);
    pty->term_height = conf_get_int(conf, CONF_height);

    if (!ptyfds)
        ptyfds = newtree234(ptyfd_compare);

    if (pipes_instead) {
        if (pty->master_fd >= 0) {
            /* If somehow we've got a pty master already and don't
             * need it, throw it away! */
            close(pty->master_fd);
#ifndef OMIT_UTMP
            if (pty_utmp_helper_pipe >= 0) {
                close(pty_utmp_helper_pipe); /* don't need this either */
                pty_utmp_helper_pipe = -1;
            }
#endif
        }


        for (i = 0; i < 6; i += 2) {
            if (pipe(pty->pipefds + i) < 0) {
                backend_free(&pty->backend);
                return NULL;
            }
        }

        pty->fds[0].fd = pty->master_i = pty->pipefds[1];
        pty->fds[1].fd = pty->master_o = pty->pipefds[2];
        pty->fds[2].fd = pty->master_e = pty->pipefds[4];

        add234(ptyfds, &pty->fds[0]);
        add234(ptyfds, &pty->fds[1]);
        add234(ptyfds, &pty->fds[2]);
    } else {
        if (pty->master_fd < 0)
            pty_open_master(pty);

#ifndef OMIT_UTMP
        /*
         * Stamp utmp (that is, tell the utmp helper process to do so),
         * or not.
         */
        if (pty_utmp_helper_pipe >= 0) {   /* if it's < 0, we can't anyway */
            if (!conf_get_bool(conf, CONF_stamp_utmp)) {
                /* We're not stamping utmp, so just let the child
                 * process die that was waiting to unstamp it later. */
                close(pty_utmp_helper_pipe);
                pty_utmp_helper_pipe = -1;
            } else {
                const char *location = seat_get_x_display(pty->seat);
                int len = strlen(location)+1, pos = 0; /* +1 to include NUL */
                while (pos < len) {
                    int ret = write(pty_utmp_helper_pipe,
                                    location + pos, len - pos);
                    if (ret < 0) {
                        perror("pterm: writing to utmp helper process");
                        close(pty_utmp_helper_pipe); /* arrgh, just give up */
                        pty_utmp_helper_pipe = -1;
                        break;
                    }
                    pos += ret;
                }
            }
        }
#endif

        pty->master_i = pty->master_fd;
        pty->master_o = pty->master_fd;
        pty->master_e = -1;

        pty->fds[0].fd = pty->master_fd;
        add234(ptyfds, &pty->fds[0]);
    }

#ifndef NOT_X_WINDOWS                  /* for Mac OS X native compilation */
    got_windowid = seat_get_windowid(pty->seat, &windowid);
#endif

    /*
     * Set up the signal handler to catch SIGCHLD, if pty_pre_init
     * didn't already do it.
     */
    pty_setup_sigchld_handler();

    /*
     * Fork and execute the command.
     */
    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }

    if (pid == 0) {
        struct termios attrs;

        /*
         * We are the child.
         */

        if (pty_osx_envrestore_prefix) {
            int plen = strlen(pty_osx_envrestore_prefix);
            extern char **environ;
            char **ep;

          restart_osx_env_restore:
            for (ep = environ; *ep; ep++) {
                char *e = *ep;

                if (!strncmp(e, pty_osx_envrestore_prefix, plen)) {
                    bool unset = (e[plen] == 'u');
                    char *pname = dupprintf("%.*s", (int)strcspn(e, "="), e);
                    char *name = pname + plen + 1;
                    char *value = e + strcspn(e, "=");
                    if (*value) value++;
                    value = dupstr(value);
                    if (unset)
                        unsetenv(name);
                    else
                        setenv(name, value, 1);
                    unsetenv(pname);
                    sfree(pname);
                    sfree(value);
                    goto restart_osx_env_restore;
                }
            }
        }

        pgrp = getpid();

        if (pipes_instead) {
            int i;
            dup2(pty->pipefds[0], 0);
            dup2(pty->pipefds[3], 1);
            dup2(pty->pipefds[5], 2);
            for (i = 0; i < 6; i++)
                close(pty->pipefds[i]);

            setsid();
        } else {
            slavefd = pty_open_slave(pty);
            if (slavefd < 0) {
                perror("slave pty: open");
                _exit(1);
            }

            close(pty->master_fd);
            noncloexec(slavefd);
            dup2(slavefd, 0);
            dup2(slavefd, 1);
            dup2(slavefd, 2);
            close(slavefd);
            setsid();
#ifdef TIOCSCTTY
            ioctl(0, TIOCSCTTY, 1);
#endif
            tcsetpgrp(0, pgrp);

            /*
             * Set up configuration-dependent termios settings on the new
             * pty. Linux would have let us do this on the pty master
             * before we forked, but that fails on OS X, so we do it here
             * instead.
             */
            if (tcgetattr(0, &attrs) == 0) {
                /*
                 * Set the backspace character to be whichever of ^H and
                 * ^? is specified by bksp_is_delete.
                 */
                attrs.c_cc[VERASE] = conf_get_bool(conf, CONF_bksp_is_delete)
                    ? '\177' : '\010';

                /*
                 * Set the IUTF8 bit iff the character set is UTF-8.
                 */
#ifdef IUTF8
                if (seat_is_utf8(seat))
                    attrs.c_iflag |= IUTF8;
                else
                    attrs.c_iflag &= ~IUTF8;
#endif

                copy_ttymodes_into_termios(&attrs, ttymodes);

                tcsetattr(0, TCSANOW, &attrs);
            }
        }

        setpgid(pgrp, pgrp);
        if (!pipes_instead) {
            int ptyfd = open(pty->name, O_WRONLY, 0);
            if (ptyfd >= 0)
                close(ptyfd);
        }
        setpgid(pgrp, pgrp);

        if (env_vars_to_unset)
            for (const char *const *p = env_vars_to_unset; *p; p++)
                unsetenv(*p);

        if (!pipes_instead) {
            char *term_env_var = dupprintf("TERM=%s",
                                           conf_get_str(conf, CONF_termtype));
            putenv(term_env_var);
            /* We mustn't free term_env_var, as putenv links it into the
             * environment in place.
             */
        }
#ifndef NOT_X_WINDOWS                  /* for Mac OS X native compilation */
        if (got_windowid) {
            char *windowid_env_var = dupprintf("WINDOWID=%ld", windowid);
            putenv(windowid_env_var);
            /* We mustn't free windowid_env_var, as putenv links it into the
             * environment in place.
             */
        }
        {
            /*
             * In case we were invoked with a --display argument that
             * doesn't match DISPLAY in our actual environment, we
             * should set DISPLAY for processes running inside the
             * terminal to match the display the terminal itself is
             * on.
             */
            const char *x_display = seat_get_x_display(pty->seat);
            if (x_display) {
                char *x_display_env_var = dupprintf("DISPLAY=%s", x_display);
                putenv(x_display_env_var);
                /* As above, we don't free this. */
            } else {
                unsetenv("DISPLAY");
            }
        }
#endif
        {
            char *key, *val;

            for (val = conf_get_str_strs(conf, CONF_environmt, NULL, &key);
                 val != NULL;
                 val = conf_get_str_strs(conf, CONF_environmt, key, &key)) {
                char *varval = dupcat(key, "=", val);
                putenv(varval);
                /*
                 * We must not free varval, since putenv links it
                 * into the environment _in place_. Weird, but
                 * there we go. Memory usage will be rationalised
                 * as soon as we exec anyway.
                 */
            }
        }

        if (dir) {
            if (chdir(dir) < 0) {
                /* Ignore the error - nothing we can sensibly do about it,
                 * and our existing cwd is as good a fallback as any. */
            }
        }

        /*
         * SIGINT, SIGQUIT and SIGPIPE may have been set to ignored by
         * our parent, particularly by things like sh -c 'pterm &' and
         * some window or session managers. SIGPIPE was also
         * (potentially) blocked by us during startup. Reverse all
         * this for our child process.
         */
        putty_signal(SIGINT, SIG_DFL);
        putty_signal(SIGQUIT, SIG_DFL);
        putty_signal(SIGPIPE, SIG_DFL);
        block_signal(SIGPIPE, false);
        if (argv || cmd) {
            /*
             * If we were given a separated argument list, try to exec
             * it.
             */
            if (argv) {
                execvp(argv[0], argv);
            }
            /*
             * Otherwise, if we were given a single command string,
             * try passing that to $SHELL -c.
             *
             * In the case of pterm, this system of fallbacks arranges
             * that we can _either_ follow 'pterm -e' with a list of
             * argv elements to be fed directly to exec, _or_ with a
             * single argument containing a command to be parsed by a
             * shell (but, in cases of doubt, the former is more
             * reliable). We arrange this by setting argv to the full
             * argument list, and also setting cmd to the single
             * element of argv if it's a length-1 list.
             *
             * A quick survey of other terminal emulators' -e options
             * (as of Debian squeeze) suggests that:
             *
             *  - xterm supports both modes, more or less like this
             *  - gnome-terminal will only accept a one-string shell command
             *  - Eterm, kterm and rxvt will only accept a list of
             *    argv elements (as did older versions of pterm).
             *
             * It therefore seems important to support both usage
             * modes in order to be a drop-in replacement for either
             * xterm or gnome-terminal, and hence for anyone's
             * plausible uses of the Debian-style alias
             * 'x-terminal-emulator'.
             *
             * In other use cases, a caller can set only one of argv
             * and cmd to get a fixed handling of the input.
             */
            if (cmd) {
                char *shell = getenv("SHELL");
                if (shell)
                    execl(shell, shell, "-c", cmd, (void *)NULL);
            }
        } else {
            const char *shell = getenv("SHELL");
            if (!shell)
                shell = "/bin/sh";
            char *shellname;
            if (conf_get_bool(conf, CONF_login_shell)) {
                const char *p = strrchr(shell, '/');
                shellname = snewn(2+strlen(shell), char);
                p = p ? p+1 : shell;
                sprintf(shellname, "-%s", p);
            } else
                shellname = (char *)shell;
            execl(shell, shellname, (void *)NULL);
        }

        /*
         * If we're here, exec has gone badly foom.
         */
        perror("exec");
        _exit(127);
    } else {
        pty->child_pid = pid;
        pty->child_dead = false;
        pty->finished = false;
        if (pty->slave_fd > 0)
            close(pty->slave_fd);
        if (!ptys_by_pid)
            ptys_by_pid = newtree234(pty_compare_by_pid);
        if (pty->pipefds[0] >= 0) {
            close(pty->pipefds[0]);
            pty->pipefds[0] = -1;
        }
        if (pty->pipefds[3] >= 0) {
            close(pty->pipefds[3]);
            pty->pipefds[3] = -1;
        }
        if (pty->pipefds[5] >= 0) {
            close(pty->pipefds[5]);
            pty->pipefds[5] = -1;
        }
        add234(ptys_by_pid, pty);
    }

    pty_uxsel_setup(pty);

    return &pty->backend;
}

/*
 * This is the pty backend's _official_ init method, for BackendVtable
 * purposes. Its job is just to be an API converter, ignoring the
 * irrelevant input parameters and making up auxiliary outputs. Also
 * it gets the argv array from the global variable pty_argv, expecting
 * that it will have been invoked by pterm.
 */
static char *pty_init(const BackendVtable *vt, Seat *seat,
                      Backend **backend_handle, LogContext *logctx,
                      Conf *conf, const char *host, int port,
                      char **realhost, bool nodelay, bool keepalive)
{
    const char *cmd = NULL;
    struct ssh_ttymodes modes;

    memset(&modes, 0, sizeof(modes));

    if (pty_argv && pty_argv[0] && !pty_argv[1])
        cmd = pty_argv[0];

    assert(vt == &pty_backend);
    *backend_handle = pty_backend_create(
        seat, logctx, conf, pty_argv, cmd, modes, false, NULL, NULL);
    *realhost = dupstr("");
    return NULL;
}

static void pty_reconfig(Backend *be, Conf *conf)
{
    Pty *pty = container_of(be, Pty, backend);
    /*
     * We don't have much need to reconfigure this backend, but
     * unfortunately we do need to pick up the setting of Close On
     * Exit so we know whether to give a `terminated' message.
     */
    conf_copy_into(pty->conf, conf);
}

/*
 * Stub routine (never called in pterm).
 */
static void pty_free(Backend *be)
{
    Pty *pty = container_of(be, Pty, backend);
    int i;

    pty_close(pty);

    /* Either of these may fail `not found'. That's fine with us. */
    del234(ptys_by_pid, pty);
    for (i = 0; i < 3; i++)
        if (pty->fds[i].fd >= 0)
            del234(ptyfds, &pty->fds[i]);

    bufchain_clear(&pty->output_data);

    conf_free(pty->conf);
    pty->conf = NULL;

    if (pty == single_pty) {
        /*
         * Leave this structure around in case we need to Restart
         * Session.
         */
    } else {
        sfree(pty);
    }
}

static void pty_try_write(Pty *pty)
{
    ssize_t ret;

    assert(pty->master_i >= 0);

    while (bufchain_size(&pty->output_data) > 0) {
        ptrlen data = bufchain_prefix(&pty->output_data);
        ret = write(pty->master_i, data.ptr, data.len);

        if (ret < 0 && (errno == EWOULDBLOCK)) {
            /*
             * We've sent all we can for the moment.
             */
            break;
        }
        if (ret < 0) {
            perror("write pty master");
            exit(1);
        }
        bufchain_consume(&pty->output_data, ret);
    }

    if (pty->pending_eof && bufchain_size(&pty->output_data) == 0) {
        /* This should only happen if pty->master_i is a pipe that
         * doesn't alias either output fd */
        assert(pty->master_i != pty->master_o);
        assert(pty->master_i != pty->master_e);
        uxsel_del(pty->master_i);
        close(pty->master_i);
        pty->master_i = -1;
        pty->pending_eof = false;
    }

    pty_uxsel_setup(pty);
}

/*
 * Called to send data down the pty.
 */
static void pty_send(Backend *be, const char *buf, size_t len)
{
    Pty *pty = container_of(be, Pty, backend);

    if (pty->master_i < 0 || pty->pending_eof)
        return;                   /* ignore all writes if fd closed */

    bufchain_add(&pty->output_data, buf, len);
    pty_try_write(pty);
}

static void pty_close(Pty *pty)
{
    int i;

    if (pty->master_o >= 0)
        uxsel_del(pty->master_o);
    if (pty->master_e >= 0)
        uxsel_del(pty->master_e);
    if (pty->master_i >= 0)
        uxsel_del(pty->master_i);

    if (pty->master_fd >= 0) {
        close(pty->master_fd);
        pty->master_fd = -1;
    }
    for (i = 0; i < 6; i++) {
        if (pty->pipefds[i] >= 0)
            close(pty->pipefds[i]);
        pty->pipefds[i] = -1;
    }
    pty->master_i = pty->master_o = pty->master_e = -1;
#ifndef OMIT_UTMP
    if (pty_utmp_helper_pipe >= 0) {
        close(pty_utmp_helper_pipe);   /* this causes utmp to be cleaned up */
        pty_utmp_helper_pipe = -1;
    }
#endif
}

/*
 * Called to query the current socket sendability status.
 */
static size_t pty_sendbuffer(Backend *be)
{
    Pty *pty = container_of(be, Pty, backend);
    return bufchain_size(&pty->output_data);
}

/*
 * Called to set the size of the window
 */
static void pty_size(Backend *be, int width, int height)
{
    Pty *pty = container_of(be, Pty, backend);
    struct winsize size;
    int xpixel = 0, ypixel = 0;

    pty->term_width = width;
    pty->term_height = height;

    if (pty->master_fd < 0)
        return;

    seat_get_window_pixel_size(pty->seat, &xpixel, &ypixel);

    size.ws_row = (unsigned short)pty->term_height;
    size.ws_col = (unsigned short)pty->term_width;
    size.ws_xpixel = (unsigned short)xpixel;
    size.ws_ypixel = (unsigned short)ypixel;
    ioctl(pty->master_fd, TIOCSWINSZ, (void *)&size);
    return;
}

/*
 * Send special codes.
 */
static void pty_special(Backend *be, SessionSpecialCode code, int arg)
{
    Pty *pty = container_of(be, Pty, backend);

    if (code == SS_BRK) {
        if (pty->master_fd >= 0)
            tcsendbreak(pty->master_fd, 0);
        return;
    }

    if (code == SS_EOF) {
        if (pty->master_i >= 0 && pty->master_i != pty->master_fd) {
            pty->pending_eof = true;
            pty_try_write(pty);
        }
        return;
    }

    {
        int sig = -1;

        #define SIGNAL_SUB(name) if (code == SS_SIG ## name) sig = SIG ## name;
        #define SIGNAL_MAIN(name, text) SIGNAL_SUB(name)
        #define SIGNALS_LOCAL_ONLY
        #include "ssh/signal-list.h"
        #undef SIGNAL_SUB
        #undef SIGNAL_MAIN
        #undef SIGNALS_LOCAL_ONLY

        if (sig != -1) {
            if (!pty->child_dead)
                kill(pty->child_pid, sig);
            return;
        }
    }

    return;
}

/*
 * Return a list of the special codes that make sense in this
 * protocol.
 */
static const SessionSpecial *pty_get_specials(Backend *be)
{
    /* Pty *pty = container_of(be, Pty, backend); */
    /*
     * Hmm. When I get round to having this actually usable, it
     * might be quite nice to have the ability to deliver a few
     * well chosen signals to the child process - SIGINT, SIGTERM,
     * SIGKILL at least.
     */
    return NULL;
}

static bool pty_connected(Backend *be)
{
    /* Pty *pty = container_of(be, Pty, backend); */
    return true;
}

static bool pty_sendok(Backend *be)
{
    /* Pty *pty = container_of(be, Pty, backend); */
    return true;
}

static void pty_unthrottle(Backend *be, size_t backlog)
{
    Pty *pty = container_of(be, Pty, backend);
    pty->output_backlog = backlog;
    pty_uxsel_setup(pty);
}

static bool pty_ldisc(Backend *be, int option)
{
    /* Pty *pty = container_of(be, Pty, backend); */
    return false;                    /* neither editing nor echoing */
}

static void pty_provide_ldisc(Backend *be, Ldisc *ldisc)
{
    /* Pty *pty = container_of(be, Pty, backend); */
    /* This is a stub. */
}

static int pty_exitcode(Backend *be)
{
    Pty *pty = container_of(be, Pty, backend);
    if (!pty->finished)
        return -1;                     /* not dead yet */
    else if (WIFSIGNALED(pty->exit_code))
        return 128 + WTERMSIG(pty->exit_code);
    else
        return WEXITSTATUS(pty->exit_code);
}

int pty_backend_exit_signum(Backend *be)
{
    Pty *pty = container_of(be, Pty, backend);

    if (!pty->finished || !WIFSIGNALED(pty->exit_code))
        return -1;

    return WTERMSIG(pty->exit_code);
}

ptrlen pty_backend_exit_signame(Backend *be, char **aux_msg)
{
    *aux_msg = NULL;

    int sig = pty_backend_exit_signum(be);
    if (sig < 0)
        return PTRLEN_LITERAL("");

    #define SIGNAL_SUB(s) {                             \
        if (sig == SIG ## s)                            \
            return PTRLEN_LITERAL(#s);                  \
    }
    #define SIGNAL_MAIN(s, desc) SIGNAL_SUB(s)
    #define SIGNALS_LOCAL_ONLY
    #include "ssh/signal-list.h"
    #undef SIGNAL_MAIN
    #undef SIGNAL_SUB
    #undef SIGNALS_LOCAL_ONLY

    *aux_msg = dupprintf("untranslatable signal number %d: %s",
                         sig, strsignal(sig));
    return PTRLEN_LITERAL("HUP");      /* need some kind of default */
}

static int pty_cfg_info(Backend *be)
{
    /* Pty *pty = container_of(be, Pty, backend); */
    return 0;
}

const BackendVtable pty_backend = {
    .init = pty_init,
    .free = pty_free,
    .reconfig = pty_reconfig,
    .send = pty_send,
    .sendbuffer = pty_sendbuffer,
    .size = pty_size,
    .special = pty_special,
    .get_specials = pty_get_specials,
    .connected = pty_connected,
    .exitcode = pty_exitcode,
    .sendok = pty_sendok,
    .ldisc_option_state = pty_ldisc,
    .provide_ldisc = pty_provide_ldisc,
    .unthrottle = pty_unthrottle,
    .cfg_info = pty_cfg_info,
    .id = "pty",
    .displayname_tc = "pty",
    .displayname_lc = "pty",
    .protocol = -1,
};
