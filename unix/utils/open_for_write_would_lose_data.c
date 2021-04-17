/*
 * Unix implementation of open_for_write_would_lose_data().
 */

#include <sys/types.h>
#include <sys/stat.h>

#include "putty.h"

bool open_for_write_would_lose_data(const Filename *fn)
{
    struct stat st;

    if (stat(fn->path, &st) < 0) {
        /*
         * If the file doesn't even exist, we obviously want to return
         * false. If we failed to stat it for any other reason,
         * ignoring the precise error code and returning false still
         * doesn't seem too unreasonable, because then we'll try to
         * open the file for writing and report _that_ error, which is
         * likely to be more to the point.
         */
        return false;
    }

    /*
     * OK, something exists at this pathname and we've found out
     * something about it. But an open-for-write will only
     * destructively truncate it if it's a regular file with nonzero
     * size. If it's empty, or some other kind of special thing like a
     * character device (e.g. /dev/tty) or a named pipe, then opening
     * it for write is already non-destructive and it's pointless and
     * annoying to warn about it just because the same file can be
     * opened for reading. (Indeed, if it's a named pipe, opening it
     * for reading actually _causes inconvenience_ in its own right,
     * even before the question of whether it gives misleading
     * information.)
     */
    if (S_ISREG(st.st_mode) && st.st_size > 0) {
        return true;
    }

    return false;
}
