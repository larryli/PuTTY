#ifndef PUTTY_PUTTY_H
#define PUTTY_PUTTY_H

#include "network.h"

#define PUTTY_REG_POS "Software\\SimonTatham\\PuTTY"
#define PUTTY_REG_PARENT "Software\\SimonTatham"
#define PUTTY_REG_PARENT_CHILD "PuTTY"
#define PUTTY_REG_GPARENT "Software"
#define PUTTY_REG_GPARENT_CHILD "SimonTatham"

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

/* Three attribute types: 
 * The ATTRs (normal attributes) are stored with the characters in the main
 * display arrays
 *
 * The TATTRs (temporary attributes) are generated on the fly, they can overlap
 * with characters but not with normal attributes.
 *
 * The LATTRs (line attributes) conflict with no others and only have one
 * value per line. But on area clears the LATTR cells are set to the erase_char
 * (or DEFAULT_ATTR + 'E')
 *
 * ATTR_INVALID is an illegal colour combination.
 */

#define TATTR_ACTCURS 	    0x4UL      /* active cursor (block) */
#define TATTR_PASCURS 	    0x2UL      /* passive cursor (box) */
#define TATTR_RIGHTCURS	    0x1UL      /* cursor-on-RHS */

#define LATTR_NORM   0x00000000UL
#define LATTR_WIDE   0x01000000UL
#define LATTR_TOP    0x02000000UL
#define LATTR_BOT    0x03000000UL
#define LATTR_MODE   0x03000000UL
#define LATTR_WRAPPED 0x10000000UL

#define ATTR_INVALID 0x00FF0000UL

/* Like Linux use the F000 page for direct to font. */
#define ATTR_OEMCP   0x0000F000UL      /* OEM Codepage DTF */
#define ATTR_ACP     0x0000F100UL      /* Ansi Codepage DTF */

/* These are internal use overlapping with the UTF-16 surrogates */
#define ATTR_ASCII   0x0000D800UL      /* normal ASCII charset ESC ( B */
#define ATTR_LINEDRW 0x0000D900UL      /* line drawing charset ESC ( 0 */
#define ATTR_SCOACS  0x0000DA00UL      /* SCO Alternate charset */
#define ATTR_GBCHR   0x0000DB00UL      /* UK variant   charset ESC ( A */
#define CSET_MASK    0x0000FF00UL      /* Character set mask; MUST be 0xFF00 */

#define DIRECT_CHAR(c) ((c&0xFC00)==0xD800)
#define DIRECT_FONT(c) ((c&0xFE00)==0xF000)

#define UCSERR	     (ATTR_LINEDRW|'a')	/* UCS Format error character. */
#define UCSWIDE	     0x303F

#define ATTR_WIDE    0x10000000UL
#define ATTR_BOLD    0x01000000UL
#define ATTR_UNDER   0x02000000UL
#define ATTR_REVERSE 0x04000000UL
#define ATTR_BLINK   0x08000000UL
#define ATTR_FGMASK  0x000F0000UL
#define ATTR_BGMASK  0x00F00000UL
#define ATTR_COLOURS 0x00FF0000UL
#define ATTR_FGSHIFT 16
#define ATTR_BGSHIFT 20

#define ATTR_DEFAULT 0x00980000UL
#define ATTR_DEFFG   0x00080000UL
#define ATTR_DEFBG   0x00900000UL
#define ERASE_CHAR   (ATTR_DEFAULT | ATTR_ASCII | ' ')
#define ATTR_MASK    0xFFFFFF00UL
#define CHAR_MASK    0x000000FFUL

#define ATTR_CUR_AND (~(ATTR_BOLD|ATTR_REVERSE|ATTR_BLINK|ATTR_COLOURS))
#define ATTR_CUR_XOR 0x00BA0000UL

typedef HDC Context;
#define SEL_NL { 13, 10 }

GLOBAL int rows, cols, savelines;

GLOBAL int font_width, font_height;

#define INBUF_SIZE 2048
GLOBAL unsigned char inbuf[INBUF_SIZE];
GLOBAL int inbuf_head;

#define OUTBUF_SIZE 2048
#define OUTBUF_MASK (OUTBUF_SIZE-1)
GLOBAL unsigned char outbuf[OUTBUF_SIZE];
GLOBAL int outbuf_head, outbuf_reap;

GLOBAL int has_focus;

GLOBAL int in_vbell;
GLOBAL long vbell_timeout;

GLOBAL int app_cursor_keys, app_keypad_keys, vt52_mode;
GLOBAL int repeat_off, cr_lf_return;

GLOBAL int seen_key_event;
GLOBAL int seen_disp_event;

GLOBAL int session_closed;

GLOBAL int big_cursor;

GLOBAL int utf;
GLOBAL int dbcs_screenfont;
GLOBAL int font_codepage;
GLOBAL int kbd_codepage;
GLOBAL int line_codepage;
GLOBAL WCHAR unitab_scoacs[256];
GLOBAL WCHAR unitab_line[256];
GLOBAL WCHAR unitab_font[256];
GLOBAL WCHAR unitab_xterm[256];
GLOBAL WCHAR unitab_oemcp[256];
GLOBAL unsigned char unitab_ctrl[256];
#define in_utf (utf || line_codepage==CP_UTF8)

#define LGXF_OVR  1		       /* existing logfile overwrite */
#define LGXF_APN  0		       /* existing logfile append */
#define LGXF_ASK -1		       /* existing logfile ask */
#define LGTYP_NONE  0		       /* logmode: no logging */
#define LGTYP_ASCII 1		       /* logmode: pure ascii */
#define LGTYP_DEBUG 2		       /* logmode: all chars of taffic */
GLOBAL char *logfile;

/*
 * Window handles for the dialog boxes that can be running during a
 * PuTTY session.
 */
GLOBAL HWND logbox;

/*
 * I've just looked in the windows standard headr files for WM_USER, there
 * are hundreds of flags defined using the form WM_USER+123 so I've 
 * renumbered this NETEVENT value and the two in window.c
 */
#define WM_XUSER     (WM_USER + 0x2000)
#define WM_NETEVENT  (WM_XUSER + 5)

typedef enum {
    TS_AYT, TS_BRK, TS_SYNCH, TS_EC, TS_EL, TS_GA, TS_NOP, TS_ABORT,
    TS_AO, TS_IP, TS_SUSP, TS_EOR, TS_EOF, TS_LECHO, TS_RECHO, TS_PING,
    TS_EOL
} Telnet_Special;

typedef enum {
    MBT_NOTHING,
    MBT_LEFT, MBT_MIDDLE, MBT_RIGHT,   /* `raw' button designations */
    MBT_SELECT, MBT_EXTEND, MBT_PASTE, /* `cooked' button designations */
    MBT_WHEEL_UP, MBT_WHEEL_DOWN       /* mouse wheel */
} Mouse_Button;

typedef enum {
    MA_NOTHING, MA_CLICK, MA_2CLK, MA_3CLK, MA_DRAG, MA_RELEASE
} Mouse_Action;

typedef enum {
    VT_XWINDOWS, VT_OEMANSI, VT_OEMONLY, VT_POORMAN, VT_UNICODE
} VT_Mode;

enum {
    /*
     * Line discipline option states: off, on, up to the backend.
     */
    LD_YES, LD_NO, LD_BACKEND
};

enum {
    /*
     * Line discipline options which the backend might try to control.
     */
    LD_EDIT,			       /* local line editing */
    LD_ECHO			       /* local echo */
};

enum {
    /*
     * Close On Exit behaviours. (cfg.close_on_exit)
     */
    COE_NEVER,			       /* Never close the window */
    COE_NORMAL,			       /* Close window on "normal" (non-error) exits only */
    COE_ALWAYS			       /* Always close the window */
};

typedef struct {
    char *(*init) (char *host, int port, char **realhost);
    void (*send) (char *buf, int len);
    void (*size) (void);
    void (*special) (Telnet_Special code);
     Socket(*socket) (void);
    int (*sendok) (void);
    int (*ldisc) (int);
    int default_port;
} Backend;

GLOBAL Backend *back;

extern struct backend_list {
    int protocol;
    char *name;
    Backend *backend;
} backends[];

typedef struct {
    /* Basic options */
    char host[512];
    int port;
    enum { PROT_RAW, PROT_TELNET, PROT_RLOGIN, PROT_SSH } protocol;
    int close_on_exit;
    int warn_on_close;
    int ping_interval;		       /* in seconds */
    /* SSH options */
    char remote_cmd[512];
    char *remote_cmd_ptr;	       /* might point to a larger command
				        * but never for loading/saving */
    int nopty;
    int compression;
    int agentfwd;
    enum { CIPHER_3DES, CIPHER_BLOWFISH, CIPHER_DES, CIPHER_AES } cipher;
    char keyfile[FILENAME_MAX];
    int sshprot;		       /* use v1 or v2 when both available */
    int buggymac;		       /* MAC bug commmercial <=v2.3.x SSH2 */
    int try_tis_auth;
    int ssh_subsys;		       /* run a subsystem rather than a command */
    /* Telnet options */
    char termtype[32];
    char termspeed[32];
    char environmt[1024];	       /* VAR\tvalue\0VAR\tvalue\0\0 */
    char username[32];
    char localusername[32];
    int rfc_environ;
    int passive_telnet;
    /* Keyboard options */
    int bksp_is_delete;
    int rxvt_homeend;
    int funky_type;
    int no_applic_c;		       /* totally disable app cursor keys */
    int no_applic_k;		       /* totally disable app keypad */
    int app_cursor;
    int app_keypad;
    int nethack_keypad;
    int telnet_keyboard;
    int alt_f4;			       /* is it special? */
    int alt_space;		       /* is it special? */
    int alt_only;		       /* is it special? */
    int localecho;
    int localedit;
    int alwaysontop;
    int scroll_on_key;
    int scroll_on_disp;
    int compose_key;
    int ctrlaltkeys;
    char wintitle[256];		       /* initial window title */
    /* Terminal options */
    int savelines;
    int dec_om;
    int wrap_mode;
    int lfhascr;
    int cursor_type;		       /* 0=block 1=underline 2=vertical */
    int blink_cur;
    enum {
	BELL_DISABLED, BELL_DEFAULT, BELL_VISUAL, BELL_WAVEFILE
    } beep;
    enum {
	B_IND_DISABLED, B_IND_FLASH, B_IND_STEADY
    } beep_ind;
    int bellovl;		       /* bell overload protection active? */
    int bellovl_n;		       /* number of bells to cause overload */
    int bellovl_t;		       /* time interval for overload (seconds) */
    int bellovl_s;		       /* period of silence to re-enable bell (s) */
    char bell_wavefile[FILENAME_MAX];
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
    char logfilename[FILENAME_MAX];
    int logtype;
    int logxfovr;
    int hide_mouseptr;
    int sunken_edge;
    char answerback[256];
    /* Colour options */
    int try_palette;
    int bold_colour;
    unsigned char colours[22][3];
    /* Selection options */
    int mouse_is_xterm;
    int rawcnp;
    short wordness[256];
    /* translations */
    VT_Mode vtmode;
    char line_codepage[32];
    /* X11 forwarding */
    int x11_forward;
    char x11_display[128];
    /* port forwarding */
    int lport_acceptall; /* accepts connection from hosts other than localhost */
    char portfwd[1024]; /* [LR]localport\thost:port\000[LR]localport\thost:port\000\000 */
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
 * 
 * FLAG_VERBOSE is set when the user requests verbose details.
 * 
 * FLAG_STDERR is set in command-line applications (which have a
 * functioning stderr that it makes sense to write to) and not in
 * GUI applications (which don't).
 * 
 * FLAG_INTERACTIVE is set when a full interactive shell session is
 * being run, _either_ because no remote command has been provided
 * _or_ because the application is GUI and can't run non-
 * interactively.
 */
#define FLAG_VERBOSE     0x0001
#define FLAG_STDERR      0x0002
#define FLAG_INTERACTIVE 0x0004
GLOBAL int flags;

GLOBAL Config cfg;
GLOBAL int default_protocol;
GLOBAL int default_port;

struct RSAKey;			       /* be a little careful of scope */

/*
 * Exports from window.c.
 */
void request_resize(int, int, int);
void do_text(Context, int, int, char *, int, unsigned long, int);
void do_cursor(Context, int, int, char *, int, unsigned long, int);
void set_title(char *);
void set_icon(char *);
void set_sbar(int, int, int);
Context get_ctx(void);
void free_ctx(Context);
void palette_set(int, int, int, int);
void palette_reset(void);
void write_aclip(char *, int, int);
void write_clip(wchar_t *, int, int);
void get_clip(wchar_t **, int *);
void optimised_move(int, int, int);
void set_raw_mouse_mode(int);
Mouse_Button translate_button(Mouse_Button b);
void connection_fatal(char *, ...);
void fatalbox(char *, ...);
void beep(int);
void begin_session(void);
void sys_cursor(int x, int y);
#define OPTIMISE_IS_SCROLL 1

/*
 * Exports from noise.c.
 */
void noise_get_heavy(void (*func) (void *, int));
void noise_get_light(void (*func) (void *, int));
void noise_regular(void);
void noise_ultralight(DWORD data);
void random_save_seed(void);
void random_destroy_seed(void);

/*
 * Exports from windlg.c.
 */
void defuse_showwindow(void);
int do_config(void);
int do_reconfig(HWND);
void do_defaults(char *, Config *);
void logevent(char *);
void showeventlog(HWND);
void showabout(HWND);
void verify_ssh_host_key(char *host, int port, char *keytype,
			 char *keystr, char *fingerprint);
int askappend(char *filename);
void registry_cleanup(void);
void force_normal(HWND hwnd);

GLOBAL int nsessions;
GLOBAL char **sessions;

/*
 * Exports from settings.c.
 */
void save_settings(char *section, int do_host, Config * cfg);
void load_settings(char *section, int do_host, Config * cfg);
void get_sesslist(int allocate);

/*
 * Exports from terminal.c.
 */

void term_init(void);
void term_size(int, int, int);
void term_out(void);
void term_paint(Context, int, int, int, int);
void term_scroll(int, int);
void term_pwron(void);
void term_clrsb(void);
void term_mouse(Mouse_Button, Mouse_Action, int, int, int, int);
void term_deselect(void);
void term_update(void);
void term_invalidate(void);
void term_blink(int set_cursor);
void term_do_paste(void);
void term_paste(void);
void term_nopaste(void);
int term_ldisc(int option);
void from_backend(int is_stderr, char *data, int len);
void logfopen(void);
void logfclose(void);
void term_copyall(void);

/*
 * Exports from raw.c.
 */

extern Backend raw_backend;

/*
 * Exports from rlogin.c.
 */

extern Backend rlogin_backend;

/*
 * Exports from telnet.c.
 */

extern Backend telnet_backend;

/*
 * Exports from ssh.c.
 */

extern int (*ssh_get_line) (const char *prompt, char *str, int maxlen,
			    int is_pw);
extern Backend ssh_backend;

/*
 * Exports from ldisc.c.
 */

extern void ldisc_send(char *buf, int len);

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

#include "misc.h"

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
 * Exports from unicode.c.
 */
#ifndef CP_UTF8
#define CP_UTF8 65001
#endif
void init_ucs_tables(void);
void lpage_send(int codepage, char *buf, int len);
void luni_send(wchar_t * widebuf, int len);
int check_compose(int first, int second);
int decode_codepage(char *cp_name);
char *cp_enumerate (int index);
char *cp_name(int codepage);

/*
 * Exports from mscrypto.c
 */
#ifdef MSCRYPTOAPI
int crypto_startup();
void crypto_wrapup();
#endif

/*
 * Exports from pageantc.c
 */
void agent_query(void *in, int inlen, void **out, int *outlen);
int agent_exists(void);


#endif
