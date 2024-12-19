#include "putty.h"

struct Ldisc_tag {
    int dummy;
};

Ldisc *ldisc_create(Conf *conf, Terminal *term, Backend *backend, Seat *seat)
{
    Ldisc *ldisc = snew(Ldisc);
    memset(ldisc, 0, sizeof(Ldisc));
    return ldisc;
}

void ldisc_configure(Ldisc *ldisc, Conf *conf)
{
}

void ldisc_free(Ldisc *ldisc)
{
    sfree(ldisc);
}

void ldisc_echoedit_update(Ldisc *ldisc)
{
}

void ldisc_provide_userpass_le(Ldisc *ldisc, TermLineEditor *le)
{
}

void ldisc_check_sendok(Ldisc *ldisc)
{
}

void ldisc_send(Ldisc *ldisc, const void *vbuf, int len, bool interactive)
{
}
