#include "putty.h"
#include "network.h"
#include "mac.h"
#include "ssh.h"

struct macnet_stack {
    SockAddr (*namelookup)(char const *, char **);
    SockAddr (*nonamelookup)(char const *);
    void (*getaddr)(SockAddr, char *, int);
    int (*hostname_is_local)(char *);
    int (*address_is_local)(SockAddr);
    int (*addrtype)(SockAddr);
    void (*addrcopy)(SockAddr, char *);
    void (*addr_free)(SockAddr);
    Socket (*skregister)(void *, Plug); /* "register" is a reserved word */
    Socket (*new)(SockAddr, int, int, int, int, int, Plug);
    Socket (*newlistener)(char *, int, Plug, int, int);
    char *(*addr_error)(SockAddr);
    void (*poll)(void);
    void (*cleanup)(void);
};

static struct macnet_stack *stack;

static struct macnet_stack ot = {
    ot_namelookup, ot_nonamelookup, ot_getaddr, ot_hostname_is_local,
    ot_address_is_local, ot_addrtype, ot_addrcopy, ot_addr_free,
    ot_register, ot_new, ot_newlistener, ot_addr_error, ot_poll, ot_cleanup
};

#if !TARGET_API_MAC_CARBON
static struct macnet_stack mactcp = {
    mactcp_namelookup, mactcp_nonamelookup, mactcp_getaddr,
    mactcp_hostname_is_local, mactcp_address_is_local, mactcp_addrtype,
    mactcp_addrcopy, mactcp_addr_free, mactcp_register, mactcp_new,
    mactcp_newlistener, mactcp_addr_error, mactcp_poll, mactcp_cleanup
};
#endif

void sk_init(void)
{

#ifndef NO_OT
    if (ot_init() == noErr)
	stack = &ot;
    else
#endif
#if !TARGET_API_MAC_CARBON
    if (mactcp_init() == noErr)
	stack = &mactcp;
    else
#endif
	stack = NULL;
}

/*
 * Network functions exported to the world.  These choose whether to call
 * MacTCP or OpenTransport and behave accordingly.
 */
SockAddr sk_namelookup(char const *host, char **canonicalname, int address_family)
{

    if (stack != NULL)
	return stack->namelookup(host, canonicalname);
    return NULL;
}

SockAddr sk_nonamelookup(char const *host)
{

    if (stack != NULL)
	return stack->nonamelookup(host);
    return NULL;
}

void sk_getaddr(SockAddr addr, char *buf, int buflen)
{

    if (stack != NULL)
	stack->getaddr(addr, buf, buflen);
    else
	*buf = '\0';
}

int sk_hostname_is_local(char *name)
{

    if (stack != NULL)
	return stack->hostname_is_local(name);
    return 0;
}

int sk_address_is_local(SockAddr addr)
{

    if (stack != NULL)
	return stack->address_is_local(addr);
    return 0;
}

int sk_addrtype(SockAddr addr)
{

    if (stack != NULL)
	return stack->addrtype(addr);
    return 0;
}

void sk_addrcopy(SockAddr addr, char *buf)
{

    if (stack != NULL)
	stack->addrcopy(addr, buf);
}

void sk_addr_free(SockAddr addr)
{

    if (stack != NULL)
	stack->addr_free(addr);
}

Socket sk_register(void *sock, Plug plug)
{

    if (stack != NULL)
	return stack->skregister(sock, plug);
    return NULL;
}

Socket sk_new(SockAddr addr, int port, int privport, int oobinline,
	      int nodelay, int keepalive, Plug plug)
{

    if (stack != NULL)
	return stack->new(addr, port, privport, oobinline, nodelay, keepalive,
			  plug);
    return NULL;
}

Socket sk_newlistener(char *srcaddr, int port, Plug plug, int local_host_only, int address_family)
{

    if (stack != NULL)
	return stack->newlistener(srcaddr, port, plug, local_host_only, address_family);
    return NULL;
}

const char *sk_addr_error(SockAddr addr)
{

    if (stack != NULL)
	return stack->addr_error(addr);
    return "No TCP/IP stack installed";
}

void sk_poll(void)
{

    if (stack != NULL)
	stack->poll();
}

void sk_cleanup(void)
{

    if (stack != NULL)
	stack->cleanup();
}

/* We should use Internet Config here. */
int net_service_lookup(char *service)
{

    return 0;
}

SockAddr platform_get_x11_unix_address(int displaynum, char **canonicalname)
{
    return NULL;
}

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */

