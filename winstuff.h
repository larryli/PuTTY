/*
 * winstuff.h: Windows-specific inter-module stuff.
 */

#ifndef PUTTY_WINSTUFF_H
#define PUTTY_WINSTUFF_H

/*
 * Global variables. Most modules declare these `extern', but
 * window.c will do `#define PUTTY_DO_GLOBALS' before including this
 * module, and so will get them properly defined.
 */
#ifndef GLOBAL
#ifdef PUTTY_DO_GLOBALS
#define GLOBAL
#else
#define GLOBAL extern
#endif
#endif

#define PUTTY_REG_POS "Software\\SimonTatham\\PuTTY"
#define PUTTY_REG_PARENT "Software\\SimonTatham"
#define PUTTY_REG_PARENT_CHILD "PuTTY"
#define PUTTY_REG_GPARENT "Software"
#define PUTTY_REG_GPARENT_CHILD "SimonTatham"

#define GETTICKCOUNT GetTickCount
#define CURSORBLINK GetCaretBlinkTime()
#define TICKSPERSEC 1000	       /* GetTickCount returns milliseconds */

#define DEFAULT_CODEPAGE CP_ACP

typedef HDC Context;

/*
 * Window handles for the dialog boxes that can be running during a
 * PuTTY session.
 */
GLOBAL HWND logbox;

/*
 * The all-important instance handle.
 */
GLOBAL HINSTANCE hinst;

/*
 * I've just looked in the windows standard headr files for WM_USER, there
 * are hundreds of flags defined using the form WM_USER+123 so I've 
 * renumbered this NETEVENT value and the two in window.c
 */
#define WM_XUSER     (WM_USER + 0x2000)
#define WM_NETEVENT  (WM_XUSER + 5)

/*
 * On Windows, we send MA_2CLK as the only event marking the second
 * press of a mouse button. Compare unix.h.
 */
#define MULTICLICK_ONLY_EVENT 1

/*
 * On Windows, data written to the clipboard must be NUL-terminated.
 */
#define SELECTION_NUL_TERMINATED 1

/*
 * On Windows, copying to the clipboard terminates lines with CRLF.
 */
#define SEL_NL { 13, 10 }

/*
 * Exports from winctrls.c.
 */

struct ctlpos {
    HWND hwnd;
    WPARAM font;
    int dlu4inpix;
    int ypos, width;
    int xoff;
    int boxystart, boxid;
    char *boxtext;
};

/*
 * Exports from winutils.c.
 */
void split_into_argv(char *, int *, char ***, char ***);

/*
 * Private structure for prefslist state. Only in the header file
 * so that we can delegate allocation to callers.
 */
struct prefslist {
    int listid, upbid, dnbid;
    int srcitem;
    int dummyitem;
    int dragging;
};

/*
 * Exports from winctrls.c.
 */
void ctlposinit(struct ctlpos *cp, HWND hwnd,
		int leftborder, int rightborder, int topborder);
HWND doctl(struct ctlpos *cp, RECT r,
	   char *wclass, int wstyle, int exstyle, char *wtext, int wid);
void bartitle(struct ctlpos *cp, char *name, int id);
void beginbox(struct ctlpos *cp, char *name, int idbox);
void endbox(struct ctlpos *cp);
void multiedit(struct ctlpos *cp, ...);
void radioline(struct ctlpos *cp, char *text, int id, int nacross, ...);
void bareradioline(struct ctlpos *cp, int nacross, ...);
void radiobig(struct ctlpos *cp, char *text, int id, ...);
void checkbox(struct ctlpos *cp, char *text, int id);
void statictext(struct ctlpos *cp, char *text, int lines, int id);
void staticbtn(struct ctlpos *cp, char *stext, int sid,
	       char *btext, int bid);
void static2btn(struct ctlpos *cp, char *stext, int sid,
		char *btext1, int bid1, char *btext2, int bid2);
void staticedit(struct ctlpos *cp, char *stext,
		int sid, int eid, int percentedit);
void staticddl(struct ctlpos *cp, char *stext,
	       int sid, int lid, int percentlist);
void combobox(struct ctlpos *cp, char *text, int staticid, int listid);
void staticpassedit(struct ctlpos *cp, char *stext,
		    int sid, int eid, int percentedit);
void bigeditctrl(struct ctlpos *cp, char *stext,
		 int sid, int eid, int lines);
void ersatztab(struct ctlpos *cp, char *stext, int sid, int lid, int s2id);
void editbutton(struct ctlpos *cp, char *stext, int sid,
		int eid, char *btext, int bid);
void sesssaver(struct ctlpos *cp, char *text,
	       int staticid, int editid, int listid, ...);
void envsetter(struct ctlpos *cp, char *stext, int sid,
	       char *e1stext, int e1sid, int e1id,
	       char *e2stext, int e2sid, int e2id,
	       int listid, char *b1text, int b1id, char *b2text, int b2id);
void charclass(struct ctlpos *cp, char *stext, int sid, int listid,
	       char *btext, int bid, int eid, char *s2text, int s2id);
void colouredit(struct ctlpos *cp, char *stext, int sid, int listid,
		char *btext, int bid, ...);
void prefslist(struct prefslist *hdl, struct ctlpos *cp, char *stext,
	       int sid, int listid, int upbid, int dnbid);
int handle_prefslist(struct prefslist *hdl,
		     int *array, int maxmemb,
		     int is_dlmsg, HWND hwnd,
		     WPARAM wParam, LPARAM lParam);
void progressbar(struct ctlpos *cp, int id);
void fwdsetter(struct ctlpos *cp, int listid, char *stext, int sid,
	       char *e1stext, int e1sid, int e1id,
	       char *e2stext, int e2sid, int e2id,
	       char *btext, int bid);

/*
 * Exports from windlg.c.
 */
void defuse_showwindow(void);
int do_config(void);
int do_reconfig(HWND);
void showeventlog(HWND);
void showabout(HWND);
void force_normal(HWND hwnd);

/*
 * Exports from sizetip.c.
 */
void UpdateSizeTip(HWND src, int cx, int cy);
void EnableSizeTip(int bEnable);

/*
 * Unicode and multi-byte character handling stuff.
 */
#define is_dbcs_leadbyte(cp, c) IsDBCSLeadByteEx(cp, c)
#define mb_to_wc(cp, flags, mbstr, mblen, wcstr, wclen) \
	MultiByteToWideChar(cp, flags, mbstr, mblen, wcstr, wclen)
#define wc_to_mb(cp, flags, wcstr, wclen, mbstr, mblen, def, defused) \
	WideCharToMultiByte(cp, flags, mbstr, mblen, wcstr, wclen, def,defused)

#endif
