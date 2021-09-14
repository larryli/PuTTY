/*
 * sshproxy.c: implement a Socket type that talks to an entire
 * subsidiary SSH connection (sometimes called a 'jump host').
 */

#include <stdio.h>
#include <assert.h>

#include "putty.h"
#include "ssh.h"
#include "network.h"
#include "storage.h"

const bool ssh_proxy_supported = true;

/*
 * TODO for future work:
 *
 * All the interactive prompts we present to the main Seat - the host
 * key and weak-crypto dialog boxes, and all prompts presented via the
 * userpass_input system - need adjusting so that it's clear to the
 * user _which_ SSH connection they come from. At the moment, you just
 * get shown a host key fingerprint or a cryptic "login as:" prompt,
 * and you have to guess which server you're currently supposed to be
 * interpreting it relative to.
 *
 * If the user manually aborts the attempt to make the proxy SSH
 * connection (e.g. by hitting ^C at a userpass prompt, or refusing to
 * accept the proxy server's host key), then an assertion failure
 * occurs, because the main backend receives an indication of
 * connection failure that causes it to want to call
 * seat_connection_fatal("Remote side unexpectedly closed network
 * connection"), which fails an assertion in tempseat.c because that
 * method of TempSeat expects never to be called. To fix this, I think
 * we need to distinguish 'connection attempt unexpectedly failed, in
 * a way the user needs to be told about' from 'connection attempt was
 * aborted by deliberate user action, so the user already knows'.
 */

typedef struct SshProxy {
    char *errmsg;
    Conf *conf;
    LogContext *logctx;
    Backend *backend;
    LogPolicy *clientlp;
    Seat *clientseat;

    ProxyStderrBuf psb;
    Plug *plug;

    bool frozen;
    bufchain ssh_to_socket;
    bool rcvd_eof_ssh_to_socket, sent_eof_ssh_to_socket;

    SockAddr *addr;
    int port;

    /* Traits implemented: we're a Socket from the point of view of
     * the client connection, and a Seat from the POV of the SSH
     * backend we instantiate. */
    Socket sock;
    LogPolicy logpolicy;
    Seat seat;
} SshProxy;

static Plug *sshproxy_plug(Socket *s, Plug *p)
{
    SshProxy *sp = container_of(s, SshProxy, sock);
    Plug *oldplug = sp->plug;
    if (p)
        sp->plug = p;
    return oldplug;
}

static void sshproxy_close(Socket *s)
{
    SshProxy *sp = container_of(s, SshProxy, sock);

    sk_addr_free(sp->addr);
    sfree(sp->errmsg);
    conf_free(sp->conf);
    if (sp->backend)
        backend_free(sp->backend);
    if (sp->logctx)
        log_free(sp->logctx);
    bufchain_clear(&sp->ssh_to_socket);

    delete_callbacks_for_context(sp);
    sfree(sp);
}

static size_t sshproxy_write(Socket *s, const void *data, size_t len)
{
    SshProxy *sp = container_of(s, SshProxy, sock);
    if (!sp->backend)
        return 0;
    backend_send(sp->backend, data, len);
    return backend_sendbuffer(sp->backend);
}

static size_t sshproxy_write_oob(Socket *s, const void *data, size_t len)
{
    /*
     * oob data is treated as inband; nasty, but nothing really
     * better we can do
     */
    return sshproxy_write(s, data, len);
}

static void sshproxy_write_eof(Socket *s)
{
    SshProxy *sp = container_of(s, SshProxy, sock);
    if (!sp->backend)
        return;
    backend_special(sp->backend, SS_EOF, 0);
}

static void try_send_ssh_to_socket(void *ctx);

static void sshproxy_set_frozen(Socket *s, bool is_frozen)
{
    SshProxy *sp = container_of(s, SshProxy, sock);
    sp->frozen = is_frozen;
    if (!sp->frozen)
        queue_toplevel_callback(try_send_ssh_to_socket, sp);
}

static const char *sshproxy_socket_error(Socket *s)
{
    SshProxy *sp = container_of(s, SshProxy, sock);
    return sp->errmsg;
}

static SocketPeerInfo *sshproxy_peer_info(Socket *s)
{
    return NULL;
}

static const SocketVtable SshProxy_sock_vt = {
    .plug = sshproxy_plug,
    .close = sshproxy_close,
    .write = sshproxy_write,
    .write_oob = sshproxy_write_oob,
    .write_eof = sshproxy_write_eof,
    .set_frozen = sshproxy_set_frozen,
    .socket_error = sshproxy_socket_error,
    .peer_info = sshproxy_peer_info,
};

static void sshproxy_eventlog(LogPolicy *lp, const char *event)
{
    SshProxy *sp = container_of(lp, SshProxy, logpolicy);
    log_proxy_stderr(sp->plug, &sp->psb, event, strlen(event));
    log_proxy_stderr(sp->plug, &sp->psb, "\n", 1);
}

static int sshproxy_askappend(LogPolicy *lp, Filename *filename,
                              void (*callback)(void *ctx, int result),
                              void *ctx)
{
    SshProxy *sp = container_of(lp, SshProxy, logpolicy);

    /*
     * If we have access to the outer LogPolicy, pass on this request
     * to the end user.
     */
    if (sp->clientlp)
        return lp_askappend(sp->clientlp, filename, callback, ctx);

    /*
     * Otherwise, fall back to the safe noninteractive assumption.
     */
    char *msg = dupprintf("Log file \"%s\" already exists; logging cancelled",
                          filename_to_str(filename));
    sshproxy_eventlog(lp, msg);
    sfree(msg);
    return 0;
}

static void sshproxy_logging_error(LogPolicy *lp, const char *event)
{
    SshProxy *sp = container_of(lp, SshProxy, logpolicy);

    /*
     * If we have access to the outer LogPolicy, pass on this request
     * to it.
     */
    if (sp->clientlp) {
        lp_logging_error(sp->clientlp, event);
        return;
    }

    /*
     * Otherwise, the best we can do is to put it in the outer SSH
     * connection's Event Log.
     */
    char *msg = dupprintf("Logging error: %s", event);
    sshproxy_eventlog(lp, msg);
    sfree(msg);
}

static const LogPolicyVtable SshProxy_logpolicy_vt = {
    .eventlog = sshproxy_eventlog,
    .askappend = sshproxy_askappend,
    .logging_error = sshproxy_logging_error,
    .verbose = null_lp_verbose_no,
};

/*
 * Function called when we encounter an error during connection setup that's
 * likely to be the cause of terminating the proxy SSH connection. Putting it
 * in the Event Log is useful on general principles; also putting it in
 * sp->errmsg meaks that it will be passed back through plug_closing when the
 * proxy SSH connection actually terminates, so that the end user will see
 * what went wrong in the proxy connection.
 */
static void sshproxy_error(SshProxy *sp, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *msg = dupvprintf(fmt, ap);
    va_end(ap);

    if (!sp->errmsg)
        sp->errmsg = dupstr(msg);

    sshproxy_eventlog(&sp->logpolicy, msg);
    sfree(msg);
}

static void try_send_ssh_to_socket(void *ctx)
{
    SshProxy *sp = (SshProxy *)ctx;

    if (sp->frozen)
        return;

    while (bufchain_size(&sp->ssh_to_socket)) {
        ptrlen pl = bufchain_prefix(&sp->ssh_to_socket);
        plug_receive(sp->plug, 0, pl.ptr, pl.len);
        bufchain_consume(&sp->ssh_to_socket, pl.len);
    }

    if (sp->rcvd_eof_ssh_to_socket &&
        !sp->sent_eof_ssh_to_socket) {
        sp->sent_eof_ssh_to_socket = true;
        plug_closing(sp->plug, sp->errmsg, 0, 0);
    }
}

static void sshproxy_notify_session_started(Seat *seat)
{
    SshProxy *sp = container_of(seat, SshProxy, seat);
    plug_log(sp->plug, PLUGLOG_CONNECT_SUCCESS, sp->addr, sp->port, NULL, 0);
}

static size_t sshproxy_output(Seat *seat, bool is_stderr,
                              const void *data, size_t len)
{
    SshProxy *sp = container_of(seat, SshProxy, seat);
    bufchain_add(&sp->ssh_to_socket, data, len);
    try_send_ssh_to_socket(sp);
    return bufchain_size(&sp->ssh_to_socket);
}

static bool sshproxy_eof(Seat *seat)
{
    SshProxy *sp = container_of(seat, SshProxy, seat);
    sp->rcvd_eof_ssh_to_socket = true;
    try_send_ssh_to_socket(sp);
    return false;
}

static void sshproxy_sent(Seat *seat, size_t new_bufsize)
{
    SshProxy *sp = container_of(seat, SshProxy, seat);
    plug_sent(sp->plug, new_bufsize);
}

static void sshproxy_notify_remote_disconnect(Seat *seat)
{
    SshProxy *sp = container_of(seat, SshProxy, seat);
    if (!sp->rcvd_eof_ssh_to_socket && !backend_connected(sp->backend))
        sshproxy_eof(seat);
}

static int sshproxy_get_userpass_input(Seat *seat, prompts_t *p)
{
    SshProxy *sp = container_of(seat, SshProxy, seat);

    if (sp->clientseat) {
        /*
         * If we have access to the outer Seat, pass this prompt
         * request on to it. FIXME: appropriately adjusted
         */
        return seat_get_userpass_input(sp->clientseat, p);
    }

    /*
     * Otherwise, behave as if noninteractive (like plink -batch):
     * reject all attempts to present a prompt to the user, and log in
     * the Event Log to say why not.
     */
    sshproxy_error(sp, "Unable to provide interactive authentication "
                   "requested by proxy SSH connection");
    return 0;
}

static void sshproxy_connection_fatal_callback(void *vctx)
{
    SshProxy *sp = (SshProxy *)vctx;
    plug_closing(sp->plug, sp->errmsg, 0, true);
}

static void sshproxy_connection_fatal(Seat *seat, const char *message)
{
    SshProxy *sp = container_of(seat, SshProxy, seat);
    if (!sp->errmsg) {
        sp->errmsg = dupprintf(
            "fatal error in proxy SSH connection: %s", message);
        queue_toplevel_callback(sshproxy_connection_fatal_callback, sp);
    }
}

static int sshproxy_verify_ssh_host_key(
        Seat *seat, const char *host, int port, const char *keytype,
        char *keystr, const char *keydisp, char **key_fingerprints,
        void (*callback)(void *ctx, int result), void *ctx)
{
    SshProxy *sp = container_of(seat, SshProxy, seat);

    if (sp->clientseat) {
        /*
         * If we have access to the outer Seat, pass this prompt
         * request on to it. FIXME: appropriately adjusted
         */
        return seat_verify_ssh_host_key(
            sp->clientseat, host, port, keytype, keystr, keydisp,
            key_fingerprints, callback, ctx);
    }

    /*
     * Otherwise, behave as if we're in batch mode: directly verify
     * the host key against the cache, and if that fails, take the
     * safe option in the absence of interactive confirmation, and
     * abort the connection.
     */
    int hkstatus = verify_host_key(host, port, keytype, keystr);
    FingerprintType fptype = ssh2_pick_default_fingerprint(key_fingerprints);

    switch (hkstatus) {
      case 0:                          /* host key matched */
        return 1;

      case 1:                          /* host key not in cache at all */
        sshproxy_error(sp, "Host key not in cache for %s:%d (fingerprint %s). "
                       "Abandoning proxy SSH connection.", host, port, 
                       key_fingerprints[fptype]);
        return 0;

      case 2:
        sshproxy_error(sp, "HOST KEY DOES NOT MATCH CACHE for %s:%d "
                       "(fingerprint %s). Abandoning proxy SSH connection.",
                       host, port, key_fingerprints[fptype]);
        return 0;

      default:
        unreachable("bad return value from verify_host_key");
    }
}

static int sshproxy_confirm_weak_crypto_primitive(
        Seat *seat, const char *algtype, const char *algname,
        void (*callback)(void *ctx, int result), void *ctx)
{
    SshProxy *sp = container_of(seat, SshProxy, seat);

    if (sp->clientseat) {
        /*
         * If we have access to the outer Seat, pass this prompt
         * request on to it. FIXME: appropriately adjusted
         */
        return seat_confirm_weak_crypto_primitive(
            sp->clientseat, algtype, algname, callback, ctx);
    }

    /*
     * Otherwise, behave as if we're in batch mode: take the safest
     * option.
     */
    sshproxy_error(sp, "First %s supported by server is %s, below warning "
                   "threshold. Abandoning proxy SSH connection.",
                   algtype, algname);
    return 0;
}

static int sshproxy_confirm_weak_cached_hostkey(
        Seat *seat, const char *algname, const char *betteralgs,
        void (*callback)(void *ctx, int result), void *ctx)
{
    SshProxy *sp = container_of(seat, SshProxy, seat);

    if (sp->clientseat) {
        /*
         * If we have access to the outer Seat, pass this prompt
         * request on to it. FIXME: appropriately adjusted
         */
        return seat_confirm_weak_cached_hostkey(
            sp->clientseat, algname, betteralgs, callback, ctx);
    }

    /*
     * Otherwise, behave as if we're in batch mode: take the safest
     * option.
     */
    sshproxy_error(sp, "First host key type stored for server is %s, below "
                   "warning threshold. Abandoning proxy SSH connection.",
                   algname);
    return 0;
}

static void sshproxy_set_trust_status(Seat *seat, bool trusted)
{
    /*
     * This is called by the proxy SSH connection, to set our Seat
     * into a given trust status. We can safely do nothing here, and
     * have can_set_trust_status return true to claim we did something
     * (effectively eliminating the spoofing defences completely, by
     * suppressing the 'press Return to begin session' prompt and not
     * providing anything in place of it), on the basis that session
     * I/O from the proxy SSH connection is never passed directly on
     * to the end user, so a malicious proxy SSH server wouldn't be
     * able to spoof our human in any case.
     */
}

static bool sshproxy_can_set_trust_status(Seat *seat)
{
    return true; /* see comment above */
}

static const SeatVtable SshProxy_seat_vt = {
    .output = sshproxy_output,
    .eof = sshproxy_eof,
    .sent = sshproxy_sent,
    .get_userpass_input = sshproxy_get_userpass_input,
    .notify_session_started = sshproxy_notify_session_started,
    .notify_remote_exit = nullseat_notify_remote_exit,
    .notify_remote_disconnect = sshproxy_notify_remote_disconnect,
    .connection_fatal = sshproxy_connection_fatal,
    .update_specials_menu = nullseat_update_specials_menu,
    .get_ttymode = nullseat_get_ttymode,
    .set_busy_status = nullseat_set_busy_status,
    .verify_ssh_host_key = sshproxy_verify_ssh_host_key,
    .confirm_weak_crypto_primitive = sshproxy_confirm_weak_crypto_primitive,
    .confirm_weak_cached_hostkey = sshproxy_confirm_weak_cached_hostkey,
    .is_utf8 = nullseat_is_never_utf8,
    .echoedit_update = nullseat_echoedit_update,
    .get_x_display = nullseat_get_x_display,
    .get_windowid = nullseat_get_windowid,
    .get_window_pixel_size = nullseat_get_window_pixel_size,
    .stripctrl_new = nullseat_stripctrl_new,
    .set_trust_status = sshproxy_set_trust_status,
    .can_set_trust_status = sshproxy_can_set_trust_status,
    .verbose = nullseat_verbose_no,
    .interactive = nullseat_interactive_no,
    .get_cursor_position = nullseat_get_cursor_position,
};

Socket *sshproxy_new_connection(SockAddr *addr, const char *hostname,
                                int port, bool privport,
                                bool oobinline, bool nodelay, bool keepalive,
                                Plug *plug, Conf *clientconf,
                                LogPolicy *clientlp, Seat **clientseat)
{
    SshProxy *sp = snew(SshProxy);
    memset(sp, 0, sizeof(*sp));

    sp->sock.vt = &SshProxy_sock_vt;
    sp->logpolicy.vt = &SshProxy_logpolicy_vt;
    sp->seat.vt = &SshProxy_seat_vt;
    sp->plug = plug;
    psb_init(&sp->psb);
    bufchain_init(&sp->ssh_to_socket);

    sp->addr = addr;
    sp->port = port;

    sp->conf = conf_new();
    /* Try to treat proxy_hostname as the title of a saved session. If
     * that fails, set up a default Conf of our own treating it as a
     * hostname. */
    const char *proxy_hostname = conf_get_str(clientconf, CONF_proxy_host);
    if (do_defaults(proxy_hostname, sp->conf)) {
        if (!conf_launchable(sp->conf)) {
            sp->errmsg = dupprintf("saved session '%s' is not launchable",
                                   proxy_hostname);
            return &sp->sock;
        }
    } else {
        do_defaults(NULL, sp->conf);
        /* In hostname mode, we default to PROT_SSH. This is more useful than
         * the obvious approach of defaulting to the protocol defined in
         * Default Settings, because only SSH (ok, and bare ssh-connection)
         * can be used for this kind of proxy. */
        conf_set_int(sp->conf, CONF_protocol, PROT_SSH);
        conf_set_str(sp->conf, CONF_host, proxy_hostname);
        conf_set_int(sp->conf, CONF_port,
                     conf_get_int(clientconf, CONF_proxy_port));
    }
    const char *proxy_username = conf_get_str(clientconf, CONF_proxy_username);
    if (*proxy_username)
        conf_set_str(sp->conf, CONF_username, proxy_username);

    const struct BackendVtable *backvt = backend_vt_from_proto(
        conf_get_int(sp->conf, CONF_protocol));

    /*
     * We don't actually need an _SSH_ session specifically: it's also
     * OK to use PROT_SSHCONN, because really, the criterion is
     * whether setting CONF_ssh_nc_host will do anything useful. So
     * our check is for whether the backend sets the flag promising
     * that it does.
     */
    if (!(backvt->flags & BACKEND_SUPPORTS_NC_HOST)) {
        sp->errmsg = dupprintf("saved session '%s' is not an SSH session",
                               proxy_hostname);
        return &sp->sock;
    }

    /*
     * We also expect that the backend will announce a willingness to
     * notify us that the session has started. Any backend providing
     * NC_HOST should also provide this.
     */
    assert(backvt->flags & BACKEND_NOTIFIES_SESSION_START &&
           "Backend provides NC_HOST without SESSION_START!");

    /*
     * Turn off SSH features we definitely don't want. It would be
     * awkward and counterintuitive to have the proxy SSH connection
     * become a connection-sharing upstream (but it's fine to have it
     * be a downstream, if that's configured). And we don't want to
     * open X forwardings, agent forwardings or (other) port
     * forwardings as a side effect of this one operation.
     */
    conf_set_bool(sp->conf, CONF_ssh_connection_sharing_upstream, false);
    conf_set_bool(sp->conf, CONF_x11_forward, false);
    conf_set_bool(sp->conf, CONF_agentfwd, false);
    for (const char *subkey;
         (subkey = conf_get_str_nthstrkey(sp->conf, CONF_portfwd, 0)) != NULL;)
        conf_del_str_str(sp->conf, CONF_portfwd, subkey);

    /*
     * We'll only be running one channel through this connection
     * (since we've just turned off all the other things we might have
     * done with it), so we can configure it as simple.
     */
    conf_set_bool(sp->conf, CONF_ssh_simple, true);

    /*
     * Configure the main channel of this SSH session to be a
     * direct-tcpip connection to the destination host/port.
     */
    conf_set_str(sp->conf, CONF_ssh_nc_host, hostname);
    conf_set_int(sp->conf, CONF_ssh_nc_port, port);

    sp->logctx = log_init(&sp->logpolicy, sp->conf);

    char *error, *realhost;
    error = backend_init(backvt, &sp->seat, &sp->backend, sp->logctx, sp->conf,
                         conf_get_str(sp->conf, CONF_host),
                         conf_get_int(sp->conf, CONF_port),
                         &realhost, nodelay,
                         conf_get_bool(sp->conf, CONF_tcp_keepalives));
    if (error) {
        sp->errmsg = dupprintf("unable to open SSH proxy connection: %s",
                               error);
        return &sp->sock;
    }

    sfree(realhost);

    /*
     * If we've been given useful bits and pieces for interacting with
     * the end user, squirrel them away now.
     */
    sp->clientlp = clientlp;
    if (clientseat && (backvt->flags & BACKEND_NOTIFIES_SESSION_START)) {
        /*
         * We can only keep the client's Seat if our own backend will
         * tell us when to give it back. (SSH-based backends _should_
         * do that, but we check the flag here anyway.)
         *
         * Also, check if the client already has a TempSeat, and if
         * so, don't wrap it with another one.
         */
        if (is_tempseat(*clientseat)) {
            sp->clientseat = tempseat_get_real(*clientseat);
        } else {
            sp->clientseat = *clientseat;
            *clientseat = tempseat_new(sp->clientseat);
        }
    }

    return &sp->sock;
}
