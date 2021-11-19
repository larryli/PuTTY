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

typedef struct HttpProxyNegotiator {
    int crLine;
    strbuf *line;
    ProxyNegotiator pn;
} HttpProxyNegotiator;

static ProxyNegotiator *proxy_http_new(const ProxyNegotiatorVT *vt)
{
    HttpProxyNegotiator *s = snew(HttpProxyNegotiator);
    s->pn.vt = vt;
    s->crLine = 0;
    s->line = strbuf_new();
    return &s->pn;
}

static void proxy_http_free(ProxyNegotiator *pn)
{
    HttpProxyNegotiator *s = container_of(pn, HttpProxyNegotiator, pn);
    strbuf_free(s->line);
    sfree(s);
}

static void proxy_http_process_queue(ProxyNegotiator *pn)
{
    HttpProxyNegotiator *s = container_of(pn, HttpProxyNegotiator, pn);

    crBegin(s->crLine);

    /*
     * Standard prefix for the HTTP CONNECT request.
     */
    {
        char dest[512];
        sk_getaddr(pn->ps->remote_addr, dest, lenof(dest));
        put_fmt(pn->output,
                "CONNECT %s:%d HTTP/1.1\r\n"
                "Host: %s:%d\r\n",
                dest, pn->ps->remote_port, dest, pn->ps->remote_port);
    }

    /*
     * Optionally send an HTTP Basic auth header with the username and
     * password.
     */
    {
        const char *username = conf_get_str(pn->ps->conf, CONF_proxy_username);
        const char *password = conf_get_str(pn->ps->conf, CONF_proxy_password);
        if (username[0] || password[0]) {
            put_datalit(pn->output, "Proxy-Authorization: Basic ");

            char *base64_input = dupcat(username, ":", password);
            char base64_output[4];
            for (size_t i = 0, e = strlen(base64_input); i < e; i += 3) {
                base64_encode_atom((const unsigned char *)base64_input + i,
                                   e-i > 3 ? 3 : e-i, base64_output);
                put_data(pn->output, base64_output, 4);
            }
            burnstr(base64_input);
            smemclr(base64_output, sizeof(base64_output));
            put_datalit(pn->output, "\r\n");
        }
    }

    /*
     * Blank line to terminate the HTTP request.
     */
    put_datalit(pn->output, "\r\n");
    crReturnV;

    /*
     * Read and parse the HTTP status line, and check if it's a 2xx
     * for success.
     */
    strbuf_clear(s->line);
    crMaybeWaitUntilV(read_line(pn->input, s->line, false));
    {
        int maj_ver, min_ver, status_pos = -1;
        sscanf(s->line->s, "HTTP/%d.%d %n", &maj_ver, &min_ver, &status_pos);

        /* If status_pos is still -1 then the sscanf didn't get right
         * to the end of the string */
        if (status_pos == -1) {
            pn->error = dupstr("HTTP response was absent or malformed");
            crStopV;
        }

        if (s->line->s[status_pos] != '2') {
            pn->error = dupprintf("HTTP response %s", s->line->s + status_pos);
            crStopV;
        }
    }

    /*
     * Read and skip the rest of the HTTP response headers, terminated
     * by a blank line.
     */
    do {
        strbuf_clear(s->line);
        crMaybeWaitUntilV(read_line(pn->input, s->line, true));
    } while (s->line->len > 0);

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
