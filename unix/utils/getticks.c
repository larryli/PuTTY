/*
 * Implement getticks() for Unix.
 */

#include <time.h>
#include <sys/time.h>

#include "putty.h"

unsigned long getticks(void)
{
    /*
     * We want to use milliseconds rather than the microseconds or
     * nanoseconds given by the underlying clock functions, because we
     * need a decent number of them to fit into a 32-bit word so it
     * can be used for keepalives.
     */
#if HAVE_CLOCK_GETTIME && HAVE_CLOCK_MONOTONIC
    {
        /* Use CLOCK_MONOTONIC if available, so as to be unconfused if
         * the system clock changes. */
        struct timespec ts;
        if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
            return ts.tv_sec * TICKSPERSEC +
                ts.tv_nsec / (1000000000 / TICKSPERSEC);
    }
#endif
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return tv.tv_sec * TICKSPERSEC + tv.tv_usec / (1000000 / TICKSPERSEC);
    }
}
