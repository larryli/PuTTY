/*
 * mac.h -- macintosh-specific declarations
 */

#ifndef PUTTY_MAC_H
#define PUTTY_MAC_H

#include <MacTypes.h>
#include <Events.h>
#include <Gestalt.h>
#include <MacWindows.h>
#include <Palettes.h>

struct mac_gestalts {
    long qdvers;
    long apprvers;
    long cntlattr;
    long windattr;
    long thdsattr;
};

extern struct mac_gestalts mac_gestalts;

#define HAVE_COLOR_QD() (mac_gestalts.qdvers > gestaltOriginalQD)

/* from macterm.c */
extern void mac_newsession(void);
extern void mac_activateterm(WindowPtr, Boolean);
extern void mac_adjusttermcursor(WindowPtr, Point, RgnHandle);
extern void mac_adjusttermmenus(WindowPtr);
extern void mac_updateterm(WindowPtr);
extern void mac_clickterm(WindowPtr, EventRecord *);
extern void mac_growterm(WindowPtr, EventRecord *);
extern void mac_keyterm(WindowPtr, EventRecord *);
extern void mac_menuterm(WindowPtr, short, short);
/* from maccfg.c */
extern void mac_loadconfig(Config *);
/* from macnet.c */
extern void macnet_eventcheck(void);

typedef struct {
    /* Config that created this session */
    Config cfg;
    /* Terminal emulator internal state */
    Terminal *term;
    /* Display state */
    int font_width, font_height;
    /* Line discipline */
    void *ldisc;
    /* Backend */
    Backend *back;
    void *backhandle;
    char *realhost;
    /* Logging */
    void *logctx;
    /* Conveniences */
    unsigned long attr_mask;		/* Mask of attributes to display */

    /* Mac-specific elements */
    short		fontnum;
    int			font_ascent;
    int			font_leading;
    int			font_boldadjust;
    WindowPtr		window;
    PaletteHandle	palette;
    ControlHandle	scrollbar;
    WCTabHandle		wctab;
    ThreadID		thread;
    int			raw_mouse;
} Session;

#endif

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */
