/*
 * uxstore.c: Unix-specific implementation of the interface defined
 * in storage.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
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

static Display *display;

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
    return XGetDefault(display, app_name, key);
}

void *open_settings_r(char *sessionname)
{
    static int thing_to_return_an_arbitrary_non_null_pointer_to;
    display = GDK_DISPLAY();
    if (!display)
	return NULL;
    else
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

int verify_host_key(char *hostname, int port, char *keytype, char *key)
{
    return 1;			       /* key does not exist in registry */
}

void store_host_key(char *hostname, int port, char *keytype, char *key)
{
}

void read_random_seed(noise_consumer_t consumer)
{
}

void write_random_seed(void *data, int len)
{
}

void cleanup_all(void)
{
}
