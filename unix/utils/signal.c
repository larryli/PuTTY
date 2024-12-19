/*
 * PuTTY's wrapper on signal(2).
 *
 * Calling signal() is non-portable, as it varies in meaning between
 * platforms and depending on feature macros, and has stupid semantics
 * at least some of the time.
 *
 * This function provides the same interface as the libc function, but
 * provides consistent semantics. It assumes POSIX semantics for
 * sigaction() (so you might need to do some more work if you port to
 * something ancient like SunOS 4).
 */

#include <signal.h>

#include "defs.h"

void (*putty_signal(int sig, void (*func)(int)))(int)
{
    struct sigaction sa;
    struct sigaction old;

    sa.sa_handler = func;
    if (sigemptyset(&sa.sa_mask) < 0)
        return SIG_ERR;
    sa.sa_flags = SA_RESTART;
    if (sigaction(sig, &sa, &old) < 0)
        return SIG_ERR;
    return old.sa_handler;
}
