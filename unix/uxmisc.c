/*
 * PuTTY miscellaneous Unix stuff
 */

#include <stdio.h>
#include <sys/time.h>

unsigned long getticks(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    /*
     * This will wrap around approximately every 4000 seconds, i.e.
     * just over an hour, which is more than enough.
     */
    return tv.tv_sec * 1000000 + tv.tv_usec;
}
