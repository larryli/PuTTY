/*
 * Pseudo-tty backend for pterm.
 * 
 * Unlike the other backends, data for this one is not neatly
 * encapsulated into a data structure, because it wouldn't make
 * sense to do so - the utmp stuff has to be done before a backend
 * is initialised, and starting a second pterm from the same
 * process would therefore be infeasible because privileges would
 * already have been dropped. Hence, I haven't bothered to keep the
 * data dynamically allocated: instead, the backend handle is just
 * a null pointer and ignored everywhere.
 */

#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED
#define _GNU_SOURCE
#include <features.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <termios.h>
#include <grp.h>
#include <utmp.h>
#include <pwd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "putty.h"

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#ifndef UTMP_FILE
#define UTMP_FILE "/var/run/utmp"
#endif
#ifndef WTMP_FILE
#define WTMP_FILE "/var/log/wtmp"
#endif
#ifndef LASTLOG_FILE
#ifdef _PATH_LASTLOG
#define LASTLOG_FILE _PATH_LASTLOG
#else
#define LASTLOG_FILE "/var/log/lastlog"
#endif
#endif

/*
 * Set up a default for vaguely sane systems. The idea is that if
 * OMIT_UTMP is not defined, then at least one of the symbols which
 * enable particular forms of utmp processing should be, if only so
 * that a link error can warn you that you should have defined
 * OMIT_UTMP if you didn't want any. Currently HAVE_PUTUTLINE is
 * the only such symbol.
 */
#ifndef OMIT_UTMP
#if !defined HAVE_PUTUTLINE
#define HAVE_PUTUTLINE
#endif
#endif

static Config pty_cfg;
static int pty_master_fd;
static void *pty_frontend;
static char pty_name[FILENAME_MAX];
static int pty_signal_pipe[2];
static int pty_stamped_utmp = 0;
static int pty_child_pid;
static int pty_utmp_helper_pid, pty_utmp_helper_pipe;
static int pty_term_width, pty_term_height;
static int pty_child_dead, pty_finished;
static int pty_exit_code;
#ifndef OMIT_UTMP
static struct utmp utmp_entry;
#endif
char **pty_argv;
int use_pty_argv = TRUE;

static void pty_close(void);

static void setup_utmp(char *ttyname, char *location)
{
#ifndef OMIT_UTMP
#ifdef HAVE_LASTLOG
    struct lastlog lastlog_entry;
    FILE *lastlog;
#endif
    struct passwd *pw;
    FILE *wtmp;

    pw = getpwuid(getuid());
    memset(&utmp_entry, 0, sizeof(utmp_entry));
    utmp_entry.ut_type = USER_PROCESS;
    utmp_entry.ut_pid = getpid();
    strncpy(utmp_entry.ut_line, ttyname+5, lenof(utmp_entry.ut_line));
    strncpy(utmp_entry.ut_id, ttyname+8, lenof(utmp_entry.ut_id));
    strncpy(utmp_entry.ut_user, pw->pw_name, lenof(utmp_entry.ut_user));
    strncpy(utmp_entry.ut_host, location, lenof(utmp_entry.ut_host));
    time(&utmp_entry.ut_time);

#if defined HAVE_PUTUTLINE
    utmpname(UTMP_FILE);
    setutent();
    pututline(&utmp_entry);
    endutent();
#endif

    if ((wtmp = fopen(WTMP_FILE, "a")) != NULL) {
	fwrite(&utmp_entry, 1, sizeof(utmp_entry), wtmp);
	fclose(wtmp);
    }

#ifdef HAVE_LASTLOG
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

    pty_stamped_utmp = 1;

#endif
}

static void cleanup_utmp(void)
{
#ifndef OMIT_UTMP
    FILE *wtmp;

    if (!pty_stamped_utmp)
	return;

    utmp_entry.ut_type = DEAD_PROCESS;
    memset(utmp_entry.ut_user, 0, lenof(utmp_entry.ut_user));
    time(&utmp_entry.ut_time);

    if ((wtmp = fopen(WTMP_FILE, "a")) != NULL) {
	fwrite(&utmp_entry, 1, sizeof(utmp_entry), wtmp);
	fclose(wtmp);
    }

    memset(utmp_entry.ut_line, 0, lenof(utmp_entry.ut_line));
    utmp_entry.ut_time = 0;

#if defined HAVE_PUTUTLINE
    utmpname(UTMP_FILE);
    setutent();
    pututline(&utmp_entry);
    endutent();
#endif

    pty_stamped_utmp = 0;	       /* ensure we never double-cleanup */
#endif
}

static void sigchld_handler(int signum)
{
    write(pty_signal_pipe[1], "x", 1);
}

static void fatal_sig_handler(int signum)
{
    putty_signal(signum, SIG_DFL);
    cleanup_utmp();
    setuid(getuid());
    raise(signum);
}

static void pty_open_master(void)
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
	    pty_master_fd = open(master_name, O_RDWR);
	    if (pty_master_fd >= 0) {
		if (geteuid() == 0 ||
		    access(master_name, R_OK | W_OK) == 0)
		    goto got_one;
		close(pty_master_fd);
	    }
	}

    /* If we get here, we couldn't get a tty at all. */
    fprintf(stderr, "pterm: unable to open a pseudo-terminal device\n");
    exit(1);

    got_one:
    strcpy(pty_name, master_name);
    pty_name[5] = 't';		       /* /dev/ptyXX -> /dev/ttyXX */

    /* We need to chown/chmod the /dev/ttyXX device. */
    gp = getgrnam("tty");
    chown(pty_name, getuid(), gp ? gp->gr_gid : -1);
    chmod(pty_name, 0600);
#else
    pty_master_fd = open("/dev/ptmx", O_RDWR);

    if (pty_master_fd < 0) {
	perror("/dev/ptmx: open");
	exit(1);
    }

    if (grantpt(pty_master_fd) < 0) {
	perror("grantpt");
	exit(1);
    }
    
    if (unlockpt(pty_master_fd) < 0) {
	perror("unlockpt");
	exit(1);
    }

    pty_name[FILENAME_MAX-1] = '\0';
    strncpy(pty_name, ptsname(pty_master_fd), FILENAME_MAX-1);
#endif
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
    pid_t pid;
    int pipefd[2];

    /* set the child signal handler straight away; it needs to be set
     * before we ever fork. */
    putty_signal(SIGCHLD, sigchld_handler);
    pty_master_fd = -1;

    if (geteuid() != getuid() || getegid() != getgid()) {
	pty_open_master();
    }

#ifndef OMIT_UTMP
    /*
     * Fork off the utmp helper.
     */
    if (pipe(pipefd) < 0) {
	perror("pterm: pipe");
	exit(1);
    }
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
		    setup_utmp(pty_name, display);
		}
	    }
	}
    } else {
	close(pipefd[0]);
	pty_utmp_helper_pid = pid;
	pty_utmp_helper_pipe = pipefd[1];
    }
#endif

    /* Drop privs. */
    {
	int gid = getgid(), uid = getuid();
#ifndef HAVE_NO_SETRESUID
	int setresgid(gid_t, gid_t, gid_t);
	int setresuid(uid_t, uid_t, uid_t);
	setresgid(gid, gid, gid);
	setresuid(uid, uid, uid);
#else
	setgid(getgid());
	setuid(getuid());
#endif
    }
}

int pty_select_result(int fd, int event)
{
    char buf[4096];
    int ret;
    int finished = FALSE;

    if (fd == pty_master_fd && event == 1) {

	ret = read(pty_master_fd, buf, sizeof(buf));

	/*
	 * Clean termination condition is that either ret == 0, or ret
	 * < 0 and errno == EIO. Not sure why the latter, but it seems
	 * to happen. Boo.
	 */
	if (ret == 0 || (ret < 0 && errno == EIO)) {
	    /*
	     * We assume a clean exit if the pty has closed but the
	     * actual child process hasn't. The only way I can
	     * imagine this happening is if it detaches itself from
	     * the pty and goes daemonic - in which case the
	     * expected usage model would precisely _not_ be for
	     * the pterm window to hang around!
	     */
	    finished = TRUE;
	    if (!pty_child_dead)
		pty_exit_code = 0;
	} else if (ret < 0) {
	    perror("read pty master");
	    exit(1);
	} else if (ret > 0) {
	    from_backend(pty_frontend, 0, buf, ret);
	}
    } else if (fd == pty_signal_pipe[0]) {
	pid_t pid;
	int status;
	char c[1];

	read(pty_signal_pipe[0], c, 1); /* ignore its value; it'll be `x' */

	do {
	    pid = waitpid(-1, &status, WNOHANG);
	    if (pid == pty_child_pid &&
		(WIFEXITED(status) || WIFSIGNALED(status))) {
		/*
		 * The primary child process died. We could keep
		 * the terminal open for remaining subprocesses to
		 * output to, but conventional wisdom seems to feel
		 * that that's the Wrong Thing for an xterm-alike,
		 * so we bail out now (though we don't necessarily
		 * _close_ the window, depending on the state of
		 * Close On Exit). This would be easy enough to
		 * change or make configurable if necessary.
		 */
		pty_exit_code = status;
		pty_child_dead = TRUE;
		finished = TRUE;
	    }
	} while(pid > 0);
    }

    if (finished && !pty_finished) {
	uxsel_del(pty_master_fd);
	pty_close();
	pty_master_fd = -1;

	pty_finished = TRUE;

	/*
	 * This is a slight layering-violation sort of hack: only
	 * if we're not closing on exit (COE is set to Never, or to
	 * Only On Clean and it wasn't a clean exit) do we output a
	 * `terminated' message.
	 */
	if (pty_cfg.close_on_exit == FORCE_OFF ||
	    (pty_cfg.close_on_exit == AUTO && pty_exit_code != 0)) {
	    char message[512];
	    if (WIFEXITED(pty_exit_code))
		sprintf(message, "\r\n[pterm: process terminated with exit"
			" code %d]\r\n", WEXITSTATUS(pty_exit_code));
	    else if (WIFSIGNALED(pty_exit_code))
#ifdef HAVE_NO_STRSIGNAL
		sprintf(message, "\r\n[pterm: process terminated on signal"
			" %d]\r\n", WTERMSIG(pty_exit_code));
#else
		sprintf(message, "\r\n[pterm: process terminated on signal"
			" %d (%.400s)]\r\n", WTERMSIG(pty_exit_code),
			strsignal(WTERMSIG(pty_exit_code)));
#endif
	    from_backend(pty_frontend, 0, message, strlen(message));
	}
    }
    return !finished;
}

static void pty_uxsel_setup(void)
{
    uxsel_set(pty_master_fd, 1, pty_select_result);
    uxsel_set(pty_signal_pipe[0], 1, pty_select_result);
}

/*
 * Called to set up the pty.
 * 
 * Returns an error message, or NULL on success.
 *
 * Also places the canonical host name into `realhost'. It must be
 * freed by the caller.
 */
static const char *pty_init(void *frontend, void **backend_handle, Config *cfg,
			    char *host, int port, char **realhost, int nodelay)
{
    int slavefd;
    pid_t pid, pgrp;
    long windowid;

    pty_frontend = frontend;
    *backend_handle = NULL;	       /* we can't sensibly use this, sadly */

    pty_cfg = *cfg;		       /* structure copy */
    pty_term_width = cfg->width;
    pty_term_height = cfg->height;

    if (pty_master_fd < 0)
	pty_open_master();

    /*
     * Set the backspace character to be whichever of ^H and ^? is
     * specified by bksp_is_delete.
     */
    {
	struct termios attrs;
	tcgetattr(pty_master_fd, &attrs);
	attrs.c_cc[VERASE] = cfg->bksp_is_delete ? '\177' : '\010';
	tcsetattr(pty_master_fd, TCSANOW, &attrs);
    }

    /*
     * Stamp utmp (that is, tell the utmp helper process to do so),
     * or not.
     */
    if (!cfg->stamp_utmp)
	close(pty_utmp_helper_pipe);   /* just let the child process die */
    else {
	char *location = get_x_display(pty_frontend);
	int len = strlen(location)+1, pos = 0;   /* +1 to include NUL */
	while (pos < len) {
	    int ret = write(pty_utmp_helper_pipe, location+pos, len - pos);
	    if (ret < 0) {
		perror("pterm: writing to utmp helper process");
		close(pty_utmp_helper_pipe);   /* arrgh, just give up */
		break;
	    }
	    pos += ret;
	}
    }

    windowid = get_windowid(pty_frontend);

    /*
     * Fork and execute the command.
     */
    pid = fork();
    if (pid < 0) {
	perror("fork");
	exit(1);
    }

    if (pid == 0) {
	int i;
	/*
	 * We are the child.
	 */

	slavefd = open(pty_name, O_RDWR);
	if (slavefd < 0) {
	    perror("slave pty: open");
	    _exit(1);
	}

	close(pty_master_fd);
	fcntl(slavefd, F_SETFD, 0);    /* don't close on exec */
	dup2(slavefd, 0);
	dup2(slavefd, 1);
	dup2(slavefd, 2);
	setsid();
	ioctl(slavefd, TIOCSCTTY, 1);
	pgrp = getpid();
	tcsetpgrp(slavefd, pgrp);
	setpgrp();
	close(open(pty_name, O_WRONLY, 0));
	setpgrp();
	/* Close everything _else_, for tidiness. */
	for (i = 3; i < 1024; i++)
	    close(i);
	{
	    char term_env_var[10 + sizeof(cfg->termtype)];
	    sprintf(term_env_var, "TERM=%s", cfg->termtype);
	    putenv(term_env_var);
	}
	{
	    char windowid_env_var[40];
	    sprintf(windowid_env_var, "WINDOWID=%ld", windowid);
	    putenv(windowid_env_var);
	}
	/*
	 * SIGINT and SIGQUIT may have been set to ignored by our
	 * parent, particularly by things like sh -c 'pterm &' and
	 * some window managers. Reverse this for our child process.
	 */
	putty_signal(SIGINT, SIG_DFL);
	putty_signal(SIGQUIT, SIG_DFL);
	if (pty_argv)
	    execvp(pty_argv[0], pty_argv);
	else {
	    char *shell = getenv("SHELL");
	    char *shellname;
	    if (cfg->login_shell) {
		char *p = strrchr(shell, '/');
		shellname = snewn(2+strlen(shell), char);
		p = p ? p+1 : shell;
		sprintf(shellname, "-%s", p);
	    } else
		shellname = shell;
	    execl(getenv("SHELL"), shellname, NULL);
	}

	/*
	 * If we're here, exec has gone badly foom.
	 */
	perror("exec");
	_exit(127);
    } else {
	pty_child_pid = pid;
	pty_child_dead = FALSE;
	pty_finished = FALSE;
    }      

    if (pipe(pty_signal_pipe) < 0) {
	perror("pipe");
	exit(1);
    }
    pty_uxsel_setup();

    return NULL;
}

static void pty_reconfig(void *handle, Config *cfg)
{
    /*
     * We don't have much need to reconfigure this backend, but
     * unfortunately we do need to pick up the setting of Close On
     * Exit so we know whether to give a `terminated' message.
     */
    pty_cfg = *cfg;		       /* structure copy */
}

/*
 * Stub routine (never called in pterm).
 */
static void pty_free(void *handle)
{
}

/*
 * Called to send data down the pty.
 */
static int pty_send(void *handle, char *buf, int len)
{
    if (pty_master_fd < 0)
	return 0;		       /* ignore all writes if fd closed */

    while (len > 0) {
	int ret = write(pty_master_fd, buf, len);
	if (ret < 0) {
	    perror("write pty master");
	    exit(1);
	}
	buf += ret;
	len -= ret;
    }
    return 0;
}

static void pty_close(void)
{
    if (pty_master_fd >= 0) {
	close(pty_master_fd);
	pty_master_fd = -1;
    }
    close(pty_utmp_helper_pipe);       /* this causes utmp to be cleaned up */
}

/*
 * Called to query the current socket sendability status.
 */
static int pty_sendbuffer(void *handle)
{
    return 0;
}

/*
 * Called to set the size of the window
 */
static void pty_size(void *handle, int width, int height)
{
    struct winsize size;

    pty_term_width = width;
    pty_term_height = height;

    size.ws_row = (unsigned short)pty_term_height;
    size.ws_col = (unsigned short)pty_term_width;
    size.ws_xpixel = (unsigned short) pty_term_width *
	font_dimension(pty_frontend, 0);
    size.ws_ypixel = (unsigned short) pty_term_height *
	font_dimension(pty_frontend, 1);
    ioctl(pty_master_fd, TIOCSWINSZ, (void *)&size);
    return;
}

/*
 * Send special codes.
 */
static void pty_special(void *handle, Telnet_Special code)
{
    /* Do nothing! */
    return;
}

/*
 * Return a list of the special codes that make sense in this
 * protocol.
 */
static const struct telnet_special *pty_get_specials(void *handle)
{
    /*
     * Hmm. When I get round to having this actually usable, it
     * might be quite nice to have the ability to deliver a few
     * well chosen signals to the child process - SIGINT, SIGTERM,
     * SIGKILL at least.
     */
    return NULL;
}

static Socket pty_socket(void *handle)
{
    return NULL;		       /* shouldn't ever be needed */
}

static int pty_sendok(void *handle)
{
    return 1;
}

static void pty_unthrottle(void *handle, int backlog)
{
    /* do nothing */
}

static int pty_ldisc(void *handle, int option)
{
    return 0;			       /* neither editing nor echoing */
}

static void pty_provide_ldisc(void *handle, void *ldisc)
{
    /* This is a stub. */
}

static void pty_provide_logctx(void *handle, void *logctx)
{
    /* This is a stub. */
}

static int pty_exitcode(void *handle)
{
    if (!pty_finished)
	return -1;		       /* not dead yet */
    else
	return pty_exit_code;
}

Backend pty_backend = {
    pty_init,
    pty_free,
    pty_reconfig,
    pty_send,
    pty_sendbuffer,
    pty_size,
    pty_special,
    pty_get_specials,
    pty_socket,
    pty_exitcode,
    pty_sendok,
    pty_ldisc,
    pty_provide_ldisc,
    pty_provide_logctx,
    pty_unthrottle,
    1
};
