/*
 * Copyright (c) 1999 Simon Tatham
 * Copyright (c) 1999 Ben Harris
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* PuTTY test backends */

#include <stdio.h>
#include <stdlib.h>

#include "putty.h"

static char *loop_init(const BackendVtable *, Seat *, Backend **, LogContext *,
                       Conf *, const char *, int, char **, bool, bool);
static void loop_free(Backend *);
static void null_reconfig(Backend *, Conf *);
static void null_send(Backend *, const char *, size_t);
static void loop_send(Backend *, const char *, size_t);
static size_t null_sendbuffer(Backend *);
static size_t loop_sendbuffer(Backend *);
static void null_size(Backend *, int, int);
static void null_special(Backend *, SessionSpecialCode, int);
static const SessionSpecial *null_get_specials(Backend *);
static bool null_connected(Backend *);
static int null_exitcode(Backend *);
static bool null_sendok(Backend *);
static bool null_ldisc(Backend *, int);
static void null_provide_ldisc(Backend *, Ldisc *);
static void null_unthrottle(Backend *, size_t);
static int null_cfg_info(Backend *);

const BackendVtable null_backend = {
    .init = loop_init,
    .free = loop_free,
    .reconfig = null_reconfig,
    .send = null_send,
    .sendbuffer = null_sendbuffer,
    .size = null_size,
    .special = null_special,
    .get_specials = null_get_specials,
    .connected = null_connected,
    .exitcode = null_exitcode,
    .sendok = null_sendok,
    .ldisc_option_state = null_ldisc,
    .provide_ldisc = null_provide_ldisc,
    .unthrottle = null_unthrottle,
    .cfg_info = null_cfg_info,
    .id = "null",
    .displayname_tc = "Null",
    .displayname_lc = "null",
    .protocol = -1,
    .default_port = 0,
};

const BackendVtable loop_backend = {
    .init = loop_init,
    .free = loop_free,
    .reconfig = null_reconfig,
    .send = loop_send,
    .sendbuffer = loop_sendbuffer,
    .size = null_size,
    .special = null_special,
    .get_specials = null_get_specials,
    .connected = null_connected,
    .exitcode = null_exitcode,
    .sendok = null_sendok,
    .ldisc_option_state = null_ldisc,
    .provide_ldisc = null_provide_ldisc,
    .unthrottle = null_unthrottle,
    .cfg_info = null_cfg_info,
    .id = "loop",
    .displayname_tc = "Loop",
    .displayname_lc = "loop",
    .protocol = -1,
    .default_port = 0,
};

struct loop_state {
    Seat *seat;
    Backend backend;
    size_t sendbuffer;
};

static char *loop_init(const BackendVtable *vt, Seat *seat,
                       Backend **backend_handle, LogContext *logctx,
                       Conf *conf, const char *host, int port,
                       char **realhost, bool nodelay, bool keepalive) {
    struct loop_state *st = snew(struct loop_state);

    /* No local authentication phase in this protocol */
    seat_set_trust_status(seat, false);

    st->seat = seat;
    st->backend.vt = vt;
    *backend_handle = &st->backend;

    *realhost = dupstr(host);

    return NULL;
}

static void loop_free(Backend *be)
{
    struct loop_state *st = container_of(be, struct loop_state, backend);

    sfree(st);
}

static void null_reconfig(Backend *be, Conf *conf) {

}

static void null_send(Backend *be, const char *buf, size_t len) {

}

static void loop_send(Backend *be, const char *buf, size_t len) {
    struct loop_state *st = container_of(be, struct loop_state, backend);

    st->sendbuffer = seat_output(st->seat, 0, buf, len);
}

static size_t null_sendbuffer(Backend *be) {

    return 0;
}

static size_t loop_sendbuffer(Backend *be) {
    struct loop_state *st = container_of(be, struct loop_state, backend);

    return st->sendbuffer;
}

static void null_size(Backend *be, int width, int height) {

}

static void null_special(Backend *be, SessionSpecialCode code, int arg) {

}

static const SessionSpecial *null_get_specials (Backend *be) {

    return NULL;
}

static bool null_connected(Backend *be) {

    return false;
}

static int null_exitcode(Backend *be) {

    return 0;
}

static bool null_sendok(Backend *be) {

    return true;
}

static void null_unthrottle(Backend *be, size_t backlog) {

}

static bool null_ldisc(Backend *be, int option) {

    return false;
}

static void null_provide_ldisc (Backend *be, Ldisc *ldisc) {

}

static int null_cfg_info(Backend *be)
{
    return 0;
}
