/*
 * Set and clear the O_NONBLOCK fcntl option on an open file.
 *
 * We don't realistically expect these operations to fail (the most
 * plausible error condition is EBADF, but we always believe ourselves
 * to be passing a valid fd so even that's an assertion-fail sort of
 * response), so we don't make any effort to return sensible error
 * codes to the caller - we just log to standard error and die
 * unceremoniously.
 *
 * Returns the previous state of O_NONBLOCK.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <fcntl.h>

#include "putty.h"

bool nonblock(int fd)
{
    int fdflags;

    fdflags = fcntl(fd, F_GETFL);
    if (fdflags < 0) {
        fprintf(stderr, "%d: fcntl(F_GETFL): %s\n", fd, strerror(errno));
        exit(1);
    }
    if (fcntl(fd, F_SETFL, fdflags | O_NONBLOCK) < 0) {
        fprintf(stderr, "%d: fcntl(F_SETFL): %s\n", fd, strerror(errno));
        exit(1);
    }

    return fdflags & O_NONBLOCK;
}

bool no_nonblock(int fd)
{
    int fdflags;

    fdflags = fcntl(fd, F_GETFL);
    if (fdflags < 0) {
        fprintf(stderr, "%d: fcntl(F_GETFL): %s\n", fd, strerror(errno));
        exit(1);
    }
    if (fcntl(fd, F_SETFL, fdflags & ~O_NONBLOCK) < 0) {
        fprintf(stderr, "%d: fcntl(F_SETFL): %s\n", fd, strerror(errno));
        exit(1);
    }

    return fdflags & O_NONBLOCK;
}
