/*
 * Macintosh OpenTransport networking abstraction
 */

#include <OpenTransport.h>
#include <OpenTptInternet.h>

#define DEFINE_PLUG_METHOD_MACROS
#include "putty.h"
#include "network.h"
#include "mac.h"

struct Socket_tag {
    struct socket_function_table *fn;
    /* other stuff... */
    char *error;
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
    char *error;
    DNSAddress address;
};

/* Globals */

static struct {
    Actual_Socket socklist;
} ot;

OSErr ot_init(void)
{
    return InitOpenTransport();
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

static char *error_string(int error)
{
    return "An error...";
}

SockAddr ot_namelookup(char *host, char **canonicalname)
{
    SockAddr ret = smalloc(sizeof(struct SockAddr_tag));
    
    OTInitDNSAddress(&(ret->address), host);
  
    /* for now we'll pretend canonicalname is always just host */

    *canonicalname = smalloc(1+strlen(host));
    strcpy(*canonicalname, host);
    return ret;
}

SockAddr ot_nonamelookup(char *host)
{
    SockAddr ret = smalloc(sizeof(struct SockAddr_tag));
    
    OTInitDNSAddress(&(ret->address), host);
  
    return ret;
}

void ot_getaddr(SockAddr addr, char *buf, int buflen)
{
    strncpy(buf, (addr->address).fName, buflen);
}

/* I think "local" here really means "loopback" */

int ot_hostname_is_local(char *name)
{

    return !strcmp(name, "localhost");
}

int ot_address_is_local(SockAddr addr)
{

    /* FIXME */
    return FALSE;
}

int ot_addrtype(SockAddr addr)
{
    return ADDRTYPE_IPV4;
}

void ot_addrcopy(SockAddr addr, char *buf)
{
  
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
static char *ot_tcp_socket_error(Socket s);
static void ot_recv(Actual_Socket s);
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

    ret = smalloc(sizeof(struct Socket_tag));
    ret->fn = &fn_table;
    ret->error = NULL;
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
	      int nodelay, Plug plug)
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
    TCall connectCall;

    ret = smalloc(sizeof(struct Socket_tag));
    ret->fn = &fn_table;
    ret->error = NULL;
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
	ret->error = error_string(err);
	return (Socket) ret;
    }

    /* TODO: oobinline, nodelay */

    /*
     * Bind to local address.
     */
  
    /* FIXME: pay attention to privport */

    err = OTBind(ep, NULL, NULL); /* OpenTransport always picks our address */

    if (err) {
	ret->error = error_string(err);
	return (Socket) ret;
    }

    /*
     * Connect to remote address.
     */

    /* FIXME: bolt the port onto the end */

    OTMemzero(&connectCall, sizeof(TCall));
    connectCall.addr.buf = (UInt8 *) &(addr->address);
    connectCall.addr.len = sizeof(DNSAddress);

    err = OTConnect(ep, &connectCall, nil);
  
    if (err) {
	ret->error = error_string(err);
	return (Socket) ret;
    } else {
	ret->connected = 1;
	ret->writable = 1;
    }

    /* Add this to the list of all sockets */
    ret->next = ot.socklist;
    ret->prev = &ot.socklist;
    ot.socklist = ret;

    return (Socket) ret;
}
    
Socket ot_newlistener(char *foobar, int port, Plug plug, int local_host_only)
{
    Actual_Socket s;

    return (Socket) s;
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
    return addr->error;
}
static char *ot_tcp_socket_error(Socket sock)
{
    Actual_Socket s = (Actual_Socket) sock;
    return s->error;
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
	}
    }
}

void ot_recv(Actual_Socket s)
{
    OTResult o;
    char buf[20480];
    OTFlags flags;

    if (s->frozen) return;

    while ((o = OTRcv(s->ep, buf, sizeof(buf), &flags)) != kOTNoDataErr) {
	plug_receive(s->plug, 0, buf, sizeof(buf));
    }
}


/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */
