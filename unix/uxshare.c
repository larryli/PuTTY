/*
 * Unix implementation of SSH connection-sharing IPC setup.
 */

#include <stdio.h>
#include <assert.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/file.h>

#define DEFINE_PLUG_METHOD_MACROS
#include "tree234.h"
#include "putty.h"
#include "network.h"
#include "proxy.h"
#include "ssh.h"

#define CONNSHARE_SOCKETDIR_PREFIX "/tmp/putty-connshare"

/*
 * Functions provided by uxnet.c to help connection sharing.
 */
SockAddr unix_sock_addr(const char *path);
Socket new_unix_listener(SockAddr listenaddr, Plug plug);

static char *make_dirname(const char *name, char **parent_out)
{
    char *username, *dirname, *parent;

    username = get_username();
    parent = dupprintf("%s.%s", CONNSHARE_SOCKETDIR_PREFIX, username);
    sfree(username);
    assert(*parent == '/');

    dirname = dupprintf("%s/%s", parent, name);

    if (parent_out)
        *parent_out = parent;
    else
        sfree(parent);

    return dirname;
}

static char *make_dir_and_check_ours(const char *dirname)
{
    struct stat st;

    /*
     * Create the directory. We might have created it before, so
     * EEXIST is an OK error; but anything else is doom.
     */
    if (mkdir(dirname, 0700) < 0 && errno != EEXIST)
        return dupprintf("%s: mkdir: %s", dirname, strerror(errno));

    /*
     * Now check that that directory is _owned by us_ and not writable
     * by anybody else. This protects us against somebody else
     * previously having created the directory in a way that's
     * writable to us, and thus manipulating us into creating the
     * actual socket in a directory they can see so that they can
     * connect to it and use our authenticated SSH sessions.
     */
    if (stat(dirname, &st) < 0)
        return dupprintf("%s: stat: %s", dirname, strerror(errno));
    if (st.st_uid != getuid())
        return dupprintf("%s: directory owned by uid %d, not by us",
                         dirname, st.st_uid);
    if ((st.st_mode & 077) != 0)
        return dupprintf("%s: directory has overgenerous permissions %03o"
                         " (expected 700)", dirname, st.st_mode & 0777);

    return NULL;
}

int platform_ssh_share(const char *pi_name, Conf *conf,
                       Plug downplug, Plug upplug, Socket *sock,
                       char **logtext, char **ds_err, char **us_err,
                       int can_upstream, int can_downstream)
{
    char *name, *parentdirname, *dirname, *lockname, *sockname, *err;
    int lockfd;
    Socket retsock;

    /*
     * Transform the platform-independent version of the connection
     * identifier into something valid for a Unix socket, by escaping
     * slashes (and, while we're here, any control characters).
     */
    {
        const char *p;
        char *q;

        name = snewn(1+3*strlen(pi_name), char);

        for (p = pi_name, q = name; *p; p++) {
            if (*p == '/' || *p == '%' ||
                (unsigned char)*p < 0x20 || *p == 0x7f) {
                q += sprintf(q, "%%%02x", (unsigned char)*p);
            } else {
                *q++ = *p;
            }
        }
        *q = '\0';
    }

    /*
     * First, make sure our subdirectory exists. We must create two
     * levels of directory - the one for this particular connection,
     * and the containing one for our username.
     */
    dirname = make_dirname(name, &parentdirname);
    if ((err = make_dir_and_check_ours(parentdirname)) != NULL) {
        *logtext = err;
        sfree(dirname);
        sfree(parentdirname);
        sfree(name);
        return SHARE_NONE;
    }
    sfree(parentdirname);
    if ((err = make_dir_and_check_ours(dirname)) != NULL) {
        *logtext = err;
        sfree(dirname);
        sfree(name);
        return SHARE_NONE;
    }

    /*
     * Acquire a lock on a file in that directory.
     */
    lockname = dupcat(dirname, "/lock", (char *)NULL);
    lockfd = open(lockname, O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (lockfd < 0) {
        *logtext = dupprintf("%s: open: %s", lockname, strerror(errno));
        sfree(dirname);
        sfree(lockname);
        sfree(name);
        return SHARE_NONE;
    }
    if (flock(lockfd, LOCK_EX) < 0) {
        *logtext = dupprintf("%s: flock(LOCK_EX): %s",
                             lockname, strerror(errno));
        sfree(dirname);
        sfree(lockname);
        close(lockfd);
        sfree(name);
        return SHARE_NONE;
    }

    sockname = dupprintf("%s/socket", dirname);

    *logtext = NULL;

    if (can_downstream) {
        retsock = new_connection(unix_sock_addr(sockname),
                                 "", 0, 0, 1, 0, 0, downplug, conf);
        if (sk_socket_error(retsock) == NULL) {
            sfree(*logtext);
            *logtext = sockname;
            *sock = retsock;
            sfree(dirname);
            sfree(lockname);
            close(lockfd);
            sfree(name);
            return SHARE_DOWNSTREAM;
        }
        sfree(*ds_err);
        *ds_err = dupprintf("%s: %s", sockname, sk_socket_error(retsock));
        sk_close(retsock);
    }

    if (can_upstream) {
        retsock = new_unix_listener(unix_sock_addr(sockname), upplug);
        if (sk_socket_error(retsock) == NULL) {
            sfree(*logtext);
            *logtext = sockname;
            *sock = retsock;
            sfree(dirname);
            sfree(lockname);
            close(lockfd);
            sfree(name);
            return SHARE_UPSTREAM;
        }
        sfree(*us_err);
        *us_err = dupprintf("%s: %s", sockname, sk_socket_error(retsock));
        sk_close(retsock);
    }

    /* One of the above clauses ought to have happened. */
    assert(*logtext || *ds_err || *us_err);

    sfree(dirname);
    sfree(lockname);
    sfree(sockname);
    close(lockfd);
    sfree(name);
    return SHARE_NONE;
}

void platform_ssh_share_cleanup(const char *name)
{
    char *dirname, *filename;

    dirname = make_dirname(name, NULL);

    filename = dupcat(dirname, "/socket", (char *)NULL);
    remove(filename);
    sfree(filename);

    filename = dupcat(dirname, "/lock", (char *)NULL);
    remove(filename);
    sfree(filename);

    rmdir(dirname);

    /*
     * We deliberately _don't_ clean up the parent directory
     * /tmp/putty-connshare.<username>, because if we leave it around
     * then it reduces the ability for other users to be a nuisance by
     * putting their own directory in the way of it.
     */

    sfree(dirname);
}
