/*
 * Linking module for PSCP: list no available backends. This is
 * only present to satisfy linker requirements. I should really
 * untangle the whole lot a bit better.
 */

#include <windows.h>
#include <stdio.h>
#include "putty.h"

struct backend_list backends[] = {
    {0, NULL}
};
