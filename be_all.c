/*
 * Linking module for PuTTY proper: list the available backends
 * including ssh.
 */

#include <stdio.h>
#include "putty.h"

#ifdef TELNET_DEFAULT
const int be_default_protocol = PROT_TELNET;
#else
const int be_default_protocol = PROT_SSH;
#endif

struct backend_list backends[] = {
    {PROT_SSH, "ssh", &ssh_backend},
    {PROT_TELNET, "telnet", &telnet_backend},
    {PROT_RLOGIN, "rlogin", &rlogin_backend},
    {PROT_RAW, "raw", &raw_backend},
    {0, NULL}
};
