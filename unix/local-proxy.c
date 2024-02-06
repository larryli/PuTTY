/*
 * local-proxy.c: Unix implementation of platform_new_connection(),
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
#include "proxy/proxy.h"

char *platform_setup_local_proxy(Socket *socket, const char *cmd)
{
    /*
     * Create the pipes to the proxy command, and spawn the proxy
     * command process.
     */
    int to_cmd_pipe[2], from_cmd_pipe[2], cmd_err_pipe[2];
    if (pipe(to_cmd_pipe) < 0 ||
        pipe(from_cmd_pipe) < 0 ||
        pipe(cmd_err_pipe) < 0) {
        return dupprintf("pipe: %s", strerror(errno));
    }
    cloexec(to_cmd_pipe[1]);
    cloexec(from_cmd_pipe[0]);
    cloexec(cmd_err_pipe[0]);

    int pid = fork();
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

    if (pid < 0) {
        return dupprintf("fork: %s", strerror(errno));
    }

    close(to_cmd_pipe[0]);
    close(from_cmd_pipe[1]);
    close(cmd_err_pipe[1]);

    setup_fd_socket(socket, from_cmd_pipe[0], to_cmd_pipe[1], cmd_err_pipe[0]);

    return NULL;
}

Socket *platform_new_connection(SockAddr *addr, const char *hostname,
                                int port, bool privport,
                                bool oobinline, bool nodelay, bool keepalive,
                                Plug *plug, Conf *conf, Interactor *itr)
{
    switch (conf_get_int(conf, CONF_proxy_type)) {
      case PROXY_CMD: {
        DeferredSocketOpener *opener = local_proxy_opener(
            addr, port, plug, conf, itr);
        Socket *socket = make_deferred_fd_socket(opener, addr, port, plug);
        local_proxy_opener_set_socket(opener, socket);
        return socket;
      }

      case PROXY_FUZZ: {
        char *cmd = format_telnet_command(addr, port, conf, NULL);
        int outfd = open("/dev/null", O_WRONLY);
        if (outfd == -1) {
            sfree(cmd);
            return new_error_socket_fmt(
                plug, "/dev/null: %s", strerror(errno));
        }
        int infd = open(cmd, O_RDONLY);
        if (infd == -1) {
            Socket *toret = new_error_socket_fmt(
                plug, "%s: %s", cmd, strerror(errno));
            sfree(cmd);
            close(outfd);
            return toret;
        }
        sfree(cmd);
        return make_fd_socket(infd, outfd, -1, addr, port, plug);
      }

      default:
        return NULL;
    }
}
