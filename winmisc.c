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

Filename filename_from_str(char *str)
{
    Filename ret;
    strncpy(ret.path, str, sizeof(ret.path));
    ret.path[sizeof(ret.path)-1] = '\0';
    return ret;
}

char *filename_to_str(Filename fn)
{
    return fn.path;
}

int filename_equal(Filename f1, Filename f2)
{
    return !strcmp(f1.path, f2.path);
}

int filename_is_null(Filename fn)
{
    return !*fn.path;
}
