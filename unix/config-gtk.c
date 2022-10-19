/*
 * config-gtk.c - the GTK-specific parts of the PuTTY configuration
 * box.
 */

#include <assert.h>
#include <stdlib.h>

#include "putty.h"
#include "dialog.h"
#include "storage.h"

static void about_handler(union control *ctrl, dlgparam *dlg,
                          void *data, int event)
{
    if (event == EVENT_ACTION) {
        about_box(ctrl->generic.context.p);
    }
}

void gtk_setup_config_box(struct controlbox *b, bool midsession, void *win)
{
    struct controlset *s, *s2;
    union control *c;
    int i;

    if (!midsession) {
        /*
         * Add the About button to the standard panel.
         */
        s = ctrl_getset(b, "", "", "");
        c = ctrl_pushbutton(s, "关于(A)", 'a', HELPCTX(no_help),
                            about_handler, P(win));
        c->generic.column = 0;
    }

    /*
     * GTK makes it rather easier to put the scrollbar on the left
     * than Windows does!
     */
    s = ctrl_getset(b, "窗口", "scrollback",
                    "设置窗口回滚");
    ctrl_checkbox(s, "滚动条显示在左侧(L)", 'l',
                  HELPCTX(no_help),
                  conf_checkbox_handler,
                  I(CONF_scrollbar_on_left));
    /*
     * Really this wants to go just after `Display scrollbar'. See
     * if we can find that control, and do some shuffling.
     */
    for (i = 0; i < s->ncontrols; i++) {
        c = s->ctrls[i];
        if (c->generic.type == CTRL_CHECKBOX &&
            c->generic.context.i == CONF_scrollbar) {
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
    s2 = ctrl_getset(b, "窗口/外观", "font",
                     "字体设置");
    /* Remove this controlset from b. */
    for (i = 0; i < b->nctrlsets; i++) {
        if (b->ctrlsets[i] == s2) {
            memmove(b->ctrlsets+i, b->ctrlsets+i+1,
                    (b->nctrlsets-i-1) * sizeof(*b->ctrlsets));
            b->nctrlsets--;
            ctrl_free_set(s2);
            break;
        }
    }
    ctrl_settitle(b, "窗口/字体", "设置字体使用选项");
    s = ctrl_getset(b, "窗口/字体", "font",
                    "用于显示非粗体文本的字体");
    ctrl_fontsel(s, "用于普通文本的字体(F)", 'f',
                 HELPCTX(no_help),
                 conf_fontsel_handler, I(CONF_font));
    ctrl_fontsel(s, "用于宽 (CJK) 文本的字体(W)", 'w',
                 HELPCTX(no_help),
                 conf_fontsel_handler, I(CONF_widefont));
    s = ctrl_getset(b, "窗口/字体", "fontbold",
                    "用于显示非体文本的字体");
    ctrl_fontsel(s, "用于粗体文本的字体(B)", 'b',
                 HELPCTX(no_help),
                 conf_fontsel_handler, I(CONF_boldfont));
    ctrl_fontsel(s, "用于粗体宽文本的字体(I)", 'i',
                 HELPCTX(no_help),
                 conf_fontsel_handler, I(CONF_wideboldfont));
    ctrl_checkbox(s, "使用阴影粗体替代粗体字体(U)", 'u',
                  HELPCTX(no_help),
                  conf_checkbox_handler,
                  I(CONF_shadowbold));
    ctrl_text(s, "（请注意，粗体字体或阴影粗体仅"
              "在未设置更改文本颜色请求粗体时使"
              "用。）",
              HELPCTX(no_help));
    ctrl_editbox(s, "阴影加粗的水平偏移(Z):", 'z', 20,
                 HELPCTX(no_help), conf_editbox_handler,
                 I(CONF_shadowboldoffset), I(-1));

    /*
     * Markus Kuhn feels, not totally unreasonably, that it's good
     * for all applications to shift into UTF-8 mode if they notice
     * that they've been started with a LANG setting dictating it,
     * so that people don't have to keep remembering a separate
     * UTF-8 option for every application they use. Therefore,
     * here's an override option in the Translation panel.
     */
    s = ctrl_getset(b, "窗口/转换", "trans",
                    "接收数据时字符集转换");
    ctrl_checkbox(s, "区域设置使用 UTF-8 字符集(L)", 'l',
                  HELPCTX(translation_utf8_override),
                  conf_checkbox_handler,
                  I(CONF_utf8_override));

#ifdef OSX_META_KEY_CONFIG
    /*
     * On OS X, there are multiple reasonable opinions about whether
     * Option or Command (or both, or neither) should act as a Meta
     * key, or whether they should have their normal OS functions.
     */
    s = ctrl_getset(b, "终端/键盘", "meta",
                    "选择 Meta 键:");
    ctrl_checkbox(s, "选项键用作 Meta 键(P)", 'p',
                  HELPCTX(no_help),
                  conf_checkbox_handler, I(CONF_osx_option_meta));
    ctrl_checkbox(s, "命令键用作 Meta 键", 'm',
                  HELPCTX(no_help),
                  conf_checkbox_handler, I(CONF_osx_command_meta));
#endif

    if (!midsession) {
        /*
         * Allow the user to specify the window class as part of the saved
         * configuration, so that they can have their window manager treat
         * different kinds of PuTTY and pterm differently if they want to.
         */
        s = ctrl_getset(b, "窗口/行为", "x11",
                        "X Window 系统设置");
        ctrl_editbox(s, "窗口类名:", 'z', 50,
                     HELPCTX(no_help), conf_editbox_handler,
                     I(CONF_winclass), I(1));
    }
}
