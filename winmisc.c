/*
 * winmisc.c: miscellaneous Windows-specific things.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "putty.h"

void platform_get_x11_auth(char *display, int *proto,
                           unsigned char *data, int *datalen)
{
    /* We don't support this at all under Windows. */
}
