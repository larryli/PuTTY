/*
 * Handy wrappers around GetDlgItemText (A and W) which don't make you
 * invent an arbitrary length limit on the output string. Returned
 * string is dynamically allocated; caller must free.
 */

#include "putty.h"

char *GetDlgItemText_alloc(HWND hwnd, int id)
{
    char *ret = NULL;
    size_t size = 0;

    do {
        sgrowarray_nm(ret, size, size);
        GetDlgItemText(hwnd, id, ret, size);
    } while (!memchr(ret, '\0', size-1));

    return ret;
}

wchar_t *GetDlgItemTextW_alloc(HWND hwnd, int id)
{
    wchar_t *ret = NULL;
    size_t size = 0;

    do {
        sgrowarray_nm(ret, size, size);
        GetDlgItemTextW(hwnd, id, ret, size);
    } while (!memchr(ret, '\0', size-1));

    return ret;
}
