/*
 * mac.h -- macintosh-specific declarations
 */

#ifndef _PUTTY_MAC_H
#define _PUTTY_MAC_H

#include <MacTypes.h>
#include <MacWindows.h>

extern long mac_qdversion;

/* from macterm.c */
extern void mac_newsession(void);
extern void mac_activateterm(WindowPtr, Boolean);
extern void mac_updateterm(WindowPtr);

extern void mac_loadconfig(Config *);

#endif
