/*
 * ux_x11.c: fetch local auth data for X forwarding.
 */

#include <ctype.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>

#include "putty.h"
#include "ssh.h"
#include "network.h"

void platform_get_x11_auth(struct X11Display *disp, const Config *cfg)
{
    char *xauthfile;
    int needs_free;

    /*
     * Upgrade an IP-style localhost display to a Unix-socket
     * display.
     */
    if (!disp->unixdomain && sk_address_is_local(disp->addr)) {
	sk_addr_free(disp->addr);
	disp->unixdomain = TRUE;
	disp->addr = platform_get_x11_unix_address(NULL, disp->displaynum);
	disp->realhost = dupprintf("unix:%d", disp->displaynum);
	disp->port = 0;
    }

    /*
     * Set the hostname for Unix-socket displays, so that we'll
     * look it up correctly in the X authority file.
     */
    if (disp->unixdomain) {
	int len;

	sfree(disp->hostname);
	len = 128;
	do {
	    len *= 2;
	    disp->hostname = snewn(len, char);
	    if (gethostname(disp->hostname, len) < 0) {
		disp->hostname = NULL;
		return;
	    }
	} while (strlen(disp->hostname) >= len-1);
    }

    /*
     * Find the .Xauthority file.
     */
    needs_free = FALSE;
    xauthfile = getenv("XAUTHORITY");
    if (!xauthfile) {
	xauthfile = getenv("HOME");
	if (xauthfile) {
	    xauthfile = dupcat(xauthfile, "/.Xauthority", NULL);
	    needs_free = TRUE;
	}
    }

    if (xauthfile) {
	x11_get_auth_from_authfile(disp, xauthfile);
	if (needs_free)
	    sfree(xauthfile);
    }
}

const int platform_uses_x11_unix_by_default = TRUE;
