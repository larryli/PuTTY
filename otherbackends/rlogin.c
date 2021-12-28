/*
 * Rlogin backend.
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>

#include "putty.h"

#define RLOGIN_MAX_BACKLOG 4096

typedef struct Rlogin Rlogin;
struct Rlogin {
    Socket *s;
    bool closed_on_socket_error;
    int bufsize;
    bool socket_connected;
    bool firstbyte;
    bool cansize;
    int term_width, term_height;
    Seat *seat;
    LogContext *logctx;
    Ldisc *ldisc;
    char *description;

    Conf *conf;

    /* In case we need to read a username from the terminal before starting */
    prompts_t *prompt;

    Plug plug;
    Backend backend;
    Interactor interactor;
};

static void rlogin_startup(Rlogin *rlogin, SeatPromptResult spr,
                           const char *ruser);
static void rlogin_try_username_prompt(void *ctx);

static void c_write(Rlogin *rlogin, const void *buf, size_t len)
{
    size_t backlog = seat_stdout(rlogin->seat, buf, len);
    sk_set_frozen(rlogin->s, backlog > RLOGIN_MAX_BACKLOG);
}

static void rlogin_log(Plug *plug, PlugLogType type, SockAddr *addr, int port,
                       const char *error_msg, int error_code)
{
    Rlogin *rlogin = container_of(plug, Rlogin, plug);
    backend_socket_log(rlogin->seat, rlogin->logctx, type, addr, port,
                       error_msg, error_code,
                       rlogin->conf, rlogin->socket_connected);
    if (type == PLUGLOG_CONNECT_SUCCESS) {
        rlogin->socket_connected = true;

        char *ruser = get_remote_username(rlogin->conf);
        if (ruser) {
            /*
             * If we already know the remote username, call
             * rlogin_startup, which will send the initial protocol
             * greeting including local username, remote username,
             * terminal type and terminal speed.
             */
            /* Next terminal output will come from server */
            seat_set_trust_status(rlogin->seat, false);
            rlogin_startup(rlogin, SPR_OK, ruser);
            sfree(ruser);
        } else {
            /*
             * Otherwise, set up a prompts_t asking for the local
             * username. If it completes synchronously, call
             * rlogin_startup as above; otherwise, wait until it does.
             */
            rlogin->prompt = new_prompts();
            rlogin->prompt->to_server = true;
            rlogin->prompt->from_server = false;
            rlogin->prompt->name = dupstr("Rlogin login name");
            rlogin->prompt->callback = rlogin_try_username_prompt;
            rlogin->prompt->callback_ctx = rlogin;
            add_prompt(rlogin->prompt, dupstr("rlogin username: "), true);
            rlogin_try_username_prompt(rlogin);
        }
    }
}

static void rlogin_closing(Plug *plug, PlugCloseType type,
                           const char *error_msg)
{
    Rlogin *rlogin = container_of(plug, Rlogin, plug);

    /*
     * We don't implement independent EOF in each direction for Telnet
     * connections; as soon as we get word that the remote side has
     * sent us EOF, we wind up the whole connection.
     */

    if (rlogin->s) {
        sk_close(rlogin->s);
        rlogin->s = NULL;
        if (error_msg)
            rlogin->closed_on_socket_error = true;
        seat_notify_remote_exit(rlogin->seat);
        seat_notify_remote_disconnect(rlogin->seat);
    }
    if (type != PLUGCLOSE_NORMAL) {
        /* A socket error has occurred. */
        logevent(rlogin->logctx, error_msg);
        if (type != PLUGCLOSE_USER_ABORT)
            seat_connection_fatal(rlogin->seat, "%s", error_msg);
    }
    /* Otherwise, the remote side closed the connection normally. */
}

static void rlogin_receive(
    Plug *plug, int urgent, const char *data, size_t len)
{
    Rlogin *rlogin = container_of(plug, Rlogin, plug);
    if (len == 0)
        return;
    if (urgent == 2) {
        char c;

        c = *data++;
        len--;
        if (c == '\x80') {
            rlogin->cansize = true;
            backend_size(&rlogin->backend,
                         rlogin->term_width, rlogin->term_height);
        }
        /*
         * We should flush everything (aka Telnet SYNCH) if we see
         * 0x02, and we should turn off and on _local_ flow control
         * on 0x10 and 0x20 respectively. I'm not convinced it's
         * worth it...
         */
    } else {
        /*
         * Main rlogin protocol. This is really simple: the first
         * byte is expected to be NULL and is ignored, and the rest
         * is printed.
         */
        if (rlogin->firstbyte) {
            if (data[0] == '\0') {
                data++;
                len--;
            }
            rlogin->firstbyte = false;
        }
        if (len > 0)
            c_write(rlogin, data, len);
    }
}

static void rlogin_sent(Plug *plug, size_t bufsize)
{
    Rlogin *rlogin = container_of(plug, Rlogin, plug);
    rlogin->bufsize = bufsize;
    seat_sent(rlogin->seat, rlogin->bufsize);
}

static void rlogin_startup(Rlogin *rlogin, SeatPromptResult spr,
                           const char *ruser)
{
    char z = 0;
    char *p;

    if (spr.kind == SPRK_USER_ABORT) {
        /* User aborted at the username prompt. */
        sk_close(rlogin->s);
        rlogin->s = NULL;
        seat_notify_remote_exit(rlogin->seat);
    } else if (spr.kind == SPRK_SW_ABORT) {
        /* Something else went wrong at the username prompt, so we
         * have to show some kind of error. */
        sk_close(rlogin->s);
        rlogin->s = NULL;
        char *err = spr_get_error_message(spr);
        seat_connection_fatal(rlogin->seat, "%s", err);
        sfree(err);
    } else {
        sk_write(rlogin->s, &z, 1);
        p = conf_get_str(rlogin->conf, CONF_localusername);
        sk_write(rlogin->s, p, strlen(p));
        sk_write(rlogin->s, &z, 1);
        sk_write(rlogin->s, ruser, strlen(ruser));
        sk_write(rlogin->s, &z, 1);
        p = conf_get_str(rlogin->conf, CONF_termtype);
        sk_write(rlogin->s, p, strlen(p));
        sk_write(rlogin->s, "/", 1);
        p = conf_get_str(rlogin->conf, CONF_termspeed);
        sk_write(rlogin->s, p, strspn(p, "0123456789"));
        rlogin->bufsize = sk_write(rlogin->s, &z, 1);
    }

    rlogin->prompt = NULL;
    if (rlogin->ldisc)
        ldisc_check_sendok(rlogin->ldisc);
}

static const PlugVtable Rlogin_plugvt = {
    .log = rlogin_log,
    .closing = rlogin_closing,
    .receive = rlogin_receive,
    .sent = rlogin_sent,
};

static char *rlogin_description(Interactor *itr)
{
    Rlogin *rlogin = container_of(itr, Rlogin, interactor);
    return dupstr(rlogin->description);
}

static LogPolicy *rlogin_logpolicy(Interactor *itr)
{
    Rlogin *rlogin = container_of(itr, Rlogin, interactor);
    return log_get_policy(rlogin->logctx);
}

static Seat *rlogin_get_seat(Interactor *itr)
{
    Rlogin *rlogin = container_of(itr, Rlogin, interactor);
    return rlogin->seat;
}

static void rlogin_set_seat(Interactor *itr, Seat *seat)
{
    Rlogin *rlogin = container_of(itr, Rlogin, interactor);
    rlogin->seat = seat;
}

static const InteractorVtable Rlogin_interactorvt = {
    .description = rlogin_description,
    .logpolicy = rlogin_logpolicy,
    .get_seat = rlogin_get_seat,
    .set_seat = rlogin_set_seat,
};

/*
 * Called to set up the rlogin connection.
 *
 * Returns an error message, or NULL on success.
 *
 * Also places the canonical host name into `realhost'. It must be
 * freed by the caller.
 */
static char *rlogin_init(const BackendVtable *vt, Seat *seat,
                         Backend **backend_handle, LogContext *logctx,
                         Conf *conf, const char *host, int port,
                         char **realhost, bool nodelay, bool keepalive)
{
    SockAddr *addr;
    const char *err;
    Rlogin *rlogin;
    int addressfamily;
    char *loghost;

    rlogin = snew(Rlogin);
    memset(rlogin, 0, sizeof(Rlogin));
    rlogin->plug.vt = &Rlogin_plugvt;
    rlogin->backend.vt = vt;
    rlogin->interactor.vt = &Rlogin_interactorvt;
    rlogin->backend.interactor = &rlogin->interactor;
    rlogin->s = NULL;
    rlogin->closed_on_socket_error = false;
    rlogin->seat = seat;
    rlogin->logctx = logctx;
    rlogin->term_width = conf_get_int(conf, CONF_width);
    rlogin->term_height = conf_get_int(conf, CONF_height);
    rlogin->socket_connected = false;
    rlogin->firstbyte = true;
    rlogin->cansize = false;
    rlogin->prompt = NULL;
    rlogin->conf = conf_copy(conf);
    rlogin->description = default_description(vt, host, port);
    *backend_handle = &rlogin->backend;

    addressfamily = conf_get_int(conf, CONF_addressfamily);
    /*
     * Try to find host.
     */
    addr = name_lookup(host, port, realhost, conf, addressfamily,
                       rlogin->logctx, "rlogin connection");
    if ((err = sk_addr_error(addr)) != NULL) {
        sk_addr_free(addr);
        return dupstr(err);
    }

    if (port < 0)
        port = 513;                    /* default rlogin port */

    /*
     * Open socket.
     */
    rlogin->s = new_connection(addr, *realhost, port, true, false,
                               nodelay, keepalive, &rlogin->plug, conf,
                               &rlogin->interactor);
    if ((err = sk_socket_error(rlogin->s)) != NULL)
        return dupstr(err);

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

static void rlogin_free(Backend *be)
{
    Rlogin *rlogin = container_of(be, Rlogin, backend);

    if (is_tempseat(rlogin->seat))
        tempseat_free(rlogin->seat);
    if (rlogin->prompt)
        free_prompts(rlogin->prompt);
    if (rlogin->s)
        sk_close(rlogin->s);
    conf_free(rlogin->conf);
    sfree(rlogin->description);
    sfree(rlogin);
}

/*
 * Stub routine (we don't have any need to reconfigure this backend).
 */
static void rlogin_reconfig(Backend *be, Conf *conf)
{
}

static void rlogin_try_username_prompt(void *ctx)
{
    Rlogin *rlogin = (Rlogin *)ctx;

    SeatPromptResult spr = seat_get_userpass_input(
        interactor_announce(&rlogin->interactor), rlogin->prompt);
    if (spr.kind == SPRK_INCOMPLETE)
        return;

    /* Next terminal output will come from server */
    seat_set_trust_status(rlogin->seat, false);

    /* Send the rlogin setup protocol data, and then we're ready to
     * start receiving normal input to send down the wire, which
     * rlogin_startup will signal to rlogin_sendok by nulling out
     * rlogin->prompt. */
    rlogin_startup(
        rlogin, spr, prompt_get_result_ref(rlogin->prompt->prompts[0]));
}

/*
 * Called to send data down the rlogin connection.
 */
static void rlogin_send(Backend *be, const char *buf, size_t len)
{
    Rlogin *rlogin = container_of(be, Rlogin, backend);

    if (rlogin->s == NULL)
        return;

    rlogin->bufsize = sk_write(rlogin->s, buf, len);
}

/*
 * Called to query the current socket sendability status.
 */
static size_t rlogin_sendbuffer(Backend *be)
{
    Rlogin *rlogin = container_of(be, Rlogin, backend);
    return rlogin->bufsize;
}

/*
 * Called to set the size of the window
 */
static void rlogin_size(Backend *be, int width, int height)
{
    Rlogin *rlogin = container_of(be, Rlogin, backend);
    char b[12] = { '\xFF', '\xFF', 0x73, 0x73, 0, 0, 0, 0, 0, 0, 0, 0 };

    rlogin->term_width = width;
    rlogin->term_height = height;

    if (rlogin->s == NULL || !rlogin->cansize)
        return;

    b[6] = rlogin->term_width >> 8;
    b[7] = rlogin->term_width & 0xFF;
    b[4] = rlogin->term_height >> 8;
    b[5] = rlogin->term_height & 0xFF;
    rlogin->bufsize = sk_write(rlogin->s, b, 12);
    return;
}

/*
 * Send rlogin special codes.
 */
static void rlogin_special(Backend *be, SessionSpecialCode code, int arg)
{
    /* Do nothing! */
    return;
}

/*
 * Return a list of the special codes that make sense in this
 * protocol.
 */
static const SessionSpecial *rlogin_get_specials(Backend *be)
{
    return NULL;
}

static bool rlogin_connected(Backend *be)
{
    Rlogin *rlogin = container_of(be, Rlogin, backend);
    return rlogin->s != NULL;
}

static bool rlogin_sendok(Backend *be)
{
    /*
     * We only want to receive input data if the socket is connected
     * and we're not still at the username prompt stage.
     */
    Rlogin *rlogin = container_of(be, Rlogin, backend);
    return rlogin->socket_connected && !rlogin->prompt;
}

static void rlogin_unthrottle(Backend *be, size_t backlog)
{
    Rlogin *rlogin = container_of(be, Rlogin, backend);
    sk_set_frozen(rlogin->s, backlog > RLOGIN_MAX_BACKLOG);
}

static bool rlogin_ldisc(Backend *be, int option)
{
    /* Rlogin *rlogin = container_of(be, Rlogin, backend); */
    return false;
}

static void rlogin_provide_ldisc(Backend *be, Ldisc *ldisc)
{
    Rlogin *rlogin = container_of(be, Rlogin, backend);
    rlogin->ldisc = ldisc;
}

static int rlogin_exitcode(Backend *be)
{
    Rlogin *rlogin = container_of(be, Rlogin, backend);
    if (rlogin->s != NULL)
        return -1;                     /* still connected */
    else if (rlogin->closed_on_socket_error)
        return INT_MAX;     /* a socket error counts as an unclean exit */
    else
        /* If we ever implement RSH, we'll probably need to do this properly */
        return 0;
}

/*
 * cfg_info for rlogin does nothing at all.
 */
static int rlogin_cfg_info(Backend *be)
{
    return 0;
}

const BackendVtable rlogin_backend = {
    .init = rlogin_init,
    .free = rlogin_free,
    .reconfig = rlogin_reconfig,
    .send = rlogin_send,
    .sendbuffer = rlogin_sendbuffer,
    .size = rlogin_size,
    .special = rlogin_special,
    .get_specials = rlogin_get_specials,
    .connected = rlogin_connected,
    .exitcode = rlogin_exitcode,
    .sendok = rlogin_sendok,
    .ldisc_option_state = rlogin_ldisc,
    .provide_ldisc = rlogin_provide_ldisc,
    .unthrottle = rlogin_unthrottle,
    .cfg_info = rlogin_cfg_info,
    .id = "rlogin",
    .displayname_tc = "Rlogin",
    .displayname_lc = "Rlogin", /* proper name, so capitalise it anyway */
    .protocol = PROT_RLOGIN,
    .default_port = 513,
};
