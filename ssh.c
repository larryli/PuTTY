#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>

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

#define bombout(msg) ( ssh_state = SSH_STATE_CLOSED, \
                          (s ? sk_close(s), s = NULL : 0), \
                          connection_fatal msg )

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
#define SSH1_SMSG_X11_OPEN                        27   /* 0x1b */
#define SSH1_CMSG_PORT_FORWARD_REQUEST            28   /* 0x1c */
#define SSH1_MSG_PORT_OPEN                        29   /* 0x1d */
#define SSH1_CMSG_AGENT_REQUEST_FORWARDING        30   /* 0x1e */
#define SSH1_SMSG_AGENT_OPEN                      31   /* 0x1f */
#define SSH1_MSG_IGNORE                           32   /* 0x20 */
#define SSH1_CMSG_EXIT_CONFIRMATION               33   /* 0x21 */
#define SSH1_CMSG_X11_REQUEST_FORWARDING          34   /* 0x22 */
#define SSH1_CMSG_AUTH_RHOSTS_RSA                 35   /* 0x23 */
#define SSH1_MSG_DEBUG                            36   /* 0x24 */
#define SSH1_CMSG_REQUEST_COMPRESSION             37   /* 0x25 */
#define SSH1_CMSG_AUTH_TIS                        39   /* 0x27 */
#define SSH1_SMSG_AUTH_TIS_CHALLENGE              40   /* 0x28 */
#define SSH1_CMSG_AUTH_TIS_RESPONSE               41   /* 0x29 */
#define SSH1_CMSG_AUTH_CCARD                      70   /* 0x46 */
#define SSH1_SMSG_AUTH_CCARD_CHALLENGE            71   /* 0x47 */
#define SSH1_CMSG_AUTH_CCARD_RESPONSE             72   /* 0x48 */

#define SSH1_AUTH_TIS                             5    /* 0x5 */
#define SSH1_AUTH_CCARD                           16   /* 0x10 */

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
#define SSH2_MSG_KEX_DH_GEX_REQUEST               30   /* 0x1e */
#define SSH2_MSG_KEX_DH_GEX_GROUP                 31   /* 0x1f */
#define SSH2_MSG_KEX_DH_GEX_INIT                  32   /* 0x20 */
#define SSH2_MSG_KEX_DH_GEX_REPLY                 33   /* 0x21 */
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

#define SSH2_DISCONNECT_HOST_NOT_ALLOWED_TO_CONNECT 1  /* 0x1 */
#define SSH2_DISCONNECT_PROTOCOL_ERROR            2    /* 0x2 */
#define SSH2_DISCONNECT_KEY_EXCHANGE_FAILED       3    /* 0x3 */
#define SSH2_DISCONNECT_HOST_AUTHENTICATION_FAILED 4   /* 0x4 */
#define SSH2_DISCONNECT_MAC_ERROR                 5    /* 0x5 */
#define SSH2_DISCONNECT_COMPRESSION_ERROR         6    /* 0x6 */
#define SSH2_DISCONNECT_SERVICE_NOT_AVAILABLE     7    /* 0x7 */
#define SSH2_DISCONNECT_PROTOCOL_VERSION_NOT_SUPPORTED 8 /* 0x8 */
#define SSH2_DISCONNECT_HOST_KEY_NOT_VERIFIABLE   9    /* 0x9 */
#define SSH2_DISCONNECT_CONNECTION_LOST           10   /* 0xa */
#define SSH2_DISCONNECT_BY_APPLICATION            11   /* 0xb */

#define SSH2_OPEN_ADMINISTRATIVELY_PROHIBITED     1    /* 0x1 */
#define SSH2_OPEN_CONNECT_FAILED                  2    /* 0x2 */
#define SSH2_OPEN_UNKNOWN_CHANNEL_TYPE            3    /* 0x3 */
#define SSH2_OPEN_RESOURCE_SHORTAGE               4    /* 0x4 */

#define SSH2_EXTENDED_DATA_STDERR                 1    /* 0x1 */

/*
 * Various remote-bug flags.
 */
#define BUG_CHOKES_ON_SSH1_IGNORE                 1
#define BUG_SSH2_HMAC                             2

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

extern char *x11_init (Socket *, char *, void *);
extern void x11_close (Socket);
extern void x11_send  (Socket , char *, int);
extern void x11_invent_auth(char *, int, char *, int);

/*
 * Ciphers for SSH2. We miss out single-DES because it isn't
 * supported; also 3DES and Blowfish are both done differently from
 * SSH1. (3DES uses outer chaining; Blowfish has the opposite
 * endianness and different-sized keys.)
 */
const static struct ssh2_ciphers *ciphers[] = {
    &ssh2_aes,
    &ssh2_blowfish,
    &ssh2_3des,
};

const static struct ssh_kex *kex_algs[] = {
    &ssh_diffiehellman_gex,
    &ssh_diffiehellman };

const static struct ssh_signkey *hostkey_algs[] = { &ssh_rsa, &ssh_dss };

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

static void ssh_comp_none_init(void) { }
static int ssh_comp_none_block(unsigned char *block, int len,
			       unsigned char **outblock, int *outlen) {
    return 0;
}
static int ssh_comp_none_disable(void) { return 0; }
const static struct ssh_compress ssh_comp_none = {
    "none",
    ssh_comp_none_init, ssh_comp_none_block,
    ssh_comp_none_init, ssh_comp_none_block,
    ssh_comp_none_disable
};
extern const struct ssh_compress ssh_zlib;
const static struct ssh_compress *compressions[] = {
    &ssh_zlib, &ssh_comp_none };

enum {                                 /* channel types */
    CHAN_MAINSESSION,
    CHAN_X11,
    CHAN_AGENT,
};

/*
 * 2-3-4 tree storing channels.
 */
struct ssh_channel {
    unsigned remoteid, localid;
    int type;
    int closes;
    struct ssh2_data_channel {
        unsigned char *outbuffer;
        unsigned outbuflen, outbufsize;
        unsigned remwindow, remmaxpkt;
    } v2;
    union {
        struct ssh_agent_channel {
            unsigned char *message;
            unsigned char msglen[4];
            int lensofar, totallen;
        } a;
        struct ssh_x11_channel {
            Socket s;
        } x11;
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

static SHA_State exhash, exhashbase;

static Socket s = NULL;

static unsigned char session_key[32];
static int ssh1_compressing;
static int ssh_agentfwd_enabled;
static int ssh_X11_fwd_enabled;
static int ssh_remote_bugs;
static const struct ssh_cipher *cipher = NULL;
static const struct ssh2_cipher *cscipher = NULL;
static const struct ssh2_cipher *sccipher = NULL;
static const struct ssh_mac *csmac = NULL;
static const struct ssh_mac *scmac = NULL;
static const struct ssh_compress *cscomp = NULL;
static const struct ssh_compress *sccomp = NULL;
static const struct ssh_kex *kex = NULL;
static const struct ssh_signkey *hostkey = NULL;
static unsigned char ssh2_session_id[20];
int (*ssh_get_line)(const char *prompt, char *str, int maxlen,
                    int is_pw) = NULL;

static char *savedhost;
static int savedport;
static int ssh_send_ok;
static int ssh_echoing, ssh_editing;

static tree234 *ssh_channels;           /* indexed by local id */
static struct ssh_channel *mainchan;   /* primary session channel */

static enum {
    SSH_STATE_PREPACKET,
    SSH_STATE_BEFORE_SIZE,
    SSH_STATE_INTERMED,
    SSH_STATE_SESSION,
    SSH_STATE_CLOSED
} ssh_state = SSH_STATE_PREPACKET;

static int size_needed = FALSE, eof_needed = FALSE;

static struct Packet pktin = { 0, 0, NULL, NULL, 0 };
static struct Packet pktout = { 0, 0, NULL, NULL, 0 };
static unsigned char *deferred_send_data = NULL;
static int deferred_len = 0, deferred_size = 0;

static int ssh_version;
static void (*ssh_protocol)(unsigned char *in, int inlen, int ispkt);
static void ssh1_protocol(unsigned char *in, int inlen, int ispkt);
static void ssh2_protocol(unsigned char *in, int inlen, int ispkt);
static void ssh_size(void);
static void ssh_special (Telnet_Special);
static void ssh2_try_send(struct ssh_channel *c);
static void ssh2_add_channel_data(struct ssh_channel *c, char *buf, int len);

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

static void c_write (char *buf, int len) {
    if ((flags & FLAG_STDERR)) {
        int i;
        for (i = 0; i < len; i++)
            if (buf[i] != '\r')
                fputc(buf[i], stderr);
	return;
    }
    from_backend(1, buf, len);
}

static void c_write_untrusted(char *buf, int len) {
    int i;
    for (i = 0; i < len; i++) {
        if (buf[i] == '\n')
            c_write("\r\n", 2);
        else if ((buf[i] & 0x60) || (buf[i] == '\r'))
            c_write(buf+i, 1);
    }
}

static void c_write_str (char *buf) {
    c_write(buf, strlen(buf));
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
	pktin.data = (pktin.data == NULL ? smalloc(st->biglen+APIEXTRA) :
	              srealloc(pktin.data, st->biglen+APIEXTRA));
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
#if 0
    debug(("Got packet len=%d pad=%d\r\n", st->len, st->pad));
    for (st->i = 0; st->i < st->biglen; st->i++)
        debug(("  %02x", (unsigned char)pktin.data[st->i]));
    debug(("\r\n"));
#endif

    st->realcrc = crc32(pktin.data, st->biglen-4);
    st->gotcrc = GET_32BIT(pktin.data+st->biglen-4);
    if (st->gotcrc != st->realcrc) {
	bombout(("Incorrect CRC received on packet"));
        crReturn(0);
    }

    pktin.body = pktin.data + st->pad + 1;

    if (ssh1_compressing) {
	unsigned char *decompblk;
	int decomplen;
#if 0
	int i;
	debug(("Packet payload pre-decompression:\n"));
	for (i = -1; i < pktin.length; i++)
	    debug(("  %02x", (unsigned char)pktin.body[i]));
	debug(("\r\n"));
#endif
	zlib_decompress_block(pktin.body-1, pktin.length+1,
			      &decompblk, &decomplen);

	if (pktin.maxlen < st->pad + decomplen) {
	    pktin.maxlen = st->pad + decomplen;
	    pktin.data = srealloc(pktin.data, pktin.maxlen+APIEXTRA);
            pktin.body = pktin.data + st->pad + 1;
	    if (!pktin.data)
		fatalbox("Out of memory");
	}

	memcpy(pktin.body-1, decompblk, decomplen);
	sfree(decompblk);
	pktin.length = decomplen-1;
#if 0
	debug(("Packet payload post-decompression:\n"));
	for (i = -1; i < pktin.length; i++)
	    debug(("  %02x", (unsigned char)pktin.body[i]));
	debug(("\r\n"));
#endif
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

    pktin.type = pktin.body[-1];

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
	pktin.data = (pktin.data == NULL ? smalloc(st->cipherblk+APIEXTRA) :
	              srealloc(pktin.data, st->cipherblk+APIEXTRA));
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
	pktin.data = (pktin.data == NULL ? smalloc(pktin.maxlen+APIEXTRA) :
	              srealloc(pktin.data, pktin.maxlen+APIEXTRA));
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

    /*
     * Decompress packet payload.
     */
    {
	unsigned char *newpayload;
	int newlen;
	if (sccomp && sccomp->decompress(pktin.data+5, pktin.length-5,
					 &newpayload, &newlen)) {
	    if (pktin.maxlen < newlen+5) {
		pktin.maxlen = newlen+5;
		pktin.data = (pktin.data == NULL ? smalloc(pktin.maxlen+APIEXTRA) :
			      srealloc(pktin.data, pktin.maxlen+APIEXTRA));
		if (!pktin.data)
		    fatalbox("Out of memory");
	    }
	    pktin.length = 5 + newlen;
	    memcpy(pktin.data+5, newpayload, newlen);
#if 0
	    debug(("Post-decompression payload:\r\n"));
	    for (st->i = 0; st->i < newlen; st->i++)
		debug(("  %02x", (unsigned char)pktin.data[5+st->i]));
	    debug(("\r\n"));
#endif

	    sfree(newpayload);
	}
    }

    pktin.savedpos = 6;
    pktin.type = pktin.data[5];

    if (pktin.type == SSH2_MSG_IGNORE || pktin.type == SSH2_MSG_DEBUG)
        goto next_packet;              /* FIXME: print DEBUG message */

    crFinish(0);
}

static void ssh1_pktout_size(int len) {
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
	pktout.data = (pktout.data == NULL ? smalloc(biglen+12) :
		       srealloc(pktout.data, biglen+12));
#else
	pktout.data = (pktout.data == NULL ? smalloc(biglen+4) :
		       srealloc(pktout.data, biglen+4));
#endif
	if (!pktout.data)
	    fatalbox("Out of memory");
    }
    pktout.body = pktout.data+4+pad+1;
}

static void s_wrpkt_start(int type, int len) {
    ssh1_pktout_size(len);
    pktout.type = type;
}

static int s_wrpkt_prepare(void) {
    int pad, len, biglen, i;
    unsigned long crc;

    pktout.body[-1] = pktout.type;

#if 0
    debug(("Packet payload pre-compression:\n"));
    for (i = -1; i < pktout.length; i++)
        debug(("  %02x", (unsigned char)pktout.body[i]));
    debug(("\r\n"));
#endif

    if (ssh1_compressing) {
	unsigned char *compblk;
	int complen;
	zlib_compress_block(pktout.body-1, pktout.length+1,
			    &compblk, &complen);
	ssh1_pktout_size(complen-1);
	memcpy(pktout.body-1, compblk, complen);
	sfree(compblk);
#if 0
	debug(("Packet payload post-compression:\n"));
	for (i = -1; i < pktout.length; i++)
	    debug(("  %02x", (unsigned char)pktout.body[i]));
	debug(("\r\n"));
#endif
    }

    len = pktout.length + 5;	       /* type and CRC */
    pad = 8 - (len%8);
    biglen = len + pad;

    for (i=0; i<pad; i++)
	pktout.data[i+4] = random_byte();
    crc = crc32(pktout.data+4, biglen-4);
    PUT_32BIT(pktout.data+biglen, crc);
    PUT_32BIT(pktout.data, len);

#if 0
    debug(("Sending packet len=%d\r\n", biglen+4));
    for (i = 0; i < biglen+4; i++)
        debug(("  %02x", (unsigned char)pktout.data[i]));
    debug(("\r\n"));
#endif
    if (cipher)
	cipher->encrypt(pktout.data+4, biglen);

    return biglen+4;
}

static void s_wrpkt(void) {
    int len;
    len = s_wrpkt_prepare();
    sk_write(s, pktout.data, len);
}

static void s_wrpkt_defer(void) {
    int len;
    len = s_wrpkt_prepare();
    if (deferred_len + len > deferred_size) {
        deferred_size = deferred_len + len + 128;
        deferred_send_data = srealloc(deferred_send_data, deferred_size);
    }
    memcpy(deferred_send_data+deferred_len, pktout.data, len);
    deferred_len += len;
}

/*
 * Construct a packet with the specified contents.
 */
static void construct_packet(int pkttype, va_list ap1, va_list ap2)
{
    unsigned char *p, *argp, argchar;
    unsigned long argint;
    int pktlen, argtype, arglen;
    Bignum bn;

    pktlen = 0;
    while ((argtype = va_arg(ap1, int)) != PKT_END) {
	switch (argtype) {
	  case PKT_INT:
	    (void) va_arg(ap1, int);
	    pktlen += 4;
	    break;
	  case PKT_CHAR:
	    (void) va_arg(ap1, char);
	    pktlen++;
	    break;
	  case PKT_DATA:
	    (void) va_arg(ap1, unsigned char *);
	    arglen = va_arg(ap1, int);
	    pktlen += arglen;
	    break;
	  case PKT_STR:
	    argp = va_arg(ap1, unsigned char *);
	    arglen = strlen(argp);
	    pktlen += 4 + arglen;
	    break;
	  case PKT_BIGNUM:
	    bn = va_arg(ap1, Bignum);
            pktlen += ssh1_bignum_length(bn);
	    break;
	  default:
	    assert(0);
	}
    }

    s_wrpkt_start(pkttype, pktlen);
    p = pktout.body;

    while ((argtype = va_arg(ap2, int)) != PKT_END) {
	switch (argtype) {
	  case PKT_INT:
	    argint = va_arg(ap2, int);
	    PUT_32BIT(p, argint);
	    p += 4;
	    break;
	  case PKT_CHAR:
	    argchar = va_arg(ap2, unsigned char);
	    *p = argchar;
	    p++;
	    break;
	  case PKT_DATA:
	    argp = va_arg(ap2, unsigned char *);
	    arglen = va_arg(ap2, int);
	    memcpy(p, argp, arglen);
	    p += arglen;
	    break;
	  case PKT_STR:
	    argp = va_arg(ap2, unsigned char *);
	    arglen = strlen(argp);
	    PUT_32BIT(p, arglen);
	    memcpy(p + 4, argp, arglen);
	    p += 4 + arglen;
	    break;
	  case PKT_BIGNUM:
	    bn = va_arg(ap2, Bignum);
            p += ssh1_write_bignum(p, bn);
	    break;
	}
    }
}

static void send_packet(int pkttype, ...) {
    va_list ap1, ap2;
    va_start(ap1, pkttype);
    va_start(ap2, pkttype);
    construct_packet(pkttype, ap1, ap2);
    s_wrpkt();
}

static void defer_packet(int pkttype, ...) {
    va_list ap1, ap2;
    va_start(ap1, pkttype);
    va_start(ap2, pkttype);
    construct_packet(pkttype, ap1, ap2);
    s_wrpkt_defer();
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
 * Utility routines for putting an SSH-protocol `string' and
 * `uint32' into a SHA state.
 */
#include <stdio.h>
static void sha_string(SHA_State *s, void *str, int len) {
    unsigned char lenblk[4];
    PUT_32BIT(lenblk, len);
    SHA_Bytes(s, lenblk, 4);
    SHA_Bytes(s, str, len);
}

static void sha_uint32(SHA_State *s, unsigned i) {
    unsigned char intblk[4];
    PUT_32BIT(intblk, i);
    SHA_Bytes(s, intblk, 4);
}

/*
 * SSH2 packet construction functions.
 */
static void ssh2_pkt_ensure(int length) {
    if (pktout.maxlen < length) {
        pktout.maxlen = length + 256;
	pktout.data = (pktout.data == NULL ? smalloc(pktout.maxlen+APIEXTRA) :
                       srealloc(pktout.data, pktout.maxlen+APIEXTRA));
        if (!pktout.data)
            fatalbox("Out of memory");
    }
}
static void ssh2_pkt_adddata(void *data, int len) {
    pktout.length += len;
    ssh2_pkt_ensure(pktout.length);
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
    int i, n = (ssh1_bignum_bitcount(b)+7)/8;
    p = smalloc(n + 1);
    if (!p)
        fatalbox("out of memory");
    p[0] = 0;
    for (i = 1; i <= n; i++)
        p[i] = bignum_byte(b, n-i);
    i = 0;
    while (i <= n && p[i] == 0 && (p[i+1] & 0x80) == 0)
        i++;
    memmove(p, p+i, n+1-i);
    *len = n+1-i;
    return p;
}
static void ssh2_pkt_addmp(Bignum b) {
    unsigned char *p;
    int len;
    p = ssh2_mpint_fmt(b, &len);
    ssh2_pkt_addstring_start();
    ssh2_pkt_addstring_data(p, len);
    sfree(p);
}

/*
 * Construct an SSH2 final-form packet: compress it, encrypt it,
 * put the MAC on it. Final packet, ready to be sent, is stored in
 * pktout.data. Total length is returned.
 */
static int ssh2_pkt_construct(void) {
    int cipherblk, maclen, padding, i;
    static unsigned long outgoing_sequence = 0;

    /*
     * Compress packet payload.
     */
#if 0
    debug(("Pre-compression payload:\r\n"));
    for (i = 5; i < pktout.length; i++)
	debug(("  %02x", (unsigned char)pktout.data[i]));
    debug(("\r\n"));
#endif
    {
	unsigned char *newpayload;
	int newlen;
	if (cscomp && cscomp->compress(pktout.data+5, pktout.length-5,
				       &newpayload, &newlen)) {
	    pktout.length = 5;
	    ssh2_pkt_adddata(newpayload, newlen);
	    sfree(newpayload);
	}
    }

    /*
     * Add padding. At least four bytes, and must also bring total
     * length (minus MAC) up to a multiple of the block size.
     */
    cipherblk = cscipher ? cscipher->blksize : 8;   /* block size */
    cipherblk = cipherblk < 8 ? 8 : cipherblk;   /* or 8 if blksize < 8 */
    padding = 4;
    padding += (cipherblk - (pktout.length + padding) % cipherblk) % cipherblk;
    maclen = csmac ? csmac->len : 0;
    ssh2_pkt_ensure(pktout.length + padding + maclen);
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

    /* Ready-to-send packet starts at pktout.data. We return length. */
    return pktout.length + padding + maclen;
}

/*
 * Construct and send an SSH2 packet immediately.
 */
static void ssh2_pkt_send(void) {
    int len = ssh2_pkt_construct();
    sk_write(s, pktout.data, len);
}

/*
 * Construct an SSH2 packet and add it to a deferred data block.
 * Useful for sending multiple packets in a single sk_write() call,
 * to prevent a traffic-analysing listener from being able to work
 * out the length of any particular packet (such as the password
 * packet).
 * 
 * Note that because SSH2 sequence-numbers its packets, this can
 * NOT be used as an m4-style `defer' allowing packets to be
 * constructed in one order and sent in another.
 */
static void ssh2_pkt_defer(void) {
    int len = ssh2_pkt_construct();
    if (deferred_len + len > deferred_size) {
        deferred_size = deferred_len + len + 128;
        deferred_send_data = srealloc(deferred_send_data, deferred_size);
    }
    memcpy(deferred_send_data+deferred_len, pktout.data, len);
    deferred_len += len;
}

/*
 * Send the whole deferred data block constructed by
 * ssh2_pkt_defer() or SSH1's defer_packet().
 */
static void ssh_pkt_defersend(void) {
    sk_write(s, deferred_send_data, deferred_len);
    deferred_len = deferred_size = 0;
    sfree(deferred_send_data);
    deferred_send_data = NULL;
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
    sfree(p);
}
#endif

static void sha_mpint(SHA_State *s, Bignum b) {
    unsigned char *p;
    int len;
    p = ssh2_mpint_fmt(b, &len);
    sha_string(s, p, len);
    sfree(p);
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
static int ssh2_pkt_getbool(void) {
    unsigned long value;
    if (pktin.length - pktin.savedpos < 1)
        return 0;                      /* arrgh, no way to decline (FIXME?) */
    value = pktin.data[pktin.savedpos] != 0;
    pktin.savedpos++;
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
    int length;
    Bignum b;

    ssh2_pkt_getstring(&p, &length);
    if (!p)
        return NULL;
    if (p[0] & 0x80) {
        bombout(("internal error: Can't handle negative mpints"));
        return NULL;
    }
    b = bignum_from_bytes(p, length);
    return b;
}

/*
 * Examine the remote side's version string and compare it against
 * a list of known buggy implementations.
 */
static void ssh_detect_bugs(char *vstring) {
    char *imp;                         /* pointer to implementation part */
    imp = vstring;
    imp += strcspn(imp, "-");
    imp += strcspn(imp, "-");

    ssh_remote_bugs = 0;

    if (!strcmp(imp, "1.2.18") || !strcmp(imp, "1.2.19") ||
        !strcmp(imp, "1.2.20") || !strcmp(imp, "1.2.21") ||
        !strcmp(imp, "1.2.22")) {
        /*
         * These versions don't support SSH1_MSG_IGNORE, so we have
         * to use a different defence against password length
         * sniffing.
         */
        ssh_remote_bugs |= BUG_CHOKES_ON_SSH1_IGNORE;
        logevent("We believe remote version has SSH1 ignore bug");
    }

    if (!strncmp(imp, "2.1.0", 5) || !strncmp(imp, "2.0.", 4) ||
        !strncmp(imp, "2.2.0", 5) || !strncmp(imp, "2.3.0", 5) ||
        !strncmp(imp, "2.1 ", 4)) {
        /*
         * These versions have the HMAC bug.
         */
        ssh_remote_bugs |= BUG_SSH2_HMAC;
        logevent("We believe remote version has SSH2 HMAC bug");
    }
}

static int do_ssh_init(unsigned char c) {
    static char *vsp;
    static char version[10];
    static char vstring[80];
    static char vlog[sizeof(vstring)+20];
    static int i;

    crBegin;

    /* Search for the string "SSH-" in the input. */
    i = 0;
    while (1) {
	static const int transS[] = { 1, 2, 2, 1 };
	static const int transH[] = { 0, 0, 3, 0 };
	static const int transminus[] = { 0, 0, 0, -1 };
	if (c == 'S') i = transS[i];
	else if (c == 'H') i = transH[i];
	else if (c == '-') i = transminus[i];
	else i = 0;
	if (i < 0)
	    break;
	crReturn(1);		       /* get another character */
    }

    strcpy(vstring, "SSH-");
    vsp = vstring+4;
    i = 0;
    while (1) {
	crReturn(1);		       /* get another char */
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

    ssh_agentfwd_enabled = FALSE;
    rdpkt2_state.incoming_sequence = 0;

    *vsp = 0;
    sprintf(vlog, "Server version: %s", vstring);
    ssh_detect_bugs(vstring);
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
        char verstring[80];
        sprintf(verstring, "SSH-2.0-%s", sshver);
        SHA_Init(&exhashbase);
        /*
         * Hash our version string and their version string.
         */
        sha_string(&exhashbase, verstring, strlen(verstring));
        sha_string(&exhashbase, vstring, strcspn(vstring, "\r\n"));
        sprintf(vstring, "%s\n", verstring);
        sprintf(vlog, "We claim version: %s", verstring);
        logevent(vlog);
        logevent("Using SSH protocol version 2");
        sk_write(s, vstring, strlen(vstring));
        ssh_protocol = ssh2_protocol;
        ssh_version = 2;
        s_rdpkt = ssh2_rdpkt;
    } else {
        /*
         * This is a v1 server. Begin v1 protocol.
         */
        sprintf(vstring, "SSH-%s-%s\n",
                (ssh_versioncmp(version, "1.5") <= 0 ? version : "1.5"),
                sshver);
        sprintf(vlog, "We claim version: %s", vstring);
        vlog[strcspn(vlog, "\r\n")] = '\0';
        logevent(vlog);
        logevent("Using SSH protocol version 1");
        sk_write(s, vstring, strlen(vstring));
        ssh_protocol = ssh1_protocol;
        ssh_version = 1;
        s_rdpkt = ssh1_rdpkt;
    }
    ssh_state = SSH_STATE_BEFORE_SIZE;

    crFinish(0);
}

static void ssh_gotdata(unsigned char *data, int datalen)
{
    crBegin;

    /*
     * To begin with, feed the characters one by one to the
     * protocol initialisation / selection function do_ssh_init().
     * When that returns 0, we're done with the initial greeting
     * exchange and can move on to packet discipline.
     */
    while (1) {
	int ret;
	if (datalen == 0)
	    crReturnV;		       /* more data please */
	ret = do_ssh_init(*data);
	data++; datalen--;
	if (ret == 0)
	    break;
    }

    /*
     * We emerge from that loop when the initial negotiation is
     * over and we have selected an s_rdpkt function. Now pass
     * everything to s_rdpkt, and then pass the resulting packets
     * to the proper protocol handler.
     */
    if (datalen == 0)
	crReturnV;
    while (1) {
	while (datalen > 0) {
	    if ( s_rdpkt(&data, &datalen) == 0 ) {
		ssh_protocol(NULL, 0, 1);
		if (ssh_state == SSH_STATE_CLOSED) {
		    return;
		}
	    }
	}
	crReturnV;
    }
    crFinishV;
}

static int ssh_closing (Plug plug, char *error_msg, int error_code, int calling_back) {
    ssh_state = SSH_STATE_CLOSED;
    sk_close(s);
    s = NULL;
    if (error_msg) {
        /* A socket error has occurred. */
        connection_fatal (error_msg);
    } else {
	/* Otherwise, the remote side closed the connection normally. */
    }
    return 0;
}

static int ssh_receive(Plug plug, int urgent, char *data, int len) {
    ssh_gotdata (data, len);
    if (ssh_state == SSH_STATE_CLOSED) {
        if (s) {
            sk_close(s);
            s = NULL;
        }
        return 0;
    }
    return 1;
}

/*
 * Connect to specified host and port.
 * Returns an error message, or NULL on success.
 * Also places the canonical host name into `realhost'.
 */
static char *connect_to_host(char *host, int port, char **realhost)
{
    static struct plug_function_table fn_table = {
	ssh_closing,
	ssh_receive
    }, *fn_table_ptr = &fn_table;

    SockAddr addr;
    char *err;
#ifdef FWHACK
    char *FWhost;
    int FWport;
#endif

    savedhost = smalloc(1+strlen(host));
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
    addr = sk_namelookup(host, realhost);
    if ( (err = sk_addr_error(addr)) )
	return err;

#ifdef FWHACK
    *realhost = FWhost;
#endif

    /*
     * Open socket.
     */
    s = sk_new(addr, port, 0, 1, &fn_table_ptr);
    if ( (err = sk_socket_error(s)) )
	return err;

#ifdef FWHACK
    sk_write(s, "connect ", 8);
    sk_write(s, FWhost, strlen(FWhost));
    {
	char buf[20];
	sprintf(buf, " %d\n", FWport);
	sk_write(s, buf, strlen(buf));
    }
#endif

    return NULL;
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

    rsabuf = smalloc(len);
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
        char *keystr = smalloc(len);
        if (!keystr)
            fatalbox("Out of memory");
        rsastr_fmt(keystr, &hostkey);
        rsa_fingerprint(fingerprint, sizeof(fingerprint), &hostkey);
        verify_ssh_host_key(savedhost, savedport, "rsa", keystr, fingerprint);
        sfree(keystr);
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

    switch (cfg.cipher) {
      case CIPHER_BLOWFISH: cipher_type = SSH_CIPHER_BLOWFISH; break;
      case CIPHER_DES:      cipher_type = SSH_CIPHER_DES;      break;
      case CIPHER_3DES:     cipher_type = SSH_CIPHER_3DES;     break;
      case CIPHER_AES:
        c_write_str("AES not supported in SSH1, falling back to 3DES\r\n");
        cipher_type = SSH_CIPHER_3DES;
        break;
    }
    if ((supported_ciphers_mask & (1 << cipher_type)) == 0) {
	c_write_str("Selected cipher not supported, falling back to 3DES\r\n");
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

    sfree(rsabuf);

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
            if (ssh_get_line) {
                if (!ssh_get_line("login as: ",
                                  username, sizeof(username), FALSE)) {
                    /*
                     * get_line failed to get a username.
                     * Terminate.
                     */
                    logevent("No username provided. Abandoning session.");
                    ssh_state = SSH_STATE_CLOSED;
                    crReturn(1);
                }
            } else {
                c_write_str("login as: ");
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
                            c_write_str("\b \b");
                            pos--;
                        }
                        break;
                      case 21: case 27:
                        while (pos > 0) {
                            c_write_str("\b \b");
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
                c_write_str("\r\n");
                username[strcspn(username, "\n\r")] = '\0';
            }
        } else {
	    strncpy(username, cfg.username, 99);
	    username[99] = '\0';
	}

	send_packet(SSH1_CMSG_USER, PKT_STR, username, PKT_END);
	{
	    char userlog[22+sizeof(username)];
	    sprintf(userlog, "Sent username \"%s\"", username);
	    logevent(userlog);
            if (flags & FLAG_INTERACTIVE &&
                (!((flags & FLAG_STDERR) && (flags & FLAG_VERBOSE)))) {
		strcat(userlog, "\r\n");
                c_write_str(userlog);
	    }
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
            request[4] = SSH1_AGENTC_REQUEST_RSA_IDENTITIES;
            agent_query(request, 5, &r, &responselen);
            response = (unsigned char *)r;
            if (response && responselen >= 5 &&
                response[4] == SSH1_AGENT_RSA_IDENTITIES_ANSWER) {
                p = response + 5;
                nkeys = GET_32BIT(p); p += 4;
                { char buf[64]; sprintf(buf, "Pageant has %d SSH1 keys", nkeys);
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
                        agentreq = smalloc(4 + len);
                        PUT_32BIT(agentreq, len);
                        q = agentreq + 4;
                        *q++ = SSH1_AGENTC_RSA_CHALLENGE;
                        PUT_32BIT(q, ssh1_bignum_bitcount(key.modulus));
                        q += 4;
                        q += ssh1_write_bignum(q, key.exponent);
                        q += ssh1_write_bignum(q, key.modulus);
                        q += ssh1_write_bignum(q, challenge);
                        memcpy(q, session_id, 16); q += 16;
                        PUT_32BIT(q, 1);   /* response format */
                        agent_query(agentreq, len+4, &ret, &retlen);
                        sfree(agentreq);
                        if (ret) {
                            if (ret[4] == SSH1_AGENT_RSA_RESPONSE) {
                                logevent("Sending Pageant's response");
                                send_packet(SSH1_CMSG_AUTH_RSA_RESPONSE,
                                            PKT_DATA, ret+5, 16, PKT_END);
                                sfree(ret);
                                crWaitUntil(ispkt);
                                if (pktin.type == SSH1_SMSG_SUCCESS) {
                                    logevent("Pageant's response accepted");
                                    if (flags & FLAG_VERBOSE) {
                                        c_write_str("Authenticated using RSA key \"");
                                        c_write(commentp, commentlen);
                                        c_write_str("\" from agent\r\n");
                                    }
                                    authed = TRUE;
                                } else
                                    logevent("Pageant's response not accepted");
                            } else {
                                logevent("Pageant failed to answer challenge");
                                sfree(ret);
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
                    c_write_str("TIS authentication refused.\r\n");
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
                c_write_str("CryptoCard authentication refused.\r\n");
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
                c_write_str("Trying public key authentication.\r\n");
            if (!rsakey_encrypted(cfg.keyfile, &comment)) {
                if (flags & FLAG_VERBOSE)
                    c_write_str("No passphrase required.\r\n");
                goto tryauth;
            }
            sprintf(prompt, "Passphrase for key \"%.100s\": ", comment);
            sfree(comment);
        }

	if (ssh_get_line) {
	    if (!ssh_get_line(prompt, password, sizeof(password), TRUE)) {
                /*
                 * get_line failed to get a password (for example
                 * because one was supplied on the command line
                 * which has already failed to work). Terminate.
                 */
                logevent("No more passwords to try");
                ssh_state = SSH_STATE_CLOSED;
                crReturn(1);
            }
	} else {
            c_write_str(prompt);
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
            c_write_str("\r\n");
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
                c_write_str("Couldn't load public key from ");
                c_write_str(cfg.keyfile);
                c_write_str(".\r\n");
                continue;              /* go and try password */
            }
            if (i == -1) {
                c_write_str("Wrong passphrase.\r\n");
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
                c_write_str("Server refused our public key.\r\n");
                continue;              /* go and try password */
            }
            if (pktin.type != SSH1_SMSG_AUTH_RSA_CHALLENGE) {
                bombout(("Bizarre response to offer of public key"));
                crReturn(0);
            }
            ssh1_read_bignum(pktin.body, &challenge);
            response = rsadecrypt(challenge, &pubkey);
            freebn(pubkey.private_exponent);   /* burn the evidence */

            for (i = 0; i < 32; i++) {
                buffer[i] = bignum_byte(response, 31-i);
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
                    c_write_str("Failed to authenticate with our public key.\r\n");
                continue;              /* go and try password */
            } else if (pktin.type != SSH1_SMSG_SUCCESS) {
                bombout(("Bizarre response to RSA authentication response"));
                crReturn(0);
            }

            break;                     /* we're through! */
        } else {
            if (pwpkt_type == SSH1_CMSG_AUTH_PASSWORD) {
                /*
                 * Defence against traffic analysis: we send a
                 * whole bunch of packets containing strings of
                 * different lengths. One of these strings is the
                 * password, in a SSH1_CMSG_AUTH_PASSWORD packet.
                 * The others are all random data in
                 * SSH1_MSG_IGNORE packets. This way a passive
                 * listener can't tell which is the password, and
                 * hence can't deduce the password length.
                 * 
                 * Anybody with a password length greater than 16
                 * bytes is going to have enough entropy in their
                 * password that a listener won't find it _that_
                 * much help to know how long it is. So what we'll
                 * do is:
                 * 
                 *  - if password length < 16, we send 15 packets
                 *    containing string lengths 1 through 15
                 * 
                 *  - otherwise, we let N be the nearest multiple
                 *    of 8 below the password length, and send 8
                 *    packets containing string lengths N through
                 *    N+7. This won't obscure the order of
                 *    magnitude of the password length, but it will
                 *    introduce a bit of extra uncertainty.
                 * 
                 * A few servers (the old 1.2.18 through 1.2.22)
                 * can't deal with SSH1_MSG_IGNORE. For these
                 * servers, we need an alternative defence. We make
                 * use of the fact that the password is interpreted
                 * as a C string: so we can append a NUL, then some
                 * random data.
                 */
                if (ssh_remote_bugs & BUG_CHOKES_ON_SSH1_IGNORE) {
                    char string[64];
                    char *s;
                    int len;

                    len = strlen(password);
                    if (len < sizeof(string)) {
                        s = string;
                        strcpy(string, password);
                        len++;         /* cover the zero byte */
                        while (len < sizeof(string)) {
                            string[len++] = (char)random_byte();
                        }
                    } else {
                        s = password;
                    }
                    send_packet(pwpkt_type, PKT_INT, len,
                                PKT_DATA, s, len, PKT_END);
                } else {
                    int bottom, top, pwlen, i;
                    char *randomstr;

                    pwlen = strlen(password);
                    if (pwlen < 16) {
                        bottom = 0;    /* zero length passwords are OK! :-) */
                        top = 15;
                    } else {
                        bottom = pwlen &~ 7;
                        top = bottom + 7;
                    }

                    assert(pwlen >= bottom && pwlen <= top);

                    randomstr = smalloc(top+1);

                    for (i = bottom; i <= top; i++) {
                        if (i == pwlen)
                            defer_packet(pwpkt_type, PKT_STR, password, PKT_END);
                        else {
                            for (j = 0; j < i; j++) {
                                do {
                                    randomstr[j] = random_byte();
                                } while (randomstr[j] == '\0');
                            }
                            randomstr[i] = '\0';
                            defer_packet(SSH1_MSG_IGNORE,
                                         PKT_STR, randomstr, PKT_END);
                        }
                    }
                    ssh_pkt_defersend();
                }
            } else {
                send_packet(pwpkt_type, PKT_STR, password, PKT_END);
            }
        }
	logevent("Sent password");
	memset(password, 0, strlen(password));
	crWaitUntil(ispkt);
	if (pktin.type == SSH1_SMSG_FAILURE) {
            if (flags & FLAG_VERBOSE)
                c_write_str("Access denied\r\n");
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

void sshfwd_close(struct ssh_channel *c) {
    if (c && !c->closes) {
        if (ssh_version == 1) {
            send_packet(SSH1_MSG_CHANNEL_CLOSE, PKT_INT, c->remoteid, PKT_END);
        } else {
            ssh2_pkt_init(SSH2_MSG_CHANNEL_CLOSE);
            ssh2_pkt_adduint32(c->remoteid);
            ssh2_pkt_send();
        }
        c->closes = 1;
        if (c->type == CHAN_X11) {
            c->u.x11.s = NULL;
            logevent("X11 connection terminated");
        }
    }
}

void sshfwd_write(struct ssh_channel *c, char *buf, int len) {
    if (ssh_version == 1) {
        send_packet(SSH1_MSG_CHANNEL_DATA,
                    PKT_INT, c->remoteid,
                    PKT_INT, len,
                    PKT_DATA, buf, len,
                    PKT_END);
    } else {
        ssh2_add_channel_data(c, buf, len);
        ssh2_try_send(c);
    }
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
        } else {
            logevent("Agent forwarding enabled");
	    ssh_agentfwd_enabled = TRUE;
	}
    }

    if (cfg.x11_forward) {
        char proto[20], data[64];
        logevent("Requesting X11 forwarding");
        x11_invent_auth(proto, sizeof(proto), data, sizeof(data));
        send_packet(SSH1_CMSG_X11_REQUEST_FORWARDING, 
		    PKT_STR, proto, PKT_STR, data,
		    PKT_INT, 0,
		    PKT_END);
        do { crReturnV; } while (!ispkt);
        if (pktin.type != SSH1_SMSG_SUCCESS && pktin.type != SSH1_SMSG_FAILURE) {
            bombout(("Protocol confusion"));
            crReturnV;
        } else if (pktin.type == SSH1_SMSG_FAILURE) {
            logevent("X11 forwarding refused");
        } else {
            logevent("X11 forwarding enabled");
	    ssh_X11_fwd_enabled = TRUE;
	}
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
            c_write_str("Server refused to allocate pty\r\n");
            ssh_editing = ssh_echoing = 1;
        }
	logevent("Allocated pty");
    } else {
        ssh_editing = ssh_echoing = 1;
    }

    if (cfg.compression) {
        send_packet(SSH1_CMSG_REQUEST_COMPRESSION, PKT_INT, 6, PKT_END);
        do { crReturnV; } while (!ispkt);
        if (pktin.type != SSH1_SMSG_SUCCESS && pktin.type != SSH1_SMSG_FAILURE) {
            bombout(("Protocol confusion"));
            crReturnV;
        } else if (pktin.type == SSH1_SMSG_FAILURE) {
            c_write_str("Server refused to compress\r\n");
        }
	logevent("Started compression");
	ssh1_compressing = TRUE;
	zlib_compress_init();
	zlib_decompress_init();
    }

    if (*cfg.remote_cmd)
        send_packet(SSH1_CMSG_EXEC_CMD, PKT_STR, cfg.remote_cmd, PKT_END);
    else
        send_packet(SSH1_CMSG_EXEC_SHELL, PKT_END);
    logevent("Started session");

    ssh_state = SSH_STATE_SESSION;
    if (size_needed)
	ssh_size();
    if (eof_needed)
        ssh_special(TS_EOF);

    ldisc_send(NULL, 0);               /* cause ldisc to notice changes */
    ssh_send_ok = 1;
    ssh_channels = newtree234(ssh_channelcmp);
    while (1) {
	crReturnV;
	if (ispkt) {
	    if (pktin.type == SSH1_SMSG_STDOUT_DATA ||
                pktin.type == SSH1_SMSG_STDERR_DATA) {
		long len = GET_32BIT(pktin.body);
		from_backend(pktin.type == SSH1_SMSG_STDERR_DATA,
			     pktin.body+4, len);
	    } else if (pktin.type == SSH1_MSG_DISCONNECT) {
                ssh_state = SSH_STATE_CLOSED;
		logevent("Received disconnect request");
                crReturnV;
            } else if (pktin.type == SSH1_SMSG_X11_OPEN) {
                /* Remote side is trying to open a channel to talk to our
                 * X-Server. Give them back a local channel number. */
                unsigned i;
                struct ssh_channel *c, *d;
                enum234 e;

		logevent("Received X11 connect request");
		/* Refuse if X11 forwarding is disabled. */
		if (!ssh_X11_fwd_enabled) {
		    send_packet(SSH1_MSG_CHANNEL_OPEN_FAILURE,
				PKT_INT, GET_32BIT(pktin.body),
				PKT_END);
		    logevent("Rejected X11 connect request");
		} else {
		    c = smalloc(sizeof(struct ssh_channel));

		    if ( x11_init(&c->u.x11.s, cfg.x11_display, c) != NULL ) {
		      logevent("opening X11 forward connection failed");
		      sfree(c);
		      send_packet(SSH1_MSG_CHANNEL_OPEN_FAILURE,
				  PKT_INT, GET_32BIT(pktin.body),
				  PKT_END);
		    } else {
		      logevent("opening X11 forward connection succeeded");
		      for (i=1, d = first234(ssh_channels, &e); d; d = next234(&e)) {
                          if (d->localid > i)
                              break;     /* found a free number */
                          i = d->localid + 1;
		      }
		      c->remoteid = GET_32BIT(pktin.body);
		      c->localid = i;
		      c->closes = 0;
		      c->type = CHAN_X11;   /* identify channel type */
		      add234(ssh_channels, c);
		      send_packet(SSH1_MSG_CHANNEL_OPEN_CONFIRMATION,
				  PKT_INT, c->remoteid, PKT_INT, c->localid,
				  PKT_END);
		      logevent("Opened X11 forward channel");
		    }
		}
            } else if (pktin.type == SSH1_SMSG_AGENT_OPEN) {
                /* Remote side is trying to open a channel to talk to our
                 * agent. Give them back a local channel number. */
                unsigned i;
                struct ssh_channel *c;
                enum234 e;

		/* Refuse if agent forwarding is disabled. */
		if (!ssh_agentfwd_enabled) {
		    send_packet(SSH1_MSG_CHANNEL_OPEN_FAILURE,
				PKT_INT, GET_32BIT(pktin.body),
				PKT_END);
		} else {
		    i = 1;
		    for (c = first234(ssh_channels, &e); c; c = next234(&e)) {
			if (c->localid > i)
			    break;     /* found a free number */
			i = c->localid + 1;
		    }
		    c = smalloc(sizeof(struct ssh_channel));
		    c->remoteid = GET_32BIT(pktin.body);
		    c->localid = i;
		    c->closes = 0;
		    c->type = CHAN_AGENT;   /* identify channel type */
		    c->u.a.lensofar = 0;
		    add234(ssh_channels, c);
		    send_packet(SSH1_MSG_CHANNEL_OPEN_CONFIRMATION,
				PKT_INT, c->remoteid, PKT_INT, c->localid,
				PKT_END);
		}
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
		    if ((c->closes == 0) && (c->type == CHAN_X11)) {
		        logevent("X11 connection closed");
			assert(c->u.x11.s != NULL);
			x11_close(c->u.x11.s);
			c->u.x11.s = NULL;
		    }
                    c->closes |= closetype;
                    if (c->closes == 3) {
                        del234(ssh_channels, c);
                        sfree(c);
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
                      case CHAN_X11:
			x11_send(c->u.x11.s, p, len);
			break;
                      case CHAN_AGENT:
                        /* Data for an agent message. Buffer it. */
                        while (len > 0) {
                            if (c->u.a.lensofar < 4) {
                                int l = min(4 - c->u.a.lensofar, len);
                                memcpy(c->u.a.msglen + c->u.a.lensofar, p, l);
                                p += l; len -= l; c->u.a.lensofar += l;
                            }
                            if (c->u.a.lensofar == 4) {
                                c->u.a.totallen = 4 + GET_32BIT(c->u.a.msglen);
                                c->u.a.message = smalloc(c->u.a.totallen);
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
                                    sfree(reply);
                                sfree(c->u.a.message);
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
	    while (inlen > 0) {
		int len = min(inlen, 512);
		send_packet(SSH1_CMSG_STDIN_DATA,
			    PKT_INT, len, PKT_DATA, in, len, PKT_END);
		in += len;
		inlen -= len;
	    }
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
static void ssh2_mkkey(Bignum K, char *H, char *sessid, char chr, char *keyspace) {
    SHA_State s;
    /* First 20 bytes. */
    SHA_Init(&s);
    sha_mpint(&s, K);
    SHA_Bytes(&s, H, 20);
    SHA_Bytes(&s, &chr, 1);
    SHA_Bytes(&s, sessid, 20);
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
    static int i, j, len, nbits, pbits;
    static char *str;
    static Bignum p, g, e, f, K;
    static int kex_init_value, kex_reply_value;
    static const struct ssh_mac **maclist;
    static int nmacs;
    static const struct ssh2_cipher *cscipher_tobe = NULL;
    static const struct ssh2_cipher *sccipher_tobe = NULL;
    static const struct ssh_mac *csmac_tobe = NULL;
    static const struct ssh_mac *scmac_tobe = NULL;
    static const struct ssh_compress *cscomp_tobe = NULL;
    static const struct ssh_compress *sccomp_tobe = NULL;
    static char *hostkeydata, *sigdata, *keystr, *fingerprint;
    static int hostkeylen, siglen;
    static void *hkey;		       /* actual host key */
    static unsigned char exchange_hash[20];
    static unsigned char keyspace[40];
    static const struct ssh2_ciphers *preferred_cipher;
    static const struct ssh_compress *preferred_comp;
    static int first_kex;

    crBegin;
    random_init();
    first_kex = 1;

    /*
     * Set up the preferred cipher and compression.
     */
    if (cfg.cipher == CIPHER_BLOWFISH) {
        preferred_cipher = &ssh2_blowfish;
    } else if (cfg.cipher == CIPHER_DES) {
        logevent("Single DES not supported in SSH2; using 3DES");
        preferred_cipher = &ssh2_3des;
    } else if (cfg.cipher == CIPHER_3DES) {
        preferred_cipher = &ssh2_3des;
    } else if (cfg.cipher == CIPHER_AES) {
        preferred_cipher = &ssh2_aes;
    } else {
        /* Shouldn't happen, but we do want to initialise to _something_. */
        preferred_cipher = &ssh2_3des;
    }
    if (cfg.compression)
	preferred_comp = &ssh_zlib;
    else
	preferred_comp = &ssh_comp_none;

    /*
     * Be prepared to work around the buggy MAC problem.
     */
    if (cfg.buggymac || (ssh_remote_bugs & BUG_SSH2_HMAC))
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
        const struct ssh2_ciphers *c = i==0 ? preferred_cipher : ciphers[i-1];
        for (j = 0; j < c->nciphers; j++) {
            ssh2_pkt_addstring_str(c->list[j]->name);
            if (i < lenof(ciphers) || j < c->nciphers-1)
                ssh2_pkt_addstring_str(",");
        }
    }
    /* List server->client encryption algorithms. */
    ssh2_pkt_addstring_start();
    for (i = 0; i < lenof(ciphers)+1; i++) {
        const struct ssh2_ciphers *c = i==0 ? preferred_cipher : ciphers[i-1];
        for (j = 0; j < c->nciphers; j++) {
            ssh2_pkt_addstring_str(c->list[j]->name);
            if (i < lenof(ciphers) || j < c->nciphers-1)
                ssh2_pkt_addstring_str(",");
        }
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
    for (i = 0; i < lenof(compressions)+1; i++) {
        const struct ssh_compress *c = i==0 ? preferred_comp : compressions[i-1];
        ssh2_pkt_addstring_str(c->name);
        if (i < lenof(compressions))
            ssh2_pkt_addstring_str(",");
    }
    /* List server->client compression algorithms. */
    ssh2_pkt_addstring_start();
    for (i = 0; i < lenof(compressions)+1; i++) {
        const struct ssh_compress *c = i==0 ? preferred_comp : compressions[i-1];
        ssh2_pkt_addstring_str(c->name);
        if (i < lenof(compressions))
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

    exhash = exhashbase;
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
        const struct ssh2_ciphers *c = i==0 ? preferred_cipher : ciphers[i-1];
        for (j = 0; j < c->nciphers; j++) {
            if (in_commasep_string(c->list[j]->name, str, len)) {
                cscipher_tobe = c->list[j];
                break;
            }
        }
        if (cscipher_tobe)
            break;
    }
    ssh2_pkt_getstring(&str, &len);    /* server->client cipher */
    for (i = 0; i < lenof(ciphers)+1; i++) {
        const struct ssh2_ciphers *c = i==0 ? preferred_cipher : ciphers[i-1];
        for (j = 0; j < c->nciphers; j++) {
            if (in_commasep_string(c->list[j]->name, str, len)) {
                sccipher_tobe = c->list[j];
                break;
            }
        }
        if (sccipher_tobe)
            break;
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
    for (i = 0; i < lenof(compressions)+1; i++) {
        const struct ssh_compress *c = i==0 ? preferred_comp : compressions[i-1];
        if (in_commasep_string(c->name, str, len)) {
            cscomp_tobe = c;
            break;
        }
    }
    ssh2_pkt_getstring(&str, &len);    /* server->client compression */
    for (i = 0; i < lenof(compressions)+1; i++) {
        const struct ssh_compress *c = i==0 ? preferred_comp : compressions[i-1];
        if (in_commasep_string(c->name, str, len)) {
            sccomp_tobe = c;
            break;
        }
    }

    /*
     * Work out the number of bits of key we will need from the key
     * exchange. We start with the maximum key length of either
     * cipher...
     */
    {
        int csbits, scbits;

	csbits = cscipher_tobe->keylen;
	scbits = sccipher_tobe->keylen;
	nbits = (csbits > scbits ? csbits : scbits);
    }
    /* The keys only have 160-bit entropy, since they're based on
     * a SHA-1 hash. So cap the key size at 160 bits. */
    if (nbits > 160) nbits = 160;

    /*
     * If we're doing Diffie-Hellman group exchange, start by
     * requesting a group.
     */
    if (kex == &ssh_diffiehellman_gex) {
        logevent("Doing Diffie-Hellman group exchange");
        /*
         * Work out how big a DH group we will need to allow that
         * much data.
	 */
        pbits = 512 << ((nbits-1) / 64);
        ssh2_pkt_init(SSH2_MSG_KEX_DH_GEX_REQUEST);
        ssh2_pkt_adduint32(pbits);
        ssh2_pkt_send();

        crWaitUntil(ispkt);
        if (pktin.type != SSH2_MSG_KEX_DH_GEX_GROUP) {
            bombout(("expected key exchange group packet from server"));
            crReturn(0);
        }
        p = ssh2_pkt_getmp();
        g = ssh2_pkt_getmp();
        dh_setup_group(p, g);
        kex_init_value = SSH2_MSG_KEX_DH_GEX_INIT;
        kex_reply_value = SSH2_MSG_KEX_DH_GEX_REPLY;
    } else {
        dh_setup_group1();
        kex_init_value = SSH2_MSG_KEXDH_INIT;
        kex_reply_value = SSH2_MSG_KEXDH_REPLY;
    }

    logevent("Doing Diffie-Hellman key exchange");
    /*
     * Now generate and send e for Diffie-Hellman.
     */
    e = dh_create_e(nbits*2);
    ssh2_pkt_init(kex_init_value);
    ssh2_pkt_addmp(e);
    ssh2_pkt_send();

    crWaitUntil(ispkt);
    if (pktin.type != kex_reply_value) {
        bombout(("expected key exchange reply packet from server"));
        crReturn(0);
    }
    ssh2_pkt_getstring(&hostkeydata, &hostkeylen);
    f = ssh2_pkt_getmp();
    ssh2_pkt_getstring(&sigdata, &siglen);

    K = dh_find_K(f);

    sha_string(&exhash, hostkeydata, hostkeylen);
    if (kex == &ssh_diffiehellman_gex) {
        sha_uint32(&exhash, pbits);
        sha_mpint(&exhash, p);
        sha_mpint(&exhash, g);
    }
    sha_mpint(&exhash, e);
    sha_mpint(&exhash, f);
    sha_mpint(&exhash, K);
    SHA_Final(&exhash, exchange_hash);

    dh_cleanup();

#if 0
    debug(("Exchange hash is:\r\n"));
    for (i = 0; i < 20; i++)
        debug((" %02x", exchange_hash[i]));
    debug(("\r\n"));
#endif

    hkey = hostkey->newkey(hostkeydata, hostkeylen);
    if (!hostkey->verifysig(hkey, sigdata, siglen, exchange_hash, 20)) {
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
    keystr = hostkey->fmtkey(hkey);
    fingerprint = hostkey->fingerprint(hkey);
    verify_ssh_host_key(savedhost, savedport, hostkey->keytype,
                        keystr, fingerprint);
    if (first_kex) {                /* don't bother logging this in rekeys */
	logevent("Host key fingerprint is:");
	logevent(fingerprint);
    }
    sfree(fingerprint);
    sfree(keystr);
    hostkey->freekey(hkey);

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
    cscomp->compress_init();
    sccomp->decompress_init();
    /*
     * Set IVs after keys. Here we use the exchange hash from the
     * _first_ key exchange.
     */
    if (first_kex)
	memcpy(ssh2_session_id, exchange_hash, sizeof(exchange_hash));
    ssh2_mkkey(K, exchange_hash, ssh2_session_id, 'C', keyspace);
    cscipher->setcskey(keyspace);
    ssh2_mkkey(K, exchange_hash, ssh2_session_id, 'D', keyspace);
    sccipher->setsckey(keyspace);
    ssh2_mkkey(K, exchange_hash, ssh2_session_id, 'A', keyspace);
    cscipher->setcsiv(keyspace);
    ssh2_mkkey(K, exchange_hash, ssh2_session_id, 'B', keyspace);
    sccipher->setsciv(keyspace);
    ssh2_mkkey(K, exchange_hash, ssh2_session_id, 'E', keyspace);
    csmac->setcskey(keyspace);
    ssh2_mkkey(K, exchange_hash, ssh2_session_id, 'F', keyspace);
    scmac->setsckey(keyspace);

    /*
     * If this is the first key exchange phase, we must pass the
     * SSH2_MSG_NEWKEYS packet to the next layer, not because it
     * wants to see it but because it will need time to initialise
     * itself before it sees an actual packet. In subsequent key
     * exchange phases, we don't pass SSH2_MSG_NEWKEYS on, because
     * it would only confuse the layer above.
     */
    if (!first_kex) {
        crReturn(0);
    }
    first_kex = 0;

    /*
     * Now we're encrypting. Begin returning 1 to the protocol main
     * function so that other things can run on top of the
     * transport. If we ever see a KEXINIT, we must go back to the
     * start.
     */
    while (!(ispkt && pktin.type == SSH2_MSG_KEXINIT)) {
        crReturn(1);
    }
    logevent("Server initiated key re-exchange");
    goto begin_key_exchange;

    crFinish(1);
}

/*
 * Add data to an SSH2 channel output buffer.
 */
static void ssh2_add_channel_data(struct ssh_channel *c, char *buf, int len) {
    if (c->v2.outbufsize <
        c->v2.outbuflen + len) {
        c->v2.outbufsize =
            c->v2.outbuflen + len + 1024;
        c->v2.outbuffer = srealloc(c->v2.outbuffer,
                                   c->v2.outbufsize);
    }
    memcpy(c->v2.outbuffer + c->v2.outbuflen,
           buf, len);
    c->v2.outbuflen += len;
}

/*
 * Attempt to send data on an SSH2 channel.
 */
static void ssh2_try_send(struct ssh_channel *c) {
    while (c->v2.remwindow > 0 &&
           c->v2.outbuflen > 0) {
        unsigned len = c->v2.remwindow;
        if (len > c->v2.outbuflen)
            len = c->v2.outbuflen;
        if (len > c->v2.remmaxpkt)
            len = c->v2.remmaxpkt;
        ssh2_pkt_init(SSH2_MSG_CHANNEL_DATA);
        ssh2_pkt_adduint32(c->remoteid);
        ssh2_pkt_addstring_start();
        ssh2_pkt_addstring_data(c->v2.outbuffer, len);
        ssh2_pkt_send();
        c->v2.outbuflen -= len;
        memmove(c->v2.outbuffer, c->v2.outbuffer+len,
                c->v2.outbuflen);
        c->v2.remwindow -= len;
    }
}

/*
 * Handle the SSH2 userauth and connection layers.
 */
static void do_ssh2_authconn(unsigned char *in, int inlen, int ispkt)
{
    static unsigned long remote_winsize;
    static unsigned long remote_maxpkt;
    static enum {
	AUTH_INVALID, AUTH_PUBLICKEY_AGENT, AUTH_PUBLICKEY_FILE, AUTH_PASSWORD
    } method;
    static enum {
        AUTH_TYPE_NONE,
        AUTH_TYPE_PUBLICKEY,
        AUTH_TYPE_PUBLICKEY_OFFER_LOUD,
        AUTH_TYPE_PUBLICKEY_OFFER_QUIET,
        AUTH_TYPE_PASSWORD
    } type;
    static int gotit, need_pw, can_pubkey, can_passwd;
    static int tried_pubkey_config, tried_agent;
    static int we_are_in;
    static char username[100];
    static char pwprompt[200];
    static char password[100];

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
     * We repeat this whole loop, including the username prompt,
     * until we manage a successful authentication. If the user
     * types the wrong _password_, they are sent back to the
     * beginning to try another username. (If they specify a
     * username in the config, they are never asked, even if they
     * do give a wrong password.)
     * 
     * I think this best serves the needs of
     * 
     *  - the people who have no configuration, no keys, and just
     *    want to try repeated (username,password) pairs until they
     *    type both correctly
     * 
     *  - people who have keys and configuration but occasionally
     *    need to fall back to passwords
     * 
     *  - people with a key held in Pageant, who might not have
     *    logged in to a particular machine before; so they want to
     *    type a username, and then _either_ their key will be
     *    accepted, _or_ they will type a password. If they mistype
     *    the username they will want to be able to get back and
     *    retype it!
     */
    do {
	static int pos;
	static char c;

	/*
	 * Get a username.
	 */
	pos = 0;
	if ((flags & FLAG_INTERACTIVE) && !*cfg.username) {
            if (ssh_get_line) {
                if (!ssh_get_line("login as: ",
                                  username, sizeof(username), FALSE)) {
                    /*
                     * get_line failed to get a username.
                     * Terminate.
                     */
                    logevent("No username provided. Abandoning session.");
                    ssh_state = SSH_STATE_CLOSED;
                    crReturnV;
                }
            } else {
                c_write_str("login as: ");
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
                            c_write_str("\b \b");
                            pos--;
                        }
                        break;
                      case 21: case 27:
                        while (pos > 0) {
                            c_write_str("\b \b");
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
            }
	    c_write_str("\r\n");
	    username[strcspn(username, "\n\r")] = '\0';
	} else {
	    char stuff[200];
	    strncpy(username, cfg.username, 99);
	    username[99] = '\0';
	    if ((flags & FLAG_VERBOSE) || (flags & FLAG_INTERACTIVE)) {
		sprintf(stuff, "Using username \"%s\".\r\n", username);
		c_write_str(stuff);
	    }
	}

	/*
	 * Send an authentication request using method "none": (a)
	 * just in case it succeeds, and (b) so that we know what
	 * authentication methods we can usefully try next.
	 */
	ssh2_pkt_init(SSH2_MSG_USERAUTH_REQUEST);
	ssh2_pkt_addstring(username);
	ssh2_pkt_addstring("ssh-connection");   /* service requested */
	ssh2_pkt_addstring("none");    /* method */
	ssh2_pkt_send();
	type = AUTH_TYPE_NONE;
	gotit = FALSE;
	we_are_in = FALSE;

	tried_pubkey_config = FALSE;
	tried_agent = FALSE;

	while (1) {
	    /*
	     * Wait for the result of the last authentication request.
	     */
	    if (!gotit)
		crWaitUntilV(ispkt);
	    while (pktin.type == SSH2_MSG_USERAUTH_BANNER) {
                char *banner;
                int size;
		ssh2_pkt_getstring(&banner, &size);
                if (banner)
                    c_write_untrusted(banner, size);
		crWaitUntilV(ispkt);
	    }
	    if (pktin.type == SSH2_MSG_USERAUTH_SUCCESS) {
		logevent("Access granted");
		we_are_in = TRUE;
		break;
	    }

	    if (pktin.type != SSH2_MSG_USERAUTH_FAILURE) {
		bombout(("Strange packet received during authentication: type %d",
			 pktin.type));
	    }

	    gotit = FALSE;

	    /*
	     * OK, we're now sitting on a USERAUTH_FAILURE message, so
	     * we can look at the string in it and know what we can
	     * helpfully try next.
	     */
	    {
		char *methods;
		int methlen;
		ssh2_pkt_getstring(&methods, &methlen);
		if (!ssh2_pkt_getbool()) {
		    /*
		     * We have received an unequivocal Access
		     * Denied. This can translate to a variety of
		     * messages:
		     * 
		     *  - if we'd just tried "none" authentication,
		     *    it's not worth printing anything at all
		     * 
		     *  - if we'd just tried a public key _offer_,
		     *    the message should be "Server refused our
		     *    key" (or no message at all if the key
		     *    came from Pageant)
		     * 
		     *  - if we'd just tried anything else, the
		     *    message really should be "Access denied".
		     * 
		     * Additionally, if we'd just tried password
		     * authentication, we should break out of this
		     * whole loop so as to go back to the username
		     * prompt.
		     */
		    if (type == AUTH_TYPE_NONE) {
			/* do nothing */
		    } else if (type == AUTH_TYPE_PUBLICKEY_OFFER_LOUD ||
			       type == AUTH_TYPE_PUBLICKEY_OFFER_QUIET) {
			if (type == AUTH_TYPE_PUBLICKEY_OFFER_LOUD)
			    c_write_str("Server refused our key\r\n");
			logevent("Server refused public key");
		    } else {
			c_write_str("Access denied\r\n");
			logevent("Access denied");
			if (type == AUTH_TYPE_PASSWORD) {
			    we_are_in = FALSE;
			    break;
			}
		    }
		} else {
		    c_write_str("Further authentication required\r\n");
		    logevent("Further authentication required");
		}

		can_pubkey = in_commasep_string("publickey", methods, methlen);
		can_passwd = in_commasep_string("password", methods, methlen);
	    }

	    method = 0;

	    if (!method && can_pubkey && agent_exists && !tried_agent) {
		/*
		 * Attempt public-key authentication using Pageant.
		 */
		static unsigned char request[5], *response, *p;
		static int responselen;
		static int i, nkeys;
		static int authed = FALSE;
		void *r;

		tried_agent = TRUE;

		logevent("Pageant is running. Requesting keys.");

		/* Request the keys held by the agent. */
		PUT_32BIT(request, 1);
		request[4] = SSH2_AGENTC_REQUEST_IDENTITIES;
		agent_query(request, 5, &r, &responselen);
		response = (unsigned char *)r;
		if (response && responselen >= 5 &&
                    response[4] == SSH2_AGENT_IDENTITIES_ANSWER) {
		    p = response + 5;
		    nkeys = GET_32BIT(p); p += 4;
		    { char buf[64]; sprintf(buf, "Pageant has %d SSH2 keys", nkeys);
			logevent(buf); }
		    for (i = 0; i < nkeys; i++) {
			static char *pkblob, *alg, *commentp;
			static int pklen, alglen, commentlen;
			static int siglen, retlen, len;
			static char *q, *agentreq, *ret;

			{ char buf[64]; sprintf(buf, "Trying Pageant key #%d", i);
			    logevent(buf); }
			pklen = GET_32BIT(p); p += 4;
			pkblob = p; p += pklen;
			alglen = GET_32BIT(pkblob);
			alg = pkblob + 4;
			commentlen = GET_32BIT(p); p += 4;
			commentp = p; p += commentlen;
			ssh2_pkt_init(SSH2_MSG_USERAUTH_REQUEST);
			ssh2_pkt_addstring(username);
			ssh2_pkt_addstring("ssh-connection");/* service requested */
			ssh2_pkt_addstring("publickey");/* method */
			ssh2_pkt_addbool(FALSE);   /* no signature included */
			ssh2_pkt_addstring_start();
			ssh2_pkt_addstring_data(alg, alglen);
			ssh2_pkt_addstring_start();
			ssh2_pkt_addstring_data(pkblob, pklen);
			ssh2_pkt_send();

			crWaitUntilV(ispkt);
			if (pktin.type != SSH2_MSG_USERAUTH_PK_OK) {
			    logevent("Key refused");
			    continue;
			}

			c_write_str("Authenticating with public key \"");
			c_write(commentp, commentlen);
			c_write_str("\" from agent\r\n");

			/*
			 * Server is willing to accept the key.
			 * Construct a SIGN_REQUEST.
			 */
			ssh2_pkt_init(SSH2_MSG_USERAUTH_REQUEST);
			ssh2_pkt_addstring(username);
			ssh2_pkt_addstring("ssh-connection");   /* service requested */
			ssh2_pkt_addstring("publickey");    /* method */
			ssh2_pkt_addbool(TRUE);
			ssh2_pkt_addstring_start();
			ssh2_pkt_addstring_data(alg, alglen);
			ssh2_pkt_addstring_start();
			ssh2_pkt_addstring_data(pkblob, pklen);

			siglen = pktout.length - 5 + 4 + 20;
			len = 1;   /* message type */
			len += 4 + pklen;   /* key blob */
			len += 4 + siglen;   /* data to sign */
			len += 4;  /* flags */
			agentreq = smalloc(4 + len);
			PUT_32BIT(agentreq, len);
			q = agentreq + 4;
			*q++ = SSH2_AGENTC_SIGN_REQUEST;
			PUT_32BIT(q, pklen); q += 4;
			memcpy(q, pkblob, pklen); q += pklen;
			PUT_32BIT(q, siglen); q += 4;
			/* Now the data to be signed... */
			PUT_32BIT(q, 20); q += 4;
			memcpy(q, ssh2_session_id, 20); q += 20;
			memcpy(q, pktout.data+5, pktout.length-5);
			q += pktout.length-5;
			/* And finally the (zero) flags word. */
			PUT_32BIT(q, 0);
			agent_query(agentreq, len+4, &ret, &retlen);
			sfree(agentreq);
			if (ret) {
			    if (ret[4] == SSH2_AGENT_SIGN_RESPONSE) {
				logevent("Sending Pageant's response");
				ssh2_pkt_addstring_start();
				ssh2_pkt_addstring_data(ret+9, GET_32BIT(ret+5));
				ssh2_pkt_send();
				authed = TRUE;
				break;
			    } else {
				logevent("Pageant failed to answer challenge");
				sfree(ret);
			    }
			}
		    }
		    if (authed)
			continue;
		}
	    }

	    if (!method && can_pubkey && *cfg.keyfile && !tried_pubkey_config) {
		unsigned char *pub_blob;
		char *algorithm, *comment;
		int pub_blob_len;

		tried_pubkey_config = TRUE;

		/*
		 * Try the public key supplied in the configuration.
		 *
		 * First, offer the public blob to see if the server is
		 * willing to accept it.
		 */
		pub_blob = ssh2_userkey_loadpub(cfg.keyfile, &algorithm,
						&pub_blob_len);
		if (pub_blob) {
		    ssh2_pkt_init(SSH2_MSG_USERAUTH_REQUEST);
		    ssh2_pkt_addstring(username);
		    ssh2_pkt_addstring("ssh-connection");   /* service requested */
		    ssh2_pkt_addstring("publickey");/* method */
		    ssh2_pkt_addbool(FALSE);   /* no signature included */
		    ssh2_pkt_addstring(algorithm);
		    ssh2_pkt_addstring_start();
		    ssh2_pkt_addstring_data(pub_blob, pub_blob_len);
		    ssh2_pkt_send();
		    logevent("Offered public key"); /* FIXME */

		    crWaitUntilV(ispkt);
		    if (pktin.type != SSH2_MSG_USERAUTH_PK_OK) {
			gotit = TRUE;
			type = AUTH_TYPE_PUBLICKEY_OFFER_LOUD;
			continue;	       /* key refused; give up on it */
		    }

		    logevent("Offer of public key accepted");
		    /*
		     * Actually attempt a serious authentication using
		     * the key.
		     */
		    if (ssh2_userkey_encrypted(cfg.keyfile, &comment)) {
			sprintf(pwprompt, "Passphrase for key \"%.100s\": ", comment);
			need_pw = TRUE;
		    } else {
			need_pw = FALSE;
		    }
		    c_write_str("Authenticating with public key \"");
		    c_write_str(comment);
		    c_write_str("\"\r\n");
		    method = AUTH_PUBLICKEY_FILE;
		}
	    }

	    if (!method && can_passwd) {
		method = AUTH_PASSWORD;
		sprintf(pwprompt, "%.90s@%.90s's password: ", username, savedhost);
		need_pw = TRUE;
	    }

	    if (need_pw) {
		if (ssh_get_line) {
		    if (!ssh_get_line(pwprompt, password,
                                      sizeof(password), TRUE)) {
			/*
			 * get_line failed to get a password (for
			 * example because one was supplied on the
			 * command line which has already failed to
			 * work). Terminate.
			 */
			logevent("No more passwords to try");
			ssh_state = SSH_STATE_CLOSED;
			crReturnV;
		    }
		} else {
		    static int pos = 0;
		    static char c;

		    c_write_str(pwprompt);
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
		    c_write_str("\r\n");
		}
	    }

	    if (method == AUTH_PUBLICKEY_FILE) {
		/*
		 * We have our passphrase. Now try the actual authentication.
		 */
		struct ssh2_userkey *key;

		key = ssh2_load_userkey(cfg.keyfile, password);
		if (key == SSH2_WRONG_PASSPHRASE || key == NULL) {
		    if (key == SSH2_WRONG_PASSPHRASE) {
			c_write_str("Wrong passphrase\r\n");
			tried_pubkey_config = FALSE;
		    } else {
			c_write_str("Unable to load private key\r\n");
			tried_pubkey_config = TRUE;
		    }
		    /* Send a spurious AUTH_NONE to return to the top. */
		    ssh2_pkt_init(SSH2_MSG_USERAUTH_REQUEST);
		    ssh2_pkt_addstring(username);
		    ssh2_pkt_addstring("ssh-connection");   /* service requested */
		    ssh2_pkt_addstring("none");    /* method */
		    ssh2_pkt_send();
		    type = AUTH_TYPE_NONE;
		} else {
		    unsigned char *blob, *sigdata;
		    int blob_len, sigdata_len;

		    /*
		     * We have loaded the private key and the server
		     * has announced that it's willing to accept it.
		     * Hallelujah. Generate a signature and send it.
		     */
		    ssh2_pkt_init(SSH2_MSG_USERAUTH_REQUEST);
		    ssh2_pkt_addstring(username);
		    ssh2_pkt_addstring("ssh-connection");   /* service requested */
		    ssh2_pkt_addstring("publickey");    /* method */
		    ssh2_pkt_addbool(TRUE);
		    ssh2_pkt_addstring(key->alg->name);
		    blob = key->alg->public_blob(key->data, &blob_len);
		    ssh2_pkt_addstring_start();
		    ssh2_pkt_addstring_data(blob, blob_len);
		    sfree(blob);

		    /*
		     * The data to be signed is:
		     *
		     *   string  session-id
		     *
		     * followed by everything so far placed in the
		     * outgoing packet.
		     */
		    sigdata_len = pktout.length - 5 + 4 + 20;
		    sigdata = smalloc(sigdata_len);
		    PUT_32BIT(sigdata, 20);
		    memcpy(sigdata+4, ssh2_session_id, 20);
		    memcpy(sigdata+24, pktout.data+5, pktout.length-5);
		    blob = key->alg->sign(key->data, sigdata, sigdata_len, &blob_len);
		    ssh2_pkt_addstring_start();
		    ssh2_pkt_addstring_data(blob, blob_len);
		    sfree(blob);
		    sfree(sigdata);

		    ssh2_pkt_send();
		    type = AUTH_TYPE_PUBLICKEY;
		}
	    } else if (method == AUTH_PASSWORD) {
		/*
		 * We send the password packet lumped tightly together with
		 * an SSH_MSG_IGNORE packet. The IGNORE packet contains a
		 * string long enough to make the total length of the two
		 * packets constant. This should ensure that a passive
		 * listener doing traffic analyis can't work out the length
		 * of the password.
		 *
		 * For this to work, we need an assumption about the
		 * maximum length of the password packet. I think 256 is
		 * pretty conservative. Anyone using a password longer than
		 * that probably doesn't have much to worry about from
		 * people who find out how long their password is!
		 */
		ssh2_pkt_init(SSH2_MSG_USERAUTH_REQUEST);
		ssh2_pkt_addstring(username);
		ssh2_pkt_addstring("ssh-connection");   /* service requested */
		ssh2_pkt_addstring("password");
		ssh2_pkt_addbool(FALSE);
		ssh2_pkt_addstring(password);
		ssh2_pkt_defer();
		/*
		 * We'll include a string that's an exact multiple of the
		 * cipher block size. If the cipher is NULL for some
		 * reason, we don't do this trick at all because we gain
		 * nothing by it.
		 */
                if (cscipher) {
                    int stringlen, i;

                    stringlen = (256 - deferred_len);
                    stringlen += cscipher->blksize - 1;
                    stringlen -= (stringlen % cscipher->blksize);
                    if (cscomp) {
                        /*
                         * Temporarily disable actual compression,
                         * so we can guarantee to get this string
                         * exactly the length we want it. The
                         * compression-disabling routine should
                         * return an integer indicating how many
                         * bytes we should adjust our string length
                         * by.
                         */
                        stringlen -= cscomp->disable_compression();
                    }
		    ssh2_pkt_init(SSH2_MSG_IGNORE);
		    ssh2_pkt_addstring_start();
		    for (i = 0; i < stringlen; i++) {
                        char c = (char)random_byte();
                        ssh2_pkt_addstring_data(&c, 1);
		    }
		    ssh2_pkt_defer();
		}
		ssh_pkt_defersend();
		logevent("Sent password");
		type = AUTH_TYPE_PASSWORD;
	    } else {
		c_write_str("No supported authentication methods left to try!\r\n");
		logevent("No supported authentications offered. Disconnecting");
		ssh2_pkt_init(SSH2_MSG_DISCONNECT);
		ssh2_pkt_adduint32(SSH2_DISCONNECT_BY_APPLICATION);
		ssh2_pkt_addstring("No supported authentication methods available");
		ssh2_pkt_addstring("en");   /* language tag */
		ssh2_pkt_send();
		ssh_state = SSH_STATE_CLOSED;
		crReturnV;
	    }
	}
    } while (!we_are_in);

    /*
     * Now we're authenticated for the connection protocol. The
     * connection protocol will automatically have started at this
     * point; there's no need to send SERVICE_REQUEST.
     */

    /*
     * So now create a channel with a session in it.
     */
    mainchan = smalloc(sizeof(struct ssh_channel));
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
    mainchan->type = CHAN_MAINSESSION;
    mainchan->closes = 0;
    mainchan->v2.remwindow = ssh2_pkt_getuint32();
    mainchan->v2.remmaxpkt = ssh2_pkt_getuint32();
    mainchan->v2.outbuffer = NULL;
    mainchan->v2.outbuflen = mainchan->v2.outbufsize = 0;
    ssh_channels = newtree234(ssh_channelcmp);
    add234(ssh_channels, mainchan);
    logevent("Opened channel for session");

    /*
     * Potentially enable X11 forwarding.
     */
    if (cfg.x11_forward) {
        char proto[20], data[64];
        logevent("Requesting X11 forwarding");
        x11_invent_auth(proto, sizeof(proto), data, sizeof(data));
        ssh2_pkt_init(SSH2_MSG_CHANNEL_REQUEST);
        ssh2_pkt_adduint32(mainchan->remoteid);
        ssh2_pkt_addstring("x11-req");
        ssh2_pkt_addbool(1);           /* want reply */
        ssh2_pkt_addbool(0);           /* many connections */
        ssh2_pkt_addstring(proto);
        ssh2_pkt_addstring(data);
        ssh2_pkt_adduint32(0);         /* screen number */
        ssh2_pkt_send();

        do {
            crWaitUntilV(ispkt);
            if (pktin.type == SSH2_MSG_CHANNEL_WINDOW_ADJUST) {
                unsigned i = ssh2_pkt_getuint32();
                struct ssh_channel *c;
                c = find234(ssh_channels, &i, ssh_channelfind);
                if (!c)
                    continue;          /* nonexistent channel */
                c->v2.remwindow += ssh2_pkt_getuint32();
            }
        } while (pktin.type == SSH2_MSG_CHANNEL_WINDOW_ADJUST);

        if (pktin.type != SSH2_MSG_CHANNEL_SUCCESS) {
            if (pktin.type != SSH2_MSG_CHANNEL_FAILURE) {
                bombout(("Server got confused by X11 forwarding request"));
                crReturnV;
            }
            logevent("X11 forwarding refused");
        } else {
            logevent("X11 forwarding enabled");
	    ssh_X11_fwd_enabled = TRUE;
        }
    }

    /*
     * Potentially enable agent forwarding.
     */
    if (cfg.agentfwd && agent_exists()) {
        logevent("Requesting OpenSSH-style agent forwarding");
        ssh2_pkt_init(SSH2_MSG_CHANNEL_REQUEST);
        ssh2_pkt_adduint32(mainchan->remoteid);
        ssh2_pkt_addstring("auth-agent-req@openssh.com");
        ssh2_pkt_addbool(1);           /* want reply */
        ssh2_pkt_send();

        do {
            crWaitUntilV(ispkt);
            if (pktin.type == SSH2_MSG_CHANNEL_WINDOW_ADJUST) {
                unsigned i = ssh2_pkt_getuint32();
                struct ssh_channel *c;
                c = find234(ssh_channels, &i, ssh_channelfind);
                if (!c)
                    continue;          /* nonexistent channel */
                c->v2.remwindow += ssh2_pkt_getuint32();
            }
        } while (pktin.type == SSH2_MSG_CHANNEL_WINDOW_ADJUST);

        if (pktin.type != SSH2_MSG_CHANNEL_SUCCESS) {
            if (pktin.type != SSH2_MSG_CHANNEL_FAILURE) {
                bombout(("Server got confused by agent forwarding request"));
                crReturnV;
            }
            logevent("Agent forwarding refused");
        } else {
            logevent("Agent forwarding enabled");
	    ssh_agentfwd_enabled = TRUE;
        }
    }

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
        ssh_state = SSH_STATE_INTERMED;

        do {
            crWaitUntilV(ispkt);
            if (pktin.type == SSH2_MSG_CHANNEL_WINDOW_ADJUST) {
                unsigned i = ssh2_pkt_getuint32();
                struct ssh_channel *c;
                c = find234(ssh_channels, &i, ssh_channelfind);
                if (!c)
                    continue;          /* nonexistent channel */
                c->v2.remwindow += ssh2_pkt_getuint32();
            }
        } while (pktin.type == SSH2_MSG_CHANNEL_WINDOW_ADJUST);

        if (pktin.type != SSH2_MSG_CHANNEL_SUCCESS) {
            if (pktin.type != SSH2_MSG_CHANNEL_FAILURE) {
                bombout(("Server got confused by pty request"));
                crReturnV;
            }
            c_write_str("Server refused to allocate pty\r\n");
            ssh_editing = ssh_echoing = 1;
        } else {
            logevent("Allocated pty");
        }
    } else {
        ssh_editing = ssh_echoing = 1;
    }

    /*
     * Start a shell or a remote command.
     */
    ssh2_pkt_init(SSH2_MSG_CHANNEL_REQUEST);
    ssh2_pkt_adduint32(mainchan->remoteid); /* recipient channel */
    if (cfg.ssh_subsys) {
        ssh2_pkt_addstring("subsystem");
        ssh2_pkt_addbool(1);           /* want reply */
        ssh2_pkt_addstring(cfg.remote_cmd);
    } else if (*cfg.remote_cmd) {
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
            unsigned i = ssh2_pkt_getuint32();
            struct ssh_channel *c;
            c = find234(ssh_channels, &i, ssh_channelfind);
            if (!c)
                continue;              /* nonexistent channel */
            c->v2.remwindow += ssh2_pkt_getuint32();
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

    ssh_state = SSH_STATE_SESSION;
    if (size_needed)
	ssh_size();
    if (eof_needed)
        ssh_special(TS_EOF);

    /*
     * Transfer data!
     */
    ldisc_send(NULL, 0);               /* cause ldisc to notice changes */
    ssh_send_ok = 1;
    while (1) {
        static int try_send;
	crReturnV;
        try_send = FALSE;
	if (ispkt) {
	    if (pktin.type == SSH2_MSG_CHANNEL_DATA ||
                pktin.type == SSH2_MSG_CHANNEL_EXTENDED_DATA) {
                char *data;
                int length;
                unsigned i = ssh2_pkt_getuint32();
                struct ssh_channel *c;
                c = find234(ssh_channels, &i, ssh_channelfind);
                if (!c)
                    continue;          /* nonexistent channel */
                if (pktin.type == SSH2_MSG_CHANNEL_EXTENDED_DATA &&
                    ssh2_pkt_getuint32() != SSH2_EXTENDED_DATA_STDERR)
                    continue;          /* extended but not stderr */
                ssh2_pkt_getstring(&data, &length);
                if (data) {
                    switch (c->type) {
                      case CHAN_MAINSESSION:
                        from_backend(pktin.type == SSH2_MSG_CHANNEL_EXTENDED_DATA,
                                     data, length);
                        break;
                      case CHAN_X11:
                        x11_send(c->u.x11.s, data, length);
                        break;
		      case CHAN_AGENT:
                        while (length > 0) {
                            if (c->u.a.lensofar < 4) {
                                int l = min(4 - c->u.a.lensofar, length);
                                memcpy(c->u.a.msglen + c->u.a.lensofar, data, l);
                                data += l; length -= l; c->u.a.lensofar += l;
                            }
                            if (c->u.a.lensofar == 4) {
                                c->u.a.totallen = 4 + GET_32BIT(c->u.a.msglen);
                                c->u.a.message = smalloc(c->u.a.totallen);
                                memcpy(c->u.a.message, c->u.a.msglen, 4);
                            }
                            if (c->u.a.lensofar >= 4 && length > 0) {
                                int l = min(c->u.a.totallen - c->u.a.lensofar,
					    length);
                                memcpy(c->u.a.message + c->u.a.lensofar, data, l);
                                data += l; length -= l; c->u.a.lensofar += l;
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
				ssh2_add_channel_data(c, sentreply, replylen);
				try_send = TRUE;
                                if (reply)
                                    sfree(reply);
                                sfree(c->u.a.message);
                                c->u.a.lensofar = 0;
                            }
                        }
                        break;
                    }
                    /*
                     * Enlarge the window again at the remote
                     * side, just in case it ever runs down and
                     * they fail to send us any more data.
                     */
                    ssh2_pkt_init(SSH2_MSG_CHANNEL_WINDOW_ADJUST);
                    ssh2_pkt_adduint32(c->remoteid);
                    ssh2_pkt_adduint32(length);
                    ssh2_pkt_send();
                }
	    } else if (pktin.type == SSH2_MSG_DISCONNECT) {
                ssh_state = SSH_STATE_CLOSED;
		logevent("Received disconnect message");
                crReturnV;
	    } else if (pktin.type == SSH2_MSG_CHANNEL_REQUEST) {
                continue;              /* exit status et al; ignore (FIXME?) */
	    } else if (pktin.type == SSH2_MSG_CHANNEL_EOF) {
                unsigned i = ssh2_pkt_getuint32();
                struct ssh_channel *c;

                c = find234(ssh_channels, &i, ssh_channelfind);
                if (!c)
                    continue;          /* nonexistent channel */
                
                if (c->type == CHAN_X11) {
                    /*
                     * Remote EOF on an X11 channel means we should
                     * wrap up and close the channel ourselves.
                     */
                    x11_close(c->u.x11.s);
                    sshfwd_close(c);
                } else if (c->type == CHAN_AGENT) {
		    sshfwd_close(c);
		}
	    } else if (pktin.type == SSH2_MSG_CHANNEL_CLOSE) {
                unsigned i = ssh2_pkt_getuint32();
                struct ssh_channel *c;
                enum234 e;

                c = find234(ssh_channels, &i, ssh_channelfind);
                if (!c)
                    continue;          /* nonexistent channel */
                if (c->closes == 0) {
                    ssh2_pkt_init(SSH2_MSG_CHANNEL_CLOSE);
                    ssh2_pkt_adduint32(c->remoteid);
                    ssh2_pkt_send();
                }
                /* Do pre-close processing on the channel. */
                switch (c->type) {
                  case CHAN_MAINSESSION:
                    break;             /* nothing to see here, move along */
                  case CHAN_X11:
                    break;
                  case CHAN_AGENT:
                    break;
                }
                del234(ssh_channels, c);
                sfree(c->v2.outbuffer);
                sfree(c);

                /*
                 * See if that was the last channel left open.
                 */
                c = first234(ssh_channels, &e);
                if (!c) {
                    logevent("All channels closed. Disconnecting");
                    ssh2_pkt_init(SSH2_MSG_DISCONNECT);
                    ssh2_pkt_adduint32(SSH2_DISCONNECT_BY_APPLICATION);
                    ssh2_pkt_addstring("All open channels closed");
                    ssh2_pkt_addstring("en");   /* language tag */
                    ssh2_pkt_send();
                    ssh_state = SSH_STATE_CLOSED;
                    crReturnV;
                }
                continue;              /* remote sends close; ignore (FIXME) */
	    } else if (pktin.type == SSH2_MSG_CHANNEL_WINDOW_ADJUST) {
                unsigned i = ssh2_pkt_getuint32();
                struct ssh_channel *c;
                c = find234(ssh_channels, &i, ssh_channelfind);
                if (!c)
                    continue;          /* nonexistent channel */
                mainchan->v2.remwindow += ssh2_pkt_getuint32();
                try_send = TRUE;
	    } else if (pktin.type == SSH2_MSG_CHANNEL_OPEN) {
                char *type;
                int typelen;
                char *error = NULL;
                struct ssh_channel *c;
                ssh2_pkt_getstring(&type, &typelen);
                c = smalloc(sizeof(struct ssh_channel));

                if (typelen == 3 && !memcmp(type, "x11", 3)) {
                    if (!ssh_X11_fwd_enabled)
                        error = "X11 forwarding is not enabled";
                    else if ( x11_init(&c->u.x11.s, cfg.x11_display, c) != NULL ) {
                        error = "Unable to open an X11 connection";
                    } else {
                        c->type = CHAN_X11;
                    }
                } else if (typelen == 22 &&
			   !memcmp(type, "auth-agent@openssh.com", 3)) {
                    if (!ssh_agentfwd_enabled)
                        error = "Agent forwarding is not enabled";
		    else {
			c->type = CHAN_AGENT;   /* identify channel type */
			c->u.a.lensofar = 0;
                    }
                } else {
                    error = "Unsupported channel type requested";
                }

                c->remoteid = ssh2_pkt_getuint32();
                if (error) {
                    ssh2_pkt_init(SSH2_MSG_CHANNEL_OPEN_FAILURE);
                    ssh2_pkt_adduint32(c->remoteid);
                    ssh2_pkt_adduint32(SSH2_OPEN_CONNECT_FAILED);
                    ssh2_pkt_addstring(error);
                    ssh2_pkt_addstring("en");   /* language tag */
                    ssh2_pkt_send();
                    sfree(c);
                } else {
                    struct ssh_channel *d;
                    unsigned i;
                    enum234 e;

                    for (i=1, d = first234(ssh_channels, &e); d;
                         d = next234(&e)) {
			if (d->localid > i)
                            break;     /* found a free number */
			i = d->localid + 1;
                    }
                    c->localid = i;
                    c->closes = 0;
                    c->v2.remwindow = ssh2_pkt_getuint32();
                    c->v2.remmaxpkt = ssh2_pkt_getuint32();
                    c->v2.outbuffer = NULL;
                    c->v2.outbuflen = c->v2.outbufsize = 0;
                    add234(ssh_channels, c);
                    ssh2_pkt_init(SSH2_MSG_CHANNEL_OPEN_CONFIRMATION);
                    ssh2_pkt_adduint32(c->remoteid);
                    ssh2_pkt_adduint32(c->localid);
                    ssh2_pkt_adduint32(0x8000UL);  /* our window size */
                    ssh2_pkt_adduint32(0x4000UL);  /* our max pkt size */
                    ssh2_pkt_send();
                }
	    } else {
		bombout(("Strange packet received: type %d", pktin.type));
                crReturnV;
	    }
	} else {
            /*
             * We have spare data. Add it to the channel buffer.
             */
            ssh2_add_channel_data(mainchan, in, inlen);
            try_send = TRUE;
	}
        if (try_send) {
            enum234 e;
            struct ssh_channel *c;
            /*
             * Try to send data on all channels if we can.
             */
            for (c = first234(ssh_channels, &e); c; c = next234(&e))
                ssh2_try_send(c);
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
 * Called to set up the connection.
 *
 * Returns an error message, or NULL on success.
 */
static char *ssh_init (char *host, int port, char **realhost) {
    char *p;
	
#ifdef MSCRYPTOAPI
    if(crypto_startup() == 0)
	return "Microsoft high encryption pack not installed!";
#endif

    ssh_send_ok = 0;
    ssh_editing = 0;
    ssh_echoing = 0;

    p = connect_to_host(host, port, realhost);
    if (p != NULL)
	return p;

    return NULL;
}

/*
 * Called to send data down the Telnet connection.
 */
static void ssh_send (char *buf, int len) {
    if (s == NULL || ssh_protocol == NULL)
	return;

    ssh_protocol(buf, len, 0);
}

/*
 * Called to set the size of the window from SSH's POV.
 */
static void ssh_size(void) {
    switch (ssh_state) {
      case SSH_STATE_BEFORE_SIZE:
      case SSH_STATE_PREPACKET:
      case SSH_STATE_CLOSED:
	break;			       /* do nothing */
      case SSH_STATE_INTERMED:
	size_needed = TRUE;	       /* buffer for later */
	break;
      case SSH_STATE_SESSION:
        if (!cfg.nopty) {
            if (ssh_version == 1) {
                send_packet(SSH1_CMSG_WINDOW_SIZE,
                            PKT_INT, rows, PKT_INT, cols,
                            PKT_INT, 0, PKT_INT, 0, PKT_END);
            } else {
                ssh2_pkt_init(SSH2_MSG_CHANNEL_REQUEST);
                ssh2_pkt_adduint32(mainchan->remoteid);
                ssh2_pkt_addstring("window-change");
                ssh2_pkt_addbool(0);
                ssh2_pkt_adduint32(cols);
                ssh2_pkt_adduint32(rows);
                ssh2_pkt_adduint32(0);
                ssh2_pkt_adduint32(0);
                ssh2_pkt_send();
            }
        }
        break;
    }
}

/*
 * Send Telnet special codes. TS_EOF is useful for `plink', so you
 * can send an EOF and collect resulting output (e.g. `plink
 * hostname sort').
 */
static void ssh_special (Telnet_Special code) {
    if (code == TS_EOF) {
        if (ssh_state != SSH_STATE_SESSION) {
            /*
             * Buffer the EOF in case we are pre-SESSION, so we can
             * send it as soon as we reach SESSION.
             */
            if (code == TS_EOF)
                eof_needed = TRUE;
            return;
        }
        if (ssh_version == 1) {
            send_packet(SSH1_CMSG_EOF, PKT_END);
        } else {
            ssh2_pkt_init(SSH2_MSG_CHANNEL_EOF);
            ssh2_pkt_adduint32(mainchan->remoteid);
            ssh2_pkt_send();
        }
        logevent("Sent EOF message");
    } else if (code == TS_PING) {
        if (ssh_state == SSH_STATE_CLOSED || ssh_state == SSH_STATE_PREPACKET)
            return;
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

static Socket ssh_socket(void) { return s; }

static int ssh_sendok(void) { return ssh_send_ok; }

static int ssh_ldisc(int option) {
    if (option == LD_ECHO) return ssh_echoing;
    if (option == LD_EDIT) return ssh_editing;
    return FALSE;
}

Backend ssh_backend = {
    ssh_init,
    ssh_send,
    ssh_size,
    ssh_special,
    ssh_socket,
    ssh_sendok,
    ssh_ldisc,
    22
};
