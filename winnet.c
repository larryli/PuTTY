/*
 * Windows networking abstraction.
 *
 * Due to this clean abstraction it was possible
 * to easily implement IPv6 support :)
 *
 * IPv6 patch 1 (27 October 2000) Jeroen Massar <jeroen@unfix.org>
 *  - Preliminary hacked IPv6 support.
 *    - Connecting to IPv6 address (eg fec0:4242:4242:100:2d0:b7ff:fe8f:5d42) works.
 *    - Connecting to IPv6 hostname (eg heaven.ipv6.unfix.org) works.
 *  - Compiles as either IPv4 or IPv6.
 *
 * IPv6 patch 2 (29 October 2000) Jeroen Massar <jeroen@unfix.org>
 *  - When compiled as IPv6 it also allows connecting to IPv4 hosts.
 *  - Added some more documentation.
 *
 * IPv6 patch 3 (18 November 2000) Jeroen Massar <jeroen@unfix.org>
 *  - It now supports dynamically loading the IPv6 resolver dll's.
 *    This way we should be able to distribute one (1) binary
 *    which supports both IPv4 and IPv6.
 *  - getaddrinfo() and getnameinfo() are loaded dynamicaly if possible.
 *  - in6addr_any is defined in this file so we don't need to link to wship6.lib
 *  - The patch is now more unified so that we can still
 *    remove all IPv6 support by undef'ing IPV6.
 *    But where it fallsback to IPv4 it uses the IPv4 code which is already in place...
 *  - Canonical name resolving works.
 *
 * IPv6 patch 4 (07 January 2001) Jeroen Massar <jeroen@unfix.org>
 *  - patch against CVS of today, will be submitted to the bugs list
 *    as a 'cvs diff -u' on Simon's request...
 *
 */

/*
 * Define IPV6 to have IPv6 on-the-fly-loading support.
 * This means that one doesn't have to have an IPv6 stack to use it.
 * But if an IPv6 stack is found it is used with a fallback to IPv4.
 */
/* #define IPV6 1 */

#ifdef IPV6
#include <winsock2.h>
#include <ws2tcpip.h>
#include <tpipv6.h>
#else
#include <winsock.h>
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "putty.h"
#include "network.h"
#include "tree234.h"

#define BUFFER_GRANULE 512

struct Socket_tag {
    char *error;
    SOCKET s;
    sk_receiver_t receiver;
    void *private_ptr;
    struct buffer *head, *tail;
    int writable;
    int in_oob, sending_oob;
};

struct SockAddr_tag {
    char *error;
    /* address family this belongs to, AF_INET for IPv4, AF_INET6 for IPv6. */
    int family;			     
    unsigned long address;	       /* Address IPv4 style. */
#ifdef IPV6
    struct addrinfo *ai;	       /* Address IPv6 style. */
#endif
    /*
     * We need to have this lengthy enough to hold *any* hostname
     * (including IPv6 reverse...)
     */
    char realhost[8192];
};

struct buffer {
    struct buffer *next;
    int buflen, bufpos;
    char buf[BUFFER_GRANULE];
};

static tree234 *sktree;

static int cmpfortree(void *av, void *bv) {
    Socket a = (Socket)av, b = (Socket)bv;
    unsigned long as = (unsigned long)a->s, bs = (unsigned long)b->s;
    if (as < bs) return -1;
    if (as > bs) return +1;
    return 0;
}

static int cmpforsearch(void *av, void *bv) {
    Socket b = (Socket)bv;
    unsigned long as = (unsigned long)av, bs = (unsigned long)b->s;
    if (as < bs) return -1;
    if (as > bs) return +1;
    return 0;
}

void sk_init(void) {
    sktree = newtree234(cmpfortree);
}

char *winsock_error_string(int error) {
    switch (error) {
      case WSAEACCES: return "Network error: Permission denied";
      case WSAEADDRINUSE: return "Network error: Address already in use";
      case WSAEADDRNOTAVAIL: return "Network error: Cannot assign requested address";
      case WSAEAFNOSUPPORT: return "Network error: Address family not supported by protocol family";
      case WSAEALREADY: return "Network error: Operation already in progress";
      case WSAECONNABORTED: return "Network error: Software caused connection abort";
      case WSAECONNREFUSED: return "Network error: Connection refused";
      case WSAECONNRESET: return "Network error: Connection reset by peer";
      case WSAEDESTADDRREQ: return "Network error: Destination address required";
      case WSAEFAULT: return "Network error: Bad address";
      case WSAEHOSTDOWN: return "Network error: Host is down";
      case WSAEHOSTUNREACH: return "Network error: No route to host";
      case WSAEINPROGRESS: return "Network error: Operation now in progress";
      case WSAEINTR: return "Network error: Interrupted function call";
      case WSAEINVAL: return "Network error: Invalid argument";
      case WSAEISCONN: return "Network error: Socket is already connected";
      case WSAEMFILE: return "Network error: Too many open files";
      case WSAEMSGSIZE: return "Network error: Message too long";
      case WSAENETDOWN: return "Network error: Network is down";
      case WSAENETRESET: return "Network error: Network dropped connection on reset";
      case WSAENETUNREACH: return "Network error: Network is unreachable";
      case WSAENOBUFS: return "Network error: No buffer space available";
      case WSAENOPROTOOPT: return "Network error: Bad protocol option";
      case WSAENOTCONN: return "Network error: Socket is not connected";
      case WSAENOTSOCK: return "Network error: Socket operation on non-socket";
      case WSAEOPNOTSUPP: return "Network error: Operation not supported";
      case WSAEPFNOSUPPORT: return "Network error: Protocol family not supported";
      case WSAEPROCLIM: return "Network error: Too many processes";
      case WSAEPROTONOSUPPORT: return "Network error: Protocol not supported";
      case WSAEPROTOTYPE: return "Network error: Protocol wrong type for socket";
      case WSAESHUTDOWN: return "Network error: Cannot send after socket shutdown";
      case WSAESOCKTNOSUPPORT: return "Network error: Socket type not supported";
      case WSAETIMEDOUT: return "Network error: Connection timed out";
      case WSAEWOULDBLOCK: return "Network error: Resource temporarily unavailable";
      case WSAEDISCON: return "Network error: Graceful shutdown in progress";
      default: return "Unknown network error";
    }
}

SockAddr sk_namelookup(char *host, char **canonicalname)
{
    SockAddr ret = smalloc(sizeof(struct SockAddr_tag));
    unsigned long a;
    struct hostent *h = NULL;

    /* Clear the structure and default to IPv4. */
    memset(ret, 0, sizeof(struct SockAddr_tag));
    ret->family = 0;		       /* We set this one when we have resolved the host. */
    *canonicalname = ret->realhost;    /* This makes sure we always have a hostname to return. */

    if ( (a = inet_addr(host)) == (unsigned long) INADDR_NONE)
    {
#ifdef IPV6

	/* Try to get the getaddrinfo() function from wship6.dll */
	/* This way one doesn't need to have IPv6 dll's to use PuTTY and
	 * it will fallback to IPv4. */
	typedef int (CALLBACK* FGETADDRINFO)(const char *nodename,
					     const char *servname,
					     const struct addrinfo *hints,
					     struct addrinfo **res);
	FGETADDRINFO fGetAddrInfo = NULL;

	HINSTANCE dllWSHIP6 = LoadLibrary("wship6.dll");
	if (dllWSHIP6)
	    fGetAddrInfo = (FGETADDRINFO)GetProcAddress(dllWSHIP6,
							"getaddrinfo");

	/*
	 * Use fGetAddrInfo when it's available (which usually also
	 * means IPv6 is installed...)
	 */
	if (fGetAddrInfo)
	{
	    /*debug(("Resolving \"%s\" with getaddrinfo()  (IPv4+IPv6 capable)...\n", host)); */
	    if (fGetAddrInfo(host, NULL, NULL, &ret->ai) == 0)
		ret->family = ret->ai->ai_family;
	}
	else
#endif
	    /*
	     * Otherwise use the IPv4-only gethostbyname...
	     * (NOTE: we don't use gethostbyname as a
	     * fallback!)
	     */
	    if (ret->family == 0)
	{
	    /*debug(("Resolving \"%s\" with gethostbyname() (IPv4 only)...\n", host)); */
	    if (h = gethostbyname(host)) ret->family = AF_INET;
	}
	/*debug(("Done resolving...(family is %d) AF_INET = %d, AF_INET6 = %d\n", ret->family, AF_INET, AF_INET6)); */

	if (ret->family == 0)
	{
	    DWORD err = WSAGetLastError();
	    ret->error = (err == WSAENETDOWN ? "Network is down" :
			  err == WSAHOST_NOT_FOUND ? "Host does not exist" :
			  err == WSATRY_AGAIN ? "Host not found" :
#ifdef IPV6
			  fGetAddrInfo ? "getaddrinfo: unknown error" :
#endif
			  "gethostbyname: unknown error");
#ifdef DEBUG
	    {
		LPVOID lpMsgBuf;
		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf,	0,	NULL);
		/*debug(("Error %ld: %s (h=%lx)\n", err, lpMsgBuf, h));*/
		/* Free the buffer. */
		LocalFree(lpMsgBuf);
	    }
#endif
	}
	else
	{
	    ret->error = NULL;

#ifdef IPV6
	    /* If we got an address info use that... */
	    if (ret->ai)
	    {
		typedef int (CALLBACK* FGETNAMEINFO)
		    (const struct sockaddr FAR *sa, socklen_t salen,
		     char FAR * host, size_t hostlen, char FAR * serv,
		     size_t servlen, int flags);
		FGETNAMEINFO fGetNameInfo = NULL;

		/* Are we in IPv4 fallback mode? */
		/* We put the IPv4 address into the a variable so we can further-on use the IPv4 code... */
		if (ret->family == AF_INET)
		    memcpy(&a, (char *)&((SOCKADDR_IN *)ret->ai->ai_addr)->sin_addr, sizeof(a));

		/* Now let's find that canonicalname... */
		if ((dllWSHIP6) && (fGetNameInfo = (FGETNAMEINFO)GetProcAddress(dllWSHIP6, "getnameinfo")))
		{
		    if (fGetNameInfo((struct sockaddr *)ret->ai->ai_addr,
				     ret->family == AF_INET ?
				     sizeof(SOCKADDR_IN) :
				     sizeof(SOCKADDR_IN6), ret->realhost,
				     sizeof(ret->realhost), NULL,
				     0, 0) != 0)
		    {
			strncpy(ret->realhost, host,
				sizeof(ret->realhost));
		    }
		}
	    }
	    /* We used the IPv4-only gethostbyname()... */
	    else
	    {
#endif
		memcpy(&a, h->h_addr, sizeof(a));
		/* This way we are always sure the h->h_name is valid :) */
		strncpy(ret->realhost, h->h_name, sizeof(ret->realhost));
#ifdef IPV6
	    }
#endif
	}
#ifdef IPV6
	FreeLibrary(dllWSHIP6);
#endif
    }
    else
    {
        /*
         * Hack inserted to deal with problems with numeric IPs.
         * FIXME: how will this work in IPv6?
         */
        ret->family = AF_INET;
	*canonicalname = host;
    }
    ret->address = ntohl(a);
    return ret;
}

void sk_addr_free(SockAddr addr) {
    sfree(addr);
}

Socket sk_new(SockAddr addr, int port, int privport, sk_receiver_t receiver) {
    SOCKET s;
#ifdef IPV6
    SOCKADDR_IN6 a6;
#endif
    SOCKADDR_IN a;
    DWORD err;
    char *errstr;
    Socket ret;
    extern char *do_select(SOCKET skt, int startup);
    short localport;

    /*
     * Create Socket structure.
     */
    ret = smalloc(sizeof(struct Socket_tag));
    ret->error = NULL;
    ret->receiver = receiver;
    ret->head = ret->tail = NULL;
    ret->writable = 1;		       /* to start with */
    ret->in_oob = FALSE;
    ret->sending_oob = 0;

    /*
     * Open socket.
     */
    s = socket(addr->family, SOCK_STREAM, 0);
    ret->s = s;

    if (s == INVALID_SOCKET) {
	err = WSAGetLastError();
        ret->error = winsock_error_string(err);
	return ret;
    }

    /*
     * Bind to local address.
     */
    if (privport)
        localport = 1023;              /* count from 1023 downwards */
    else
        localport = 0;                 /* just use port 0 (ie winsock picks) */

    /* Loop round trying to bind */
    while (1) {
        int retcode;

#ifdef IPV6
        if (addr->family == AF_INET6)
        {
            memset(&a6,0,sizeof(a6));
            a6.sin6_family	= AF_INET6;
            /*a6.sin6_addr	= in6addr_any;*/  /* == 0 */
            a6.sin6_port	= htons(localport);
        }
        else
        {
#endif
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_ANY);
            a.sin_port = htons(localport);
#ifdef IPV6
        }
        retcode = bind (s, (addr->family == AF_INET6 ?
                            (struct sockaddr *)&a6 :
                            (struct sockaddr *)&a),
                        (addr->family == AF_INET6 ? sizeof(a6) : sizeof(a)));
#else
        retcode = bind (s, (struct sockaddr *)&a, sizeof(a));
#endif
        if (retcode != SOCKET_ERROR) {
            err = 0;
            break;                     /* done */
        } else {
            err = WSAGetLastError();
            if (err != WSAEADDRINUSE)  /* failed, for a bad reason */
                break;
        }

        if (localport == 0)
            break;                     /* we're only looping once */
        localport--;
        if (localport == 0)
            break;                     /* we might have got to the end */
    }

    if (err)
    {
	ret->error = winsock_error_string(err);
	return ret;
    }

    /*
     * Connect to remote address.
     */
#ifdef IPV6
    if (addr->family == AF_INET6)
    {
	memset(&a,0,sizeof(a));
	a6.sin6_family = AF_INET6;
	a6.sin6_port = htons((short)port);
	a6.sin6_addr = ((struct sockaddr_in6 *)addr->ai->ai_addr)->sin6_addr;
    }
    else
    {
#endif
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(addr->address);
	a.sin_port = htons((short)port);
#ifdef IPV6
    }
    if (connect (s, (addr->family == AF_INET6) ? (struct sockaddr *)&a6 : (struct sockaddr *)&a, (addr->family == AF_INET6) ? sizeof(a6) : sizeof(a)) == SOCKET_ERROR)
#else
	if (connect (s, (struct sockaddr *)&a, sizeof(a)) == SOCKET_ERROR)
#endif
    {
	err = WSAGetLastError();
	ret->error = winsock_error_string(err);
	return ret;
    }

    /* Set up a select mechanism. This could be an AsyncSelect on a
     * window, or an EventSelect on an event object. */
    errstr = do_select(s, 1);
    if (errstr) {
	ret->error = errstr;
	return ret;
    }

    add234(sktree, ret);

    return ret;
}

void sk_close(Socket s) {
    del234(sktree, s);
    do_select(s->s, 0);
    closesocket(s->s);
    sfree(s);
}

/*
 * The function which tries to send on a socket once it's deemed
 * writable.
 */
void try_send(Socket s) {
    while (s->head) {
	int nsent;
	DWORD err;
        int len, urgentflag;

        if (s->sending_oob) {
            urgentflag = MSG_OOB;
            len = s->sending_oob;
        } else {
            urgentflag = 0;
            len = s->head->buflen - s->head->bufpos;
        }

	nsent = send(s->s, s->head->buf + s->head->bufpos, len, urgentflag);
        noise_ultralight(nsent);
	if (nsent <= 0) {
	    err = (nsent < 0 ? WSAGetLastError() : 0);
	    if (err == WSAEWOULDBLOCK) {
		/* Perfectly normal: we've sent all we can for the moment. */
		s->writable = FALSE;
                return;
	    } else if (nsent == 0 ||
		       err == WSAECONNABORTED ||
		       err == WSAECONNRESET) {
                /*
                 * FIXME. This will have to be done better when we
                 * start managing multiple sockets (e.g. SSH port
                 * forwarding), because if we get CONNRESET while
                 * trying to write a particular forwarded socket
                 * then it isn't necessarily the end of the world.
                 * Ideally I'd like to pass the error code back to
                 * somewhere the next select_result() will see it,
                 * but that might be hard. Perhaps I should pass it
                 * back to be queued in the Windows front end bit.
                 */
                fatalbox(winsock_error_string(err));
	    } else {
		fatalbox(winsock_error_string(err));
	    }
	} else {
	    s->head->bufpos += nsent;
            if (s->sending_oob)
                s->sending_oob -= nsent;
	    if (s->head->bufpos >= s->head->buflen) {
		struct buffer *tmp = s->head;
		s->head = tmp->next;
		sfree(tmp);
		if (!s->head)
		    s->tail = NULL;
	    }
	}
    }
}

void sk_write(Socket s, char *buf, int len) {
    /*
     * Add the data to the buffer list on the socket.
     */
    if (s->tail && s->tail->buflen < BUFFER_GRANULE) {
	int copylen = min(len, BUFFER_GRANULE - s->tail->buflen);
	memcpy(s->tail->buf + s->tail->buflen, buf, copylen);
	buf += copylen;
	len -= copylen;
	s->tail->buflen += copylen;
    }
    while (len > 0) {
	int grainlen = min(len, BUFFER_GRANULE);
	struct buffer *newbuf;
	newbuf = smalloc(sizeof(struct buffer));
	newbuf->bufpos = 0;
	newbuf->buflen = grainlen;
	memcpy(newbuf->buf, buf, grainlen);
	buf += grainlen;
	len -= grainlen;
	if (s->tail)
	    s->tail->next = newbuf;
	else
	    s->head = s->tail = newbuf;
	newbuf->next = NULL;
	s->tail = newbuf;
    }

    /*
     * Now try sending from the start of the buffer list.
     */
    if (s->writable)
	try_send(s);
}

void sk_write_oob(Socket s, char *buf, int len) {
    /*
     * Replace the buffer list on the socket with the data.
     */
    if (!s->head) {
	s->head = smalloc(sizeof(struct buffer));
    } else {
        struct buffer *walk = s->head->next;
        while (walk) {
            struct buffer *tmp = walk;
            walk = tmp->next;
            sfree(tmp);
        }
    }
    s->head->next = NULL;
    s->tail = s->head;
    s->head->buflen = len;
    memcpy(s->head->buf, buf, len);

    /*
     * Set the Urgent marker.
     */
    s->sending_oob = len;

    /*
     * Now try sending from the start of the buffer list.
     */
    if (s->writable)
	try_send(s);
}

int select_result(WPARAM wParam, LPARAM lParam) {
    int ret;
    DWORD err;
    char buf[BUFFER_GRANULE];
    Socket s;
    u_long atmark;

    /* wParam is the socket itself */
    s = find234(sktree, (void *)wParam, cmpforsearch);
    if (!s)
	return 1;		       /* boggle */

    if ((err = WSAGETSELECTERROR(lParam)) != 0) {
	fatalbox(winsock_error_string(err));
    }

    noise_ultralight(lParam);

    switch (WSAGETSELECTEVENT(lParam)) {
      case FD_READ:
	ret = recv(s->s, buf, sizeof(buf), 0);
	if (ret < 0) {
	    err = WSAGetLastError();
	    if (err == WSAEWOULDBLOCK) {
		break;
	    }
	}
	if (ret < 0) {
	    fatalbox(winsock_error_string(err));
	} else {
            int type = s->in_oob ? 2 : 0;
            s->in_oob = FALSE;
	    return s->receiver(s, type, buf, ret);
	}
	break;
      case FD_OOB:
	/*
	 * Read all data up to the OOB marker, and send it to the
	 * receiver with urgent==1 (OOB pending).
	 */
        atmark = 1;
        s->in_oob = TRUE;
        /* Some WinSock wrappers don't support this call, so we
         * deliberately don't check the return value. If the call
         * fails and does nothing, we will get back atmark==1,
         * which is good enough to keep going at least. */
        ioctlsocket(s->s, SIOCATMARK, &atmark);
        ret = recv(s->s, buf, sizeof(buf), MSG_OOB);
        noise_ultralight(ret);
        if (ret <= 0) {
            fatalbox(ret == 0 ? "Internal networking trouble" :
                     winsock_error_string(WSAGetLastError()));
        } else {
            return s->receiver(s, atmark ? 2 : 1, buf, ret);
        }
        break;
      case FD_WRITE:
	s->writable = 1;
	try_send(s);
	break;
      case FD_CLOSE:
	/* Signal a close on the socket. */
	return s->receiver(s, 0, NULL, 0);
	break;
    }

    return 1;
}

/*
 * Each socket abstraction contains a `void *' private field in
 * which the client can keep state.
 */
void sk_set_private_ptr(Socket s, void *ptr) {
    s->private_ptr = ptr;
}
void *sk_get_private_ptr(Socket s) {
    return s->private_ptr;
}

/*
 * Special error values are returned from sk_namelookup and sk_new
 * if there's a problem. These functions extract an error message,
 * or return NULL if there's no problem.
 */
char *sk_addr_error(SockAddr addr) {
    return addr->error;
}
char *sk_socket_error(Socket s) {
    return s->error;
}

/*
 * For Plink: enumerate all sockets currently active.
 */
SOCKET first_socket(enum234 *e) {
    Socket s = first234(sktree, e);
    return s ? s->s : INVALID_SOCKET;
}
SOCKET next_socket(enum234 *e) {
    Socket s = next234(e);
    return s ? s->s : INVALID_SOCKET;
}
