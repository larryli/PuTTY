#include "putty.h"

#if HAVE_DWMAPI_H

#include <dwmapi.h>

char *save_screenshot(HWND hwnd, const char *outfile)
{
    HDC dcWindow = NULL, dcSave = NULL;
    HBITMAP bmSave = NULL;
    uint8_t *buffer = NULL;
    char *err = NULL;

    static HMODULE dwmapi_module;
    DECL_WINDOWS_FUNCTION(static, HRESULT, DwmGetWindowAttribute,
                          (HWND, DWORD, PVOID, DWORD));

    if (!dwmapi_module) {
        dwmapi_module = load_system32_dll("dwmapi.dll");
        GET_WINDOWS_FUNCTION(dwmapi_module, DwmGetWindowAttribute);
    }

    dcWindow = GetDC(NULL);
    if (!dcWindow) {
        err = dupprintf("GetDC(window): %s", win_strerror(GetLastError()));
        goto out;
    }

    int x, y, w, h;
    RECT wr;

    /* Use DwmGetWindowAttribute in place of GetWindowRect to exclude
     * drop shadow, otherwise we get a load of unwanted desktop
     * background under the shadow */
    if (p_DwmGetWindowAttribute &&
        0 <= p_DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                                     &wr, sizeof(wr))) {
        x = wr.left;
        y = wr.top;
        w = wr.right - wr.left;
        h = wr.bottom - wr.top;
    } else {
        BITMAP bmhdr;
        memset(&bmhdr, 0, sizeof(bmhdr));
        GetObject(GetCurrentObject(dcWindow, OBJ_BITMAP),
                  sizeof(bmhdr), &bmhdr);
        x = y = 0;
        w = bmhdr.bmWidth;
        h = bmhdr.bmHeight;
    }

    dcSave = CreateCompatibleDC(dcWindow);
    if (!dcSave) {
        err = dupprintf("CreateCompatibleDC(desktop window dc): %s",
                        win_strerror(GetLastError()));
        goto out;
    }

    bmSave = CreateCompatibleBitmap(dcWindow, w, h);
    if (!bmSave) {
        err = dupprintf("CreateCompatibleBitmap: %s",
                        win_strerror(GetLastError()));
        goto out;
    }

    if (!SelectObject(dcSave, bmSave)) {
        err = dupprintf("SelectObject: %s", win_strerror(GetLastError()));
        goto out;
    }

    if (!BitBlt(dcSave, 0, 0, w, h, dcWindow, x, y, SRCCOPY)) {
        err = dupprintf("BitBlt: %s", win_strerror(GetLastError()));
        goto out;
    }

    BITMAPINFO bmInfo;
    memset(&bmInfo, 0, sizeof(bmInfo));
    bmInfo.bmiHeader.biSize = sizeof(bmInfo.bmiHeader);
    bmInfo.bmiHeader.biWidth = w;
    bmInfo.bmiHeader.biHeight = h;
    bmInfo.bmiHeader.biPlanes = 1;
    bmInfo.bmiHeader.biBitCount = 32;
    bmInfo.bmiHeader.biCompression = BI_RGB;

    size_t bmPixels = (size_t)w*h, bmBytes = bmPixels * 4;
    buffer = snewn(bmBytes, uint8_t);

    if (!GetDIBits(dcWindow, bmSave, 0, h, buffer, &bmInfo, DIB_RGB_COLORS))
        err = dupprintf("GetDIBits (get data): %s",
                        win_strerror(GetLastError()));

    FILE *fp = fopen(outfile, "wb");
    if (!fp) {
        err = dupprintf("'%s': unable to open file", outfile);
        goto out;
    }

    BITMAPFILEHEADER bmFileHdr;
    bmFileHdr.bfType = 'B' | ('M' << 8);
    bmFileHdr.bfSize = sizeof(bmFileHdr) + sizeof(bmInfo.bmiHeader) + bmBytes;
    bmFileHdr.bfOffBits = sizeof(bmFileHdr) + sizeof(bmInfo.bmiHeader);
    fwrite((void *)&bmFileHdr, 1, sizeof(bmFileHdr), fp);
    fwrite((void *)&bmInfo.bmiHeader, 1, sizeof(bmInfo.bmiHeader), fp);
    fwrite((void *)buffer, 1, bmBytes, fp);
    fclose(fp);

  out:
    if (dcWindow)
        ReleaseDC(NULL, dcWindow);
    if (bmSave)
        DeleteObject(bmSave);
    if (dcSave)
        DeleteObject(dcSave);
    sfree(buffer);
    return err;
}

#else /* HAVE_DWMAPI_H */

/* Without <dwmapi.h> we can't get the right window rectangle */
char *save_screenshot(HWND hwnd, const char *outfile)
{
    return dupstr("Demo screenshots not compiled in to this build");
}

#endif /* HAVE_DWMAPI_H */
