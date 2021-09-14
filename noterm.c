/*
 * Stubs of functions in terminal.c, for use in programs that don't
 * have a terminal.
 */

#include "putty.h"
#include "terminal.h"

void term_nopaste(Terminal *term)
{
}

int term_get_userpass_input(Terminal *term, prompts_t *p)
{
    return 0;
}
