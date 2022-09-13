/*
 * Main state structure for an instance of the Windows PuTTY front
 * end, containing all the PuTTY objects and all the Windows API
 * resources like the window handle.
 */

typedef struct WinGuiSeat WinGuiSeat;

struct PopupMenu {
    HMENU menu;
};
enum { SYSMENU, CTXMENU };             /* indices into popup_menus field */

#define FONT_NORMAL 0
#define FONT_BOLD 1
#define FONT_UNDERLINE 2
#define FONT_BOLDUND 3
#define FONT_WIDE       0x04
#define FONT_HIGH       0x08
#define FONT_NARROW     0x10

#define FONT_OEM        0x20
#define FONT_OEMBOLD    0x21
#define FONT_OEMUND     0x22
#define FONT_OEMBOLDUND 0x23

#define FONT_MAXNO      0x40
#define FONT_SHIFT      5

enum BoldMode {
    BOLD_NONE, BOLD_SHADOW, BOLD_FONT
};
enum UnderlineMode {
    UND_LINE, UND_FONT
};

struct _dpi_info {
    POINT cur_dpi;
    RECT new_wnd_rect;
};

/*
 * Against the future possibility of having more than one of these
 * in a process (and the current possibility of having zero), we
 * keep a linked list of all live WinGuiSeats, so that cleanups
 * can be done to any that exist.
 */
struct WinGuiSeatListNode {
    struct WinGuiSeatListNode *next, *prev;
};
extern struct WinGuiSeatListNode wgslisthead; /* static end pointer */

struct WinGuiSeat {
    struct WinGuiSeatListNode wgslistnode;

    Seat seat;
    TermWin termwin;
    LogPolicy logpolicy;

    HWND term_hwnd;

    int caret_x, caret_y;

    int kbd_codepage;

    Ldisc *ldisc;
    Backend *backend;

    cmdline_get_passwd_input_state cmdline_get_passwd_state;

    struct unicode_data ucsdata;
    bool session_closed;
    bool reconfiguring;

    const SessionSpecial *specials;
    HMENU specials_menu;
    int n_specials;

    struct PopupMenu popup_menus[2];
    HMENU savedsess_menu;

    Conf *conf;
    LogContext *logctx;
    Terminal *term;

    int cursor_type;
    int vtmode;

    HFONT fonts[FONT_MAXNO];
    LOGFONT lfont;
    bool fontflag[FONT_MAXNO];
    enum BoldMode bold_font_mode;

    bool bold_colours;
    enum UnderlineMode und_mode;
    int descent, font_strikethrough_y;

    COLORREF colours[OSC4_NCOLOURS];
    HPALETTE pal;
    LPLOGPALETTE logpal;
    bool tried_pal;
    COLORREF colorref_modifier;

    struct _dpi_info dpi_info;

    int dbltime, lasttime, lastact;
    Mouse_Button lastbtn;

    bool send_raw_mouse;
    int wheel_accumulator;

    bool pointer_indicates_raw_mouse;

    BusyStatus busy_status;

    wchar_t *window_name, *icon_name;

    int compose_state;

    HDC wintw_hdc;

    bool resizing;

    long next_flash;
    bool flashing;
};

extern const LogPolicyVtable win_gui_logpolicy_vt; /* in dialog.c */

static inline void wgs_link(WinGuiSeat *wgs)
{
    wgs->wgslistnode.prev = wgslisthead.prev;
    wgs->wgslistnode.next = &wgslisthead;
    wgs->wgslistnode.prev->next = &wgs->wgslistnode;
    wgs->wgslistnode.next->prev = &wgs->wgslistnode;
}

static inline void wgs_unlink(WinGuiSeat *wgs)
{
    wgs->wgslistnode.prev->next = wgs->wgslistnode.next;
    wgs->wgslistnode.next->prev = wgs->wgslistnode.prev;
}
