#include <stdio.h>
#include <stdlib.h>

#include "putty.h"

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

static void pty_size(void);

static void c_write(char *buf, int len)
{
    from_backend(0, buf, len);
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
    /* FIXME: do nothing for now */
    return NULL;
}

/*
 * Called to send data down the pty.
 */
static int pty_send(char *buf, int len)
{
    c_write(buf, len);		       /* FIXME: diagnostic thingy */
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
    /* FIXME: will need to do TIOCSWINSZ or whatever. */
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
