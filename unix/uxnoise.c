/*
 * Noise generation for PuTTY's cryptographic random number
 * generator.
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>

#include "putty.h"
#include "ssh.h"
#include "storage.h"

/*
 * FIXME. This module currently depends critically on /dev/urandom,
 * because it has no fallback mechanism for doing anything else.
 */

static void read_dev_urandom(char *buf, int len)
{
    int fd;
    int ngot, ret;

    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
	perror("/dev/urandom: open");
	exit(1);
    }

    ngot = 0;
    while (ngot < len) {
	ret = read(fd, buf+ngot, len-ngot);
	if (ret < 0) {
	    perror("/dev/urandom: read");
	    exit(1);
	}
	ngot += ret;
    }
}

/*
 * This function is called once, at PuTTY startup. Currently it
 * will read 32 bytes out of /dev/urandom and seed the internal
 * generator with them.
 */

void noise_get_heavy(void (*func) (void *, int))
{
    char buf[32];
    read_dev_urandom(buf, sizeof(buf));
    func(buf, sizeof(buf));
}

void random_save_seed(void)
{
    /* Currently we do nothing here. FIXME? */
}

/*
 * This function is called every time the urandom pool needs
 * stirring, and will acquire the system time.
 */
void noise_get_light(void (*func) (void *, int))
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    func(&tv, sizeof(tv));
}

/*
 * This function is called on a timer, and it will just pull some
 * stuff out of /dev/urandom. FIXME: really I suspect we ought not
 * to deplete /dev/urandom like this. Better to grab something more
 * harmless.
 */
void noise_regular(void)
{
    char buf[4];
    read_dev_urandom(buf, sizeof(buf));
    random_add_noise(buf, sizeof(buf));
}

/*
 * This function is called on every keypress or mouse move, and
 * will add the current time to the noise pool. It gets the scan
 * code or mouse position passed in, and adds that too.
 */
void noise_ultralight(unsigned long data)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    random_add_noise(&tv, sizeof(tv));
    random_add_noise(&data, sizeof(data));
}
