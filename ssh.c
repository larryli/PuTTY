#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#ifndef AUTO_WINSOCK
#ifdef WINSOCK_TWO
#include <winsock2.h>
#else
#include <winsock.h>
#endif
#endif

#include "putty.h"
#include "tree234.h"
#include "ssh.h"

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#define logevent(s) { logevent(s); \
                      if ((flags & FLAG_STDERR) && (flags & FLAG_VERBOSE)) \
                      fprintf(stderr, "%s\n", s); }

#define bombout(msg) ( ssh_state = SSH_STATE_CLOSED, closesocket(s), \
                       s = INVALID_SOCKET, connection_fatal msg )

#define SSH1_MSG_DISCONNECT                       1    /* 0x1 */
#define SSH1_SMSG_PUBLIC_KEY                      2    /* 0x2 */
#define SSH1_CMSG_SESSION_KEY                     3    /* 0x3 */
#define SSH1_CMSG_USER                            4    /* 0x4 */
#define SSH1_CMSG_AUTH_RSA                        6    /* 0x6 */
#define SSH1_SMSG_AUTH_RSA_CHALLENGE              7    /* 0x7 */
#define SSH1_CMSG_AUTH_RSA_RESPONSE               8    /* 0x8 */
#define SSH1_CMSG_AUTH_PASSWORD                   9    /* 0x9 */
#define SSH1_CMSG_REQUEST_PTY                     10   /* 0xa */
#define SSH1_CMSG_WINDOW_SIZE                     11   /* 0xb */
#define SSH1_CMSG_EXEC_SHELL                      12   /* 0xc */
#define SSH1_CMSG_EXEC_CMD                        13   /* 0xd */
#define SSH1_SMSG_SUCCESS                         14   /* 0xe */
#define SSH1_SMSG_FAILURE                         15   /* 0xf */
#define SSH1_CMSG_STDIN_DATA                      16   /* 0x10 */
#define SSH1_SMSG_STDOUT_DATA                     17   /* 0x11 */
#define SSH1_SMSG_STDERR_DATA                     18   /* 0x12 */
#define SSH1_CMSG_EOF                             19   /* 0x13 */
#define SSH1_SMSG_EXIT_STATUS                     20   /* 0x14 */
#define SSH1_MSG_CHANNEL_OPEN_CONFIRMATION        21   /* 0x15 */
#define SSH1_MSG_CHANNEL_OPEN_FAILURE             22   /* 0x16 */
#define SSH1_MSG_CHANNEL_DATA                     23   /* 0x17 */
#define SSH1_MSG_CHANNEL_CLOSE                    24   /* 0x18 */
#define SSH1_MSG_CHANNEL_CLOSE_CONFIRMATION       25   /* 0x19 */
#define SSH1_CMSG_AGENT_REQUEST_FORWARDING        30   /* 0x1e */
#define SSH1_SMSG_AGENT_OPEN                      31   /* 0x1f */
#define SSH1_CMSG_EXIT_CONFIRMATION               33   /* 0x21 */
#define SSH1_MSG_IGNORE                           32   /* 0x20 */
#define SSH1_MSG_DEBUG                            36   /* 0x24 */
#define SSH1_CMSG_AUTH_TIS                        39   /* 0x27 */
#define SSH1_SMSG_AUTH_TIS_CHALLENGE              40   /* 0x28 */
#define SSH1_CMSG_AUTH_TIS_RESPONSE               41   /* 0x29 */
#define SSH1_CMSG_AUTH_CCARD                      70   /* 0x46 */
#define SSH1_SMSG_AUTH_CCARD_CHALLENGE            71   /* 0x47 */
#define SSH1_CMSG_AUTH_CCARD_RESPONSE             72   /* 0x48 */

#define SSH1_AUTH_TIS                             5    /* 0x5 */
#define SSH1_AUTH_CCARD                           16   /* 0x10 */

#define SSH_AGENTC_REQUEST_RSA_IDENTITIES         1    /* 0x1 */
#define SSH_AGENT_RSA_IDENTITIES_ANSWER           2    /* 0x2 */
#define SSH_AGENTC_RSA_CHALLENGE                  3    /* 0x3 */
#define SSH_AGENT_RSA_RESPONSE                    4    /* 0x4 */
#define SSH_AGENT_FAILURE                         5    /* 0x5 */
#define SSH_AGENT_SUCCESS                         6    /* 0x6 */
#define SSH_AGENTC_ADD_RSA_IDENTITY               7    /* 0x7 */
#define SSH_AGENTC_REMOVE_RSA_IDENTITY            8    /* 0x8 */

#define SSH2_MSG_DISCONNECT                       1    /* 0x1 */
#define SSH2_MSG_IGNORE                           2    /* 0x2 */
#define SSH2_MSG_UNIMPLEMENTED                    3    /* 0x3 */
#define SSH2_MSG_DEBUG                            4    /* 0x4 */
#define SSH2_MSG_SERVICE_REQUEST                  5    /* 0x5 */
#define SSH2_MSG_SERVICE_ACCEPT                   6    /* 0x6 */
#define SSH2_MSG_KEXINIT                          20   /* 0x14 */
#define SSH2_MSG_NEWKEYS                          21   /* 0x15 */
#define SSH2_MSG_KEXDH_INIT                       30   /* 0x1e */
#define SSH2_MSG_KEXDH_REPLY                      31   /* 0x1f */
#define SSH2_MSG_USERAUTH_REQUEST                 50   /* 0x32 */
#define SSH2_MSG_USERAUTH_FAILURE                 51   /* 0x33 */
#define SSH2_MSG_USERAUTH_SUCCESS                 52   /* 0x34 */
#define SSH2_MSG_USERAUTH_BANNER                  53   /* 0x35 */
#define SSH2_MSG_USERAUTH_PK_OK                   60   /* 0x3c */
#define SSH2_MSG_USERAUTH_PASSWD_CHANGEREQ        60   /* 0x3c */
#define SSH2_MSG_GLOBAL_REQUEST                   80   /* 0x50 */
#define SSH2_MSG_REQUEST_SUCCESS                  81   /* 0x51 */
#define SSH2_MSG_REQUEST_FAILURE                  82   /* 0x52 */
#define SSH2_MSG_CHANNEL_OPEN                     90   /* 0x5a */
#define SSH2_MSG_CHANNEL_OPEN_CONFIRMATION        91   /* 0x5b */
#define SSH2_MSG_CHANNEL_OPEN_FAILURE             92   /* 0x5c */
#define SSH2_MSG_CHANNEL_WINDOW_ADJUST            93   /* 0x5d */
#define SSH2_MSG_CHANNEL_DATA                     94   /* 0x5e */
#define SSH2_MSG_CHANNEL_EXTENDED_DATA            95   /* 0x5f */
#define SSH2_MSG_CHANNEL_EOF                      96   /* 0x60 */
#define SSH2_MSG_CHANNEL_CLOSE                    97   /* 0x61 */
#define SSH2_MSG_CHANNEL_REQUEST                  98   /* 0x62 */
#define SSH2_MSG_CHANNEL_SUCCESS                  99   /* 0x63 */
#define SSH2_MSG_CHANNEL_FAILURE                  100  /* 0x64 */

#define SSH2_OPEN_ADMINISTRATIVELY_PROHIBITED     1    /* 0x1 */
#define SSH2_OPEN_CONNECT_FAILED                  2    /* 0x2 */
#define SSH2_OPEN_UNKNOWN_CHANNEL_TYPE            3    /* 0x3 */
#define SSH2_OPEN_RESOURCE_SHORTAGE               4    /* 0x4 */

#define SSH2_EXTENDED_DATA_STDERR                 1    /* 0x1 */

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

extern const struct ssh_cipher ssh_3des;
extern const struct ssh_cipher ssh_3des_ssh2;
extern const struct ssh_cipher ssh_des;
extern const struct ssh_cipher ssh_blowfish_ssh1;
extern const struct ssh_cipher ssh_blowfish_ssh2;

/*
 * Ciphers for SSH2. We miss out single-DES because it isn't
 * supported; also 3DES and Blowfish are both done differently from
 * SSH1. (3DES uses outer chaining; Blowfish has the opposite
 * endianness and different-sized keys.)
 */
const static struct ssh_cipher *ciphers[] = { &ssh_blowfish_ssh2, &ssh_3des_ssh2 };

extern const struct ssh_kex ssh_diffiehellman;
const static struct ssh_kex *kex_algs[] = { &ssh_diffiehellman };

extern const struct ssh_hostkey ssh_dss;
const static struct ssh_hostkey *hostkey_algs[] = { &ssh_dss };

extern const struct ssh_mac ssh_md5, ssh_sha1, ssh_sha1_buggy;

static void nullmac_key(unsigned char *key) { }
static void nullmac_generate(unsigned char *blk, int len, unsigned long seq) { }
static int nullmac_verify(unsigned char *blk, int len, unsigned long seq) { return 1; }
const static struct ssh_mac ssh_mac_none = {
    nullmac_key, nullmac_key, nullmac_generate, nullmac_verify, "none", 0
};
const static struct ssh_mac *macs[] = {
    &ssh_sha1, &ssh_md5, &ssh_mac_none };
const static struct ssh_mac *buggymacs[] = {
    &ssh_sha1_buggy, &ssh_md5, &ssh_mac_none };

const static struct ssh_compress ssh_comp_none = {
    "none"
};
const static struct ssh_compress *compressions[] = { &ssh_comp_none };

/*
 * 2-3-4 tree storing channels.
 */
struct ssh_channel {
    unsigned remoteid, localid;
    int type;
    int closes;
    union {
        struct ssh_agent_channel {
            unsigned char *message;
            unsigned char msglen[4];
            int lensofar, totallen;
        } a;
        struct ssh2_data_channel {
            unsigned char *outbuffer;
            unsigned outbuflen, outbufsize;
            unsigned remwindow, remmaxpkt;
        } v2;
    } u;
};

struct Packet {
    long length;
    int type;
    unsigned char *data;
    unsigned char *body;
    long savedpos;
    long maxlen;
};

static SHA_State exhash;

static SOCKET s = INVALID_SOCKET;

static unsigned char session_key[32];
static const struct ssh_cipher *cipher = NULL;
static const struct ssh_cipher *cscipher = NULL;
static const struct ssh_cipher *sccipher = NULL;
static const struct ssh_mac *csmac = NULL;
static const struct ssh_mac *scmac = NULL;
static const struct ssh_compress *cscomp = NULL;
static const struct ssh_compress *sccomp = NULL;
static const struct ssh_kex *kex = NULL;
static const struct ssh_hostkey *hostkey = NULL;
int (*ssh_get_password)(const char *prompt, char *str, int maxlen) = NULL;

static char *savedhost;
static int savedport;
static int ssh_send_ok;

static tree234 *ssh_channels;           /* indexed by local id */
static struct ssh_channel *mainchan;   /* primary session channel */

static enum {
    SSH_STATE_BEFORE_SIZE,
    SSH_STATE_INTERMED,
    SSH_STATE_SESSION,
    SSH_STATE_CLOSED
} ssh_state = SSH_STATE_BEFORE_SIZE;

static int size_needed = FALSE;

static struct Packet pktin = { 0, 0, NULL, NULL, 0 };
static struct Packet pktout = { 0, 0, NULL, NULL, 0 };

static int ssh_version;
static void (*ssh_protocol)(unsigned char *in, int inlen, int ispkt);
static void ssh1_protocol(unsigned char *in, int inlen, int ispkt);
static void ssh2_protocol(unsigned char *in, int inlen, int ispkt);
static void ssh_size(void);

static int (*s_rdpkt)(unsigned char **data, int *datalen);

static struct rdpkt1_state_tag {
    long len, pad, biglen, to_read;
    unsigned long realcrc, gotcrc;
    unsigned char *p;
    int i;
    int chunk;
} rdpkt1_state;

static struct rdpkt2_state_tag {
    long len, pad, payload, packetlen, maclen;
    int i;
    int cipherblk;
    unsigned long incoming_sequence;
} rdpkt2_state;

static int ssh_channelcmp(void *av, void *bv) {
    struct ssh_channel *a = (struct ssh_channel *)av;
    struct ssh_channel *b = (struct ssh_channel *)bv;
    if (a->localid < b->localid) return -1;
    if (a->localid > b->localid) return +1;
    return 0;
}
static int ssh_channelfind(void *av, void *bv) {
    unsigned *a = (unsigned *)av;
    struct ssh_channel *b = (struct ssh_channel *)bv;
    if (*a < b->localid) return -1;
    if (*a > b->localid) return +1;
    return 0;
}

static void s_write (char *buf, int len) {
    while (len > 0) {
	int i = send (s, buf, len, 0);
        noise_ultralight(i);
        if (i <= 0) {
            bombout(("Lost connection while sending"));
            return;
        }
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
    if ((flags & FLAG_STDERR)) {
        int i;
        for (i = 0; i < len; i++)
            if (buf[i] != '\r')
                fputc(buf[i], stderr);
	return;
    }
    while (len--) 
        c_write1(*buf++);
}

static void c_writedata (char *buf, int len) {
    while (len--)
        c_write1(*buf++);
}

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
    struct rdpkt1_state_tag *st = &rdpkt1_state;

    crBegin;

next_packet:

    pktin.type = 0;
    pktin.length = 0;

    for (st->i = st->len = 0; st->i < 4; st->i++) {
	while ((*datalen) == 0)
	    crReturn(4-st->i);
	st->len = (st->len << 8) + **data;
	(*data)++, (*datalen)--;
    }

#ifdef FWHACK
    if (st->len == 0x52656d6f) {        /* "Remo"te server has closed ... */
        st->len = 0x300;                /* big enough to carry to end */
    }
#endif

    st->pad = 8 - (st->len % 8);
    st->biglen = st->len + st->pad;
    pktin.length = st->len - 5;

    if (pktin.maxlen < st->biglen) {
	pktin.maxlen = st->biglen;
	pktin.data = (pktin.data == NULL ? malloc(st->biglen+APIEXTRA) :
	              realloc(pktin.data, st->biglen+APIEXTRA));
	if (!pktin.data)
	    fatalbox("Out of memory");
    }

    st->to_read = st->biglen;
    st->p = pktin.data;
    while (st->to_read > 0) {
        st->chunk = st->to_read;
	while ((*datalen) == 0)
	    crReturn(st->to_read);
	if (st->chunk > (*datalen))
	    st->chunk = (*datalen);
	memcpy(st->p, *data, st->chunk);
	*data += st->chunk;
	*datalen -= st->chunk;
	st->p += st->chunk;
	st->to_read -= st->chunk;
    }

    if (cipher)
	cipher->decrypt(pktin.data, st->biglen);

    pktin.type = pktin.data[st->pad];
    pktin.body = pktin.data + st->pad + 1;

    st->realcrc = crc32(pktin.data, st->biglen-4);
    st->gotcrc = GET_32BIT(pktin.data+st->biglen-4);
    if (st->gotcrc != st->realcrc) {
	bombout(("Incorrect CRC received on packet"));
        crReturn(0);
    }

    if (pktin.type == SSH1_SMSG_STDOUT_DATA ||
        pktin.type == SSH1_SMSG_STDERR_DATA ||
        pktin.type == SSH1_MSG_DEBUG ||
        pktin.type == SSH1_SMSG_AUTH_TIS_CHALLENGE ||
        pktin.type == SSH1_SMSG_AUTH_CCARD_CHALLENGE) {
	long strlen = GET_32BIT(pktin.body);
	if (strlen + 4 != pktin.length) {
	    bombout(("Received data packet with bogus string length"));
            crReturn(0);
        }
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
    struct rdpkt2_state_tag *st = &rdpkt2_state;

    crBegin;

next_packet:
    pktin.type = 0;
    pktin.length = 0;
    if (sccipher)
        st->cipherblk = sccipher->blksize;
    else
        st->cipherblk = 8;
    if (st->cipherblk < 8)
        st->cipherblk = 8;

    if (pktin.maxlen < st->cipherblk) {
	pktin.maxlen = st->cipherblk;
	pktin.data = (pktin.data == NULL ? malloc(st->cipherblk+APIEXTRA) :
	              realloc(pktin.data, st->cipherblk+APIEXTRA));
	if (!pktin.data)
	    fatalbox("Out of memory");
    }

    /*
     * Acquire and decrypt the first block of the packet. This will
     * contain the length and padding details.
     */
     for (st->i = st->len = 0; st->i < st->cipherblk; st->i++) {
	while ((*datalen) == 0)
	    crReturn(st->cipherblk-st->i);
	pktin.data[st->i] = *(*data)++;
        (*datalen)--;
    }
#ifdef FWHACK
    if (!memcmp(pktin.data, "Remo", 4)) {/* "Remo"te server has closed ... */
        /* FIXME */
    }
#endif
    if (sccipher)
        sccipher->decrypt(pktin.data, st->cipherblk);

    /*
     * Now get the length and padding figures.
     */
    st->len = GET_32BIT(pktin.data);
    st->pad = pktin.data[4];

    /*
     * This enables us to deduce the payload length.
     */
    st->payload = st->len - st->pad - 1;

    pktin.length = st->payload + 5;

    /*
     * So now we can work out the total packet length.
     */
    st->packetlen = st->len + 4;
    st->maclen = scmac ? scmac->len : 0;

    /*
     * Adjust memory allocation if packet is too big.
     */
    if (pktin.maxlen < st->packetlen+st->maclen) {
	pktin.maxlen = st->packetlen+st->maclen;
	pktin.data = (pktin.data == NULL ? malloc(pktin.maxlen+APIEXTRA) :
	              realloc(pktin.data, pktin.maxlen+APIEXTRA));
	if (!pktin.data)
	    fatalbox("Out of memory");
    }

    /*
     * Read and decrypt the remainder of the packet.
     */
    for (st->i = st->cipherblk; st->i < st->packetlen + st->maclen; st->i++) {
	while ((*datalen) == 0)
	    crReturn(st->packetlen + st->maclen - st->i);
	pktin.data[st->i] = *(*data)++;
        (*datalen)--;
    }
    /* Decrypt everything _except_ the MAC. */
    if (sccipher)
        sccipher->decrypt(pktin.data + st->cipherblk,
                          st->packetlen - st->cipherblk);

#if 0
    debug(("Got packet len=%d pad=%d\r\n", st->len, st->pad));
    for (st->i = 0; st->i < st->packetlen; st->i++)
        debug(("  %02x", (unsigned char)pktin.data[st->i]));
    debug(("\r\n"));
#endif

    /*
     * Check the MAC.
     */
    if (scmac && !scmac->verify(pktin.data, st->len+4, st->incoming_sequence)) {
	bombout(("Incorrect MAC received on packet"));
        crReturn(0);
    }
    st->incoming_sequence++;               /* whether or not we MACed */

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
            pktlen += ssh1_bignum_length(bn);
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
            p += ssh1_write_bignum(p, bn);
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
    savedport = port;

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
static void sha_string(SHA_State *s, void *str, int len) {
    unsigned char lenblk[4];
    PUT_32BIT(lenblk, len);
    SHA_Bytes(s, lenblk, 4);
    SHA_Bytes(s, str, len);
}

/*
 * SSH2 packet construction functions.
 */
static void ssh2_pkt_adddata(void *data, int len) {
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
static void ssh2_pkt_addbyte(unsigned char byte) {
    ssh2_pkt_adddata(&byte, 1);
}
static void ssh2_pkt_init(int pkt_type) {
    pktout.length = 5;
    ssh2_pkt_addbyte((unsigned char)pkt_type);
}
static void ssh2_pkt_addbool(unsigned char value) {
    ssh2_pkt_adddata(&value, 1);
}
static void ssh2_pkt_adduint32(unsigned long value) {
    unsigned char x[4];
    PUT_32BIT(x, value);
    ssh2_pkt_adddata(x, 4);
}
static void ssh2_pkt_addstring_start(void) {
    ssh2_pkt_adduint32(0);
    pktout.savedpos = pktout.length;
}
static void ssh2_pkt_addstring_str(char *data) {
    ssh2_pkt_adddata(data, strlen(data));
    PUT_32BIT(pktout.data + pktout.savedpos - 4,
              pktout.length - pktout.savedpos);
}
static void ssh2_pkt_addstring_data(char *data, int len) {
    ssh2_pkt_adddata(data, len);
    PUT_32BIT(pktout.data + pktout.savedpos - 4,
              pktout.length - pktout.savedpos);
}
static void ssh2_pkt_addstring(char *data) {
    ssh2_pkt_addstring_start();
    ssh2_pkt_addstring_str(data);
}
static char *ssh2_mpint_fmt(Bignum b, int *len) {
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
static void ssh2_pkt_addmp(Bignum b) {
    unsigned char *p;
    int len;
    p = ssh2_mpint_fmt(b, &len);
    ssh2_pkt_addstring_start();
    ssh2_pkt_addstring_data(p, len);
    free(p);
}
static void ssh2_pkt_send(void) {
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

static void sha_mpint(SHA_State *s, Bignum b) {
    unsigned char *p;
    int len;
    p = ssh2_mpint_fmt(b, &len);
    sha_string(s, p, len);
    free(p);
}

/*
 * SSH2 packet decode functions.
 */
static unsigned long ssh2_pkt_getuint32(void) {
    unsigned long value;
    if (pktin.length - pktin.savedpos < 4)
        return 0;                      /* arrgh, no way to decline (FIXME?) */
    value = GET_32BIT(pktin.data+pktin.savedpos);
    pktin.savedpos += 4;
    return value;
}
static void ssh2_pkt_getstring(char **p, int *length) {
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
static Bignum ssh2_pkt_getmp(void) {
    char *p;
    int i, j, length;
    Bignum b;

    ssh2_pkt_getstring(&p, &length);
    if (!p)
        return NULL;
    if (p[0] & 0x80) {
        bombout(("internal error: Can't handle negative mpints"));
        return NULL;
    }
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

    rdpkt2_state.incoming_sequence = 0;

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
    static char username[100];

    crBegin;

    if (!ispkt) crWaitUntil(ispkt);

    if (pktin.type != SSH1_SMSG_PUBLIC_KEY) {
	bombout(("Public key packet not received"));
        crReturn(0);
    }

    logevent("Received public keys");

    memcpy(cookie, pktin.body, 8);

    i = makekey(pktin.body+8, &servkey, &keystr1, 0);
    j = makekey(pktin.body+8+i, &hostkey, &keystr2, 0);

    /*
     * Log the host key fingerprint.
     */
    {
	char logmsg[80];
	logevent("Host key fingerprint is:");
	strcpy(logmsg, "      ");
        hostkey.comment = NULL;
        rsa_fingerprint(logmsg+strlen(logmsg), sizeof(logmsg)-strlen(logmsg),
                        &hostkey);
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
        char fingerprint[100];
        char *keystr = malloc(len);
        if (!keystr)
            fatalbox("Out of memory");
        rsastr_fmt(keystr, &hostkey);
        rsa_fingerprint(fingerprint, sizeof(fingerprint), &hostkey);
        verify_ssh_host_key(savedhost, savedport, "rsa", keystr, fingerprint);
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

    cipher = cipher_type == SSH_CIPHER_BLOWFISH ? &ssh_blowfish_ssh1 :
             cipher_type == SSH_CIPHER_DES ? &ssh_des :
             &ssh_3des;
    cipher->sesskey(session_key);

    crWaitUntil(ispkt);

    if (pktin.type != SSH1_SMSG_SUCCESS) {
	bombout(("Encryption not successfully enabled"));
        crReturn(0);
    }

    logevent("Successfully started encryption");

    fflush(stdout);
    {
	static int pos = 0;
	static char c;
	if ((flags & FLAG_INTERACTIVE) && !*cfg.username) {
	    c_write("login as: ", 10);
            ssh_send_ok = 1;
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
            if ((flags & FLAG_VERBOSE) || (flags & FLAG_INTERACTIVE)) {
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
	static char prompt[200];
	static int pos;
	static char c;
        static int pwpkt_type;
        /*
         * Show password prompt, having first obtained it via a TIS
         * or CryptoCard exchange if we're doing TIS or CryptoCard
         * authentication.
         */
        pwpkt_type = SSH1_CMSG_AUTH_PASSWORD;
        if (agent_exists()) {
            /*
             * Attempt RSA authentication using Pageant.
             */
            static unsigned char request[5], *response, *p;
            static int responselen;
            static int i, nkeys;
            static int authed = FALSE;
            void *r;

            logevent("Pageant is running. Requesting keys.");

            /* Request the keys held by the agent. */
            PUT_32BIT(request, 1);
            request[4] = SSH_AGENTC_REQUEST_RSA_IDENTITIES;
            agent_query(request, 5, &r, &responselen);
            response = (unsigned char *)r;
            if (response) {
                p = response + 5;
                nkeys = GET_32BIT(p); p += 4;
                { char buf[64]; sprintf(buf, "Pageant has %d keys", nkeys);
                    logevent(buf); }
                for (i = 0; i < nkeys; i++) {
                    static struct RSAKey key;
                    static Bignum challenge;
                    static char *commentp;
                    static int commentlen;

                    { char buf[64]; sprintf(buf, "Trying Pageant key #%d", i);
                        logevent(buf); }
                    p += 4;
                    p += ssh1_read_bignum(p, &key.exponent);
                    p += ssh1_read_bignum(p, &key.modulus);
                    commentlen = GET_32BIT(p); p += 4;
                    commentp = p; p += commentlen;
                    send_packet(SSH1_CMSG_AUTH_RSA,
                                PKT_BIGNUM, key.modulus, PKT_END);
                    crWaitUntil(ispkt);
                    if (pktin.type != SSH1_SMSG_AUTH_RSA_CHALLENGE) {
                        logevent("Key refused");
                        continue;
                    }
                    logevent("Received RSA challenge");
                    ssh1_read_bignum(pktin.body, &challenge);
                    {
                        char *agentreq, *q, *ret;
                        int len, retlen;
                        len = 1 + 4;   /* message type, bit count */
                        len += ssh1_bignum_length(key.exponent);
                        len += ssh1_bignum_length(key.modulus);
                        len += ssh1_bignum_length(challenge);
                        len += 16;     /* session id */
                        len += 4;      /* response format */
                        agentreq = malloc(4 + len);
                        PUT_32BIT(agentreq, len);
                        q = agentreq + 4;
                        *q++ = SSH_AGENTC_RSA_CHALLENGE;
                        PUT_32BIT(q, ssh1_bignum_bitcount(key.modulus));
                        q += 4;
                        q += ssh1_write_bignum(q, key.exponent);
                        q += ssh1_write_bignum(q, key.modulus);
                        q += ssh1_write_bignum(q, challenge);
                        memcpy(q, session_id, 16); q += 16;
                        PUT_32BIT(q, 1);   /* response format */
                        agent_query(agentreq, len+4, &ret, &retlen);
                        free(agentreq);
                        if (ret) {
                            if (ret[4] == SSH_AGENT_RSA_RESPONSE) {
                                logevent("Sending Pageant's response");
                                send_packet(SSH1_CMSG_AUTH_RSA_RESPONSE,
                                            PKT_DATA, ret+5, 16, PKT_END);
                                free(ret);
                                crWaitUntil(ispkt);
                                if (pktin.type == SSH1_SMSG_SUCCESS) {
                                    logevent("Pageant's response accepted");
                                    if (flags & FLAG_VERBOSE) {
                                        c_write("Authenticated using RSA key \"",
                                                29);
                                        c_write(commentp, commentlen);
                                        c_write("\" from agent\r\n", 14);
                                    }
                                    authed = TRUE;
                                } else
                                    logevent("Pageant's response not accepted");
                            } else {
                                logevent("Pageant failed to answer challenge");
                                free(ret);
                            }
                        } else {
                            logevent("No reply received from Pageant");
                        }
                    }
                    freebn(key.exponent);
                    freebn(key.modulus);
                    freebn(challenge);
                    if (authed)
                        break;
                }
            }
            if (authed)
                break;
        }
        if (*cfg.keyfile && !tried_publickey)
            pwpkt_type = SSH1_CMSG_AUTH_RSA;

        if (pktin.type == SSH1_SMSG_FAILURE &&
            cfg.try_tis_auth &&
            (supported_auths_mask & (1<<SSH1_AUTH_TIS))) {
            pwpkt_type = SSH1_CMSG_AUTH_TIS_RESPONSE;
            logevent("Requested TIS authentication");
            send_packet(SSH1_CMSG_AUTH_TIS, PKT_END);
            crWaitUntil(ispkt);
            if (pktin.type != SSH1_SMSG_AUTH_TIS_CHALLENGE) {
                logevent("TIS authentication declined");
                if (flags & FLAG_INTERACTIVE)
                    c_write("TIS authentication refused.\r\n", 29);
            } else {
                int challengelen = ((pktin.body[0] << 24) |
                                    (pktin.body[1] << 16) |
                                    (pktin.body[2] << 8) |
                                    (pktin.body[3]));
                logevent("Received TIS challenge");
                if (challengelen > sizeof(prompt)-1)
                    challengelen = sizeof(prompt)-1;   /* prevent overrun */
                memcpy(prompt, pktin.body+4, challengelen);
                prompt[challengelen] = '\0';
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
                if (challengelen > sizeof(prompt)-1)
                    challengelen = sizeof(prompt)-1;   /* prevent overrun */
                memcpy(prompt, pktin.body+4, challengelen);
                strncpy(prompt + challengelen, "\r\nResponse : ",
                        sizeof(prompt)-challengelen);
                prompt[sizeof(prompt)-1] = '\0';
            }
        }
        if (pwpkt_type == SSH1_CMSG_AUTH_PASSWORD) {
            sprintf(prompt, "%.90s@%.90s's password: ",
                    username, savedhost);
        }
        if (pwpkt_type == SSH1_CMSG_AUTH_RSA) {
            char *comment = NULL;
            if (flags & FLAG_VERBOSE)
                c_write("Trying public key authentication.\r\n", 35);
            if (!rsakey_encrypted(cfg.keyfile, &comment)) {
                if (flags & FLAG_VERBOSE)
                    c_write("No passphrase required.\r\n", 25);
                goto tryauth;
            }
            sprintf(prompt, "Passphrase for key \"%.100s\": ", comment);
            free(comment);
        }

	if (ssh_get_password) {
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
            c_write(prompt, strlen(prompt));
            pos = 0;
            ssh_send_ok = 1;
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
                c_write("Server refused our public key.\r\n", 32);
                continue;              /* go and try password */
            }
            if (pktin.type != SSH1_SMSG_AUTH_RSA_CHALLENGE) {
                bombout(("Bizarre response to offer of public key"));
                crReturn(0);
            }
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
                bombout(("Bizarre response to RSA authentication response"));
                crReturn(0);
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
	    bombout(("Strange packet received, type %d", pktin.type));
            crReturn(0);
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

    if (cfg.agentfwd && agent_exists()) {
        logevent("Requesting agent forwarding");
        send_packet(SSH1_CMSG_AGENT_REQUEST_FORWARDING, PKT_END);
        do { crReturnV; } while (!ispkt);
        if (pktin.type != SSH1_SMSG_SUCCESS && pktin.type != SSH1_SMSG_FAILURE) {
            bombout(("Protocol confusion"));
            crReturnV;
        } else if (pktin.type == SSH1_SMSG_FAILURE) {
            logevent("Agent forwarding refused");
        } else
            logevent("Agent forwarding enabled");
    }

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
            bombout(("Protocol confusion"));
            crReturnV;
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
    ssh_channels = newtree234(ssh_channelcmp);
    begin_session();
    while (1) {
	crReturnV;
	if (ispkt) {
	    if (pktin.type == SSH1_SMSG_STDOUT_DATA ||
                pktin.type == SSH1_SMSG_STDERR_DATA) {
		long len = GET_32BIT(pktin.body);
		c_writedata(pktin.body+4, len);
	    } else if (pktin.type == SSH1_MSG_DISCONNECT) {
                ssh_state = SSH_STATE_CLOSED;
		logevent("Received disconnect request");
            } else if (pktin.type == SSH1_SMSG_AGENT_OPEN) {
                /* Remote side is trying to open a channel to talk to our
                 * agent. Give them back a local channel number. */
                unsigned i = 1;
                struct ssh_channel *c;
                enum234 e;
                for (c = first234(ssh_channels, &e); c; c = next234(&e)) {
                    if (c->localid > i)
                        break;         /* found a free number */
                    i = c->localid + 1;
                }
                c = malloc(sizeof(struct ssh_channel));
                c->remoteid = GET_32BIT(pktin.body);
                c->localid = i;
                c->closes = 0;
                c->type = SSH1_SMSG_AGENT_OPEN;   /* identify channel type */
                c->u.a.lensofar = 0;
                add234(ssh_channels, c);
                send_packet(SSH1_MSG_CHANNEL_OPEN_CONFIRMATION,
                            PKT_INT, c->remoteid, PKT_INT, c->localid,
                            PKT_END);
            } else if (pktin.type == SSH1_MSG_CHANNEL_CLOSE ||
                       pktin.type == SSH1_MSG_CHANNEL_CLOSE_CONFIRMATION) {
                /* Remote side closes a channel. */
                unsigned i = GET_32BIT(pktin.body);
                struct ssh_channel *c;
                c = find234(ssh_channels, &i, ssh_channelfind);
                if (c) {
                    int closetype;
                    closetype = (pktin.type == SSH1_MSG_CHANNEL_CLOSE ? 1 : 2);
                    send_packet(pktin.type, PKT_INT, c->remoteid, PKT_END);
                    c->closes |= closetype;
                    if (c->closes == 3) {
                        del234(ssh_channels, c);
                        free(c);
                    }
                }
            } else if (pktin.type == SSH1_MSG_CHANNEL_DATA) {
                /* Data sent down one of our channels. */
                int i = GET_32BIT(pktin.body);
                int len = GET_32BIT(pktin.body+4);
                unsigned char *p = pktin.body+8;
                struct ssh_channel *c;
                c = find234(ssh_channels, &i, ssh_channelfind);
                if (c) {
                    switch(c->type) {
                      case SSH1_SMSG_AGENT_OPEN:
                        /* Data for an agent message. Buffer it. */
                        while (len > 0) {
                            if (c->u.a.lensofar < 4) {
                                int l = min(4 - c->u.a.lensofar, len);
                                memcpy(c->u.a.msglen + c->u.a.lensofar, p, l);
                                p += l; len -= l; c->u.a.lensofar += l;
                            }
                            if (c->u.a.lensofar == 4) {
                                c->u.a.totallen = 4 + GET_32BIT(c->u.a.msglen);
                                c->u.a.message = malloc(c->u.a.totallen);
                                memcpy(c->u.a.message, c->u.a.msglen, 4);
                            }
                            if (c->u.a.lensofar >= 4 && len > 0) {
                                int l = min(c->u.a.totallen - c->u.a.lensofar, len);
                                memcpy(c->u.a.message + c->u.a.lensofar, p, l);
                                p += l; len -= l; c->u.a.lensofar += l;
                            }
                            if (c->u.a.lensofar == c->u.a.totallen) {
                                void *reply, *sentreply;
                                int replylen;
                                agent_query(c->u.a.message, c->u.a.totallen,
                                            &reply, &replylen);
                                if (reply)
                                    sentreply = reply;
                                else {
                                    /* Fake SSH_AGENT_FAILURE. */
                                    sentreply = "\0\0\0\1\5";
                                    replylen = 5;
                                }
                                send_packet(SSH1_MSG_CHANNEL_DATA,
                                            PKT_INT, c->remoteid,
                                            PKT_INT, replylen,
                                            PKT_DATA, sentreply, replylen,
                                            PKT_END);
                                if (reply)
                                    free(reply);
                                free(c->u.a.message);
                                c->u.a.lensofar = 0;
                            }
                        }
                        break;
                    }
                }                
	    } else if (pktin.type == SSH1_SMSG_SUCCESS) {
		/* may be from EXEC_SHELL on some servers */
	    } else if (pktin.type == SSH1_SMSG_FAILURE) {
		/* may be from EXEC_SHELL on some servers
		 * if no pty is available or in other odd cases. Ignore */
	    } else if (pktin.type == SSH1_SMSG_EXIT_STATUS) {
		send_packet(SSH1_CMSG_EXIT_CONFIRMATION, PKT_END);
	    } else {
		bombout(("Strange packet received: type %d", pktin.type));
                crReturnV;
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
static int in_commasep_string(char *needle, char *haystack, int haylen) {
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
static void ssh2_mkkey(Bignum K, char *H, char chr, char *keyspace) {
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
    static const struct ssh_mac **maclist;
    static int nmacs;
    static const struct ssh_cipher *cscipher_tobe = NULL;
    static const struct ssh_cipher *sccipher_tobe = NULL;
    static const struct ssh_mac *csmac_tobe = NULL;
    static const struct ssh_mac *scmac_tobe = NULL;
    static const struct ssh_compress *cscomp_tobe = NULL;
    static const struct ssh_compress *sccomp_tobe = NULL;
    static char *hostkeydata, *sigdata, *keystr, *fingerprint;
    static int hostkeylen, siglen;
    static unsigned char exchange_hash[20];
    static unsigned char keyspace[40];
    static const struct ssh_cipher *preferred_cipher;

    crBegin;
    random_init();

    /*
     * Set up the preferred cipher.
     */
    if (cfg.cipher == CIPHER_BLOWFISH) {
        preferred_cipher = &ssh_blowfish_ssh2;
    } else if (cfg.cipher == CIPHER_DES) {
        logevent("Single DES not supported in SSH2; using 3DES");
        preferred_cipher = &ssh_3des_ssh2;
    } else if (cfg.cipher == CIPHER_3DES) {
        preferred_cipher = &ssh_3des_ssh2;
    } else {
        /* Shouldn't happen, but we do want to initialise to _something_. */
        preferred_cipher = &ssh_3des_ssh2;
    }

    /*
     * Be prepared to work around the buggy MAC problem.
     */
    if (cfg.buggymac)
        maclist = buggymacs, nmacs = lenof(buggymacs);
    else
        maclist = macs, nmacs = lenof(macs);

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
    for (i = 0; i < lenof(ciphers)+1; i++) {
        const struct ssh_cipher *c = i==0 ? preferred_cipher : ciphers[i-1];
        ssh2_pkt_addstring_str(c->name);
        if (i < lenof(ciphers))
            ssh2_pkt_addstring_str(",");
    }
    /* List server->client encryption algorithms. */
    ssh2_pkt_addstring_start();
    for (i = 0; i < lenof(ciphers)+1; i++) {
        const struct ssh_cipher *c = i==0 ? preferred_cipher : ciphers[i-1];
        ssh2_pkt_addstring_str(c->name);
        if (i < lenof(ciphers))
            ssh2_pkt_addstring_str(",");
    }
    /* List client->server MAC algorithms. */
    ssh2_pkt_addstring_start();
    for (i = 0; i < nmacs; i++) {
        ssh2_pkt_addstring_str(maclist[i]->name);
        if (i < nmacs-1)
            ssh2_pkt_addstring_str(",");
    }
    /* List server->client MAC algorithms. */
    ssh2_pkt_addstring_start();
    for (i = 0; i < nmacs; i++) {
        ssh2_pkt_addstring_str(maclist[i]->name);
        if (i < nmacs-1)
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
        bombout(("expected key exchange packet from server"));
        crReturn(0);
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
    for (i = 0; i < lenof(ciphers)+1; i++) {
        const struct ssh_cipher *c = i==0 ? preferred_cipher : ciphers[i-1];
        if (in_commasep_string(c->name, str, len)) {
            cscipher_tobe = c;
            break;
        }
    }
    ssh2_pkt_getstring(&str, &len);    /* server->client cipher */
    for (i = 0; i < lenof(ciphers)+1; i++) {
        const struct ssh_cipher *c = i==0 ? preferred_cipher : ciphers[i-1];
        if (in_commasep_string(c->name, str, len)) {
            sccipher_tobe = c;
            break;
        }
    }
    ssh2_pkt_getstring(&str, &len);    /* client->server mac */
    for (i = 0; i < nmacs; i++) {
        if (in_commasep_string(maclist[i]->name, str, len)) {
            csmac_tobe = maclist[i];
            break;
        }
    }
    ssh2_pkt_getstring(&str, &len);    /* server->client mac */
    for (i = 0; i < nmacs; i++) {
        if (in_commasep_string(maclist[i]->name, str, len)) {
            scmac_tobe = maclist[i];
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
    if (kex != &ssh_diffiehellman || hostkey != &ssh_dss) {
        bombout(("internal fault: chaos in SSH 2 transport layer"));
        crReturn(0);
    }

    /*
     * Now we begin the fun. Generate and send e for Diffie-Hellman.
     */
    e = dh_create_e();
    ssh2_pkt_init(SSH2_MSG_KEXDH_INIT);
    ssh2_pkt_addmp(e);
    ssh2_pkt_send();

    crWaitUntil(ispkt);
    if (pktin.type != SSH2_MSG_KEXDH_REPLY) {
        bombout(("expected key exchange packet from server"));
        crReturn(0);
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
    if (!hostkey->verifysig(sigdata, siglen, exchange_hash, 20)) {
        bombout(("Server failed host key check"));
        crReturn(0);
    }

    /*
     * Expect SSH2_MSG_NEWKEYS from server.
     */
    crWaitUntil(ispkt);
    if (pktin.type != SSH2_MSG_NEWKEYS) {
        bombout(("expected new-keys packet from server"));
        crReturn(0);
    }

    /*
     * Authenticate remote host: verify host key. (We've already
     * checked the signature of the exchange hash.)
     */
    keystr = hostkey->fmtkey();
    fingerprint = hostkey->fingerprint();
    verify_ssh_host_key(savedhost, savedport, hostkey->keytype,
                        keystr, fingerprint);
    logevent("Host key fingerprint is:");
    logevent(fingerprint);
    free(fingerprint);
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
    if (pktin.type != SSH2_MSG_SERVICE_ACCEPT) {
        bombout(("Server refused user authentication protocol"));
        crReturnV;
    }

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

	if ((flags & FLAG_INTERACTIVE) && !*cfg.username) {
	    c_write("login as: ", 10);
            ssh_send_ok = 1;
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
            if ((flags & FLAG_VERBOSE) || (flags & FLAG_INTERACTIVE)) {
		sprintf(stuff, "Using username \"%s\".\r\n", username);
		c_write(stuff, strlen(stuff));
	    }
	}

	if (ssh_get_password) {
	    char prompt[200];
	    sprintf(prompt, "%.90s@%.90s's password: ", username, savedhost);
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
            ssh_send_ok = 1;

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
    mainchan = malloc(sizeof(struct ssh_channel));
    mainchan->localid = 100;           /* as good as any */
    ssh2_pkt_init(SSH2_MSG_CHANNEL_OPEN);
    ssh2_pkt_addstring("session");
    ssh2_pkt_adduint32(mainchan->localid);
    ssh2_pkt_adduint32(0x8000UL);  /* our window size */
    ssh2_pkt_adduint32(0x4000UL);  /* our max pkt size */
    ssh2_pkt_send();
    crWaitUntilV(ispkt);
    if (pktin.type != SSH2_MSG_CHANNEL_OPEN_CONFIRMATION) {
        bombout(("Server refused to open a session"));
        crReturnV;
        /* FIXME: error data comes back in FAILURE packet */
    }
    if (ssh2_pkt_getuint32() != mainchan->localid) {
        bombout(("Server's channel confirmation cited wrong channel"));
        crReturnV;
    }
    mainchan->remoteid = ssh2_pkt_getuint32();
    mainchan->u.v2.remwindow = ssh2_pkt_getuint32();
    mainchan->u.v2.remmaxpkt = ssh2_pkt_getuint32();
    mainchan->u.v2.outbuffer = NULL;
    mainchan->u.v2.outbuflen = mainchan->u.v2.outbufsize = 0;
    logevent("Opened channel for session");

    /*
     * Now allocate a pty for the session.
     */
    if (!cfg.nopty) {
        ssh2_pkt_init(SSH2_MSG_CHANNEL_REQUEST);
        ssh2_pkt_adduint32(mainchan->remoteid); /* recipient channel */
        ssh2_pkt_addstring("pty-req");
        ssh2_pkt_addbool(1);           /* want reply */
        ssh2_pkt_addstring(cfg.termtype);
        ssh2_pkt_adduint32(cols);
        ssh2_pkt_adduint32(rows);
        ssh2_pkt_adduint32(0);         /* pixel width */
        ssh2_pkt_adduint32(0);         /* pixel height */
        ssh2_pkt_addstring_start();
        ssh2_pkt_addstring_data("\0", 1);/* TTY_OP_END, no special options */
        ssh2_pkt_send();

        do {
            crWaitUntilV(ispkt);
            if (pktin.type == SSH2_MSG_CHANNEL_WINDOW_ADJUST) {
                /* FIXME: be able to handle other channels here */
                if (ssh2_pkt_getuint32() != mainchan->localid)
                    continue;          /* wrong channel */
                mainchan->u.v2.remwindow += ssh2_pkt_getuint32();
            }
        } while (pktin.type == SSH2_MSG_CHANNEL_WINDOW_ADJUST);

        if (pktin.type != SSH2_MSG_CHANNEL_SUCCESS) {
            if (pktin.type != SSH2_MSG_CHANNEL_FAILURE) {
                bombout(("Server got confused by pty request"));
                crReturnV;
            }
            c_write("Server refused to allocate pty\r\n", 32);
        } else {
            logevent("Allocated pty");
        }
    }

    /*
     * Start a shell or a remote command.
     */
    ssh2_pkt_init(SSH2_MSG_CHANNEL_REQUEST);
    ssh2_pkt_adduint32(mainchan->remoteid); /* recipient channel */
    if (*cfg.remote_cmd) {
        ssh2_pkt_addstring("exec");
        ssh2_pkt_addbool(1);           /* want reply */
        ssh2_pkt_addstring(cfg.remote_cmd);
    } else {
        ssh2_pkt_addstring("shell");
        ssh2_pkt_addbool(1);           /* want reply */
    }
    ssh2_pkt_send();
    do {
        crWaitUntilV(ispkt);
        if (pktin.type == SSH2_MSG_CHANNEL_WINDOW_ADJUST) {
            /* FIXME: be able to handle other channels here */
            if (ssh2_pkt_getuint32() != mainchan->localid)
                continue;          /* wrong channel */
            mainchan->u.v2.remwindow += ssh2_pkt_getuint32();
        }
    } while (pktin.type == SSH2_MSG_CHANNEL_WINDOW_ADJUST);
    if (pktin.type != SSH2_MSG_CHANNEL_SUCCESS) {
        if (pktin.type != SSH2_MSG_CHANNEL_FAILURE) {
            bombout(("Server got confused by shell/command request"));
            crReturnV;
        }
        bombout(("Server refused to start a shell/command"));
        crReturnV;
    } else {
        logevent("Started a shell/command");
    }

    /*
     * Transfer data!
     */
    ssh_send_ok = 1;
    begin_session();
    while (1) {
        static int try_send;
	crReturnV;
        try_send = FALSE;
	if (ispkt) {
	    if (pktin.type == SSH2_MSG_CHANNEL_DATA ||
                pktin.type == SSH2_MSG_CHANNEL_EXTENDED_DATA) {
                char *data;
                int length;
                /* FIXME: be able to handle other channels here */
                if (ssh2_pkt_getuint32() != mainchan->localid)
                    continue;          /* wrong channel */
                if (pktin.type == SSH2_MSG_CHANNEL_EXTENDED_DATA &&
                    ssh2_pkt_getuint32() != SSH2_EXTENDED_DATA_STDERR)
                    continue;          /* extended but not stderr */
                ssh2_pkt_getstring(&data, &length);
                if (data) {
                    c_writedata(data, length);
                    /*
                     * Enlarge the window again at the remote side,
                     * just in case it ever runs down and they fail
                     * to send us any more data.
                     */
                    ssh2_pkt_init(SSH2_MSG_CHANNEL_WINDOW_ADJUST);
                    ssh2_pkt_adduint32(mainchan->remoteid);
                    ssh2_pkt_adduint32(length);
                    ssh2_pkt_send();
                }
	    } else if (pktin.type == SSH2_MSG_DISCONNECT) {
                ssh_state = SSH_STATE_CLOSED;
		logevent("Received disconnect message");
	    } else if (pktin.type == SSH2_MSG_CHANNEL_REQUEST) {
                continue;              /* exit status et al; ignore (FIXME?) */
	    } else if (pktin.type == SSH2_MSG_CHANNEL_EOF) {
                continue;              /* remote sends EOF; ignore */
	    } else if (pktin.type == SSH2_MSG_CHANNEL_CLOSE) {
                /* FIXME: be able to handle other channels here */
                if (ssh2_pkt_getuint32() != mainchan->localid)
                    continue;          /* wrong channel */
                ssh2_pkt_init(SSH2_MSG_CHANNEL_CLOSE);
                ssh2_pkt_adduint32(mainchan->remoteid);
                ssh2_pkt_send();
                /* FIXME: mark the channel as closed */
                if (1 /* FIXME: "all channels are closed" */) {
                    logevent("All channels closed. Disconnecting");
                    ssh2_pkt_init(SSH2_MSG_DISCONNECT);
                    ssh2_pkt_send();
                    ssh_state = SSH_STATE_CLOSED;
                    closesocket(s);
                    s = INVALID_SOCKET;
                }
                continue;              /* remote sends close; ignore (FIXME) */
	    } else if (pktin.type == SSH2_MSG_CHANNEL_WINDOW_ADJUST) {
                /* FIXME: be able to handle other channels here */
                if (ssh2_pkt_getuint32() != mainchan->localid)
                    continue;          /* wrong channel */
                mainchan->u.v2.remwindow += ssh2_pkt_getuint32();
                try_send = TRUE;
	    } else {
		bombout(("Strange packet received: type %d", pktin.type));
                crReturnV;
	    }
	} else {
            /*
             * We have spare data. Add it to the channel buffer.
             */
            if (mainchan->u.v2.outbufsize <
                mainchan->u.v2.outbuflen + inlen) {
                mainchan->u.v2.outbufsize =
                    mainchan->u.v2.outbuflen + inlen + 1024;
                mainchan->u.v2.outbuffer = srealloc(mainchan->u.v2.outbuffer,
                                                    mainchan->u.v2.outbufsize);
            }
            memcpy(mainchan->u.v2.outbuffer + mainchan->u.v2.outbuflen,
                   in, inlen);
            mainchan->u.v2.outbuflen += inlen;
            try_send = TRUE;
	}
        if (try_send) {
            /*
             * Try to send data on the channel if we can. (FIXME:
             * on _all_ channels.)
             */
            while (mainchan->u.v2.remwindow > 0 &&
                   mainchan->u.v2.outbuflen > 0) {
                unsigned len = mainchan->u.v2.remwindow;
                if (len > mainchan->u.v2.outbuflen)
                    len = mainchan->u.v2.outbuflen;
                if (len > mainchan->u.v2.remmaxpkt)
                    len = mainchan->u.v2.remmaxpkt;
                ssh2_pkt_init(SSH2_MSG_CHANNEL_DATA);
                ssh2_pkt_adduint32(mainchan->remoteid);
                ssh2_pkt_addstring_start();
                ssh2_pkt_addstring_data(mainchan->u.v2.outbuffer, len);
                ssh2_pkt_send();
                mainchan->u.v2.outbuflen -= len;
                memmove(mainchan->u.v2.outbuffer, mainchan->u.v2.outbuffer+len,
                        mainchan->u.v2.outbuflen);
                mainchan->u.v2.remwindow -= len;
            }
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

    if (WSAGETSELECTERROR(lParam) != 0) {
        closesocket(s);
        s = INVALID_SOCKET;
	return -WSAGETSELECTERROR(lParam);
    }

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
        if (ssh_version == 1) {
            send_packet(SSH1_CMSG_EOF, PKT_END);
        } else {
            ssh2_pkt_init(SSH2_MSG_CHANNEL_EOF);
            ssh2_pkt_adduint32(mainchan->remoteid);
            ssh2_pkt_send();
        }
        logevent("Sent EOF message");
    } else if (code == TS_PING) {
        if (ssh_version == 1) {
            send_packet(SSH1_MSG_IGNORE, PKT_STR, "", PKT_END);
        } else {
            ssh2_pkt_init(SSH2_MSG_IGNORE);
            ssh2_pkt_addstring_start();
            ssh2_pkt_send();
        }
    } else {
        /* do nothing */
    }
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
    ssh_sendok,
    22
};
