/*
 * Macintosh OpenTransport networking abstraction
 */

#if TARGET_API_MAC_CARBON
#define OTCARBONAPPLICATION 1
#endif

#include <Files.h> /* Needed by OpenTransportInternet.h */
#include <OpenTransport.h>
#include <OpenTptInternet.h>

#include <string.h>

#define DEFINE_PLUG_METHOD_MACROS
#include "putty.h"
#include "network.h"
#include "mac.h"

struct Socket_tag {
    struct socket_function_table *fn;
    /* other stuff... */
    OSStatus error;
    EndpointRef ep;
    Plug plug;
    void *private_ptr;
    bufchain output_data;
    int connected;
    int writable;
    int frozen; /* this causes readability notifications to be ignored */
    int frozen_readable; /* this means we missed at least one readability
                          * notification while we were frozen */
    int localhost_only;                /* for listening sockets */
    char oobdata[1];
    int sending_oob;
    int oobpending;        /* is there OOB data available to read?*/
    int oobinline;
    int pending_error;                 /* in case send() returns error */
    int listener;
    struct Socket_tag *next;
    struct Socket_tag **prev;
};

typedef struct Socket_tag *Actual_Socket;

struct SockAddr_tag {
    int resolved;
    OSStatus error;
    InetHostInfo hostinfo;
    char hostname[512];
};

/* Globals */

static struct {
    Actual_Socket socklist;
    InetSvcRef inetsvc;
} ot;

OSErr ot_init(void)
{
    OSStatus err;

    err = InitOpenTransport();
    if (err != kOTNoError) return err;
    ot.inetsvc = OTOpenInternetServices(kDefaultInternetServicesPath, 0, &err);
    return err;
}

void ot_cleanup(void)
{
    Actual_Socket s;

    for (s = ot.socklist; s !=NULL; s = s->next) {
	OTUnbind(s->ep);
	OTCloseProvider(s->ep);
    }

    CloseOpenTransport();
}

SockAddr ot_namelookup(char const *host, char **canonicalname)
{
    SockAddr ret = snew(struct SockAddr_tag);
    char *realhost;

    /* Casting away const -- hope OTInetStringToAddress is sensible */
    ret->error = OTInetStringToAddress(ot.inetsvc, (char *)host,
				       &ret->hostinfo);
    ret->resolved = TRUE;

    if (ret->error == kOTNoError)
	realhost = ret->hostinfo.name;
    else
	realhost = "";
    *canonicalname = snewn(1+strlen(realhost), char);
    strcpy(*canonicalname, realhost);
    return ret;
}

SockAddr ot_nonamelookup(char const *host)
{
    SockAddr ret = snew(struct SockAddr_tag);
    
    ret->resolved = FALSE;
    ret->error = kOTNoError;
    ret->hostname[0] = '\0';
    strncat(ret->hostname, host, lenof(ret->hostname) - 1);
    return ret;
}

void ot_getaddr(SockAddr addr, char *buf, int buflen)
{
    char mybuf[16];

    buf[0] = '\0';
    if (addr->resolved) {
	/* XXX only return first address */
	OTInetHostToString(addr->hostinfo.addrs[0], mybuf);
	strncat(buf, mybuf, buflen - 1);
    } else
	strncat(buf, addr->hostname, buflen - 1);
}

/* I think "local" here really means "loopback" */

int ot_hostname_is_local(char *name)
{

    return !strcmp(name, "localhost");
}

int ot_address_is_local(SockAddr addr)
{
    int i;

    if (addr->resolved)
	for (i = 0; i < kMaxHostAddrs; i++)
	    if (addr->hostinfo.addrs[i] & 0xff000000 == 0x7f000000)
		return TRUE;
    return FALSE;
}

int ot_addrtype(SockAddr addr)
{

    if (addr->resolved)
	return ADDRTYPE_IPV4;
    return ADDRTYPE_NAME;
}

void ot_addrcopy(SockAddr addr, char *buf)
{

    /* XXX only return first address */
    memcpy(buf, &addr->hostinfo.addrs[0], 4);
}

void ot_addr_free(SockAddr addr)
{
    sfree(addr);
}


static Plug ot_tcp_plug(Socket sock, Plug p)
{
    Actual_Socket s = (Actual_Socket) sock;
    Plug ret = s->plug;
    if (p)
	s->plug = p;
    return ret;
}

static void ot_tcp_flush(Socket s)
{
    /*
     * We send data to the socket as soon as we can anyway,
     * so we don't need to do anything here.  :-)
     */
}

static void ot_tcp_close(Socket s);
static int ot_tcp_write(Socket s, char const *data, int len);
static int ot_tcp_write_oob(Socket s, char const *data, int len);
static void ot_tcp_set_private_ptr(Socket s, void *ptr);
static void *ot_tcp_get_private_ptr(Socket s);
static void ot_tcp_set_frozen(Socket s, int is_frozen);
static const char *ot_tcp_socket_error(Socket s);
static void ot_recv(Actual_Socket s);
static void ot_listenaccept(Actual_Socket s);
void ot_poll(void);

Socket ot_register(void *sock, Plug plug)
{
    static struct socket_function_table fn_table = {
	ot_tcp_plug,
	ot_tcp_close,
	ot_tcp_write,
	ot_tcp_write_oob,
	ot_tcp_flush,
	ot_tcp_set_private_ptr,
	ot_tcp_get_private_ptr,
	ot_tcp_set_frozen,
	ot_tcp_socket_error
    };
    
    Actual_Socket ret;

    ret = snew(struct Socket_tag);
    ret->fn = &fn_table;
    ret->error = kOTNoError;
    ret->plug = plug;
    bufchain_init(&ret->output_data);
    ret->writable = 1;                 /* to start with */
    ret->sending_oob = 0;
    ret->frozen = 1;
    ret->frozen_readable = 0;
    ret->localhost_only = 0;           /* unused, but best init anyway */
    ret->pending_error = 0;
    ret->oobpending = FALSE;
    ret->listener = 0;

    ret->ep = (EndpointRef)sock;
  
    /* some sort of error checking */

    ret->oobinline = 0;
  
    /* Add this to the list of all sockets */
    ret->next = ot.socklist;
    ret->prev = &ot.socklist;
    ot.socklist = ret;

    return (Socket) ret;
}

Socket ot_new(SockAddr addr, int port, int privport, int oobinline,
	      int nodelay, int keepalive, Plug plug)
{
    static struct socket_function_table fn_table = {
	ot_tcp_plug,
	ot_tcp_close,
	ot_tcp_write,
	ot_tcp_write_oob,
	ot_tcp_flush,
	ot_tcp_set_private_ptr,
	ot_tcp_get_private_ptr,
	ot_tcp_set_frozen,
	ot_tcp_socket_error
    };

    Actual_Socket ret;
    EndpointRef ep;
    OSStatus err;
    InetAddress dest;
    TCall connectCall;

    ret = snew(struct Socket_tag);
    ret->fn = &fn_table;
    ret->error = kOTNoError;
    ret->plug = plug;
    bufchain_init(&ret->output_data);
    ret->connected = 0;                /* to start with */
    ret->writable = 0;                 /* to start with */
    ret->sending_oob = 0;
    ret->frozen = 0;
    ret->frozen_readable = 0;
    ret->localhost_only = 0;           /* unused, but best init anyway */
    ret->pending_error = 0;
    ret->oobinline = oobinline;
    ret->oobpending = FALSE;
    ret->listener = 0;

    /* Open Endpoint, configure it for TCP over anything */

    ep = OTOpenEndpoint(OTCreateConfiguration("tcp"), 0, NULL, &err);

    ret->ep = ep;

    if (err) {
	ret->error = err;
	return (Socket) ret;
    }

    /* TODO: oobinline, nodelay, keepalive */

    /*
     * Bind to local address.
     */
  
    /* FIXME: pay attention to privport */

    err = OTBind(ep, NULL, NULL); /* OpenTransport always picks our address */

    if (err) {
	ret->error = err;
	return (Socket) ret;
    }

    /*
     * Connect to remote address.
     */

    /* XXX Try non-primary addresses */
    OTInitInetAddress(&dest, port, addr->hostinfo.addrs[0]);

    memset(&connectCall, 0, sizeof(TCall));
    connectCall.addr.buf = (UInt8 *) &dest;
    connectCall.addr.len = sizeof(dest);

    err = OTConnect(ep, &connectCall, nil);
  
    if (err) {
	ret->error = err;
	return (Socket) ret;
    } else {
	ret->connected = 1;
	ret->writable = 1;
    }

    /* Add this to the list of all sockets */
    ret->next = ot.socklist;
    ret->prev = &ot.socklist;
    if (ret->next != NULL)
	ret->next->prev = &ret->next;
    ot.socklist = ret;

    /* XXX: don't know whether we can sk_addr_free(addr); */

    return (Socket) ret;
}
    
Socket ot_newlistener(char *srcaddr, int port, Plug plug, int local_host_only,
		      int address_family)
{
    static struct socket_function_table fn_table = {
	ot_tcp_plug,
	ot_tcp_close,
	ot_tcp_write,
	ot_tcp_write_oob,
	ot_tcp_flush,
	ot_tcp_set_private_ptr,
	ot_tcp_get_private_ptr,
	ot_tcp_set_frozen,
	ot_tcp_socket_error
    };

    Actual_Socket ret;
    EndpointRef ep;
    OSStatus err;
    InetAddress addr;
    TBind tbind;

    ret = snew(struct Socket_tag);
    ret->fn = &fn_table;
    ret->error = kOTNoError;
    ret->plug = plug;
    bufchain_init(&ret->output_data);
    ret->writable = 0;                 /* to start with */
    ret->sending_oob = 0;
    ret->frozen = 0;
    ret->frozen_readable = 0;
    ret->localhost_only = local_host_only;
    ret->pending_error = 0;
    ret->oobinline = 0;
    ret->oobpending = FALSE;
    ret->listener = 1;

    /* Open Endpoint, configure it for TCP over anything, and load the
     * tilisten module to serialize multiple simultaneous
     * connections. */

    ep = OTOpenEndpoint(OTCreateConfiguration("tilisten,tcp"), 0, NULL, &err);

    ret->ep = ep;

    if (err) {
	ret->error = err;
	return (Socket) ret;
    }

    /* TODO: set SO_REUSEADDR */

    OTInitInetAddress(&addr, port, kOTAnyInetAddress);
    /* XXX: pay attention to local_host_only */

    tbind.addr.buf = (UInt8 *) &addr;
    tbind.addr.len = sizeof(addr);
    tbind.qlen = 10;

    err = OTBind(ep, &tbind, NULL); /* XXX: check qlen we got */
    
    if (err) {
	ret->error = err;
	return (Socket) ret;
    }
	
    /* Add this to the list of all sockets */
    ret->next = ot.socklist;
    ret->prev = &ot.socklist;
    if (ret->next != NULL)
	ret->next->prev = &ret->next;
    ot.socklist = ret;

    return (Socket) ret;
}

static void ot_tcp_close(Socket sock)
{
    Actual_Socket s = (Actual_Socket) sock;
    
    OTCloseProvider(s->ep);

    /* Unhitch from list of sockets */
    *s->prev = s->next;
    if (s->next != NULL)
	s->next->prev = s->prev;

    sfree(s);
}

static void try_send(Actual_Socket s)
{
    while (bufchain_size(&s->output_data) > 0) {
	int nsent;
	void *data;
	int len;

	/* Don't care about oob right now */

	bufchain_prefix(&s->output_data, &data, &len);

	nsent = OTSnd(s->ep, data, len, 0);
	noise_ultralight(nsent);

	if (nsent <= 0) {
	    /* something bad happened, hey ho */
	} else {
	    /* still don't care about oob */
	    bufchain_consume(&s->output_data, nsent);
	}
    }
}

static int ot_tcp_write(Socket sock, char const *buf, int len)
{
    Actual_Socket s = (Actual_Socket) sock;

    bufchain_add(&s->output_data, buf, len);

    if (s->writable)
	try_send(s);
    return bufchain_size(&s->output_data);
}

static int ot_tcp_write_oob(Socket sock, char const *buf, int len)
{
    /* Don't care about oob */
    return 0;
}


/*
 * Each socket abstraction contains a `void *' private field in
 * which the client can keep state.
 */
static void ot_tcp_set_private_ptr(Socket sock, void *ptr)
{
    Actual_Socket s = (Actual_Socket) sock;
    s->private_ptr = ptr;
}

static void *ot_tcp_get_private_ptr(Socket sock)
{
    Actual_Socket s = (Actual_Socket) sock;
    return s->private_ptr;
}


/*
 * Special error values are returned from ot_namelookup and ot_new
 * if there's a problem. These functions extract an error message,
 * or return NULL if there's no problem.
 */
char *ot_addr_error(SockAddr addr)
{
    static char buf[128];

    if (addr->error == kOTNoError)
	return NULL;
    sprintf(buf, "error %d", addr->error);
    return buf;
}
static const char *ot_tcp_socket_error(Socket sock)
{
    Actual_Socket s = (Actual_Socket) sock;
    static char buf[128];

    if (s->error == kOTNoError)
	return NULL;
    sprintf(buf, "error %d", s->error);
    return buf;
}

static void ot_tcp_set_frozen(Socket sock, int is_frozen)
{
    Actual_Socket s = (Actual_Socket) sock;

    if (s->frozen == is_frozen)
	return;
    s->frozen = is_frozen;
}

/*
 * Poll all our sockets from an event loop
 */

void ot_poll(void)
{
    Actual_Socket s;
    OTResult o;

    for (s = ot.socklist; s != NULL; s = s->next) {
	o = OTLook(s->ep);

	switch(o) {
	  case T_DATA: /* Normal Data */
	    ot_recv(s);
	    break;
	  case T_EXDATA: /* Expedited Data (urgent?) */
	    ot_recv(s);
	    break;
	  case T_LISTEN: /* Connection attempt */
	    ot_listenaccept(s);
	    break;
	}
    }
}

void ot_recv(Actual_Socket s)
{
    OTResult o;
    char buf[2048];
    OTFlags flags;

    if (s->frozen) return;

    o = OTRcv(s->ep, buf, sizeof(buf), &flags);
    if (o > 0)
        plug_receive(s->plug, 0, buf, o);
    if (o < 0 && o != kOTNoDataErr)
        plug_closing(s->plug, NULL, 0, 0); /* XXX Error msg */
}

void ot_listenaccept(Actual_Socket s)
{
    OTResult o;
    OSStatus err;
    InetAddress remoteaddr;
    TCall tcall;
    EndpointRef ep;

    tcall.addr.maxlen = sizeof(InetAddress);
    tcall.addr.buf = (unsigned char *)&remoteaddr;
    tcall.opt.maxlen = 0;
    tcall.opt.buf = NULL;
    tcall.udata.maxlen = 0;
    tcall.udata.buf = NULL;

    o = OTListen(s->ep, &tcall);
    
    if (o != kOTNoError)
	return;

    /* We've found an incoming connection, accept it */

    ep = OTOpenEndpoint(OTCreateConfiguration("tcp"), 0, NULL, &err);
    o = OTAccept(s->ep, ep, &tcall);
    if (plug_accepting(s->plug, ep)) {
	OTUnbind(ep);
	OTCloseProvider(ep);
    }
}
    
/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */
