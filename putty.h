#ifndef PUTTY_PUTTY_H
#define PUTTY_PUTTY_H

#define PUTTY_REG_POS "Software\\SimonTatham\\PuTTY"

/*
 * Global variables. Most modules declare these `extern', but
 * window.c will do `#define PUTTY_DO_GLOBALS' before including this
 * module, and so will get them properly defined.
 */
#ifdef PUTTY_DO_GLOBALS
#define GLOBAL
#else
#define GLOBAL extern
#endif

GLOBAL HINSTANCE putty_inst;

#define ATTR_ACTCURS 0x80000000UL      /* active cursor (block) */
#define ATTR_PASCURS 0x40000000UL      /* passive cursor (box) */
#define ATTR_INVALID 0x20000000UL
#define ATTR_WRAPPED 0x10000000UL

#define LATTR_NORM   0x00000000UL
#define LATTR_WIDE   0x01000000UL
#define LATTR_TOP    0x02000000UL
#define LATTR_BOT    0x03000000UL
#define LATTR_MODE   0x03000000UL

#define ATTR_ASCII   0x00000000UL      /* normal ASCII charset ESC ( B */
#define ATTR_GBCHR   0x00100000UL      /* UK variant   charset ESC ( A */
#define ATTR_LINEDRW 0x00200000UL      /* line drawing charset ESC ( 0 */

#define ATTR_BOLD    0x00000100UL
#define ATTR_UNDER   0x00000200UL
#define ATTR_REVERSE 0x00000400UL
#define ATTR_BLINK   0x00000800UL
#define ATTR_FGMASK  0x0000F000UL
#define ATTR_BGMASK  0x000F0000UL
#define ATTR_FGSHIFT 12
#define ATTR_BGSHIFT 16

#define ATTR_DEFAULT 0x00098000UL
#define ATTR_DEFFG   0x00008000UL
#define ATTR_DEFBG   0x00090000UL
#define ATTR_CUR_XOR 0x000BA000UL
#define ERASE_CHAR   (ATTR_DEFAULT | ' ')
#define ATTR_MASK    0xFFFFFF00UL
#define CHAR_MASK    0x000000FFUL

typedef HDC Context;
#define SEL_NL { 13, 10 }

GLOBAL int rows, cols, savelines;

GLOBAL int font_width, font_height;

#define c_write1(_C) do { if (inbuf_head >= INBUF_SIZE) term_out(); \
			  inbuf[inbuf_head++] = (_C) ; } while(0)
#define INBUF_SIZE 2048
GLOBAL unsigned char inbuf[INBUF_SIZE];
GLOBAL int inbuf_head;

#define OUTBUF_SIZE 2048
#define OUTBUF_MASK (OUTBUF_SIZE-1)
GLOBAL unsigned char outbuf[OUTBUF_SIZE];
GLOBAL int outbuf_head, outbuf_reap;

GLOBAL int has_focus;

GLOBAL int app_cursor_keys, app_keypad_keys, vt52_mode;
GLOBAL int repeat_off, cr_lf_return;

GLOBAL int seen_key_event;
GLOBAL int seen_disp_event;

GLOBAL int session_closed;

GLOBAL char *logfile;

/*
 * I've just looked in the windows standard headr files for WM_USER, there
 * are hundreds of flags defined using the form WM_USER+123 so I've 
 * renumbered this NETEVENT value and the two in window.c
 */
#define WM_XUSER     (WM_USER + 0x2000)
#define WM_NETEVENT  (WM_XUSER + 5)

typedef enum {
    TS_AYT, TS_BRK, TS_SYNCH, TS_EC, TS_EL, TS_GA, TS_NOP, TS_ABORT,
    TS_AO, TS_IP, TS_SUSP, TS_EOR, TS_EOF, TS_LECHO, TS_RECHO
} Telnet_Special;

typedef enum {
    MB_NOTHING, MB_SELECT, MB_EXTEND, MB_PASTE
} Mouse_Button;

typedef enum {
    MA_NOTHING, MA_CLICK, MA_2CLK, MA_3CLK, MA_DRAG, MA_RELEASE
} Mouse_Action;

typedef enum {
    VT_XWINDOWS, VT_OEMANSI, VT_OEMONLY, VT_POORMAN
} VT_Mode;

typedef struct {
    char *(*init) (HWND hwnd, char *host, int port, char **realhost);
    int (*msg) (WPARAM wParam, LPARAM lParam);
    void (*send) (char *buf, int len);
    void (*size) (void);
    void (*special) (Telnet_Special code);
    SOCKET (*socket) (void);
} Backend;

GLOBAL Backend *back;

extern struct backend_list {
    int protocol;
    char *name;
    Backend *backend;
} backends[];

typedef struct {
    void (*send) (char *buf, int len);
} Ldisc;

GLOBAL Ldisc *ldisc;

typedef struct {
    /* Basic options */
    char host[512];
    int port;
    enum { PROT_RAW, PROT_TELNET, PROT_SSH } protocol;
    int close_on_exit;
    int warn_on_close;
    /* SSH options */
    char remote_cmd[512];
    int nopty;
    enum { CIPHER_3DES, CIPHER_BLOWFISH, CIPHER_DES } cipher;
    char keyfile[FILENAME_MAX];
    int try_tis_auth;
    /* Telnet options */
    char termtype[32];
    char termspeed[32];
    char environmt[1024];                    /* VAR\tvalue\0VAR\tvalue\0\0 */
    char username[32];
    int rfc_environ;
    /* Keyboard options */
    int bksp_is_delete;
    int rxvt_homeend;
    int funky_type;
    int app_cursor;
    int app_keypad;
    int nethack_keypad;
    int alt_f4;			       /* is it special? */
    int alt_space;		       /* is it special? */
    int ldisc_term;
    int scroll_on_key;
    /* Terminal options */
    int savelines;
    int dec_om;
    int wrap_mode;
    int lfhascr;
    int blink_cur;
    int beep;
    int scrollbar;
    int locksize;
    int bce;
    int blinktext;
    int win_name_always;
    int width, height;
    char font[64];
    int fontisbold;
    int fontheight;
    int fontcharset;
    /* Colour options */
    int try_palette;
    int bold_colour;
    unsigned char colours[22][3];
    /* Selection options */
    int mouse_is_xterm;
    short wordness[256];
    /* translations */
    VT_Mode vtmode;
    int xlat_enablekoiwin;
    int xlat_88592w1250;
    int xlat_capslockcyr;
} Config;

/*
 * You can compile with -DSSH_DEFAULT to have ssh by default.
 */
#ifndef SSH_DEFAULT
#define DEFAULT_PROTOCOL PROT_TELNET
#define DEFAULT_PORT 23
#else
#define DEFAULT_PROTOCOL PROT_SSH
#define DEFAULT_PORT 22
#endif

/*
 * Some global flags denoting the type of application.
 */
#define FLAG_VERBOSE  0x0001
#define FLAG_WINDOWED 0x0002
#define FLAG_CONNECTION 0x0004
GLOBAL int flags;

GLOBAL Config cfg;
GLOBAL int default_protocol;
GLOBAL int default_port;

struct RSAKey;			       /* be a little careful of scope */

/*
 * Exports from window.c.
 */
void request_resize (int, int, int);
void do_text (Context, int, int, char *, int, unsigned long, int);
void set_title (char *);
void set_icon (char *);
void set_sbar (int, int, int);
Context get_ctx(void);
void free_ctx (Context);
void palette_set (int, int, int, int);
void palette_reset (void);
void write_clip (void *, int);
void get_clip (void **, int *);
void optimised_move (int, int, int);
void fatalbox (char *, ...);
void beep (int);
#define OPTIMISE_IS_SCROLL 1

/*
 * Exports from noise.c.
 */
void noise_get_heavy(void (*func) (void *, int));
void noise_get_light(void (*func) (void *, int));
void noise_ultralight(DWORD data);
void random_save_seed(void);

/*
 * Exports from windlg.c.
 */
int do_config (void);
int do_reconfig (HWND);
void do_defaults (char *);
void logevent (char *);
void showeventlog (HWND);
void showabout (HWND);
void verify_ssh_host_key(char *host, char *keystr);
void get_sesslist(int allocate);

GLOBAL int nsessions;
GLOBAL char **sessions;

/*
 * Exports from terminal.c.
 */

void term_init (void);
void term_size (int, int, int);
void term_out (void);
void term_paint (Context, int, int, int, int);
void term_scroll (int, int);
void term_pwron (void);
void term_clrsb (void);
void term_mouse (Mouse_Button, Mouse_Action, int, int);
void term_deselect (void);
void term_update (void);
void term_invalidate(void);
void term_blink(int set_cursor);
void term_paste(void);
void term_nopaste(void);

/*
 * Exports from raw.c.
 */

extern Backend raw_backend;

/*
 * Exports from telnet.c.
 */

extern Backend telnet_backend;

/*
 * Exports from ssh.c.
 */

extern Backend ssh_backend;

/*
 * Exports from ldisc.c.
 */

extern Ldisc ldisc_term, ldisc_simple;

/*
 * Exports from sshrand.c.
 */

void random_add_noise(void *noise, int length);
void random_init(void);
int random_byte(void);
void random_get_savedata(void **data, int *len);

/*
 * Exports from misc.c.
 */

/* #define MALLOC_LOG  do this if you suspect putty of leaking memory */
#ifdef MALLOC_LOG
#define smalloc(z) (mlog(__FILE__,__LINE__), safemalloc(z))
#define srealloc(y,z) (mlog(__FILE__,__LINE__), saferealloc(y,z))
#define sfree(z) (mlog(__FILE__,__LINE__), safefree(z))
void mlog(char *, int);
#else
#define smalloc safemalloc
#define srealloc saferealloc
#define sfree safefree
#endif

void *safemalloc(size_t);
void *saferealloc(void *, size_t);
void safefree(void *);

/*
 * Exports from version.c.
 */
extern char ver[];

/*
 * Exports from sizetip.c.
 */
void UpdateSizeTip(HWND src, int cx, int cy);
void EnableSizeTip(int bEnable);

/*
 * Exports from xlat.c.
 */
unsigned char xlat_kbd2tty(unsigned char c);
unsigned char xlat_tty2scr(unsigned char c);
unsigned char xlat_latkbd2win(unsigned char c);

/*
 * Exports from mscrypto.c
 */
#ifdef MSCRYPTOAPI
int crypto_startup();
void crypto_wrapup();
#endif

/*
 * A debug system.
 */
#ifdef DEBUG
#include <stdarg.h>
#define debug(x) (dprintf x)
static void dprintf(char *fmt, ...) {
    char buf[2048];
    DWORD dw;
    va_list ap;
    static int gotconsole = 0;

    if (!gotconsole) {
	AllocConsole();
	gotconsole = 1;
    }

    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    WriteFile (GetStdHandle(STD_OUTPUT_HANDLE), buf, strlen(buf), &dw, NULL);
    va_end(ap);
}
#else
#define debug(x)
#endif

#endif
