#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <ctype.h>

#include <unistd.h>

#include "putty.h"
#include "ssh.h"
#include "misc.h"
#include "pageant.h"

Socket *platform_make_agent_socket(
    Plug *plug, const char *dirprefix, char **error, char **name)
{
    char *username, *socketdir, *socketname, *errw;
    const char *errr;
    Socket *sock;

    *name = NULL;

    username = get_username();
    socketdir = dupprintf("%s.%s", dirprefix, username);
    sfree(username);

    assert(*socketdir == '/');
    if ((errw = make_dir_and_check_ours(socketdir)) != NULL) {
        *error = dupprintf("%s: %s\n", socketdir, errw);
        sfree(errw);
        sfree(socketdir);
        return NULL;
    }

    socketname = dupprintf("%s/pageant.%d", socketdir, (int)getpid());
    sock = new_unix_listener(unix_sock_addr(socketname), plug);
    if ((errr = sk_socket_error(sock)) != NULL) {
        *error = dupprintf("%s: %s\n", socketname, errr);
        sk_close(sock);
        sfree(socketname);
        rmdir(socketdir);
        sfree(socketdir);
        return NULL;
    }

    /*
     * Spawn a subprocess which will try to reliably delete our socket
     * and its containing directory when we terminate, in case we die
     * unexpectedly.
     */
    {
        int cleanup_pipe[2];
        pid_t pid;

        /* Don't worry if pipe or fork fails; it's not _that_ critical. */
        if (!pipe(cleanup_pipe)) {
            if ((pid = fork()) == 0) {
                int buf[1024];
                /*
                 * Our parent process holds the writing end of
                 * this pipe, and writes nothing to it. Hence,
                 * we expect read() to return EOF as soon as
                 * that process terminates.
                 */

                close(0);
                close(1);
                close(2);

                setpgid(0, 0);
                close(cleanup_pipe[1]);
                while (read(cleanup_pipe[0], buf, sizeof(buf)) > 0);
                unlink(socketname);
                rmdir(socketdir);
                _exit(0);
            } else if (pid < 0) {
                close(cleanup_pipe[0]);
                close(cleanup_pipe[1]);
            } else {
                close(cleanup_pipe[0]);
                cloexec(cleanup_pipe[1]);
            }
        }
    }

    *name = socketname;
    *error = NULL;
    sfree(socketdir);
    return sock;
}
