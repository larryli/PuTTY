/*
 * nocmdline.c - stubs in applications which don't do the
 * standard(ish) PuTTY tools' command-line parsing
 */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "putty.h"

/*
 * Stub version of the function in cmdline.c which provides the
 * password to SSH authentication by remembering it having been passed
 * as a command-line option. If we're not doing normal command-line
 * handling, then there is no such option, so that function always
 * returns failure.
 */
SeatPromptResult cmdline_get_passwd_input(
    prompts_t *p, cmdline_get_passwd_input_state *state, bool restartable)
{
    return SPR_INCOMPLETE;
}
