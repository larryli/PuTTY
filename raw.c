#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef AUTO_WINSOCK
#ifdef WINSOCK_TWO
#include <winsock2.h>
#else
#include <winsock.h>
#endif
#endif

#include "putty.h"

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

static SOCKET s = INVALID_SOCKET;

static void raw_size(void);

static int sb_opt, sb_len;
static char *sb_buf = NULL;
static int sb_size = 0;
#define SB_DELTA 1024

static void try_write (void) {
    while (outbuf_head != outbuf_reap) {
	int end = (outbuf_reap < outbuf_head ? outbuf_head : OUTBUF_SIZE);
	int len = end - outbuf_reap;
	int ret;

	ret = send (s, outbuf+outbuf_reap, len, 0);
	if (ret > 0)
	    outbuf_reap = (outbuf_reap + ret) & OUTBUF_MASK;
	if (ret < len)
	    return;
    }
}

static void s_write (void *buf, int len) {
    unsigned char *p = buf;
    while (len--) {
	int new_head = (outbuf_head + 1) & OUTBUF_MASK;
	if (new_head != outbuf_reap) {
	    outbuf[outbuf_head] = *p++;
	    outbuf_head = new_head;
	}
    }
    try_write();
}

static void c_write (char *buf, int len) {
    from_backend(0, buf, len);
}

/*
 * Called to set up the raw connection. Will arrange for
 * WM_NETEVENT messages to be passed to the specified window, whose
 * window procedure should then call raw_msg().
 *
 * Returns an error message, or NULL on success.
 *
 * Also places the canonical host name into `realhost'.
 */
static char *raw_init (HWND hwnd, char *host, int port, char **realhost) {
    SOCKADDR_IN addr;
    struct hostent *h;
    unsigned long a;

    /*
     * Try to find host.
     */
    if ( (a = inet_addr(host)) == (unsigned long) INADDR_NONE) {
	if ( (h = gethostbyname(host)) == NULL)
	    switch (WSAGetLastError()) {
	      case WSAENETDOWN: return "Network is down";
	      case WSAHOST_NOT_FOUND: case WSANO_DATA:
		return "Host does not exist";
	      case WSATRY_AGAIN: return "Host not found";
	      default: return "gethostbyname: unknown error";
	    }
	memcpy (&a, h->h_addr, sizeof(a));
	*realhost = h->h_name;
    } else
	*realhost = host;
    a = ntohl(a);

    if (port < 0)
	port = 23;		       /* default telnet port */

    /*
     * Open socket.
     */
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET)
	switch (WSAGetLastError()) {
	  case WSAENETDOWN: return "Network is down";
	  case WSAEAFNOSUPPORT: return "TCP/IP support not present";
	  default: return "socket(): unknown error";
	}

    /*
     * Bind to local address.
     */
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(0);
    if (bind (s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
	switch (WSAGetLastError()) {
	  case WSAENETDOWN: return "Network is down";
	  default: return "bind(): unknown error";
	}

    /*
     * Connect to remote address.
     */
    addr.sin_addr.s_addr = htonl(a);
    addr.sin_port = htons((short)port);
    if (connect (s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
	switch (WSAGetLastError()) {
	  case WSAENETDOWN: return "Network is down";
	  case WSAECONNREFUSED: return "Connection refused";
	  case WSAENETUNREACH: return "Network is unreachable";
	  case WSAEHOSTUNREACH: return "No route to host";
	  default: return "connect(): unknown error";
	}

    if (hwnd && WSAAsyncSelect (s, hwnd, WM_NETEVENT, FD_READ |
			FD_WRITE | FD_OOB | FD_CLOSE) == SOCKET_ERROR)
	switch (WSAGetLastError()) {
	  case WSAENETDOWN: return "Network is down";
	  default: return "WSAAsyncSelect(): unknown error";
	}

    /*
     * We have no pre-session phase.
     */
    begin_session();

    return NULL;
}

/*
 * Process a WM_NETEVENT message. Will return 0 if the connection
 * has closed, or <0 for a socket error.
 */
static int raw_msg (WPARAM wParam, LPARAM lParam) {
    int ret;
    char buf[256];

    /*
     * Because reading less than the whole of the available pending
     * data can generate an FD_READ event, we need to allow for the
     * possibility that FD_READ may arrive with FD_CLOSE already in
     * the queue; so it's possible that we can get here even with s
     * invalid. If so, we return 1 and don't worry about it.
     */
    if (s == INVALID_SOCKET) {
        closesocket(s);
        s = INVALID_SOCKET;
	return 1;
    }

    if (WSAGETSELECTERROR(lParam) != 0)
	return -WSAGETSELECTERROR(lParam);

    switch (WSAGETSELECTEVENT(lParam)) {
      case FD_READ:
      case FD_CLOSE:
	ret = recv(s, buf, sizeof(buf), 0);
	if (ret < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
	    return 1;
	if (ret < 0) {		       /* any _other_ error */
            closesocket(s);
            s = INVALID_SOCKET;
	    return -10000-WSAGetLastError();
        }
	if (ret == 0) {
	    s = INVALID_SOCKET;
	    return 0;
	}
	c_write( buf, ret );
	return 1;
      case FD_OOB:
	do {
	    ret = recv(s, buf, sizeof(buf), 0);
	    c_write( buf, ret );
	} while (ret > 0);
	do {
	    ret = recv(s, buf, 1, MSG_OOB);
	} while (ret > 0);
	if (ret < 0 && WSAGetLastError() != WSAEWOULDBLOCK)
	    return -30000-WSAGetLastError();
	return 1;
      case FD_WRITE:
	if (outbuf_head != outbuf_reap)
	    try_write();
	return 1;
    }
    return 1;			       /* shouldn't happen, but WTF */
}

/*
 * Called to send data down the raw connection.
 */
static void raw_send (char *buf, int len) {

    if (s == INVALID_SOCKET)
	return;

    s_write( buf, len );
}

/*
 * Called to set the size of the window
 */
static void raw_size(void) {
    /* Do nothing! */
    return;
}

/*
 * Send raw special codes.
 */
static void raw_special (Telnet_Special code) {
    /* Do nothing! */
    return;
}

static SOCKET raw_socket(void) { return s; }

static int raw_sendok(void) { return 1; }

Backend raw_backend = {
    raw_init,
    raw_msg,
    raw_send,
    raw_size,
    raw_special,
    raw_socket,
    raw_sendok,
    1
};
