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

#define ipv4_is_loopback(addr) (inet_netof(addr) == IN_LOOPBACKNET)

struct Socket_tag {
    struct socket_function_table *fn;
    /* the above variable absolutely *must* be the first in this structure */
    const char *error;
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
    const char *error;
    /*
     * Which address family this address belongs to. AF_INET for
     * IPv4; AF_INET6 for IPv6; AF_UNSPEC indicates that name
     * resolution has not been done and a simple host name is held
     * in this SockAddr structure.
     */
    int family;
#ifdef IPV6
    struct addrinfo *ai;	       /* Address IPv6 style. */
#else
    unsigned long address;	       /* Address IPv4 style. */
#endif
    char hostname[512];		       /* Store an unresolved host name. */
};

static tree234 *sktree;

static void uxsel_tell(Actual_Socket s);

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
    int as = *(int *)av, bs = b->s;
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

const char *error_string(int error)
{
    return strerror(error);
}

SockAddr sk_namelookup(const char *host, char **canonicalname)
{
    SockAddr ret = snew(struct SockAddr_tag);
#ifdef IPV6
    struct addrinfo hints;
    int err;
#else
    unsigned long a;
    struct hostent *h = NULL;
#endif
    char realhost[8192];

    /* Clear the structure and default to IPv4. */
    memset(ret, 0, sizeof(struct SockAddr_tag));
    ret->family = 0;		       /* We set this one when we have resolved the host. */
    *realhost = '\0';
    ret->error = NULL;

#ifdef IPV6
    hints.ai_flags = AI_CANONNAME;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = 0;
    hints.ai_protocol = 0;
    hints.ai_addrlen = 0;
    hints.ai_addr = NULL;
    hints.ai_canonname = NULL;
    hints.ai_next = NULL;
    err = getaddrinfo(host, NULL, NULL, &ret->ai);
    if (err != 0) {
	ret->error = gai_strerror(err);
	return ret;
    }
    ret->family = ret->ai->ai_family;
    *realhost = '\0';
    if (ret->ai->ai_canonname != NULL)
	strncat(realhost, ret->ai->ai_canonname, sizeof(realhost) - 1);
    else
	strncat(realhost, host, sizeof(realhost) - 1);
#else
    if ((a = inet_addr(host)) == (unsigned long) INADDR_NONE) {
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
	memcpy(&a, h->h_addr, sizeof(a));
	/* This way we are always sure the h->h_name is valid :) */
	strncpy(realhost, h->h_name, sizeof(realhost));
    } else {
	/*
	 * This must be a numeric IPv4 address because it caused a
	 * success return from inet_addr.
	 */
	ret->family = AF_INET;
	strncpy(realhost, host, sizeof(realhost));
    }
    ret->address = ntohl(a);
#endif
    realhost[lenof(realhost)-1] = '\0';
    *canonicalname = snewn(1+strlen(realhost), char);
    strcpy(*canonicalname, realhost);
    return ret;
}

SockAddr sk_nonamelookup(const char *host)
{
    SockAddr ret = snew(struct SockAddr_tag);
    ret->error = NULL;
    ret->family = AF_UNSPEC;
    strncpy(ret->hostname, host, lenof(ret->hostname));
    ret->hostname[lenof(ret->hostname)-1] = '\0';
    return ret;
}

void sk_getaddr(SockAddr addr, char *buf, int buflen)
{

    if (addr->family == AF_UNSPEC) {
	strncpy(buf, addr->hostname, buflen);
	buf[buflen-1] = '\0';
    } else {
#ifdef IPV6
	if (getnameinfo(addr->ai->ai_addr, addr->ai->ai_addrlen, buf, buflen,
			NULL, 0, NI_NUMERICHOST) != 0) {
	    buf[0] = '\0';
	    strncat(buf, "<unknown>", buflen - 1);
	}
#else
	struct in_addr a;
	assert(addr->family == AF_INET);
	a.s_addr = htonl(addr->address);
	strncpy(buf, inet_ntoa(a), buflen);
	buf[buflen-1] = '\0';
#endif
    }
}

int sk_hostname_is_local(char *name)
{
    return !strcmp(name, "localhost");
}

int sk_address_is_local(SockAddr addr)
{

    if (addr->family == AF_UNSPEC)
	return 0;                      /* we don't know; assume not */
    else {
#ifdef IPV6
	if (addr->family == AF_INET)
	    return ipv4_is_loopback(
		((struct sockaddr_in *)addr->ai->ai_addr)->sin_addr);
	else if (addr->family == AF_INET6)
	    return IN6_IS_ADDR_LOOPBACK(
		&((struct sockaddr_in6 *)addr->ai->ai_addr)->sin6_addr);
	else
	    return 0;
#else
	struct in_addr a;
	assert(addr->family == AF_INET);
	a.s_addr = htonl(addr->address);
	return ipv4_is_loopback(a);
#endif
    }
}

int sk_addrtype(SockAddr addr)
{
    return (addr->family == AF_INET ? ADDRTYPE_IPV4 :
#ifdef IPV6
	    addr->family == AF_INET6 ? ADDRTYPE_IPV6 :
#endif
	    ADDRTYPE_NAME);
}

void sk_addrcopy(SockAddr addr, char *buf)
{

#ifdef IPV6
    if (addr->family == AF_INET)
	memcpy(buf, &((struct sockaddr_in *)addr->ai->ai_addr)->sin_addr,
	       sizeof(struct in_addr));
    else if (addr->family == AF_INET6)
	memcpy(buf, &((struct sockaddr_in6 *)addr->ai->ai_addr)->sin6_addr,
	       sizeof(struct in6_addr));
    else
	assert(FALSE);
#else
    struct in_addr a;

    assert(addr->family == AF_INET);
    a.s_addr = htonl(addr->address);
    memcpy(buf, (char*) &a.s_addr, 4);
#endif
}

void sk_addr_free(SockAddr addr)
{

#ifdef IPV6
    if (addr->ai != NULL)
	freeaddrinfo(addr->ai);
#endif
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
static int sk_tcp_write(Socket s, const char *data, int len);
static int sk_tcp_write_oob(Socket s, const char *data, int len);
static void sk_tcp_set_private_ptr(Socket s, void *ptr);
static void *sk_tcp_get_private_ptr(Socket s);
static void sk_tcp_set_frozen(Socket s, int is_frozen);
static const char *sk_tcp_socket_error(Socket s);

static struct socket_function_table tcp_fn_table = {
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

Socket sk_register(OSSocket sockfd, Plug plug)
{
    Actual_Socket ret;

    /*
     * Create Socket structure.
     */
    ret = snew(struct Socket_tag);
    ret->fn = &tcp_fn_table;
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

    ret->s = sockfd;

    if (ret->s < 0) {
	ret->error = error_string(errno);
	return (Socket) ret;
    }

    ret->oobinline = 0;

    uxsel_tell(ret);
    add234(sktree, ret);

    return (Socket) ret;
}

Socket sk_new(SockAddr addr, int port, int privport, int oobinline,
	      int nodelay, Plug plug)
{
    int s;
#ifdef IPV6
    struct sockaddr_in6 a6;
#endif
    struct sockaddr_in a;
    int err;
    Actual_Socket ret;
    short localport;
    int fl;

    /*
     * Create Socket structure.
     */
    ret = snew(struct Socket_tag);
    ret->fn = &tcp_fn_table;
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
    assert(addr->family != AF_UNSPEC);
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
	    /* XXX use getaddrinfo to get a local address? */
	    a6.sin6_family = AF_INET6;
	    a6.sin6_addr = in6addr_any;
	    a6.sin6_port = htons(localport);
	    retcode = bind(s, (struct sockaddr *) &a6, sizeof(a6));
	} else
#endif
	{
	    assert(addr->family == AF_INET);
	    a.sin_family = AF_INET;
	    a.sin_addr.s_addr = htonl(INADDR_ANY);
	    a.sin_port = htons(localport);
	    retcode = bind(s, (struct sockaddr *) &a, sizeof(a));
	}
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
    /* XXX would be better to have got getaddrinfo() to fill in the port. */
    if (addr->family == AF_INET)
	((struct sockaddr_in *)addr->ai->ai_addr)->sin_port =
	    htons(port);
    else {
	assert(addr->family == AF_INET6);
	((struct sockaddr_in *)addr->ai->ai_addr)->sin_port =
	    htons(port);
    }
#else
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(addr->address);
    a.sin_port = htons((short) port);
#endif

    fl = fcntl(s, F_GETFL);
    if (fl != -1)
	fcntl(s, F_SETFL, fl | O_NONBLOCK);

    if ((
#ifdef IPV6
	    connect(s, addr->ai->ai_addr, addr->ai->ai_addrlen)
#else
	    connect(s, (struct sockaddr *) &a, sizeof(a))
#endif
	) < 0) {
	if ( errno != EINPROGRESS ) {
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

    uxsel_tell(ret);
    add234(sktree, ret);

    return (Socket) ret;
}

Socket sk_newlistener(char *srcaddr, int port, Plug plug, int local_host_only)
{
    int s;
#ifdef IPV6
#if 0
    struct sockaddr_in6 a6;
#endif
    struct addrinfo hints, *ai;
    char portstr[6];
#endif
    struct sockaddr_in a;
    int err;
    Actual_Socket ret;
    int retcode;
    int on = 1;

    /*
     * Create Socket structure.
     */
    ret = snew(struct Socket_tag);
    ret->fn = &tcp_fn_table;
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
    hints.ai_flags = AI_NUMERICHOST;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = 0;
    hints.ai_protocol = 0;
    hints.ai_addrlen = 0;
    hints.ai_addr = NULL;
    hints.ai_canonname = NULL;
    hints.ai_next = NULL;
    sprintf(portstr, "%d", port);
    if (srcaddr != NULL && getaddrinfo(srcaddr, portstr, &hints, &ai) == 0)
	retcode = bind(s, ai->ai_addr, ai->ai_addrlen);
    else
#if 0
    {
	/*
	 * FIXME: Need two listening sockets, in principle, one for v4
	 * and one for v6
	 */
	if (local_host_only)
	    a6.sin6_addr = in6addr_loopback;
	else
	    a6.sin6_addr = in6addr_any;
	a6.sin6_port = htons(port);
    } else
#endif
#endif
    {
	int got_addr = 0;
	a.sin_family = AF_INET;

	/*
	 * Bind to source address. First try an explicitly
	 * specified one...
	 */
	if (srcaddr) {
	    a.sin_addr.s_addr = inet_addr(srcaddr);
	    if (a.sin_addr.s_addr != INADDR_NONE) {
		/* Override localhost_only with specified listen addr. */
		ret->localhost_only = ipv4_is_loopback(a.sin_addr);
		got_addr = 1;
	    }
	}

	/*
	 * ... and failing that, go with one of the standard ones.
	 */
	if (!got_addr) {
	    if (local_host_only)
		a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	    else
		a.sin_addr.s_addr = htonl(INADDR_ANY);
	}

	a.sin_port = htons((short)port);
	retcode = bind(s, (struct sockaddr *) &a, sizeof(a));
    }

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

    uxsel_tell(ret);
    add234(sktree, ret);

    return (Socket) ret;
}

static void sk_tcp_close(Socket sock)
{
    Actual_Socket s = (Actual_Socket) sock;

    uxsel_del(s->s);
    del234(sktree, s);
    close(s->s);
    sfree(s);
}

int sk_getxdmdata(void *sock, unsigned long *ip, int *port)
{
    Actual_Socket s = (Actual_Socket) sock;
    struct sockaddr_in addr;
    socklen_t addrlen;

    /*
     * We must check that this socket really _is_ an Actual_Socket.
     */
    if (s->fn != &tcp_fn_table)
	return 0;		       /* failure */

    /*
     * If we ever implement connecting to a local X server through
     * a Unix socket, we return 0xFFFFFFFF for the IP address and
     * our current pid for the port. Bizarre, but such is life.
     */

    addrlen = sizeof(addr);
    if (getsockname(s->s, (struct sockaddr *)&addr, &addrlen) < 0 ||
	addr.sin_family != AF_INET)
	return 0;

    *ip = ntohl(addr.sin_addr.s_addr);
    *port = ntohs(addr.sin_port);

    return 1;
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
    uxsel_tell(s);
}

static int sk_tcp_write(Socket sock, const char *buf, int len)
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

    /*
     * Update the select() status to correctly reflect whether or
     * not we should be selecting for write.
     */
    uxsel_tell(s);

    return bufchain_size(&s->output_data);
}

static int sk_tcp_write_oob(Socket sock, const char *buf, int len)
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

    /*
     * Update the select() status to correctly reflect whether or
     * not we should be selecting for write.
     */
    uxsel_tell(s);

    return s->sending_oob;
}

static int net_select_result(int fd, int event)
{
    int ret;
    int err;
    char buf[20480];		       /* nice big buffer for plenty of speed */
    Actual_Socket s;
    u_long atmark;

    /* Find the Socket structure */
    s = find234(sktree, &fd, cmpforsearch);
    if (!s)
	return 1;		       /* boggle */

    noise_ultralight(event);

    switch (event) {
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
		const char *str = (ret == 0 ? "Internal networking trouble" :
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

	    if (s->localhost_only && !ipv4_is_loopback(isa.sin_addr)) {
		close(t);	       /* someone let nonlocal through?! */
	    } else if (plug_accepting(s->plug, t)) {
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
	if (!s->connected) {
	    /*
	     * select() reports a socket as _writable_ when an
	     * asynchronous connection is completed.
	     */
	    s->connected = s->writable = 1;
	    uxsel_tell(s);
	    break;
	} else {
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
const char *sk_addr_error(SockAddr addr)
{
    return addr->error;
}
static const char *sk_tcp_socket_error(Socket sock)
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
    uxsel_tell(s);
}

static void uxsel_tell(Actual_Socket s)
{
    int rwx = 0;
    if (!s->connected)
	rwx |= 2;		       /* write == connect */
    if (s->connected && !s->frozen)
	rwx |= 1 | 4;		       /* read, except */
    if (bufchain_size(&s->output_data))
	rwx |= 2;		       /* write */
    if (s->listener)
	rwx |= 1;		       /* read == accept */
    uxsel_set(s->s, rwx, net_select_result);
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
