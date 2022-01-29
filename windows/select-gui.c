/*
 * Implementation of do_select() for network.c to use, that uses
 * WSAAsyncSelect to convert network activity into window messages,
 * for integration into a GUI event loop.
 */

#include "putty.h"

static HWND winsel_hwnd = NULL;

void winselgui_set_hwnd(HWND hwnd)
{
    winsel_hwnd = hwnd;
}

void winselgui_clear_hwnd(void)
{
    winsel_hwnd = NULL;
}

const char *do_select(SOCKET skt, bool enable)
{
    int msg, events;
    if (enable) {
        msg = WM_NETEVENT;
        events = (FD_CONNECT | FD_READ | FD_WRITE |
                  FD_OOB | FD_CLOSE | FD_ACCEPT);
    } else {
        msg = events = 0;
    }

    assert(winsel_hwnd);

    if (p_WSAAsyncSelect(skt, winsel_hwnd, msg, events) == SOCKET_ERROR)
        return winsock_error_string(p_WSAGetLastError());

    return NULL;
}

struct wm_netevent_params {
    /* Used to pass data to wm_netevent_callback */
    WPARAM wParam;
    LPARAM lParam;
};

static void wm_netevent_callback(void *vctx)
{
    struct wm_netevent_params *params = (struct wm_netevent_params *)vctx;
    select_result(params->wParam, params->lParam);
    sfree(params);
}

void winselgui_response(WPARAM wParam, LPARAM lParam)
{
    /*
     * To protect against re-entrancy when Windows's recv()
     * immediately triggers a new WSAAsyncSelect window message, we
     * don't call select_result directly from this handler but instead
     * wait until we're back out at the top level of the message loop.
     */
    struct wm_netevent_params *params = snew(struct wm_netevent_params);
    params->wParam = wParam;
    params->lParam = lParam;
    queue_toplevel_callback(wm_netevent_callback, params);
}
