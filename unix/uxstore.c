/*
 * uxstore.c: Unix-specific implementation of the interface defined
 * in storage.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include "putty.h"
#include "storage.h"

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
    char *val = XGetDefault(display, app_name, key);
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
    char *val = XGetDefault(display, app_name, key);
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
