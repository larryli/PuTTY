/*
 * Locally authenticate a TCP socket via /proc/net.
 *
 * Obviously, if a TCP connection comes from a different host, there's
 * no way to find out the identity of the thing at the other end (or
 * even really to assign that concept a meaning) except by the usual
 * method of speaking a protocol over the socket itself which involves
 * some form of (preferably cryptographic) authentication exchange.
 *
 * But if the connection comes from localhost, then on at least some
 * operating systems, you can do better. On Linux, /proc/net/tcp and
 * /proc/net/tcp6 list the full set of active TCP connection
 * endpoints, and they list an owning uid for each one. So once you've
 * accepted a connection to a listening socket and found that the
 * other end of it is a localhost address, you can look up the _other_
 * endpoint in the right one of those files, and find out which uid
 * owns it.
 */

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "misc.h"

static ptrlen get_space_separated_field(ptrlen *string)
{
    const char *p = string->ptr, *end = p + string->len;

    while (p < end && isspace((unsigned char)*p))
        p++;
    if (p == end)
        return PTRLEN_LITERAL("");

    const char *start = p;
    while (p < end && !isspace((unsigned char)*p))
        p++;
    *string = make_ptrlen(p, end - p);
    return make_ptrlen(start, p - start);
}

enum { GOT_LOCAL_UID = 1, GOT_REMOTE_UID = 2 };

/*
 * Open a file formatted like /proc/net/tcp{,6}, and search it for
 * both ends of a particular connection.
 *
 * The operands 'local' and 'remote' give the expected string
 * representations of the local and remote addresses of the connection
 * we're looking for.
 *
 * Return value is the bitwise OR of 1 if we found the local end of
 * the connection and 2 if we found the remote. Each output uid_t
 * parameter is filled in iff the corresponding bit is set in the
 * return value.
 */
static int lookup_uids_in_procnet_file(
    const char *path, ptrlen local, ptrlen remote,
    uid_t *local_uid, uid_t *remote_uid)
{
    FILE *fp = NULL;
    int toret = 0;
    ptrlen line, field;

    enum { GF_LOCAL = 1, GF_REMOTE = 2, GF_UID = 4 };

    fp = fopen(path, "r");
    if (!fp)
        goto out;

    /* Expected indices of fields in /proc/net/tcp* */
    const int LOCAL_ADDR_INDEX = 1;
    const int REMOTE_ADDR_INDEX = 2;
    const int UID_INDEX = 7;

    for (char *linez; (linez = chomp(fgetline(fp))) != NULL ;) {
        line = ptrlen_from_asciz(linez);
        int gotfields = 0;
        ptrlen local_addr = PTRLEN_LITERAL("");
        ptrlen remote_addr = PTRLEN_LITERAL("");
        long uid = -1;

        for (int i = 0; (field = get_space_separated_field(&line)).len != 0;
             i++) {

            if (i == LOCAL_ADDR_INDEX) {
                gotfields |= GF_LOCAL;
                local_addr = field;
            } else if (i == REMOTE_ADDR_INDEX) {
                gotfields |= GF_REMOTE;
                remote_addr = field;
            } else if (i == UID_INDEX) {
                uid = 0;
                for (const char *p = field.ptr, *end = p + field.len;
                     p < end; p++) {
                    if (!isdigit((unsigned char)*p)) {
                        uid = -1;
                        break;
                    }
                    int dval = *p - '0';
                    if (uid > LONG_MAX/10) {
                        uid = -1;
                        break;
                    }
                    uid *= 10;
                    if (uid > LONG_MAX - dval) {
                        uid = -1;
                        break;
                    }
                    uid += dval;
                }

                gotfields |= GF_UID;
            }
        }

        if (gotfields == (GF_LOCAL | GF_REMOTE | GF_UID)) {
            if (ptrlen_eq_ptrlen(local_addr, local) &&
                ptrlen_eq_ptrlen(remote_addr, remote)) {
                *local_uid = uid;
                toret |= GOT_LOCAL_UID;
            }
            if (ptrlen_eq_ptrlen(local_addr, remote) &&
                ptrlen_eq_ptrlen(remote_addr, local)) {
                *remote_uid = uid;
                toret |= GOT_REMOTE_UID;
            }
        }

        sfree(linez);
    }

    fclose(fp);
    fp = NULL;

  out:
    if (fp)
        fclose(fp);
    return toret;
}

static const char *procnet_path(int family)
{
    switch (family) {
      case AF_INET: return "/proc/net/tcp";
      case AF_INET6: return "/proc/net/tcp6";
      default: return NULL;
    }
}

static char *format_sockaddr(const void *addr, int family)
{
    if (family == AF_INET) {
        const struct sockaddr_in *a = (const struct sockaddr_in *)addr;
        assert(a->sin_family == family);
        /* Linux /proc/net formats the IP address native-endian, so we
         * don't use ntohl */
        return dupprintf("%08X:%04X", a->sin_addr.s_addr, ntohs(a->sin_port));
    } else if (family == AF_INET6) {
        struct sockaddr_in6 *a = (struct sockaddr_in6 *)addr;
        assert(a->sin6_family == family);

        strbuf *sb = strbuf_new();

        const uint32_t *addrwords = (const uint32_t *)a->sin6_addr.s6_addr;
        for (int i = 0; i < 4; i++)
            put_fmt(sb, "%08X", addrwords[i]);
        put_fmt(sb, ":%04X", ntohs(a->sin6_port));

        return strbuf_to_str(sb);
    } else {
        return NULL;
    }
}

bool socket_peer_is_same_user(int fd)
{
    struct sockaddr_storage addr;
    socklen_t addrlen;
    int family;
    bool toret = false;
    char *local = NULL, *remote = NULL;
    const char *path;

    addrlen = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &addrlen) != 0)
        goto out;
    family = addr.ss_family;
    if ((path = procnet_path(family)) == NULL)
        goto out;
    local = format_sockaddr(&addr, family);
    if (!local)
        goto out;

    addrlen = sizeof(addr);
    if (getpeername(fd, (struct sockaddr *)&addr, &addrlen) != 0)
        goto out;
    if (addr.ss_family != family)
        goto out;
    remote = format_sockaddr(&addr, family);
    if (!remote)
        goto out;

    ptrlen locpl = ptrlen_from_asciz(local);
    ptrlen rempl = ptrlen_from_asciz(remote);

    /*
     * Check that _both_ end of the socket are the uid we expect, as a
     * sanity check on the /proc/net file being reasonable at all.
     */
    uid_t our_uid = getuid();
    uid_t local_uid = -1, remote_uid = -1;
    int got = lookup_uids_in_procnet_file(
        path, locpl, rempl, &local_uid, &remote_uid);
    if (got == (GOT_LOCAL_UID | GOT_REMOTE_UID) &&
        local_uid == our_uid && remote_uid == our_uid)
        toret = true;

  out:
    sfree(local);
    sfree(remote);
    return toret;
}
