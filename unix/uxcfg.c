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
	about_box();
    }
}

void unix_setup_config_box(struct controlbox *b, int midsession)
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
			    about_handler, P(NULL));
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
    ctrl_text(s, "If you leave the bold font selectors blank, bold text"
              " will be displayed by overprinting (\"shadow bold\"). Note"
              " that this only applies if you have not requested bolding"
              " to be done by changing the text colour.",
              HELPCTX(no_help));
    ctrl_editbox(s, "Horizontal offset for shadow bold:", 'z', 20,
		 HELPCTX(no_help), dlg_stdeditbox_handler,
                 I(offsetof(Config,shadowboldoffset)), I(-1));
}
