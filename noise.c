/*
 * Noise generation for PuTTY's cryptographic random number
 * generator.
 */

#include <windows.h>
#include <stdio.h>

#include "putty.h"
#include "ssh.h"
#include "storage.h"

/*
 * This function is called once, at PuTTY startup, and will do some
 * seriously silly things like listing directories and getting disk
 * free space and a process snapshot.
 */

void noise_get_heavy(void (*func) (void *, int)) {
    HANDLE srch;
    WIN32_FIND_DATA finddata;
    char winpath[MAX_PATH+3];

    GetWindowsDirectory(winpath, sizeof(winpath));
    strcat(winpath, "\\*");
    srch = FindFirstFile(winpath, &finddata);
    if (srch != INVALID_HANDLE_VALUE) {
	do {
	    func(&finddata, sizeof(finddata));
	} while (FindNextFile(srch, &finddata));
	FindClose(srch);
    }

    read_random_seed(func);
}

void random_save_seed(void) {
    int len;
    void *data;

    random_get_savedata(&data, &len);
    write_random_seed(data, len);
}

/*
 * This function is called every time the random pool needs
 * stirring, and will acquire the system time in all available
 * forms and the battery status.
 */
void noise_get_light(void (*func) (void *, int)) {
    SYSTEMTIME systime;
    DWORD adjust[2];
    BOOL rubbish;
    SYSTEM_POWER_STATUS pwrstat;

    GetSystemTime(&systime);
    func(&systime, sizeof(systime));

    GetSystemTimeAdjustment(&adjust[0], &adjust[1], &rubbish);
    func(&adjust, sizeof(adjust));

#ifndef WIN32S_COMPAT
    if (GetSystemPowerStatus(&pwrstat))
	func(&pwrstat, sizeof(pwrstat));
#endif
}

/*
 * This function is called on every keypress or mouse move, and
 * will add the current Windows time and performance monitor
 * counter to the noise pool. It gets the scan code or mouse
 * position passed in.
 */
void noise_ultralight(DWORD data) {
    DWORD wintime;
    LARGE_INTEGER perftime;

    random_add_noise(&data, sizeof(DWORD));

    wintime = GetTickCount();
    random_add_noise(&wintime, sizeof(DWORD));

    if (QueryPerformanceCounter(&perftime))
	random_add_noise(&perftime, sizeof(perftime));
}
