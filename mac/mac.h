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
    long mtcpvers;
    long otptattr;
};

extern struct mac_gestalts mac_gestalts;

#if TARGET_RT_MAC_CFM
/* All systems that can use CFM have Color QuickDraw */
#define HAVE_COLOR_QD() 1
#else
#define HAVE_COLOR_QD() (mac_gestalts.qdvers > gestaltOriginalQD)
#endif

typedef struct Session {
    struct Session *next;
    struct Session **prev;
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
    Point		font_stdnumer;
    Point		font_stddenom;
    Point		font_widenumer;
    Point		font_widedenom;
    Point		font_bignumer;
    Point		font_bigdenom;
    WindowPtr		window;
    WindowPtr		settings_window;
    PaletteHandle	palette;
    ControlHandle	scrollbar;
    WCTabHandle		wctab;
    int			raw_mouse;
    UnicodeToTextInfo	uni_to_font;  /* Only one of uni_to_font and	 */
    charset_t		font_charset; /* font_charset is used at a time. */
} Session;

extern Session *sesslist;

/* from macdlg.c */
extern void mac_newsession(void);
extern void mac_clickdlg(WindowPtr, EventRecord *);
extern void mac_activatedlg(WindowPtr, EventRecord *);
/* from macterm.c */
extern void mac_opensession(void);
extern void mac_startsession(Session *);
extern void mac_pollterm(void);
extern void mac_activateterm(WindowPtr, Boolean);
extern void mac_adjusttermcursor(WindowPtr, Point, RgnHandle);
extern void mac_adjusttermmenus(WindowPtr);
extern void mac_updateterm(WindowPtr);
extern void mac_clickterm(WindowPtr, EventRecord *);
extern void mac_growterm(WindowPtr, EventRecord *);
extern void mac_keyterm(WindowPtr, EventRecord *);
extern void mac_menuterm(WindowPtr, short, short);
/* from macstore.c */
extern OSErr get_putty_dir(Boolean makeit, short *pVRefNum, long *pDirID);
extern OSErr get_session_dir(Boolean makeit, short *pVRefNum, long *pDirID);
extern void *open_settings_r_fsp(FSSpec *);
/* from macucs.c */
extern void init_ucs(void);
/* from mtcpnet.c */
extern OSErr mactcp_init(void);
extern void mactcp_cleanup(void);
extern void mactcp_poll(void);
extern SockAddr mactcp_namelookup(char const *, char **);
extern SockAddr mactcp_nonamelookup(char const *);
extern void mactcp_getaddr(SockAddr, char *, int);
extern int mactcp_hostname_is_local(char *);
extern int mactcp_address_is_local(SockAddr);
extern int mactcp_addrtype(SockAddr);
extern void mactcp_addrcopy(SockAddr, char *);
extern void mactcp_addr_free(SockAddr);
extern Socket mactcp_register(void *, Plug);
extern Socket mactcp_new(SockAddr addr, int, int, int, int, Plug);
extern Socket mactcp_newlistener(char *, int, Plug, int);
extern char *mactcp_addr_error(SockAddr);
/* from otnet.c */
extern OSErr ot_init(void);
extern void ot_cleanup(void);
extern void ot_poll(void);
extern SockAddr ot_namelookup(char const *, char **);
extern SockAddr ot_nonamelookup(char const *);
extern void ot_getaddr(SockAddr, char *, int);
extern int ot_hostname_is_local(char *);
extern int ot_address_is_local(SockAddr);
extern int ot_addrtype(SockAddr);
extern void ot_addrcopy(SockAddr, char *);
extern void ot_addr_free(SockAddr);
extern Socket ot_register(void *, Plug);
extern Socket ot_new(SockAddr addr, int, int, int, int, Plug);
extern Socket ot_newlistener(char *, int, Plug, int);
extern char *ot_addr_error(SockAddr);

#endif

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */
