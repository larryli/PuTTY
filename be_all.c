/*
 * Linking module for PuTTY proper: list the available backends
 * including ssh.
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
    {PROT_SSH, "ssh", &ssh_backend},
    {PROT_TELNET, "telnet", &telnet_backend},
    {PROT_RAW, "raw", &raw_backend},
    {0, NULL}
};
