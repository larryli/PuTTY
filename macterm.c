/*
 * macterm.c -- Macintosh terminal front-end
 */

#include <MacTypes.h>
#include <Fonts.h>
#include <Gestalt.h>
#include <MacWindows.h>
#include <QuickdrawText.h>
#include <Sound.h>

#include <stdlib.h>

#include "macresid.h"
#include "putty.h"
#include "mac.h"

struct mac_session {
    short fontnum;
    int font_ascent;
    WindowPtr(window);
};

static void mac_initfont(struct mac_session *);

/* Temporary hack till I get the terminal emulator supporting multiple sessions */

static struct mac_session *onlysession;

void mac_newsession(void) {
    struct mac_session *s;

    /* This should obviously be initialised by other means */
    s = smalloc(sizeof(*s));
    strcpy(cfg.font, "Monaco");
    cfg.fontisbold = 0;
    cfg.fontheight = 9;
    onlysession = s;
	
    /* XXX: non-Color-QuickDraw?  Own storage management? */
    if (mac_qdversion == gestaltOriginalQD)
	s->window = GetNewWindow(wTerminal, NULL, (WindowPtr)-1);
    else
	s->window = GetNewCWindow(wTerminal, NULL, (WindowPtr)-1);
    SetWRefCon(s->window, (long)s);
    term_init();
    term_size(24, 80, 100);
    mac_initfont(s);
    ShowWindow(s->window);
}

static void inbuf_putc(int c) {
    inbuf[inbuf_head] = c;
    inbuf_head = (inbuf_head+1) & INBUF_MASK;
}

static void inbuf_putstr(const char *c) {
    while (*c)
	inbuf_putc(*c++);
}

static void mac_initfont(struct mac_session *s) {
    Str255 macfont;
    FontInfo fi;
 
    SetPort(s->window);
    macfont[0] = sprintf((char *)&macfont[1], "%s", cfg.font);
    GetFNum(macfont, &s->fontnum);
    TextFont(s->fontnum);
    TextFace(cfg.fontisbold ? bold : 0);
    TextSize(cfg.fontheight);
    GetFontInfo(&fi);
    font_width = fi.widMax;
    font_height = fi.ascent + fi.descent + fi.leading;
    s->font_ascent = fi.ascent;
    SizeWindow(s->window, cols * font_width, rows * font_height, true);
    inbuf_putstr("Hello,\007 world\007");
    term_out();
}

/*
 * Call from the terminal emulator to draw a bit of text
 *
 * x and y are text row and column (zero-based)
 */
void do_text(struct mac_session *s, int x, int y, char *text, int len,
	     unsigned long attr) {
    int style = 0;

    SetPort(s->window);
    TextFont(s->fontnum);
    if (cfg.fontisbold || (attr & ATTR_BOLD) && !cfg.bold_colour)
    	style |= bold;
    if (attr & ATTR_UNDER)
	style |= underline;
    TextFace(style);
    TextSize(cfg.fontheight);
    TextMode(srcCopy);
    SetFractEnable(FALSE); /* We want characters on pixel boundaries */
    MoveTo(x * font_width, y * font_height + s->font_ascent);
    DrawText(text, 0, len);
}

/*
 * Call from the terminal emulator to get its graphics context.
 * I feel this should disappear entirely (and do_text should take
 * a Session as an argument.  Simon may disagree.
 */
struct mac_session *get_ctx(void) {

    return onlysession;
}

/*
 * Presumably this does something in Windows
 */
void free_ctx(struct mac_session *ctx) {

}

/*
 * Set the scroll bar position
 */
void set_sbar(int total, int start, int page) {

    /* Do something once we actually have a scroll bar */
}

/*
 * Beep
 */
void beep(void) {

    SysBeep(30);
}

/*
 * Set icon string -- a no-op here (WIndowshade?)
 */
void set_icon(char *icon) {

}

/*
 * Set the window title
 */
void set_title(char *title) {
    Str255 mactitle;

    mactitle[0] = sprintf((char *)&mactitle[1], "%s", title);
    SetWTitle(onlysession->window, mactitle);
}

/*
 * Resize the window at the emulator's request
 */
void request_resize(int w, int h) {

    /* XXX: Do something */
}

/*
 * Set the logical palette
 */
void palette_set(int n, int r, int g, int b) {

    /* XXX: Do something */
}

/*
 * Reset to the default palette
 */
void palette_reset(void) {

    /* XXX: Do something */
}
