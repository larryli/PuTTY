/*
 * pterm main program.
 */

#include <stdio.h>

#include "putty.h"

Backend *select_backend(Config *cfg)
{
    return &pty_backend;
}

int cfgbox(Config *cfg)
{
    return 1;			       /* no-op in pterm */
}

char *make_default_wintitle(char *hostname)
{
    return dupstr("pterm");
}

int main(int argc, char **argv)
{
    extern int pt_main(int argc, char **argv);
    extern void pty_pre_init(void);    /* declared in pty.c */

    pty_pre_init();

    return pt_main(argc, argv);
}
