/*
 * config-unix.c - the Unix-specific parts of the PuTTY configuration
 * box.
 */

#include <assert.h>
#include <stdlib.h>

#include "putty.h"
#include "dialog.h"
#include "storage.h"

void unix_setup_config_box(struct controlbox *b, bool midsession, int protocol)
{
    struct controlset *s;
    union control *c;

    /*
     * The Conf structure contains two Unix-specific elements which
     * are not configured in here: stamp_utmp and login_shell. This
     * is because pterm does not put up a configuration box right at
     * the start, which is the only time when these elements would
     * be useful to configure.
     */

    /*
     * On Unix, we don't have a drop-down list for the printer
     * control.
     */
    s = ctrl_getset(b, "Terminal", "printing", "Remote-controlled printing");
    assert(s->ncontrols == 1 && s->ctrls[0]->generic.type == CTRL_EDITBOX);
    s->ctrls[0]->editbox.has_list = false;

    /*
     * Unix supports a local-command proxy.
     */
    if (!midsession) {
        int i;
        s = ctrl_getset(b, "Connection/Proxy", "basics", NULL);
        for (i = 0; i < s->ncontrols; i++) {
            c = s->ctrls[i];
            if (c->generic.type == CTRL_RADIO &&
                c->generic.handler == proxy_type_handler) {
                c->generic.context.i |= PROXY_UI_FLAG_LOCAL;
                break;
            }
        }
    }
}
