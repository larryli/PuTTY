/*
 * HTTP CONNECT proxy negotiation.
 */

#include "putty.h"
#include "network.h"
#include "proxy.h"
#include "sshcr.h"

static bool read_line(bufchain *input, strbuf *output, bool is_header)
{
    char c;

    while (bufchain_try_fetch(input, &c, 1)) {
        if (is_header && output->len > 0 &&
            output->s[output->len - 1] == '\n') {
            /*
             * A newline terminates the header, provided we're sure it
             * is _not_ followed by a space or a tab.
             */
            if (c != ' ' && c != '\t')
                goto done;  /* we have a complete header line */
        } else {
            put_byte(output, c);
            bufchain_consume(input, 1);

            if (!is_header && output->len > 0 &&
                output->s[output->len - 1] == '\n') {
                /* If we're looking for just a line, not an HTTP
                 * header, then any newline terminates it. */
                goto done;
            }
        }
    }

    return false;

  done:
    strbuf_chomp(output, '\n');
    strbuf_chomp(output, '\r');
    return true;
}

typedef enum HttpAuthType { AUTH_NONE, AUTH_BASIC, AUTH_DIGEST } HttpAuthType;

typedef struct HttpProxyNegotiator {
    int crLine;
    strbuf *response, *header, *token;
    int http_status_pos;
    size_t header_pos;
    strbuf *username, *password;
    int http_status;
    bool connection_close;
    HttpAuthType next_auth_type;
    bool try_auth_from_conf;
    strbuf *realm, *nonce, *opaque, *uri;
    bool got_opaque;
    uint32_t nonce_count;
    bool digest_nonce_was_stale;
    HttpDigestHash digest_hash;
    bool hash_username;
    prompts_t *prompts;
    int username_prompt_index, password_prompt_index;
    size_t content_length;
    ProxyNegotiator pn;
} HttpProxyNegotiator;

static ProxyNegotiator *proxy_http_new(const ProxyNegotiatorVT *vt)
{
    HttpProxyNegotiator *s = snew(HttpProxyNegotiator);
    memset(s, 0, sizeof(*s));
    s->pn.vt = vt;
    s->response = strbuf_new();
    s->header = strbuf_new();
    s->token = strbuf_new();
    s->username = strbuf_new();
    s->password = strbuf_new_nm();
    s->realm = strbuf_new();
    s->nonce = strbuf_new();
    s->opaque = strbuf_new();
    s->uri = strbuf_new();
    s->nonce_count = 0;
    /*
     * Always start with a CONNECT request containing no auth. If the
     * proxy rejects that, it will tell us what kind of auth it would
     * prefer.
     */
    s->next_auth_type = AUTH_NONE;
    return &s->pn;
}

static void proxy_http_free(ProxyNegotiator *pn)
{
    HttpProxyNegotiator *s = container_of(pn, HttpProxyNegotiator, pn);
    strbuf_free(s->response);
    strbuf_free(s->header);
    strbuf_free(s->token);
    strbuf_free(s->username);
    strbuf_free(s->password);
    strbuf_free(s->realm);
    strbuf_free(s->nonce);
    strbuf_free(s->opaque);
    strbuf_free(s->uri);
    if (s->prompts)
        free_prompts(s->prompts);
    sfree(s);
}

#define HTTP_HEADER_LIST(X) \
    X(HDR_CONNECTION, "Connection") \
    X(HDR_CONTENT_LENGTH, "Content-Length") \
    X(HDR_PROXY_AUTHENTICATE, "Proxy-Authenticate") \
    /* end of list */

typedef enum HttpHeader {
    #define ENUM_DEF(id, string) id,
    HTTP_HEADER_LIST(ENUM_DEF)
    #undef ENUM_DEF
    HDR_UNKNOWN
} HttpHeader;

static inline bool is_whitespace(char c)
{
    return (c == ' ' || c == '\t' || c == '\n');
}

static inline bool is_separator(char c)
{
    return (c == '(' || c == ')' || c == '<' || c == '>' || c == '@' ||
            c == ',' || c == ';' || c == ':' || c == '\\' || c == '"' ||
            c == '/' || c == '[' || c == ']' || c == '?' || c == '=' ||
            c == '{' || c == '}');
}

#define HTTP_SEPARATORS

static bool get_end_of_header(HttpProxyNegotiator *s)
{
    size_t pos = s->header_pos;

    while (pos < s->header->len && is_whitespace(s->header->s[pos]))
        pos++;

    if (pos == s->header->len) {
        s->header_pos = pos;
        return true;
    }

    return false;
}

static bool get_token(HttpProxyNegotiator *s)
{
    size_t pos = s->header_pos;

    while (pos < s->header->len && is_whitespace(s->header->s[pos]))
        pos++;

    if (pos == s->header->len)
        return false;                  /* end of string */

    if (is_separator(s->header->s[pos]))
        return false;

    strbuf_clear(s->token);
    while (pos < s->header->len &&
           !is_whitespace(s->header->s[pos]) &&
           !is_separator(s->header->s[pos]))
        put_byte(s->token, s->header->s[pos++]);

    s->header_pos = pos;
    return true;
}

static bool get_separator(HttpProxyNegotiator *s, char sep)
{
    size_t pos = s->header_pos;

    while (pos < s->header->len && is_whitespace(s->header->s[pos]))
        pos++;

    if (pos == s->header->len)
        return false;                  /* end of string */

    if (s->header->s[pos] != sep)
        return false;

    s->header_pos = ++pos;
    return true;
}

static bool get_quoted_string(HttpProxyNegotiator *s)
{
    size_t pos = s->header_pos;

    while (pos < s->header->len && is_whitespace(s->header->s[pos]))
        pos++;

    if (pos == s->header->len)
        return false;                  /* end of string */

    if (s->header->s[pos] != '"')
        return false;
    pos++;

    strbuf_clear(s->token);
    while (pos < s->header->len && s->header->s[pos] != '"') {
        if (s->header->s[pos] == '\\') {
            /* Backslash makes the next char literal, even if it's " or \ */
            pos++;
            if (pos == s->header->len)
                return false;          /* unexpected end of string */
        }
        put_byte(s->token, s->header->s[pos++]);
    }

    if (pos == s->header->len)
        return false;                  /* no closing quote */
    pos++;

    s->header_pos = pos;
    return true;
}

static void proxy_http_process_queue(ProxyNegotiator *pn)
{
    HttpProxyNegotiator *s = container_of(pn, HttpProxyNegotiator, pn);

    crBegin(s->crLine);

    /*
     * Initialise our username and password strbufs from the Conf.
     */
    put_dataz(s->username, conf_get_str(pn->ps->conf, CONF_proxy_username));
    put_dataz(s->password, conf_get_str(pn->ps->conf, CONF_proxy_password));
    if (s->username->len || s->password->len)
        s->try_auth_from_conf = true;

    /*
     * Set up the host:port string we're trying to connect to, also
     * used as the URI string in HTTP Digest auth.
     */
    {
        char dest[512];
        sk_getaddr(pn->ps->remote_addr, dest, lenof(dest));
        put_fmt(s->uri, "%s:%d", dest, pn->ps->remote_port);
    }

    while (true) {
        /*
         * Standard prefix for the HTTP CONNECT request.
         */
        put_fmt(pn->output,
                "CONNECT %s HTTP/1.1\r\n"
                "Host: %s\r\n", s->uri->s, s->uri->s);

        /*
         * Add an auth header, if we're planning to this time round.
         */
        if (s->next_auth_type == AUTH_BASIC) {
            put_datalit(pn->output, "Proxy-Authorization: Basic ");

            strbuf *base64_input = strbuf_new_nm();
            put_datapl(base64_input, ptrlen_from_strbuf(s->username));
            put_byte(base64_input, ':');
            put_datapl(base64_input, ptrlen_from_strbuf(s->password));

            char base64_output[4];
            for (size_t i = 0, e = base64_input->len; i < e; i += 3) {
                base64_encode_atom(base64_input->u + i,
                                   e-i > 3 ? 3 : e-i, base64_output);
                put_data(pn->output, base64_output, 4);
            }
            strbuf_free(base64_input);
            smemclr(base64_output, sizeof(base64_output));
            put_datalit(pn->output, "\r\n");
        } else if (s->next_auth_type == AUTH_DIGEST) {
            put_datalit(pn->output, "Proxy-Authorization: Digest ");
            http_digest_response(BinarySink_UPCAST(pn->output),
                                 ptrlen_from_strbuf(s->username),
                                 ptrlen_from_strbuf(s->password),
                                 ptrlen_from_strbuf(s->realm),
                                 PTRLEN_LITERAL("CONNECT"),
                                 ptrlen_from_strbuf(s->uri),
                                 PTRLEN_LITERAL("auth"),
                                 ptrlen_from_strbuf(s->nonce),
                                 (s->got_opaque ?
                                  ptrlen_from_strbuf(s->opaque) :
                                  make_ptrlen(NULL, 0)),
                                 ++s->nonce_count, s->digest_hash,
                                 s->hash_username);
            put_datalit(pn->output, "\r\n");
        }

        /*
         * Blank line to terminate the HTTP request.
         */
        put_datalit(pn->output, "\r\n");
        crReturnV;

        s->content_length = 0;
        s->connection_close = false;

        /*
         * Read and parse the HTTP status line, and check if it's a 2xx
         * for success.
         */
        strbuf_clear(s->response);
        crMaybeWaitUntilV(read_line(pn->input, s->response, false));
        {
            int maj_ver, min_ver, n_scanned;
            n_scanned = sscanf(
                s->response->s, "HTTP/%d.%d %n%d",
                &maj_ver, &min_ver, &s->http_status_pos, &s->http_status);

            if (n_scanned < 3) {
                pn->error = dupstr("HTTP response was absent or malformed");
                crStopV;
            }

            if (maj_ver < 1 && (maj_ver == 1 && min_ver < 1)) {
                /* Before HTTP/1.1, connections close by default */
                s->connection_close = true;
            }
        }

        /*
         * Read the HTTP response header section.
         */
        do {
            strbuf_clear(s->header);
            crMaybeWaitUntilV(read_line(pn->input, s->header, true));
            s->header_pos = 0;

            if (!get_token(s)) {
                /* Possibly we ought to panic if we see an HTTP header
                 * we can't make any sense of at all? But whatever,
                 * ignore it and hope the next one makes more sense */
                continue;
            }

            /* Parse the header name */
            HttpHeader hdr = HDR_UNKNOWN;
            {
                #define CHECK_HEADER(id, string) \
                    if (!stricmp(s->token->s, string)) hdr = id;
                HTTP_HEADER_LIST(CHECK_HEADER);
                #undef CHECK_HEADER
            }

            if (!get_separator(s, ':'))
                continue;

            if (hdr == HDR_CONTENT_LENGTH) {
                if (!get_token(s))
                    continue;
                s->content_length = strtoumax(s->token->s, NULL, 10);
            } else if (hdr == HDR_CONNECTION) {
                if (!get_token(s))
                    continue;
                if (!stricmp(s->token->s, "close"))
                    s->connection_close = true;
                else if (!stricmp(s->token->s, "keep-alive"))
                    s->connection_close = false;
            } else if (hdr == HDR_PROXY_AUTHENTICATE) {
                if (!get_token(s))
                    continue;

                if (!stricmp(s->token->s, "Basic")) {
                    s->next_auth_type = AUTH_BASIC;
                } else if (!stricmp(s->token->s, "Digest")) {
                    if (!http_digest_available) {
                        pn->error = dupprintf(
                            "HTTP proxy requested Digest authentication "
                            "which we do not support");
                        crStopV;
                    }

                    /* Parse the rest of the Digest header */
                    s->digest_nonce_was_stale = false;
                    s->digest_hash = HTTP_DIGEST_MD5;
                    strbuf_clear(s->realm);
                    strbuf_clear(s->nonce);
                    strbuf_clear(s->opaque);
                    s->got_opaque = false;
                    s->hash_username = false;

                    while (true) {
                        if (!get_token(s))
                            goto bad_digest;

                        if (!stricmp(s->token->s, "realm")) {
                            if (!get_separator(s, '=') ||
                                !get_quoted_string(s))
                                goto bad_digest;
                            put_datapl(s->realm, ptrlen_from_strbuf(s->token));
                        } else if (!stricmp(s->token->s, "nonce")) {
                            if (!get_separator(s, '=') ||
                                !get_quoted_string(s))
                                goto bad_digest;

                            /* If we have a fresh nonce, reset the
                             * nonce count. Otherwise, keep incrementing it. */
                            if (!ptrlen_eq_ptrlen(
                                    ptrlen_from_strbuf(s->token),
                                    ptrlen_from_strbuf(s->nonce)))
                                s->nonce_count = 0;

                            put_datapl(s->nonce, ptrlen_from_strbuf(s->token));
                        } else if (!stricmp(s->token->s, "opaque")) {
                            if (!get_separator(s, '=') ||
                                !get_quoted_string(s))
                                goto bad_digest;
                            put_datapl(s->opaque,
                                       ptrlen_from_strbuf(s->token));
                            s->got_opaque = true;
                        } else if (!stricmp(s->token->s, "stale")) {
                            if (!get_separator(s, '=') ||
                                !get_token(s))
                                goto bad_digest;
                            s->digest_nonce_was_stale = !stricmp(
                                s->token->s, "true");
                        } else if (!stricmp(s->token->s, "userhash")) {
                            if (!get_separator(s, '=') ||
                                !get_token(s))
                                goto bad_digest;
                            s->hash_username = !stricmp(s->token->s, "true");
                        } else if (!stricmp(s->token->s, "algorithm")) {
                            if (!get_separator(s, '=') ||
                                !get_token(s))
                                goto bad_digest;
                            bool found = false;

                            for (size_t i = 0; i < N_HTTP_DIGEST_HASHES; i++) {
                                if (!stricmp(s->token->s, httphashnames[i])) {
                                    s->digest_hash = i;
                                    found = true;
                                    break;
                                }
                            }

                            if (!found) {
                                pn->error = dupprintf(
                                    "HTTP proxy requested Digest hash "
                                    "algorithm '%s' which we do not support",
                                    s->token->s);
                                crStopV;
                            }
                        } else if (!stricmp(s->token->s, "qop")) {
                            if (!get_separator(s, '=') ||
                                !get_quoted_string(s))
                                goto bad_digest;
                            if (stricmp(s->token->s, "auth")) {
                                pn->error = dupprintf(
                                    "HTTP proxy requested Digest quality-of-"
                                    "protection type '%s' which we do not "
                                    "support", s->token->s);
                                crStopV;
                            }
                        } else {
                            /* Ignore any other auth-param */
                            if (!get_separator(s, '=') ||
                                (!get_quoted_string(s) && !get_token(s)))
                                goto bad_digest;
                        }

                        if (get_end_of_header(s))
                            break;
                        if (!get_separator(s, ','))
                            goto bad_digest;
                    }
                    s->next_auth_type = AUTH_DIGEST;
                    continue;
                  bad_digest:
                    pn->error = dupprintf("HTTP proxy sent Digest auth "
                                          "request we could not parse");
                    crStopV;
                } else {
                    pn->error = dupprintf("HTTP proxy asked for unsupported "
                                          "authentication type '%s'",
                                          s->token->s);
                    crStopV;
                }
            }
        } while (s->header->len > 0);

        /* Read and ignore the entire response document */
        crMaybeWaitUntilV(bufchain_try_consume(
                              pn->input, s->content_length));

        if (200 <= s->http_status && s->http_status < 300) {
            /* Any 2xx HTTP response means we're done */
            goto authenticated;
        } else if (s->http_status == 407) {
            /* 407 is Proxy Authentication Required, which we may be
             * able to do something about. */
            if (s->connection_close) {
                pn->error = dupprintf("HTTP proxy closed connection after "
                                      "asking for authentication");
                crStopV;
            }

            /* If we have auth details from the Conf and haven't tried
             * them yet, that's our first step. */
            if (s->try_auth_from_conf) {
                s->try_auth_from_conf = false;
                continue;
            }

            /* If the server sent us stale="true" in a Digest auth
             * header, that means we _don't_ need to request a new
             * password yet; just try again with the existing details
             * and the fresh nonce it sent us. */
            if (s->digest_nonce_was_stale)
                continue;

            /* Either we never had a password in the first place, or
             * the one we already presented was rejected. We can only
             * proceed from here if we have a way to ask the user
             * questions. */
            if (!pn->itr) {
                pn->error = dupprintf("HTTP proxy requested authentication "
                                      "which we do not have");
                crStopV;
            }

            /*
             * Send some prompts to the user. We'll assume the
             * password is always required (since it's just been
             * rejected, even if we did send one before), and we'll
             * prompt for the username only if we don't have one from
             * the Conf.
             */
            s->prompts = proxy_new_prompts(pn->ps);
            s->prompts->to_server = true;
            s->prompts->from_server = false;
            s->prompts->name = dupstr("HTTP proxy authentication");
            if (!s->username->len) {
                s->username_prompt_index = s->prompts->n_prompts;
                add_prompt(s->prompts, dupstr("Proxy username: "), true);
            } else {
                s->username_prompt_index = -1;
            }

            s->password_prompt_index = s->prompts->n_prompts;
            add_prompt(s->prompts, dupstr("Proxy password: "), false);

            while (true) {
                int prompt_result = seat_get_userpass_input(
                    interactor_announce(pn->itr), s->prompts);
                if (prompt_result > 0) {
                    break;
                } else if (prompt_result == 0) {
                    pn->aborted = true;
                    crStopV;
                }
                crReturnV;
            }

            if (s->username_prompt_index != -1) {
                strbuf_clear(s->username);
                put_dataz(s->username,
                          prompt_get_result_ref(
                              s->prompts->prompts[s->username_prompt_index]));
            }

            strbuf_clear(s->password);
            put_dataz(s->password,
                      prompt_get_result_ref(
                          s->prompts->prompts[s->password_prompt_index]));

            free_prompts(s->prompts);
            s->prompts = NULL;
        } else {
            /* Any other HTTP response is treated as permanent failure */
            pn->error = dupprintf("HTTP response %s",
                                  s->response->s + s->http_status_pos);
            crStopV;
        }
    }

  authenticated:
    /*
     * Success! Hand over to the main connection.
     */
    pn->done = true;

    crFinishV;
}

const struct ProxyNegotiatorVT http_proxy_negotiator_vt = {
    .new = proxy_http_new,
    .free = proxy_http_free,
    .process_queue = proxy_http_process_queue,
    .type = "HTTP",
};
