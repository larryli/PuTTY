#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED
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

#include "putty.h"
#include "terminal.h"

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

int pty_master_fd;
static char pty_name[FILENAME_MAX];
static int pty_stamped_utmp = 0;
static int pty_child_pid;
static int pty_utmp_helper_pid, pty_utmp_helper_pipe;
static sig_atomic_t pty_child_dead;
#ifndef OMIT_UTMP
static struct utmp utmp_entry;
#endif
char **pty_argv;

int pty_child_is_dead(void)
{
    return pty_child_dead;
}

static void pty_size(void);

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
    pid_t pid;
    int status;
    pid = waitpid(-1, &status, WNOHANG);
    if (pid == pty_child_pid && (WIFEXITED(status) || WIFSIGNALED(status)))
	pty_child_dead = TRUE;	
}

static void fatal_sig_handler(int signum)
{
    signal(signum, SIG_DFL);
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
		exit(0);
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
		    signal(SIGHUP, fatal_sig_handler);
		    signal(SIGINT, fatal_sig_handler);
		    signal(SIGQUIT, fatal_sig_handler);
		    signal(SIGILL, fatal_sig_handler);
		    signal(SIGABRT, fatal_sig_handler);
		    signal(SIGFPE, fatal_sig_handler);
		    signal(SIGPIPE, fatal_sig_handler);
		    signal(SIGALRM, fatal_sig_handler);
		    signal(SIGTERM, fatal_sig_handler);
		    signal(SIGSEGV, fatal_sig_handler);
		    signal(SIGUSR1, fatal_sig_handler);
		    signal(SIGUSR2, fatal_sig_handler);
#ifdef SIGBUS
		    signal(SIGBUS, fatal_sig_handler);
#endif
#ifdef SIGPOLL
		    signal(SIGPOLL, fatal_sig_handler);
#endif
#ifdef SIGPROF
		    signal(SIGPROF, fatal_sig_handler);
#endif
#ifdef SIGSYS
		    signal(SIGSYS, fatal_sig_handler);
#endif
#ifdef SIGTRAP
		    signal(SIGTRAP, fatal_sig_handler);
#endif
#ifdef SIGVTALRM
		    signal(SIGVTALRM, fatal_sig_handler);
#endif
#ifdef SIGXCPU
		    signal(SIGXCPU, fatal_sig_handler);
#endif
#ifdef SIGXFSZ
		    signal(SIGXFSZ, fatal_sig_handler);
#endif
#ifdef SIGIO
		    signal(SIGIO, fatal_sig_handler);
#endif
		    /* Also clean up utmp on normal exit. */
		    atexit(cleanup_utmp);
		    setup_utmp(pty_name, display);
		}
	    }
	}
    } else {
	close(pipefd[0]);
	pty_utmp_helper_pid = pid;
	pty_utmp_helper_pipe = pipefd[1];
	signal(SIGCHLD, sigchld_handler);
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

/*
 * Called to set up the pty.
 * 
 * Returns an error message, or NULL on success.
 *
 * Also places the canonical host name into `realhost'. It must be
 * freed by the caller.
 */
static char *pty_init(void *frontend,
		      char *host, int port, char **realhost, int nodelay)
{
    int slavefd;
    pid_t pid, pgrp;

    if (pty_master_fd < 0)
	pty_open_master();

    /*
     * Set the backspace character to be whichever of ^H and ^? is
     * specified by bksp_is_delete.
     */
    {
	struct termios attrs;
	tcgetattr(pty_master_fd, &attrs);
	attrs.c_cc[VERASE] = cfg.bksp_is_delete ? '\177' : '\010';
	tcsetattr(pty_master_fd, TCSANOW, &attrs);
    }

    /*
     * Stamp utmp (that is, tell the utmp helper process to do so),
     * or not.
     */
    if (!cfg.stamp_utmp)
	close(pty_utmp_helper_pipe);   /* just let the child process die */
    else {
	char *location = get_x_display();
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
	    exit(1);
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
	    char term_env_var[10 + sizeof(cfg.termtype)];
	    sprintf(term_env_var, "TERM=%s", cfg.termtype);
	    putenv(term_env_var);
	}
	/*
	 * SIGINT and SIGQUIT may have been set to ignored by our
	 * parent, particularly by things like sh -c 'pterm &' and
	 * some window managers. Reverse this for our child process.
	 */
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	if (pty_argv)
	    execvp(pty_argv[0], pty_argv);
	else {
	    char *shell = getenv("SHELL");
	    char *shellname;
	    if (cfg.login_shell) {
		char *p = strrchr(shell, '/');
		shellname = smalloc(2+strlen(shell));
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
	exit(127);
    } else {
	close(slavefd);
	pty_child_pid = pid;
	pty_child_dead = FALSE;
	signal(SIGCHLD, sigchld_handler);
    }

    return NULL;
}

/*
 * Called to send data down the pty.
 */
static int pty_send(char *buf, int len)
{
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

/*
 * Called to query the current socket sendability status.
 */
static int pty_sendbuffer(void)
{
    return 0;
}

/*
 * Called to set the size of the window
 */
static void pty_size(void)
{
    struct winsize size;

    size.ws_row = (unsigned short)term->rows;
    size.ws_col = (unsigned short)term->cols;
    size.ws_xpixel = (unsigned short) term->cols * font_dimension(0);
    size.ws_ypixel = (unsigned short) term->rows * font_dimension(1);
    ioctl(pty_master_fd, TIOCSWINSZ, (void *)&size);
    return;
}

/*
 * Send special codes.
 */
static void pty_special(Telnet_Special code)
{
    /* Do nothing! */
    return;
}

static Socket pty_socket(void)
{
    return NULL;		       /* shouldn't ever be needed */
}

static int pty_sendok(void)
{
    return 1;
}

static void pty_unthrottle(int backlog)
{
    /* do nothing */
}

static int pty_ldisc(int option)
{
    return 0;			       /* neither editing nor echoing */
}

static int pty_exitcode(void)
{
    /* Shouldn't ever be required */
    return 0;
}

Backend pty_backend = {
    pty_init,
    pty_send,
    pty_sendbuffer,
    pty_size,
    pty_special,
    pty_socket,
    pty_exitcode,
    pty_sendok,
    pty_ldisc,
    pty_unthrottle,
    1
};
