/*
 * Unix networking abstraction.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>

#define DEFINE_PLUG_METHOD_MACROS
#include "putty.h"
#include "network.h"
#include "tree234.h"

struct Socket_tag {
    struct socket_function_table *fn;
    /* the above variable absolutely *must* be the first in this structure */
    char *error;
    int s;
    Plug plug;
    void *private_ptr;
    bufchain output_data;
    int connected;
    int writable;
    int frozen; /* this causes readability notifications to be ignored */
    int frozen_readable; /* this means we missed at least one readability
			  * notification while we were frozen */
    int localhost_only;		       /* for listening sockets */
    char oobdata[1];
    int sending_oob;
    int oobpending;		       /* is there OOB data available to read? */
    int oobinline;
    int pending_error;		       /* in case send() returns error */
    int listener;
};

/*
 * We used to typedef struct Socket_tag *Socket.
 *
 * Since we have made the networking abstraction slightly more
 * abstract, Socket no longer means a tcp socket (it could mean
 * an ssl socket).  So now we must use Actual_Socket when we know
 * we are talking about a tcp socket.
 */
typedef struct Socket_tag *Actual_Socket;

struct SockAddr_tag {
    char *error;
    /* address family this belongs to, AF_INET for IPv4, AF_INET6 for IPv6. */
    int family;
    unsigned long address;	       /* Address IPv4 style. */
#ifdef IPV6
    struct addrinfo *ai;	       /* Address IPv6 style. */
#endif
};

static tree234 *sktree;

static int cmpfortree(void *av, void *bv)
{
    Actual_Socket a = (Actual_Socket) av, b = (Actual_Socket) bv;
    int as = a->s, bs = b->s;
    if (as < bs)
	return -1;
    if (as > bs)
	return +1;
    return 0;
}

static int cmpforsearch(void *av, void *bv)
{
    Actual_Socket b = (Actual_Socket) bv;
    int as = (int) av, bs = b->s;
    if (as < bs)
	return -1;
    if (as > bs)
	return +1;
    return 0;
}

void sk_init(void)
{
    sktree = newtree234(cmpfortree);
}

void sk_cleanup(void)
{
    Actual_Socket s;
    int i;

    if (sktree) {
	for (i = 0; (s = index234(sktree, i)) != NULL; i++) {
	    close(s->s);
	}
    }
}

char *error_string(int error)
{
    return strerror(error);
}

SockAddr sk_namelookup(char *host, char **canonicalname)
{
    SockAddr ret = smalloc(sizeof(struct SockAddr_tag));
    unsigned long a;
    struct hostent *h = NULL;
    char realhost[8192];

    /* Clear the structure and default to IPv4. */
    memset(ret, 0, sizeof(struct SockAddr_tag));
    ret->family = 0;		       /* We set this one when we have resolved the host. */
    *realhost = '\0';
    ret->error = NULL;

    if ((a = inet_addr(host)) == (unsigned long) INADDR_NONE) {
#ifdef IPV6
	if (getaddrinfo(host, NULL, NULL, &ret->ai) == 0) {
	    ret->family = ret->ai->ai_family;
	} else
#endif
	{
	    /*
	     * Otherwise use the IPv4-only gethostbyname... (NOTE:
	     * we don't use gethostbyname as a fallback!)
	     */
	    if (ret->family == 0) {
		/*debug(("Resolving \"%s\" with gethostbyname() (IPv4 only)...\n", host)); */
		if ( (h = gethostbyname(host)) )
		    ret->family = AF_INET;
	    }
	    if (ret->family == 0) {
		ret->error = (h_errno == HOST_NOT_FOUND ||
			      h_errno == NO_DATA ||
			      h_errno == NO_ADDRESS ? "Host does not exist" :
			      h_errno == TRY_AGAIN ?
			      "Temporary name service failure" :
			      "gethostbyname: unknown error");
		return ret;
	    }
	}

#ifdef IPV6
	/* If we got an address info use that... */
	if (ret->ai) {

	    /* Are we in IPv4 fallback mode? */
	    /* We put the IPv4 address into the a variable so we can further-on use the IPv4 code... */
	    if (ret->family == AF_INET)
		memcpy(&a,
		       (char *) &((struct sockaddr_in *) ret->ai->
				  ai_addr)->sin_addr, sizeof(a));

	    /* Now let's find that canonicalname... */
	    if (getnameinfo((struct sockaddr *) ret->ai->ai_addr,
			    ret->family ==
			    AF_INET ? sizeof(struct sockaddr_in) :
			    sizeof(struct sockaddr_in6), realhost,
			    sizeof(realhost), NULL, 0, 0) != 0) {
		strncpy(realhost, host, sizeof(realhost));
	    }
	}
	/* We used the IPv4-only gethostbyname()... */
	else
#endif
	{
	    memcpy(&a, h->h_addr, sizeof(a));
	    /* This way we are always sure the h->h_name is valid :) */
	    strncpy(realhost, h->h_name, sizeof(realhost));
	}
    } else {
	/*
	 * This must be a numeric IPv4 address because it caused a
	 * success return from inet_addr.
	 */
	ret->family = AF_INET;
	strncpy(realhost, host, sizeof(realhost));
    }
    ret->address = ntohl(a);
    realhost[lenof(realhost)-1] = '\0';
    *canonicalname = smalloc(1+strlen(realhost));
    strcpy(*canonicalname, realhost);
    return ret;
}

void sk_getaddr(SockAddr addr, char *buf, int buflen)
{
#ifdef IPV6
    if (addr->family == AF_INET) {
#endif
	struct in_addr a;
	a.s_addr = htonl(addr->address);
	strncpy(buf, inet_ntoa(a), buflen);
#ifdef IPV6
    } else {
	FIXME; /* I don't know how to get a text form of an IPv6 address. */
    }
#endif
}

int sk_addrtype(SockAddr addr)
{
    return (addr->family == AF_INET ? ADDRTYPE_IPV4 : ADDRTYPE_IPV6);
}

void sk_addrcopy(SockAddr addr, char *buf)
{
#ifdef IPV6
    if (addr->family == AF_INET) {
#endif
	struct in_addr a;
	a.s_addr = htonl(addr->address);
	memcpy(buf, (char*) &a.s_addr, 4);
#ifdef IPV6
    } else {
	memcpy(buf, (char*) addr->ai, 16);
    }
#endif
}

void sk_addr_free(SockAddr addr)
{
    sfree(addr);
}

static Plug sk_tcp_plug(Socket sock, Plug p)
{
    Actual_Socket s = (Actual_Socket) sock;
    Plug ret = s->plug;
    if (p)
	s->plug = p;
    return ret;
}

static void sk_tcp_flush(Socket s)
{
    /*
     * We send data to the socket as soon as we can anyway,
     * so we don't need to do anything here.  :-)
     */
}

static void sk_tcp_close(Socket s);
static int sk_tcp_write(Socket s, char *data, int len);
static int sk_tcp_write_oob(Socket s, char *data, int len);
static void sk_tcp_set_private_ptr(Socket s, void *ptr);
static void *sk_tcp_get_private_ptr(Socket s);
static void sk_tcp_set_frozen(Socket s, int is_frozen);
static char *sk_tcp_socket_error(Socket s);

Socket sk_register(void *sock, Plug plug)
{
    static struct socket_function_table fn_table = {
	sk_tcp_plug,
	sk_tcp_close,
	sk_tcp_write,
	sk_tcp_write_oob,
	sk_tcp_flush,
	sk_tcp_set_private_ptr,
	sk_tcp_get_private_ptr,
	sk_tcp_set_frozen,
	sk_tcp_socket_error
    };

    Actual_Socket ret;

    /*
     * Create Socket structure.
     */
    ret = smalloc(sizeof(struct Socket_tag));
    ret->fn = &fn_table;
    ret->error = NULL;
    ret->plug = plug;
    bufchain_init(&ret->output_data);
    ret->writable = 1;		       /* to start with */
    ret->sending_oob = 0;
    ret->frozen = 1;
    ret->frozen_readable = 0;
    ret->localhost_only = 0;	       /* unused, but best init anyway */
    ret->pending_error = 0;
    ret->oobpending = FALSE;
    ret->listener = 0;

    ret->s = (int)sock;

    if (ret->s < 0) {
	ret->error = error_string(errno);
	return (Socket) ret;
    }

    ret->oobinline = 0;

    add234(sktree, ret);

    return (Socket) ret;
}

Socket sk_new(SockAddr addr, int port, int privport, int oobinline,
	      int nodelay, Plug plug)
{
    static struct socket_function_table fn_table = {
	sk_tcp_plug,
	sk_tcp_close,
	sk_tcp_write,
	sk_tcp_write_oob,
	sk_tcp_flush,
	sk_tcp_set_private_ptr,
	sk_tcp_get_private_ptr,
	sk_tcp_set_frozen,
	sk_tcp_socket_error
    };

    int s;
#ifdef IPV6
    struct sockaddr_in6 a6;
#endif
    struct sockaddr_in a;
    int err;
    Actual_Socket ret;
    short localport;

    /*
     * Create Socket structure.
     */
    ret = smalloc(sizeof(struct Socket_tag));
    ret->fn = &fn_table;
    ret->error = NULL;
    ret->plug = plug;
    bufchain_init(&ret->output_data);
    ret->connected = 0;		       /* to start with */
    ret->writable = 0;		       /* to start with */
    ret->sending_oob = 0;
    ret->frozen = 0;
    ret->frozen_readable = 0;
    ret->localhost_only = 0;	       /* unused, but best init anyway */
    ret->pending_error = 0;
    ret->oobpending = FALSE;
    ret->listener = 0;

    /*
     * Open socket.
     */
    s = socket(addr->family, SOCK_STREAM, 0);
    ret->s = s;

    if (s < 0) {
	ret->error = error_string(errno);
	return (Socket) ret;
    }

    ret->oobinline = oobinline;
    if (oobinline) {
	int b = TRUE;
	setsockopt(s, SOL_SOCKET, SO_OOBINLINE, (void *) &b, sizeof(b));
    }

    if (nodelay) {
	int b = TRUE;
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (void *) &b, sizeof(b));
    }

    /*
     * Bind to local address.
     */
    if (privport)
	localport = 1023;	       /* count from 1023 downwards */
    else
	localport = 0;		       /* just use port 0 (ie kernel picks) */

    /* Loop round trying to bind */
    while (1) {
	int retcode;

#ifdef IPV6
	if (addr->family == AF_INET6) {
	    memset(&a6, 0, sizeof(a6));
	    a6.sin6_family = AF_INET6;
/*a6.sin6_addr      = in6addr_any; *//* == 0 */
	    a6.sin6_port = htons(localport);
	} else
#endif
	{
	    a.sin_family = AF_INET;
	    a.sin_addr.s_addr = htonl(INADDR_ANY);
	    a.sin_port = htons(localport);
	}
#ifdef IPV6
	retcode = bind(s, (addr->family == AF_INET6 ?
			   (struct sockaddr *) &a6 :
			   (struct sockaddr *) &a),
		       (addr->family ==
			AF_INET6 ? sizeof(a6) : sizeof(a)));
#else
	retcode = bind(s, (struct sockaddr *) &a, sizeof(a));
#endif
	if (retcode >= 0) {
	    err = 0;
	    break;		       /* done */
	} else {
	    err = errno;
	    if (err != EADDRINUSE)     /* failed, for a bad reason */
		break;
	}

	if (localport == 0)
	    break;		       /* we're only looping once */
	localport--;
	if (localport == 0)
	    break;		       /* we might have got to the end */
    }

    if (err) {
	ret->error = error_string(err);
	return (Socket) ret;
    }

    /*
     * Connect to remote address.
     */
#ifdef IPV6
    if (addr->family == AF_INET6) {
	memset(&a, 0, sizeof(a));
	a6.sin6_family = AF_INET6;
	a6.sin6_port = htons((short) port);
	a6.sin6_addr =
	    ((struct sockaddr_in6 *) addr->ai->ai_addr)->sin6_addr;
    } else
#endif
    {
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(addr->address);
	a.sin_port = htons((short) port);
    }

    if ((
#ifdef IPV6
	    connect(s, ((addr->family == AF_INET6) ?
			(struct sockaddr *) &a6 : (struct sockaddr *) &a),
		    (addr->family == AF_INET6) ? sizeof(a6) : sizeof(a))
#else
	    connect(s, (struct sockaddr *) &a, sizeof(a))
#endif
	) < 0) {
	/*
	 * FIXME: We are prepared to receive EWOULDBLOCK here,
	 * because we might want the connection to be made
	 * asynchronously; but how do we actually arrange this in
	 * Unix? I forget.
	 */
	if ( errno != EWOULDBLOCK ) {
	    ret->error = error_string(errno);
	    return (Socket) ret;
	}
    } else {
	/*
	 * If we _don't_ get EWOULDBLOCK, the connect has completed
	 * and we should set the socket as connected and writable.
	 */
	ret->connected = 1;
	ret->writable = 1;
    }

    add234(sktree, ret);

    return (Socket) ret;
}

Socket sk_newlistener(int port, Plug plug, int local_host_only)
{
    static struct socket_function_table fn_table = {
	sk_tcp_plug,
	sk_tcp_close,
	sk_tcp_write,
	sk_tcp_write_oob,
	sk_tcp_flush,
	sk_tcp_set_private_ptr,
	sk_tcp_get_private_ptr,
	sk_tcp_set_frozen,
	sk_tcp_socket_error
    };

    int s;
#ifdef IPV6
    struct sockaddr_in6 a6;
#endif
    struct sockaddr_in a;
    int err;
    Actual_Socket ret;
    int retcode;
    int on = 1;

    /*
     * Create Socket structure.
     */
    ret = smalloc(sizeof(struct Socket_tag));
    ret->fn = &fn_table;
    ret->error = NULL;
    ret->plug = plug;
    bufchain_init(&ret->output_data);
    ret->writable = 0;		       /* to start with */
    ret->sending_oob = 0;
    ret->frozen = 0;
    ret->frozen_readable = 0;
    ret->localhost_only = local_host_only;
    ret->pending_error = 0;
    ret->oobpending = FALSE;
    ret->listener = 1;

    /*
     * Open socket.
     */
    s = socket(AF_INET, SOCK_STREAM, 0);
    ret->s = s;

    if (s < 0) {
	ret->error = error_string(errno);
	return (Socket) ret;
    }

    ret->oobinline = 0;

    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));

#ifdef IPV6
    if (addr->family == AF_INET6) {
	memset(&a6, 0, sizeof(a6));
	a6.sin6_family = AF_INET6;
	if (local_host_only)
	    a6.sin6_addr = in6addr_loopback;
	else
	    a6.sin6_addr = in6addr_any;
	a6.sin6_port = htons(port);
    } else
#endif
    {
	a.sin_family = AF_INET;
	if (local_host_only)
	    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	else
	    a.sin_addr.s_addr = htonl(INADDR_ANY);
	a.sin_port = htons((short)port);
    }
#ifdef IPV6
    retcode = bind(s, (addr->family == AF_INET6 ?
		       (struct sockaddr *) &a6 :
		       (struct sockaddr *) &a),
		   (addr->family ==
		    AF_INET6 ? sizeof(a6) : sizeof(a)));
#else
    retcode = bind(s, (struct sockaddr *) &a, sizeof(a));
#endif
    if (retcode >= 0) {
	err = 0;
    } else {
	err = errno;
    }

    if (err) {
	ret->error = error_string(err);
	return (Socket) ret;
    }


    if (listen(s, SOMAXCONN) < 0) {
        close(s);
	ret->error = error_string(errno);
	return (Socket) ret;
    }

    add234(sktree, ret);

    return (Socket) ret;
}

static void sk_tcp_close(Socket sock)
{
    Actual_Socket s = (Actual_Socket) sock;

    del234(sktree, s);
    close(s->s);
    sfree(s);
}

/*
 * The function which tries to send on a socket once it's deemed
 * writable.
 */
void try_send(Actual_Socket s)
{
    while (s->sending_oob || bufchain_size(&s->output_data) > 0) {
	int nsent;
	int err;
	void *data;
	int len, urgentflag;

	if (s->sending_oob) {
	    urgentflag = MSG_OOB;
	    len = s->sending_oob;
	    data = &s->oobdata;
	} else {
	    urgentflag = 0;
	    bufchain_prefix(&s->output_data, &data, &len);
	}
	nsent = send(s->s, data, len, urgentflag);
	noise_ultralight(nsent);
	if (nsent <= 0) {
	    err = (nsent < 0 ? errno : 0);
	    if (err == EWOULDBLOCK) {
		/*
		 * Perfectly normal: we've sent all we can for the moment.
		 */
		s->writable = FALSE;
		return;
	    } else if (nsent == 0 ||
		       err == ECONNABORTED || err == ECONNRESET) {
		/*
		 * If send() returns CONNABORTED or CONNRESET, we
		 * unfortunately can't just call plug_closing(),
		 * because it's quite likely that we're currently
		 * _in_ a call from the code we'd be calling back
		 * to, so we'd have to make half the SSH code
		 * reentrant. Instead we flag a pending error on
		 * the socket, to be dealt with (by calling
		 * plug_closing()) at some suitable future moment.
		 */
		s->pending_error = err;
		return;
	    } else {
		/* We're inside the Unix frontend here, so we know
		 * that the frontend handle is unnecessary. */
		logevent(NULL, error_string(err));
		fatalbox("%s", error_string(err));
	    }
	} else {
	    if (s->sending_oob) {
		if (nsent < len) {
		    memmove(s->oobdata, s->oobdata+nsent, len-nsent);
		    s->sending_oob = len - nsent;
		} else {
		    s->sending_oob = 0;
		}
	    } else {
		bufchain_consume(&s->output_data, nsent);
	    }
	}
    }
}

static int sk_tcp_write(Socket sock, char *buf, int len)
{
    Actual_Socket s = (Actual_Socket) sock;

    /*
     * Add the data to the buffer list on the socket.
     */
    bufchain_add(&s->output_data, buf, len);

    /*
     * Now try sending from the start of the buffer list.
     */
    if (s->writable)
	try_send(s);

    return bufchain_size(&s->output_data);
}

static int sk_tcp_write_oob(Socket sock, char *buf, int len)
{
    Actual_Socket s = (Actual_Socket) sock;

    /*
     * Replace the buffer list on the socket with the data.
     */
    bufchain_clear(&s->output_data);
    assert(len <= sizeof(s->oobdata));
    memcpy(s->oobdata, buf, len);
    s->sending_oob = len;

    /*
     * Now try sending from the start of the buffer list.
     */
    if (s->writable)
	try_send(s);

    return s->sending_oob;
}

int select_result(int fd, int event)
{
    int ret;
    int err;
    char buf[20480];		       /* nice big buffer for plenty of speed */
    Actual_Socket s;
    u_long atmark;

    /* Find the Socket structure */
    s = find234(sktree, (void *) fd, cmpforsearch);
    if (!s)
	return 1;		       /* boggle */

    noise_ultralight(event);

    switch (event) {
#ifdef FIXME_NONBLOCKING_CONNECTIONS
      case FIXME:		       /* connected */
	s->connected = s->writable = 1;
	break;
#endif
      case 4:			       /* exceptional */
	if (!s->oobinline) {
	    /*
	     * On a non-oobinline socket, this indicates that we
	     * can immediately perform an OOB read and get back OOB
	     * data, which we will send to the back end with
	     * type==2 (urgent data).
	     */
	    ret = recv(s->s, buf, sizeof(buf), MSG_OOB);
	    noise_ultralight(ret);
	    if (ret <= 0) {
		char *str = (ret == 0 ? "Internal networking trouble" :
			     error_string(errno));
		/* We're inside the Unix frontend here, so we know
		 * that the frontend handle is unnecessary. */
		logevent(NULL, str);
		fatalbox("%s", str);
	    } else {
		return plug_receive(s->plug, 2, buf, ret);
	    }
	    break;
	}

	/*
	 * If we reach here, this is an oobinline socket, which
	 * means we should set s->oobpending and then deal with it
	 * when we get called for the readability event (which
	 * should also occur).
	 */
	s->oobpending = TRUE;
        break;
      case 1: 			       /* readable; also acceptance */
	if (s->listener) {
	    /*
	     * On a listening socket, the readability event means a
	     * connection is ready to be accepted.
	     */
	    struct sockaddr_in isa;
	    int addrlen = sizeof(struct sockaddr_in);
	    int t;  /* socket of connection */

	    memset(&isa, 0, sizeof(struct sockaddr_in));
	    err = 0;
	    t = accept(s->s,(struct sockaddr *)&isa,&addrlen);
	    if (t < 0) {
		break;
	    }

	    if (s->localhost_only &&
		ntohl(isa.sin_addr.s_addr) != INADDR_LOOPBACK) {
		close(t);	       /* someone let nonlocal through?! */
	    } else if (plug_accepting(s->plug, (void*)t)) {
		close(t);	       /* denied or error */
	    }
	    break;
	}

	/*
	 * If we reach here, this is not a listening socket, so
	 * readability really means readability.
	 */

	/* In the case the socket is still frozen, we don't even bother */
	if (s->frozen) {
	    s->frozen_readable = 1;
	    break;
	}

	/*
	 * We have received data on the socket. For an oobinline
	 * socket, this might be data _before_ an urgent pointer,
	 * in which case we send it to the back end with type==1
	 * (data prior to urgent).
	 */
	if (s->oobinline && s->oobpending) {
	    atmark = 1;
	    if (ioctl(s->s, SIOCATMARK, &atmark) == 0 && atmark)
		s->oobpending = FALSE; /* clear this indicator */
	} else
	    atmark = 1;

	ret = recv(s->s, buf, s->oobpending ? 1 : sizeof(buf), 0);
	noise_ultralight(ret);
	if (ret < 0) {
	    if (errno == EWOULDBLOCK) {
		break;
	    }
	}
	if (ret < 0) {
	    return plug_closing(s->plug, error_string(errno), errno, 0);
	} else if (0 == ret) {
	    return plug_closing(s->plug, NULL, 0, 0);
	} else {
	    return plug_receive(s->plug, atmark ? 0 : 1, buf, ret);
	}
	break;
      case 2:			       /* writable */
	{
	    int bufsize_before, bufsize_after;
	    s->writable = 1;
	    bufsize_before = s->sending_oob + bufchain_size(&s->output_data);
	    try_send(s);
	    bufsize_after = s->sending_oob + bufchain_size(&s->output_data);
	    if (bufsize_after < bufsize_before)
		plug_sent(s->plug, bufsize_after);
	}
	break;
    }

    return 1;
}

/*
 * Deal with socket errors detected in try_send().
 */
void net_pending_errors(void)
{
    int i;
    Actual_Socket s;

    /*
     * This might be a fiddly business, because it's just possible
     * that handling a pending error on one socket might cause
     * others to be closed. (I can't think of any reason this might
     * happen in current SSH implementation, but to maintain
     * generality of this network layer I'll assume the worst.)
     * 
     * So what we'll do is search the socket list for _one_ socket
     * with a pending error, and then handle it, and then search
     * the list again _from the beginning_. Repeat until we make a
     * pass with no socket errors present. That way we are
     * protected against the socket list changing under our feet.
     */

    do {
	for (i = 0; (s = index234(sktree, i)) != NULL; i++) {
	    if (s->pending_error) {
		/*
		 * An error has occurred on this socket. Pass it to the
		 * plug.
		 */
		plug_closing(s->plug, error_string(s->pending_error),
			     s->pending_error, 0);
		break;
	    }
	}
    } while (s);
}

/*
 * Each socket abstraction contains a `void *' private field in
 * which the client can keep state.
 */
static void sk_tcp_set_private_ptr(Socket sock, void *ptr)
{
    Actual_Socket s = (Actual_Socket) sock;
    s->private_ptr = ptr;
}

static void *sk_tcp_get_private_ptr(Socket sock)
{
    Actual_Socket s = (Actual_Socket) sock;
    return s->private_ptr;
}

/*
 * Special error values are returned from sk_namelookup and sk_new
 * if there's a problem. These functions extract an error message,
 * or return NULL if there's no problem.
 */
char *sk_addr_error(SockAddr addr)
{
    return addr->error;
}
static char *sk_tcp_socket_error(Socket sock)
{
    Actual_Socket s = (Actual_Socket) sock;
    return s->error;
}

static void sk_tcp_set_frozen(Socket sock, int is_frozen)
{
    Actual_Socket s = (Actual_Socket) sock;
    if (s->frozen == is_frozen)
	return;
    s->frozen = is_frozen;
    if (!is_frozen && s->frozen_readable) {
	char c;
	recv(s->s, &c, 1, MSG_PEEK);
    }
    s->frozen_readable = 0;
}

/*
 * For Unix select()-based frontends: enumerate all sockets
 * currently active, and state whether we currently wish to receive
 * select events on them for reading, writing and exceptional
 * status.
 */
static void set_rwx(Actual_Socket s, int *rwx)
{
    int val = 0;
    if (s->connected && !s->frozen)
	val |= 1 | 4;		       /* read, except */
    if (bufchain_size(&s->output_data))
	val |= 2;		       /* write */
    if (s->listener)
	val |= 1;		       /* read == accept */
    *rwx = val;
}

int first_socket(int *state, int *rwx)
{
    Actual_Socket s;
    *state = 0;
    s = index234(sktree, (*state)++);
    if (s)
	set_rwx(s, rwx);
    return s ? s->s : -1;
}

int next_socket(int *state, int *rwx)
{
    Actual_Socket s = index234(sktree, (*state)++);
    if (s)
	set_rwx(s, rwx);
    return s ? s->s : -1;
}

int net_service_lookup(char *service)
{
    struct servent *se;
    se = getservbyname(service, NULL);
    if (se != NULL)
	return ntohs(se->s_port);
    else
	return 0;
}
