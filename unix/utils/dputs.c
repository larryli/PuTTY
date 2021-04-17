/*
 * Implementation of dputs() for Unix.
 *
 * The debug messages are written to standard output, and also into a
 * file called debug.log.
 */

#include <unistd.h>

#include "putty.h"

static FILE *debug_fp = NULL;

void dputs(const char *buf)
{
    if (!debug_fp) {
        debug_fp = fopen("debug.log", "w");
    }

    if (write(1, buf, strlen(buf)) < 0) {} /* 'error check' to placate gcc */

    fputs(buf, debug_fp);
    fflush(debug_fp);
}
