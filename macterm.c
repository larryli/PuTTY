/*
 * macterm.c -- Macintosh terminal front-end
 */

#include <MacWindows.h>

#include <stdlib.h>

#include "macresid.h"
#include "putty.h"

struct mac_session {
	short fnum;
	int fsize;
}

void mac_newsession(void) {
    WindowPtr window;
    struct mac_session *s;
	
    /* This should obviously be initialised by other means */
    s = smalloc(sizeof(*s));
    s->fnum = GetFNum("\pMonaco");
    s->fsize = 9;
    rows = 24;
    cols = 80;
	
    /* XXX: non-Color-QuickDraw?  Own storage management? */
    window = GetNewCWindow(wTerminal, NULL, (WindowPtr)-1);
    SetPort(window);
    mac_initfont(s);
    term_init();
    term_size(rows, cols);
}

void mac_initfont(struct mac_session *s) {
    FMetricRec metrics;
	
    TextFont(s->fnum);
    TextFace(0);
    TextSize(s->fsize);
    FontMetrics(&metrics);
    font_width = metrics.widMax;
    font_height = metrics.ascent + metrics.descent + metrics.leading;
    SizeWindow(window, cols * font_width, rows * font_height, TRUE);
}
