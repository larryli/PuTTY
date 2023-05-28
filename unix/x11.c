/*
 * x11.c: fetch local auth data for X forwarding.
 */

#include <ctype.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "putty.h"
#include "ssh.h"
#include "network.h"

void platform_get_x11_auth(struct X11Display *disp, Conf *conf)
{
    char *xauthfile;
    bool needs_free;

    /*
     * Find the .Xauthority file.
     */
    needs_free = false;
    xauthfile = getenv("XAUTHORITY");
    if (!xauthfile) {
        xauthfile = getenv("HOME");
        if (xauthfile) {
            xauthfile = dupcat(xauthfile, "/.Xauthority");
            needs_free = true;
        }
    }

    if (xauthfile) {
        Filename *xauthfn = filename_from_str(xauthfile);
        if (needs_free)
            sfree(xauthfile);

        x11_get_auth_from_authfile(disp, xauthfn);
        filename_free(xauthfn);
    }
}

const bool platform_uses_x11_unix_by_default = true;

int platform_make_x11_server(Plug *plug, const char *progname, int mindisp,
                             const char *screen_number_suffix,
                             ptrlen authproto, ptrlen authdata,
                             Socket **sockets, Conf *conf)
{
    char *tmpdir;
    char *authfilename = NULL;
    strbuf *authfiledata = NULL;
    char *unix_path = NULL;

    SockAddr *a_tcp = NULL, *a_unix = NULL;

    int authfd;
    FILE *authfp;

    int displayno;

    authfiledata = strbuf_new_nm();

    int nsockets = 0;

    /*
     * Look for a free TCP port to run our server on.
     */
    for (displayno = mindisp;; displayno++) {
        const char *err;
        int tcp_port = displayno + 6000;
        int addrtype = ADDRTYPE_IPV4;

        sockets[nsockets] = new_listener(
            NULL, tcp_port, plug, false, conf, addrtype);

        err = sk_socket_error(sockets[nsockets]);
        if (!err) {
            char *hostname = get_hostname();
            if (hostname) {
                char *canonicalname = NULL;
                a_tcp = sk_namelookup(hostname, &canonicalname, addrtype);
                sfree(canonicalname);
            }
            sfree(hostname);
            nsockets++;
            break;                     /* success! */
        } else {
            sk_close(sockets[nsockets]);
        }

        /*
         * If we weren't able to bind to this port because it's in use
         * by another program, go round this loop and try again. But
         * for any other reason, give up completely and return failure
         * to our caller.
         *
         * sk_socket_error currently has no machine-readable component
         * (it would need a cross-platform abstraction of the socket
         * error types we care about, plus translation from each OS
         * error enumeration into that). So we use the disgusting
         * approach of a string compare between the error string and
         * the one EADDRINUSE would have given :-(
         */
        if (strcmp(err, strerror(EADDRINUSE)))
            goto out;
    }

    if (a_tcp) {
        x11_format_auth_for_authfile(
            BinarySink_UPCAST(authfiledata),
            a_tcp, displayno, authproto, authdata);
    }

    /*
     * Try to establish the Unix-domain analogue. That may or may not
     * work - file permissions in /tmp may prevent it, for example -
     * but it's worth a try, and we don't consider it a fatal error if
     * it doesn't work.
     */
    unix_path = dupprintf("/tmp/.X11-unix/X%d", displayno);
    a_unix = unix_sock_addr(unix_path);

    sockets[nsockets] = new_unix_listener(a_unix, plug);
    if (!sk_socket_error(sockets[nsockets])) {
        x11_format_auth_for_authfile(
            BinarySink_UPCAST(authfiledata),
            a_unix, displayno, authproto, authdata);
        nsockets++;
    } else {
        sk_close(sockets[nsockets]);
        sfree(unix_path);
        unix_path = NULL;
    }

    /*
     * Decide where the authority data will be written.
     */

    tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir)
        tmpdir = "/tmp";

    authfilename = dupcat(tmpdir, "/", progname, "-Xauthority-XXXXXX");

    {
        int oldumask = umask(077);
        authfd = mkstemp(authfilename);
        umask(oldumask);
    }
    if (authfd < 0) {
        while (nsockets-- > 0)
            sk_close(sockets[nsockets]);
        goto out;
    }

    /*
     * Spawn a subprocess which will try to reliably delete our
     * auth file when we terminate, in case we die unexpectedly.
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
                close(authfd);
                while (read(cleanup_pipe[0], buf, sizeof(buf)) > 0);
                unlink(authfilename);
                if (unix_path)
                    unlink(unix_path);
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

    authfp = fdopen(authfd, "wb");
    fwrite(authfiledata->u, 1, authfiledata->len, authfp);
    fclose(authfp);

    {
        char *display = dupprintf(":%d%s", displayno, screen_number_suffix);
        conf_set_str_str(conf, CONF_environmt, "DISPLAY", display);
        sfree(display);
    }
    conf_set_str_str(conf, CONF_environmt, "XAUTHORITY", authfilename);

    /*
     * FIXME: return at least the DISPLAY and XAUTHORITY env settings,
     * and perhaps also the display number
     */

  out:
    if (a_tcp)
        sk_addr_free(a_tcp);
    /* a_unix doesn't need freeing, because new_unix_listener took it over */
    sfree(authfilename);
    strbuf_free(authfiledata);
    sfree(unix_path);
    return nsockets;
}
