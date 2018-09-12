/*
 * ldisc.h: defines the Ldisc data structure used by ldisc.c and
 * ldiscucs.c. (Unfortunately it was necessary to split the ldisc
 * module in two, to avoid unnecessarily linking in the Unicode
 * stuff in tools that don't require it.)
 */

#ifndef PUTTY_LDISC_H
#define PUTTY_LDISC_H

struct Ldisc_tag {
    Terminal *term;
    Backend *backend;
    Frontend *frontend;

    /*
     * Values cached out of conf.
     */
    int telnet_keyboard, telnet_newline, protocol, localecho, localedit;

    char *buf;
    int buflen, bufsiz, quotenext;
};

#endif /* PUTTY_LDISC_H */
