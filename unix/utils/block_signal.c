/*
 * Handy function to block or unblock a signal, which does all the
 * messing about with sigset_t for you.
 */

#include <signal.h>
#include <stdlib.h>

#include "defs.h"

void block_signal(int sig, bool block_it)
{
    sigset_t ss;

    sigemptyset(&ss);
    sigaddset(&ss, sig);
    if (sigprocmask(block_it ? SIG_BLOCK : SIG_UNBLOCK, &ss, 0) < 0) {
        perror("sigprocmask");
        exit(1);
    }
}
