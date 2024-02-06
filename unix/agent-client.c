/*
 * SSH agent client code.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>

#include "putty.h"
#include "misc.h"
#include "tree234.h"
#include "puttymem.h"

bool agent_exists(void)
{
    const char *p = getenv("SSH_AUTH_SOCK");
    if (p && *p)
        return true;
    return false;
}

static tree234 *agent_pending_queries;
struct agent_pending_query {
    int fd;
    char *retbuf;
    char sizebuf[4];
    int retsize, retlen;
    void (*callback)(void *, void *, int);
    void *callback_ctx;
};
static int agent_conncmp(void *av, void *bv)
{
    agent_pending_query *a = (agent_pending_query *) av;
    agent_pending_query *b = (agent_pending_query *) bv;
    if (a->fd < b->fd)
        return -1;
    if (a->fd > b->fd)
        return +1;
    return 0;
}
static int agent_connfind(void *av, void *bv)
{
    int afd = *(int *) av;
    agent_pending_query *b = (agent_pending_query *) bv;
    if (afd < b->fd)
        return -1;
    if (afd > b->fd)
        return +1;
    return 0;
}

/*
 * Attempt to read from an agent socket fd. Returns false if the
 * expected response is as yet incomplete; returns true if it's either
 * complete (conn->retbuf non-NULL and filled with something useful)
 * or has failed totally (conn->retbuf is NULL).
 */
static bool agent_try_read(agent_pending_query *conn)
{
    int ret;

    ret = read(conn->fd, conn->retbuf+conn->retlen,
               conn->retsize-conn->retlen);
    if (ret <= 0) {
        if (conn->retbuf != conn->sizebuf) sfree(conn->retbuf);
        conn->retbuf = NULL;
        conn->retlen = 0;
        return true;
    }
    conn->retlen += ret;
    if (conn->retsize == 4 && conn->retlen == 4) {
        conn->retsize = toint(GET_32BIT_MSB_FIRST(conn->retbuf) + 4);
        if (conn->retsize <= 0) {
            conn->retbuf = NULL;
            conn->retlen = 0;
            return true;                 /* way too large */
        }
        assert(conn->retbuf == conn->sizebuf);
        conn->retbuf = snewn(conn->retsize, char);
        memcpy(conn->retbuf, conn->sizebuf, 4);
    }

    if (conn->retlen < conn->retsize)
        return false;                  /* more data to come */

    return true;
}

void agent_cancel_query(agent_pending_query *conn)
{
    uxsel_del(conn->fd);
    close(conn->fd);
    del234(agent_pending_queries, conn);
    if (conn->retbuf && conn->retbuf != conn->sizebuf)
        sfree(conn->retbuf);
    sfree(conn);
}

static void agent_select_result(int fd, int event)
{
    agent_pending_query *conn;

    assert(event == SELECT_R);  /* not selecting for anything but R */

    conn = find234(agent_pending_queries, &fd, agent_connfind);
    if (!conn) {
        uxsel_del(fd);
        return;
    }

    if (!agent_try_read(conn))
        return;                /* more data to come */

    /*
     * We have now completed the agent query. Do the callback.
     */
    conn->callback(conn->callback_ctx, conn->retbuf, conn->retlen);
    /* Null out conn->retbuf, since ownership of that buffer has
     * passed to the callback. */
    conn->retbuf = NULL;
    agent_cancel_query(conn);
}

static const char *agent_socket_path(void)
{
    return getenv("SSH_AUTH_SOCK");
}

Socket *agent_connect(Plug *plug)
{
    const char *path = agent_socket_path();
    if (!path)
        return new_error_socket_fmt(plug, "SSH_AUTH_SOCK not set");
    return sk_new(unix_sock_addr(path), 0, false, false, false, false, plug);
}

agent_pending_query *agent_query(
    strbuf *query, void **out, int *outlen,
    void (*callback)(void *, void *, int), void *callback_ctx)
{
    const char *name;
    int sock;
    struct sockaddr_un addr;
    int done;
    agent_pending_query *conn;

    name = agent_socket_path();
    if (!name || strlen(name) >= sizeof(addr.sun_path))
        goto failure;

    sock = socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket(PF_UNIX)");
        exit(1);
    }

    cloexec(sock);

    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, name);
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        goto failure;
    }

    strbuf_finalise_agent_query(query);

    for (done = 0; done < query->len ;) {
        int ret = write(sock, query->s + done,
                        query->len - done);
        if (ret <= 0) {
            close(sock);
            goto failure;
        }
        done += ret;
    }

    conn = snew(agent_pending_query);
    conn->fd = sock;
    conn->retbuf = conn->sizebuf;
    conn->retsize = 4;
    conn->retlen = 0;
    conn->callback = callback;
    conn->callback_ctx = callback_ctx;

    if (!callback) {
        /*
         * Bodge to permit making deliberately synchronous agent
         * requests. Used by Unix Pageant in command-line client mode,
         * which is legit because it really is true that no other part
         * of the program is trying to get anything useful done
         * simultaneously. But this special case shouldn't be used in
         * any more general program.
         */
        no_nonblock(conn->fd);
        while (!agent_try_read(conn))
            /* empty loop body */;

        *out = conn->retbuf;
        *outlen = conn->retlen;
        sfree(conn);
        return NULL;
    }

    /*
     * Otherwise do it properly: add conn to the tree of agent
     * connections currently in flight, return 0 to indicate that the
     * response hasn't been received yet, and call the callback when
     * select_result comes back to us.
     */
    if (!agent_pending_queries)
        agent_pending_queries = newtree234(agent_conncmp);
    add234(agent_pending_queries, conn);

    uxsel_set(sock, SELECT_R, agent_select_result);
    return conn;

  failure:
    *out = NULL;
    *outlen = 0;
    return NULL;
}
