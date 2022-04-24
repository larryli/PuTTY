/*
 * Centre a window on the screen. Used to position the main config box.
 */

#include "putty.h"

void centre_window(HWND win)
{
    RECT rd, rw;

    if (!GetWindowRect(GetDesktopWindow(), &rd))
        return;
    if (!GetWindowRect(win, &rw))
        return;

    MoveWindow(win,
               (rd.right + rd.left + rw.left - rw.right) / 2,
               (rd.bottom + rd.top + rw.top - rw.bottom) / 2,
               rw.right - rw.left, rw.bottom - rw.top, true);
}
