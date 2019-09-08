/*
 * PuTTY miscellaneous Unix stuff
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>

#include "putty.h"

unsigned long getticks(void)
{
    /*
     * We want to use milliseconds rather than the microseconds or
     * nanoseconds given by the underlying clock functions, because we
     * need a decent number of them to fit into a 32-bit word so it
     * can be used for keepalives.
     */
#if defined HAVE_CLOCK_GETTIME && defined HAVE_DECL_CLOCK_MONOTONIC
    {
        /* Use CLOCK_MONOTONIC if available, so as to be unconfused if
         * the system clock changes. */
        struct timespec ts;
        if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
            return ts.tv_sec * TICKSPERSEC +
                ts.tv_nsec / (1000000000 / TICKSPERSEC);
    }
#endif
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return tv.tv_sec * TICKSPERSEC + tv.tv_usec / (1000000 / TICKSPERSEC);
    }
}

Filename *filename_from_str(const char *str)
{
    Filename *ret = snew(Filename);
    ret->path = dupstr(str);
    return ret;
}

Filename *filename_copy(const Filename *fn)
{
    return filename_from_str(fn->path);
}

const char *filename_to_str(const Filename *fn)
{
    return fn->path;
}

bool filename_equal(const Filename *f1, const Filename *f2)
{
    return !strcmp(f1->path, f2->path);
}

bool filename_is_null(const Filename *fn)
{
    return !fn->path[0];
}

void filename_free(Filename *fn)
{
    sfree(fn->path);
    sfree(fn);
}

void filename_serialise(BinarySink *bs, const Filename *f)
{
    put_asciz(bs, f->path);
}
Filename *filename_deserialise(BinarySource *src)
{
    return filename_from_str(get_asciz(src));
}

char filename_char_sanitise(char c)
{
    if (c == '/')
        return '.';
    return c;
}

#ifdef DEBUG
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

/*
 * Display the fingerprints of the PGP Master Keys to the user.
 * (This is here rather than in uxcons because it's appropriate even for
 * Unix GUI apps.)
 */
void pgp_fingerprints(void)
{
    fputs("These are the fingerprints of the PuTTY PGP Master Keys. They can\n"
          "be used to establish a trust path from this executable to another\n"
          "one. See the manual for more information.\n"
          "(Note: these fingerprints have nothing to do with SSH!)\n"
          "\n"
          "PuTTY Master Key as of " PGP_MASTER_KEY_YEAR
          " (" PGP_MASTER_KEY_DETAILS "):\n"
          "  " PGP_MASTER_KEY_FP "\n\n"
          "Previous Master Key (" PGP_PREV_MASTER_KEY_YEAR
          ", " PGP_PREV_MASTER_KEY_DETAILS "):\n"
          "  " PGP_PREV_MASTER_KEY_FP "\n", stdout);
}

/*
 * Set and clear fcntl options on a file descriptor. We don't
 * realistically expect any of these operations to fail (the most
 * plausible error condition is EBADF, but we always believe ourselves
 * to be passing a valid fd so even that's an assertion-fail sort of
 * response), so we don't make any effort to return sensible error
 * codes to the caller - we just log to standard error and die
 * unceremoniously. However, nonblock and no_nonblock do return the
 * previous state of O_NONBLOCK.
 */
void cloexec(int fd) {
    int fdflags;

    fdflags = fcntl(fd, F_GETFD);
    if (fdflags < 0) {
        fprintf(stderr, "%d: fcntl(F_GETFD): %s\n", fd, strerror(errno));
        exit(1);
    }
    if (fcntl(fd, F_SETFD, fdflags | FD_CLOEXEC) < 0) {
        fprintf(stderr, "%d: fcntl(F_SETFD): %s\n", fd, strerror(errno));
        exit(1);
    }
}
void noncloexec(int fd) {
    int fdflags;

    fdflags = fcntl(fd, F_GETFD);
    if (fdflags < 0) {
        fprintf(stderr, "%d: fcntl(F_GETFD): %s\n", fd, strerror(errno));
        exit(1);
    }
    if (fcntl(fd, F_SETFD, fdflags & ~FD_CLOEXEC) < 0) {
        fprintf(stderr, "%d: fcntl(F_SETFD): %s\n", fd, strerror(errno));
        exit(1);
    }
}
bool nonblock(int fd) {
    int fdflags;

    fdflags = fcntl(fd, F_GETFL);
    if (fdflags < 0) {
        fprintf(stderr, "%d: fcntl(F_GETFL): %s\n", fd, strerror(errno));
        exit(1);
    }
    if (fcntl(fd, F_SETFL, fdflags | O_NONBLOCK) < 0) {
        fprintf(stderr, "%d: fcntl(F_SETFL): %s\n", fd, strerror(errno));
        exit(1);
    }

    return fdflags & O_NONBLOCK;
}
bool no_nonblock(int fd) {
    int fdflags;

    fdflags = fcntl(fd, F_GETFL);
    if (fdflags < 0) {
        fprintf(stderr, "%d: fcntl(F_GETFL): %s\n", fd, strerror(errno));
        exit(1);
    }
    if (fcntl(fd, F_SETFL, fdflags & ~O_NONBLOCK) < 0) {
        fprintf(stderr, "%d: fcntl(F_SETFL): %s\n", fd, strerror(errno));
        exit(1);
    }

    return fdflags & O_NONBLOCK;
}

FILE *f_open(const Filename *filename, char const *mode, bool is_private)
{
    if (!is_private) {
        return fopen(filename->path, mode);
    } else {
        int fd;
        assert(mode[0] == 'w');        /* is_private is meaningless for read,
                                          and tricky for append */
        fd = open(filename->path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0)
            return NULL;
        return fdopen(fd, mode);
    }
}

FontSpec *fontspec_new(const char *name)
{
    FontSpec *f = snew(FontSpec);
    f->name = dupstr(name);
    return f;
}
FontSpec *fontspec_copy(const FontSpec *f)
{
    return fontspec_new(f->name);
}
void fontspec_free(FontSpec *f)
{
    sfree(f->name);
    sfree(f);
}
void fontspec_serialise(BinarySink *bs, FontSpec *f)
{
    put_asciz(bs, f->name);
}
FontSpec *fontspec_deserialise(BinarySource *src)
{
    return fontspec_new(get_asciz(src));
}

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
     * Now check that that directory is _owned by us_ and not writable
     * by anybody else. This protects us against somebody else
     * previously having created the directory in a way that's
     * writable to us, and thus manipulating us into creating the
     * actual socket in a directory they can see so that they can
     * connect to it and use our authenticated SSH sessions.
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
