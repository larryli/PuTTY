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

strbuf *chap_response(ptrlen challenge, ptrlen password)
{
    unreachable("CHAP is not built into this binary");
}
