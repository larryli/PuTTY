/*
 * uxstore.c: Unix-specific implementation of the interface defined
 * in storage.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "putty.h"
#include "storage.h"
#include "tree234.h"

/*
 * For the moment, the only existing Unix utility is pterm and that
 * has no GUI configuration at all, so our write routines need do
 * nothing. Eventually I suppose these will read and write an rc
 * file somewhere or other.
 */

void *open_settings_w(char *sessionname)
{
    return NULL;
}

void write_setting_s(void *handle, char *key, char *value)
{
}

void write_setting_i(void *handle, char *key, int value)
{
}

void close_settings_w(void *handle)
{
}

/*
 * Reading settings, for the moment, is done by retrieving X
 * resources from the X display. When we introduce disk files, I
 * think what will happen is that the X resources will override
 * PuTTY's inbuilt defaults, but that the disk files will then
 * override those. This isn't optimal, but it's the best I can
 * immediately work out.
 */

struct xrm_string {
    char *key;
    char *value;
};

static tree234 *xrmtree = NULL;

int xrmcmp(void *av, void *bv)
{
    struct xrm_string *a = (struct xrm_string *)av;
    struct xrm_string *b = (struct xrm_string *)bv;
    return strcmp(a->key, b->key);
}

void provide_xrm_string(char *string)
{
    char *p, *q;
    struct xrm_string *xrms, *ret;

    p = q = strchr(string, ':');
    if (!q) {
	fprintf(stderr, "pterm: expected a colon in resource string"
		" \"%s\"\n", string);
	return;
    }
    q++;
    while (p > string && p[-1] != '.' && p[-1] != '*')
	p--;
    xrms = smalloc(sizeof(struct xrm_string));
    xrms->key = smalloc(q-p);
    memcpy(xrms->key, p, q-p);
    xrms->key[q-p-1] = '\0';
    while (*q && isspace(*q))
	q++;
    xrms->value = dupstr(q);

    if (!xrmtree)
	xrmtree = newtree234(xrmcmp);

    ret = add234(xrmtree, xrms);
    if (ret) {
	/* Override an existing string. */
	del234(xrmtree, ret);
	add234(xrmtree, xrms);
    }
}

char *get_setting(char *key)
{
    struct xrm_string tmp, *ret;
    tmp.key = key;
    if (xrmtree) {
	ret = find234(xrmtree, &tmp, NULL);
	if (ret)
	    return ret->value;
    }
    return x_get_default(key);
}

void *open_settings_r(char *sessionname)
{
    static int thing_to_return_an_arbitrary_non_null_pointer_to;
    return &thing_to_return_an_arbitrary_non_null_pointer_to;
}

char *read_setting_s(void *handle, char *key, char *buffer, int buflen)
{
    char *val = get_setting(key);
    if (!val)
	return NULL;
    else {
	strncpy(buffer, val, buflen);
	buffer[buflen-1] = '\0';
	return buffer;
    }
}

int read_setting_i(void *handle, char *key, int defvalue)
{
    char *val = get_setting(key);
    if (!val)
	return defvalue;
    else
	return atoi(val);
}

void close_settings_r(void *handle)
{
}

void del_settings(char *sessionname)
{
}

void *enum_settings_start(void)
{
    return NULL;
}

char *enum_settings_next(void *handle, char *buffer, int buflen)
{
    return NULL;
}

void enum_settings_finish(void *handle)
{
}

enum {
    INDEX_DIR, INDEX_HOSTKEYS, INDEX_RANDSEED
};

static void make_filename(char *filename, int index)
{
    char *home;
    int len;
    home = getenv("HOME");
    strncpy(filename, home, FILENAME_MAX);
    len = strlen(filename);
    strncpy(filename + len,
	    index == INDEX_DIR ? "/.putty" :
	    index == INDEX_HOSTKEYS ? "/.putty/sshhostkeys" :
	    index == INDEX_RANDSEED ? "/.putty/randomseed" :
	    "/.putty/ERROR", FILENAME_MAX - len);
    filename[FILENAME_MAX-1] = '\0';
}

/*
 * Read an entire line of text from a file. Return a buffer
 * malloced to be as big as necessary (caller must free).
 */
static char *fgetline(FILE *fp)
{
    char *ret = smalloc(512);
    int size = 512, len = 0;
    while (fgets(ret + len, size - len, fp)) {
	len += strlen(ret + len);
	if (ret[len-1] == '\n')
	    break;		       /* got a newline, we're done */
	size = len + 512;
	ret = srealloc(ret, size);
    }
    if (len == 0) {		       /* first fgets returned NULL */
	sfree(ret);
	return NULL;
    }
    ret[len] = '\0';
    return ret;
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
int verify_host_key(char *hostname, int port, char *keytype, char *key)
{
    FILE *fp;
    char filename[FILENAME_MAX];
    char *line;
    int ret;

    make_filename(filename, INDEX_HOSTKEYS);
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

    return ret;
}

void store_host_key(char *hostname, int port, char *keytype, char *key)
{
    FILE *fp;
    int fd;
    char filename[FILENAME_MAX];

    make_filename(filename, INDEX_HOSTKEYS);
    fd = open(filename, O_CREAT | O_APPEND | O_RDWR, 0600);
    if (fd < 0) {
	char dir[FILENAME_MAX];

	make_filename(dir, INDEX_DIR);
	mkdir(dir, 0700);
	fd = open(filename, O_CREAT | O_APPEND | O_RDWR, 0600);
    }
    if (fd < 0) {
	perror(filename);
	exit(1);
    }
    fp = fdopen(fd, "a");
    fprintf(fp, "%s@%d:%s %s\n", keytype, port, hostname, key);
    fclose(fp);
}

void read_random_seed(noise_consumer_t consumer)
{
    int fd;
    char fname[FILENAME_MAX];

    make_filename(fname, INDEX_RANDSEED);
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
    char fname[FILENAME_MAX];

    make_filename(fname, INDEX_RANDSEED);
    /*
     * Don't truncate the random seed file if it already exists; if
     * something goes wrong half way through writing it, it would
     * be better to leave the old data there than to leave it empty.
     */
    fd = open(fname, O_CREAT | O_WRONLY, 0600);
    if (fd < 0) {
	char dir[FILENAME_MAX];

	make_filename(dir, INDEX_DIR);
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
