/*
 * Linking module for PSCP: list the available backends, but
 * without accompanying function suites. Used only for name
 * lookups.
 */

#include <windows.h>
#ifndef AUTO_WINSOCK
#ifdef WINSOCK_TWO
#include <winsock2.h>
#else
#include <winsock.h>
#endif
#endif
#include <stdio.h>
#include "putty.h"

struct backend_list backends[] = {
    {PROT_SSH, "ssh", NULL},
    {PROT_TELNET, "telnet", NULL},
    {PROT_RAW, "raw", NULL},
    {0, NULL}
};
