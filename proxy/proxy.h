/*
 * Network proxy abstraction in PuTTY
 *
 * A proxy layer, if necessary, wedges itself between the
 * network code and the higher level backend.
 *
 * Supported proxies: HTTP CONNECT, generic telnet, SOCKS 4 & 5
 */

#ifndef PUTTY_PROXY_H
#define PUTTY_PROXY_H

typedef struct ProxySocket ProxySocket;
typedef struct ProxyNegotiator ProxyNegotiator;
typedef struct ProxyNegotiatorVT ProxyNegotiatorVT;

struct ProxySocket {
    const char *error;

    Socket *sub_socket;
    Plug *plug;
    SockAddr *remote_addr;
    int remote_port;

    bufchain pending_output_data;
    bufchain pending_oob_output_data;
    bufchain pending_input_data;
    bool pending_eof;

    bool freeze; /* should we freeze the underlying socket when
                  * we are done with the proxy negotiation? this
                  * simply caches the value of sk_set_frozen calls.
                  */

    ProxyNegotiator *pn; /* non-NULL if still negotiating */
    bufchain output_from_negotiator;

    /* configuration, used to look up proxy settings */
    Conf *conf;

    /* for interaction with the Seat */
    Interactor *clientitr;
    LogPolicy *clientlp;
    Seat *clientseat;

    Socket sock;
    Plug plugimpl;
    Interactor interactor;
};

struct ProxyNegotiator {
    const ProxyNegotiatorVT *vt;

    /* Standard fields for any ProxyNegotiator. new() and free() don't
     * have to set these up; that's done centrally, to save duplication. */
    ProxySocket *ps;
    bufchain *input;
    bufchain_sink output[1];
    Interactor *itr; /* NULL if we are not able to interact with the user */

    /* Set to report success during proxy negotiation.  */
    bool done;

    /* Set to report an error during proxy negotiation. The main
     * ProxySocket will free it, and will then guarantee never to call
     * process_queue again. */
    char *error;

    /* Set to report user abort during proxy negotiation.  */
    bool aborted;
};

struct ProxyNegotiatorVT {
    ProxyNegotiator *(*new)(const ProxyNegotiatorVT *);
    void (*process_queue)(ProxyNegotiator *);
    void (*free)(ProxyNegotiator *);
    const char *type;
};

static inline ProxyNegotiator *proxy_negotiator_new(
    const ProxyNegotiatorVT *vt)
{ return vt->new(vt); }
static inline void proxy_negotiator_process_queue(ProxyNegotiator *pn)
{ pn->vt->process_queue(pn); }
static inline void proxy_negotiator_free(ProxyNegotiator *pn)
{ pn->vt->free(pn); }

extern const ProxyNegotiatorVT http_proxy_negotiator_vt;
extern const ProxyNegotiatorVT socks4_proxy_negotiator_vt;
extern const ProxyNegotiatorVT socks5_proxy_negotiator_vt;
extern const ProxyNegotiatorVT telnet_proxy_negotiator_vt;

/*
 * Centralised function to allow ProxyNegotiators to get hold of a
 * prompts_t.
 */
prompts_t *proxy_new_prompts(ProxySocket *ps);

/*
 * This may be reused by local-command proxies on individual
 * platforms.
 */
char *format_telnet_command(SockAddr *addr, int port, Conf *conf);

/*
 * These are implemented in cproxy.c or nocproxy.c, depending on
 * whether encrypted proxy authentication is available.
 */
extern const bool socks5_chap_available;
strbuf *chap_response(ptrlen challenge, ptrlen password);

#endif
