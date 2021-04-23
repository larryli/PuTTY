/*
 * uxproxy.c: Unix implementation of platform_new_connection(),
 * supporting an OpenSSH-like proxy command.
 */

#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include "tree234.h"
#include "putty.h"
#include "network.h"
#include "proxy.h"

Socket *platform_new_connection(SockAddr *addr, const char *hostname,
                                int port, bool privport,
                                bool oobinline, bool nodelay, bool keepalive,
                                Plug *plug, Conf *conf)
{
    char *cmd;

    int to_cmd_pipe[2], from_cmd_pipe[2], cmd_err_pipe[2], pid, proxytype;
    int infd, outfd, inerrfd;

    proxytype = conf_get_int(conf, CONF_proxy_type);
    if (proxytype != PROXY_CMD && proxytype != PROXY_FUZZ)
        return NULL;

    if (proxytype == PROXY_CMD) {
        cmd = format_telnet_command(addr, port, conf);

        {
            char *logmsg = dupprintf("Starting local proxy command: %s", cmd);
            plug_log(plug, PLUGLOG_PROXY_MSG, NULL, 0, logmsg, 0);
            sfree(logmsg);
        }

        /*
         * Create the pipes to the proxy command, and spawn the proxy
         * command process.
         */
        if (pipe(to_cmd_pipe) < 0 ||
            pipe(from_cmd_pipe) < 0 ||
            pipe(cmd_err_pipe) < 0) {
            sfree(cmd);
            return new_error_socket_fmt(plug, "pipe: %s", strerror(errno));
        }
        cloexec(to_cmd_pipe[1]);
        cloexec(from_cmd_pipe[0]);
        cloexec(cmd_err_pipe[0]);

        pid = fork();
        if (pid == 0) {
            close(0);
            close(1);
            dup2(to_cmd_pipe[0], 0);
            dup2(from_cmd_pipe[1], 1);
            close(to_cmd_pipe[0]);
            close(from_cmd_pipe[1]);
            dup2(cmd_err_pipe[1], 2);
            noncloexec(0);
            noncloexec(1);
            execl("/bin/sh", "sh", "-c", cmd, (void *)NULL);
            _exit(255);
        }

        sfree(cmd);

        if (pid < 0)
            return new_error_socket_fmt(plug, "fork: %s", strerror(errno));

        close(to_cmd_pipe[0]);
        close(from_cmd_pipe[1]);
        close(cmd_err_pipe[1]);

        outfd = to_cmd_pipe[1];
        infd = from_cmd_pipe[0];
        inerrfd = cmd_err_pipe[0];
    } else {
        cmd = format_telnet_command(addr, port, conf);
        outfd = open("/dev/null", O_WRONLY);
        if (outfd == -1) {
            sfree(cmd);
            return new_error_socket_fmt(
                plug, "/dev/null: %s", strerror(errno));
        }
        infd = open(cmd, O_RDONLY);
        if (infd == -1) {
            Socket *toret = new_error_socket_fmt(
                plug, "%s: %s", cmd, strerror(errno));
            sfree(cmd);
            close(outfd);
            return toret;
        }
        sfree(cmd);
        inerrfd = -1;
    }

    /* We are responsible for this and don't need it any more */
    sk_addr_free(addr);

    return make_fd_socket(infd, outfd, inerrfd, plug);
}
