/*
 * filehash: print the MD5 and SHA-1 hashes of a set of files, computed
 * with PuTTY's own hash implementations (crypto/md5.c and
 * crypto/sha1-*.c).
 *
 * The output is a Markdown table in the same format as the checksum
 * tables in this project's release notes:
 *
 *   File | MD5 | SHA1
 *   --- | --- | ---
 *   putty.exe | `f4060753239e460950fb039e72cdc7e0` | `5d0bfe66c747f96d11c4fbe59dc2492c62b32cc0`
 *
 * Usage:
 *   filehash [-m|-s] [-o outfile] file ...
 *
 * Options:
 *   -m          output only the MD5 column
 *   -s          output only the SHA1 column
 *   -o outfile  write the table to outfile instead of standard output
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

#include "defs.h"
#include "misc.h"
#include "ssh.h"

static NORETURN PRINTF_LIKE(1, 2) void fatal(const char *p, ...)
{
    va_list ap;
    fprintf(stderr, "filehash: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

/* Defined here, as in test/testcrypt.c, so that this standalone tool
 * doesn't need any platform-specific fatal-error dialog. */
NORETURN void out_of_memory(void)
{
    fatal("out of memory");
}

/* The column heading "Filename", spelled out as individual UTF-8 bytes so
 * that the table header comes out in UTF-8 no matter what character set
 * the compiler assumes the source file is written in. */
static const char col_filename[] =
    {0xE6, 0x96, 0x87, 0xE4, 0xBB, 0xB6, 0x00};

static const char *basename_of(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    return base;
}

static bool hash_file(const ssh_hashalg *alg, const char *path,
                      unsigned char *digest)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return false;

    ssh_hash *hash = ssh_hash_new(alg);
    if (!hash) {
        fclose(fp);
        errno = ENOMEM;
        return false;
    }
    bool ok = read_file_into(BinarySink_UPCAST(hash), fp);
    fclose(fp);
    if (!ok) {
        ssh_hash_free(hash);
        return false;
    }

    ssh_hash_final(hash, digest);
    return true;
}

static void put_digest_cell(FILE *out, const unsigned char *digest,
                            size_t len)
{
    static const char digits[] = "0123456789abcdef";
    fputs(" | `", out);
    for (size_t i = 0; i < len; i++) {
        fputc(digits[digest[i] >> 4], out);
        fputc(digits[digest[i] & 0xF], out);
    }
    fputc('`', out);
}

int main(int argc, char **argv)
{
    const char *outfile = NULL;
    bool want_md5 = true, want_sha1 = true;
    int argi = 1;

    for (; argi < argc; argi++) {
        const char *arg = argv[argi];
        if (arg[0] != '-' || arg[1] == '\0')
            break; /* first non-option argument */
        if (!strcmp(arg, "-m")) {
            want_md5 = true;
            want_sha1 = false;
        } else if (!strcmp(arg, "-s")) {
            want_md5 = false;
            want_sha1 = true;
        } else if (!strcmp(arg, "-o")) {
            if (argi + 1 >= argc)
                fatal("option %s requires an argument", arg);
            outfile = argv[++argi];
        } else if (!strcmp(arg, "--")) {
            argi++;
            break;
        } else {
            fatal("unknown option %s", arg);
        }
    }

    size_t nfiles = argc - argi;
    if (nfiles < 1) {
        fprintf(stderr, "usage: filehash [-m|-s] [-o outfile] file ...\n");
        return 1;
    }

    /* Hash everything up front, so that an error on any file aborts
     * before we've printed a partial table. */
    unsigned char *md5s = snewn(nfiles * 16, unsigned char);
    unsigned char *sha1s = snewn(nfiles * 20, unsigned char);
    for (size_t f = 0; f < nfiles; f++) {
        const char *path = argv[argi + f];
        if (want_md5 &&
            !hash_file(&ssh_md5, path, md5s + 16 * f))
            fatal("%s: %s", path, strerror(errno));
        if (want_sha1 &&
            !hash_file(&ssh_sha1, path, sha1s + 20 * f))
            fatal("%s: %s", path, strerror(errno));
    }

    FILE *out = outfile ? fopen(outfile, "wb") : stdout;
    if (!out)
        fatal("%s: %s", outfile, strerror(errno));

    fprintf(out, "%s", col_filename);
    if (want_md5)
        fprintf(out, " | MD5");
    if (want_sha1)
        fprintf(out, " | SHA1");
    fputc('\n', out);

    fprintf(out, "---");
    if (want_md5)
        fprintf(out, " | ---");
    if (want_sha1)
        fprintf(out, " | ---");
    fputc('\n', out);

    for (size_t f = 0; f < nfiles; f++) {
        fprintf(out, "%s", basename_of(argv[argi + f]));
        if (want_md5)
            put_digest_cell(out, md5s + 16 * f, 16);
        if (want_sha1)
            put_digest_cell(out, sha1s + 20 * f, 20);
        fputc('\n', out);
    }

    if (out != stdout)
        fclose(out);
    return 0;
}
