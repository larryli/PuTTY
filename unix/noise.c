/*
 * Noise generation for PuTTY's cryptographic random number
 * generator.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "putty.h"
#include "ssh.h"
#include "storage.h"

static bool read_dev_urandom(char *buf, int len)
{
    int fd;
    int ngot, ret;

    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return false;

    ngot = 0;
    while (ngot < len) {
        ret = read(fd, buf+ngot, len-ngot);
        if (ret < 0) {
            close(fd);
            return false;
        }
        ngot += ret;
    }

    close(fd);

    return true;
}

/*
 * This function is called once, at PuTTY startup. It will do some
 * slightly silly things such as fetching an entire process listing
 * and scanning /tmp, load the saved random seed from disk, and
 * also read 32 bytes out of /dev/urandom.
 */

void noise_get_heavy(void (*func) (void *, int))
{
    char buf[512];
    FILE *fp;
    int ret;
    bool got_dev_urandom = false;

    if (read_dev_urandom(buf, 32)) {
        got_dev_urandom = true;
        func(buf, 32);
    }

    fp = popen("ps -axu 2>/dev/null", "r");
    if (fp) {
        while ( (ret = fread(buf, 1, sizeof(buf), fp)) > 0)
            func(buf, ret);
        pclose(fp);
    } else if (!got_dev_urandom) {
        fprintf(stderr, "popen: %s\n"
                "Unable to access fallback entropy source\n", strerror(errno));
        exit(1);
    }

    fp = popen("ls -al /tmp 2>/dev/null", "r");
    if (fp) {
        while ( (ret = fread(buf, 1, sizeof(buf), fp)) > 0)
            func(buf, ret);
        pclose(fp);
    } else if (!got_dev_urandom) {
        fprintf(stderr, "popen: %s\n"
                "Unable to access fallback entropy source\n", strerror(errno));
        exit(1);
    }

    read_random_seed(func);
}

/*
 * This function is called on a timer, and grabs as much changeable
 * system data as it can quickly get its hands on.
 */
void noise_regular(void)
{
    int fd;
    int ret;
    char buf[512];
    struct rusage rusage;

    if ((fd = open("/proc/meminfo", O_RDONLY)) >= 0) {
        while ( (ret = read(fd, buf, sizeof(buf))) > 0)
            random_add_noise(NOISE_SOURCE_MEMINFO, buf, ret);
        close(fd);
    }
    if ((fd = open("/proc/stat", O_RDONLY)) >= 0) {
        while ( (ret = read(fd, buf, sizeof(buf))) > 0)
            random_add_noise(NOISE_SOURCE_STAT, buf, ret);
        close(fd);
    }
    getrusage(RUSAGE_SELF, &rusage);
    random_add_noise(NOISE_SOURCE_RUSAGE, &rusage, sizeof(rusage));
}

/*
 * This function is called on every keypress or mouse move, and
 * will add the current time to the noise pool. It gets the scan
 * code or mouse position passed in, and adds that too.
 */
void noise_ultralight(NoiseSourceId id, unsigned long data)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    random_add_noise(NOISE_SOURCE_TIME, &tv, sizeof(tv));
    random_add_noise(id, &data, sizeof(data));
}

uint64_t prng_reseed_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}
