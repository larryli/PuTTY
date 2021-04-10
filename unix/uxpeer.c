/*
 * Unix: wrapper for getsockopt(SO_PEERCRED), conditionalised on
 * appropriate autoconfery.
 */

#if HAVE_CMAKE_H
#include "cmake.h"
#endif

#if HAVE_SO_PEERCRED
#define _GNU_SOURCE
#include <features.h>
#endif

#include <sys/socket.h>

#include "putty.h"

bool so_peercred(int fd, int *pid, int *uid, int *gid)
{
#if HAVE_SO_PEERCRED
    struct ucred cr;
    socklen_t crlen = sizeof(cr);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cr, &crlen) == 0) {
        *pid = cr.pid;
        *uid = cr.uid;
        *gid = cr.gid;
        return true;
    }
#endif
    return false;
}
