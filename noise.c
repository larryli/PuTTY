/*
 * Noise generation for PuTTY's cryptographic random number
 * generator.
 */

#include <windows.h>
#include <stdio.h>

#include "putty.h"
#include "ssh.h"

static char seedpath[2*MAX_PATH+10] = "\0";

/*
 * Find the random seed file path and store it in `seedpath'.
 */
static void get_seedpath(void) {
    HKEY rkey;
    DWORD type, size;

    size = sizeof(seedpath);

    if (RegOpenKey(HKEY_CURRENT_USER, PUTTY_REG_POS, &rkey)==ERROR_SUCCESS) {
	int ret = RegQueryValueEx(rkey, "RandSeedFile",
				  0, &type, seedpath, &size);
	if (ret != ERROR_SUCCESS || type != REG_SZ)
	    seedpath[0] = '\0';
	RegCloseKey(rkey);
    } else
	seedpath[0] = '\0';

    if (!seedpath[0]) {
	int len, ret;

	len = GetEnvironmentVariable("HOMEDRIVE", seedpath, sizeof(seedpath));
	ret = GetEnvironmentVariable("HOMEPATH", seedpath+len,
				      sizeof(seedpath)-len);
	if (ret == 0) {		       /* probably win95; store in \WINDOWS */
	    GetWindowsDirectory(seedpath, sizeof(seedpath));
	    len = strlen(seedpath);
	} else
	    len += ret;
	strcpy(seedpath+len, "\\PUTTY.RND");
    }
}

/*
 * This function is called once, at PuTTY startup, and will do some
 * seriously silly things like listing directories and getting disk
 * free space and a process snapshot.
 */

void noise_get_heavy(void (*func) (void *, int)) {
    HANDLE srch;
    HANDLE seedf;
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

    if (!seedpath[0])
	get_seedpath();

    seedf = CreateFile(seedpath, GENERIC_READ,
		       FILE_SHARE_READ | FILE_SHARE_WRITE,
		       NULL, OPEN_EXISTING, 0, NULL);

    if (seedf != INVALID_HANDLE_VALUE) {
	while (1) {
	    char buf[1024];
	    DWORD len;

	    if (ReadFile(seedf, buf, sizeof(buf), &len, NULL) && len)
		func(buf, len);
	    else
		break;
	}
	CloseHandle(seedf);
    }
}

void random_save_seed(void) {
    HANDLE seedf;

    if (!seedpath[0])
	get_seedpath();

    seedf = CreateFile(seedpath, GENERIC_WRITE, 0,
		       NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (seedf != INVALID_HANDLE_VALUE) {
	int len;
	DWORD lenwritten;
	void *data;

	random_get_savedata(&data, &len);
	WriteFile(seedf, data, len, &lenwritten, NULL);
	CloseHandle(seedf);
    }
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

    if (GetSystemPowerStatus(&pwrstat))
	func(&pwrstat, sizeof(pwrstat));
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
