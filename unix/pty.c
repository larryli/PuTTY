#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED
#include <features.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>

#include "putty.h"

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

int pty_master_fd;
char **pty_argv;

static void pty_size(void);

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
	else
	    execl(getenv("SHELL"), getenv("SHELL"), NULL);
	/*
	 * If we're here, exec has gone badly foom.
	 */
	perror("exec");
	exit(127);
    } else {
	close(slavefd);
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
