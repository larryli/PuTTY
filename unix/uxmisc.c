/*
 * PuTTY miscellaneous Unix stuff
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <pwd.h>

#include "putty.h"

unsigned long getticks(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    /*
     * We want to use milliseconds rather than microseconds,
     * because we need a decent number of them to fit into a 32-bit
     * word so it can be used for keepalives.
     */
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
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
    setpwent();
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
    endpwent();

    return dupstr(ret);
}
