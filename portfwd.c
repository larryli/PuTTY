#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "putty.h"
#include "ssh.h"

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#define GET_32BIT_LSB_FIRST(cp) \
  (((unsigned long)(unsigned char)(cp)[0]) | \
  ((unsigned long)(unsigned char)(cp)[1] << 8) | \
  ((unsigned long)(unsigned char)(cp)[2] << 16) | \
  ((unsigned long)(unsigned char)(cp)[3] << 24))

#define PUT_32BIT_LSB_FIRST(cp, value) ( \
  (cp)[0] = (value), \
  (cp)[1] = (value) >> 8, \
  (cp)[2] = (value) >> 16, \
  (cp)[3] = (value) >> 24 )

#define GET_16BIT_LSB_FIRST(cp) \
  (((unsigned long)(unsigned char)(cp)[0]) | \
  ((unsigned long)(unsigned char)(cp)[1] << 8))

#define PUT_16BIT_LSB_FIRST(cp, value) ( \
  (cp)[0] = (value), \
  (cp)[1] = (value) >> 8 )

#define GET_32BIT_MSB_FIRST(cp) \
  (((unsigned long)(unsigned char)(cp)[0] << 24) | \
  ((unsigned long)(unsigned char)(cp)[1] << 16) | \
  ((unsigned long)(unsigned char)(cp)[2] << 8) | \
  ((unsigned long)(unsigned char)(cp)[3]))

#define PUT_32BIT_MSB_FIRST(cp, value) ( \
  (cp)[0] = (value) >> 24, \
  (cp)[1] = (value) >> 16, \
  (cp)[2] = (value) >> 8, \
  (cp)[3] = (value) )

#define GET_16BIT_MSB_FIRST(cp) \
  (((unsigned long)(unsigned char)(cp)[0] << 8) | \
  ((unsigned long)(unsigned char)(cp)[1]))

#define PUT_16BIT_MSB_FIRST(cp, value) ( \
  (cp)[0] = (value) >> 8, \
  (cp)[1] = (value) )

extern void sshfwd_close(void *);
extern void sshfwd_write(void *, char *, int);

struct pfwd_queue {
    struct pfwd_queue *next;
    char *buf;
};

struct PFwdPrivate {
    struct plug_function_table *fn;
    /* the above variable absolutely *must* be the first in this structure */
    void *c;			       /* (channel) data used by ssh.c */
    Socket s;
    char hostname[128];
    int port;
    int ready;
    struct pfwd_queue *waiting;
};

void pfd_close(Socket s);


static int pfd_closing(Plug plug, char *error_msg, int error_code,
		       int calling_back)
{
    struct PFwdPrivate *pr = (struct PFwdPrivate *) plug;

    /*
     * We have no way to communicate down the forwarded connection,
     * so if an error occurred on the socket, we just ignore it
     * and treat it like a proper close.
     */
    sshfwd_close(pr->c);
    pfd_close(pr->s);
    return 1;
}

static int pfd_receive(Plug plug, int urgent, char *data, int len)
{
    struct PFwdPrivate *pr = (struct PFwdPrivate *) plug;

    if (pr->ready)
	sshfwd_write(pr->c, data, len);
    return 1;
}

/*
 * Called when receiving a PORT OPEN from the server
 */
char *pfd_newconnect(Socket *s, char *hostname, int port, void *c)
{
    static struct plug_function_table fn_table = {
	pfd_closing,
	pfd_receive,
	NULL
    };

    SockAddr addr;
    char *err, *dummy_realhost;
    struct PFwdPrivate *pr;

    /*
     * Try to find host.
     */
    addr = sk_namelookup(hostname, &dummy_realhost);
    if ((err = sk_addr_error(addr)))
	return err;

    /*
     * Open socket.
     */
    pr = (struct PFwdPrivate *) smalloc(sizeof(struct PFwdPrivate));
    pr->fn = &fn_table;
    pr->ready = 1;
    pr->c = c;

    pr->s = *s = sk_new(addr, port, 0, 1, (Plug) pr);
    if ((err = sk_socket_error(*s))) {
	sfree(pr);
	return err;
    }

    sk_set_private_ptr(*s, pr);
    sk_addr_free(addr);
    return NULL;
}

/*
 called when someone connects to the local port
 */

static int pfd_accepting(Plug p, struct sockaddr *addr, void *sock)
{
    /* for now always accept this socket */
    static struct plug_function_table fn_table = {
	pfd_closing,
	pfd_receive,
	NULL
    };
    struct PFwdPrivate *pr, *org;
    struct sockaddr_in *sin = (struct sockaddr_in *)addr;
    Socket s;
    char *err;

    if (ntohl(sin->sin_addr.s_addr) != 0x7F000001 && !cfg.lport_acceptall)
	return 1; /* denied */

    org = (struct PFwdPrivate *)p;
    pr = (struct PFwdPrivate *) smalloc(sizeof(struct PFwdPrivate));
    pr->fn = &fn_table;

    pr->c = NULL;

    pr->s = s = sk_register(sock, (Plug) pr);
    if ((err = sk_socket_error(s))) {
	sfree(pr);
	return err != NULL;
    }

    pr->c = new_sock_channel(s);

    strcpy(pr->hostname, org->hostname);
    pr->port = org->port;
    pr->ready = 0;
    pr->waiting = NULL;

    sk_set_private_ptr(s, pr);

    if (pr->c == NULL) {
	sfree(pr);
	return 1;
    } else {
	/* asks to forward to the specified host/port for this */
	ssh_send_port_open(pr->c, pr->hostname, pr->port, "forwarding");
    }

    return 0;
}


/* Add a new forwarding from port -> desthost:destport
 sets up a listenner on the local machine on port
 */
char *pfd_addforward(char *desthost, int destport, int port)
{
    static struct plug_function_table fn_table = {
	pfd_closing,
	pfd_receive, /* should not happen... */
	pfd_accepting
    };

    char *err;
    struct PFwdPrivate *pr;
    Socket s;

    /*
     * Open socket.
     */
    pr = (struct PFwdPrivate *) smalloc(sizeof(struct PFwdPrivate));
    pr->fn = &fn_table;
    pr->c = NULL;
    strcpy(pr->hostname, desthost);
    pr->port = destport;
    pr->ready = 0;
    pr->waiting = NULL;

    pr->s = s = sk_newlistenner(port, (Plug) pr);
    if ((err = sk_socket_error(s))) {
	sfree(pr);
	return err;
    }

    sk_set_private_ptr(s, pr);

    return NULL;
}

void pfd_close(Socket s)
{
    struct PFwdPrivate *pr;

    if (!s)
	return;

    pr = (struct PFwdPrivate *) sk_get_private_ptr(s);

    sfree(pr);

    sk_close(s);
}

/*
 * Called to send data down the raw connection.
 */
void pfd_send(Socket s, char *data, int len)
{
    struct PFwdPrivate *pr = (struct PFwdPrivate *) sk_get_private_ptr(s);

    if (s == NULL)
	return;

    sk_write(s, data, len);
}


void pfd_confirm(Socket s)
{
    struct PFwdPrivate *pr = (struct PFwdPrivate *) sk_get_private_ptr(s);

    if (s == NULL)
	return;

    pr->ready = 1;
    sk_set_frozen(s, 0);
    sk_write(s, NULL, 0);
}
