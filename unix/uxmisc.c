/*
 * PuTTY miscellaneous Unix stuff
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

#include "putty.h"

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

Filename filename_from_str(const char *str)
{
    Filename ret;
    strncpy(ret.path, str, sizeof(ret.path));
    ret.path[sizeof(ret.path)-1] = '\0';
    return ret;
}

const char *filename_to_str(const Filename *fn)
{
    return fn->path;
}

int filename_equal(Filename f1, Filename f2)
{
    return !strcmp(f1.path, f2.path);
}

int filename_is_null(Filename fn)
{
    return !*fn.path;
}

#ifdef DEBUG
static FILE *debug_fp = NULL;

void dputs(char *buf)
{
    if (!debug_fp) {
	debug_fp = fopen("debug.log", "w");
    }

    write(1, buf, strlen(buf));

    fputs(buf, debug_fp);
    fflush(debug_fp);
}
#endif
