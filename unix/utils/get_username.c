/*
 * Implementation of get_username() for Unix.
 */

#include <unistd.h>
#include <pwd.h>

#include "putty.h"

char *get_username(void)
{
    struct passwd *p;
    uid_t uid = getuid();
    char *user, *ret = NULL;

    /*
     * First, find who we think we are using getlogin. If this
     * agrees with our uid, we'll go along with it. This should
     * allow sharing of uids between several login names whilst
     * coping correctly with people who have su'ed.
     */
    user = getlogin();
#if HAVE_SETPWENT
    setpwent();
#endif
    if (user)
        p = getpwnam(user);
    else
        p = NULL;
    if (p && p->pw_uid == uid) {
        /*
         * The result of getlogin() really does correspond to
         * our uid. Fine.
         */
        ret = user;
    } else {
        /*
         * If that didn't work, for whatever reason, we'll do
         * the simpler version: look up our uid in the password
         * file and map it straight to a name.
         */
        p = getpwuid(uid);
        if (!p)
            return NULL;
        ret = p->pw_name;
    }
#if HAVE_ENDPWENT
    endpwent();
#endif

    return dupstr(ret);
}
