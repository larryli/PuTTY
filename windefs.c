/*
 * windefs.c: default settings that are specific to Windows.
 */

#include <windows.h>
#include <commctrl.h>

#include "winstuff.h"
#include "puttymem.h"

#include "putty.h"

char *platform_default_s(char *name)
{
    if (!strcmp(name, "Font"))
	return "Courier New";
    return NULL;
}

int platform_default_i(char *name, int def)
{
    if (!strcmp(name, "FontCharSet"))
	return ANSI_CHARSET;
    return def;
}
