/*
 * uxstore.c: Unix-specific implementation of the interface defined
 * in storage.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "putty.h"
#include "storage.h"
#include "tree234.h"

#ifdef PATH_MAX
#define FNLEN PATH_MAX
#else
#define FNLEN 1024 /* XXX */
#endif

enum {
    INDEX_DIR, INDEX_HOSTKEYS, INDEX_HOSTKEYS_TMP, INDEX_RANDSEED,
    INDEX_SESSIONDIR, INDEX_SESSION,
};

static const char hex[16] = "0123456789ABCDEF";

static char *mungestr(const char *in)
{
    char *out, *ret;

    if (!in || !*in)
        in = "Default Settings";

    ret = out = snewn(3*strlen(in)+1, char);

    while (*in) {
        /*
         * There are remarkably few punctuation characters that
         * aren't shell-special in some way or likely to be used as
         * separators in some file format or another! Hence we use
         * opt-in for safe characters rather than opt-out for
         * specific unsafe ones...
         */
	if (*in!='+' && *in!='-' && *in!='.' && *in!='@' && *in!='_' &&
            !(*in >= '0' && *in <= '9') &&
            !(*in >= 'A' && *in <= 'Z') &&
            !(*in >= 'a' && *in <= 'z')) {
	    *out++ = '%';
	    *out++ = hex[((unsigned char) *in) >> 4];
	    *out++ = hex[((unsigned char) *in) & 15];
	} else
	    *out++ = *in;
	in++;
    }
    *out = '\0';
    return ret;
}

static char *unmungestr(const char *in)
{
    char *out, *ret;
    out = ret = snewn(strlen(in)+1, char);
    while (*in) {
	if (*in == '%' && in[1] && in[2]) {
	    int i, j;

	    i = in[1] - '0';
	    i -= (i > 9 ? 7 : 0);
	    j = in[2] - '0';
	    j -= (j > 9 ? 7 : 0);

	    *out++ = (i << 4) + j;
	    in += 3;
	} else {
	    *out++ = *in++;
	}
    }
    *out = '\0';
    return ret;
}

static void make_filename(char *filename, int index, const char *subname)
{
    char *home;
    int len;
    home = getenv("HOME");
    if (!home)
        home="/";
    strncpy(filename, home, FNLEN);
    len = strlen(filename);
    if (index == INDEX_SESSION) {
        char *munged = mungestr(subname);
        char *fn = dupprintf("/.putty/sessions/%s", munged);
        strncpy(filename + len, fn, FNLEN - len);
        sfree(fn);
        sfree(munged);
    } else {
        strncpy(filename + len,
                index == INDEX_DIR ? "/.putty" :
                index == INDEX_SESSIONDIR ? "/.putty/sessions" :
                index == INDEX_HOSTKEYS ? "/.putty/sshhostkeys" :
                index == INDEX_HOSTKEYS_TMP ? "/.putty/sshhostkeys.tmp" :
                index == INDEX_RANDSEED ? "/.putty/randomseed" :
                "/.putty/ERROR", FNLEN - len);
    }
    filename[FNLEN-1] = '\0';
}

void *open_settings_w(const char *sessionname, char **errmsg)
{
    char filename[FNLEN];
    FILE *fp;

    *errmsg = NULL;

    /*
     * Start by making sure the .putty directory and its sessions
     * subdir actually exist. Ignore error returns from mkdir since
     * they're perfectly likely to be `already exists', and any
     * other error will trip us up later on so there's no real need
     * to catch it now.
     */
    make_filename(filename, INDEX_DIR, sessionname);
    mkdir(filename, 0700);
    make_filename(filename, INDEX_SESSIONDIR, sessionname);
    mkdir(filename, 0700);

    make_filename(filename, INDEX_SESSION, sessionname);
    fp = fopen(filename, "w");
    if (!fp) {
        *errmsg = dupprintf("Unable to create %s: %s",
                            filename, strerror(errno));
	return NULL;                   /* can't open */
    }
    return fp;
}

void write_setting_s(void *handle, const char *key, const char *value)
{
    FILE *fp = (FILE *)handle;
    fprintf(fp, "%s=%s\n", key, value);
}

void write_setting_i(void *handle, const char *key, int value)
{
    FILE *fp = (FILE *)handle;
    fprintf(fp, "%s=%d\n", key, value);
}

void close_settings_w(void *handle)
{
    FILE *fp = (FILE *)handle;
    fclose(fp);
}

/*
 * Reading settings, for the moment, is done by retrieving X
 * resources from the X display. When we introduce disk files, I
 * think what will happen is that the X resources will override
 * PuTTY's inbuilt defaults, but that the disk files will then
 * override those. This isn't optimal, but it's the best I can
 * immediately work out.
 * FIXME: the above comment is a bit out of date. Did it happen?
 */

struct keyval {
    const char *key;
    const char *value;
};

static tree234 *xrmtree = NULL;

int keycmp(void *av, void *bv)
{
    struct keyval *a = (struct keyval *)av;
    struct keyval *b = (struct keyval *)bv;
    return strcmp(a->key, b->key);
}

void provide_xrm_string(char *string)
{
    char *p, *q, *key;
    struct keyval *xrms, *ret;

    p = q = strchr(string, ':');
    if (!q) {
	fprintf(stderr, "pterm: expected a colon in resource string"
		" \"%s\"\n", string);
	return;
    }
    q++;
    while (p > string && p[-1] != '.' && p[-1] != '*')
	p--;
    xrms = snew(struct keyval);
    key = snewn(q-p, char);
    memcpy(key, p, q-p);
    key[q-p-1] = '\0';
    xrms->key = key;
    while (*q && isspace((unsigned char)*q))
	q++;
    xrms->value = dupstr(q);

    if (!xrmtree)
	xrmtree = newtree234(keycmp);

    ret = add234(xrmtree, xrms);
    if (ret) {
	/* Override an existing string. */
	del234(xrmtree, ret);
	add234(xrmtree, xrms);
    }
}

const char *get_setting(const char *key)
{
    struct keyval tmp, *ret;
    tmp.key = key;
    if (xrmtree) {
	ret = find234(xrmtree, &tmp, NULL);
	if (ret)
	    return ret->value;
    }
    return x_get_default(key);
}

void *open_settings_r(const char *sessionname)
{
    char filename[FNLEN];
    FILE *fp;
    char *line;
    tree234 *ret;

    make_filename(filename, INDEX_SESSION, sessionname);
    fp = fopen(filename, "r");
    if (!fp)
	return NULL;		       /* can't open */

    ret = newtree234(keycmp);

    while ( (line = fgetline(fp)) ) {
        char *value = strchr(line, '=');
        struct keyval *kv;

        if (!value)
            continue;
        *value++ = '\0';
        value[strcspn(value, "\r\n")] = '\0';   /* trim trailing NL */

        kv = snew(struct keyval);
        kv->key = dupstr(line);
        kv->value = dupstr(value);
        add234(ret, kv);

        sfree(line);
    }

    fclose(fp);

    return ret;
}

char *read_setting_s(void *handle, const char *key, char *buffer, int buflen)
{
    tree234 *tree = (tree234 *)handle;
    const char *val;
    struct keyval tmp, *kv;

    tmp.key = key;
    if (tree != NULL &&
        (kv = find234(tree, &tmp, NULL)) != NULL) {
        val = kv->value;
        assert(val != NULL);
    } else
        val = get_setting(key);

    if (!val)
	return NULL;
    else {
	strncpy(buffer, val, buflen);
	buffer[buflen-1] = '\0';
	return buffer;
    }
}

int read_setting_i(void *handle, const char *key, int defvalue)
{
    tree234 *tree = (tree234 *)handle;
    const char *val;
    struct keyval tmp, *kv;

    tmp.key = key;
    if (tree != NULL &&
        (kv = find234(tree, &tmp, NULL)) != NULL) {
        val = kv->value;
        assert(val != NULL);
    } else
        val = get_setting(key);

    if (!val)
	return defvalue;
    else
	return atoi(val);
}

int read_setting_fontspec(void *handle, const char *name, FontSpec *result)
{
    return !!read_setting_s(handle, name, result->name, sizeof(result->name));
}
int read_setting_filename(void *handle, const char *name, Filename *result)
{
    return !!read_setting_s(handle, name, result->path, sizeof(result->path));
}

void write_setting_fontspec(void *handle, const char *name, FontSpec result)
{
    write_setting_s(handle, name, result.name);
}
void write_setting_filename(void *handle, const char *name, Filename result)
{
    write_setting_s(handle, name, result.path);
}

void close_settings_r(void *handle)
{
    tree234 *tree = (tree234 *)handle;
    struct keyval *kv;

    if (!tree)
        return;

    while ( (kv = index234(tree, 0)) != NULL) {
        del234(tree, kv);
        sfree((char *)kv->key);
        sfree((char *)kv->value);
        sfree(kv);
    }

    freetree234(tree);
}

void del_settings(const char *sessionname)
{
    char filename[FNLEN];
    make_filename(filename, INDEX_SESSION, sessionname);
    unlink(filename);
}

void *enum_settings_start(void)
{
    DIR *dp;
    char filename[FNLEN];

    make_filename(filename, INDEX_SESSIONDIR, NULL);
    dp = opendir(filename);

    return dp;
}

char *enum_settings_next(void *handle, char *buffer, int buflen)
{
    DIR *dp = (DIR *)handle;
    struct dirent *de;
    struct stat st;
    char fullpath[FNLEN];
    int len;
    char *unmunged;

    make_filename(fullpath, INDEX_SESSIONDIR, NULL);
    len = strlen(fullpath);

    while ( (de = readdir(dp)) != NULL ) {
        if (len < FNLEN) {
            fullpath[len] = '/';
            strncpy(fullpath+len+1, de->d_name, FNLEN-(len+1));
            fullpath[FNLEN-1] = '\0';
        }

        if (stat(fullpath, &st) < 0 || !S_ISREG(st.st_mode))
            continue;                  /* try another one */

        unmunged = unmungestr(de->d_name);
        strncpy(buffer, unmunged, buflen);
        buffer[buflen-1] = '\0';
        sfree(unmunged);
        return buffer;
    }

    return NULL;
}

void enum_settings_finish(void *handle)
{
    DIR *dp = (DIR *)handle;
    closedir(dp);
}

/*
 * Lines in the host keys file are of the form
 * 
 *   type@port:hostname keydata
 * 
 * e.g.
 * 
 *   rsa@22:foovax.example.org 0x23,0x293487364395345345....2343
 */
int verify_host_key(const char *hostname, int port,
		    const char *keytype, const char *key)
{
    FILE *fp;
    char filename[FNLEN];
    char *line;
    int ret;

    make_filename(filename, INDEX_HOSTKEYS, NULL);
    fp = fopen(filename, "r");
    if (!fp)
	return 1;		       /* key does not exist */

    ret = 1;
    while ( (line = fgetline(fp)) ) {
	int i;
	char *p = line;
	char porttext[20];

	line[strcspn(line, "\n")] = '\0';   /* strip trailing newline */

	i = strlen(keytype);
	if (strncmp(p, keytype, i))
	    goto done;
	p += i;

	if (*p != '@')
	    goto done;
	p++;

	sprintf(porttext, "%d", port);
	i = strlen(porttext);
	if (strncmp(p, porttext, i))
	    goto done;
	p += i;

	if (*p != ':')
	    goto done;
	p++;

	i = strlen(hostname);
	if (strncmp(p, hostname, i))
	    goto done;
	p += i;

	if (*p != ' ')
	    goto done;
	p++;

	/*
	 * Found the key. Now just work out whether it's the right
	 * one or not.
	 */
	if (!strcmp(p, key))
	    ret = 0;		       /* key matched OK */
	else
	    ret = 2;		       /* key mismatch */

	done:
	sfree(line);
	if (ret != 1)
	    break;
    }

    fclose(fp);
    return ret;
}

void store_host_key(const char *hostname, int port,
		    const char *keytype, const char *key)
{
    FILE *rfp, *wfp;
    char *newtext, *line;
    int headerlen;
    char filename[FNLEN], tmpfilename[FNLEN];

    newtext = dupprintf("%s@%d:%s %s\n", keytype, port, hostname, key);
    headerlen = 1 + strcspn(newtext, " ");   /* count the space too */

    /*
     * Open both the old file and a new file.
     */
    make_filename(tmpfilename, INDEX_HOSTKEYS_TMP, NULL);
    wfp = fopen(tmpfilename, "w");
    if (!wfp) {
        char dir[FNLEN];

        make_filename(dir, INDEX_DIR, NULL);
        mkdir(dir, 0700);
        wfp = fopen(tmpfilename, "w");
    }
    if (!wfp)
	return;
    make_filename(filename, INDEX_HOSTKEYS, NULL);
    rfp = fopen(filename, "r");

    /*
     * Copy all lines from the old file to the new one that _don't_
     * involve the same host key identifier as the one we're adding.
     */
    if (rfp) {
        while ( (line = fgetline(rfp)) ) {
            if (strncmp(line, newtext, headerlen))
                fputs(line, wfp);
        }
        fclose(rfp);
    }

    /*
     * Now add the new line at the end.
     */
    fputs(newtext, wfp);

    fclose(wfp);

    rename(tmpfilename, filename);

    sfree(newtext);
}

void read_random_seed(noise_consumer_t consumer)
{
    int fd;
    char fname[FNLEN];

    make_filename(fname, INDEX_RANDSEED, NULL);
    fd = open(fname, O_RDONLY);
    if (fd) {
	char buf[512];
	int ret;
	while ( (ret = read(fd, buf, sizeof(buf))) > 0)
	    consumer(buf, ret);
	close(fd);
    }
}

void write_random_seed(void *data, int len)
{
    int fd;
    char fname[FNLEN];

    make_filename(fname, INDEX_RANDSEED, NULL);
    /*
     * Don't truncate the random seed file if it already exists; if
     * something goes wrong half way through writing it, it would
     * be better to leave the old data there than to leave it empty.
     */
    fd = open(fname, O_CREAT | O_WRONLY, 0600);
    if (fd < 0) {
	char dir[FNLEN];

	make_filename(dir, INDEX_DIR, NULL);
	mkdir(dir, 0700);
	fd = open(fname, O_CREAT | O_WRONLY, 0600);
    }

    while (len > 0) {
	int ret = write(fd, data, len);
	if (ret <= 0) break;
	len -= ret;
	data = (char *)data + len;
    }

    close(fd);
}

void cleanup_all(void)
{
}
