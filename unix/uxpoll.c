/* On some systems this is needed to get poll.h to define eg.. POLLRDNORM */
#define _XOPEN_SOURCE

#include <poll.h>

#include "putty.h"
#include "tree234.h"

struct pollwrapper {
    struct pollfd *fds;
    size_t nfd, fdsize;
    tree234 *fdtopos;
};

typedef struct pollwrap_fdtopos pollwrap_fdtopos;
struct pollwrap_fdtopos {
    int fd;
    size_t pos;
};

static int pollwrap_fd_cmp(void *av, void *bv)
{
    pollwrap_fdtopos *a = (pollwrap_fdtopos *)av;
    pollwrap_fdtopos *b = (pollwrap_fdtopos *)bv;
    return a->fd < b->fd ? -1 : a->fd > b->fd ? +1 : 0;
}

pollwrapper *pollwrap_new(void)
{
    pollwrapper *pw = snew(pollwrapper);
    pw->fdsize = 16;
    pw->nfd = 0;
    pw->fds = snewn(pw->fdsize, struct pollfd);
    pw->fdtopos = newtree234(pollwrap_fd_cmp);
    return pw;
}

void pollwrap_free(pollwrapper *pw)
{
    pollwrap_clear(pw);
    freetree234(pw->fdtopos);
    sfree(pw->fds);
    sfree(pw);
}

void pollwrap_clear(pollwrapper *pw)
{
    pw->nfd = 0;
    for (pollwrap_fdtopos *f2p;
         (f2p = delpos234(pw->fdtopos, 0)) != NULL ;)
        sfree(f2p);
}

void pollwrap_add_fd_events(pollwrapper *pw, int fd, int events)
{
    pollwrap_fdtopos *f2p, f2p_find;

    assert(fd >= 0);

    f2p_find.fd = fd;
    f2p = find234(pw->fdtopos, &f2p_find, NULL);
    if (!f2p) {
        sgrowarray(pw->fds, pw->fdsize, pw->nfd);
        size_t index = pw->nfd++;
        pw->fds[index].fd = fd;
        pw->fds[index].events = pw->fds[index].revents = 0;

        f2p = snew(pollwrap_fdtopos);
        f2p->fd = fd;
        f2p->pos = index;
        pollwrap_fdtopos *added = add234(pw->fdtopos, f2p);
        assert(added == f2p);
    }

    pw->fds[f2p->pos].events |= events;
}

/* Omit any of the POLL{RD,WR}{NORM,BAND} flag values that are still
 * not defined by poll.h, just in case */
#ifndef POLLRDNORM
#define POLLRDNORM 0
#endif
#ifndef POLLRDBAND
#define POLLRDBAND 0
#endif
#ifndef POLLWRNORM
#define POLLWRNORM 0
#endif
#ifndef POLLWRBAND
#define POLLWRBAND 0
#endif

#define SELECT_R_IN (POLLIN  | POLLRDNORM | POLLRDBAND)
#define SELECT_W_IN (POLLOUT | POLLWRNORM | POLLWRBAND)
#define SELECT_X_IN (POLLPRI)

#define SELECT_R_OUT (SELECT_R_IN | POLLERR | POLLHUP)
#define SELECT_W_OUT (SELECT_W_IN | POLLERR)
#define SELECT_X_OUT (SELECT_X_IN)

void pollwrap_add_fd_rwx(pollwrapper *pw, int fd, int rwx)
{
    int events = 0;
    if (rwx & SELECT_R)
        events |= SELECT_R_IN;
    if (rwx & SELECT_W)
        events |= SELECT_W_IN;
    if (rwx & SELECT_X)
        events |= SELECT_X_IN;
    pollwrap_add_fd_events(pw, fd, events);
}

int pollwrap_poll_instant(pollwrapper *pw)
{
    return poll(pw->fds, pw->nfd, 0);
}

int pollwrap_poll_endless(pollwrapper *pw)
{
    return poll(pw->fds, pw->nfd, -1);
}

int pollwrap_poll_timeout(pollwrapper *pw, int milliseconds)
{
    assert(milliseconds >= 0);
    return poll(pw->fds, pw->nfd, milliseconds);
}

static void pollwrap_get_fd_events_revents(pollwrapper *pw, int fd,
                                           int *events_p, int *revents_p)
{
    pollwrap_fdtopos *f2p, f2p_find;
    int events = 0, revents = 0;

    assert(fd >= 0);

    f2p_find.fd = fd;
    f2p = find234(pw->fdtopos, &f2p_find, NULL);
    if (f2p) {
        events = pw->fds[f2p->pos].events;
        revents = pw->fds[f2p->pos].revents;
    }

    if (events_p)
        *events_p = events;
    if (revents_p)
        *revents_p = revents;
}

int pollwrap_get_fd_events(pollwrapper *pw, int fd)
{
    int revents;
    pollwrap_get_fd_events_revents(pw, fd, NULL, &revents);
    return revents;
}

int pollwrap_get_fd_rwx(pollwrapper *pw, int fd)
{
    int events, revents;
    pollwrap_get_fd_events_revents(pw, fd, &events, &revents);
    int rwx = 0;
    if ((events & POLLIN) && (revents & SELECT_R_OUT))
        rwx |= SELECT_R;
    if ((events & POLLOUT) && (revents & SELECT_W_OUT))
        rwx |= SELECT_W;
    if ((events & POLLPRI) && (revents & SELECT_X_OUT))
        rwx |= SELECT_X;
    return rwx;
}
