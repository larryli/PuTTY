/*
 * uxsftp.c: the Unix-specific parts of PSFTP.
 */

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>

#include "putty.h"
#include "psftp.h"

/*
 * In PSFTP our selects are synchronous, so these functions are
 * empty stubs.
 */
int uxsel_input_add(int fd, int rwx) { return 0; }
void uxsel_input_remove(int id) { }

char *x_get_default(const char *key)
{
    return NULL;		       /* this is a stub */
}

void platform_get_x11_auth(char *display, int *protocol,
                           unsigned char *data, int *datalen)
{
    /* Do nothing, therefore no auth. */
}

/*
 * Default settings that are specific to PSFTP.
 */
char *platform_default_s(const char *name)
{
    if (!strcmp(name, "UserName")) {
	/*
	 * Remote login username will default to the local username.
	 */
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
	    ret = p->pw_name;
	}
	endpwent();

	return ret;
    }
    return NULL;
}

int platform_default_i(const char *name, int def)
{
    return def;
}

FontSpec platform_default_fontspec(const char *name)
{
    FontSpec ret;
    *ret.name = '\0';
    return ret;
}

Filename platform_default_filename(const char *name)
{
    Filename ret;
    if (!strcmp(name, "LogFileName"))
	strcpy(ret.path, "putty.log");
    else
	*ret.path = '\0';
    return ret;
}

/*
 * Set local current directory. Returns NULL on success, or else an
 * error message which must be freed after printing.
 */
char *psftp_lcd(char *dir)
{
    if (chdir(dir) < 0)
	return dupprintf("%s: chdir: %s", dir, strerror(errno));
    else
	return NULL;
}

/*
 * Get local current directory. Returns a string which must be
 * freed.
 */
char *psftp_getcwd(void)
{
    char *buffer, *ret;
    int size = 256;

    buffer = snewn(size, char);
    while (1) {
	ret = getcwd(buffer, size);
	if (ret != NULL)
	    return ret;
	if (errno != ERANGE) {
	    sfree(buffer);
	    return dupprintf("[cwd unavailable: %s]", strerror(errno));
	}
	/*
	 * Otherwise, ERANGE was returned, meaning the buffer
	 * wasn't big enough.
	 */
	size = size * 3 / 2;
	buffer = sresize(buffer, size, char);
    }
}

/*
 * Wait for some network data and process it.
 */
int ssh_sftp_loop_iteration(void)
{
    fd_set rset, wset, xset;
    int i, fdcount, fdsize, *fdlist;
    int fd, fdstate, rwx, ret, maxfd;

    fdlist = NULL;
    fdcount = fdsize = 0;

    /* Count the currently active fds. */
    i = 0;
    for (fd = first_fd(&fdstate, &rwx); fd >= 0;
	 fd = next_fd(&fdstate, &rwx)) i++;

    if (i < 1)
	return -1;		       /* doom */

    /* Expand the fdlist buffer if necessary. */
    if (i > fdsize) {
	fdsize = i + 16;
	fdlist = sresize(fdlist, fdsize, int);
    }

    FD_ZERO(&rset);
    FD_ZERO(&wset);
    FD_ZERO(&xset);
    maxfd = 0;

    /*
     * Add all currently open fds to the select sets, and store
     * them in fdlist as well.
     */
    fdcount = 0;
    for (fd = first_fd(&fdstate, &rwx); fd >= 0;
	 fd = next_fd(&fdstate, &rwx)) {
	fdlist[fdcount++] = fd;
	if (rwx & 1)
	    FD_SET_MAX(fd, maxfd, rset);
	if (rwx & 2)
	    FD_SET_MAX(fd, maxfd, wset);
	if (rwx & 4)
	    FD_SET_MAX(fd, maxfd, xset);
    }

    do {
	ret = select(maxfd, &rset, &wset, &xset, NULL);
    } while (ret < 0 && errno == EINTR);

    if (ret < 0) {
	perror("select");
	exit(1);
    }

    for (i = 0; i < fdcount; i++) {
	fd = fdlist[i];
	/*
	 * We must process exceptional notifications before
	 * ordinary readability ones, or we may go straight
	 * past the urgent marker.
	 */
	if (FD_ISSET(fd, &xset))
	    select_result(fd, 4);
	if (FD_ISSET(fd, &rset))
	    select_result(fd, 1);
	if (FD_ISSET(fd, &wset))
	    select_result(fd, 2);
    }

    sfree(fdlist);

    return 0;
}

/*
 * Main program: do platform-specific initialisation and then call
 * psftp_main().
 */
int main(int argc, char *argv[])
{
    uxsel_init();
    return psftp_main(argc, argv);
}
