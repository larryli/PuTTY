/*
 * uxstore.c: Unix-specific implementation of the interface defined
 * in storage.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include "putty.h"
#include "storage.h"

/* FIXME. For the moment, we do nothing at all here. */

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

void *open_settings_r(char *sessionname)
{
    return NULL;
}

char *read_setting_s(void *handle, char *key, char *buffer, int buflen)
{
    return NULL;
}

int read_setting_i(void *handle, char *key, int defvalue)
{
    return defvalue;
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
