/*
 * Routines to do cryptographic interaction with proxies in PuTTY.
 * This is in a separate module from proxy.c, so that it can be
 * conveniently removed in PuTTYtel by replacing this module with
 * the stub version nocproxy.c.
 */

#include <assert.h>
#include <ctype.h>
#include <string.h>

#include "putty.h"
#include "ssh.h" /* For MD5 support */
#include "network.h"
#include "proxy.h"
#include "marshal.h"

const bool socks5_chap_available = true;

strbuf *chap_response(ptrlen challenge, ptrlen password)
{
    strbuf *sb = strbuf_new_nm();
    const ssh2_macalg *alg = &ssh_hmac_md5;
    mac_simple(alg, password, challenge, strbuf_append(sb, alg->len));
    return sb;
}
