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

#include "charset.h"

struct mac_gestalts {
    long sysvers;
    long qdvers;
    long apprvers;
    long cntlattr;
    long windattr;
    long encvvers; /* TEC version (from TECGetInfo()) */
    long uncvattr; /* Unicode Converter attributes (frem TECGetInfo()) */
};

extern struct mac_gestalts mac_gestalts;

#if TARGET_RT_MAC_CFM
/* All systems that can use CFM have Color QuickDraw */
#define HAVE_COLOR_QD() 1
#else
#define HAVE_COLOR_QD() (mac_gestalts.qdvers > gestaltOriginalQD)
#endif

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
    UnicodeToTextInfo	uni_to_font;  /* Only one of uni_to_font and	 */
    charset_t		font_charset; /* font_charset is used at a time. */
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
extern OSErr get_session_dir(Boolean makeit, short *pVRefNum, long *pDirID);
extern void *open_settings_r_fsp(FSSpec *);
/* from macucs.c */
extern void init_ucs(void);
/* from mtcpnet.c */
extern OSErr mactcp_init(void);
extern void mactcp_shutdown(void);

#endif

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */
