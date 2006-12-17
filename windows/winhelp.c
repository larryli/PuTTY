/*
 * winhelp.c: centralised functions to launch Windows help files,
 * and to decide whether to use .HLP or .CHM help in any given
 * situation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "putty.h"

#include <htmlhelp.h>

typedef HWND (CALLBACK *htmlhelp_t)(HWND, LPCSTR, UINT, DWORD);

static char *help_path, *chm_path;
static int help_has_contents;
static int requested_help;
static DWORD html_help_cookie;
static htmlhelp_t htmlhelp;

void init_help(void)
{
    char b[2048], *p, *q, *r;
    FILE *fp;

    GetModuleFileName(NULL, b, sizeof(b) - 1);
    r = b;
    p = strrchr(b, '\\');
    if (p && p >= r) r = p+1;
    q = strrchr(b, ':');
    if (q && q >= r) r = q+1;
    strcpy(r, PUTTY_HELP_FILE);
    if ( (fp = fopen(b, "r")) != NULL) {
	help_path = dupstr(b);
	fclose(fp);
    } else
	help_path = NULL;
    strcpy(r, PUTTY_HELP_CONTENTS);
    if ( (fp = fopen(b, "r")) != NULL) {
	help_has_contents = TRUE;
	fclose(fp);
    } else
	help_has_contents = FALSE;

    strcpy(r, PUTTY_CHM_FILE);
    if ( (fp = fopen(b, "r")) != NULL) {
	chm_path = dupstr(b);
	fclose(fp);
    } else
	chm_path = NULL;
    if (chm_path) {
	HINSTANCE dllHH = LoadLibrary("hhctrl.ocx");
	if (dllHH) {
	    htmlhelp = (htmlhelp_t)GetProcAddress(dllHH, "HtmlHelpA");
	    if (!htmlhelp)
		FreeLibrary(dllHH);
	}
	if (htmlhelp)
	    htmlhelp(NULL, NULL, HH_INITIALIZE, (DWORD)&html_help_cookie);
	else
	    chm_path = NULL;
    }
}

void shutdown_help(void)
{
    if (chm_path)
	htmlhelp(NULL, NULL, HH_UNINITIALIZE, html_help_cookie);
}

int has_help(void)
{
    /*
     * FIXME: it would be nice here to disregard help_path on
     * platforms that didn't have WINHLP32. But that's probably
     * unrealistic, since even Vista will have it if the user
     * specifically downloads it.
     */
    return (help_path || chm_path);
}

void launch_help(HWND hwnd, const char *topic)
{
    if (topic) {
	int colonpos = strcspn(topic, ":");

	if (chm_path) {
	    char *fname;
	    assert(topic[colonpos] != '\0');
	    fname = dupprintf("%s::/%s.html>main", chm_path,
			      topic + colonpos + 1);
	    htmlhelp(hwnd, fname, HH_DISPLAY_TOPIC, 0);
	    sfree(fname);
	} else if (help_path) {
	    char *cmd = dupprintf("JI(`',`%.*s')", colonpos, topic);
	    WinHelp(hwnd, help_path, HELP_COMMAND, (DWORD)cmd);
	    sfree(cmd);
	}
    } else {
	if (chm_path) {
	    htmlhelp(hwnd, chm_path, HH_DISPLAY_TOPIC, 0);
	} else if (help_path) {
	    WinHelp(hwnd, help_path,
		    help_has_contents ? HELP_FINDER : HELP_CONTENTS, 0);
	}
    }
    requested_help = TRUE;
}

void quit_help(HWND hwnd)
{
    if (requested_help) {
	if (chm_path) {
	    htmlhelp(NULL, NULL, HH_CLOSE_ALL, 0);
	} else if (help_path) {
	    WinHelp(hwnd, help_path, HELP_QUIT, 0);
	}
	requested_help = FALSE;
    }
}
