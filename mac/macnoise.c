/*
 * Noise generation for PuTTY's cryptographic random number
 * generator.
 */

#include <Processes.h>
#include <Types.h>
#include <Timer.h>

#include "putty.h"
#include "ssh.h"
#include "storage.h"

/*
 * This function is called once, at PuTTY startup, and will do some
 * seriously silly things like listing directories and getting disk
 * free space and a process snapshot.
 */

static void noise_get_processes(void (*func) (void *, int))
{
    ProcessSerialNumber psn = {0, kNoProcess};
    ProcessInfoRec info;

    for (;;) {
	GetNextProcess(&psn);
	if (psn.highLongOfPSN == 0 && psn.lowLongOfPSN == kNoProcess) return;
	info.processInfoLength = sizeof(info);
	info.processName = NULL;
	info.processAppSpec = NULL;
	GetProcessInformation(&psn, &info);
	func(&info, sizeof(info));
    }
}

void noise_get_heavy(void (*func) (void *, int))
{

    noise_get_light(func);
    noise_get_processes(func);
    read_random_seed(func);
    /* Update the seed immediately, in case another instance uses it. */
    random_save_seed();
}

void random_save_seed(void)
{
    int len;
    void *data;

    if (random_active) {
	random_get_savedata(&data, &len);
	write_random_seed(data, len);
	sfree(data);
    }
}

/*
 * This function is called every time the random pool needs
 * stirring, and will acquire the system time.
 */
void noise_get_light(void (*func) (void *, int))
{
    UnsignedWide utc;

    Microseconds(&utc);
    func(&utc, sizeof(utc));
}

/*
 * This function is called on every keypress or mouse move, and
 * will add the current time to the noise pool. It gets the scan
 * code or mouse position passed in, and adds that too.
 */
void noise_ultralight(unsigned long data)
{
    UnsignedWide utc;

    Microseconds(&utc);
    random_add_noise(&utc, sizeof(utc));
    random_add_noise(&data, sizeof(data));
}

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */
