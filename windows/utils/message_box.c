/*
 * Enhanced version of the MessageBox API function. Permits enabling a
 * Help button by setting helpctxid to a context id in the help file
 * relevant to this dialog box. Also permits setting the 'utf8' flag
 * to indicate that the char strings given as 'text' and 'caption' are
 * encoded in UTF-8 rather than the system code page.
 */

#include "putty.h"

static HWND message_box_owner;

/* Callback function to launch context help. */
static VOID CALLBACK message_box_help_callback(LPHELPINFO lpHelpInfo)
{
    const char *context = NULL;
#define CHECK_CTX(name) \
    do { \
        if (lpHelpInfo->dwContextId == WINHELP_CTXID_ ## name) \
            context = WINHELP_CTX_ ## name; \
    } while (0)
    CHECK_CTX(errors_hostkey_absent);
    CHECK_CTX(errors_hostkey_changed);
    CHECK_CTX(errors_cantloadkey);
    CHECK_CTX(option_cleanup);
    CHECK_CTX(pgp_fingerprints);
#undef CHECK_CTX
    if (context)
        launch_help(message_box_owner, context);
}

int message_box(HWND owner, LPCTSTR text, LPCTSTR caption, DWORD style,
                bool utf8, DWORD helpctxid)
{
    MSGBOXPARAMSW mbox;

    /*
     * We use MessageBoxIndirect() because it allows us to specify a
     * callback function for the Help button.
     */
    mbox.cbSize = sizeof(mbox);
    /* Assumes the globals `hinst' and `hwnd' have sensible values. */
    mbox.hInstance = hinst;
    mbox.dwLanguageId = LANG_NEUTRAL;

    mbox.hwndOwner = message_box_owner = owner;

    wchar_t *wtext, *wcaption;
    if (utf8) {
        wtext = decode_utf8_to_wide_string(text);
        wcaption = decode_utf8_to_wide_string(caption);
    } else {
        wtext = dup_mb_to_wc(DEFAULT_CODEPAGE, text);
        wcaption = dup_mb_to_wc(DEFAULT_CODEPAGE, caption);
    }
    mbox.lpszText = wtext;
    mbox.lpszCaption = wcaption;

    mbox.dwStyle = style;

    mbox.dwContextHelpId = helpctxid;
    if (helpctxid != 0 && has_help()) mbox.dwStyle |= MB_HELP;
    mbox.lpfnMsgBoxCallback = &message_box_help_callback;

    int toret = MessageBoxIndirectW(&mbox);

    sfree(wtext);
    sfree(wcaption);

    return toret;
}
