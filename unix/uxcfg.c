/*
 * uxcfg.c - the Unix-specific parts of the PuTTY configuration
 * box.
 */

#include <assert.h>
#include <stdlib.h>

#include "putty.h"
#include "dialog.h"
#include "storage.h"

static void about_handler(union control *ctrl, void *dlg,
			  void *data, int event)
{
    if (event == EVENT_ACTION) {
	about_box(ctrl->generic.context.p);
    }
}

void unix_setup_config_box(struct controlbox *b, int midsession, void *win)
{
    struct controlset *s, *s2;
    union control *c;
    int i;

    if (!midsession) {
	/*
	 * Add the About button to the standard panel.
	 */
	s = ctrl_getset(b, "", "", "");
	c = ctrl_pushbutton(s, "About", 'a', HELPCTX(no_help),
			    about_handler, P(win));
	c->generic.column = 0;
    }

    /*
     * The Config structure contains two Unix-specific elements
     * which are not configured in here: stamp_utmp and
     * login_shell. This is because pterm does not put up a
     * configuration box right at the start, which is the only time
     * when these elements would be useful to configure.
     */

    /*
     * On Unix, we don't have a drop-down list for the printer
     * control.
     */
    s = ctrl_getset(b, "Terminal", "printing", "Remote-controlled printing");
    assert(s->ncontrols == 1 && s->ctrls[0]->generic.type == CTRL_EDITBOX);
    s->ctrls[0]->editbox.has_list = 0;

    /*
     * GTK makes it rather easier to put the scrollbar on the left
     * than Windows does!
     */
    s = ctrl_getset(b, "Window", "scrollback",
		    "Control the scrollback in the window");
    ctrl_checkbox(s, "Scrollbar on left", 'l',
		  HELPCTX(no_help),
		  dlg_stdcheckbox_handler,
                  I(offsetof(Config,scrollbar_on_left)));
    /*
     * Really this wants to go just after `Display scrollbar'. See
     * if we can find that control, and do some shuffling.
     */
    for (i = 0; i < s->ncontrols; i++) {
        c = s->ctrls[i];
        if (c->generic.type == CTRL_CHECKBOX &&
            c->generic.context.i == offsetof(Config,scrollbar)) {
            /*
             * Control i is the scrollbar checkbox.
             * Control s->ncontrols-1 is the scrollbar-on-left one.
             */
            if (i < s->ncontrols-2) {
                c = s->ctrls[s->ncontrols-1];
                memmove(s->ctrls+i+2, s->ctrls+i+1,
                        (s->ncontrols-i-2)*sizeof(union control *));
                s->ctrls[i+1] = c;
            }
            break;
        }
    }

    /*
     * X requires three more fonts: bold, wide, and wide-bold; also
     * we need the fiddly shadow-bold-offset control. This would
     * make the Window/Appearance panel rather unwieldy and large,
     * so I think the sensible thing here is to _move_ this
     * controlset into a separate Window/Fonts panel!
     */
    s2 = ctrl_getset(b, "Window/Appearance", "font",
                     "Font settings");
    /* Remove this controlset from b. */
    for (i = 0; i < b->nctrlsets; i++) {
        if (b->ctrlsets[i] == s2) {
            memmove(b->ctrlsets+i, b->ctrlsets+i+1,
                    (b->nctrlsets-i-1) * sizeof(*b->ctrlsets));
            b->nctrlsets--;
            break;
        }
    }
    ctrl_settitle(b, "Window/Fonts", "Options controlling font usage");
    s = ctrl_getset(b, "Window/Fonts", "font",
                    "Fonts for displaying non-bold text");
    ctrl_fontsel(s, "Font used for ordinary text", 'f',
		 HELPCTX(no_help),
		 dlg_stdfontsel_handler, I(offsetof(Config,font)));
    ctrl_fontsel(s, "Font used for wide (CJK) text", 'w',
		 HELPCTX(no_help),
		 dlg_stdfontsel_handler, I(offsetof(Config,widefont)));
    s = ctrl_getset(b, "Window/Fonts", "fontbold",
                    "Fonts for displaying bolded text");
    ctrl_fontsel(s, "Font used for bolded text", 'b',
		 HELPCTX(no_help),
		 dlg_stdfontsel_handler, I(offsetof(Config,boldfont)));
    ctrl_fontsel(s, "Font used for bold wide text", 'i',
		 HELPCTX(no_help),
		 dlg_stdfontsel_handler, I(offsetof(Config,wideboldfont)));
    ctrl_checkbox(s, "Use shadow bold instead of bold fonts", 'u',
		  HELPCTX(no_help),
		  dlg_stdcheckbox_handler,
		  I(offsetof(Config,shadowbold)));
    ctrl_text(s, "(Note that bold fonts or shadow bolding are only"
	      " used if you have not requested bolding to be done by"
	      " changing the text colour.)",
              HELPCTX(no_help));
    ctrl_editbox(s, "Horizontal offset for shadow bold:", 'z', 20,
		 HELPCTX(no_help), dlg_stdeditbox_handler,
                 I(offsetof(Config,shadowboldoffset)), I(-1));

    /*
     * Markus Kuhn feels, not totally unreasonably, that it's good
     * for all applications to shift into UTF-8 mode if they notice
     * that they've been started with a LANG setting dictating it,
     * so that people don't have to keep remembering a separate
     * UTF-8 option for every application they use. Therefore,
     * here's an override option in the Translation panel.
     */
    s = ctrl_getset(b, "Window/Translation", "trans",
		    "Character set translation on received data");
    ctrl_checkbox(s, "Override with UTF-8 if locale says so", 'l',
		  HELPCTX(translation_utf8_override),
		  dlg_stdcheckbox_handler,
		  I(offsetof(Config,utf8_override)));

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
		c->generic.context.i == offsetof(Config, proxy_type)) {
		assert(c->generic.handler == dlg_stdradiobutton_handler);
		c->radio.nbuttons++;
		c->radio.buttons =
		    sresize(c->radio.buttons, c->radio.nbuttons, char *);
		c->radio.buttons[c->radio.nbuttons-1] =
		    dupstr("Local");
		c->radio.buttondata =
		    sresize(c->radio.buttondata, c->radio.nbuttons, intorptr);
		c->radio.buttondata[c->radio.nbuttons-1] = I(PROXY_CMD);
		break;
	    }
	}

	for (i = 0; i < s->ncontrols; i++) {
	    c = s->ctrls[i];
	    if (c->generic.type == CTRL_EDITBOX &&
		c->generic.context.i ==
		offsetof(Config, proxy_telnet_command)) {
		assert(c->generic.handler == dlg_stdeditbox_handler);
		sfree(c->generic.label);
		c->generic.label = dupstr("Telnet command, or local"
					  " proxy command");
		break;
	    }
	}
    }

}
