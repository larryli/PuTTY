#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <winsock.h>

#include "putty.h"
#include "ssh.h"
#include "scp.h"

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#define logevent(s) { logevent(s); \
                      if (IS_SCP && (scp_flags & SCP_VERBOSE) != 0) \
                      fprintf(stderr, "%s\n", s); }

#define SSH_MSG_DISCONNECT	1
#define SSH_SMSG_PUBLIC_KEY	2
#define SSH_CMSG_SESSION_KEY	3
#define SSH_CMSG_USER		4
#define SSH_CMSG_AUTH_PASSWORD	9
#define SSH_CMSG_REQUEST_PTY	10
#define SSH_CMSG_WINDOW_SIZE	11
#define SSH_CMSG_EXEC_SHELL	12
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
#define SSH_CMSG_AUTH_TIS	39
#define SSH_SMSG_AUTH_TIS_CHALLENGE	40
#define SSH_CMSG_AUTH_TIS_RESPONSE	41

#define SSH_AUTH_TIS		5

#define GET_32BIT(cp) \
    (((unsigned long)(unsigned char)(cp)[0] << 24) | \
    ((unsigned long)(unsigned char)(cp)[1] << 16) | \
    ((unsigned long)(unsigned char)(cp)[2] << 8) | \
    ((unsigned long)(unsigned char)(cp)[3]))

#define PUT_32BIT(cp, value) { \
    (cp)[0] = (unsigned char)((value) >> 24); \
    (cp)[1] = (unsigned char)((value) >> 16); \
    (cp)[2] = (unsigned char)((value) >> 8); \
    (cp)[3] = (unsigned char)(value); }

enum { PKT_END, PKT_INT, PKT_CHAR, PKT_DATA, PKT_STR };

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
#define crWaitUntil(c)	do { crReturn(0); } while (!(c))

static SOCKET s = INVALID_SOCKET;

static unsigned char session_key[32];
static struct ssh_cipher *cipher = NULL;
int scp_flags = 0;
int (*ssh_get_password)(const char *prompt, char *str, int maxlen) = NULL;

static char *savedhost;

static enum {
    SSH_STATE_BEFORE_SIZE,
    SSH_STATE_INTERMED,
    SSH_STATE_SESSION,
    SSH_STATE_CLOSED
} ssh_state = SSH_STATE_BEFORE_SIZE;

static int size_needed = FALSE;

static void s_write (char *buf, int len) {
    while (len > 0) {
	int i = send (s, buf, len, 0);
	if (IS_SCP) {
	    noise_ultralight(i);
	    if (i <= 0)
		fatalbox("Lost connection while sending");
	}
	if (i > 0)
	    len -= i, buf += i;
    }
}

static int s_read (char *buf, int len) {
    int ret = 0;
    while (len > 0) {
	int i = recv (s, buf, len, 0);
	if (IS_SCP)
	    noise_ultralight(i);
	if (i > 0)
	    len -= i, buf += i, ret += i;
	else
	    return i;
    }
    return ret;
}

static void c_write (char *buf, int len) {
    if (IS_SCP) {
	if (len > 0 && buf[len-1] == '\n') len--;
	if (len > 0 && buf[len-1] == '\r') len--;
	if (len > 0) { fwrite(buf, len, 1, stderr); fputc('\n', stderr); }
	return;
    }
    while (len--) 
        c_write1(*buf++);
}

struct Packet {
    long length;
    int type;
    unsigned char *data;
    unsigned char *body;
    long maxlen;
};

static struct Packet pktin = { 0, 0, NULL, NULL, 0 };
static struct Packet pktout = { 0, 0, NULL, NULL, 0 };

static void ssh_protocol(unsigned char *in, int inlen, int ispkt);
static void ssh_size(void);


/*
 * Collect incoming data in the incoming packet buffer.
 * Decihper and verify the packet when it is completely read.
 * Drop SSH_MSG_DEBUG and SSH_MSG_IGNORE packets.
 * Update the *data and *datalen variables.
 * Return the additional nr of bytes needed, or 0 when
 * a complete packet is available.
 */
static int s_rdpkt(unsigned char **data, int *datalen)
{
    static long len, pad, biglen, to_read;
    static unsigned long realcrc, gotcrc;
    static unsigned char *p;
    static int i;

    crBegin;

next_packet:

    pktin.type = 0;
    pktin.length = 0;

    for (i = len = 0; i < 4; i++) {
	while ((*datalen) == 0)
	    crReturn(4-i);
	len = (len << 8) + **data;
	(*data)++, (*datalen)--;
    }

#ifdef FWHACK
    if (len == 0x52656d6f) {       /* "Remo"te server has closed ... */
        len = 0x300;               /* big enough to carry to end */
    }
#endif

    pad = 8 - (len % 8);
    biglen = len + pad;
    pktin.length = len - 5;

    if (pktin.maxlen < biglen) {
	pktin.maxlen = biglen;
#ifdef MSCRYPTOAPI
	/* Allocate enough buffer space for extra block
	 * for MS CryptEncrypt() */
	pktin.data = (pktin.data == NULL ? malloc(biglen+8) :
	              realloc(pktin.data, biglen+8));
#else
	pktin.data = (pktin.data == NULL ? malloc(biglen) :
	              realloc(pktin.data, biglen));
#endif
	if (!pktin.data)
	    fatalbox("Out of memory");
    }

    to_read = biglen;
    p = pktin.data;
    while (to_read > 0) {
	static int chunk;
	chunk = to_read;
	while ((*datalen) == 0)
	    crReturn(to_read);
	if (chunk > (*datalen))
	    chunk = (*datalen);
	memcpy(p, *data, chunk);
	*data += chunk;
	*datalen -= chunk;
	p += chunk;
	to_read -= chunk;
    }

    if (cipher)
	cipher->decrypt(pktin.data, biglen);

    pktin.type = pktin.data[pad];
    pktin.body = pktin.data + pad + 1;

    realcrc = crc32(pktin.data, biglen-4);
    gotcrc = GET_32BIT(pktin.data+biglen-4);
    if (gotcrc != realcrc) {
	fatalbox("Incorrect CRC received on packet");
    }

    if (pktin.type == SSH_SMSG_STDOUT_DATA ||
        pktin.type == SSH_SMSG_STDERR_DATA ||
        pktin.type == SSH_MSG_DEBUG ||
        pktin.type == SSH_SMSG_AUTH_TIS_CHALLENGE) {
	long strlen = GET_32BIT(pktin.body);
	if (strlen + 4 != pktin.length)
	    fatalbox("Received data packet with bogus string length");
    }

    if (pktin.type == SSH_MSG_DEBUG) {
	/* log debug message */
	char buf[80];
	int strlen = GET_32BIT(pktin.body);
	strcpy(buf, "Remote: ");
	if (strlen > 70) strlen = 70;
	memcpy(buf+8, pktin.body+4, strlen);
	buf[8+strlen] = '\0';
	logevent(buf);
	goto next_packet;
    } else if (pktin.type == SSH_MSG_IGNORE) {
	/* do nothing */
	goto next_packet;
    }

    crFinish(0);
}

static void ssh_gotdata(unsigned char *data, int datalen)
{
    while (datalen > 0) {
	if ( s_rdpkt(&data, &datalen) == 0 ) {
	    ssh_protocol(NULL, 0, 1);
            if (ssh_state == SSH_STATE_CLOSED) {
                return;
            }
        }
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
#ifdef MSCRYPTOAPI
	/* Allocate enough buffer space for extra block
	 * for MS CryptEncrypt() */
	pktout.data = (pktout.data == NULL ? malloc(biglen+12) :
		       realloc(pktout.data, biglen+12));
#else
	pktout.data = (pktout.data == NULL ? malloc(biglen+4) :
		       realloc(pktout.data, biglen+4));
#endif
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

/*
 * Construct a packet with the specified contents and
 * send it to the server.
 */
static void send_packet(int pkttype, ...)
{
    va_list args;
    unsigned char *p, *argp, argchar;
    unsigned long argint;
    int pktlen, argtype, arglen;

    pktlen = 0;
    va_start(args, pkttype);
    while ((argtype = va_arg(args, int)) != PKT_END) {
	switch (argtype) {
	  case PKT_INT:
	    (void) va_arg(args, int);
	    pktlen += 4;
	    break;
	  case PKT_CHAR:
	    (void) va_arg(args, char);
	    pktlen++;
	    break;
	  case PKT_DATA:
	    (void) va_arg(args, unsigned char *);
	    arglen = va_arg(args, int);
	    pktlen += arglen;
	    break;
	  case PKT_STR:
	    argp = va_arg(args, unsigned char *);
	    arglen = strlen(argp);
	    pktlen += 4 + arglen;
	    break;
	  default:
	    assert(0);
	}
    }
    va_end(args);

    s_wrpkt_start(pkttype, pktlen);
    p = pktout.body;

    va_start(args, pkttype);
    while ((argtype = va_arg(args, int)) != PKT_END) {
	switch (argtype) {
	  case PKT_INT:
	    argint = va_arg(args, int);
	    PUT_32BIT(p, argint);
	    p += 4;
	    break;
	  case PKT_CHAR:
	    argchar = va_arg(args, unsigned char);
	    *p = argchar;
	    p++;
	    break;
	  case PKT_DATA:
	    argp = va_arg(args, unsigned char *);
	    arglen = va_arg(args, int);
	    memcpy(p, argp, arglen);
	    p += arglen;
	    break;
	  case PKT_STR:
	    argp = va_arg(args, unsigned char *);
	    arglen = strlen(argp);
	    PUT_32BIT(p, arglen);
	    memcpy(p + 4, argp, arglen);
	    p += 4 + arglen;
	    break;
	}
    }
    va_end(args);

    s_wrpkt();
}


/*
 * Connect to specified host and port.
 * Returns an error message, or NULL on success.
 * Also places the canonical host name into `realhost'.
 */
static char *connect_to_host(char *host, int port, char **realhost)
{
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

    if (port < 0)
	port = 22;		       /* default ssh port */

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

    return NULL;
}

static int ssh_versioncmp(char *a, char *b) {
    char *ae, *be;
    unsigned long av, bv;

    av = strtoul(a, &ae, 10);
    bv = strtoul(b, &be, 10);
    if (av != bv) return (av < bv ? -1 : +1);
    if (*ae == '.') ae++;
    if (*be == '.') be++;
    av = strtoul(ae, &ae, 10);
    bv = strtoul(be, &be, 10);
    if (av != bv) return (av < bv ? -1 : +1);
    return 0;
}

static int do_ssh_init(void) {
    char c, *vsp;
    char version[10];
    char vstring[80];
    char vlog[sizeof(vstring)+20];
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
    strcpy(vstring, "SSH-");
    vsp = vstring+4;
    if (s_read(&c,1) != 1 || c != '-') return 0;
    i = 0;
    while (1) {
	if (s_read(&c,1) != 1)
	    return 0;
	if (vsp < vstring+sizeof(vstring)-1)
	    *vsp++ = c;
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

    *vsp = 0;
    sprintf(vlog, "Server version: %s", vstring);
    vlog[strcspn(vlog, "\r\n")] = '\0';
    logevent(vlog);

    sprintf(vstring, "SSH-%s-PuTTY\n",
	    (ssh_versioncmp(version, "1.5") <= 0 ? version : "1.5"));
    sprintf(vlog, "We claim version: %s", vstring);
    vlog[strcspn(vlog, "\r\n")] = '\0';
    logevent(vlog);
    s_write(vstring, strlen(vstring));
    return 1;
}

/*
 * Handle the key exchange and user authentication phases.
 */
static int do_ssh_login(unsigned char *in, int inlen, int ispkt)
{
    int i, j, len;
    unsigned char session_id[16];
    unsigned char *rsabuf, *keystr1, *keystr2;
    unsigned char cookie[8];
    struct RSAKey servkey, hostkey;
    struct MD5Context md5c;
    static unsigned long supported_ciphers_mask, supported_auths_mask;
    int cipher_type;

    extern struct ssh_cipher ssh_3des;
    extern struct ssh_cipher ssh_des;
    extern struct ssh_cipher ssh_blowfish;

    crBegin;

    if (!ispkt) crWaitUntil(ispkt);

    if (pktin.type != SSH_SMSG_PUBLIC_KEY)
	fatalbox("Public key packet not received");

    logevent("Received public keys");

    memcpy(cookie, pktin.body, 8);

    i = makekey(pktin.body+8, &servkey, &keystr1);
    j = makekey(pktin.body+8+i, &hostkey, &keystr2);

    /*
     * Hash the host key and print the hash in the log box. Just as
     * a last resort in case the registry's host key checking is
     * compromised, we'll allow the user some ability to verify
     * host keys by eye.
     */
    MD5Init(&md5c);
    MD5Update(&md5c, keystr2, hostkey.bytes);
    MD5Final(session_id, &md5c);
    {
	char logmsg[80];
	int i;
	logevent("Host key MD5 is:");
	strcpy(logmsg, "      ");
	for (i = 0; i < 16; i++)
	    sprintf(logmsg+strlen(logmsg), "%02x", session_id[i]);
	logevent(logmsg);
    }

    supported_ciphers_mask = GET_32BIT(pktin.body+12+i+j);
    supported_auths_mask = GET_32BIT(pktin.body+16+i+j);

    MD5Init(&md5c);
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

    /*
     * Verify the host key.
     */
    {
        /*
         * First format the key into a string.
         */
        int len = rsastr_len(&hostkey);
        char *keystr = malloc(len);
        if (!keystr)
            fatalbox("Out of memory");
        rsastr_fmt(keystr, &hostkey);
        verify_ssh_host_key(savedhost, keystr);
        free(keystr);
    }

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

    logevent("Encrypted session key");

    cipher_type = cfg.cipher == CIPHER_BLOWFISH ? SSH_CIPHER_BLOWFISH :
                  cfg.cipher == CIPHER_DES ? SSH_CIPHER_DES : 
                  SSH_CIPHER_3DES;
    if ((supported_ciphers_mask & (1 << cipher_type)) == 0) {
	c_write("Selected cipher not supported, falling back to 3DES\r\n", 53);
	cipher_type = SSH_CIPHER_3DES;
    }
    switch (cipher_type) {
      case SSH_CIPHER_3DES: logevent("Using 3DES encryption"); break;
      case SSH_CIPHER_DES: logevent("Using single-DES encryption"); break;
      case SSH_CIPHER_BLOWFISH: logevent("Using Blowfish encryption"); break;
    }

    send_packet(SSH_CMSG_SESSION_KEY,
                PKT_CHAR, cipher_type,
                PKT_DATA, cookie, 8,
                PKT_CHAR, (len*8) >> 8, PKT_CHAR, (len*8) & 0xFF,
                PKT_DATA, rsabuf, len,
                PKT_INT, 0,
                PKT_END);

    logevent("Trying to enable encryption...");

    free(rsabuf);

    cipher = cipher_type == SSH_CIPHER_BLOWFISH ? &ssh_blowfish :
             cipher_type == SSH_CIPHER_DES ? &ssh_des :
             &ssh_3des;
    cipher->sesskey(session_key);

    crWaitUntil(ispkt);

    if (pktin.type != SSH_SMSG_SUCCESS)
	fatalbox("Encryption not successfully enabled");

    logevent("Successfully started encryption");

    fflush(stdout);
    {
	static char username[100];
	static int pos = 0;
	static char c;
	if (!IS_SCP && !*cfg.username) {
	    c_write("login as: ", 10);
	    while (pos >= 0) {
		crWaitUntil(!ispkt);
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
		    if (((c >= ' ' && c <= '~') ||
                         ((unsigned char)c >= 160)) && pos < 40) {
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
	    if (!IS_SCP) {
		sprintf(stuff, "Sent username \"%s\".\r\n", username);
		c_write(stuff, strlen(stuff));
	    }
	}

	send_packet(SSH_CMSG_USER, PKT_STR, username, PKT_END);
	{
	    char userlog[20+sizeof(username)];
	    sprintf(userlog, "Sent username \"%s\"", username);
	    logevent(userlog);
	}
    }

    crWaitUntil(ispkt);

    while (pktin.type == SSH_SMSG_FAILURE) {
	static char password[100];
	static int pos;
	static char c;
        static int pwpkt_type;

        /*
         * Show password prompt, having first obtained it via a TIS
         * exchange if we're doing TIS authentication.
         */
        pwpkt_type = SSH_CMSG_AUTH_PASSWORD;

	if (IS_SCP) {
	    char prompt[200];
	    sprintf(prompt, "%s@%s's password: ", cfg.username, savedhost);
	    if (!ssh_get_password(prompt, password, sizeof(password))) {
                /*
                 * get_password failed to get a password (for
                 * example because one was supplied on the command
                 * line which has already failed to work).
                 * Terminate.
                 */
                logevent("No more passwords to try");
                ssh_state = SSH_STATE_CLOSED;
                crReturn(1);
            }
	} else {

        if (pktin.type == SSH_SMSG_FAILURE &&
            cfg.try_tis_auth &&
            (supported_auths_mask & (1<<SSH_AUTH_TIS))) {
            pwpkt_type = SSH_CMSG_AUTH_TIS_RESPONSE;
	    logevent("Requested TIS authentication");
	    send_packet(SSH_CMSG_AUTH_TIS, PKT_END);
            crWaitUntil(ispkt);
            if (pktin.type != SSH_SMSG_AUTH_TIS_CHALLENGE) {
                logevent("TIS authentication declined");
                c_write("TIS authentication refused.\r\n", 29);
            } else {
                int challengelen = ((pktin.body[0] << 24) |
                                    (pktin.body[1] << 16) |
                                    (pktin.body[2] << 8) |
                                    (pktin.body[3]));
                logevent("Received TIS challenge");
                c_write(pktin.body+4, challengelen);
            }
        }
        if (pwpkt_type == SSH_CMSG_AUTH_PASSWORD)
            c_write("password: ", 10);

	pos = 0;
	while (pos >= 0) {
	    crWaitUntil(!ispkt);
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
		if (((c >= ' ' && c <= '~') ||
                     ((unsigned char)c >= 160)) && pos < 40)
		    password[pos++] = c;
		break;
	    }
	}
	c_write("\r\n", 2);

	}

	send_packet(pwpkt_type, PKT_STR, password, PKT_END);
	logevent("Sent password");
	memset(password, 0, strlen(password));
	crWaitUntil(ispkt);
	if (pktin.type == SSH_SMSG_FAILURE) {
	    c_write("Access denied\r\n", 15);
	    logevent("Authentication refused");
	} else if (pktin.type == SSH_MSG_DISCONNECT) {
	    logevent("Received disconnect request");
            ssh_state = SSH_STATE_CLOSED;
	    crReturn(1);
	} else if (pktin.type != SSH_SMSG_SUCCESS) {
	    fatalbox("Strange packet received, type %d", pktin.type);
	}
    }

    logevent("Authentication successful");

    crFinish(1);
}

static void ssh_protocol(unsigned char *in, int inlen, int ispkt) {
    crBegin;

    random_init();

    while (!do_ssh_login(in, inlen, ispkt)) {
	crReturnV;
    }
    if (ssh_state == SSH_STATE_CLOSED)
        crReturnV;

    if (!cfg.nopty) {
	send_packet(SSH_CMSG_REQUEST_PTY,
	            PKT_STR, cfg.termtype,
	            PKT_INT, rows, PKT_INT, cols,
	            PKT_INT, 0, PKT_INT, 0,
	            PKT_CHAR, 0,
	            PKT_END);
        ssh_state = SSH_STATE_INTERMED;
        do { crReturnV; } while (!ispkt);
        if (pktin.type != SSH_SMSG_SUCCESS && pktin.type != SSH_SMSG_FAILURE) {
            fatalbox("Protocol confusion");
        } else if (pktin.type == SSH_SMSG_FAILURE) {
            c_write("Server refused to allocate pty\r\n", 32);
        }
	logevent("Allocated pty");
    }

    send_packet(SSH_CMSG_EXEC_SHELL, PKT_END);
    logevent("Started session");

    ssh_state = SSH_STATE_SESSION;
    if (size_needed)
	ssh_size();

    while (1) {
	crReturnV;
	if (ispkt) {
	    if (pktin.type == SSH_SMSG_STDOUT_DATA ||
                pktin.type == SSH_SMSG_STDERR_DATA) {
		long len = GET_32BIT(pktin.body);
		c_write(pktin.body+4, len);
	    } else if (pktin.type == SSH_MSG_DISCONNECT) {
                ssh_state = SSH_STATE_CLOSED;
		logevent("Received disconnect request");
	    } else if (pktin.type == SSH_SMSG_SUCCESS) {
		/* may be from EXEC_SHELL on some servers */
	    } else if (pktin.type == SSH_SMSG_FAILURE) {
		/* may be from EXEC_SHELL on some servers
		 * if no pty is available or in other odd cases. Ignore */
	    } else if (pktin.type == SSH_SMSG_EXIT_STATUS) {
		send_packet(SSH_CMSG_EXIT_CONFIRMATION, PKT_END);
	    } else {
		fatalbox("Strange packet received: type %d", pktin.type);
	    }
	} else {
	    send_packet(SSH_CMSG_STDIN_DATA,
	                PKT_INT, inlen, PKT_DATA, in, inlen, PKT_END);
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
 */
static char *ssh_init (HWND hwnd, char *host, int port, char **realhost) {
    char *p;
	
#ifdef MSCRYPTOAPI
    if(crypto_startup() == 0)
	return "Microsoft high encryption pack not installed!";
#endif

    p = connect_to_host(host, port, realhost);
    if (p != NULL)
	return p;

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

    /*
     * Because reading less than the whole of the available pending
     * data can generate an FD_READ event, we need to allow for the
     * possibility that FD_READ may arrive with FD_CLOSE already in
     * the queue; so it's possible that we can get here even with s
     * invalid. If so, we return 1 and don't worry about it.
     */
    if (s == INVALID_SOCKET)
	return 1;

    if (WSAGETSELECTERROR(lParam) != 0)
	return -WSAGETSELECTERROR(lParam);

    switch (WSAGETSELECTEVENT(lParam)) {
      case FD_READ:
      case FD_CLOSE:
	ret = recv(s, buf, sizeof(buf), 0);
	if (ret < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
	    return 1;
	if (ret < 0)		       /* any _other_ error */
	    return -10000-WSAGetLastError();
	if (ret == 0) {
	    s = INVALID_SOCKET;
	    return 0;
	}
	ssh_gotdata (buf, ret);
        if (ssh_state == SSH_STATE_CLOSED) {
            closesocket(s);
            s = INVALID_SOCKET;
            return 0;
        }
	return 1;
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
      case SSH_STATE_CLOSED:
	break;			       /* do nothing */
      case SSH_STATE_INTERMED:
	size_needed = TRUE;	       /* buffer for later */
	break;
      case SSH_STATE_SESSION:
        if (!cfg.nopty) {
	    send_packet(SSH_CMSG_WINDOW_SIZE,
	                PKT_INT, rows, PKT_INT, cols,
	                PKT_INT, 0, PKT_INT, 0, PKT_END);
        }
    }
}

/*
 * (Send Telnet special codes)
 */
static void ssh_special (Telnet_Special code) {
    /* do nothing */
}


/*
 * Read and decrypt one incoming SSH packet.
 * (only used by pSCP)
 */
static void get_packet(void)
{
    unsigned char buf[4096], *p;
    long to_read;
    int len;

    assert(IS_SCP);

    p = NULL;
    len = 0;

    while ((to_read = s_rdpkt(&p, &len)) > 0) {
	if (to_read > sizeof(buf)) to_read = sizeof(buf);
	len = s_read(buf, to_read);
	if (len != to_read) {
	    closesocket(s);
	    s = INVALID_SOCKET;
	    return;
	}
	p = buf;
    }

    assert(len == 0);
}

/*
 * Receive a block of data over the SSH link. Block until
 * all data is available. Return nr of bytes read (0 if lost connection).
 * (only used by pSCP)
 */
int ssh_scp_recv(unsigned char *buf, int len)
{
    static int pending_input_len = 0;
    static unsigned char *pending_input_ptr;
    int to_read = len;

    assert(IS_SCP);

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
		logevent("Received disconnect request");
	} else if (pktin.type == SSH_SMSG_SUCCESS ||
	           pktin.type == SSH_SMSG_FAILURE) {
		/* ignore */
	} else if (pktin.type == SSH_SMSG_EXIT_STATUS) {
	    char logbuf[100];
	    sprintf(logbuf, "Remote exit status: %d", GET_32BIT(pktin.body));
	    logevent(logbuf);
	    send_packet(SSH_CMSG_EXIT_CONFIRMATION, PKT_END);
	    logevent("Closing connection");
	    closesocket(s);
	    s = INVALID_SOCKET;
	} else {
	    fatalbox("Strange packet received: type %d", pktin.type);
	}
    }

    return len;
}

/*
 * Send a block of data over the SSH link.
 * Block until all data is sent.
 * (only used by pSCP)
 */
void ssh_scp_send(unsigned char *buf, int len)
{
    assert(IS_SCP);
    if (s == INVALID_SOCKET)
	return;
    send_packet(SSH_CMSG_STDIN_DATA,
                PKT_INT, len, PKT_DATA, buf, len, PKT_END);
}

/*
 * Send an EOF notification to the server.
 * (only used by pSCP)
 */
void ssh_scp_send_eof(void)
{
    assert(IS_SCP);
    if (s == INVALID_SOCKET)
	return;
    send_packet(SSH_CMSG_EOF, PKT_END);
}

/*
 * Set up the connection, login on the remote host and
 * start execution of a command.
 * Returns an error message, or NULL on success.
 * (only used by pSCP)
 */
char *ssh_scp_init(char *host, int port, char *cmd, char **realhost)
{
    char buf[160], *p;

    assert(IS_SCP);

#ifdef MSCRYPTOAPI
    if (crypto_startup() == 0)
	return "Microsoft high encryption pack not installed!";
#endif

    p = connect_to_host(host, port, realhost);
    if (p != NULL)
	return p;

    random_init();

    if (!do_ssh_init())
	return "Protocol initialisation error";

    /* Exchange keys and login */
    do {
	get_packet();
	if (s == INVALID_SOCKET)
	    return "Connection closed by remote host";
    } while (!do_ssh_login(NULL, 0, 1));

    if (ssh_state == SSH_STATE_CLOSED) {
        closesocket(s);
        s = INVALID_SOCKET;
        return "Session initialisation error";
    }

    /* Execute command */
    sprintf(buf, "Sending command: %.100s", cmd);
    logevent(buf);
    send_packet(SSH_CMSG_EXEC_CMD, PKT_STR, cmd, PKT_END);

    return NULL;
}


Backend ssh_backend = {
    ssh_init,
    ssh_msg,
    ssh_send,
    ssh_size,
    ssh_special
};

