/*
 * Windows networking abstraction.
 */

#include <windows.h>
#include <winsock.h>
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
    unsigned long address;
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

SockAddr sk_namelookup(char *host, char **canonicalname) {
    SockAddr ret = smalloc(sizeof(struct SockAddr_tag));
    unsigned long a;
    struct hostent *h;

    ret->error = NULL;
    if ( (a = inet_addr(host)) == (unsigned long) INADDR_NONE) {
	if ( (h = gethostbyname(host)) == NULL) {
	    DWORD err = WSAGetLastError();
	    ret->error = (err == WSAENETDOWN ? "Network is down" :
			  err == WSAHOST_NOT_FOUND ? "Host does not exist" :
			  err == WSATRY_AGAIN ? "Host not found" :
			  "gethostbyname: unknown error");
	} else {
	    memcpy (&a, h->h_addr, sizeof(a));
	    *canonicalname = h->h_name;
	}
    } else {
	*canonicalname = host;
    }
    ret->address = ntohl(a);

    return ret;
}

void sk_addr_free(SockAddr addr) {
    sfree(addr);
}

Socket sk_new(SockAddr addr, int port, sk_receiver_t receiver) {
    SOCKET s;
    SOCKADDR_IN a;
    DWORD err;
    char *errstr;
    Socket ret;
    extern char *do_select(SOCKET skt, int startup);

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
    s = socket(AF_INET, SOCK_STREAM, 0);
    ret->s = s;

    if (s == INVALID_SOCKET) {
	err = WSAGetLastError();
	ret->error = (err == WSAENETDOWN ? "Network is down" :
		      err == WSAEAFNOSUPPORT ? "TCP/IP support not present" :
		      "socket(): unknown error");
	return ret;
    }

    /*
     * Bind to local address.
     */
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(0);
    if (bind (s, (struct sockaddr *)&a, sizeof(a)) == SOCKET_ERROR) {
	err = WSAGetLastError();
	ret->error = (err == WSAENETDOWN ? "Network is down" :
		      "bind(): unknown error");
	return ret;
    }

    /*
     * Connect to remote address.
     */
    a.sin_addr.s_addr = htonl(addr->address);
    a.sin_port = htons((short)port);
    if (connect (s, (struct sockaddr *)&a, sizeof(a)) == SOCKET_ERROR) {
	err = WSAGetLastError();
	ret->error = (err == WSAENETDOWN ? "Network is down" :
		      err == WSAECONNREFUSED ? "Connection refused" :
		      err == WSAENETUNREACH ? "Network is unreachable" :
		      err == WSAEHOSTUNREACH ? "No route to host" :
		      "connect(): unknown error");
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
    free(s);
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
		free(tmp);
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
            free(tmp);
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
