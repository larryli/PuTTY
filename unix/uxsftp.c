/*
 * uxsftp.c: the Unix-specific parts of PSFTP and PSCP.
 */

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <utime.h>
#include <errno.h>
#include <assert.h>

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
 * Stubs for the GUI feedback mechanism in Windows PSCP.
 */
void gui_update_stats(char *name, unsigned long size,
		      int percentage, unsigned long elapsed,
		      unsigned long done, unsigned long eta,
		      unsigned long ratebs) {}
void gui_send_errcount(int list, int errs) {}
void gui_send_char(int is_stderr, int c) {}
void gui_enable(char *arg) {}


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

struct RFile {
    int fd;
};

RFile *open_existing_file(char *name, unsigned long *size,
			  unsigned long *mtime, unsigned long *atime)
{
    int fd;
    RFile *ret;

    fd = open(name, O_RDONLY);
    if (fd < 0)
	return NULL;

    ret = snew(RFile);
    ret->fd = fd;

    if (size || mtime || atime) {
	struct stat statbuf;
	if (fstat(fd, &statbuf) < 0) {
	    fprintf(stderr, "%s: stat: %s\n", name, strerror(errno));
	    memset(&statbuf, 0, sizeof(statbuf));
	}

	if (size)
	    *size = statbuf.st_size;

	if (mtime)
	    *mtime = statbuf.st_mtime;

	if (atime)
	    *atime = statbuf.st_atime;
    }

    return ret;
}

int read_from_file(RFile *f, void *buffer, int length)
{
    return read(f->fd, buffer, length);
}

void close_rfile(RFile *f)
{
    close(f->fd);
    sfree(f);
}

struct WFile {
    int fd;
    char *name;
};

WFile *open_new_file(char *name)
{
    int fd;
    WFile *ret;

    fd = open(name, O_CREAT | O_TRUNC | O_WRONLY, 0666);
    if (fd < 0)
	return NULL;

    ret = snew(WFile);
    ret->fd = fd;
    ret->name = dupstr(name);

    return ret;
}

int write_to_file(WFile *f, void *buffer, int length)
{
    char *p = (char *)buffer;
    int so_far = 0;

    /* Keep trying until we've really written as much as we can. */
    while (length > 0) {
	int ret = write(f->fd, p, length);

	if (ret < 0)
	    return ret;

	if (ret == 0)
	    break;

	p += ret;
	length -= ret;
	so_far += ret;
    }

    return so_far;
}

void set_file_times(WFile *f, unsigned long mtime, unsigned long atime)
{
    struct utimbuf ut;

    ut.actime = atime;
    ut.modtime = mtime;

    utime(f->name, &ut);
}

/* Closes and frees the WFile */
void close_wfile(WFile *f)
{
    close(f->fd);
    sfree(f->name);
    sfree(f);
}

int file_type(char *name)
{
    struct stat statbuf;

    if (stat(name, &statbuf) < 0) {
	if (errno != ENOENT)
	    fprintf(stderr, "%s: stat: %s\n", name, strerror(errno));
	return FILE_TYPE_NONEXISTENT;
    }

    if (S_ISREG(statbuf.st_mode))
	return FILE_TYPE_FILE;

    if (S_ISDIR(statbuf.st_mode))
	return FILE_TYPE_DIRECTORY;

    return FILE_TYPE_WEIRD;
}

struct DirHandle {
    DIR *dir;
};

DirHandle *open_directory(char *name)
{
    DIR *dir;
    DirHandle *ret;

    dir = opendir(name);
    if (!dir)
	return NULL;

    ret = snew(DirHandle);
    ret->dir = dir;
    return ret;
}

char *read_filename(DirHandle *dir)
{
    struct dirent *de;

    do {
	de = readdir(dir->dir);
	if (de == NULL)
	    return NULL;
    } while ((de->d_name[0] == '.' &&
	      (de->d_name[1] == '\0' ||
	       (de->d_name[1] == '.' && de->d_name[2] == '\0'))));

    return dupstr(de->d_name);
}

void close_directory(DirHandle *dir)
{
    closedir(dir->dir);
    sfree(dir);
}

int test_wildcard(char *name, int cmdline)
{
    /*
     * On Unix, we currently don't support local wildcards at all.
     * We will have to do so (FIXME) once PSFTP starts implementing
     * mput, but until then we can assume `cmdline' is always set.
     */
    struct stat statbuf;

    assert(cmdline);
    if (stat(name, &statbuf) < 0)
	return WCTYPE_NONEXISTENT;
    else
	return WCTYPE_FILENAME;
}

/*
 * Actually return matching file names for a local wildcard. FIXME:
 * we currently don't support this at all.
 */
struct WildcardMatcher {
    int x;
};
WildcardMatcher *begin_wildcard_matching(char *name) { return NULL; }
char *wildcard_get_filename(WildcardMatcher *dir) { return NULL; }
void finish_wildcard_matching(WildcardMatcher *dir) {}

int create_directory(char *name)
{
    return mkdir(name, 0777) == 0;
}

char *dir_file_cat(char *dir, char *file)
{
    return dupcat(dir, "/", file, NULL);
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
