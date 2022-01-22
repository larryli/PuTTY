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
     * Unix supports a local-command proxy. This also means we must
     * adjust the text on the `Telnet command' control.
     */
    if (!midsession) {
        int i;
        s = ctrl_getset(b, "Connection/Proxy", "basics", NULL);
        for (i = 0; i < s->ncontrols; i++) {
            c = s->ctrls[i];
            if (c->generic.type == CTRL_RADIO &&
                c->generic.context.i == CONF_proxy_type) {
                assert(c->generic.handler == conf_radiobutton_handler);
                c->radio.nbuttons++;
                c->radio.buttons =
                    sresize(c->radio.buttons, c->radio.nbuttons, char *);
                c->radio.buttons[c->radio.nbuttons-1] =
                    dupstr("Local");
                c->radio.buttondata =
                    sresize(c->radio.buttondata, c->radio.nbuttons, intorptr);
                c->radio.buttondata[c->radio.nbuttons-1] = I(PROXY_CMD);
                if (c->radio.ncolumns < 4)
                    c->radio.ncolumns = 4;
                break;
            }
        }

        for (i = 0; i < s->ncontrols; i++) {
            c = s->ctrls[i];
            if (c->generic.type == CTRL_EDITBOX &&
                c->generic.context.i == CONF_proxy_telnet_command) {
                assert(c->generic.handler == conf_editbox_handler);
                sfree(c->generic.label);
                c->generic.label = dupstr("Telnet command, or local"
                                          " proxy command");
                break;
            }
        }
    }
}
