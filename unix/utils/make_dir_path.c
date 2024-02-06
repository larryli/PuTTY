/*
 * Make a path of subdirectories, tolerating EEXIST at every step.
 */

#include <errno.h>
#include <string.h>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "putty.h"

char *make_dir_path(const char *path, mode_t mode)
{
    int pos = 0;
    char *prefix;

    while (1) {
        pos += strcspn(path + pos, "/");

        if (pos > 0) {
            prefix = dupprintf("%.*s", pos, path);

            if (mkdir(prefix, mode) < 0 && errno != EEXIST) {
                char *ret = dupprintf("%s: mkdir: %s",
                                      prefix, strerror(errno));
                sfree(prefix);
                return ret;
            }

            sfree(prefix);
        }

        if (!path[pos])
            return NULL;
        pos += strspn(path + pos, "/");
    }
}
