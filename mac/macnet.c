#include "putty.h"
#include "network.h"
#include "mac.h"

/*
 * Network functions exported to the world.  These choose whether to call
 * MacTCP or OpenTransport and behave accordingly.
 */
SockAddr sk_namelookup(char *host, char **canonicalname)
{

    if (mac_gestalts.otptattr != 0)
	return ot_namelookup(host, canonicalname);
    else if (mac_gestalts.mtcpvers != 0)
	return mactcp_namelookup(host, canonicalname);
    else
	return NULL;
}

SockAddr sk_nonamelookup(char *host)
{

    if (mac_gestalts.otptattr != 0)
	return ot_nonamelookup(host);
    else if (mac_gestalts.mtcpvers != 0)
	return mactcp_nonamelookup(host);
    else
	return NULL;
}

void sk_getaddr(SockAddr addr, char *buf, int buflen)
{

    if (mac_gestalts.otptattr != 0)
	ot_getaddr(addr, buf, buflen);
    else if (mac_gestalts.mtcpvers != 0)
	mactcp_getaddr(addr, buf, buflen);
    else
	*buf = '\0';
}

int sk_hostname_is_local(char *name)
{

    if (mac_gestalts.otptattr != 0)
	return ot_hostname_is_local(name);
    else if (mac_gestalts.mtcpvers != 0)
	return mactcp_hostname_is_local(name);
    else
	return 0;
}

int sk_address_is_local(SockAddr addr)
{

    if (mac_gestalts.otptattr != 0)
	return ot_address_is_local(addr);
    else if (mac_gestalts.mtcpvers != 0)
	return mactcp_address_is_local(addr);
    else
	return 0;
}

int sk_addrtype(SockAddr addr)
{

    if (mac_gestalts.otptattr != 0)
	return ot_addrtype(addr);
    else if (mac_gestalts.mtcpvers != 0)
	return mactcp_addrtype(addr);
    else
	return 0;
}

void sk_addrcopy(SockAddr addr, char *buf)
{

    if (mac_gestalts.otptattr != 0)
	ot_addrcopy(addr, buf);
    else if (mac_gestalts.mtcpvers != 0)
	mactcp_addrcopy(addr, buf);
}

void sk_addr_free(SockAddr addr)
{

    if (mac_gestalts.otptattr != 0)
	ot_addr_free(addr);
    else if (mac_gestalts.mtcpvers != 0)
	mactcp_addr_free(addr);
}

Socket sk_register(void *sock, Plug plug)
{

    if (mac_gestalts.otptattr != 0)
	return ot_register(sock, plug);
    else if (mac_gestalts.mtcpvers != 0)
	return mactcp_register(sock, plug);
    else
	return NULL;
}

Socket sk_new(SockAddr addr, int port, int privport, int oobinline,
	      int nodelay, Plug plug)
{

    if (mac_gestalts.otptattr != 0)
	return ot_new(addr, port, privport, oobinline, nodelay, plug);
    else if (mac_gestalts.mtcpvers != 0)
	return mactcp_new(addr, port, privport, oobinline, nodelay, plug);
    else
	return NULL;
}

Socket sk_newlistener(char *srcaddr, int port, Plug plug, int local_host_only)
{

    if (mac_gestalts.otptattr != 0)
	return ot_newlistener(srcaddr, port, plug, local_host_only);
    else if (mac_gestalts.mtcpvers != 0)
	return mactcp_newlistener(srcaddr, port, plug, local_host_only);
    else
	return NULL;
}

char *sk_addr_error(SockAddr addr)
{

    if (mac_gestalts.otptattr != 0)
	return ot_addr_error(addr);
    else if (mac_gestalts.mtcpvers != 0)
	return mactcp_addr_error(addr);
    else
	return "No TCP/IP stack installed";
}

void sk_cleanup(void)
{

    if (mac_gestalts.otptattr != 0)
	ot_cleanup();
    else if (mac_gestalts.mtcpvers != 0)
	mactcp_cleanup();
}

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */

