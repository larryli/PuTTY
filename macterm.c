/*
 * macterm.c -- Macintosh terminal front-end
 */

#include <MacTypes.h>
#include <Fonts.h>
#include <Gestalt.h>
#include <MacWindows.h>
#include <Quickdraw.h>
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
    RGBColor defpal[24];
    RGBColor palette[24];
};

static void mac_initfont(struct mac_session *);
static void mac_initpalette(struct mac_session *);

/* Temporary hack till I get the terminal emulator supporting multiple sessions */

static struct mac_session *onlysession;

void mac_newsession(void) {
    struct mac_session *s;
    int i;

    /* This should obviously be initialised by other means */
    s = smalloc(sizeof(*s));
    cfg.bksp_is_delete = TRUE;
    cfg.rxvt_homeend = FALSE;
    cfg.linux_funkeys = FALSE;
    cfg.app_cursor = FALSE;
    cfg.app_keypad = FALSE;
    cfg.savelines = 100;
    cfg.dec_om = FALSE;
    cfg.wrap_mode = 
    cfg.lfhascr = FALSE;
    cfg.win_name_always = FALSE;
    cfg.width = 80;
    cfg.height = 24;
    strcpy(cfg.font, "Monaco");
    cfg.fontisbold = 0;
    cfg.fontheight = 9;
    cfg.vtmode = VT_POORMAN;
    cfg.try_palette = FALSE;
    cfg.bold_colour = TRUE;
    for (i = 0; i < 22; i++) {
        static char defaults[22][3] = {
            {187, 187, 187}, {255, 255, 255},
            {0, 0, 0}, {85, 85, 85},
            {0, 0, 0}, {0, 255, 0},
            {0, 0, 0}, {85, 85, 85},
            {187, 0, 0}, {255, 85, 85},
            {0, 187, 0}, {85, 255, 85},
            {187, 187, 0}, {255, 255, 85},
            {0, 0, 187}, {85, 85, 255},
            {187, 0, 187}, {255, 85, 255},
            {0, 187, 187}, {85, 255, 255},
            {187, 187, 187}, {255, 255, 255}
         };
         cfg.colours[i][0] = defaults[i][0];
         cfg.colours[i][1] = defaults[i][1];
         cfg.colours[i][2] = defaults[i][2];
    }
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
    mac_initpalette(s);
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
    inbuf_putstr("\033[1mBold\033[m    \033[2mfaint\033[m   \033[3mitalic\033[m  \033[4mu_line\033[m  "
                 "\033[5mslow bl\033[m \033[6mfast bl\033[m \033[7minverse\033[m \033[8mconceal\033[m "
                 "\033[9mstruck\033[m  \033[21mdbl ul\033[m\015\012");
    term_out();
    inbuf_putstr("\033[30mblack   \033[31mred     \033[32mgreen   \033[33myellow  "
                 "\033[34mblue    \033[35mmagenta \033[36mcyan    \033[37mwhite\015\012");
    term_out();
    inbuf_putstr("\033[1m\033[30mblack   \033[31mred     \033[32mgreen   \033[33myellow  "
                 "\033[1m\033[34mblue    \033[35mmagenta \033[36mcyan    \033[37mwhite\015\012");
    term_out();
    inbuf_putstr("\033[37;44mwhite on blue     \033[32;41mgreen on red\015\012");
    term_out();
}


/*
 * Set up the default palette, then call palette_reset to transfer
 * it to the working palette (should the emulator do this at
 * startup?
 */
static void mac_initpalette(struct mac_session *s) {
    int i;
    static const int ww[] = {
	6, 7, 8, 9, 10, 11, 12, 13,
	14, 15, 16, 17, 18, 19, 20, 21,
	0, 1, 2, 3, 4, 4, 5, 5
    };

    for (i=0; i<24; i++) {
	int w = ww[i];
	s->defpal[i].red   = cfg.colours[w][0] * 0x0101;
	s->defpal[i].green = cfg.colours[w][1] * 0x0101;
	s->defpal[i].blue  = cfg.colours[w][2] * 0x0101;
    }
    palette_reset();
}


/*
 * Call from the terminal emulator to draw a bit of text
 *
 * x and y are text row and column (zero-based)
 */
void do_text(struct mac_session *s, int x, int y, char *text, int len,
	     unsigned long attr) {
    int style = 0;
    int bgcolour, fgcolour;
    RGBColor rgbfore, rgbback;
    RgnHandle textregion, intersection;
    Rect textrect;

    SetPort(s->window);
#if 0
    /* First check this text is relevant */
    textregion = NewRgn();
    SetRectRgn(textregion, x * font_width, (x + len) * font_width,
	       y * font_height, (y + 1) * font_height);
    SectRgn(textregion, s->window->visRgn, textregion);
    if (EmptyRgn(textregion)) {
	DisposeRgn(textregion);
	return;
    }
#else
    /* alternatively */
    textrect.top = y * font_height;
    textrect.bottom = (y + 1) * font_height;
    textrect.left = x * font_width;
    textrect.right = (x + len) * font_width;
    if (!RectInRgn(&textrect, s->window->visRgn))
	return;
#endif

    TextFont(s->fontnum);
    if (cfg.fontisbold || (attr & ATTR_BOLD) && !cfg.bold_colour)
    	style |= bold;
    if (attr & ATTR_UNDER)
	style |= underline;
    TextFace(style);
    TextSize(cfg.fontheight);
    TextMode(srcCopy);
    if (attr & ATTR_REVERSE) {
	bgcolour = ((attr & ATTR_FGMASK) >> ATTR_FGSHIFT) * 2;
	fgcolour = ((attr & ATTR_BGMASK) >> ATTR_BGSHIFT) * 2;
    } else {
	fgcolour = ((attr & ATTR_FGMASK) >> ATTR_FGSHIFT) * 2;
	bgcolour = ((attr & ATTR_BGMASK) >> ATTR_BGSHIFT) * 2;
    }
    if ((attr & ATTR_BOLD) && cfg.bold_colour)
    	fgcolour++;
    RGBForeColor(&s->palette[fgcolour]);
    RGBBackColor(&s->palette[bgcolour]);
    SetFractEnable(FALSE); /* We want characters on pixel boundaries */
    MoveTo(x * font_width, y * font_height + s->font_ascent);
    DrawText(text, 0, len);
}

/*
 * Call from the terminal emulator to get its graphics context.
 * I feel this should disappear entirely (and do_text should take
 * a Session as an argument).  Simon may disagree.
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
    int i;
    struct mac_session *s = onlysession;

    for (i = 0; i < 24; i++) {
	s->palette[i].red   = s->defpal[i].red;
	s->palette[i].green = s->defpal[i].green;
	s->palette[i].blue  = s->defpal[i].blue;
    }
    term_invalidate();
}
