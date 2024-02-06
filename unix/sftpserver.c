/*
 * Implement the SftpServer abstraction, in the 'live' form (i.e.
 * really operating on the Unix filesystem).
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <dirent.h>
#include <utime.h>

#include "putty.h"
#include "ssh.h"
#include "ssh/server.h"
#include "ssh/sftp.h"
#include "tree234.h"

typedef struct UnixSftpServer UnixSftpServer;

struct UnixSftpServer {
    unsigned *fdseqs;
    bool *fdsopen;
    size_t fdsize;

    tree234 *dirhandles;
    int last_dirhandle_index;

    char handlekey[8];

    SftpServer srv;
};

struct uss_dirhandle {
    int index;
    DIR *dp;
};

#define USS_DIRHANDLE_SEQ (0xFFFFFFFFU)

static int uss_dirhandle_cmp(void *av, void *bv)
{
    struct uss_dirhandle *a = (struct uss_dirhandle *)av;
    struct uss_dirhandle *b = (struct uss_dirhandle *)bv;
    if (a->index < b->index)
        return -1;
    if (a->index > b->index)
        return +1;
    return 0;
}

static SftpServer *uss_new(const SftpServerVtable *vt)
{
    UnixSftpServer *uss = snew(UnixSftpServer);

    memset(uss, 0, sizeof(UnixSftpServer));

    uss->dirhandles = newtree234(uss_dirhandle_cmp);
    uss->srv.vt = vt;

    make_unix_sftp_filehandle_key(uss->handlekey, sizeof(uss->handlekey));

    return &uss->srv;
}

static void uss_free(SftpServer *srv)
{
    UnixSftpServer *uss = container_of(srv, UnixSftpServer, srv);
    struct uss_dirhandle *udh;

    for (size_t i = 0; i < uss->fdsize; i++)
        if (uss->fdsopen[i])
            close(i);
    sfree(uss->fdseqs);

    while ((udh = delpos234(uss->dirhandles, 0)) != NULL) {
        closedir(udh->dp);
        sfree(udh);
    }

    sfree(uss);
}

static void uss_return_handle_raw(
    UnixSftpServer *uss, SftpReplyBuilder *reply, int index, unsigned seq)
{
    unsigned char handlebuf[8];
    PUT_32BIT_MSB_FIRST(handlebuf, index);
    PUT_32BIT_MSB_FIRST(handlebuf + 4, seq);
    des_encrypt_xdmauth(uss->handlekey, handlebuf, 8);
    fxp_reply_handle(reply, make_ptrlen(handlebuf, 8));
}

static bool uss_decode_handle(
    UnixSftpServer *uss, ptrlen handle, int *index, unsigned *seq)
{
    unsigned char handlebuf[8];

    if (handle.len != 8)
        return false;
    memcpy(handlebuf, handle.ptr, 8);
    des_decrypt_xdmauth(uss->handlekey, handlebuf, 8);
    *index = toint(GET_32BIT_MSB_FIRST(handlebuf));
    *seq = GET_32BIT_MSB_FIRST(handlebuf + 4);
    return true;
}

static void uss_return_new_handle(
    UnixSftpServer *uss, SftpReplyBuilder *reply, int fd)
{
    assert(fd >= 0);
    if (fd >= uss->fdsize) {
        size_t old_size = uss->fdsize;
        sgrowarray(uss->fdseqs, uss->fdsize, fd);
        uss->fdsopen = sresize(uss->fdsopen, uss->fdsize, bool);
        while (old_size < uss->fdsize) {
            uss->fdseqs[old_size] = 0;
            uss->fdsopen[old_size] = false;
            old_size++;
        }
    }
    assert(!uss->fdsopen[fd]);
    uss->fdsopen[fd] = true;
    if (++uss->fdseqs[fd] == USS_DIRHANDLE_SEQ)
        uss->fdseqs[fd] = 0;
    uss_return_handle_raw(uss, reply, fd, uss->fdseqs[fd]);
}

static int uss_try_lookup_fd(UnixSftpServer *uss, ptrlen handle)
{
    int fd;
    unsigned seq;
    if (!uss_decode_handle(uss, handle, &fd, &seq) ||
        fd < 0 || fd >= uss->fdsize ||
        !uss->fdsopen[fd] || uss->fdseqs[fd] != seq)
        return -1;

    return fd;
}

static int uss_lookup_fd(UnixSftpServer *uss, SftpReplyBuilder *reply,
                         ptrlen handle)
{
    int fd = uss_try_lookup_fd(uss, handle);
    if (fd < 0)
        fxp_reply_error(reply, SSH_FX_FAILURE, "invalid file handle");
    return fd;
}

static void uss_return_new_dirhandle(
    UnixSftpServer *uss, SftpReplyBuilder *reply, DIR *dp)
{
    struct uss_dirhandle *udh = snew(struct uss_dirhandle);
    udh->index = uss->last_dirhandle_index++;
    udh->dp = dp;
    struct uss_dirhandle *added = add234(uss->dirhandles, udh);
    assert(added == udh);
    uss_return_handle_raw(uss, reply, udh->index, USS_DIRHANDLE_SEQ);
}

static struct uss_dirhandle *uss_try_lookup_dirhandle(
    UnixSftpServer *uss, ptrlen handle)
{
    struct uss_dirhandle key, *udh;
    unsigned seq;

    if (!uss_decode_handle(uss, handle, &key.index, &seq) ||
        seq != USS_DIRHANDLE_SEQ ||
        (udh = find234(uss->dirhandles, &key, NULL)) == NULL)
        return NULL;

    return udh;
}

static struct uss_dirhandle *uss_lookup_dirhandle(
    UnixSftpServer *uss, SftpReplyBuilder *reply, ptrlen handle)
{
    struct uss_dirhandle *udh = uss_try_lookup_dirhandle(uss, handle);
    if (!udh)
        fxp_reply_error(reply, SSH_FX_FAILURE, "invalid file handle");
    return udh;
}

static void uss_error(UnixSftpServer *uss, SftpReplyBuilder *reply)
{
    unsigned code = SSH_FX_FAILURE;
    switch (errno) {
      case ENOENT:
        code = SSH_FX_NO_SUCH_FILE;
        break;
      case EPERM:
        code = SSH_FX_PERMISSION_DENIED;
        break;
    }
    fxp_reply_error(reply, code, strerror(errno));
}

static void uss_realpath(SftpServer *srv, SftpReplyBuilder *reply,
                         ptrlen path)
{
    UnixSftpServer *uss = container_of(srv, UnixSftpServer, srv);

    char *inpath = mkstr(path);
    char *outpath = realpath(inpath, NULL);
    free(inpath);

    if (!outpath) {
        uss_error(uss, reply);
    } else {
        fxp_reply_simple_name(reply, ptrlen_from_asciz(outpath));
        free(outpath);
    }
}

static void uss_open(SftpServer *srv, SftpReplyBuilder *reply,
                     ptrlen path, unsigned flags, struct fxp_attrs attrs)
{
    UnixSftpServer *uss = container_of(srv, UnixSftpServer, srv);

    int openflags = 0;
    if (!((SSH_FXF_READ | SSH_FXF_WRITE) &~ flags))
        openflags |= O_RDWR;
    else if (flags & SSH_FXF_WRITE)
        openflags |= O_WRONLY;
    else if (flags & SSH_FXF_READ)
        openflags |= O_RDONLY;
    if (flags & SSH_FXF_APPEND)
        openflags |= O_APPEND;
    if (flags & SSH_FXF_CREAT)
        openflags |= O_CREAT;
    if (flags & SSH_FXF_TRUNC)
        openflags |= O_TRUNC;
    if (flags & SSH_FXF_EXCL)
        openflags |= O_EXCL;

    char *pathstr = mkstr(path);
    int fd = open(pathstr, openflags, GET_PERMISSIONS(attrs, 0777));
    free(pathstr);

    if (fd < 0) {
        uss_error(uss, reply);
    } else {
        uss_return_new_handle(uss, reply, fd);
    }
}

static void uss_opendir(SftpServer *srv, SftpReplyBuilder *reply,
                        ptrlen path)
{
    UnixSftpServer *uss = container_of(srv, UnixSftpServer, srv);

    char *pathstr = mkstr(path);
    DIR *dp = opendir(pathstr);
    free(pathstr);

    if (!dp) {
        uss_error(uss, reply);
    } else {
        uss_return_new_dirhandle(uss, reply, dp);
    }
}

static void uss_close(SftpServer *srv, SftpReplyBuilder *reply,
                      ptrlen handle)
{
    UnixSftpServer *uss = container_of(srv, UnixSftpServer, srv);
    int fd;
    struct uss_dirhandle *udh;

    if ((udh = uss_try_lookup_dirhandle(uss, handle)) != NULL) {
        closedir(udh->dp);
        del234(uss->dirhandles, udh);
        sfree(udh);
        fxp_reply_ok(reply);
    } else if ((fd = uss_lookup_fd(uss, reply, handle)) >= 0) {
        close(fd);
        assert(0 <= fd && fd <= uss->fdsize);
        uss->fdsopen[fd] = false;
        fxp_reply_ok(reply);
    }
    /* if both failed, uss_lookup_fd will have filled in an error response */
}

static void uss_mkdir(SftpServer *srv, SftpReplyBuilder *reply,
                      ptrlen path, struct fxp_attrs attrs)
{
    UnixSftpServer *uss = container_of(srv, UnixSftpServer, srv);

    char *pathstr = mkstr(path);
    int status = mkdir(pathstr, GET_PERMISSIONS(attrs, 0777));
    free(pathstr);

    if (status < 0) {
        uss_error(uss, reply);
    } else {
        fxp_reply_ok(reply);
    }
}

static void uss_rmdir(SftpServer *srv, SftpReplyBuilder *reply, ptrlen path)
{
    UnixSftpServer *uss = container_of(srv, UnixSftpServer, srv);

    char *pathstr = mkstr(path);
    int status = rmdir(pathstr);
    free(pathstr);

    if (status < 0) {
        uss_error(uss, reply);
    } else {
        fxp_reply_ok(reply);
    }
}

static void uss_remove(SftpServer *srv, SftpReplyBuilder *reply,
                       ptrlen path)
{
    UnixSftpServer *uss = container_of(srv, UnixSftpServer, srv);

    char *pathstr = mkstr(path);
    int status = unlink(pathstr);
    free(pathstr);

    if (status < 0) {
        uss_error(uss, reply);
    } else {
        fxp_reply_ok(reply);
    }
}

static void uss_rename(SftpServer *srv, SftpReplyBuilder *reply,
                       ptrlen srcpath, ptrlen dstpath)
{
    UnixSftpServer *uss = container_of(srv, UnixSftpServer, srv);

    char *srcstr = mkstr(srcpath), *dststr = mkstr(dstpath);
    int status = rename(srcstr, dststr);
    free(srcstr);
    free(dststr);

    if (status < 0) {
        uss_error(uss, reply);
    } else {
        fxp_reply_ok(reply);
    }
}

static struct fxp_attrs uss_translate_struct_stat(const struct stat *st)
{
    struct fxp_attrs attrs;

    attrs.flags = (SSH_FILEXFER_ATTR_SIZE |
                   SSH_FILEXFER_ATTR_PERMISSIONS |
                   SSH_FILEXFER_ATTR_UIDGID |
                   SSH_FILEXFER_ATTR_ACMODTIME);

    attrs.size = st->st_size;
    attrs.permissions = st->st_mode;
    attrs.uid = st->st_uid;
    attrs.gid = st->st_gid;
    attrs.atime = st->st_atime;
    attrs.mtime = st->st_mtime;

    return attrs;
}

static void uss_reply_struct_stat(SftpReplyBuilder *reply,
                                  const struct stat *st)
{
    fxp_reply_attrs(reply, uss_translate_struct_stat(st));
}

static void uss_stat(SftpServer *srv, SftpReplyBuilder *reply,
                     ptrlen path, bool follow_symlinks)
{
    UnixSftpServer *uss = container_of(srv, UnixSftpServer, srv);
    struct stat st;

    char *pathstr = mkstr(path);
    int status = (follow_symlinks ? stat : lstat) (pathstr, &st);
    free(pathstr);

    if (status < 0) {
        uss_error(uss, reply);
    } else {
        uss_reply_struct_stat(reply, &st);
    }
}

static void uss_fstat(SftpServer *srv, SftpReplyBuilder *reply,
                      ptrlen handle)
{
    UnixSftpServer *uss = container_of(srv, UnixSftpServer, srv);
    struct stat st;
    int fd;

    if ((fd = uss_lookup_fd(uss, reply, handle)) < 0)
        return;
    int status = fstat(fd, &st);

    if (status < 0) {
        uss_error(uss, reply);
    } else {
        uss_reply_struct_stat(reply, &st);
    }
}

#if !HAVE_FUTIMES
static inline int futimes(int fd, const struct timeval tv[2])
{
    /* If the OS doesn't support futimes(3) then we have to pretend it
     * always returns failure */
    errno = EINVAL;
    return -1;
}
#endif

/*
 * The guts of setstat and fsetstat, macroised so that they can call
 * fchown(fd,...) or chown(path,...) depending on parameters.
 */
#define SETSTAT_GUTS(api_prefix, api_arg, attrs, success) do            \
    {                                                                   \
        if (attrs.flags & SSH_FILEXFER_ATTR_SIZE)                       \
            if (api_prefix(truncate)(api_arg, attrs.size) < 0)          \
                success = false;                                        \
        if (attrs.flags & SSH_FILEXFER_ATTR_UIDGID)                     \
            if (api_prefix(chown)(api_arg, attrs.uid, attrs.gid) < 0)   \
                success = false;                                        \
        if (attrs.flags & SSH_FILEXFER_ATTR_PERMISSIONS)                \
            if (api_prefix(chmod)(api_arg, attrs.permissions) < 0)      \
                success = false;                                        \
        if (attrs.flags & SSH_FILEXFER_ATTR_ACMODTIME) {                \
            struct timeval tv[2];                                       \
            tv[0].tv_sec = attrs.atime;                                 \
            tv[1].tv_sec = attrs.mtime;                                 \
            tv[0].tv_usec = tv[1].tv_usec = 0;                          \
            if (api_prefix(utimes)(api_arg, tv) < 0)                    \
                success = false;                                        \
        }                                                               \
    } while (0)

#define PATH_PREFIX(func) func
#define FD_PREFIX(func) f ## func

static void uss_setstat(SftpServer *srv, SftpReplyBuilder *reply,
                        ptrlen path, struct fxp_attrs attrs)
{
    UnixSftpServer *uss = container_of(srv, UnixSftpServer, srv);

    char *pathstr = mkstr(path);
    bool success = true;
    SETSTAT_GUTS(PATH_PREFIX, pathstr, attrs, success);
    free(pathstr);

    if (!success) {
        uss_error(uss, reply);
    } else {
        fxp_reply_ok(reply);
    }
}

static void uss_fsetstat(SftpServer *srv, SftpReplyBuilder *reply,
                         ptrlen handle, struct fxp_attrs attrs)
{
    UnixSftpServer *uss = container_of(srv, UnixSftpServer, srv);
    int fd;

    if ((fd = uss_lookup_fd(uss, reply, handle)) < 0)
        return;

    bool success = true;
    SETSTAT_GUTS(FD_PREFIX, fd, attrs, success);

    if (!success) {
        uss_error(uss, reply);
    } else {
        fxp_reply_ok(reply);
    }
}

static void uss_read(SftpServer *srv, SftpReplyBuilder *reply,
                     ptrlen handle, uint64_t offset, unsigned length)
{
    UnixSftpServer *uss = container_of(srv, UnixSftpServer, srv);
    int fd;
    char *buf;

    if ((fd = uss_lookup_fd(uss, reply, handle)) < 0)
        return;

    if ((buf = malloc(length)) == NULL) {
        /* A rare case in which I bother to check malloc failure,
         * because in this case we can localise the problem easily by
         * turning it into a failure response from this one sftp
         * request */
        fxp_reply_error(reply, SSH_FX_FAILURE,
                        "Out of memory for read buffer");
        return;
    }

    char *p = buf;

    int status = lseek(fd, offset, SEEK_SET);
    if (status >= 0 || errno == ESPIPE) {
        bool seekable = (status >= 0);
        while (length > 0) {
            status = read(fd, p, length);
            if (status <= 0)
                break;

            unsigned bytes_read = status;
            assert(bytes_read <= length);
            length -= bytes_read;
            p += bytes_read;

            if (!seekable) {
                /*
                 * If the seek failed because the file is fundamentally
                 * not a seekable kind of thing, abandon this loop after
                 * one attempt, i.e. we just read whatever we could get
                 * and we don't mind returning a short buffer.
                 */
            }
        }
    }

    if (status < 0) {
        uss_error(uss, reply);
    } else if (p == buf) {
        fxp_reply_error(reply, SSH_FX_EOF, "End of file");
    } else {
        fxp_reply_data(reply, make_ptrlen(buf, p - buf));
    }

    free(buf);
}

static void uss_write(SftpServer *srv, SftpReplyBuilder *reply,
                      ptrlen handle, uint64_t offset, ptrlen data)
{
    UnixSftpServer *uss = container_of(srv, UnixSftpServer, srv);
    int fd;

    if ((fd = uss_lookup_fd(uss, reply, handle)) < 0)
        return;

    const char *p = data.ptr;
    unsigned length = data.len;

    int status = lseek(fd, offset, SEEK_SET);
    if (status >= 0 || errno == ESPIPE) {

        while (length > 0) {
            status = write(fd, p, length);
            assert(status != 0);
            if (status < 0)
                break;

            unsigned bytes_written = status;
            assert(bytes_written <= length);
            length -= bytes_written;
            p += bytes_written;
        }
    }

    if (status < 0) {
        uss_error(uss, reply);
    } else {
        fxp_reply_ok(reply);
    }
}

static void uss_readdir(SftpServer *srv, SftpReplyBuilder *reply,
                        ptrlen handle, int max_entries, bool omit_longname)
{
    UnixSftpServer *uss = container_of(srv, UnixSftpServer, srv);
    struct dirent *de;
    struct uss_dirhandle *udh;

    if ((udh = uss_lookup_dirhandle(uss, reply, handle)) == NULL)
        return;

    errno = 0;
    de = readdir(udh->dp);
    if (!de) {
        if (errno == 0) {
            fxp_reply_error(reply, SSH_FX_EOF, "End of directory");
        } else {
            uss_error(uss, reply);
        }
    } else {
        ptrlen longname = PTRLEN_LITERAL("");
        char *longnamebuf = NULL;
        struct fxp_attrs attrs = no_attrs;

#if HAVE_FSTATAT && HAVE_DIRFD
        struct stat st;
        if (!fstatat(dirfd(udh->dp), de->d_name, &st, AT_SYMLINK_NOFOLLOW)) {
            char perms[11], *uidbuf = NULL, *gidbuf = NULL;
            struct passwd *pwd;
            struct group *grp;
            const char *user, *group;
            struct tm tm;

            attrs = uss_translate_struct_stat(&st);

            if (!omit_longname) {

                strcpy(perms, "----------");
                switch (st.st_mode & S_IFMT) {
                  case S_IFBLK: perms[0] = 'b'; break;
                  case S_IFCHR: perms[0] = 'c'; break;
                  case S_IFDIR: perms[0] = 'd'; break;
                  case S_IFIFO: perms[0] = 'p'; break;
                  case S_IFLNK: perms[0] = 'l'; break;
                  case S_IFSOCK: perms[0] = 's'; break;
                }
                if (st.st_mode & S_IRUSR)
                    perms[1] = 'r';
                if (st.st_mode & S_IWUSR)
                    perms[2] = 'w';
                if (st.st_mode & S_IXUSR)
                    perms[3] = (st.st_mode & S_ISUID ? 's' : 'x');
                else
                    perms[3] = (st.st_mode & S_ISUID ? 'S' : '-');
                if (st.st_mode & S_IRGRP)
                    perms[4] = 'r';
                if (st.st_mode & S_IWGRP)
                    perms[5] = 'w';
                if (st.st_mode & S_IXGRP)
                    perms[6] = (st.st_mode & S_ISGID ? 's' : 'x');
                else
                    perms[6] = (st.st_mode & S_ISGID ? 'S' : '-');
                if (st.st_mode & S_IROTH)
                    perms[7] = 'r';
                if (st.st_mode & S_IWOTH)
                    perms[8] = 'w';
                if (st.st_mode & S_IXOTH)
                    perms[9] = 'x';

                if ((pwd = getpwuid(st.st_uid)) != NULL)
                    user = pwd->pw_name;
                else
                    user = uidbuf = dupprintf("%u", (unsigned)st.st_uid);
                if ((grp = getgrgid(st.st_gid)) != NULL)
                    group = grp->gr_name;
                else
                    group = gidbuf = dupprintf("%u", (unsigned)st.st_gid);

                tm = *localtime(&st.st_mtime);

                longnamebuf = dupprintf(
                    "%s %3u %-8s %-8s %8"PRIuMAX" %.3s %2d %02d:%02d %s",
                    perms, (unsigned)st.st_nlink, user, group,
                    (uintmax_t)st.st_size,
                    (&"JanFebMarAprMayJunJulAugSepOctNovDec"[3*tm.tm_mon]),
                    tm.tm_mday, tm.tm_hour, tm.tm_min, de->d_name);
                longname = ptrlen_from_asciz(longnamebuf);

                sfree(uidbuf);
                sfree(gidbuf);
            }
        }
#endif

        /* FIXME: be able to return more than one, in which case we
         * must also check max_entries */
        fxp_reply_name_count(reply, 1);
        fxp_reply_full_name(reply, ptrlen_from_asciz(de->d_name),
                            longname, attrs);

        sfree(longnamebuf);
    }
}

const SftpServerVtable unix_live_sftpserver_vt = {
    .new = uss_new,
    .free = uss_free,
    .realpath = uss_realpath,
    .open = uss_open,
    .opendir = uss_opendir,
    .close = uss_close,
    .mkdir = uss_mkdir,
    .rmdir = uss_rmdir,
    .remove = uss_remove,
    .rename = uss_rename,
    .stat = uss_stat,
    .fstat = uss_fstat,
    .setstat = uss_setstat,
    .fsetstat = uss_fsetstat,
    .read = uss_read,
    .write = uss_write,
    .readdir = uss_readdir,
};
