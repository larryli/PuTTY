/*
 * keygen-noise.c: Unix implementation of get_heavy_noise() from cmdgen.c.
 */

#include <stdio.h>
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>

#include "putty.h"

char *get_random_data(int len, const char *device)
{
    char *buf = snewn(len, char);
    int fd;
    int ngot, ret;

    if (!device) {
        static const char *const default_devices[] = {
            "/dev/urandom", "/dev/random"
        };
        size_t i;

        for (i = 0; i < lenof(default_devices); i++) {
            if (access(default_devices[i], R_OK) == 0) {
                device = default_devices[i];
                break;
            }
        }

        if (!device) {
            sfree(buf);
            fprintf(stderr, "puttygen: cannot find a readable "
                    "random number source; use --random-device\n");
            return NULL;
        }
    }

    fd = open(device, O_RDONLY);
    if (fd < 0) {
        sfree(buf);
        fprintf(stderr, "puttygen: %s: open: %s\n",
                device, strerror(errno));
        return NULL;
    }

    ngot = 0;
    while (ngot < len) {
        ret = read(fd, buf+ngot, len-ngot);
        if (ret < 0) {
            close(fd);
            sfree(buf);
            fprintf(stderr, "puttygen: %s: read: %s\n",
                    device, strerror(errno));
            return NULL;
        }
        ngot += ret;
    }

    close(fd);

    return buf;
}
