/*
 * ux_x11.c: fetch local auth data for X forwarding.
 */

#include <ctype.h>
#include <unistd.h>
#include "putty.h"

void platform_get_x11_auth(char *display, int *protocol,
                           unsigned char *data, int *datalen)
{
    FILE *fp;
    char *command;
    int maxsize = *datalen;
    char *localbuf;

    /*
     * Normally we should run `xauth list DISPLAYNAME'. However,
     * there's an oddity when the display is local: the display
     * `localhost:0' (or `:0') should become just `:0'.
     */
    if (!strncmp(display, "localhost:", 10))
	command = dupprintf("xauth list %s 2>/dev/null", display+9);
    else
	command = dupprintf("xauth list %s 2>/dev/null", display);
    fp = popen(command, "r");
    sfree(command);

    if (!fp)
        return;                        /* assume no auth */

    localbuf = smalloc(maxsize);

    while (1) {
        /*
         * Read a line from stdin, and attempt to parse it into a
         * display name (ignored), auth protocol, and auth string.
         */
        int c, i, hexdigit, proto;
        char protoname[64];

        /* Skip the display name. */
        while (c = getc(fp), c != EOF && c != '\n' && !isspace(c));
        if (c == EOF) break;
        if (c == '\n') continue;

        /* Skip white space. */
        while (c != EOF && c != '\n' && isspace(c))
            c = getc(fp);
        if (c == EOF) break;
        if (c == '\n') continue;

        /* Read the auth protocol name, and see if it matches any we
         * know about. */
        i = 0;
        while (c != EOF && c != '\n' && !isspace(c)) {
            if (i < lenof(protoname)-1) protoname[i++] = c;
            c = getc(fp);
        }
        protoname[i] = '\0';

        for (i = X11_NO_AUTH; ++i < X11_NAUTHS ;) {
            if (!strcmp(protoname, x11_authnames[i]))
                break;
        }
        if (i >= X11_NAUTHS || i <= proto) {
            /* Unrecognised protocol name, or a worse one than we already have.
	     * Skip this line. */
            while (c != EOF && c != '\n')
                c = getc(fp);
            if (c == EOF) break;
        }
        proto = i;

        /* Skip white space. */
        while (c != EOF && c != '\n' && isspace(c))
            c = getc(fp);
        if (c == EOF) break;
        if (c == '\n') continue;

        /*
         * Now grab pairs of hex digits and shove them into `data'.
         */
        i = 0;
        hexdigit = -1;
        while (c != EOF && c != '\n') {
            int hexval = -1;
            if (c >= 'A' && c <= 'F')
                hexval = c + 10 - 'A';
            if (c >= 'a' && c <= 'f')
                hexval = c + 10 - 'a';
            if (c >= '0' && c <= '9')
                hexval = c - '0';
            if (hexval >= 0) {
                if (hexdigit >= 0) {
                    hexdigit = (hexdigit << 4) + hexval;
                    if (i < maxsize)
                        localbuf[i++] = hexdigit;
                    hexdigit = -1;
                } else
                    hexdigit = hexval;
            }
            c = getc(fp);
        }

        *datalen = i;
        *protocol = proto;
	memcpy(data, localbuf, i);

	/* Nonetheless, continue looping round; we might find a better one. */
    }
    pclose(fp);
    sfree(localbuf);
}
