/*
 * Packet protocol layer for the SSH-2 connection protocol (RFC 4254).
 */

#include <assert.h>

#include "putty.h"
#include "ssh.h"
#include "sshbpp.h"
#include "sshppl.h"
#include "sshchan.h"
#include "sshcr.h"

struct ssh2_channel;

struct outstanding_global_request;

struct ssh2_connection_state {
    int crState;

    Ssh *ssh;

    ssh_sharing_state *connshare;
    char *peer_verstring;

    struct ssh2_channel *mainchan;     /* primary session channel */
    char *mainchan_open_error;
    int mainchan_ready;
    int echoedit;
    int mainchan_eof_pending, mainchan_eof_sent;
    int session_attempt, session_status;
    int term_width, term_height, term_width_orig, term_height_orig;
    int want_user_input;

    int ssh_is_simple;
    int persistent;

    Conf *conf;

    tree234 *channels;		       /* indexed by local id */
    int all_channels_throttled;

    int X11_fwd_enabled;
    tree234 *x11authtree;

    int got_pty;
    int agent_fwd_enabled;

    tree234 *rportfwds;
    PortFwdManager *portfwdmgr;
    int portfwdmgr_configured;

    /*
     * These store the list of global requests that we're waiting for
     * replies to. (REQUEST_FAILURE doesn't come with any indication
     * of what message caused it, so we have to keep track of the
     * queue ourselves.)
     */
    struct outstanding_global_request *globreq_head, *globreq_tail;

    ConnectionLayer cl;
    PacketProtocolLayer ppl;
};

static int ssh2_rportfwd_cmp(void *av, void *bv)
{
    struct ssh_rportfwd *a = (struct ssh_rportfwd *) av;
    struct ssh_rportfwd *b = (struct ssh_rportfwd *) bv;
    int i;
    if ( (i = strcmp(a->shost, b->shost)) != 0)
	return i < 0 ? -1 : +1;
    if (a->sport > b->sport)
	return +1;
    if (a->sport < b->sport)
	return -1;
    return 0;
}

static void ssh2_connection_free(PacketProtocolLayer *); 
static void ssh2_connection_process_queue(PacketProtocolLayer *);
static int ssh2_connection_get_specials(
    PacketProtocolLayer *ppl, add_special_fn_t add_special, void *ctx);
static void ssh2_connection_special_cmd(PacketProtocolLayer *ppl,
                                        SessionSpecialCode code, int arg);
static int ssh2_connection_want_user_input(PacketProtocolLayer *ppl);
static void ssh2_connection_got_user_input(PacketProtocolLayer *ppl);
static void ssh2_connection_reconfigure(PacketProtocolLayer *ppl, Conf *conf);

static const struct PacketProtocolLayerVtable ssh2_connection_vtable = {
    ssh2_connection_free,
    ssh2_connection_process_queue,
    ssh2_connection_get_specials,
    ssh2_connection_special_cmd,
    ssh2_connection_want_user_input,
    ssh2_connection_got_user_input,
    ssh2_connection_reconfigure,
    "ssh-connection",
};

static struct ssh_rportfwd *ssh2_rportfwd_alloc(
    ConnectionLayer *cl,
    const char *shost, int sport, const char *dhost, int dport,
    int addressfamily, const char *log_description, PortFwdRecord *pfr,
    ssh_sharing_connstate *share_ctx);
static void ssh2_rportfwd_remove(
    ConnectionLayer *cl, struct ssh_rportfwd *rpf);
static SshChannel *ssh2_lportfwd_open(
    ConnectionLayer *cl, const char *hostname, int port,
    const char *org, Channel *chan);
static struct X11FakeAuth *ssh2_add_sharing_x11_display(
    ConnectionLayer *cl, int authtype, ssh_sharing_connstate *share_cs,
    share_channel *share_chan);
static void ssh2_remove_sharing_x11_display(ConnectionLayer *cl,
                                            struct X11FakeAuth *auth);
static void ssh2_send_packet_from_downstream(
    ConnectionLayer *cl, unsigned id, int type,
    const void *pkt, int pktlen, const char *additional_log_text);
static unsigned ssh2_alloc_sharing_channel(
    ConnectionLayer *cl, ssh_sharing_connstate *connstate);
static void ssh2_delete_sharing_channel(
    ConnectionLayer *cl, unsigned localid);
static void ssh2_sharing_queue_global_request(
    ConnectionLayer *cl, ssh_sharing_connstate *share_ctx);
static void ssh2_sharing_no_more_downstreams(ConnectionLayer *cl);
static int ssh2_agent_forwarding_permitted(ConnectionLayer *cl);
static void ssh2_terminal_size(ConnectionLayer *cl, int width, int height);
static void ssh2_stdout_unthrottle(ConnectionLayer *cl, int bufsize);
static int ssh2_stdin_backlog(ConnectionLayer *cl);
static void ssh2_throttle_all_channels(ConnectionLayer *cl, int throttled);
static int ssh2_ldisc_option(ConnectionLayer *cl, int option);

static const struct ConnectionLayerVtable ssh2_connlayer_vtable = {
    ssh2_rportfwd_alloc,
    ssh2_rportfwd_remove,
    ssh2_lportfwd_open,
    ssh2_add_sharing_x11_display,
    ssh2_remove_sharing_x11_display,
    ssh2_send_packet_from_downstream,
    ssh2_alloc_sharing_channel,
    ssh2_delete_sharing_channel,
    ssh2_sharing_queue_global_request,
    ssh2_sharing_no_more_downstreams,
    ssh2_agent_forwarding_permitted,
    ssh2_terminal_size,
    ssh2_stdout_unthrottle,
    ssh2_stdin_backlog,
    ssh2_throttle_all_channels,
    ssh2_ldisc_option,
};

static char *ssh2_channel_open_failure_error_text(PktIn *pktin)
{
    static const char *const reasons[] = {
        NULL,
        "Administratively prohibited",
        "Connect failed",
        "Unknown channel type",
        "Resource shortage",
    };
    unsigned reason_code;
    const char *reason_code_string;
    char reason_code_buf[256];
    ptrlen reason;

    reason_code = get_uint32(pktin);
    if (reason_code < lenof(reasons) && reasons[reason_code]) {
        reason_code_string = reasons[reason_code];
    } else {
        reason_code_string = reason_code_buf;
        sprintf(reason_code_buf, "unknown reason code %#x", reason_code);
    }

    reason = get_string(pktin);

    return dupprintf("%s [%.*s]", reason_code_string, PTRLEN_PRINTF(reason));
}

struct outstanding_channel_request;
struct outstanding_global_request;

struct ssh2_channel {
    struct ssh2_connection_state *connlayer;

    unsigned remoteid, localid;
    int type;
    /* True if we opened this channel but server hasn't confirmed. */
    int halfopen;

    /* Bitmap of whether we've sent/received CHANNEL_EOF and
     * CHANNEL_CLOSE. */
#define CLOSES_SENT_EOF    1
#define CLOSES_SENT_CLOSE  2
#define CLOSES_RCVD_EOF    4
#define CLOSES_RCVD_CLOSE  8
    int closes;

    /*
     * This flag indicates that an EOF is pending on the outgoing side
     * of the channel: that is, wherever we're getting the data for
     * this channel has sent us some data followed by EOF. We can't
     * actually send the EOF until we've finished sending the data, so
     * we set this flag instead to remind us to do so once our buffer
     * is clear.
     */
    int pending_eof;

    /*
     * True if this channel is causing the underlying connection to be
     * throttled.
     */
    int throttling_conn;

    /*
     * True if we currently have backed-up data on the direction of
     * this channel pointing out of the SSH connection, and therefore
     * would prefer the 'Channel' implementation not to read further
     * local input if possible.
     */
    int throttled_by_backlog;

    bufchain outbuffer;
    unsigned remwindow, remmaxpkt;
    /* locwindow is signed so we can cope with excess data. */
    int locwindow, locmaxwin;
    /*
     * remlocwin is the amount of local window that we think
     * the remote end had available to it after it sent the
     * last data packet or window adjust ack.
     */
    int remlocwin;

    /*
     * These store the list of channel requests that we're waiting for
     * replies to. (CHANNEL_FAILURE doesn't come with any indication
     * of what message caused it, so we have to keep track of the
     * queue ourselves.)
     */
    struct outstanding_channel_request *chanreq_head, *chanreq_tail;

    enum { THROTTLED, UNTHROTTLING, UNTHROTTLED } throttle_state;

    ssh_sharing_connstate *sharectx; /* sharing context, if this is a
                                      * downstream channel */
    Channel *chan;      /* handle the client side of this channel, if not */
    SshChannel sc;      /* entry point for chan to talk back to */
};

static int ssh2channel_write(SshChannel *c, const void *buf, int len);
static void ssh2channel_write_eof(SshChannel *c);
static void ssh2channel_unclean_close(SshChannel *c, const char *err);
static void ssh2channel_unthrottle(SshChannel *c, int bufsize);
static Conf *ssh2channel_get_conf(SshChannel *c);
static void ssh2channel_window_override_removed(SshChannel *c);
static void ssh2channel_x11_sharing_handover(
    SshChannel *c, ssh_sharing_connstate *share_cs, share_channel *share_chan,
    const char *peer_addr, int peer_port, int endian,
    int protomajor, int protominor, const void *initial_data, int initial_len);
static void ssh2channel_request_x11_forwarding(
    SshChannel *c, int want_reply, const char *authproto,
    const char *authdata, int screen_number, int oneshot);
static void ssh2channel_request_agent_forwarding(
    SshChannel *c, int want_reply);
static void ssh2channel_request_pty(
    SshChannel *c, int want_reply, Conf *conf, int w, int h);
static int ssh2channel_send_env_var(
    SshChannel *c, int want_reply, const char *var, const char *value);
static void ssh2channel_start_shell(
    SshChannel *c, int want_reply);
static void ssh2channel_start_command(
    SshChannel *c, int want_reply, const char *command);
static int ssh2channel_start_subsystem(
    SshChannel *c, int want_reply, const char *subsystem);
static int ssh2channel_send_env_var(
    SshChannel *c, int want_reply, const char *var, const char *value);
static int ssh2channel_send_serial_break(
    SshChannel *c, int want_reply, int length);
static int ssh2channel_send_signal(
    SshChannel *c, int want_reply, const char *signame);
static void ssh2channel_send_terminal_size_change(
    SshChannel *c, int w, int h);
static void ssh2channel_hint_channel_is_simple(SshChannel *c);

static const struct SshChannelVtable ssh2channel_vtable = {
    ssh2channel_write,
    ssh2channel_write_eof,
    ssh2channel_unclean_close,
    ssh2channel_unthrottle,
    ssh2channel_get_conf,
    ssh2channel_window_override_removed,
    ssh2channel_x11_sharing_handover,
    ssh2channel_request_x11_forwarding,
    ssh2channel_request_agent_forwarding,
    ssh2channel_request_pty,
    ssh2channel_send_env_var,
    ssh2channel_start_shell,
    ssh2channel_start_command,
    ssh2channel_start_subsystem,
    ssh2channel_send_serial_break,
    ssh2channel_send_signal,
    ssh2channel_send_terminal_size_change,
    ssh2channel_hint_channel_is_simple,
};

typedef void (*cr_handler_fn_t)(struct ssh2_channel *, PktIn *, void *);

static void ssh2_channel_init(struct ssh2_channel *c);
static PktOut *ssh2_chanopen_init(struct ssh2_channel *c, const char *type);
static PktOut *ssh2_chanreq_init(struct ssh2_channel *c, const char *type,
                                 cr_handler_fn_t handler, void *ctx);
static void ssh2_channel_check_close(struct ssh2_channel *c);
static void ssh2_channel_try_eof(struct ssh2_channel *c);
static void ssh2_set_window(struct ssh2_channel *c, int newwin);
static int ssh2_try_send(struct ssh2_channel *c);
static void ssh2_try_send_and_unthrottle(struct ssh2_channel *c);
static void ssh2_channel_check_throttle(struct ssh2_channel *c);
static void ssh2_channel_close_local(struct ssh2_channel *c,
                                     const char *reason);
static void ssh2_channel_destroy(struct ssh2_channel *c);

static void ssh2_check_termination(struct ssh2_connection_state *s);

typedef void (*gr_handler_fn_t)(struct ssh2_connection_state *s,
                                PktIn *pktin, void *ctx);
struct outstanding_global_request {
    gr_handler_fn_t handler;
    void *ctx;
    struct outstanding_global_request *next;
};
static void ssh2_queue_global_request_handler(
    struct ssh2_connection_state *s, gr_handler_fn_t handler, void *ctx)
{
    struct outstanding_global_request *ogr =
        snew(struct outstanding_global_request);
    ogr->handler = handler;
    ogr->ctx = ctx;
    if (s->globreq_tail)
        s->globreq_tail->next = ogr;
    else
        s->globreq_head = ogr;
    s->globreq_tail = ogr;
}

static void create_mainchan(struct ssh2_connection_state *s, Conf *conf);

static int ssh2_channelcmp(void *av, void *bv)
{
    const struct ssh2_channel *a = (const struct ssh2_channel *) av;
    const struct ssh2_channel *b = (const struct ssh2_channel *) bv;
    if (a->localid < b->localid)
	return -1;
    if (a->localid > b->localid)
	return +1;
    return 0;
}

static int ssh2_channelfind(void *av, void *bv)
{
    const unsigned *a = (const unsigned *) av;
    const struct ssh2_channel *b = (const struct ssh2_channel *) bv;
    if (*a < b->localid)
	return -1;
    if (*a > b->localid)
	return +1;
    return 0;
}

/*
 * Each channel has a queue of outstanding CHANNEL_REQUESTS and their
 * handlers.
 */
struct outstanding_channel_request {
    cr_handler_fn_t handler;
    void *ctx;
    struct outstanding_channel_request *next;
};

static void ssh2_channel_free(struct ssh2_channel *c)
{
    bufchain_clear(&c->outbuffer);
    while (c->chanreq_head) {
        struct outstanding_channel_request *chanreq = c->chanreq_head;
        c->chanreq_head = c->chanreq_head->next;
        sfree(chanreq);
    }
    if (c->chan)
        chan_free(c->chan);
    sfree(c);
}

PacketProtocolLayer *ssh2_connection_new(
    Ssh *ssh, ssh_sharing_state *connshare, int is_simple,
    Conf *conf, const char *peer_verstring, ConnectionLayer **cl_out)
{
    struct ssh2_connection_state *s = snew(struct ssh2_connection_state);
    memset(s, 0, sizeof(*s));
    s->ppl.vt = &ssh2_connection_vtable;

    s->conf = conf_copy(conf);

    s->ssh_is_simple = is_simple;

    /*
     * If the ssh_no_shell option is enabled, we disable the usual
     * termination check, so that we persist even in the absence of
     * any at all channels (because our purpose is probably to be a
     * background port forwarder).
     */
    s->persistent = conf_get_int(s->conf, CONF_ssh_no_shell);

    s->connshare = connshare;
    s->peer_verstring = dupstr(peer_verstring);

    s->channels = newtree234(ssh2_channelcmp);

    s->x11authtree = newtree234(x11_authcmp);

    /* Need to get the log context for s->cl now, because we won't be
     * helpfully notified when a copy is written into s->ppl by our
     * owner. */
    s->cl.vt = &ssh2_connlayer_vtable;
    s->cl.logctx = ssh_get_logctx(ssh);

    s->portfwdmgr = portfwdmgr_new(&s->cl);
    s->rportfwds = newtree234(ssh2_rportfwd_cmp);

    *cl_out = &s->cl;
    if (s->connshare)
        ssh_connshare_provide_connlayer(s->connshare, &s->cl);

    return &s->ppl;
}

static void ssh2_connection_free(PacketProtocolLayer *ppl)
{
    struct ssh2_connection_state *s =
        container_of(ppl, struct ssh2_connection_state, ppl);
    struct X11FakeAuth *auth;
    struct ssh2_channel *c;
    struct ssh_rportfwd *rpf;

    sfree(s->peer_verstring);

    conf_free(s->conf);

    sfree(s->mainchan_open_error);

    while ((c = delpos234(s->channels, 0)) != NULL)
        ssh2_channel_free(c);
    freetree234(s->channels);

    while ((auth = delpos234(s->x11authtree, 0)) != NULL) {
        if (auth->disp)
            x11_free_display(auth->disp);
        x11_free_fake_auth(auth);
    }
    freetree234(s->x11authtree);

    while ((rpf = delpos234(s->rportfwds, 0)) != NULL)
        free_rportfwd(rpf);
    freetree234(s->rportfwds);
    portfwdmgr_free(s->portfwdmgr);

    sfree(s);
}

static char *chan_open_x11(
    struct ssh2_connection_state *s, Channel **ch, SshChannel *sc,
    ptrlen peeraddr, int peerport);
static char *chan_open_forwarded_tcpip(
    struct ssh2_connection_state *s, Channel **ch, SshChannel *sc,
    ssh_sharing_connstate **share_ctx,
    ptrlen fwdaddr, int fwdport, ptrlen peeraddr, int peerport);
static char *chan_open_auth_agent(
    struct ssh2_connection_state *s, Channel **ch, SshChannel *sc);

static int ssh2_connection_filter_queue(struct ssh2_connection_state *s)
{
    PktIn *pktin;
    PktOut *pktout;
    ptrlen type, data;
    struct ssh2_channel *c;
    ssh_sharing_connstate *share_ctx;
    struct outstanding_channel_request *ocr;
    unsigned localid, remid, winsize, pktsize, ext_type;
    int want_reply, reply_success, expect_halfopen;
    char *error;
    PacketProtocolLayer *ppl = &s->ppl; /* for ppl_logevent */

    /* Cross-reference to ssh2transport.c to handle the common packets
     * between login and connection: DISCONNECT, DEBUG and IGNORE. If
     * we have an instance of ssh2transport below us, then those
     * messages won't come here anyway, but they could if we're
     * running in bare ssh2-connection mode. */
    extern int ssh2_common_filter_queue(PacketProtocolLayer *ppl);

    while (1) {
        if (ssh2_common_filter_queue(&s->ppl))
            return TRUE;
        if ((pktin = pq_peek(s->ppl.in_pq)) == NULL)
            return FALSE;

        switch (pktin->type) {
          case SSH2_MSG_GLOBAL_REQUEST:
            /* type = */ get_string(pktin);
            want_reply = get_bool(pktin);

            reply_success = FALSE;

            /*
             * We currently don't support any incoming global requests
             * at all. Here's where to insert some code to handle
             * them, if and when we do.
             */

            if (want_reply) {
                int type = (reply_success ? SSH2_MSG_REQUEST_SUCCESS :
                            SSH2_MSG_REQUEST_FAILURE);
                pktout = ssh_bpp_new_pktout(s->ppl.bpp, type);
                pq_push(s->ppl.out_pq, pktout);
            }
            pq_pop(s->ppl.in_pq);
            break;

          case SSH2_MSG_REQUEST_SUCCESS:
          case SSH2_MSG_REQUEST_FAILURE:
            if (!s->globreq_head) {
                ssh_proto_error(
                    s->ppl.ssh,
                    "Received %s with no outstanding global request",
                    ssh2_pkt_type(s->ppl.bpp->pls->kctx, s->ppl.bpp->pls->actx,
                                  pktin->type));
                return TRUE;
            }

            s->globreq_head->handler(s, pktin, s->globreq_head->ctx);
            {
                struct outstanding_global_request *tmp = s->globreq_head;
                s->globreq_head = s->globreq_head->next;
                sfree(tmp);
            }

            pq_pop(s->ppl.in_pq);
            break;

          case SSH2_MSG_CHANNEL_OPEN:
            error = NULL;

            type = get_string(pktin);
            c = snew(struct ssh2_channel);
            c->connlayer = s;

            remid = get_uint32(pktin);
            winsize = get_uint32(pktin);
            pktsize = get_uint32(pktin);

            share_ctx = NULL;

            if (ptrlen_eq_string(type, "x11")) {
                ptrlen peeraddr = get_string(pktin);
                int peerport = get_uint32(pktin);

                error = chan_open_x11(
                    s, &c->chan, &c->sc, peeraddr, peerport);
            } else if (ptrlen_eq_string(type, "forwarded-tcpip")) {
                ptrlen fwdaddr = get_string(pktin);
                int fwdport = toint(get_uint32(pktin));
                ptrlen peeraddr = get_string(pktin);
                int peerport = toint(get_uint32(pktin));

                error = chan_open_forwarded_tcpip(
                    s, &c->chan, &c->sc, &share_ctx,
                    fwdaddr, fwdport, peeraddr, peerport);
            } else if (ptrlen_eq_string(type, "auth-agent@openssh.com")) {
                error = chan_open_auth_agent(s, &c->chan, &c->sc);
            } else {
                error = dupstr("Unsupported channel type requested");
            }

            if (share_ctx) {
                /*
                 * This channel-open request needs to go to a
                 * connection-sharing downstream, so abandon our own
                 * channel-open procedure and just pass the message on
                 * to sshshare.c.
                 */
                assert(!error);
                share_got_pkt_from_server(share_ctx, pktin->type,
                                          BinarySource_UPCAST(pktin)->data,
                                          BinarySource_UPCAST(pktin)->len);
                sfree(c);
                break;
            }

            c->remoteid = remid;
            c->halfopen = FALSE;
            if (error) {
                pktout = ssh_bpp_new_pktout(
                    s->ppl.bpp, SSH2_MSG_CHANNEL_OPEN_FAILURE);
                put_uint32(pktout, c->remoteid);
                put_uint32(pktout, SSH2_OPEN_CONNECT_FAILED);
                put_stringz(pktout, error);
                put_stringz(pktout, "en");	/* language tag */
                pq_push(s->ppl.out_pq, pktout);
                ppl_logevent(("Rejected channel open: %s", error));
                sfree(error);
                sfree(c);
            } else {
                ssh2_channel_init(c);
                c->remwindow = winsize;
                c->remmaxpkt = pktsize;
                if (c->chan->initial_fixed_window_size) {
                    c->locwindow = c->locmaxwin = c->remlocwin =
                        c->chan->initial_fixed_window_size;
                }
                pktout = ssh_bpp_new_pktout(
                    s->ppl.bpp, SSH2_MSG_CHANNEL_OPEN_CONFIRMATION);
                put_uint32(pktout, c->remoteid);
                put_uint32(pktout, c->localid);
                put_uint32(pktout, c->locwindow);
                put_uint32(pktout, OUR_V2_MAXPKT); /* our max pkt size */
                pq_push(s->ppl.out_pq, pktout);
            }

            pq_pop(s->ppl.in_pq);
            break;

          case SSH2_MSG_CHANNEL_DATA:
          case SSH2_MSG_CHANNEL_EXTENDED_DATA:
          case SSH2_MSG_CHANNEL_WINDOW_ADJUST:
          case SSH2_MSG_CHANNEL_REQUEST:
          case SSH2_MSG_CHANNEL_EOF:
          case SSH2_MSG_CHANNEL_CLOSE:
          case SSH2_MSG_CHANNEL_OPEN_CONFIRMATION:
          case SSH2_MSG_CHANNEL_OPEN_FAILURE:
          case SSH2_MSG_CHANNEL_SUCCESS:
          case SSH2_MSG_CHANNEL_FAILURE:
            /*
             * Common preliminary code for all the messages from the
             * server that cite one of our channel ids: look up that
             * channel id, check it exists, and if it's for a sharing
             * downstream, pass it on.
             */
            localid = get_uint32(pktin);
            c = find234(s->channels, &localid, ssh2_channelfind);

            if (c && c->sharectx) {
                share_got_pkt_from_server(c->sharectx, pktin->type,
                                          BinarySource_UPCAST(pktin)->data,
                                          BinarySource_UPCAST(pktin)->len);
                pq_pop(s->ppl.in_pq);
                break;
            }

            expect_halfopen = (
                pktin->type == SSH2_MSG_CHANNEL_OPEN_CONFIRMATION ||
                pktin->type == SSH2_MSG_CHANNEL_OPEN_FAILURE);

            if (!c || c->halfopen != expect_halfopen) {
                ssh_proto_error(s->ppl.ssh,
                                "Received %s for %s channel %u",
                                ssh2_pkt_type(s->ppl.bpp->pls->kctx,
                                              s->ppl.bpp->pls->actx,
                                              pktin->type),
                                (!c ? "nonexistent" :
                                 c->halfopen ? "half-open" : "open"),
                                localid);
                return TRUE;
            }
 
            switch (pktin->type) {
              case SSH2_MSG_CHANNEL_OPEN_CONFIRMATION:
                assert(c->halfopen);
                c->remoteid = get_uint32(pktin);
                c->halfopen = FALSE;
                c->remwindow = get_uint32(pktin);
                c->remmaxpkt = get_uint32(pktin);

                chan_open_confirmation(c->chan);

                /*
                 * Now that the channel is fully open, it's possible
                 * in principle to immediately close it. Check whether
                 * it wants us to!
                 *
                 * This can occur if a local socket error occurred
                 * between us sending out CHANNEL_OPEN and receiving
                 * OPEN_CONFIRMATION. If that happens, all we can do
                 * is immediately initiate close proceedings now that
                 * we know the server's id to put in the close
                 * message. We'll have handled that in this code by
                 * having already turned c->chan into a zombie, so its
                 * want_close method (which ssh2_channel_check_close
                 * will consult) will already be returning TRUE.
                 */
                ssh2_channel_check_close(c);

                if (c->pending_eof)
                    ssh2_channel_try_eof(c); /* in case we had a pending EOF */
                break;

              case SSH2_MSG_CHANNEL_OPEN_FAILURE:
                assert(c->halfopen);

                {
                    char *err = ssh2_channel_open_failure_error_text(pktin);
                    chan_open_failed(c->chan, err);
                    sfree(err);
                }
                chan_free(c->chan);

                del234(s->channels, c);
                ssh2_channel_free(c);

                break;

              case SSH2_MSG_CHANNEL_DATA:
              case SSH2_MSG_CHANNEL_EXTENDED_DATA:
                ext_type = (pktin->type == SSH2_MSG_CHANNEL_DATA ? 0 :
                            get_uint32(pktin));
                data = get_string(pktin);
                if (!get_err(pktin)) {
                    int bufsize;
                    c->locwindow -= data.len;
                    c->remlocwin -= data.len;
                    if (ext_type != 0 && ext_type != SSH2_EXTENDED_DATA_STDERR)
                        data.len = 0; /* ignore unknown extended data */
                    bufsize = chan_send(
                        c->chan, ext_type == SSH2_EXTENDED_DATA_STDERR,
                        data.ptr, data.len);

                    /*
                     * If it looks like the remote end hit the end of
                     * its window, and we didn't want it to do that,
                     * think about using a larger window.
                     */
                    if (c->remlocwin <= 0 &&
                        c->throttle_state == UNTHROTTLED &&
                        c->locmaxwin < 0x40000000)
                        c->locmaxwin += OUR_V2_WINSIZE;

                    /*
                     * If we are not buffering too much data, enlarge
                     * the window again at the remote side. If we are
                     * buffering too much, we may still need to adjust
                     * the window if the server's sent excess data.
                     */
                    if (bufsize < c->locmaxwin)
                        ssh2_set_window(c, c->locmaxwin - bufsize);

                    /*
                     * If we're either buffering way too much data, or
                     * if we're buffering anything at all and we're in
                     * "simple" mode, throttle the whole channel.
                     */
                    if ((bufsize > c->locmaxwin ||
                         (s->ssh_is_simple && bufsize>0)) &&
                        !c->throttling_conn) {
                        c->throttling_conn = TRUE;
                        ssh_throttle_conn(s->ppl.ssh, +1);
                    }
                }
                break;

              case SSH2_MSG_CHANNEL_WINDOW_ADJUST:
                if (!(c->closes & CLOSES_SENT_EOF)) {
                    c->remwindow += get_uint32(pktin);
                    ssh2_try_send_and_unthrottle(c);
                }
                break;

              case SSH2_MSG_CHANNEL_REQUEST:
                type = get_string(pktin);
                want_reply = get_bool(pktin);

                reply_success = FALSE;

                if (c->closes & CLOSES_SENT_CLOSE) {
                    /*
                     * We don't reply to channel requests after we've
                     * sent CHANNEL_CLOSE for the channel, because our
                     * reply might cross in the network with the other
                     * side's CHANNEL_CLOSE and arrive after they have
                     * wound the channel up completely.
                     */
                    want_reply = FALSE;
                }

                /*
                 * Try every channel request name we recognise, no
                 * matter what the channel, and see if the Channel
                 * instance will accept it.
                 */
                if (ptrlen_eq_string(type, "exit-status")) {
                    int exitcode = toint(get_uint32(pktin));
                    reply_success = chan_rcvd_exit_status(c->chan, exitcode);
                } else if (ptrlen_eq_string(type, "exit-signal")) {
                    ptrlen signame;
                    int signum;
                    int core = FALSE;
                    ptrlen errmsg;
                    int format;

                    /*
                     * ICK: older versions of OpenSSH (e.g. 3.4p1)
                     * provide an `int' for the signal, despite its
                     * having been a `string' in the drafts of RFC
                     * 4254 since at least 2001. (Fixed in session.c
                     * 1.147.) Try to infer which we can safely parse
                     * it as.
                     */

                    size_t startpos = BinarySource_UPCAST(pktin)->pos;

                    for (format = 0; format < 2; format++) {
                        BinarySource_UPCAST(pktin)->pos = startpos;
                        BinarySource_UPCAST(pktin)->err = BSE_NO_ERROR;

                        /* placate compiler warnings about unin */
                        signame = make_ptrlen(NULL, 0);
                        signum = 0;

                        if (format == 0) /* standard string-based format */
                            signame = get_string(pktin);
                        else      /* nonstandard integer format */
                            signum = toint(get_uint32(pktin));

                        core = get_bool(pktin);
                        errmsg = get_string(pktin); /* error message */
                        get_string(pktin);     /* language tag */

                        if (!get_err(pktin) && get_avail(pktin) == 0)
                            break;             /* successful parse */
                    }

                    switch (format) {
                      case 0:
                        reply_success = chan_rcvd_exit_signal(
                            c->chan, signame, core, errmsg);
                        break;
                      case 1:
                        reply_success = chan_rcvd_exit_signal_numeric(
                            c->chan, signum, core, errmsg);
                        break;
                      default:
                        /* Couldn't parse this message in either format */
                        reply_success = FALSE;
                        break;
                    }
                }
                if (want_reply) {
                    int type = (reply_success ? SSH2_MSG_CHANNEL_SUCCESS :
                                SSH2_MSG_CHANNEL_FAILURE);
                    pktout = ssh_bpp_new_pktout(s->ppl.bpp, type);
                    put_uint32(pktout, c->remoteid);
                    pq_push(s->ppl.out_pq, pktout);
                }
                break;

              case SSH2_MSG_CHANNEL_SUCCESS:
              case SSH2_MSG_CHANNEL_FAILURE:
                ocr = c->chanreq_head;
                if (!ocr) {
                    ssh_proto_error(
                        s->ppl.ssh,
                        "Received %s for channel %d with no outstanding "
                        "channel request",
                        ssh2_pkt_type(s->ppl.bpp->pls->kctx,
                                      s->ppl.bpp->pls->actx, pktin->type));
                    return TRUE;
                }
                ocr->handler(c, pktin, ocr->ctx);
                c->chanreq_head = ocr->next;
                sfree(ocr);
                /*
                 * We may now initiate channel-closing procedures, if
                 * that CHANNEL_REQUEST was the last thing outstanding
                 * before we send CHANNEL_CLOSE.
                 */
                ssh2_channel_check_close(c);
                break;

              case SSH2_MSG_CHANNEL_EOF:
                if (!(c->closes & CLOSES_RCVD_EOF)) {
                    c->closes |= CLOSES_RCVD_EOF;
                    chan_send_eof(c->chan);
                    ssh2_channel_check_close(c);
                }
                break;

              case SSH2_MSG_CHANNEL_CLOSE:
                /*
                 * When we receive CLOSE on a channel, we assume it
                 * comes with an implied EOF if we haven't seen EOF
                 * yet.
                 */
                if (!(c->closes & CLOSES_RCVD_EOF)) {
                    c->closes |= CLOSES_RCVD_EOF;
                    chan_send_eof(c->chan);
                }

                if (!(s->ppl.remote_bugs & BUG_SENDS_LATE_REQUEST_REPLY)) {
                    /*
                     * It also means we stop expecting to see replies
                     * to any outstanding channel requests, so clean
                     * those up too. (ssh_chanreq_init will enforce by
                     * assertion that we don't subsequently put
                     * anything back on this list.)
                     */
                    while (c->chanreq_head) {
                        struct outstanding_channel_request *ocr =
                            c->chanreq_head;
                        ocr->handler(c, NULL, ocr->ctx);
                        c->chanreq_head = ocr->next;
                        sfree(ocr);
                    }
                }

                /*
                 * And we also send an outgoing EOF, if we haven't
                 * already, on the assumption that CLOSE is a pretty
                 * forceful announcement that the remote side is doing
                 * away with the entire channel. (If it had wanted to
                 * send us EOF and continue receiving data from us, it
                 * would have just sent CHANNEL_EOF.)
                 */
                if (!(c->closes & CLOSES_SENT_EOF)) {
                    /*
                     * Abandon any buffered data we still wanted to
                     * send to this channel. Receiving a CHANNEL_CLOSE
                     * is an indication that the server really wants
                     * to get on and _destroy_ this channel, and it
                     * isn't going to send us any further
                     * WINDOW_ADJUSTs to permit us to send pending
                     * stuff.
                     */
                    bufchain_clear(&c->outbuffer);

                    /*
                     * Send outgoing EOF.
                     */
                    sshfwd_write_eof(&c->sc);

                    /*
                     * Make sure we don't read any more from whatever
                     * our local data source is for this channel.
                     * (This will pick up on the changes made by
                     * sshfwd_write_eof.)
                     */
                    ssh2_channel_check_throttle(c);
                }

                /*
                 * Now process the actual close.
                 */
                if (!(c->closes & CLOSES_RCVD_CLOSE)) {
                    c->closes |= CLOSES_RCVD_CLOSE;
                    ssh2_channel_check_close(c);
                }

                break;
            }

            pq_pop(s->ppl.in_pq);
            break;

          default:
            return FALSE;
        }
    }
}

static void ssh2_handle_winadj_response(struct ssh2_channel *c,
					PktIn *pktin, void *ctx)
{
    unsigned *sizep = ctx;

    /*
     * Winadj responses should always be failures. However, at least
     * one server ("boks_sshd") is known to return SUCCESS for channel
     * requests it's never heard of, such as "winadj@putty". Raised
     * with foxt.com as bug 090916-090424, but for the sake of a quiet
     * life, we don't worry about what kind of response we got.
     */

    c->remlocwin += *sizep;
    sfree(sizep);
    /*
     * winadj messages are only sent when the window is fully open, so
     * if we get an ack of one, we know any pending unthrottle is
     * complete.
     */
    if (c->throttle_state == UNTHROTTLING)
	c->throttle_state = UNTHROTTLED;
}

static void ssh2_set_window(struct ssh2_channel *c, int newwin)
{
    struct ssh2_connection_state *s = c->connlayer;

    /*
     * Never send WINDOW_ADJUST for a channel that the remote side has
     * already sent EOF on; there's no point, since it won't be
     * sending any more data anyway. Ditto if _we've_ already sent
     * CLOSE.
     */
    if (c->closes & (CLOSES_RCVD_EOF | CLOSES_SENT_CLOSE))
	return;

    /*
     * If the client-side Channel is in an initial setup phase with a
     * fixed window size, e.g. for an X11 channel when we're still
     * waiting to see its initial auth and may yet hand it off to a
     * downstream, don't send any WINDOW_ADJUST either.
     */
    if (c->chan->initial_fixed_window_size)
        return;

    /*
     * If the remote end has a habit of ignoring maxpkt, limit the
     * window so that it has no choice (assuming it doesn't ignore the
     * window as well).
     */
    if ((s->ppl.remote_bugs & BUG_SSH2_MAXPKT) && newwin > OUR_V2_MAXPKT)
	newwin = OUR_V2_MAXPKT;

    /*
     * Only send a WINDOW_ADJUST if there's significantly more window
     * available than the other end thinks there is.  This saves us
     * sending a WINDOW_ADJUST for every character in a shell session.
     *
     * "Significant" is arbitrarily defined as half the window size.
     */
    if (newwin / 2 >= c->locwindow) {
	PktOut *pktout;
	unsigned *up;

	/*
	 * In order to keep track of how much window the client
	 * actually has available, we'd like it to acknowledge each
	 * WINDOW_ADJUST.  We can't do that directly, so we accompany
	 * it with a CHANNEL_REQUEST that has to be acknowledged.
	 *
	 * This is only necessary if we're opening the window wide.
	 * If we're not, then throughput is being constrained by
	 * something other than the maximum window size anyway.
	 */
	if (newwin == c->locmaxwin &&
            !(s->ppl.remote_bugs & BUG_CHOKES_ON_WINADJ)) {
	    up = snew(unsigned);
	    *up = newwin - c->locwindow;
	    pktout = ssh2_chanreq_init(c, "winadj@putty.projects.tartarus.org",
				       ssh2_handle_winadj_response, up);
	    pq_push(s->ppl.out_pq, pktout);

	    if (c->throttle_state != UNTHROTTLED)
		c->throttle_state = UNTHROTTLING;
	} else {
	    /* Pretend the WINDOW_ADJUST was acked immediately. */
	    c->remlocwin = newwin;
	    c->throttle_state = THROTTLED;
	}
	pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH2_MSG_CHANNEL_WINDOW_ADJUST);
	put_uint32(pktout, c->remoteid);
	put_uint32(pktout, newwin - c->locwindow);
	pq_push(s->ppl.out_pq, pktout);
	c->locwindow = newwin;
    }
}

static PktIn *ssh2_connection_pop(struct ssh2_connection_state *s)
{
    ssh2_connection_filter_queue(s);
    return pq_pop(s->ppl.in_pq);
}

static void ssh2_connection_process_queue(PacketProtocolLayer *ppl)
{
    struct ssh2_connection_state *s =
        container_of(ppl, struct ssh2_connection_state, ppl);
    PktIn *pktin;

    if (ssh2_connection_filter_queue(s)) /* no matter why we were called */
        return;

    crBegin(s->crState);

    if (s->connshare)
        share_activate(s->connshare, s->peer_verstring);

    /*
     * Enable port forwardings.
     */
    portfwdmgr_config(s->portfwdmgr, s->conf);
    s->portfwdmgr_configured = TRUE;

    /*
     * Create the main session channel, if any.
     */
    create_mainchan(s, s->conf);

    /*
     * Transfer data!
     */

    while (1) {
	if ((pktin = ssh2_connection_pop(s)) != NULL) {

	    /*
	     * _All_ the connection-layer packets we expect to
	     * receive are now handled by the dispatch table.
	     * Anything that reaches here must be bogus.
	     */

            ssh_proto_error(s->ppl.ssh, "Received unexpected connection-layer "
                            "packet, type %d (%s)", pktin->type,
                            ssh2_pkt_type(s->ppl.bpp->pls->kctx,
                                          s->ppl.bpp->pls->actx,
                                          pktin->type));
            return;
	}
	crReturnV;
    }

    crFinishV;
}

static void ssh2_channel_check_close(struct ssh2_channel *c)
{
    struct ssh2_connection_state *s = c->connlayer;
    PktOut *pktout;

    if (c->halfopen) {
        /*
         * If we've sent out our own CHANNEL_OPEN but not yet seen
         * either OPEN_CONFIRMATION or OPEN_FAILURE in response, then
         * it's too early to be sending close messages of any kind.
         */
        return;
    }

    if ((!((CLOSES_SENT_EOF | CLOSES_RCVD_EOF) & ~c->closes) ||
         chan_want_close(c->chan, (c->closes & CLOSES_SENT_EOF),
                         (c->closes & CLOSES_RCVD_EOF))) &&
	!c->chanreq_head &&
	!(c->closes & CLOSES_SENT_CLOSE)) {
        /*
         * We have both sent and received EOF (or the channel is a
         * zombie), and we have no outstanding channel requests, which
         * means the channel is in final wind-up. But we haven't sent
         * CLOSE, so let's do so now.
         */
	pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH2_MSG_CHANNEL_CLOSE);
	put_uint32(pktout, c->remoteid);
	pq_push(s->ppl.out_pq, pktout);
        c->closes |= CLOSES_SENT_EOF | CLOSES_SENT_CLOSE;
    }

    if (!((CLOSES_SENT_CLOSE | CLOSES_RCVD_CLOSE) & ~c->closes)) {
	assert(c->chanreq_head == NULL);
        /*
         * We have both sent and received CLOSE, which means we're
         * completely done with the channel.
         */
        ssh2_channel_destroy(c);
    }
}

static void ssh2_channel_try_eof(struct ssh2_channel *c)
{
    struct ssh2_connection_state *s = c->connlayer;
    PktOut *pktout;
    assert(c->pending_eof);          /* precondition for calling us */
    if (c->halfopen)
        return;                 /* can't close: not even opened yet */
    if (bufchain_size(&c->outbuffer) > 0)
        return;              /* can't send EOF: pending outgoing data */

    c->pending_eof = FALSE;            /* we're about to send it */

    pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH2_MSG_CHANNEL_EOF);
    put_uint32(pktout, c->remoteid);
    pq_push(s->ppl.out_pq, pktout);
    c->closes |= CLOSES_SENT_EOF;
    ssh2_channel_check_close(c);
}

/*
 * Attempt to send data on an SSH-2 channel.
 */
static int ssh2_try_send(struct ssh2_channel *c)
{
    struct ssh2_connection_state *s = c->connlayer;
    PktOut *pktout;
    int bufsize;

    while (c->remwindow > 0 && bufchain_size(&c->outbuffer) > 0) {
	int len;
	void *data;
	bufchain_prefix(&c->outbuffer, &data, &len);
	if ((unsigned)len > c->remwindow)
	    len = c->remwindow;
	if ((unsigned)len > c->remmaxpkt)
	    len = c->remmaxpkt;
	pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH2_MSG_CHANNEL_DATA);
	put_uint32(pktout, c->remoteid);
        put_string(pktout, data, len);
        pq_push(s->ppl.out_pq, pktout);
	bufchain_consume(&c->outbuffer, len);
	c->remwindow -= len;
    }

    /*
     * After having sent as much data as we can, return the amount
     * still buffered.
     */
    bufsize = bufchain_size(&c->outbuffer);

    /*
     * And if there's no data pending but we need to send an EOF, send
     * it.
     */
    if (!bufsize && c->pending_eof)
        ssh2_channel_try_eof(c);

    return bufsize;
}

static void ssh2_try_send_and_unthrottle(struct ssh2_channel *c)
{
    int bufsize;
    if (c->closes & CLOSES_SENT_EOF)
	return;                   /* don't send on channels we've EOFed */
    bufsize = ssh2_try_send(c);
    if (bufsize == 0) {
        c->throttled_by_backlog = FALSE;
        ssh2_channel_check_throttle(c);
    }
}

static void ssh2_channel_check_throttle(struct ssh2_channel *c)
{
    /*
     * We don't want this channel to read further input if this
     * particular channel has a backed-up SSH window, or if the
     * outgoing side of the whole SSH connection is currently
     * throttled, or if this channel already has an outgoing EOF
     * either sent or pending.
     */
    chan_set_input_wanted(c->chan,
                          !c->throttled_by_backlog &&
                          !c->connlayer->all_channels_throttled &&
                          !c->pending_eof &&
                          !(c->closes & CLOSES_SENT_EOF));
}

/*
 * Close any local socket and free any local resources associated with
 * a channel.  This converts the channel into a zombie.
 */
static void ssh2_channel_close_local(struct ssh2_channel *c,
                                     const char *reason)
{
    struct ssh2_connection_state *s = c->connlayer;
    PacketProtocolLayer *ppl = &s->ppl; /* for ppl_logevent */
    char *msg = NULL;

    if (c->sharectx)
        return;

    msg = chan_log_close_msg(c->chan);

    if (msg)
        ppl_logevent(("%s%s%s", msg, reason ? " " : "", reason ? reason : ""));

    sfree(msg);

    chan_free(c->chan);
    c->chan = zombiechan_new();
}

static void ssh2_check_termination_callback(void *vctx)
{
    struct ssh2_connection_state *s = (struct ssh2_connection_state *)vctx;
    ssh2_check_termination(s);
}

static void ssh2_channel_destroy(struct ssh2_channel *c)
{
    struct ssh2_connection_state *s = c->connlayer;

    assert(c->chanreq_head == NULL);

    ssh2_channel_close_local(c, NULL);
    del234(s->channels, c);
    ssh2_channel_free(c);

    /*
     * If that was the last channel left open, we might need to
     * terminate. But we'll be a bit cautious, by doing that in a
     * toplevel callback, just in case anything on the current call
     * stack objects to this entire PPL being freed.
     */
    queue_toplevel_callback(ssh2_check_termination_callback, s);
}

static void ssh2_check_termination(struct ssh2_connection_state *s)
{
    /*
     * Decide whether we should terminate the SSH connection now.
     * Called after a channel or a downstream goes away. The general
     * policy is that we terminate when none of either is left.
     */

    if (s->persistent)
        return;     /* persistent mode: never proactively terminate */

    if (count234(s->channels) == 0 &&
        !(s->connshare && share_ndownstreams(s->connshare) > 0)) {
        /*
         * We used to send SSH_MSG_DISCONNECT here, because I'd
         * believed that _every_ conforming SSH-2 connection had to
         * end with a disconnect being sent by at least one side;
         * apparently I was wrong and it's perfectly OK to
         * unceremoniously slam the connection shut when you're done,
         * and indeed OpenSSH feels this is more polite than sending a
         * DISCONNECT. So now we don't.
         */
        ssh_user_close(s->ppl.ssh, "All channels closed");
        return;
    }
}

/*
 * Set up most of a new ssh2_channel. Nulls out sharectx, but leaves
 * chan untouched (since it will sometimes have been filled in before
 * calling this).
 */
static void ssh2_channel_init(struct ssh2_channel *c)
{
    struct ssh2_connection_state *s = c->connlayer;
    c->closes = 0;
    c->pending_eof = FALSE;
    c->throttling_conn = FALSE;
    c->sharectx = NULL;
    c->locwindow = c->locmaxwin = c->remlocwin =
        s->ssh_is_simple ? OUR_V2_BIGWIN : OUR_V2_WINSIZE;
    c->chanreq_head = NULL;
    c->throttle_state = UNTHROTTLED;
    bufchain_init(&c->outbuffer);
    c->sc.vt = &ssh2channel_vtable;
    c->localid = alloc_channel_id(s->channels, struct ssh2_channel);
    add234(s->channels, c);
}

/*
 * Construct the common parts of a CHANNEL_OPEN.
 */
static PktOut *ssh2_chanopen_init(struct ssh2_channel *c, const char *type)
{
    struct ssh2_connection_state *s = c->connlayer;
    PktOut *pktout;

    pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH2_MSG_CHANNEL_OPEN);
    put_stringz(pktout, type);
    put_uint32(pktout, c->localid);
    put_uint32(pktout, c->locwindow);     /* our window size */
    put_uint32(pktout, OUR_V2_MAXPKT);    /* our max pkt size */
    return pktout;
}

/*
 * Construct the common parts of a CHANNEL_REQUEST.  If handler is not
 * NULL then a reply will be requested and the handler will be called
 * when it arrives.  The returned packet is ready to have any
 * request-specific data added and be sent.  Note that if a handler is
 * provided, it's essential that the request actually be sent.
 *
 * The handler will usually be passed the response packet in pktin. If
 * pktin is NULL, this means that no reply will ever be forthcoming
 * (e.g. because the entire connection is being destroyed, or because
 * the server initiated channel closure before we saw the response)
 * and the handler should free any storage it's holding.
 */
static PktOut *ssh2_chanreq_init(struct ssh2_channel *c, const char *type,
                                 cr_handler_fn_t handler, void *ctx)
{
    struct ssh2_connection_state *s = c->connlayer;
    PktOut *pktout;

    assert(!(c->closes & (CLOSES_SENT_CLOSE | CLOSES_RCVD_CLOSE)));
    pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH2_MSG_CHANNEL_REQUEST);
    put_uint32(pktout, c->remoteid);
    put_stringz(pktout, type);
    put_bool(pktout, handler != NULL);
    if (handler != NULL) {
        struct outstanding_channel_request *ocr =
            snew(struct outstanding_channel_request);

        ocr->handler = handler;
        ocr->ctx = ctx;
        ocr->next = NULL;
        if (!c->chanreq_head)
            c->chanreq_head = ocr;
        else
            c->chanreq_tail->next = ocr;
        c->chanreq_tail = ocr;
    }
    return pktout;
}

static Conf *ssh2channel_get_conf(SshChannel *sc)
{
    struct ssh2_channel *c = container_of(sc, struct ssh2_channel, sc);
    struct ssh2_connection_state *s = c->connlayer;
    return s->conf;
}

static void ssh2channel_write_eof(SshChannel *sc)
{
    struct ssh2_channel *c = container_of(sc, struct ssh2_channel, sc);

    if (c->closes & CLOSES_SENT_EOF)
        return;

    c->pending_eof = TRUE;
    ssh2_channel_try_eof(c);
}

static void ssh2channel_unclean_close(SshChannel *sc, const char *err)
{
    struct ssh2_channel *c = container_of(sc, struct ssh2_channel, sc);
    char *reason;

    reason = dupprintf("due to local error: %s", err);
    ssh2_channel_close_local(c, reason);
    sfree(reason);
    c->pending_eof = FALSE;   /* this will confuse a zombie channel */

    ssh2_channel_check_close(c);
}

static void ssh2channel_unthrottle(SshChannel *sc, int bufsize)
{
    struct ssh2_channel *c = container_of(sc, struct ssh2_channel, sc);
    struct ssh2_connection_state *s = c->connlayer;
    int buflimit;

    buflimit = s->ssh_is_simple ? 0 : c->locmaxwin;
    if (bufsize < buflimit)
        ssh2_set_window(c, buflimit - bufsize);

    if (c->throttling_conn && bufsize <= buflimit) {
	c->throttling_conn = 0;
	ssh_throttle_conn(s->ppl.ssh, -1);
    }
}

static int ssh2channel_write(SshChannel *sc, const void *buf, int len)
{
    struct ssh2_channel *c = container_of(sc, struct ssh2_channel, sc);
    assert(!(c->closes & CLOSES_SENT_EOF));
    bufchain_add(&c->outbuffer, buf, len);
    return ssh2_try_send(c);
}

static void ssh2channel_x11_sharing_handover(
    SshChannel *sc, ssh_sharing_connstate *share_cs, share_channel *share_chan,
    const char *peer_addr, int peer_port, int endian,
    int protomajor, int protominor, const void *initial_data, int initial_len)
{
    struct ssh2_channel *c = container_of(sc, struct ssh2_channel, sc);
    /*
     * This function is called when we've just discovered that an X
     * forwarding channel on which we'd been handling the initial auth
     * ourselves turns out to be destined for a connection-sharing
     * downstream. So we turn the channel into a sharing one, meaning
     * that we completely stop tracking windows and buffering data and
     * just pass more or less unmodified SSH messages back and forth.
     */
    c->sharectx = share_cs;
    share_setup_x11_channel(share_cs, share_chan,
                            c->localid, c->remoteid, c->remwindow,
                            c->remmaxpkt, c->locwindow,
                            peer_addr, peer_port, endian,
                            protomajor, protominor,
                            initial_data, initial_len);
    chan_free(c->chan);
    c->chan = NULL;
}

static void ssh2channel_window_override_removed(SshChannel *sc)
{
    struct ssh2_channel *c = container_of(sc, struct ssh2_channel, sc);
    struct ssh2_connection_state *s = c->connlayer;

    /*
     * This function is called when a client-side Channel has just
     * stopped requiring an initial fixed-size window.
     */
    assert(!c->chan->initial_fixed_window_size);
    ssh2_set_window(c, s->ssh_is_simple ? OUR_V2_BIGWIN : OUR_V2_WINSIZE);
}

static void ssh2_channel_response(
    struct ssh2_channel *c, PktIn *pkt, void *ctx)
{
    chan_request_response(c->chan, pkt->type == SSH2_MSG_CHANNEL_SUCCESS);
}

static void ssh2channel_start_shell(
    SshChannel *sc, int want_reply)
{
    struct ssh2_channel *c = container_of(sc, struct ssh2_channel, sc);
    struct ssh2_connection_state *s = c->connlayer;

    PktOut *pktout = ssh2_chanreq_init(
        c, "shell", want_reply ? ssh2_channel_response : NULL, NULL);
    pq_push(s->ppl.out_pq, pktout);
}

static void ssh2channel_start_command(
    SshChannel *sc, int want_reply, const char *command)
{
    struct ssh2_channel *c = container_of(sc, struct ssh2_channel, sc);
    struct ssh2_connection_state *s = c->connlayer;

    PktOut *pktout = ssh2_chanreq_init(
        c, "exec", want_reply ? ssh2_channel_response : NULL, NULL);
    put_stringz(pktout, command);
    pq_push(s->ppl.out_pq, pktout);
}

static int ssh2channel_start_subsystem(
    SshChannel *sc, int want_reply, const char *subsystem)
{
    struct ssh2_channel *c = container_of(sc, struct ssh2_channel, sc);
    struct ssh2_connection_state *s = c->connlayer;

    PktOut *pktout = ssh2_chanreq_init(
        c, "subsystem", want_reply ? ssh2_channel_response : NULL, NULL);
    put_stringz(pktout, subsystem);
    pq_push(s->ppl.out_pq, pktout);

    return TRUE;
}

static void ssh2channel_request_x11_forwarding(
    SshChannel *sc, int want_reply, const char *authproto,
    const char *authdata, int screen_number, int oneshot)
{
    struct ssh2_channel *c = container_of(sc, struct ssh2_channel, sc);
    struct ssh2_connection_state *s = c->connlayer;

    PktOut *pktout = ssh2_chanreq_init(
        c, "x11-req", want_reply ? ssh2_channel_response : NULL, NULL);
    put_bool(pktout, oneshot);
    put_stringz(pktout, authproto);
    put_stringz(pktout, authdata);
    put_uint32(pktout, screen_number);
    pq_push(s->ppl.out_pq, pktout);
}

static void ssh2channel_request_agent_forwarding(
    SshChannel *sc, int want_reply)
{
    struct ssh2_channel *c = container_of(sc, struct ssh2_channel, sc);
    struct ssh2_connection_state *s = c->connlayer;

    PktOut *pktout = ssh2_chanreq_init(
        c, "auth-agent-req@openssh.com",
        want_reply ? ssh2_channel_response : NULL, NULL);
    pq_push(s->ppl.out_pq, pktout);
}

static void ssh2channel_request_pty(
    SshChannel *sc, int want_reply, Conf *conf, int w, int h)
{
    struct ssh2_channel *c = container_of(sc, struct ssh2_channel, sc);
    struct ssh2_connection_state *s = c->connlayer;
    int ospeed, ispeed;
    strbuf *modebuf;

    ospeed = ispeed = 38400;           /* last-resort defaults */
    sscanf(conf_get_str(conf, CONF_termspeed), "%d,%d",
           &ospeed, &ispeed);

    PktOut *pktout = ssh2_chanreq_init(
        c, "pty-req", want_reply ? ssh2_channel_response : NULL, NULL);
    put_stringz(pktout, conf_get_str(conf, CONF_termtype));
    put_uint32(pktout, w);
    put_uint32(pktout, h);
    put_uint32(pktout, 0);	       /* pixel width */
    put_uint32(pktout, 0);	       /* pixel height */
    modebuf = strbuf_new();
    write_ttymodes_to_packet_from_conf(
        BinarySink_UPCAST(modebuf), s->ppl.seat, conf,
        2, ospeed, ispeed);
    put_stringsb(pktout, modebuf);
    pq_push(s->ppl.out_pq, pktout);
}

static int ssh2channel_send_env_var(
    SshChannel *sc, int want_reply, const char *var, const char *value)
{
    struct ssh2_channel *c = container_of(sc, struct ssh2_channel, sc);
    struct ssh2_connection_state *s = c->connlayer;

    PktOut *pktout = ssh2_chanreq_init(
        c, "env", want_reply ? ssh2_channel_response : NULL, NULL);
    put_stringz(pktout, var);
    put_stringz(pktout, value);
    pq_push(s->ppl.out_pq, pktout);

    return TRUE;
}

static int ssh2channel_send_serial_break(
    SshChannel *sc, int want_reply, int length)
{
    struct ssh2_channel *c = container_of(sc, struct ssh2_channel, sc);
    struct ssh2_connection_state *s = c->connlayer;

    PktOut *pktout = ssh2_chanreq_init(
        c, "break", want_reply ? ssh2_channel_response : NULL, NULL);
    put_uint32(pktout, length);
    pq_push(s->ppl.out_pq, pktout);

    return TRUE;
}

static int ssh2channel_send_signal(
    SshChannel *sc, int want_reply, const char *signame)
{
    struct ssh2_channel *c = container_of(sc, struct ssh2_channel, sc);
    struct ssh2_connection_state *s = c->connlayer;

    PktOut *pktout = ssh2_chanreq_init(
        c, "signal", want_reply ? ssh2_channel_response : NULL, NULL);
    put_stringz(pktout, signame);
    pq_push(s->ppl.out_pq, pktout);

    return TRUE;
}

static void ssh2channel_send_terminal_size_change(SshChannel *sc, int w, int h)
{
    struct ssh2_channel *c = container_of(sc, struct ssh2_channel, sc);
    struct ssh2_connection_state *s = c->connlayer;

    PktOut *pktout = ssh2_chanreq_init(c, "window-change", NULL, NULL);
    put_uint32(pktout, w);
    put_uint32(pktout, h);
    put_uint32(pktout, 0);	       /* pixel width */
    put_uint32(pktout, 0);	       /* pixel height */
    pq_push(s->ppl.out_pq, pktout);
}

static void ssh2channel_hint_channel_is_simple(SshChannel *sc)
{
    struct ssh2_channel *c = container_of(sc, struct ssh2_channel, sc);
    struct ssh2_connection_state *s = c->connlayer;

    PktOut *pktout = ssh2_chanreq_init(
        c, "simple@putty.projects.tartarus.org", NULL, NULL);
    pq_push(s->ppl.out_pq, pktout);
}

static SshChannel *ssh2_lportfwd_open(
    ConnectionLayer *cl, const char *hostname, int port,
    const char *org, Channel *chan)
{
    struct ssh2_connection_state *s =
        container_of(cl, struct ssh2_connection_state, cl);
    PacketProtocolLayer *ppl = &s->ppl; /* for ppl_logevent */
    struct ssh2_channel *c = snew(struct ssh2_channel);
    PktOut *pktout;

    c->connlayer = s;
    ssh2_channel_init(c);
    c->halfopen = TRUE;
    c->chan = chan;

    ppl_logevent(("Opening connection to %s:%d for %s", hostname, port, org));

    pktout = ssh2_chanopen_init(c, "direct-tcpip");
    {
        char *trimmed_host = host_strduptrim(hostname);
        put_stringz(pktout, trimmed_host);
        sfree(trimmed_host);
    }
    put_uint32(pktout, port);
    /*
     * We make up values for the originator data; partly it's too much
     * hassle to keep track, and partly I'm not convinced the server
     * should be told details like that about my local network
     * configuration. The "originator IP address" is syntactically a
     * numeric IP address, and some servers (e.g., Tectia) get upset
     * if it doesn't match this syntax.
     */
    put_stringz(pktout, "0.0.0.0");
    put_uint32(pktout, 0);
    pq_push(s->ppl.out_pq, pktout);

    return &c->sc;
}

static void ssh2_rportfwd_globreq_response(struct ssh2_connection_state *s,
                                           PktIn *pktin, void *ctx)
{
    PacketProtocolLayer *ppl = &s->ppl; /* for ppl_logevent */
    struct ssh_rportfwd *rpf = (struct ssh_rportfwd *)ctx;

    if (pktin->type == SSH2_MSG_REQUEST_SUCCESS) {
	ppl_logevent(("Remote port forwarding from %s enabled",
                      rpf->log_description));
    } else {
	ppl_logevent(("Remote port forwarding from %s refused",
                      rpf->log_description));

	struct ssh_rportfwd *realpf = del234(s->rportfwds, rpf);
	assert(realpf == rpf);
        portfwdmgr_close(s->portfwdmgr, rpf->pfr);
	free_rportfwd(rpf);
    }
}

static struct ssh_rportfwd *ssh2_rportfwd_alloc(
    ConnectionLayer *cl,
    const char *shost, int sport, const char *dhost, int dport,
    int addressfamily, const char *log_description, PortFwdRecord *pfr,
    ssh_sharing_connstate *share_ctx)
{
    struct ssh2_connection_state *s =
        container_of(cl, struct ssh2_connection_state, cl);
    struct ssh_rportfwd *rpf = snew(struct ssh_rportfwd);

    rpf->shost = dupstr(shost);
    rpf->sport = sport;
    rpf->dhost = dupstr(dhost);
    rpf->dport = dport;
    rpf->addressfamily = addressfamily;
    rpf->log_description = dupstr(log_description);
    rpf->pfr = pfr;
    rpf->share_ctx = share_ctx;

    if (add234(s->rportfwds, rpf) != rpf) {
        free_rportfwd(rpf);
        return NULL;
    }

    if (!rpf->share_ctx) {
        PktOut *pktout = ssh_bpp_new_pktout(
            s->ppl.bpp, SSH2_MSG_GLOBAL_REQUEST);
        put_stringz(pktout, "tcpip-forward");
        put_bool(pktout, 1);       /* want reply */
        put_stringz(pktout, rpf->shost);
        put_uint32(pktout, rpf->sport);
        pq_push(s->ppl.out_pq, pktout);

        ssh2_queue_global_request_handler(
            s, ssh2_rportfwd_globreq_response, rpf);
    }

    return rpf;
}

static void ssh2_rportfwd_remove(ConnectionLayer *cl, struct ssh_rportfwd *rpf)
{
    struct ssh2_connection_state *s =
        container_of(cl, struct ssh2_connection_state, cl);

    if (rpf->share_ctx) {
        /*
         * We don't manufacture a cancel-tcpip-forward message for
         * remote port forwardings being removed on behalf of a
         * downstream; we just pass through the one the downstream
         * sent to us.
         */
    } else {
        PktOut *pktout = ssh_bpp_new_pktout(
            s->ppl.bpp, SSH2_MSG_GLOBAL_REQUEST);
        put_stringz(pktout, "cancel-tcpip-forward");
        put_bool(pktout, 0);           /* _don't_ want reply */
        put_stringz(pktout, rpf->shost);
        put_uint32(pktout, rpf->sport);
        pq_push(s->ppl.out_pq, pktout);
    }

    struct ssh_rportfwd *realpf = del234(s->rportfwds, rpf);
    assert(realpf == rpf);
    free_rportfwd(rpf);
}

static void ssh2_sharing_globreq_response(
    struct ssh2_connection_state *s, PktIn *pktin, void *ctx)
{
    ssh_sharing_connstate *cs = (ssh_sharing_connstate *)ctx;
    share_got_pkt_from_server(cs, pktin->type,
                              BinarySource_UPCAST(pktin)->data,
                              BinarySource_UPCAST(pktin)->len);
}

static void ssh2_sharing_queue_global_request(
    ConnectionLayer *cl, ssh_sharing_connstate *cs)
{
    struct ssh2_connection_state *s =
        container_of(cl, struct ssh2_connection_state, cl);
    ssh2_queue_global_request_handler(s, ssh2_sharing_globreq_response, cs);
}

static void ssh2_sharing_no_more_downstreams(ConnectionLayer *cl)
{
    struct ssh2_connection_state *s =
        container_of(cl, struct ssh2_connection_state, cl);
    queue_toplevel_callback(ssh2_check_termination_callback, s);
}

static struct X11FakeAuth *ssh2_add_sharing_x11_display(
    ConnectionLayer *cl, int authtype, ssh_sharing_connstate *share_cs,
    share_channel *share_chan)
{
    struct ssh2_connection_state *s =
        container_of(cl, struct ssh2_connection_state, cl);
    struct X11FakeAuth *auth;

    /*
     * Make up a new set of fake X11 auth data, and add it to the tree
     * of currently valid ones with an indication of the sharing
     * context that it's relevant to.
     */
    auth = x11_invent_fake_auth(s->x11authtree, authtype);
    auth->share_cs = share_cs;
    auth->share_chan = share_chan;

    return auth;
}

static void ssh2_remove_sharing_x11_display(
    ConnectionLayer *cl, struct X11FakeAuth *auth)
{
    struct ssh2_connection_state *s =
        container_of(cl, struct ssh2_connection_state, cl);
    del234(s->x11authtree, auth);
    x11_free_fake_auth(auth);
}

static unsigned ssh2_alloc_sharing_channel(
    ConnectionLayer *cl, ssh_sharing_connstate *connstate)
{
    struct ssh2_connection_state *s =
        container_of(cl, struct ssh2_connection_state, cl);
    struct ssh2_channel *c = snew(struct ssh2_channel);

    c->connlayer = s;
    ssh2_channel_init(c);
    c->chan = NULL;
    c->sharectx = connstate;
    return c->localid;
}

static void ssh2_delete_sharing_channel(ConnectionLayer *cl, unsigned localid)
{
    struct ssh2_connection_state *s =
        container_of(cl, struct ssh2_connection_state, cl);
    struct ssh2_channel *c = find234(s->channels, &localid, ssh2_channelfind);
    if (c)
        ssh2_channel_destroy(c);
}

static void ssh2_send_packet_from_downstream(
        ConnectionLayer *cl, unsigned id, int type,
        const void *data, int datalen, const char *additional_log_text)
{
    struct ssh2_connection_state *s =
        container_of(cl, struct ssh2_connection_state, cl);
    PktOut *pkt = ssh_bpp_new_pktout(s->ppl.bpp, type);
    pkt->downstream_id = id;
    pkt->additional_log_text = additional_log_text;
    put_data(pkt, data, datalen);
    pq_push(s->ppl.out_pq, pkt);
}

static int ssh2_agent_forwarding_permitted(ConnectionLayer *cl)
{
    struct ssh2_connection_state *s =
        container_of(cl, struct ssh2_connection_state, cl);
    return conf_get_int(s->conf, CONF_agentfwd) && agent_exists();
}

static void mainchan_free(Channel *chan);
static void mainchan_open_confirmation(Channel *chan);
static void mainchan_open_failure(Channel *chan, const char *errtext);
static int mainchan_send(Channel *chan, int is_stderr, const void *, int);
static void mainchan_send_eof(Channel *chan);
static void mainchan_set_input_wanted(Channel *chan, int wanted);
static char *mainchan_log_close_msg(Channel *chan);
static int mainchan_rcvd_exit_status(Channel *chan, int status);
static int mainchan_rcvd_exit_signal(
    Channel *chan, ptrlen signame, int core_dumped, ptrlen msg);
static int mainchan_rcvd_exit_signal_numeric(
    Channel *chan, int signum, int core_dumped, ptrlen msg);
static void mainchan_request_response(Channel *chan, int success);

static const struct ChannelVtable mainchan_channelvt = {
    mainchan_free,
    mainchan_open_confirmation,
    mainchan_open_failure,
    mainchan_send,
    mainchan_send_eof,
    mainchan_set_input_wanted,
    mainchan_log_close_msg,
    chan_no_eager_close,
    mainchan_rcvd_exit_status,
    mainchan_rcvd_exit_signal,
    mainchan_rcvd_exit_signal_numeric,
    mainchan_request_response,
};

typedef enum MainChanType {
    MAINCHAN_SESSION, MAINCHAN_DIRECT_TCPIP
} MainChanType;

typedef struct mainchan {
    struct ssh2_connection_state *connlayer;
    SshChannel *sc;
    MainChanType type;
    Conf *conf;
    int req_x11, req_agent, req_pty, req_cmd_primary, req_cmd_fallback;
    int n_req_env, n_env_replies, n_env_fails;

    Channel chan;
} mainchan;

static void create_mainchan(struct ssh2_connection_state *s, Conf *conf)
{
    PacketProtocolLayer *ppl = &s->ppl; /* for ppl_logevent */
    mainchan *mc;
    PktOut *pktout;

    if (conf_get_int(s->conf, CONF_ssh_no_shell))
        return;                        /* do nothing */

    mc = snew(mainchan);
    memset(mc, 0, sizeof(mainchan));
    mc->connlayer = s;
    mc->sc = NULL;
    mc->chan.vt = &mainchan_channelvt;
    mc->chan.initial_fixed_window_size = 0;
    mc->conf = conf_copy(conf);

    if (*conf_get_str(mc->conf, CONF_ssh_nc_host)) {
        mc->sc = ssh_lportfwd_open(
            &s->cl, conf_get_str(mc->conf, CONF_ssh_nc_host),
            conf_get_int(mc->conf, CONF_ssh_nc_port),
            "main channel", &mc->chan);
        s->mainchan = container_of(mc->sc, struct ssh2_channel, sc);
        mc->type = MAINCHAN_DIRECT_TCPIP;
    } else {
        s->mainchan = snew(struct ssh2_channel);
        mc->sc = &s->mainchan->sc;
        s->mainchan->connlayer = s;
        ssh2_channel_init(s->mainchan);
        s->mainchan->chan = &mc->chan;
        s->mainchan->halfopen = TRUE;
        pktout = ssh2_chanopen_init(s->mainchan, "session");
        ppl_logevent(("Opening session as main channel"));
        pq_push(s->ppl.out_pq, pktout);
        mc->type = MAINCHAN_SESSION;
    }
}

static void mainchan_free(Channel *chan)
{
    assert(chan->vt == &mainchan_channelvt);
    mainchan *mc = container_of(chan, mainchan, chan);
    struct ssh2_connection_state *s = mc->connlayer;
    s->mainchan = NULL;
    conf_free(mc->conf);
    sfree(mc);
}

static void mainchan_try_fallback_command(mainchan *mc);
static void mainchan_ready(mainchan *mc);

static void mainchan_open_confirmation(Channel *chan)
{
    mainchan *mc = container_of(chan, mainchan, chan);
    struct ssh2_connection_state *s = mc->connlayer;
    PacketProtocolLayer *ppl = &s->ppl; /* for ppl_logevent */

    seat_update_specials_menu(s->ppl.seat);
    ppl_logevent(("Opened main channel"));

    if (s->ssh_is_simple)
        sshfwd_hint_channel_is_simple(mc->sc);

    if (mc->type == MAINCHAN_SESSION) {
	/*
	 * Send the CHANNEL_REQUESTS for the main session channel.
         */
        char *key, *val, *cmd;
        struct X11Display *x11disp;
        struct X11FakeAuth *x11auth;
        int retry_cmd_now = FALSE;

	if (conf_get_int(mc->conf, CONF_x11_forward)) {;
            char *x11_setup_err;
            if ((x11disp = x11_setup_display(
                     conf_get_str(mc->conf, CONF_x11_display),
                     mc->conf, &x11_setup_err)) == NULL) {
                ppl_logevent(("X11 forwarding not enabled: unable to"
                              " initialise X display: %s", x11_setup_err));
                sfree(x11_setup_err);
            } else {
                x11auth = x11_invent_fake_auth(
                    s->x11authtree, conf_get_int(mc->conf, CONF_x11_auth));
                x11auth->disp = x11disp;

                sshfwd_request_x11_forwarding(
                    mc->sc, TRUE, x11auth->protoname, x11auth->datastring,
                    x11disp->screennum, FALSE);
                mc->req_x11 = TRUE;
            }
        }

	if (ssh_agent_forwarding_permitted(&s->cl)) {
            sshfwd_request_agent_forwarding(mc->sc, TRUE);
            mc->req_agent = TRUE;
        }

	if (!conf_get_int(mc->conf, CONF_nopty)) {
            sshfwd_request_pty(
                mc->sc, TRUE, mc->conf, s->term_width, s->term_height);
            /* Record the initial width/height we requested, so we
             * know whether we need to send a change later once
             * everything is set up (if the window is resized in
             * between) */
            s->term_width_orig = s->term_width;
            s->term_height_orig = s->term_height;
            mc->req_pty = TRUE;
        }

        for (val = conf_get_str_strs(mc->conf, CONF_environmt, NULL, &key);
             val != NULL;
             val = conf_get_str_strs(mc->conf, CONF_environmt, key, &key)) {
            sshfwd_send_env_var(mc->sc, TRUE, key, val);
            mc->n_req_env++;
        }
        if (mc->n_req_env)
            ppl_logevent(("Sent %d environment variables", mc->n_req_env));

        cmd = conf_get_str(s->conf, CONF_remote_cmd);
        if (conf_get_int(s->conf, CONF_ssh_subsys)) {
            retry_cmd_now = !sshfwd_start_subsystem(mc->sc, TRUE, cmd);
        } else if (*cmd) {
            sshfwd_start_command(mc->sc, TRUE, cmd);
        } else {
            sshfwd_start_shell(mc->sc, TRUE);
        }

        if (retry_cmd_now)
            mainchan_try_fallback_command(mc);
        else
            mc->req_cmd_primary = TRUE;

    } else {
	s->echoedit = TRUE;
        mainchan_ready(mc);
    }
}

static void mainchan_try_fallback_command(mainchan *mc)
{
    const char *cmd = conf_get_str(mc->conf, CONF_remote_cmd2);
    if (conf_get_int(mc->conf, CONF_ssh_subsys2)) {
        sshfwd_start_subsystem(mc->sc, TRUE, cmd);
    } else {
        sshfwd_start_command(mc->sc, TRUE, cmd);
    }
    mc->req_cmd_fallback = TRUE;
}

static void mainchan_request_response(Channel *chan, int success)
{
    assert(chan->vt == &mainchan_channelvt);
    mainchan *mc = container_of(chan, mainchan, chan);
    struct ssh2_connection_state *s = mc->connlayer;
    PacketProtocolLayer *ppl = &s->ppl; /* for ppl_logevent */

    if (mc->req_x11) {
        mc->req_x11 = FALSE;

        if (success) {
            ppl_logevent(("X11 forwarding enabled"));
            s->X11_fwd_enabled = TRUE;
        } else {
            ppl_logevent(("X11 forwarding refused"));
        }
        return;
    }

    if (mc->req_agent) {
        mc->req_agent = FALSE;

        if (success) {
            ppl_logevent(("Agent forwarding enabled"));
        } else {
            ppl_logevent(("Agent forwarding refused"));
        }
        return;
    }

    if (mc->req_pty) {
        mc->req_pty = FALSE;

        if (success) {
            ppl_logevent(("Allocated pty"));
            s->agent_fwd_enabled = TRUE;
        } else {
            ppl_logevent(("Server refused to allocate pty"));
            ppl_printf(("Server refused to allocate pty\r\n"));
            s->echoedit = TRUE;
        }
        return;
    }

    if (mc->n_env_replies < mc->n_req_env) {
        int j = mc->n_env_replies++;
        if (!success) {
            ppl_logevent(("Server refused to set environment variable %s",
                          conf_get_str_nthstrkey(mc->conf,
                                                 CONF_environmt, j)));
            mc->n_env_fails++;
        }

        if (mc->n_env_replies == mc->n_req_env) {
            if (mc->n_env_fails == 0) {
                ppl_logevent(("All environment variables successfully set"));
            } else if (mc->n_env_fails == mc->n_req_env) {
                ppl_logevent(("All environment variables refused"));
                ppl_printf(("Server refused to set environment "
                            "variables\r\n"));
            } else {
                ppl_printf(("Server refused to set all environment "
                            "variables\r\n"));
            }
        }
        return;
    }

    if (mc->req_cmd_primary) {
        mc->req_cmd_primary = FALSE;

        if (success) {
            ppl_logevent(("Started a shell/command"));
            mainchan_ready(mc);
            queue_idempotent_callback(&s->ppl.ic_process_queue);
        } else if (*conf_get_str(mc->conf, CONF_remote_cmd2)) {
            ppl_logevent(("Primary command failed; attempting fallback"));
            mainchan_try_fallback_command(mc);
        } else {
            /*
             * If there's no remote_cmd2 configured, then we have no
             * fallback command, so we've run out of options.
             */
            ssh_sw_abort(s->ppl.ssh,
                         "Server refused to start a shell/command");
        }
        return;
    }

    if (mc->req_cmd_fallback) {
        mc->req_cmd_fallback = FALSE;

        if (success) {
            ppl_logevent(("Started a shell/command"));
            ssh_got_fallback_cmd(s->ppl.ssh);
            mainchan_ready(mc);
            queue_idempotent_callback(&s->ppl.ic_process_queue);
        } else {
            ssh_sw_abort(s->ppl.ssh,
                         "Server refused to start a shell/command");
        }
        return;
    }
}

static void mainchan_ready(mainchan *mc)
{
    struct ssh2_connection_state *s = mc->connlayer;

    s->mainchan_ready = TRUE;
    s->want_user_input = TRUE;
    ssh_ppl_got_user_input(&s->ppl); /* in case any is already queued */

    /* If an EOF or a window-size change arrived before we were ready
     * to handle either one, handle them now. */
    if (s->mainchan_eof_pending)
	ssh_ppl_special_cmd(&s->ppl, SS_EOF, 0);
    if (s->term_width_orig != s->term_width ||
        s->term_height_orig != s->term_height)
	ssh_terminal_size(&s->cl, s->term_width, s->term_height);

    ssh_ldisc_update(s->ppl.ssh);
}

static void mainchan_open_failure(Channel *chan, const char *errtext)
{
    assert(chan->vt == &mainchan_channelvt);
    mainchan *mc = container_of(chan, mainchan, chan);
    struct ssh2_connection_state *s = mc->connlayer;

    ssh_sw_abort(
        s->ppl.ssh, "Server refused to open main channel: %s", errtext);
}

static int mainchan_send(Channel *chan, int is_stderr,
                         const void *data, int length)
{
    assert(chan->vt == &mainchan_channelvt);
    mainchan *mc = container_of(chan, mainchan, chan);
    struct ssh2_connection_state *s = mc->connlayer;
    return seat_output(s->ppl.seat, is_stderr, data, length);
}

static void mainchan_send_eof(Channel *chan)
{
    assert(chan->vt == &mainchan_channelvt);
    mainchan *mc = container_of(chan, mainchan, chan);
    struct ssh2_connection_state *s = mc->connlayer;
    PacketProtocolLayer *ppl = &s->ppl; /* for ppl_logevent */

    if (!s->mainchan_eof_sent && (seat_eof(s->ppl.seat) || s->got_pty)) {
        /*
         * Either seat_eof told us that the front end wants us to
         * close the outgoing side of the connection as soon as we see
         * EOF from the far end, or else we've unilaterally decided to
         * do that because we've allocated a remote pty and hence EOF
         * isn't a particularly meaningful concept.
         */
        sshfwd_write_eof(mc->sc);
        ppl_logevent(("Sent EOF message"));
        s->mainchan_eof_sent = TRUE;
        s->want_user_input = FALSE;      /* now stop reading from stdin */
    }
}

static void mainchan_set_input_wanted(Channel *chan, int wanted)
{
    assert(chan->vt == &mainchan_channelvt);
    mainchan *mc = container_of(chan, mainchan, chan);
    struct ssh2_connection_state *s = mc->connlayer;

    /*
     * This is the main channel of the SSH session, i.e. the one tied
     * to the standard input (or GUI) of the primary SSH client user
     * interface. So ssh->send_ok is how we control whether we're
     * reading from that input.
     */
    s->want_user_input = wanted;
}

static char *mainchan_log_close_msg(Channel *chan)
{
    return dupstr("Main session channel closed");
}

static int mainchan_rcvd_exit_status(Channel *chan, int status)
{
    assert(chan->vt == &mainchan_channelvt);
    mainchan *mc = container_of(chan, mainchan, chan);
    struct ssh2_connection_state *s = mc->connlayer;
    PacketProtocolLayer *ppl = &s->ppl; /* for ppl_logevent */

    ssh_got_exitcode(s->ppl.ssh, status);
    ppl_logevent(("Session sent command exit status %d", status));
    return TRUE;
}

static void ssh2_log_exit_signal_common(
    struct ssh2_connection_state *s, const char *sigdesc,
    int core_dumped, ptrlen msg)
{
    PacketProtocolLayer *ppl = &s->ppl; /* for ppl_logevent */

    const char *core_msg = core_dumped ? " (core dumped)" : "";
    const char *msg_pre = (msg.len ? " (" : "");
    const char *msg_post = (msg.len ? ")" : "");
    ppl_logevent(("Session exited on %s%s%s%.*s%s",
                  sigdesc, core_msg, msg_pre, PTRLEN_PRINTF(msg), msg_post));
}

static int mainchan_rcvd_exit_signal(
    Channel *chan, ptrlen signame, int core_dumped, ptrlen msg)
{
    assert(chan->vt == &mainchan_channelvt);
    mainchan *mc = container_of(chan, mainchan, chan);
    struct ssh2_connection_state *s = mc->connlayer;
    int exitcode;
    char *signame_str;

    /*
     * Translate the signal description back into a locally
     * meaningful number.
     */

    if (0)
        ;
#define TRANSLATE_SIGNAL(s)                                     \
    else if (ptrlen_eq_string(signame, #s))      \
        exitcode = 128 + SIG ## s
#ifdef SIGABRT
    TRANSLATE_SIGNAL(ABRT);
#endif
#ifdef SIGALRM
    TRANSLATE_SIGNAL(ALRM);
#endif
#ifdef SIGFPE
    TRANSLATE_SIGNAL(FPE);
#endif
#ifdef SIGHUP
    TRANSLATE_SIGNAL(HUP);
#endif
#ifdef SIGILL
    TRANSLATE_SIGNAL(ILL);
#endif
#ifdef SIGINT
    TRANSLATE_SIGNAL(INT);
#endif
#ifdef SIGKILL
    TRANSLATE_SIGNAL(KILL);
#endif
#ifdef SIGPIPE
    TRANSLATE_SIGNAL(PIPE);
#endif
#ifdef SIGQUIT
    TRANSLATE_SIGNAL(QUIT);
#endif
#ifdef SIGSEGV
    TRANSLATE_SIGNAL(SEGV);
#endif
#ifdef SIGTERM
    TRANSLATE_SIGNAL(TERM);
#endif
#ifdef SIGUSR1
    TRANSLATE_SIGNAL(USR1);
#endif
#ifdef SIGUSR2
    TRANSLATE_SIGNAL(USR2);
#endif
#undef TRANSLATE_SIGNAL
    else
        exitcode = 128;

    ssh_got_exitcode(s->ppl.ssh, exitcode);
    if (exitcode == 128)
        signame_str = dupprintf("unrecognised signal \"%.*s\"",
                                PTRLEN_PRINTF(signame));
    else
        signame_str = dupprintf("signal SIG%.*s", PTRLEN_PRINTF(signame));
    ssh2_log_exit_signal_common(s, signame_str, core_dumped, msg);
    sfree(signame_str);
    return TRUE;
}

static int mainchan_rcvd_exit_signal_numeric(
    Channel *chan, int signum, int core_dumped, ptrlen msg)
{
    assert(chan->vt == &mainchan_channelvt);
    mainchan *mc = container_of(chan, mainchan, chan);
    struct ssh2_connection_state *s = mc->connlayer;
    char *signum_str;

    ssh_got_exitcode(s->ppl.ssh, 128 + signum);
    signum_str = dupprintf("signal %d", signum);
    ssh2_log_exit_signal_common(s, signum_str, core_dumped, msg);
    sfree(signum_str);
    return TRUE;
}

static char *chan_open_x11(
    struct ssh2_connection_state *s, Channel **ch, SshChannel *sc,
    ptrlen peeraddr, int peerport)
{
    PacketProtocolLayer *ppl = &s->ppl; /* for ppl_logevent */
    char *peeraddr_str;

    ppl_logevent(("Received X11 connect request from %.*s:%d",
                  PTRLEN_PRINTF(peeraddr), peerport));

    if (!s->X11_fwd_enabled && !s->connshare)
        return dupstr("X11 forwarding is not enabled");

    peeraddr_str = peeraddr.ptr ? mkstr(peeraddr) : NULL;
    *ch = x11_new_channel(
        s->x11authtree, sc, peeraddr_str, peerport, s->connshare != NULL);
    sfree(peeraddr_str);
    ppl_logevent(("Opened X11 forward channel"));

    return NULL;
}

static char *chan_open_forwarded_tcpip(
    struct ssh2_connection_state *s, Channel **ch, SshChannel *sc,
    ssh_sharing_connstate **share_ctx,
    ptrlen fwdaddr, int fwdport, ptrlen peeraddr, int peerport)
{
    PacketProtocolLayer *ppl = &s->ppl; /* for ppl_logevent */
    struct ssh_rportfwd pf, *realpf;
    char *err;

    ppl_logevent(("Received remote port %.*s:%d open request from %.*s:%d",
                  PTRLEN_PRINTF(fwdaddr), fwdport,
                  PTRLEN_PRINTF(peeraddr), peerport));

    pf.shost = mkstr(fwdaddr);
    pf.sport = fwdport;
    realpf = find234(s->rportfwds, &pf, NULL);
    sfree(pf.shost);

    if (realpf == NULL)
        return dupstr("Remote port is not recognised");

    if (realpf->share_ctx) {
        /*
         * This port forwarding is on behalf of a connection-sharing
         * downstream.
         */
        *share_ctx = realpf->share_ctx;
        return NULL;
    }

    err = portfwdmgr_connect(
        s->portfwdmgr, ch, realpf->dhost, realpf->dport,
        sc, realpf->addressfamily);
    ppl_logevent(("Attempting to forward remote port to %s:%d",
                  realpf->dhost, realpf->dport));
    if (err != NULL) {
        ppl_logevent(("Port open failed: %s", err));
        sfree(err);
        return dupstr("Port open failed");
    }

    ppl_logevent(("Forwarded port opened successfully"));
    return NULL;
}

static char *chan_open_auth_agent(
    struct ssh2_connection_state *s, Channel **ch, SshChannel *sc)
{
    if (!s->agent_fwd_enabled)
        return dupstr("Agent forwarding is not enabled");

    *ch = agentf_new(sc);
    return NULL;
}

/*
 * List of signal names defined by RFC 4254. These include all the ISO
 * C signals, but are a subset of the POSIX required signals.
 *
 * The list macro takes parameters MAIN and SUB, which is an arbitrary
 * UI decision to expose the signals we think users are most likely to
 * want, with extra descriptive text, and relegate the less probable
 * ones to a submenu for people who know what they're doing.
 */
#define SIGNAL_LIST(MAIN, SUB)                  \
    MAIN(INT, "Interrupt")                      \
    MAIN(TERM, "Terminate")                     \
    MAIN(KILL, "Kill")                          \
    MAIN(QUIT, "Quit")                          \
    MAIN(HUP, "Hangup")                         \
    SUB(ABRT)                                   \
    SUB(ALRM)                                   \
    SUB(FPE)                                    \
    SUB(ILL)                                    \
    SUB(PIPE)                                   \
    SUB(SEGV)                                   \
    SUB(USR1)                                   \
    SUB(USR2)                                   \
    /* end of list */

static int ssh2_connection_get_specials(
    PacketProtocolLayer *ppl, add_special_fn_t add_special, void *ctx)
{
    struct ssh2_connection_state *s =
        container_of(ppl, struct ssh2_connection_state, ppl);
    int toret = FALSE;

    if (s->mainchan) {
	add_special(ctx, "Break", SS_BRK, 0);

        #define ADD_MAIN(name, desc) \
        add_special(ctx, "SIG" #name " (" desc ")", SS_SIG ## name, 0);
        #define ADD_SUB(name) \
	add_special(ctx, "SIG" #name, SS_SIG ## name, 0);

        #define NO_ADD_SUB(name)
        #define NO_ADD_MAIN(name, desc)

        SIGNAL_LIST(ADD_MAIN, NO_ADD_SUB);
	add_special(ctx, "More signals", SS_SUBMENU, 0);
        SIGNAL_LIST(NO_ADD_MAIN, ADD_SUB);
	add_special(ctx, NULL, SS_EXITMENU, 0);

        #undef ADD_MAIN
        #undef ADD_SUB
        #undef NO_ADD_MAIN
        #undef NO_ADD_SUB

        toret = TRUE;
    }

    /*
     * Don't bother offering IGNORE if we've decided the remote
     * won't cope with it, since we wouldn't bother sending it if
     * asked anyway.
     */
    if (!(s->ppl.remote_bugs & BUG_CHOKES_ON_SSH2_IGNORE)) {
        if (toret)
            add_special(ctx, NULL, SS_SEP, 0);

        add_special(ctx, "IGNORE message", SS_NOP, 0);
        toret = TRUE;
    }

    return toret;
}

static const char *ssh_signal_lookup(SessionSpecialCode code)
{
    #define CHECK_SUB(name) \
    if (code == SS_SIG ## name) return #name;
    #define CHECK_MAIN(name, desc) CHECK_SUB(name)

    SIGNAL_LIST(CHECK_MAIN, CHECK_SUB);
    return NULL;

    #undef CHECK_MAIN
    #undef CHECK_SUB
}

static void ssh2_connection_special_cmd(PacketProtocolLayer *ppl,
                                        SessionSpecialCode code, int arg)
{
    struct ssh2_connection_state *s =
        container_of(ppl, struct ssh2_connection_state, ppl);
    PktOut *pktout;
    const char *signame;

    if (code == SS_PING || code == SS_NOP) {
        if (!(s->ppl.remote_bugs & BUG_CHOKES_ON_SSH2_IGNORE)) {
            pktout = ssh_bpp_new_pktout(s->ppl.bpp, SSH2_MSG_IGNORE);
            put_stringz(pktout, "");
            pq_push(s->ppl.out_pq, pktout);
	}
    } else if (code == SS_EOF) {
	if (!s->mainchan_ready) {
	    /*
	     * Buffer the EOF to send as soon as the main channel is
	     * fully set up.
	     */
            s->mainchan_eof_pending = TRUE;
	} else if (s->mainchan && !s->mainchan_eof_sent) {
            sshfwd_write_eof(&s->mainchan->sc);
	}
    } else if (code == SS_BRK) {
        if (s->mainchan) {
	    pktout = ssh2_chanreq_init(s->mainchan, "break", NULL, NULL);
	    put_uint32(pktout, 0);   /* default break length */
            pq_push(s->ppl.out_pq, pktout);
	}
    } else if ((signame = ssh_signal_lookup(code)) != NULL) {
        /* It's a signal. */
        if (s->mainchan) {
            pktout = ssh2_chanreq_init(s->mainchan, "signal", NULL, NULL);
            put_stringz(pktout, signame);
            pq_push(s->ppl.out_pq, pktout);
            ppl_logevent(("Sent signal SIG%s", signame));
        }
    }
}

static void ssh2_terminal_size(ConnectionLayer *cl, int width, int height)
{
    struct ssh2_connection_state *s =
        container_of(cl, struct ssh2_connection_state, cl);

    s->term_width = width;
    s->term_height = height;

    if (s->mainchan_ready) {
        PktOut *pktout = ssh2_chanreq_init(
            s->mainchan, "window-change", NULL, NULL);
        put_uint32(pktout, s->term_width);
        put_uint32(pktout, s->term_height);
        put_uint32(pktout, 0);
        put_uint32(pktout, 0);
	pq_push(s->ppl.out_pq, pktout);
    }
}

static void ssh2_stdout_unthrottle(ConnectionLayer *cl, int bufsize)
{
    struct ssh2_connection_state *s =
        container_of(cl, struct ssh2_connection_state, cl);

    if (s->mainchan)
        ssh2channel_unthrottle(&s->mainchan->sc, bufsize);
}

static int ssh2_stdin_backlog(ConnectionLayer *cl)
{
    struct ssh2_connection_state *s =
        container_of(cl, struct ssh2_connection_state, cl);

    return s->mainchan ? bufchain_size(&s->mainchan->outbuffer) : 0;
}

static void ssh2_throttle_all_channels(ConnectionLayer *cl, int throttled)
{
    struct ssh2_connection_state *s =
        container_of(cl, struct ssh2_connection_state, cl);
    struct ssh2_channel *c;
    int i;

    s->all_channels_throttled = throttled;

    for (i = 0; NULL != (c = index234(s->channels, i)); i++)
        ssh2_channel_check_throttle(c);
}

static int ssh2_ldisc_option(ConnectionLayer *cl, int option)
{
    struct ssh2_connection_state *s =
        container_of(cl, struct ssh2_connection_state, cl);

    /* We always return the same value for LD_ECHO and LD_EDIT */
    return s->echoedit;
}

static int ssh2_connection_want_user_input(PacketProtocolLayer *ppl)
{
    struct ssh2_connection_state *s =
        container_of(ppl, struct ssh2_connection_state, ppl);
    return s->mainchan_ready && s->want_user_input;
}

static void ssh2_connection_got_user_input(PacketProtocolLayer *ppl)
{
    struct ssh2_connection_state *s =
        container_of(ppl, struct ssh2_connection_state, ppl);

    while (s->mainchan && bufchain_size(s->ppl.user_input) > 0) {
        /*
         * Add user input to the main channel's buffer.
         */
        void *data;
        int len;
        bufchain_prefix(s->ppl.user_input, &data, &len);
        sshfwd_write(&s->mainchan->sc, data, len);
        bufchain_consume(s->ppl.user_input, len);
    }
}

static void ssh2_connection_reconfigure(PacketProtocolLayer *ppl, Conf *conf)
{
    struct ssh2_connection_state *s =
        container_of(ppl, struct ssh2_connection_state, ppl);

    conf_free(s->conf);
    s->conf = conf_copy(conf);

    if (s->portfwdmgr_configured)
        portfwdmgr_config(s->portfwdmgr, s->conf);
}
