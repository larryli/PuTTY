/*
 * Set and clear the FD_CLOEXEC fcntl option on a file descriptor.
 *
 * We don't realistically expect these operations to fail (the most
 * plausible error condition is EBADF, but we always believe ourselves
 * to be passing a valid fd so even that's an assertion-fail sort of
 * response), so we don't make any effort to return sensible error
 * codes to the caller - we just log to standard error and die
 * unceremoniously.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <fcntl.h>

#include "putty.h"

void cloexec(int fd)
{
    int fdflags;

    fdflags = fcntl(fd, F_GETFD);
    if (fdflags < 0) {
        fprintf(stderr, "%d: fcntl(F_GETFD): %s\n", fd, strerror(errno));
        exit(1);
    }
    if (fcntl(fd, F_SETFD, fdflags | FD_CLOEXEC) < 0) {
        fprintf(stderr, "%d: fcntl(F_SETFD): %s\n", fd, strerror(errno));
        exit(1);
    }
}

void noncloexec(int fd)
{
    int fdflags;

    fdflags = fcntl(fd, F_GETFD);
    if (fdflags < 0) {
        fprintf(stderr, "%d: fcntl(F_GETFD): %s\n", fd, strerror(errno));
        exit(1);
    }
    if (fcntl(fd, F_SETFD, fdflags & ~FD_CLOEXEC) < 0) {
        fprintf(stderr, "%d: fcntl(F_SETFD): %s\n", fd, strerror(errno));
        exit(1);
    }
}
