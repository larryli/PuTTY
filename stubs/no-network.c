/*
 * Stub version of the whole networking abstraction.
 */

#include "putty.h"
#include "network.h"

struct SockAddr {
    int dummy;
};

void sk_init(void)
{
}

void sk_cleanup(void)
{
}

SockAddr *sk_namelookup(const char *host, char **canonicalname,
                        int address_family)
{
    return snew(SockAddr);
}
SockAddr *sk_nonamelookup(const char *host)
{
    return snew(SockAddr);
}

void sk_getaddr(SockAddr *addr, char *buf, int buflen)
{
    strncpy(buf, "nonsense", buflen);
}

bool sk_addr_needs_port(SockAddr *addr)
{
    return true;
}

bool sk_hostname_is_local(const char *name)
{
    return false;
}

bool sk_address_is_local(SockAddr *addr)
{
    return false;
}

bool sk_address_is_special_local(SockAddr *addr)
{
    return false;
}

int sk_addrtype(SockAddr *addr)
{
    return ADDRTYPE_UNSPEC;
}

void sk_addrcopy(SockAddr *addr, char *buf)
{
}

void sk_addr_free(SockAddr *addr)
{
    sfree(addr);
}

SockAddr *sk_addr_dup(SockAddr *addr)
{
    return snew(SockAddr);
}

Socket *sk_new(SockAddr *addr, int port, bool privport, bool oobinline,
               bool nodelay, bool keepalive, Plug *plug)
{
    return new_error_socket_fmt(
        plug, "no actual networking in this application");
}

Socket *sk_newlistener(const char *srcaddr, int port, Plug *plug,
                       bool local_host_only, int orig_address_family)
{
    return new_error_socket_fmt(
        plug, "no actual networking in this application");
}

void *(sk_getxdmdata)(Socket *sock, int *lenp)
{
    return NULL;
}

void plug_closing_errno(Plug *plug, int error)
{
    plug_closing(plug, PLUGCLOSE_ERROR, "dummy");
}

const char *sk_addr_error(SockAddr *addr)
{
    return "no actual network addresses in this application";
}

int net_service_lookup(const char *service)
{
    return 0;
}

char *get_hostname(void)
{
    return dupstr("dummy-hostname");
}

SockAddr *platform_get_x11_unix_address(const char *sockpath, int displaynum)
{
    return snew(SockAddr);
}

SockAddr *unix_sock_addr(const char *path)
{
    return snew(SockAddr);
}

SockAddr *sk_namedpipe_addr(const char *pipename)
{
    return snew(SockAddr);
}

Socket *new_unix_listener(SockAddr *listenaddr, Plug *plug)
{
    return new_error_socket_fmt(
        plug, "no actual networking in this application");
}

Socket *platform_start_subprocess(const char *cmd, Plug *plug,
                                  const char *prefix)
{
    return new_error_socket_fmt(
        plug, "no actual networking in this application");
}

#ifdef PUTTY_WINDOWS_PLATFORM_H
void plug_closing_system_error(Plug *plug, DWORD error) {}
void plug_closing_winsock_error(Plug *plug, DWORD error) {}
#endif
