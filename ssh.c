#include <stdio.h>
#include <stdlib.h>
#include <winsock.h>

#include "putty.h"

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#include "ssh.h"

/* Coroutine mechanics for the sillier bits of the code */
#define crBegin1	static int crLine = 0;
#define crBegin2	switch(crLine) { case 0:;
#define crBegin		crBegin1; crBegin2;
#define crFinish(z)	} crLine = 0; return (z)
#define crFinishV	} crLine = 0; return
#define crReturn(z)	\
	do {\
	    crLine=__LINE__; return (z); case __LINE__:;\
	} while (0)
#define crReturnV	\
	do {\
	    crLine=__LINE__; return; case __LINE__:;\
	} while (0)
#define crStop(z)	do{ crLine = 0; return (z); }while(0)
#define crStopV		do{ crLine = 0; return; }while(0)

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

static SOCKET s = INVALID_SOCKET;

static unsigned char session_key[32];
static struct ssh_cipher *cipher = NULL;

static char *savedhost;

static enum {
    SSH_STATE_BEFORE_SIZE,
    SSH_STATE_INTERMED,
    SSH_STATE_SESSION
} ssh_state = SSH_STATE_BEFORE_SIZE;

static int size_needed = FALSE;

static void s_write (char *buf, int len) {
    while (len > 0) {
	int i = send (s, buf, len, 0);
	if (i > 0)
	    len -= i, buf += i;
    }
}

static int s_read (char *buf, int len) {
    int ret = 0;
    while (len > 0) {
	int i = recv (s, buf, len, 0);
	if (i > 0)
	    len -= i, buf += i, ret += i;
	else
	    return i;
    }
    return ret;
}

static void c_write (char *buf, int len) {
    while (len--) {
	int new_head = (inbuf_head + 1) & INBUF_MASK;
	int c = (unsigned char) *buf;
	if (new_head != inbuf_reap) {
	    inbuf[inbuf_head] = *buf++;
	    inbuf_head = new_head;
	}
    }
}

struct Packet {
    long length;
    int type;
    unsigned long crc;
    unsigned char *data;
    unsigned char *body;
    long maxlen;
};

static struct Packet pktin = { 0, 0, 0, NULL, 0 };
static struct Packet pktout = { 0, 0, 0, NULL, 0 };

static void ssh_protocol(unsigned char *in, int inlen, int ispkt);
static void ssh_size(void);

static void ssh_gotdata(unsigned char *data, int datalen) {
    static long len, biglen, to_read;
    static unsigned char c, *p;
    static int i, pad;
    static char padding[8];
    static unsigned char word[4];

    crBegin;
    while (1) {
	for (i = len = 0; i < 4; i++) {
	    while (datalen == 0)
		crReturnV;
	    len = (len << 8) + *data;
	    data++, datalen--;
	}

#ifdef FWHACK
        if (len == 0x52656d6f) {       /* "Remo"te server has closed ... */
            len = 0x300;               /* big enough to carry to end */
        }
#endif

	pad = 8 - (len%8);

	biglen = len + pad;

	len -= 5;		       /* type and CRC */

	pktin.length = len;
	if (pktin.maxlen < biglen) {
	    pktin.maxlen = biglen;
	    pktin.data = (pktin.data == NULL ? malloc(biglen) :
			realloc(pktin.data, biglen));
	    if (!pktin.data)
		fatalbox("Out of memory");
	}

	p = pktin.data, to_read = biglen;
	while (to_read > 0) {
	    static int chunk;
	    chunk = to_read;
	    while (datalen == 0)
		crReturnV;
	    if (chunk > datalen)
		chunk = datalen;
	    memcpy(p, data, chunk);
	    data += chunk;
	    datalen -= chunk;
	    p += chunk;
	    to_read -= chunk;
	}

	if (cipher)
	    cipher->decrypt(pktin.data, biglen);

	pktin.type = pktin.data[pad];
	pktin.body = pktin.data+pad+1;

	if (pktin.type == 36) {	       /* SSH_MSG_DEBUG */
	    /* FIXME: log it */
	} else
	    ssh_protocol(NULL, 0, 1);
    }
    crFinishV;
}

static void s_wrpkt_start(int type, int len) {
    int pad, biglen;

    len += 5;			       /* type and CRC */
    pad = 8 - (len%8);
    biglen = len + pad;

    pktout.length = len-5;
    if (pktout.maxlen < biglen) {
	pktout.maxlen = biglen;
	pktout.data = (pktout.data == NULL ? malloc(biglen) :
		       realloc(pktout.data, biglen));
	if (!pktout.data)
	    fatalbox("Out of memory");
    }

    pktout.type = type;
    pktout.body = pktout.data+4+pad+1;
}

static void s_wrpkt(void) {
    int pad, len, biglen, i;
    unsigned long crc;

    len = pktout.length + 5;	       /* type and CRC */
    pad = 8 - (len%8);
    biglen = len + pad;

    pktout.body[-1] = pktout.type;
    for (i=0; i<pad; i++)
	pktout.data[i+4] = random_byte();
    crc = crc32(pktout.data+4, biglen-4);

    pktout.data[biglen+0] = (unsigned char) ((crc >> 24) & 0xFF);
    pktout.data[biglen+1] = (unsigned char) ((crc >> 16) & 0xFF);
    pktout.data[biglen+2] = (unsigned char) ((crc >> 8) & 0xFF);
    pktout.data[biglen+3] = (unsigned char) (crc & 0xFF);

    pktout.data[0] = (len >> 24) & 0xFF;
    pktout.data[1] = (len >> 16) & 0xFF;
    pktout.data[2] = (len >> 8) & 0xFF;
    pktout.data[3] = len & 0xFF;

    if (cipher)
	cipher->encrypt(pktout.data+4, biglen);

    s_write(pktout.data, biglen+4);
}

static int do_ssh_init(void) {
    char c;
    char version[10];
    char vstring[40];
    int i;

#ifdef FWHACK
    i = 0;
    while (s_read(&c, 1) == 1) {
	if (c == 'S' && i < 2) i++;
	else if (c == 'S' && i == 2) i = 2;
	else if (c == 'H' && i == 2) break;
	else i = 0;
    }
#else
    if (s_read(&c,1) != 1 || c != 'S') return 0;
    if (s_read(&c,1) != 1 || c != 'S') return 0;
    if (s_read(&c,1) != 1 || c != 'H') return 0;
#endif
    if (s_read(&c,1) != 1 || c != '-') return 0;
    i = 0;
    while (1) {
	if (s_read(&c,1) != 1)
	    return 0;
	if (i >= 0) {
	    if (c == '-') {
		version[i] = '\0';
		i = -1;
	    } else if (i < sizeof(version)-1)
		version[i++] = c;
	}
	else if (c == '\n')
	    break;
    }

    sprintf(vstring, "SSH-%s-7.7.7\n",
	    (strcmp(version, "1.5") <= 0 ? version : "1.5"));
    s_write(vstring, strlen(vstring));
    return 1;
}

static void ssh_protocol(unsigned char *in, int inlen, int ispkt) {
    int i, j, len;
    unsigned char session_id[16];
    unsigned char *rsabuf, *keystr1, *keystr2;
    unsigned char cookie[8];
    struct RSAKey servkey, hostkey;
    struct MD5Context md5c;

    extern struct ssh_cipher ssh_3des;

    crBegin;

    random_init();

    while (!ispkt)
	crReturnV;

    if (pktin.type != 2)
	fatalbox("Public key packet not received");

    memcpy(cookie, pktin.body, 8);

    MD5Init(&md5c);

    i = makekey(pktin.body+8, &servkey, &keystr1);

    j = makekey(pktin.body+8+i, &hostkey, &keystr2);

    MD5Update(&md5c, keystr2, hostkey.bytes);
    MD5Update(&md5c, keystr1, servkey.bytes);
    MD5Update(&md5c, pktin.body, 8);

    MD5Final(session_id, &md5c);

    for (i=0; i<32; i++)
	session_key[i] = random_byte();

    len = (hostkey.bytes > servkey.bytes ? hostkey.bytes : servkey.bytes);

    rsabuf = malloc(len);
    if (!rsabuf)
	fatalbox("Out of memory");

    verify_ssh_host_key(savedhost, &hostkey);

    for (i=0; i<32; i++) {
	rsabuf[i] = session_key[i];
	if (i < 16)
	    rsabuf[i] ^= session_id[i];
    }

    if (hostkey.bytes > servkey.bytes) {
	rsaencrypt(rsabuf, 32, &servkey);
	rsaencrypt(rsabuf, servkey.bytes, &hostkey);
    } else {
	rsaencrypt(rsabuf, 32, &hostkey);
	rsaencrypt(rsabuf, hostkey.bytes, &servkey);
    }

    s_wrpkt_start(3, len+15);
    pktout.body[0] = 3;		       /* SSH_CIPHER_3DES */
    memcpy(pktout.body+1, cookie, 8);
    pktout.body[9] = (len*8) >> 8;
    pktout.body[10] = (len*8) & 0xFF;
    memcpy(pktout.body+11, rsabuf, len);
    pktout.body[len+11] = pktout.body[len+12] = 0;   /* protocol flags */
    pktout.body[len+13] = pktout.body[len+14] = 0;
    s_wrpkt();

    free(rsabuf);

    cipher = &ssh_3des;
    cipher->sesskey(session_key);

    do { crReturnV; } while (!ispkt);

    if (pktin.type != 14)
	fatalbox("Encryption not successfully enabled");

    fflush(stdout);
    {
	static char username[100];
	static int pos = 0;
	static char c;
	if (!*cfg.username) {
	    c_write("login as: ", 10);
	    while (pos >= 0) {
		do { crReturnV; } while (ispkt);
		while (inlen--) switch (c = *in++) {
		  case 10: case 13:
		    username[pos] = 0;
		    pos = -1;
		    break;
		  case 8: case 127:
		    if (pos > 0) {
			c_write("\b \b", 3);
			pos--;
		    }
		    break;
		  case 21: case 27:
		    while (pos > 0) {
			c_write("\b \b", 3);
			pos--;
		    }
		    break;
		  case 3: case 4:
		    random_save_seed();
		    exit(0);
		    break;
		  default:
		    if (c >= ' ' && c <= '~' && pos < 40) {
			username[pos++] = c;
			c_write(&c, 1);
		    }
		    break;
		}
	    }
	    c_write("\r\n", 2);
	    username[strcspn(username, "\n\r")] = '\0';
	} else {
	    char stuff[200];
	    strncpy(username, cfg.username, 99);
	    username[99] = '\0';
	    sprintf(stuff, "Sent username \"%s\".\r\n", username);
	    c_write(stuff, strlen(stuff));
	}
	s_wrpkt_start(4, 4+strlen(username));
	pktout.body[0] = pktout.body[1] = pktout.body[2] = 0;
	pktout.body[3] = strlen(username);
	memcpy(pktout.body+4, username, strlen(username));
	s_wrpkt();
    }

    do { crReturnV; } while (!ispkt);

    while (pktin.type == 15) {
	static char password[100];
	static int pos;
	static char c;
	c_write("password: ", 10);
	pos = 0;
	while (pos >= 0) {
	    do { crReturnV; } while (ispkt);
	    while (inlen--) switch (c = *in++) {
	      case 10: case 13:
		password[pos] = 0;
		pos = -1;
		break;
	      case 8: case 127:
		if (pos > 0)
		    pos--;
		break;
	      case 21: case 27:
		pos = 0;
		break;
	      case 3: case 4:
		random_save_seed();
		exit(0);
		break;
	      default:
		if (c >= ' ' && c <= '~' && pos < 40)
		    password[pos++] = c;
		break;
	    }
	}
	c_write("\r\n", 2);
	s_wrpkt_start(9, 4+strlen(password));
	pktout.body[0] = pktout.body[1] = pktout.body[2] = 0;
	pktout.body[3] = strlen(password);
	memcpy(pktout.body+4, password, strlen(password));
	s_wrpkt();
	memset(password, 0, strlen(password));
	do { crReturnV; } while (!ispkt);
	if (pktin.type == 15) {
	    c_write("Access denied\r\n", 15);
	} else if (pktin.type != 14) {
	    fatalbox("Strange packet received, type %d", pktin.type);
	}
    }

    if (!cfg.nopty) {
        i = strlen(cfg.termtype);
        s_wrpkt_start(10, i+5*4+1);
        pktout.body[0] = (i >> 24) & 0xFF;
        pktout.body[1] = (i >> 16) & 0xFF;
        pktout.body[2] = (i >> 8) & 0xFF;
        pktout.body[3] = i & 0xFF;
        memcpy(pktout.body+4, cfg.termtype, i);
        i += 4;
        pktout.body[i++] = (rows >> 24) & 0xFF;
        pktout.body[i++] = (rows >> 16) & 0xFF;
        pktout.body[i++] = (rows >> 8) & 0xFF;
        pktout.body[i++] = rows & 0xFF;
        pktout.body[i++] = (cols >> 24) & 0xFF;
        pktout.body[i++] = (cols >> 16) & 0xFF;
        pktout.body[i++] = (cols >> 8) & 0xFF;
        pktout.body[i++] = cols & 0xFF;
        memset(pktout.body+i, 0, 9);       /* 0 pixwidth, 0 pixheight, 0.b endofopt */
        s_wrpkt();
        ssh_state = SSH_STATE_INTERMED;
        do { crReturnV; } while (!ispkt);
        if (pktin.type != 14 && pktin.type != 15) {
            fatalbox("Protocol confusion");
        } else if (pktin.type == 15) {
            c_write("Server refused to allocate pty\r\n", 32);
        }
    }

    s_wrpkt_start(12, 0);
    s_wrpkt();

    ssh_state = SSH_STATE_SESSION;
    if (size_needed)
	ssh_size();

    while (1) {
	crReturnV;
	if (ispkt) {
	    if (pktin.type == 17 || pktin.type == 18) {
		long len = 0;
		for (i = 0; i < 4; i++)
		    len = (len << 8) + pktin.body[i];
		c_write(pktin.body+4, len);
	    } else if (pktin.type == 1) {
		/* SSH_MSG_DISCONNECT: do nothing */
	    } else if (pktin.type == 14) {
		/* SSH_MSG_SUCCESS: may be from EXEC_SHELL on some servers */
	    } else if (pktin.type == 15) {
		/* SSH_MSG_FAILURE: may be from EXEC_SHELL on some servers
		 * if no pty is available or in other odd cases. Ignore */
	    } else if (pktin.type == 20) {
		/* EXITSTATUS */
		s_wrpkt_start(33, 0);
		s_wrpkt();
	    } else {
		fatalbox("Strange packet received: type %d", pktin.type);
	    }
	} else {
	    s_wrpkt_start(16, 4+inlen);
	    pktout.body[0] = (inlen >> 24) & 0xFF;
	    pktout.body[1] = (inlen >> 16) & 0xFF;
	    pktout.body[2] = (inlen >> 8) & 0xFF;
	    pktout.body[3] = inlen & 0xFF;
	    memcpy(pktout.body+4, in, inlen);
	    s_wrpkt();
	}
    }

    crFinishV;
}

/*
 * Called to set up the connection. Will arrange for WM_NETEVENT
 * messages to be passed to the specified window, whose window
 * procedure should then call telnet_msg().
 *
 * Returns an error message, or NULL on success.
 *
 * Also places the canonical host name into `realhost'.
 */
static char *ssh_init (HWND hwnd, char *host, int port, char **realhost) {
    SOCKADDR_IN addr;
    struct hostent *h;
    unsigned long a;
#ifdef FWHACK
    char *FWhost;
    int FWport;
#endif

    savedhost = malloc(1+strlen(host));
    if (!savedhost)
	fatalbox("Out of memory");
    strcpy(savedhost, host);

#ifdef FWHACK
    FWhost = host;
    FWport = port;
    host = FWSTR;
    port = 23;
#endif

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
#ifdef FWHACK
    *realhost = FWhost;
#endif
    a = ntohl(a);

    if (port < 0)
	port = 22;		       /* default ssh port */

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

#ifdef FWHACK
    send(s, "connect ", 8, 0);
    send(s, FWhost, strlen(FWhost), 0);
    {
	char buf[20];
	sprintf(buf, " %d\n", FWport);
	send (s, buf, strlen(buf), 0);
    }
#endif

    if (!do_ssh_init())
	return "Protocol initialisation error";

    if (WSAAsyncSelect (s, hwnd, WM_NETEVENT, FD_READ | FD_CLOSE) == SOCKET_ERROR)
	switch (WSAGetLastError()) {
	  case WSAENETDOWN: return "Network is down";
	  default: return "WSAAsyncSelect(): unknown error";
	}

    return NULL;
}

/*
 * Process a WM_NETEVENT message. Will return 0 if the connection
 * has closed, or <0 for a socket error.
 */
static int ssh_msg (WPARAM wParam, LPARAM lParam) {
    int ret;
    char buf[256];

    if (s == INVALID_SOCKET)	       /* how the hell did we get here?! */
	return -5000;

    if (WSAGETSELECTERROR(lParam) != 0)
	return -WSAGETSELECTERROR(lParam);

    switch (WSAGETSELECTEVENT(lParam)) {
      case FD_READ:
	ret = recv(s, buf, sizeof(buf), 0);
	if (ret < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
	    return 1;
	if (ret < 0)		       /* any _other_ error */
	    return -10000-WSAGetLastError();
	if (ret == 0) {
	    s = INVALID_SOCKET;
	    return 0;		       /* can't happen, in theory */
	}
	ssh_gotdata (buf, ret);
	return 1;
      case FD_CLOSE:
	s = INVALID_SOCKET;
	return 0;
    }
    return 1;			       /* shouldn't happen, but WTF */
}

/*
 * Called to send data down the Telnet connection.
 */
static void ssh_send (char *buf, int len) {
    if (s == INVALID_SOCKET)
	return;

    ssh_protocol(buf, len, 0);
}

/*
 * Called to set the size of the window from Telnet's POV.
 */
static void ssh_size(void) {
    switch (ssh_state) {
      case SSH_STATE_BEFORE_SIZE:
	break;			       /* do nothing */
      case SSH_STATE_INTERMED:
	size_needed = TRUE;	       /* buffer for later */
	break;
      case SSH_STATE_SESSION:
        if (!cfg.nopty) {
            s_wrpkt_start(11, 16);
            pktout.body[0] = (rows >> 24) & 0xFF;
            pktout.body[1] = (rows >> 16) & 0xFF;
            pktout.body[2] = (rows >> 8) & 0xFF;
            pktout.body[3] = rows & 0xFF;
            pktout.body[4] = (cols >> 24) & 0xFF;
            pktout.body[5] = (cols >> 16) & 0xFF;
            pktout.body[6] = (cols >> 8) & 0xFF;
            pktout.body[7] = cols & 0xFF;
            memset(pktout.body+8, 0, 8);
            s_wrpkt();
        }
    }
}

/*
 * (Send Telnet special codes)
 */
static void ssh_special (Telnet_Special code) {
    /* do nothing */
}

Backend ssh_backend = {
    ssh_init,
    ssh_msg,
    ssh_send,
    ssh_size,
    ssh_special
};
