/*
 *  scpssh.c  -  SSH implementation for PuTTY Secure Copy
 *  Joris van Rantwijk, Aug 1999.
 *  Based on PuTTY ssh.c by Simon Tatham.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock.h>

#include "putty.h"
#include "ssh.h"
#include "scp.h"

#define SSH_MSG_DISCONNECT	1
#define SSH_SMSG_PUBLIC_KEY	2
#define SSH_CMSG_SESSION_KEY	3
#define SSH_CMSG_USER		4
#define SSH_CMSG_AUTH_PASSWORD	9
#define SSH_CMSG_EXEC_CMD	13
#define SSH_SMSG_SUCCESS	14
#define SSH_SMSG_FAILURE	15
#define SSH_CMSG_STDIN_DATA	16
#define SSH_SMSG_STDOUT_DATA	17
#define SSH_SMSG_STDERR_DATA	18
#define SSH_CMSG_EOF		19
#define SSH_SMSG_EXIT_STATUS	20
#define SSH_CMSG_EXIT_CONFIRMATION	33
#define SSH_MSG_IGNORE		32
#define SSH_MSG_DEBUG		36

#define GET_32BIT(cp) \
    (((unsigned long)(unsigned char)(cp)[0] << 24) | \
    ((unsigned long)(unsigned char)(cp)[1] << 16) | \
    ((unsigned long)(unsigned char)(cp)[2] << 8) | \
    ((unsigned long)(unsigned char)(cp)[3]))

#define PUT_32BIT(cp, value) { \
    (cp)[0] = (value) >> 24; \
    (cp)[1] = (value) >> 16; \
    (cp)[2] = (value) >> 8; \
    (cp)[3] = (value); }

static SOCKET s = INVALID_SOCKET;

static unsigned char session_key[32];
static struct ssh_cipher *cipher = NULL;

static char *savedhost;

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


static void s_write (char *buf, int len) {
    while (len > 0) {
	int i = send (s, buf, len, 0);
	noise_ultralight(i);
	if (i <= 0)
	    fatalbox("Lost connection while sending");
	len -= i, buf += i;
    }
}

static int s_read (char *buf, int len) {
    int ret = 0;
    while (len > 0) {
	int i = recv (s, buf, len, 0);
	noise_ultralight(i);
	if (i > 0)
	    len -= i, buf += i, ret += i;
	else
	    return i;
    }
    return ret;
}

/*
 * Read and decrypt one incoming SSH packet.
 */
static void get_packet()
{
    unsigned char buf[4];
    int ret;
    int len, pad, biglen;

next_packet:

    pktin.type = 0;
    pktin.length = 0;

    ret = s_read(buf, 4);
    if (ret != 4) {
	closesocket(s);
	s = INVALID_SOCKET;
	return;
    }

    len = GET_32BIT(buf);
	
#ifdef FWHACK
    if (len == 0x52656d6f) {
	len = 0x300;
    }
#endif

    pad = 8 - (len % 8);
    biglen = len + pad;
    len -= 5;			/* type and CRC */

    pktin.length = len;
    if (pktin.maxlen < biglen) {
	pktin.maxlen = biglen;
	pktin.data = (pktin.data == NULL) ? 
	             smalloc(biglen) : srealloc(pktin.data, biglen);
    }

    ret = s_read(pktin.data, biglen);
    if (ret != biglen) {
	closesocket(s);
	s = INVALID_SOCKET;
	return;
    }

    if (cipher)
	cipher->decrypt(pktin.data, biglen);

    pktin.type = pktin.data[pad];
    pktin.body = pktin.data + pad + 1;

    if (pktin.type == SSH_MSG_DEBUG) {
	if (verbose) {
	    int len = GET_32BIT(pktin.body);
	    fprintf(stderr, "Remote: ");
	    fwrite(pktin.body + 4, len, 1, stderr);
	    fprintf(stderr, "\n");
	}
	goto next_packet;
    }
    if (pktin.type == SSH_MSG_IGNORE) {
	goto next_packet;
    }
}

static void s_wrpkt_start(int type, int len) {
    int pad, biglen;

    len += 5;			       /* type and CRC */
    pad = 8 - (len%8);
    biglen = len + pad;

    pktout.length = len-5;
    if (pktout.maxlen < biglen) {
	pktout.maxlen = biglen;
	pktout.data = (pktout.data == NULL ? malloc(biglen+4) :
		       realloc(pktout.data, biglen+4));
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
    PUT_32BIT(pktout.data+biglen, crc);
    PUT_32BIT(pktout.data, len);

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


/*
 * Login on the server and request execution of the command.
 */
static void ssh_login(char *username, char *cmd)
{
    int i, j, len;
    unsigned char session_id[16];
    unsigned char *rsabuf, *keystr1, *keystr2;
    unsigned char cookie[8];
    struct RSAKey servkey, hostkey;
    struct MD5Context md5c;
    unsigned long supported_ciphers_mask;
    int cipher_type;

    extern struct ssh_cipher ssh_3des;
    extern struct ssh_cipher ssh_blowfish;

    get_packet();

    if (pktin.type != SSH_SMSG_PUBLIC_KEY)
	fatalbox("Public key packet not received");

    memcpy(cookie, pktin.body, 8);

    MD5Init(&md5c);

    i = makekey(pktin.body+8, &servkey, &keystr1);
    j = makekey(pktin.body+8+i, &hostkey, &keystr2);

    supported_ciphers_mask = GET_32BIT(pktin.body+12+i+j);

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

    cipher_type = cfg.cipher == CIPHER_BLOWFISH ? SSH_CIPHER_BLOWFISH :
                  SSH_CIPHER_3DES;
    if ((supported_ciphers_mask & (1 << cipher_type)) == 0) {
	fprintf(stderr, "Selected cipher not supported, falling back to 3DES\n");
	cipher_type = SSH_CIPHER_3DES;
    }

    s_wrpkt_start(SSH_CMSG_SESSION_KEY, len+15);
    pktout.body[0] = cipher_type;
    memcpy(pktout.body+1, cookie, 8);
    pktout.body[9] = (len*8) >> 8;
    pktout.body[10] = (len*8) & 0xFF;
    memcpy(pktout.body+11, rsabuf, len);
    pktout.body[len+11] = pktout.body[len+12] = 0;   /* protocol flags */
    pktout.body[len+13] = pktout.body[len+14] = 0;
    s_wrpkt();

    free(rsabuf);

    cipher = cipher_type == SSH_CIPHER_BLOWFISH ? &ssh_blowfish :
             &ssh_3des;
    cipher->sesskey(session_key);

    get_packet();

    if (pktin.type != SSH_SMSG_SUCCESS)
	fatalbox("Encryption not successfully enabled");

    if (verbose)
	fprintf(stderr, "Logging in as \"%s\".\n", username);
    s_wrpkt_start(SSH_CMSG_USER, 4+strlen(username));
    pktout.body[0] = pktout.body[1] = pktout.body[2] = 0;
    pktout.body[3] = strlen(username);
    memcpy(pktout.body+4, username, strlen(username));
    s_wrpkt();

    get_packet();

    while (pktin.type == SSH_SMSG_FAILURE) {
	char password[100];
	char prompt[200];
	sprintf(prompt, "%s@%s's password: ", username, savedhost);
	ssh_get_password(prompt, password, 100);
	s_wrpkt_start(SSH_CMSG_AUTH_PASSWORD, 4+strlen(password));
	pktout.body[0] = pktout.body[1] = pktout.body[2] = 0;
	pktout.body[3] = strlen(password);
	memcpy(pktout.body+4, password, strlen(password));
	s_wrpkt();
	memset(password, 0, strlen(password));
	get_packet();
	if (pktin.type == SSH_SMSG_FAILURE) {
	    fprintf(stderr, "Access denied\n");
	} else if (pktin.type != SSH_SMSG_SUCCESS) {
	    fatalbox("Strange packet received, type %d", pktin.type);
	}
    }

    /* Execute command */
    if (verbose)
	fprintf(stderr, "Sending command: %s\n", cmd);
    i = strlen(cmd);
    s_wrpkt_start(SSH_CMSG_EXEC_CMD, 4+i);
    PUT_32BIT(pktout.body, i);
    memcpy(pktout.body+4, cmd, i);
    s_wrpkt();
}


/*
 * Receive a block of data over the SSH link. Block until
 * all data is available. Return nr of bytes read (0 if lost connection).
 */
int ssh_recv(unsigned char *buf, int len)
{
    static int pending_input_len = 0;
    static unsigned char *pending_input_ptr;
    int to_read = len;

    if (pending_input_len >= to_read) {
	memcpy(buf, pending_input_ptr, to_read);
	pending_input_ptr += to_read;
	pending_input_len -= to_read;
	return len;
    }
    
    if (pending_input_len > 0) {
	memcpy(buf, pending_input_ptr, pending_input_len);
	buf += pending_input_len;
	to_read -= pending_input_len;
	pending_input_len = 0;
    }

    if (s == INVALID_SOCKET)
	return 0;
    while (to_read > 0) {
	get_packet();
	if (s == INVALID_SOCKET)
	    return 0;
	if (pktin.type == SSH_SMSG_STDOUT_DATA) {
	    int plen = GET_32BIT(pktin.body);
	    if (plen <= to_read) {
		memcpy(buf, pktin.body + 4, plen);
		buf += plen;
		to_read -= plen;
	    } else {
		memcpy(buf, pktin.body + 4, to_read);
		pending_input_len = plen - to_read;
		pending_input_ptr = pktin.body + 4 + to_read;
		to_read = 0;
	    }
	} else if (pktin.type == SSH_SMSG_STDERR_DATA) {
	    int plen = GET_32BIT(pktin.body);
	    fwrite(pktin.body + 4, plen, 1, stderr);
	} else if (pktin.type == SSH_MSG_DISCONNECT) {
	} else if (pktin.type == SSH_SMSG_SUCCESS ||
	           pktin.type == SSH_SMSG_FAILURE) {
	} else if (pktin.type == SSH_SMSG_EXIT_STATUS) {
	    if (verbose)
		fprintf(stderr, "Remote exit status %d\n",
		        GET_32BIT(pktin.body));
	    s_wrpkt_start(SSH_CMSG_EXIT_CONFIRMATION, 0);
	    s_wrpkt();
	    if (verbose)
		fprintf(stderr, "Closing connection\n");
	    closesocket(s);
	    s = INVALID_SOCKET;
	}
    }

    return len;
}


/*
 * Send a block of data over the SSH link.
 * Block until all data is sent.
 */
void ssh_send(unsigned char *buf, int len)
{
    if (s == INVALID_SOCKET)
	return;
    s_wrpkt_start(SSH_CMSG_STDIN_DATA, 4 + len);
    PUT_32BIT(pktout.body, len);
    memcpy(pktout.body + 4, buf, len);
    s_wrpkt();
}


/*
 * Send an EOF notification to the server.
 */
void ssh_send_eof(void)
{
    if (s == INVALID_SOCKET)
	return;
    s_wrpkt_start(SSH_CMSG_EOF, 0);
    s_wrpkt();
}


/*
 * Set up the connection, login on the remote host and
 * start execution of a command.
 *
 * Returns an error message, or NULL on success.
 *
 * Also places the canonical host name into `realhost'.
 */
char *ssh_init(char *host, int port, char *cmd, char **realhost) {
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

    random_init();

    if (!do_ssh_init())
	return "Protocol initialisation error";

    ssh_login(cfg.username, cmd);

    return NULL;
}

/* end */
