/*
 * mac.h -- macintosh-specific declarations
 */

#ifndef PUTTY_MAC_H
#define PUTTY_MAC_H

#include <MacTypes.h>
#include <Controls.h>
#include <Events.h>
#include <Gestalt.h>
#include <Lists.h>
#include <MacWindows.h>
#include <Palettes.h>
#include <UnicodeConverter.h>

#include "charset.h"
#include "tree234.h"

#define PUTTY_CREATOR	FOUR_CHAR_CODE('pTTY')
#define INTERNAL_CREATOR FOUR_CHAR_CODE('pTTI')
#define SESS_TYPE	FOUR_CHAR_CODE('Sess')
#define SEED_TYPE	FOUR_CHAR_CODE('Seed')

struct mac_gestalts {
    long sysvers;
    long qdvers;
    long apprvers;
    long cntlattr;
    long windattr;
    long menuattr;
    long encvvers; /* TEC version (from TECGetInfo()) */
    long uncvattr; /* Unicode Converter attributes (frem TECGetInfo()) */
    long navsvers; /* Navigation Services version */
};

extern struct mac_gestalts mac_gestalts;
extern UInt32 sleeptime;

#if TARGET_RT_MAC_CFM
/* All systems that can use CFM have Color QuickDraw */
#define HAVE_COLOR_QD() 1
#else
#define HAVE_COLOR_QD() (mac_gestalts.qdvers > gestaltOriginalQD)
#endif

/* Every window used by PuTTY has a refCon field pointing to one of these. */
typedef struct {
    struct Session *s;    /* Only used in PuTTY */
    struct KeyState *ks; /* Only used in PuTTYgen */
    struct macctrls *mcs;

    void (*activate)	(WindowPtr, EventRecord *);
    void (*adjustcursor)(WindowPtr, Point, RgnHandle);
    void (*adjustmenus)	(WindowPtr);
    void (*update)	(WindowPtr);
    void (*click)	(WindowPtr, EventRecord *);
    void (*grow)	(WindowPtr, EventRecord *);
    void (*key)		(WindowPtr, EventRecord *);
    void (*menu)	(WindowPtr, short, short);
    void (*close)	(WindowPtr);

    int wtype;
} WinInfo;

#define mac_wininfo(w)		((WinInfo *)GetWRefCon(w))
#define mac_windowsession(w)	(((WinInfo *)GetWRefCon(w))->s)
#define mac_winctrls(w)		(((WinInfo *)GetWRefCon(w))->mcs)

union macctrl;

struct macctrls {
    tree234		*byctrl;
    void		*data; /* private data for config box */
    unsigned int	npanels;
    unsigned int	curpanel;
    union macctrl	**panels; /* lists of controls by panel */
};    

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
    /* Unicode stuff */
    struct unicode_data ucsdata;

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
    WindowPtr		eventlog_window;
    ListHandle		eventlog;
    PaletteHandle	palette;
    ControlHandle	scrollbar;
    WCTabHandle		wctab;
    int			raw_mouse;
    UnicodeToTextInfo	uni_to_font;  /* Only one of uni_to_font and	 */
    charset_t		font_charset; /* font_charset is used at a time. */
    int			hasfile;
    FSSpec		savefile;

    /* Config dialogue bits */
    WindowPtr		settings_window;
    struct controlbox	*ctrlbox;
    struct macctrls	settings_ctrls;
} Session;

extern Session *sesslist;

/* PuTTYgen per-window state */
typedef struct KeyState {
    DialogPtr		box;
    int collecting_entropy;
    int entropy_got, entropy_required, entropy_size;
    unsigned *entropy;
    ControlHandle	progress;
} KeyState;

#define mac_windowkey(w)	(((WinInfo *)GetWRefCon(w))->ks)

/* from macmisc.c */
extern WindowPtr mac_frontwindow(void);
/* from macdlg.c */
extern void mac_newsession(void);
extern void mac_dupsession(void);
extern void mac_savesession(void);
extern void mac_savesessionas(void);
/* from maceventlog.c */
extern void mac_freeeventlog(Session *);
extern void mac_showeventlog(Session *);
/* from macterm.c */
extern void mac_opensession(void);
extern void mac_startsession(Session *);
extern void mac_pollterm(void);
/* from macstore.c */
extern OSErr get_putty_dir(Boolean makeit, short *pVRefNum, long *pDirID);
extern OSErr get_session_dir(Boolean makeit, short *pVRefNum, long *pDirID);
extern void *open_settings_r_fsp(FSSpec *);
extern void *open_settings_w_fsp(FSSpec *);
/* from macucs.c */
extern void init_ucs(Session *);
/* from macnet.c */
extern void sk_poll(void);
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
/* from macabout.c */
extern void mac_openabout(void);
/* from macctrls.c */
extern void macctrl_layoutbox(struct controlbox *, WindowPtr,
			      struct macctrls *);
extern void macctrl_activate(WindowPtr, EventRecord *);
extern void macctrl_click(WindowPtr, EventRecord *);
extern void macctrl_update(WindowPtr);
extern void macctrl_adjustmenus(WindowPtr);
extern void macctrl_close(WindowPtr);


/* from macpgkey.c */
extern void mac_newkey(void);
/* Apple Event Handlers (in various files) */
extern pascal OSErr mac_aevt_oapp(const AppleEvent *, AppleEvent *, long);
extern pascal OSErr mac_aevt_odoc(const AppleEvent *, AppleEvent *, long);
extern pascal OSErr mac_aevt_pdoc(const AppleEvent *, AppleEvent *, long);
extern pascal OSErr mac_aevt_quit(const AppleEvent *, AppleEvent *, long);

#endif

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */
