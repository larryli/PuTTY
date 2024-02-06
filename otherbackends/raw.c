/*
 * "Raw" backend.
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include "putty.h"

#define RAW_MAX_BACKLOG 4096

typedef struct Raw Raw;
struct Raw {
    Socket *s;
    bool closed_on_socket_error;
    size_t bufsize;
    Seat *seat;
    LogContext *logctx;
    Ldisc *ldisc;
    bool sent_console_eof, sent_socket_eof, socket_connected;
    char *description;

    Conf *conf;

    Plug plug;
    Backend backend;
    Interactor interactor;
};

static void raw_size(Backend *be, int width, int height);

static void c_write(Raw *raw, const void *buf, size_t len)
{
    size_t backlog = seat_stdout(raw->seat, buf, len);
    sk_set_frozen(raw->s, backlog > RAW_MAX_BACKLOG);
}

static void raw_log(Plug *plug, PlugLogType type, SockAddr *addr, int port,
                    const char *error_msg, int error_code)
{
    Raw *raw = container_of(plug, Raw, plug);
    backend_socket_log(raw->seat, raw->logctx, type, addr, port, error_msg,
                       error_code, raw->conf, raw->socket_connected);
    if (type == PLUGLOG_CONNECT_SUCCESS) {
        raw->socket_connected = true;
        if (raw->ldisc)
            ldisc_check_sendok(raw->ldisc);
    }
}

static void raw_check_close(Raw *raw)
{
    /*
     * Called after we send EOF on either the socket or the console.
     * Its job is to wind up the session once we have sent EOF on both.
     */
    if (raw->sent_console_eof && raw->sent_socket_eof) {
        if (raw->s) {
            sk_close(raw->s);
            raw->s = NULL;
            seat_notify_remote_exit(raw->seat);
            seat_notify_remote_disconnect(raw->seat);
        }
    }
}

static void raw_closing(Plug *plug, PlugCloseType type, const char *error_msg)
{
    Raw *raw = container_of(plug, Raw, plug);

    if (type != PLUGCLOSE_NORMAL) {
        /* A socket error has occurred. */
        if (raw->s) {
            sk_close(raw->s);
            raw->s = NULL;
            raw->closed_on_socket_error = true;
            seat_notify_remote_exit(raw->seat);
            seat_notify_remote_disconnect(raw->seat);
        }
        logevent(raw->logctx, error_msg);
        if (type != PLUGCLOSE_USER_ABORT)
            seat_connection_fatal(raw->seat, "%s", error_msg);
    } else {
        /* Otherwise, the remote side closed the connection normally. */
        if (!raw->sent_console_eof && seat_eof(raw->seat)) {
            /*
             * The front end wants us to close the outgoing side of the
             * connection as soon as we see EOF from the far end.
             */
            if (!raw->sent_socket_eof) {
                if (raw->s)
                    sk_write_eof(raw->s);
                raw->sent_socket_eof= true;
            }
        }
        raw->sent_console_eof = true;
        raw_check_close(raw);
    }
}

static void raw_receive(Plug *plug, int urgent, const char *data, size_t len)
{
    Raw *raw = container_of(plug, Raw, plug);
    c_write(raw, data, len);
}

static void raw_sent(Plug *plug, size_t bufsize)
{
    Raw *raw = container_of(plug, Raw, plug);
    raw->bufsize = bufsize;
    seat_sent(raw->seat, raw->bufsize);
}

static const PlugVtable Raw_plugvt = {
    .log = raw_log,
    .closing = raw_closing,
    .receive = raw_receive,
    .sent = raw_sent,
};

static char *raw_description(Interactor *itr)
{
    Raw *raw = container_of(itr, Raw, interactor);
    return dupstr(raw->description);
}

static LogPolicy *raw_logpolicy(Interactor *itr)
{
    Raw *raw = container_of(itr, Raw, interactor);
    return log_get_policy(raw->logctx);
}

static Seat *raw_get_seat(Interactor *itr)
{
    Raw *raw = container_of(itr, Raw, interactor);
    return raw->seat;
}

static void raw_set_seat(Interactor *itr, Seat *seat)
{
    Raw *raw = container_of(itr, Raw, interactor);
    raw->seat = seat;
}

static const InteractorVtable Raw_interactorvt = {
    .description = raw_description,
    .logpolicy = raw_logpolicy,
    .get_seat = raw_get_seat,
    .set_seat = raw_set_seat,
};

/*
 * Called to set up the raw connection.
 *
 * Returns an error message, or NULL on success.
 *
 * Also places the canonical host name into `realhost'. It must be
 * freed by the caller.
 */
static char *raw_init(const BackendVtable *vt, Seat *seat,
                      Backend **backend_handle, LogContext *logctx,
                      Conf *conf, const char *host, int port,
                      char **realhost, bool nodelay, bool keepalive)
{
    SockAddr *addr;
    const char *err;
    Raw *raw;
    int addressfamily;
    char *loghost;

    raw = snew(Raw);
    memset(raw, 0, sizeof(Raw));
    raw->plug.vt = &Raw_plugvt;
    raw->backend.vt = vt;
    raw->interactor.vt = &Raw_interactorvt;
    raw->backend.interactor = &raw->interactor;
    raw->s = NULL;
    raw->closed_on_socket_error = false;
    *backend_handle = &raw->backend;
    raw->sent_console_eof = raw->sent_socket_eof = false;
    raw->bufsize = 0;
    raw->socket_connected = false;
    raw->conf = conf_copy(conf);
    raw->description = default_description(vt, host, port);

    raw->seat = seat;
    raw->logctx = logctx;

    addressfamily = conf_get_int(conf, CONF_addressfamily);
    /*
     * Try to find host.
     */
    addr = name_lookup(host, port, realhost, conf, addressfamily,
                       raw->logctx, "main connection");
    if ((err = sk_addr_error(addr)) != NULL) {
        sk_addr_free(addr);
        return dupstr(err);
    }

    if (port < 0)
        port = 23;                     /* default telnet port */

    /*
     * Open socket.
     */
    raw->s = new_connection(addr, *realhost, port, false, true, nodelay,
                            keepalive, &raw->plug, conf, &raw->interactor);
    if ((err = sk_socket_error(raw->s)) != NULL)
        return dupstr(err);

    /* No local authentication phase in this protocol */
    seat_set_trust_status(raw->seat, false);

    loghost = conf_get_str(conf, CONF_loghost);
    if (*loghost) {
        char *colon;

        sfree(*realhost);
        *realhost = dupstr(loghost);

        colon = host_strrchr(*realhost, ':');
        if (colon)
            *colon++ = '\0';
    }

    return NULL;
}

static void raw_free(Backend *be)
{
    Raw *raw = container_of(be, Raw, backend);

    if (is_tempseat(raw->seat))
        tempseat_free(raw->seat);
    if (raw->s)
        sk_close(raw->s);
    conf_free(raw->conf);
    sfree(raw->description);
    sfree(raw);
}

/*
 * Stub routine (we don't have any need to reconfigure this backend).
 */
static void raw_reconfig(Backend *be, Conf *conf)
{
}

/*
 * Called to send data down the raw connection.
 */
static void raw_send(Backend *be, const char *buf, size_t len)
{
    Raw *raw = container_of(be, Raw, backend);

    if (raw->s == NULL)
        return;

    raw->bufsize = sk_write(raw->s, buf, len);
}

/*
 * Called to query the current socket sendability status.
 */
static size_t raw_sendbuffer(Backend *be)
{
    Raw *raw = container_of(be, Raw, backend);
    return raw->bufsize;
}

/*
 * Called to set the size of the window
 */
static void raw_size(Backend *be, int width, int height)
{
    /* Do nothing! */
    return;
}

/*
 * Send raw special codes. We only handle outgoing EOF here.
 */
static void raw_special(Backend *be, SessionSpecialCode code, int arg)
{
    Raw *raw = container_of(be, Raw, backend);
    if (code == SS_EOF && raw->s) {
        sk_write_eof(raw->s);
        raw->sent_socket_eof= true;
        raw_check_close(raw);
    }

    return;
}

/*
 * Return a list of the special codes that make sense in this
 * protocol.
 */
static const SessionSpecial *raw_get_specials(Backend *be)
{
    return NULL;
}

static bool raw_connected(Backend *be)
{
    Raw *raw = container_of(be, Raw, backend);
    return raw->s != NULL;
}

static bool raw_sendok(Backend *be)
{
    Raw *raw = container_of(be, Raw, backend);
    return raw->socket_connected;
}

static void raw_unthrottle(Backend *be, size_t backlog)
{
    Raw *raw = container_of(be, Raw, backend);
    sk_set_frozen(raw->s, backlog > RAW_MAX_BACKLOG);
}

static bool raw_ldisc(Backend *be, int option)
{
    if (option == LD_EDIT || option == LD_ECHO)
        return true;
    return false;
}

static void raw_provide_ldisc(Backend *be, Ldisc *ldisc)
{
    Raw *raw = container_of(be, Raw, backend);
    raw->ldisc = ldisc;
}

static int raw_exitcode(Backend *be)
{
    Raw *raw = container_of(be, Raw, backend);
    if (raw->s != NULL)
        return -1;                     /* still connected */
    else if (raw->closed_on_socket_error)
        return INT_MAX;     /* a socket error counts as an unclean exit */
    else
        /* Exit codes are a meaningless concept in the Raw protocol */
        return 0;
}

/*
 * cfg_info for Raw does nothing at all.
 */
static int raw_cfg_info(Backend *be)
{
    return 0;
}

const BackendVtable raw_backend = {
    .init = raw_init,
    .free = raw_free,
    .reconfig = raw_reconfig,
    .send = raw_send,
    .sendbuffer = raw_sendbuffer,
    .size = raw_size,
    .special = raw_special,
    .get_specials = raw_get_specials,
    .connected = raw_connected,
    .exitcode = raw_exitcode,
    .sendok = raw_sendok,
    .ldisc_option_state = raw_ldisc,
    .provide_ldisc = raw_provide_ldisc,
    .unthrottle = raw_unthrottle,
    .cfg_info = raw_cfg_info,
    .id = "raw",
    .displayname_tc = "Raw",
    .displayname_lc = "raw",
    .protocol = PROT_RAW,
    .default_port = 0,
};
