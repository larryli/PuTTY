/*
 * Create a directory accessible only to us, and then check afterwards
 * that we really did end up with a directory with the right ownership
 * and permissions.
 *
 * The idea is that this is a directory in which we're about to create
 * something sensitive, like a listening Unix-domain socket for SSH
 * connection sharing or an SSH agent. We want to be protected against
 * somebody else previously having created the directory in a way
 * that's writable to us, and thus manipulating us into creating the
 * actual socket in a directory they can see so that they can connect
 * to it and (say) use our authenticated SSH sessions.
 *
 * NOTE: The strategy used in this function is not safe if the enemy
 * has unrestricted write access to the containing directory. In that
 * case, they could move our directory out of the way and make a new
 * one, after this function returns and before we create our socket
 * (or whatever) inside it.
 *
 * But this should be OK for temp directories (which modify the
 * default world-write behaviour by also setting the 't' bit,
 * preventing people from renaming or deleting things in there that
 * they don't own). And of course it's also safe if the directory is
 * writable only by our _own_ uid.
 */

#include <errno.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "putty.h"

char *make_dir_and_check_ours(const char *dirname)
{
    struct stat st;

    /*
     * Create the directory. We might have created it before, so
     * EEXIST is an OK error; but anything else is doom.
     */
    if (mkdir(dirname, 0700) < 0 && errno != EEXIST)
        return dupprintf("%s: mkdir: %s", dirname, strerror(errno));

    /*
     * Stat the directory and check its ownership and permissions.
     */
    if (stat(dirname, &st) < 0)
        return dupprintf("%s: stat: %s", dirname, strerror(errno));
    if (st.st_uid != getuid())
        return dupprintf("%s: directory owned by uid %d, not by us",
                         dirname, st.st_uid);
    if ((st.st_mode & 077) != 0)
        return dupprintf("%s: directory has overgenerous permissions %03o"
                         " (expected 700)", dirname, st.st_mode & 0777);

    return NULL;
}
