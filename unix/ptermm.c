/*
 * pterm main program.
 */

#include <stdio.h>
#include <stdlib.h>

#include "putty.h"

const char *const appname = "pterm";

/*
 * Another bunch of temporary stub functions. These ones will want
 * removing by means of implementing them properly: libcharset
 * should invent its own sensible format for codepage names and a
 * means of enumerating them, and printer_enum needs to be dealt
 * with somehow or other too.
 */

char *cp_name(int codepage)
{
    return "";
}
char *cp_enumerate(int index)
{
    return NULL;
}
int decode_codepage(char *cp_name)
{
    return -2;
}

printer_enum *printer_start_enum(int *nprinters_ptr) {
    *nprinters_ptr = 0;
    return NULL;
}
char *printer_get_name(printer_enum *pe, int i) { return NULL;
}
void printer_finish_enum(printer_enum *pe) { }

Backend *select_backend(Config *cfg)
{
    return &pty_backend;
}

int cfgbox(Config *cfg)
{
    return 1;			       /* no-op in pterm */
}

void cleanup_exit(int code)
{
    exit(code);
}

int process_nonoption_arg(char *arg, Config *cfg)
{
    return 0;                          /* pterm doesn't have any. */
}

char *make_default_wintitle(char *hostname)
{
    return dupstr("pterm");
}

int main(int argc, char **argv)
{
    extern int pt_main(int argc, char **argv);
    extern void pty_pre_init(void);    /* declared in pty.c */

    cmdline_tooltype = TOOLTYPE_NONNETWORK;

    pty_pre_init();

    return pt_main(argc, argv);
}
