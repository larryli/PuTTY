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
                      if (!(flags & FLAG_CONNECTION) && (flags & FLAG_VERBOSE)) \
                      fprintf(stderr, "%s\n", s); }

#define SSH1_MSG_DISCONNECT	1
#define SSH1_SMSG_PUBLIC_KEY	2
#define SSH1_CMSG_SESSION_KEY	3
#define SSH1_CMSG_USER		4
#define SSH1_CMSG_AUTH_RSA      6
#define SSH1_SMSG_AUTH_RSA_CHALLENGE 7
#define SSH1_CMSG_AUTH_RSA_RESPONSE 8
#define SSH1_CMSG_AUTH_PASSWORD	9
#define SSH1_CMSG_REQUEST_PTY	10
#define SSH1_CMSG_WINDOW_SIZE	11
#define SSH1_CMSG_EXEC_SHELL	12
#define SSH1_CMSG_EXEC_CMD	13
#define SSH1_SMSG_SUCCESS	14
#define SSH1_SMSG_FAILURE	15
#define SSH1_CMSG_STDIN_DATA	16
#define SSH1_SMSG_STDOUT_DATA	17
#define SSH1_SMSG_STDERR_DATA	18
#define SSH1_CMSG_EOF		19
#define SSH1_SMSG_EXIT_STATUS	20
#define SSH1_CMSG_EXIT_CONFIRMATION	33
#define SSH1_MSG_IGNORE		32
#define SSH1_MSG_DEBUG		36
#define SSH1_CMSG_AUTH_TIS	39
#define SSH1_SMSG_AUTH_TIS_CHALLENGE	40
#define SSH1_CMSG_AUTH_TIS_RESPONSE	41
#define SSH1_CMSG_AUTH_CCARD	70
#define SSH1_SMSG_AUTH_CCARD_CHALLENGE	71
#define SSH1_CMSG_AUTH_CCARD_RESPONSE	72

#define SSH1_AUTH_TIS		5
#define SSH1_AUTH_CCARD		16

#define SSH2_MSG_DISCONNECT             1
#define SSH2_MSG_IGNORE                 2
#define SSH2_MSG_UNIMPLEMENTED          3
#define SSH2_MSG_DEBUG                  4
#define SSH2_MSG_SERVICE_REQUEST        5
#define SSH2_MSG_SERVICE_ACCEPT         6
#define SSH2_MSG_KEXINIT                20
#define SSH2_MSG_NEWKEYS                21
#define SSH2_MSG_KEXDH_INIT             30
#define SSH2_MSG_KEXDH_REPLY            31
#define SSH2_MSG_USERAUTH_REQUEST            50
#define SSH2_MSG_USERAUTH_FAILURE            51
#define SSH2_MSG_USERAUTH_SUCCESS            52
#define SSH2_MSG_USERAUTH_BANNER             53
#define SSH2_MSG_USERAUTH_PK_OK              60
#define SSH2_MSG_USERAUTH_PASSWD_CHANGEREQ   60
#define SSH2_MSG_GLOBAL_REQUEST                  80
#define SSH2_MSG_REQUEST_SUCCESS                 81
#define SSH2_MSG_REQUEST_FAILURE                 82
#define SSH2_MSG_CHANNEL_OPEN                    90
#define SSH2_MSG_CHANNEL_OPEN_CONFIRMATION       91
#define SSH2_MSG_CHANNEL_OPEN_FAILURE            92
#define SSH2_MSG_CHANNEL_WINDOW_ADJUST           93
#define SSH2_MSG_CHANNEL_DATA                    94
#define SSH2_MSG_CHANNEL_EXTENDED_DATA           95
#define SSH2_MSG_CHANNEL_EOF                     96
#define SSH2_MSG_CHANNEL_CLOSE                   97
#define SSH2_MSG_CHANNEL_REQUEST                 98
#define SSH2_MSG_CHANNEL_SUCCESS                 99
#define SSH2_MSG_CHANNEL_FAILURE                 100

#define SSH2_OPEN_ADMINISTRATIVELY_PROHIBITED    1
#define SSH2_OPEN_CONNECT_FAILED                 2
#define SSH2_OPEN_UNKNOWN_CHANNEL_TYPE           3
#define SSH2_OPEN_RESOURCE_SHORTAGE              4
#define SSH2_EXTENDED_DATA_STDERR                1

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

enum { PKT_END, PKT_INT, PKT_CHAR, PKT_DATA, PKT_STR, PKT_BIGNUM };

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
#define crWaitUntilV(c)	do { crReturnV; } while (!(c))

extern struct ssh_cipher ssh_3des;
extern struct ssh_cipher ssh_3des_ssh2;
extern struct ssh_cipher ssh_des;
extern struct ssh_cipher ssh_blowfish;

/* for ssh 2; we miss out single-DES because it isn't supported */
struct ssh_cipher *ciphers[] = { &ssh_3des_ssh2, &ssh_blowfish };

extern struct ssh_kex ssh_diffiehellman;
struct ssh_kex *kex_algs[] = { &ssh_diffiehellman };

extern struct ssh_hostkey ssh_dss;
struct ssh_hostkey *hostkey_algs[] = { &ssh_dss };

extern struct ssh_mac ssh_sha1;

SHA_State exhash;

static void nullmac_key(unsigned char *key) { }
static void nullmac_generate(unsigned char *blk, int len, unsigned long seq) { }
static int nullmac_verify(unsigned char *blk, int len, unsigned long seq) { return 1; }
struct ssh_mac ssh_mac_none = {
    nullmac_key, nullmac_key, nullmac_generate, nullmac_verify, "none", 0
};
struct ssh_mac *macs[] = { &ssh_sha1, &ssh_mac_none };

struct ssh_compress ssh_comp_none = {
    "none"
};
struct ssh_compress *compressions[] = { &ssh_comp_none };

static SOCKET s = INVALID_SOCKET;

static unsigned char session_key[32];
static struct ssh_cipher *cipher = NULL;
static struct ssh_cipher *cscipher = NULL;
static struct ssh_cipher *sccipher = NULL;
static struct ssh_mac *csmac = NULL;
static struct ssh_mac *scmac = NULL;
static struct ssh_compress *cscomp = NULL;
static struct ssh_compress *sccomp = NULL;
static struct ssh_kex *kex = NULL;
static struct ssh_hostkey *hostkey = NULL;
int (*ssh_get_password)(const char *prompt, char *str, int maxlen) = NULL;

static char *savedhost;
static int ssh_send_ok;

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
        noise_ultralight(i);
        if (i <= 0)
            fatalbox("Lost connection while sending");
	if (i > 0)
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

static void c_write (char *buf, int len) {
    if (!(flags & FLAG_CONNECTION)) {
        int i;
        for (i = 0; i < len; i++)
            if (buf[i] != '\r')
                fputc(buf[i], stderr);
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
    long savedpos;
    long maxlen;
};

static struct Packet pktin = { 0, 0, NULL, NULL, 0 };
static struct Packet pktout = { 0, 0, NULL, NULL, 0 };

static int ssh_version;
static void (*ssh_protocol)(unsigned char *in, int inlen, int ispkt);
static void ssh1_protocol(unsigned char *in, int inlen, int ispkt);
static void ssh2_protocol(unsigned char *in, int inlen, int ispkt);
static void ssh_size(void);

static int (*s_rdpkt)(unsigned char **data, int *datalen);

/*
 * Collect incoming data in the incoming packet buffer.
 * Decipher and verify the packet when it is completely read.
 * Drop SSH1_MSG_DEBUG and SSH1_MSG_IGNORE packets.
 * Update the *data and *datalen variables.
 * Return the additional nr of bytes needed, or 0 when
 * a complete packet is available.
 */
static int ssh1_rdpkt(unsigned char **data, int *datalen)
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
	pktin.data = (pktin.data == NULL ? malloc(biglen+APIEXTRA) :
	              realloc(pktin.data, biglen+APIEXTRA));
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

    if (pktin.type == SSH1_SMSG_STDOUT_DATA ||
        pktin.type == SSH1_SMSG_STDERR_DATA ||
        pktin.type == SSH1_MSG_DEBUG ||
        pktin.type == SSH1_SMSG_AUTH_TIS_CHALLENGE ||
        pktin.type == SSH1_SMSG_AUTH_CCARD_CHALLENGE) {
	long strlen = GET_32BIT(pktin.body);
	if (strlen + 4 != pktin.length)
	    fatalbox("Received data packet with bogus string length");
    }

    if (pktin.type == SSH1_MSG_DEBUG) {
	/* log debug message */
	char buf[80];
	int strlen = GET_32BIT(pktin.body);
	strcpy(buf, "Remote: ");
	if (strlen > 70) strlen = 70;
	memcpy(buf+8, pktin.body+4, strlen);
	buf[8+strlen] = '\0';
	logevent(buf);
	goto next_packet;
    } else if (pktin.type == SSH1_MSG_IGNORE) {
	/* do nothing */
	goto next_packet;
    }

    crFinish(0);
}

static int ssh2_rdpkt(unsigned char **data, int *datalen)
{
    static long len, pad, payload, packetlen, maclen;
    static int i;
    static int cipherblk;
    static unsigned long incoming_sequence = 0;

    crBegin;

next_packet:

    pktin.type = 0;
    pktin.length = 0;

    if (cipher)
        cipherblk = cipher->blksize;
    else
        cipherblk = 8;
    if (cipherblk < 8)
        cipherblk = 8;

    if (pktin.maxlen < cipherblk) {
	pktin.maxlen = cipherblk;
	pktin.data = (pktin.data == NULL ? malloc(cipherblk+APIEXTRA) :
	              realloc(pktin.data, cipherblk+APIEXTRA));
	if (!pktin.data)
	    fatalbox("Out of memory");
    }

    /*
     * Acquire and decrypt the first block of the packet. This will
     * contain the length and padding details.
     */
     for (i = len = 0; i < cipherblk; i++) {
	while ((*datalen) == 0)
	    crReturn(cipherblk-i);
	pktin.data[i] = *(*data)++;
        (*datalen)--;
    }
#ifdef FWHACK
    if (!memcmp(pktin.data, "Remo", 4)) {/* "Remo"te server has closed ... */
        /* FIXME */
    }
#endif
    if (sccipher)
        sccipher->decrypt(pktin.data, cipherblk);

    /*
     * Now get the length and padding figures.
     */
    len = GET_32BIT(pktin.data);
    pad = pktin.data[4];

    /*
     * This enables us to deduce the payload length.
     */
    payload = len - pad - 1;

    pktin.length = payload + 5;

    /*
     * So now we can work out the total packet length.
     */
    packetlen = len + 4;
    maclen = scmac ? scmac->len : 0;

    /*
     * Adjust memory allocation if packet is too big.
     */
    if (pktin.maxlen < packetlen) {
	pktin.maxlen = packetlen;
	pktin.data = (pktin.data == NULL ? malloc(packetlen+APIEXTRA) :
	              realloc(pktin.data, packetlen+APIEXTRA));
	if (!pktin.data)
	    fatalbox("Out of memory");
    }

    /*
     * Read and decrypt the remainder of the packet.
     */
    for (i = cipherblk; i < packetlen + maclen; i++) {
	while ((*datalen) == 0)
	    crReturn(packetlen + maclen - i);
	pktin.data[i] = *(*data)++;
        (*datalen)--;
    }
    /* Decrypt everything _except_ the MAC. */
    if (sccipher)
        sccipher->decrypt(pktin.data + cipherblk, packetlen - cipherblk);

#if 0
    debug(("Got packet len=%d pad=%d\r\n", len, pad));
    for (i = 0; i < packetlen; i++)
        debug(("  %02x", (unsigned char)pktin.data[i]));
    debug(("\r\n"));
#endif

    /*
     * Check the MAC.
     */
    if (scmac && !scmac->verify(pktin.data, len+4, incoming_sequence))
	fatalbox("Incorrect MAC received on packet");
    incoming_sequence++;               /* whether or not we MACed */

    pktin.savedpos = 6;
    pktin.type = pktin.data[5];

    if (pktin.type == SSH2_MSG_IGNORE || pktin.type == SSH2_MSG_DEBUG)
        goto next_packet;              /* FIXME: print DEBUG message */

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
    Bignum bn;
    int i;

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
	  case PKT_BIGNUM:
	    bn = va_arg(args, Bignum);
            i = 16 * bn[0] - 1;
            while ( i > 0 && (bn[i/16+1] >> (i%16)) == 0 )
                i--;
            pktlen += 2 + (i+7)/8;
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
	  case PKT_BIGNUM:
	    bn = va_arg(args, Bignum);
            i = 16 * bn[0] - 1;
            while ( i > 0 && (bn[i/16+1] >> (i%16)) == 0 )
                i--;
            *p++ = (i >> 8) & 0xFF;
            *p++ = i & 0xFF;
            i = (i + 7) / 8;
            while (i-- > 0) {
                if (i % 2)
                    *p++ = bn[i/2+1] >> 8;
                else
                    *p++ = bn[i/2+1] & 0xFF;
            }
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


/*
 * Utility routine for putting an SSH-protocol `string' into a SHA
 * state.
 */
#include <stdio.h>
void sha_string(SHA_State *s, void *str, int len) {
    unsigned char lenblk[4];
    PUT_32BIT(lenblk, len);
    SHA_Bytes(s, lenblk, 4);
    SHA_Bytes(s, str, len);
}

/*
 * SSH2 packet construction functions.
 */
void ssh2_pkt_adddata(void *data, int len) {
    pktout.length += len;
    if (pktout.maxlen < pktout.length) {
        pktout.maxlen = pktout.length + 256;
	pktout.data = (pktout.data == NULL ? malloc(pktout.maxlen+APIEXTRA) :
                       realloc(pktout.data, pktout.maxlen+APIEXTRA));
        if (!pktout.data)
            fatalbox("Out of memory");
    }
    memcpy(pktout.data+pktout.length-len, data, len);
}
void ssh2_pkt_addbyte(unsigned char byte) {
    ssh2_pkt_adddata(&byte, 1);
}
void ssh2_pkt_init(int pkt_type) {
    pktout.length = 5;
    ssh2_pkt_addbyte((unsigned char)pkt_type);
}
void ssh2_pkt_addbool(unsigned char value) {
    ssh2_pkt_adddata(&value, 1);
}
void ssh2_pkt_adduint32(unsigned long value) {
    unsigned char x[4];
    PUT_32BIT(x, value);
    ssh2_pkt_adddata(x, 4);
}
void ssh2_pkt_addstring_start(void) {
    ssh2_pkt_adduint32(0);
    pktout.savedpos = pktout.length;
}
void ssh2_pkt_addstring_str(char *data) {
    ssh2_pkt_adddata(data, strlen(data));
    PUT_32BIT(pktout.data + pktout.savedpos - 4,
              pktout.length - pktout.savedpos);
}
void ssh2_pkt_addstring_data(char *data, int len) {
    ssh2_pkt_adddata(data, len);
    PUT_32BIT(pktout.data + pktout.savedpos - 4,
              pktout.length - pktout.savedpos);
}
void ssh2_pkt_addstring(char *data) {
    ssh2_pkt_addstring_start();
    ssh2_pkt_addstring_str(data);
}
char *ssh2_mpint_fmt(Bignum b, int *len) {
    unsigned char *p;
    int i, n = b[0];
    p = malloc(n * 2 + 1);
    if (!p)
        fatalbox("out of memory");
    p[0] = 0;
    for (i = 0; i < n; i++) {
        p[i*2+1] = (b[n-i] >> 8) & 0xFF;
        p[i*2+2] = (b[n-i]     ) & 0xFF;
    }
    i = 0;
    while (p[i] == 0 && (p[i+1] & 0x80) == 0)
        i++;
    memmove(p, p+i, n*2+1-i);
    *len = n*2+1-i;
    return p;
}
void ssh2_pkt_addmp(Bignum b) {
    unsigned char *p;
    int len;
    p = ssh2_mpint_fmt(b, &len);
    ssh2_pkt_addstring_start();
    ssh2_pkt_addstring_data(p, len);
    free(p);
}
void ssh2_pkt_send(void) {
    int cipherblk, maclen, padding, i;
    static unsigned long outgoing_sequence = 0;

    /*
     * Add padding. At least four bytes, and must also bring total
     * length (minus MAC) up to a multiple of the block size.
     */
    cipherblk = cipher ? cipher->blksize : 8;   /* block size */
    cipherblk = cipherblk < 8 ? 8 : cipherblk;   /* or 8 if blksize < 8 */
    padding = 4;
    padding += (cipherblk - (pktout.length + padding) % cipherblk) % cipherblk;
    pktout.data[4] = padding;
    for (i = 0; i < padding; i++)
        pktout.data[pktout.length + i] = random_byte();
    PUT_32BIT(pktout.data, pktout.length + padding - 4);
    if (csmac)
        csmac->generate(pktout.data, pktout.length + padding,
                        outgoing_sequence);
    outgoing_sequence++;               /* whether or not we MACed */

#if 0
    debug(("Sending packet len=%d\r\n", pktout.length+padding));
    for (i = 0; i < pktout.length+padding; i++)
        debug(("  %02x", (unsigned char)pktout.data[i]));
    debug(("\r\n"));
#endif

    if (cscipher)
        cscipher->encrypt(pktout.data, pktout.length + padding);
    maclen = csmac ? csmac->len : 0;

    s_write(pktout.data, pktout.length + padding + maclen);
}

#if 0
void bndebug(char *string, Bignum b) {
    unsigned char *p;
    int i, len;
    p = ssh2_mpint_fmt(b, &len);
    debug(("%s", string));
    for (i = 0; i < len; i++)
        debug((" %02x", p[i]));
    debug(("\r\n"));
    free(p);
}
#endif

void sha_mpint(SHA_State *s, Bignum b) {
    unsigned char *p;
    int len;
    p = ssh2_mpint_fmt(b, &len);
    sha_string(s, p, len);
    free(p);
}

/*
 * SSH2 packet decode functions.
 */
unsigned long ssh2_pkt_getuint32(void) {
    unsigned long value;
    if (pktin.length - pktin.savedpos < 4)
        return 0;                      /* arrgh, no way to decline (FIXME?) */
    value = GET_32BIT(pktin.data+pktin.savedpos);
    pktin.savedpos += 4;
    return value;
}
void ssh2_pkt_getstring(char **p, int *length) {
    *p = NULL;
    if (pktin.length - pktin.savedpos < 4)
        return;
    *length = GET_32BIT(pktin.data+pktin.savedpos);
    pktin.savedpos += 4;
    if (pktin.length - pktin.savedpos < *length)
        return;
    *p = pktin.data+pktin.savedpos;
    pktin.savedpos += *length;
}
Bignum ssh2_pkt_getmp(void) {
    char *p;
    int i, j, length;
    Bignum b;

    ssh2_pkt_getstring(&p, &length);
    if (!p)
        return NULL;
    if (p[0] & 0x80)
        fatalbox("internal error: Can't handle negative mpints");
    b = newbn((length+1)/2);
    for (i = 0; i < length; i++) {
        j = length - 1 - i;
        if (j & 1)
            b[j/2+1] |= ((unsigned char)p[i]) << 8;
        else
            b[j/2+1] |= ((unsigned char)p[i]);
    }
    return b;
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

    /*
     * Server version "1.99" means we can choose whether we use v1
     * or v2 protocol. Choice is based on cfg.sshprot.
     */
    if (ssh_versioncmp(version, cfg.sshprot == 1 ? "2.0" : "1.99") >= 0) {
        /*
         * This is a v2 server. Begin v2 protocol.
         */
        char *verstring = "SSH-2.0-PuTTY";
        SHA_Init(&exhash);
        /*
         * Hash our version string and their version string.
         */
        sha_string(&exhash, verstring, strlen(verstring));
        sha_string(&exhash, vstring, strcspn(vstring, "\r\n"));
        sprintf(vstring, "%s\n", verstring);
        sprintf(vlog, "We claim version: %s", verstring);
        logevent(vlog);
        logevent("Using SSH protocol version 2");
        s_write(vstring, strlen(vstring));
        ssh_protocol = ssh2_protocol;
        ssh_version = 2;
        s_rdpkt = ssh2_rdpkt;
    } else {
        /*
         * This is a v1 server. Begin v1 protocol.
         */
        sprintf(vstring, "SSH-%s-PuTTY\n",
                (ssh_versioncmp(version, "1.5") <= 0 ? version : "1.5"));
        sprintf(vlog, "We claim version: %s", vstring);
        vlog[strcspn(vlog, "\r\n")] = '\0';
        logevent(vlog);
        logevent("Using SSH protocol version 1");
        s_write(vstring, strlen(vstring));
        ssh_protocol = ssh1_protocol;
        ssh_version = 1;
        s_rdpkt = ssh1_rdpkt;
    }
    ssh_send_ok = 0;
    return 1;
}

/*
 * Handle the key exchange and user authentication phases.
 */
static int do_ssh1_login(unsigned char *in, int inlen, int ispkt)
{
    int i, j, len;
    unsigned char *rsabuf, *keystr1, *keystr2;
    unsigned char cookie[8];
    struct RSAKey servkey, hostkey;
    struct MD5Context md5c;
    static unsigned long supported_ciphers_mask, supported_auths_mask;
    static int tried_publickey;
    static unsigned char session_id[16];
    int cipher_type;

    crBegin;

    if (!ispkt) crWaitUntil(ispkt);

    if (pktin.type != SSH1_SMSG_PUBLIC_KEY)
	fatalbox("Public key packet not received");

    logevent("Received public keys");

    memcpy(cookie, pktin.body, 8);

    i = makekey(pktin.body+8, &servkey, &keystr1, 0);
    j = makekey(pktin.body+8+i, &hostkey, &keystr2, 0);

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

    send_packet(SSH1_CMSG_SESSION_KEY,
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

    if (pktin.type != SSH1_SMSG_SUCCESS)
	fatalbox("Encryption not successfully enabled");

    logevent("Successfully started encryption");

    fflush(stdout);
    {
	static char username[100];
	static int pos = 0;
	static char c;
	if (!(flags & FLAG_CONNECTION) && !*cfg.username) {
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
            if (flags & FLAG_VERBOSE) {
		sprintf(stuff, "Sent username \"%s\".\r\n", username);
                c_write(stuff, strlen(stuff));
	    }
	}

	send_packet(SSH1_CMSG_USER, PKT_STR, username, PKT_END);
	{
	    char userlog[20+sizeof(username)];
	    sprintf(userlog, "Sent username \"%s\"", username);
	    logevent(userlog);
	}
    }

    crWaitUntil(ispkt);

    tried_publickey = 0;

    while (pktin.type == SSH1_SMSG_FAILURE) {
	static char password[100];
	static int pos;
	static char c;
        static int pwpkt_type;
        /*
         * Show password prompt, having first obtained it via a TIS
         * or CryptoCard exchange if we're doing TIS or CryptoCard
         * authentication.
         */
        pwpkt_type = SSH1_CMSG_AUTH_PASSWORD;
        if (*cfg.keyfile && !tried_publickey)
            pwpkt_type = SSH1_CMSG_AUTH_RSA;

	if (pwpkt_type == SSH1_CMSG_AUTH_PASSWORD && !FLAG_WINDOWED) {
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

            if (pktin.type == SSH1_SMSG_FAILURE &&
                cfg.try_tis_auth &&
                (supported_auths_mask & (1<<SSH1_AUTH_TIS))) {
                pwpkt_type = SSH1_CMSG_AUTH_TIS_RESPONSE;
                logevent("Requested TIS authentication");
                send_packet(SSH1_CMSG_AUTH_TIS, PKT_END);
                crWaitUntil(ispkt);
                if (pktin.type != SSH1_SMSG_AUTH_TIS_CHALLENGE) {
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
            if (pktin.type == SSH1_SMSG_FAILURE &&
                cfg.try_tis_auth &&
                (supported_auths_mask & (1<<SSH1_AUTH_CCARD))) {
                pwpkt_type = SSH1_CMSG_AUTH_CCARD_RESPONSE;
                logevent("Requested CryptoCard authentication");
                send_packet(SSH1_CMSG_AUTH_CCARD, PKT_END);
                crWaitUntil(ispkt);
                if (pktin.type != SSH1_SMSG_AUTH_CCARD_CHALLENGE) {
                    logevent("CryptoCard authentication declined");
                    c_write("CryptoCard authentication refused.\r\n", 29);
                } else {
                    int challengelen = ((pktin.body[0] << 24) |
                                        (pktin.body[1] << 16) |
                                        (pktin.body[2] << 8) |
                                        (pktin.body[3]));
                    logevent("Received CryptoCard challenge");
                    c_write(pktin.body+4, challengelen);
                    c_write("\r\nResponse : ", 13);
                }
            }
            if (pwpkt_type == SSH1_CMSG_AUTH_PASSWORD)
                c_write("password: ", 10);
            if (pwpkt_type == SSH1_CMSG_AUTH_RSA) {
                if (flags & FLAG_VERBOSE)
                    c_write("Trying public key authentication.\r\n", 35);
                if (!rsakey_encrypted(cfg.keyfile)) {
                    if (flags & FLAG_VERBOSE)
                        c_write("No passphrase required.\r\n", 25);
                    goto tryauth;
                }
                c_write("passphrase: ", 12);
            }

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
                         ((unsigned char)c >= 160)) && pos < sizeof(password))
                        password[pos++] = c;
                    break;
                }
            }
            c_write("\r\n", 2);

	}

        tryauth:
	if (pwpkt_type == SSH1_CMSG_AUTH_RSA) {
            /*
             * Try public key authentication with the specified
             * key file.
             */
            static struct RSAKey pubkey;
            static Bignum challenge, response;
            static int i;
            static unsigned char buffer[32];

            tried_publickey = 1;
            i = loadrsakey(cfg.keyfile, &pubkey, password);
            if (i == 0) {
                c_write("Couldn't load public key from ", 30);
                c_write(cfg.keyfile, strlen(cfg.keyfile));
                c_write(".\r\n", 3);
                continue;              /* go and try password */
            }
            if (i == -1) {
                c_write("Wrong passphrase.\r\n", 19);
                tried_publickey = 0;
                continue;              /* try again */
            }

            /*
             * Send a public key attempt.
             */
            send_packet(SSH1_CMSG_AUTH_RSA,
                        PKT_BIGNUM, pubkey.modulus, PKT_END);

            crWaitUntil(ispkt);
            if (pktin.type == SSH1_SMSG_FAILURE) {
                if (flags & FLAG_VERBOSE)
                    c_write("Server refused our public key.\r\n", 32);
                continue;              /* go and try password */
            }
            if (pktin.type != SSH1_SMSG_AUTH_RSA_CHALLENGE)
                fatalbox("Bizarre response to offer of public key");
            ssh1_read_bignum(pktin.body, &challenge);
            response = rsadecrypt(challenge, &pubkey);
            freebn(pubkey.private_exponent);   /* burn the evidence */

            for (i = 0; i < 32; i += 2) {
                buffer[i] = response[16-i/2] >> 8;
                buffer[i+1] = response[16-i/2] & 0xFF;
            }

            MD5Init(&md5c);
            MD5Update(&md5c, buffer, 32);
            MD5Update(&md5c, session_id, 16);
            MD5Final(buffer, &md5c);

            send_packet(SSH1_CMSG_AUTH_RSA_RESPONSE,
                        PKT_DATA, buffer, 16, PKT_END);

            crWaitUntil(ispkt);
            if (pktin.type == SSH1_SMSG_FAILURE) {
                if (flags & FLAG_VERBOSE)
                    c_write("Failed to authenticate with our public key.\r\n",
                            45);
                continue;              /* go and try password */
            } else if (pktin.type != SSH1_SMSG_SUCCESS) {
                fatalbox("Bizarre response to RSA authentication response");
            }

            break;                     /* we're through! */
        } else {
            send_packet(pwpkt_type, PKT_STR, password, PKT_END);
        }
	logevent("Sent password");
	memset(password, 0, strlen(password));
	crWaitUntil(ispkt);
	if (pktin.type == SSH1_SMSG_FAILURE) {
            if (flags & FLAG_VERBOSE)
                c_write("Access denied\r\n", 15);
	    logevent("Authentication refused");
	} else if (pktin.type == SSH1_MSG_DISCONNECT) {
	    logevent("Received disconnect request");
            ssh_state = SSH_STATE_CLOSED;
	    crReturn(1);
	} else if (pktin.type != SSH1_SMSG_SUCCESS) {
	    fatalbox("Strange packet received, type %d", pktin.type);
	}
    }

    logevent("Authentication successful");

    crFinish(1);
}

static void ssh1_protocol(unsigned char *in, int inlen, int ispkt) {
    crBegin;

    random_init();

    while (!do_ssh1_login(in, inlen, ispkt)) {
	crReturnV;
    }
    if (ssh_state == SSH_STATE_CLOSED)
        crReturnV;

    if (!cfg.nopty) {
	send_packet(SSH1_CMSG_REQUEST_PTY,
	            PKT_STR, cfg.termtype,
	            PKT_INT, rows, PKT_INT, cols,
	            PKT_INT, 0, PKT_INT, 0,
	            PKT_CHAR, 0,
	            PKT_END);
        ssh_state = SSH_STATE_INTERMED;
        do { crReturnV; } while (!ispkt);
        if (pktin.type != SSH1_SMSG_SUCCESS && pktin.type != SSH1_SMSG_FAILURE) {
            fatalbox("Protocol confusion");
        } else if (pktin.type == SSH1_SMSG_FAILURE) {
            c_write("Server refused to allocate pty\r\n", 32);
        }
	logevent("Allocated pty");
    }

    if (*cfg.remote_cmd)
        send_packet(SSH1_CMSG_EXEC_CMD, PKT_STR, cfg.remote_cmd, PKT_END);
    else
        send_packet(SSH1_CMSG_EXEC_SHELL, PKT_END);
    logevent("Started session");

    ssh_state = SSH_STATE_SESSION;
    if (size_needed)
	ssh_size();

    ssh_send_ok = 1;
    while (1) {
	crReturnV;
	if (ispkt) {
	    if (pktin.type == SSH1_SMSG_STDOUT_DATA ||
                pktin.type == SSH1_SMSG_STDERR_DATA) {
		long len = GET_32BIT(pktin.body);
		c_write(pktin.body+4, len);
	    } else if (pktin.type == SSH1_MSG_DISCONNECT) {
                ssh_state = SSH_STATE_CLOSED;
		logevent("Received disconnect request");
	    } else if (pktin.type == SSH1_SMSG_SUCCESS) {
		/* may be from EXEC_SHELL on some servers */
	    } else if (pktin.type == SSH1_SMSG_FAILURE) {
		/* may be from EXEC_SHELL on some servers
		 * if no pty is available or in other odd cases. Ignore */
	    } else if (pktin.type == SSH1_SMSG_EXIT_STATUS) {
		send_packet(SSH1_CMSG_EXIT_CONFIRMATION, PKT_END);
	    } else {
		fatalbox("Strange packet received: type %d", pktin.type);
	    }
	} else {
	    send_packet(SSH1_CMSG_STDIN_DATA,
	                PKT_INT, inlen, PKT_DATA, in, inlen, PKT_END);
	}
    }

    crFinishV;
}

/*
 * Utility routine for decoding comma-separated strings in KEXINIT.
 */
int in_commasep_string(char *needle, char *haystack, int haylen) {
    int needlen = strlen(needle);
    while (1) {
        /*
         * Is it at the start of the string?
         */
        if (haylen >= needlen &&       /* haystack is long enough */
            !memcmp(needle, haystack, needlen) &&    /* initial match */
            (haylen == needlen || haystack[needlen] == ',')
                                       /* either , or EOS follows */
            )
            return 1;
        /*
         * If not, search for the next comma and resume after that.
         * If no comma found, terminate.
         */
        while (haylen > 0 && *haystack != ',')
            haylen--, haystack++;
        if (haylen == 0)
            return 0;
        haylen--, haystack++;          /* skip over comma itself */
    }
}

/*
 * SSH2 key creation method.
 */
void ssh2_mkkey(Bignum K, char *H, char chr, char *keyspace) {
    SHA_State s;
    /* First 20 bytes. */
    SHA_Init(&s);
    sha_mpint(&s, K);
    SHA_Bytes(&s, H, 20);
    SHA_Bytes(&s, &chr, 1);
    SHA_Bytes(&s, H, 20);
    SHA_Final(&s, keyspace);
    /* Next 20 bytes. */
    SHA_Init(&s);
    sha_mpint(&s, K);
    SHA_Bytes(&s, H, 20);
    SHA_Bytes(&s, keyspace, 20);
    SHA_Final(&s, keyspace+20);
}

/*
 * Handle the SSH2 transport layer.
 */
static int do_ssh2_transport(unsigned char *in, int inlen, int ispkt)
{
    static int i, len;
    static char *str;
    static Bignum e, f, K;
    static struct ssh_cipher *cscipher_tobe = NULL;
    static struct ssh_cipher *sccipher_tobe = NULL;
    static struct ssh_mac *csmac_tobe = NULL;
    static struct ssh_mac *scmac_tobe = NULL;
    static struct ssh_compress *cscomp_tobe = NULL;
    static struct ssh_compress *sccomp_tobe = NULL;
    static char *hostkeydata, *sigdata, *keystr;
    static int hostkeylen, siglen;
    static unsigned char exchange_hash[20];
    static unsigned char keyspace[40];

    crBegin;
    random_init();

    begin_key_exchange:
    /*
     * Construct and send our key exchange packet.
     */
    ssh2_pkt_init(SSH2_MSG_KEXINIT);
    for (i = 0; i < 16; i++)
        ssh2_pkt_addbyte((unsigned char)random_byte());
    /* List key exchange algorithms. */
    ssh2_pkt_addstring_start();
    for (i = 0; i < lenof(kex_algs); i++) {
        ssh2_pkt_addstring_str(kex_algs[i]->name);
        if (i < lenof(kex_algs)-1)
            ssh2_pkt_addstring_str(",");
    }
    /* List server host key algorithms. */
    ssh2_pkt_addstring_start();
    for (i = 0; i < lenof(hostkey_algs); i++) {
        ssh2_pkt_addstring_str(hostkey_algs[i]->name);
        if (i < lenof(hostkey_algs)-1)
            ssh2_pkt_addstring_str(",");
    }
    /* List client->server encryption algorithms. */
    ssh2_pkt_addstring_start();
    for (i = 0; i < lenof(ciphers); i++) {
        ssh2_pkt_addstring_str(ciphers[i]->name);
        if (i < lenof(ciphers)-1)
            ssh2_pkt_addstring_str(",");
    }
    /* List server->client encryption algorithms. */
    ssh2_pkt_addstring_start();
    for (i = 0; i < lenof(ciphers); i++) {
        ssh2_pkt_addstring_str(ciphers[i]->name);
        if (i < lenof(ciphers)-1)
            ssh2_pkt_addstring_str(",");
    }
    /* List client->server MAC algorithms. */
    ssh2_pkt_addstring_start();
    for (i = 0; i < lenof(macs); i++) {
        ssh2_pkt_addstring_str(macs[i]->name);
        if (i < lenof(macs)-1)
            ssh2_pkt_addstring_str(",");
    }
    /* List server->client MAC algorithms. */
    ssh2_pkt_addstring_start();
    for (i = 0; i < lenof(macs); i++) {
        ssh2_pkt_addstring_str(macs[i]->name);
        if (i < lenof(macs)-1)
            ssh2_pkt_addstring_str(",");
    }
    /* List client->server compression algorithms. */
    ssh2_pkt_addstring_start();
    for (i = 0; i < lenof(compressions); i++) {
        ssh2_pkt_addstring_str(compressions[i]->name);
        if (i < lenof(compressions)-1)
            ssh2_pkt_addstring_str(",");
    }
    /* List server->client compression algorithms. */
    ssh2_pkt_addstring_start();
    for (i = 0; i < lenof(compressions); i++) {
        ssh2_pkt_addstring_str(compressions[i]->name);
        if (i < lenof(compressions)-1)
            ssh2_pkt_addstring_str(",");
    }
    /* List client->server languages. Empty list. */
    ssh2_pkt_addstring_start();
    /* List server->client languages. Empty list. */
    ssh2_pkt_addstring_start();
    /* First KEX packet does _not_ follow, because we're not that brave. */
    ssh2_pkt_addbool(FALSE);
    /* Reserved. */
    ssh2_pkt_adduint32(0);
    sha_string(&exhash, pktout.data+5, pktout.length-5);
    ssh2_pkt_send();

    if (!ispkt) crWaitUntil(ispkt);
    sha_string(&exhash, pktin.data+5, pktin.length-5);

    /*
     * Now examine the other side's KEXINIT to see what we're up
     * to.
     */
    if (pktin.type != SSH2_MSG_KEXINIT) {
        fatalbox("expected key exchange packet from server");
    }
    kex = NULL; hostkey = NULL; cscipher_tobe = NULL; sccipher_tobe = NULL;
    csmac_tobe = NULL; scmac_tobe = NULL; cscomp_tobe = NULL; sccomp_tobe = NULL;
    pktin.savedpos += 16;              /* skip garbage cookie */
    ssh2_pkt_getstring(&str, &len);    /* key exchange algorithms */
    for (i = 0; i < lenof(kex_algs); i++) {
        if (in_commasep_string(kex_algs[i]->name, str, len)) {
            kex = kex_algs[i];
            break;
        }
    }
    ssh2_pkt_getstring(&str, &len);    /* host key algorithms */
    for (i = 0; i < lenof(hostkey_algs); i++) {
        if (in_commasep_string(hostkey_algs[i]->name, str, len)) {
            hostkey = hostkey_algs[i];
            break;
        }
    }
    ssh2_pkt_getstring(&str, &len);    /* client->server cipher */
    for (i = 0; i < lenof(ciphers); i++) {
        if (in_commasep_string(ciphers[i]->name, str, len)) {
            cscipher_tobe = ciphers[i];
            break;
        }
    }
    ssh2_pkt_getstring(&str, &len);    /* server->client cipher */
    for (i = 0; i < lenof(ciphers); i++) {
        if (in_commasep_string(ciphers[i]->name, str, len)) {
            sccipher_tobe = ciphers[i];
            break;
        }
    }
    ssh2_pkt_getstring(&str, &len);    /* client->server mac */
    for (i = 0; i < lenof(macs); i++) {
        if (in_commasep_string(macs[i]->name, str, len)) {
            csmac_tobe = macs[i];
            break;
        }
    }
    ssh2_pkt_getstring(&str, &len);    /* server->client mac */
    for (i = 0; i < lenof(macs); i++) {
        if (in_commasep_string(macs[i]->name, str, len)) {
            scmac_tobe = macs[i];
            break;
        }
    }
    ssh2_pkt_getstring(&str, &len);    /* client->server compression */
    for (i = 0; i < lenof(compressions); i++) {
        if (in_commasep_string(compressions[i]->name, str, len)) {
            cscomp_tobe = compressions[i];
            break;
        }
    }
    ssh2_pkt_getstring(&str, &len);    /* server->client compression */
    for (i = 0; i < lenof(compressions); i++) {
        if (in_commasep_string(compressions[i]->name, str, len)) {
            sccomp_tobe = compressions[i];
            break;
        }
    }

    /*
     * Currently we only support Diffie-Hellman and DSS, so let's
     * bomb out if those aren't selected.
     */
    if (kex != &ssh_diffiehellman || hostkey != &ssh_dss)
        fatalbox("internal fault: chaos in SSH 2 transport layer");

    /*
     * Now we begin the fun. Generate and send e for Diffie-Hellman.
     */
    e = dh_create_e();
    ssh2_pkt_init(SSH2_MSG_KEXDH_INIT);
    ssh2_pkt_addmp(e);
    ssh2_pkt_send();

    crWaitUntil(ispkt);
    if (pktin.type != SSH2_MSG_KEXDH_REPLY) {
        fatalbox("expected key exchange packet from server");
    }
    ssh2_pkt_getstring(&hostkeydata, &hostkeylen);
    f = ssh2_pkt_getmp();
    ssh2_pkt_getstring(&sigdata, &siglen);

    K = dh_find_K(f);

    sha_string(&exhash, hostkeydata, hostkeylen);
    sha_mpint(&exhash, e);
    sha_mpint(&exhash, f);
    sha_mpint(&exhash, K);
    SHA_Final(&exhash, exchange_hash);

#if 0
    debug(("Exchange hash is:\r\n"));
    for (i = 0; i < 20; i++)
        debug((" %02x", exchange_hash[i]));
    debug(("\r\n"));
#endif

    hostkey->setkey(hostkeydata, hostkeylen);
    if (!hostkey->verifysig(sigdata, siglen, exchange_hash, 20))
        fatalbox("Server failed host key check");

    /*
     * Expect SSH2_MSG_NEWKEYS from server.
     */
    crWaitUntil(ispkt);
    if (pktin.type != SSH2_MSG_NEWKEYS)
        fatalbox("expected new-keys packet from server");

    /*
     * Authenticate remote host: verify host key. (We've already
     * checked the signature of the exchange hash.)
     */
    keystr = hostkey->fmtkey();
    verify_ssh_host_key(savedhost, keystr);
    free(keystr);

    /*
     * Send SSH2_MSG_NEWKEYS.
     */
    ssh2_pkt_init(SSH2_MSG_NEWKEYS);
    ssh2_pkt_send();

    /*
     * Create and initialise session keys.
     */
    cscipher = cscipher_tobe;
    sccipher = sccipher_tobe;
    csmac = csmac_tobe;
    scmac = scmac_tobe;
    cscomp = cscomp_tobe;
    sccomp = sccomp_tobe;
    /*
     * Set IVs after keys.
     */
    ssh2_mkkey(K, exchange_hash, 'C', keyspace); cscipher->setcskey(keyspace);
    ssh2_mkkey(K, exchange_hash, 'D', keyspace); cscipher->setsckey(keyspace);
    ssh2_mkkey(K, exchange_hash, 'A', keyspace); cscipher->setcsiv(keyspace);
    ssh2_mkkey(K, exchange_hash, 'B', keyspace); sccipher->setsciv(keyspace);
    ssh2_mkkey(K, exchange_hash, 'E', keyspace); csmac->setcskey(keyspace);
    ssh2_mkkey(K, exchange_hash, 'F', keyspace); scmac->setsckey(keyspace);

    /*
     * Now we're encrypting. Begin returning 1 to the protocol main
     * function so that other things can run on top of the
     * transport. If we ever see a KEXINIT, we must go back to the
     * start.
     */
    do {
        crReturn(1);
    } while (!(ispkt && pktin.type == SSH2_MSG_KEXINIT));
    goto begin_key_exchange;

    crFinish(1);
}

/*
 * SSH2: remote identifier for the main session channel.
 */
static unsigned long ssh_remote_channel;

/*
 * Handle the SSH2 userauth and connection layers.
 */
static void do_ssh2_authconn(unsigned char *in, int inlen, int ispkt)
{
    static unsigned long remote_winsize;
    static unsigned long remote_maxpkt;

    crBegin;

    /*
     * Request userauth protocol, and await a response to it.
     */
    ssh2_pkt_init(SSH2_MSG_SERVICE_REQUEST);
    ssh2_pkt_addstring("ssh-userauth");
    ssh2_pkt_send();
    crWaitUntilV(ispkt);
    if (pktin.type != SSH2_MSG_SERVICE_ACCEPT)
        fatalbox("Server refused user authentication protocol");

    /*
     * FIXME: currently we support only password authentication.
     * (This places us technically in violation of the SSH2 spec.
     * We must fix this.)
     */
    while (1) {
        /*
         * Get a username and a password.
         */
	static char username[100];
	static char password[100];
	static int pos = 0;
	static char c;

	if ((flags & FLAG_CONNECTION) && !*cfg.username) {
	    c_write("login as: ", 10);
	    while (pos >= 0) {
		crWaitUntilV(!ispkt);
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
	    if (flags & FLAG_VERBOSE) {
		sprintf(stuff, "Using username \"%s\".\r\n", username);
		c_write(stuff, strlen(stuff));
	    }
	}

	if (!(flags & FLAG_WINDOWED)) {
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
                crReturnV;
            }
	} else {
            c_write("password: ", 10);

            pos = 0;
            while (pos >= 0) {
                crWaitUntilV(!ispkt);
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

        ssh2_pkt_init(SSH2_MSG_USERAUTH_REQUEST);
        ssh2_pkt_addstring(username);
        ssh2_pkt_addstring("ssh-connection");   /* service requested */
        ssh2_pkt_addstring("password");
        ssh2_pkt_addbool(FALSE);
        ssh2_pkt_addstring(password);
        ssh2_pkt_send();

        crWaitUntilV(ispkt);
        if (pktin.type != SSH2_MSG_USERAUTH_SUCCESS) {
	    c_write("Access denied\r\n", 15);
	    logevent("Authentication refused");
        } else
            break;
    }

    /*
     * Now we're authenticated for the connection protocol. The
     * connection protocol will automatically have started at this
     * point; there's no need to send SERVICE_REQUEST.
     */

    /*
     * So now create a channel with a session in it.
     */
    ssh2_pkt_init(SSH2_MSG_CHANNEL_OPEN);
    ssh2_pkt_addstring("session");
    ssh2_pkt_adduint32(100);           /* as good as any */
    ssh2_pkt_adduint32(0xFFFFFFFFUL);  /* very big window which we ignore */
    ssh2_pkt_adduint32(0xFFFFFFFFUL);  /* very big max pkt size */
    ssh2_pkt_send();
    crWaitUntilV(ispkt);
    if (pktin.type != SSH2_MSG_CHANNEL_OPEN_CONFIRMATION) {
        fatalbox("Server refused to open a session");
        /* FIXME: error data comes back in FAILURE packet */
    }
    if (ssh2_pkt_getuint32() != 100) {
        fatalbox("Server's channel confirmation cited wrong channel");
    }
    ssh_remote_channel = ssh2_pkt_getuint32();
    remote_winsize = ssh2_pkt_getuint32();
    remote_maxpkt = ssh2_pkt_getuint32();
    logevent("Opened channel for session");

    /*
     * Now allocate a pty for the session.
     */
    ssh2_pkt_init(SSH2_MSG_CHANNEL_REQUEST);
    ssh2_pkt_adduint32(ssh_remote_channel); /* recipient channel */
    ssh2_pkt_addstring("pty-req");
    ssh2_pkt_addbool(1);               /* want reply */
    ssh2_pkt_addstring(cfg.termtype);
    ssh2_pkt_adduint32(cols);
    ssh2_pkt_adduint32(rows);
    ssh2_pkt_adduint32(0);             /* pixel width */
    ssh2_pkt_adduint32(0);             /* pixel height */
    ssh2_pkt_addstring_start();
    ssh2_pkt_addstring_data("\0", 1);  /* TTY_OP_END, no special options */
    ssh2_pkt_send();

    do {                               /* FIXME: pay attention to these */
        crWaitUntilV(ispkt);
    } while (pktin.type == SSH2_MSG_CHANNEL_WINDOW_ADJUST);

    if (pktin.type != SSH2_MSG_CHANNEL_SUCCESS) {
        if (pktin.type != SSH2_MSG_CHANNEL_FAILURE) {
            fatalbox("Server got confused by pty request");
        }
        c_write("Server refused to allocate pty\r\n", 32);
    } else {
        logevent("Allocated pty");
    }

    /*
     * Start a shell.
     */
    ssh2_pkt_init(SSH2_MSG_CHANNEL_REQUEST);
    ssh2_pkt_adduint32(ssh_remote_channel); /* recipient channel */
    ssh2_pkt_addstring("shell");
    ssh2_pkt_addbool(1);               /* want reply */
    ssh2_pkt_send();
    do {                               /* FIXME: pay attention to these */
        crWaitUntilV(ispkt);
    } while (pktin.type == SSH2_MSG_CHANNEL_WINDOW_ADJUST);
    if (pktin.type != SSH2_MSG_CHANNEL_SUCCESS) {
        if (pktin.type != SSH2_MSG_CHANNEL_FAILURE) {
            fatalbox("Server got confused by shell request");
        }
        fatalbox("Server refused to start a shell");
    } else {
        logevent("Started a shell");
    }

    /*
     * Transfer data!
     */
    ssh_send_ok = 1;
    while (1) {
	crReturnV;
	if (ispkt) {
	    if (pktin.type == SSH2_MSG_CHANNEL_DATA ||
                pktin.type == SSH2_MSG_CHANNEL_EXTENDED_DATA) {
                char *data;
                int length;
                if (ssh2_pkt_getuint32() != 100)
                    continue;          /* wrong channel */
                if (pktin.type == SSH2_MSG_CHANNEL_EXTENDED_DATA &&
                    ssh2_pkt_getuint32() != SSH2_EXTENDED_DATA_STDERR)
                    continue;          /* extended but not stderr */
                ssh2_pkt_getstring(&data, &length);
                if (data)
                    c_write(data, length);
	    } else if (pktin.type == SSH2_MSG_DISCONNECT) {
                ssh_state = SSH_STATE_CLOSED;
		logevent("Received disconnect request");
	    } else if (pktin.type == SSH2_MSG_CHANNEL_REQUEST) {
                continue;              /* exit status et al; ignore (FIXME?) */
	    } else if (pktin.type == SSH2_MSG_CHANNEL_WINDOW_ADJUST) {
                continue;              /* ignore for now (FIXME!) */
	    } else {
		fatalbox("Strange packet received: type %d", pktin.type);
	    }
	} else {
            /* FIXME: for now, ignore window size */
            ssh2_pkt_init(SSH2_MSG_CHANNEL_DATA);
            ssh2_pkt_adduint32(ssh_remote_channel);
            ssh2_pkt_addstring_start();
            ssh2_pkt_addstring_data(in, inlen);
            ssh2_pkt_send();
	}
    }

    crFinishV;
}

/*
 * Handle the top-level SSH2 protocol.
 */
static void ssh2_protocol(unsigned char *in, int inlen, int ispkt)
{
    if (do_ssh2_transport(in, inlen, ispkt) == 0)
        return;
    do_ssh2_authconn(in, inlen, ispkt);
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

    if (hwnd && WSAAsyncSelect (s, hwnd, WM_NETEVENT, FD_READ | FD_CLOSE) == SOCKET_ERROR)
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
	    send_packet(SSH1_CMSG_WINDOW_SIZE,
	                PKT_INT, rows, PKT_INT, cols,
	                PKT_INT, 0, PKT_INT, 0, PKT_END);
        }
    }
}

/*
 * Send Telnet special codes. TS_EOF is useful for `plink', so you
 * can send an EOF and collect resulting output (e.g. `plink
 * hostname sort').
 */
static void ssh_special (Telnet_Special code) {
    if (code == TS_EOF) {
        if (ssh_version = 1) {
            send_packet(SSH1_CMSG_EOF, PKT_END);
        } else {
            ssh2_pkt_init(SSH2_MSG_CHANNEL_EOF);
            ssh2_pkt_adduint32(ssh_remote_channel);
            ssh2_pkt_send();
        }
    } else {
        /* do nothing */
    }
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
	if (pktin.type == SSH1_SMSG_STDOUT_DATA) {
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
	} else if (pktin.type == SSH1_SMSG_STDERR_DATA) {
	    int plen = GET_32BIT(pktin.body);
	    fwrite(pktin.body + 4, plen, 1, stderr);
	} else if (pktin.type == SSH1_MSG_DISCONNECT) {
		logevent("Received disconnect request");
	} else if (pktin.type == SSH1_SMSG_SUCCESS ||
	           pktin.type == SSH1_SMSG_FAILURE) {
		/* ignore */
	} else if (pktin.type == SSH1_SMSG_EXIT_STATUS) {
	    char logbuf[100];
	    sprintf(logbuf, "Remote exit status: %d", GET_32BIT(pktin.body));
	    logevent(logbuf);
	    send_packet(SSH1_CMSG_EXIT_CONFIRMATION, PKT_END);
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
    if (s == INVALID_SOCKET)
	return;
    send_packet(SSH1_CMSG_STDIN_DATA,
                PKT_INT, len, PKT_DATA, buf, len, PKT_END);
}

/*
 * Send an EOF notification to the server.
 * (only used by pSCP)
 */
void ssh_scp_send_eof(void)
{
    if (s == INVALID_SOCKET)
	return;
    send_packet(SSH1_CMSG_EOF, PKT_END);
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
    } while (!do_ssh1_login(NULL, 0, 1));

    if (ssh_state == SSH_STATE_CLOSED) {
        closesocket(s);
        s = INVALID_SOCKET;
        return "Session initialisation error";
    }

    /* Execute command */
    sprintf(buf, "Sending command: %.100s", cmd);
    logevent(buf);
    send_packet(SSH1_CMSG_EXEC_CMD, PKT_STR, cmd, PKT_END);

    return NULL;
}

static SOCKET ssh_socket(void) { return s; }

static int ssh_sendok(void) { return ssh_send_ok; }

Backend ssh_backend = {
    ssh_init,
    ssh_msg,
    ssh_send,
    ssh_size,
    ssh_special,
    ssh_socket,
    ssh_sendok
};
