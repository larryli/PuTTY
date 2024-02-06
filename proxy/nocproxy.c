/*
 * Routines to refuse to do cryptographic interaction with proxies
 * in PuTTY. This is a stub implementation of the same interfaces
 * provided by cproxy.c, for use in PuTTYtel.
 */

#include <assert.h>
#include <ctype.h>
#include <string.h>

#include "putty.h"
#include "network.h"
#include "proxy.h"

const bool socks5_chap_available = false;
const bool http_digest_available = false;

strbuf *chap_response(ptrlen challenge, ptrlen password)
{
    unreachable("CHAP is not built into this binary");
}

/* dummy arrays to prevent link error */
const char *const httphashnames[] = { NULL };
const bool httphashaccepted[] = { false };

void http_digest_response(BinarySink *bs, ptrlen username, ptrlen password,
                          ptrlen realm, ptrlen method, ptrlen uri, ptrlen qop,
                          ptrlen nonce, ptrlen opaque, uint32_t nonce_count,
                          HttpDigestHash hash, bool hash_username)
{
    unreachable("HTTP DIGEST is not built into this binary");
}
