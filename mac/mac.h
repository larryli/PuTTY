/*
 * mac.h -- macintosh-specific declarations
 */

#ifndef PUTTY_MAC_H
#define PUTTY_MAC_H

#include <MacTypes.h>
#include <Controls.h>
#include <Events.h>
#include <Gestalt.h>
#include <MacWindows.h>
#include <Palettes.h>
#include <UnicodeConverter.h>

struct mac_gestalts {
    long sysvers;
    long qdvers;
    long apprvers;
    long cntlattr;
    long windattr;
    long encvvers;
};

extern struct mac_gestalts mac_gestalts;

#define HAVE_COLOR_QD() (mac_gestalts.qdvers > gestaltOriginalQD)

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

    /* Mac-specific elements */
    short		fontnum;
    int			font_ascent;
    int			font_leading;
    int			font_boldadjust;
    WindowPtr		window;
    WindowPtr		settings_window;
    PaletteHandle	palette;
    ControlHandle	scrollbar;
    WCTabHandle		wctab;
    int			raw_mouse;
    UnicodeToTextInfo	uni_to_font;
} Session;

/* from macdlg.c */
extern void mac_newsession(void);
extern void mac_clickdlg(WindowPtr, EventRecord *);
extern void mac_activatedlg(WindowPtr, EventRecord *);
/* from macterm.c */
extern void mac_opensession(void);
extern void mac_startsession(Session *);
extern void mac_activateterm(WindowPtr, Boolean);
extern void mac_adjusttermcursor(WindowPtr, Point, RgnHandle);
extern void mac_adjusttermmenus(WindowPtr);
extern void mac_updateterm(WindowPtr);
extern void mac_clickterm(WindowPtr, EventRecord *);
extern void mac_growterm(WindowPtr, EventRecord *);
extern void mac_keyterm(WindowPtr, EventRecord *);
extern void mac_menuterm(WindowPtr, short, short);
/* from macstore.c */
OSErr get_session_dir(Boolean makeit, short *pVRefNum, long *pDirID);
extern void *open_settings_r_fsp(FSSpec *);

#endif

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */
