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
static int pty_stamped_utmp = 0;
static int pty_child_pid;
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

static void setup_utmp(char *ttyname)
{
#ifndef OMIT_UTMP
#ifdef HAVE_LASTLOG
    struct lastlog lastlog_entry;
    FILE *lastlog;
#endif
    struct passwd *pw;
    char *location;
    FILE *wtmp;

    if (!cfg.stamp_utmp)
	return;

    pw = getpwuid(getuid());
    location = get_x_display();
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

    if (!cfg.stamp_utmp || !pty_stamped_utmp)
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

/*
 * Called to set up the pty.
 * 
 * Returns an error message, or NULL on success.
 *
 * Also places the canonical host name into `realhost'. It must be
 * freed by the caller.
 */
static char *pty_init(char *host, int port, char **realhost, int nodelay)
{
    int slavefd;
    char name[FILENAME_MAX];
    pid_t pid, pgrp;

#ifdef BSD_PTYS
    {
	const char chars1[] = "pqrstuvwxyz";
	const char chars2[] = "0123456789abcdef";
	const char *p1, *p2;
	char master_name[20];

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
	strcpy(name, master_name);
	name[5] = 't';		       /* /dev/ptyXX -> /dev/ttyXX */
    }
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

    name[FILENAME_MAX-1] = '\0';
    strncpy(name, ptsname(pty_master_fd), FILENAME_MAX-1);
#endif

    /*
     * Trap as many fatal signals as we can in the hope of having
     * the best chance to clean up utmp before termination.
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
    setup_utmp(name);

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

	slavefd = open(name, O_RDWR);
	if (slavefd < 0) {
	    perror("slave pty: open");
	    exit(1);
	}

#ifdef BSD_PTYS
	/* We need to chown/chmod the /dev/ttyXX device. */
	{
	    struct group *gp = getgrnam("tty");
	    fchown(slavefd, getuid(), gp ? gp->gr_gid : -1);
	    fchmod(slavefd, 0600);
	}
#endif

	close(pty_master_fd);
	close(0);
	close(1);
	close(2);
	fcntl(slavefd, F_SETFD, 0);    /* don't close on exec */
	dup2(slavefd, 0);
	dup2(slavefd, 1);
	dup2(slavefd, 2);
	setsid();
	ioctl(slavefd, TIOCSCTTY, 1);
	pgrp = getpid();
	tcsetpgrp(slavefd, pgrp);
	setpgrp();
	close(open(name, O_WRONLY, 0));
	setpgrp();
	/* In case we were setgid-utmp or setuid-root, drop privs. */
	setgid(getgid());
	setuid(getuid());
	/* Close everything _else_, for tidiness. */
	for (i = 3; i < 1024; i++)
	    close(i);
	{
	    char term_env_var[10 + sizeof(cfg.termtype)];
	    sprintf(term_env_var, "TERM=%s", cfg.termtype);
	    putenv(term_env_var);
	}
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

    size.ws_row = (unsigned short)rows;
    size.ws_col = (unsigned short)cols;
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
