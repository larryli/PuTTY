/* $Id: maccfg.c,v 1.1.2.1 1999/02/28 02:38:40 ben Exp $ */
/*
 * maccfg.c -- Mac port configuration
 */

#include <Resources.h>
#include <TextUtils.h>

#include <string.h>

#include "putty.h"
#include "mac.h"
#include "macresid.h"

struct strloc {
    short id, idx;
};

static void get_string(struct strloc *l, char *d, size_t maxlen) {
    Str255 s;
    int i, len;

    GetIndString(s, l->id, l->idx);
    len = s[0];
    /* maxlen includes a terminator */
    if (len > maxlen - 1)
	len = maxlen - 1;
    for (i = 0; i < len; i++)
	d[i] = s[i + 1];
    d[i] = '\0';
}

static void get_wordness(short id, short *dst) {
    Handle h;

    h = GetResource(PREF_wordness_type, id);
    if (h == NULL || *h == NULL)
	fatalbox ("Couldn't get wordness (%d)", ResError());
    memcpy(dst, *h, 256 * sizeof(short));
}

struct pSET {
    unsigned long basic_flags;
#define CLOSE_ON_EXIT	0x80000000
    unsigned long ssh_flags;
#define NO_PTY		0x80000000
    unsigned long telnet_flags;
#define RFC_ENVIRON	0x80000000
    unsigned long kbd_flags;
#define BKSP_IS_DELETE	0x80000000
#define RXVT_HOMEEND	0x40000000
#define LINUX_FUNKEYS	0x20000000
#define APP_CURSOR	0x10000000
#define APP_KEYPAD	0x08000000
    unsigned long term_flags;
#define DEC_OM		0x80000000
#define WRAP_MODE	0x40000000
#define LFHASCR		0x20000000
#define WIN_NAME_ALWAYS	0x10000000
    unsigned long colour_flags;
#define BOLD_COLOUR	0x80000000
    struct strloc host;
    long port;
    long protocol;
    struct strloc termtype, termspeed;
    struct strloc environmt;
    struct strloc username;
    long width, height, savelines;
    struct strloc font;
    long font_height;
    short colours_id;
    short wordness_id;
};

/*
 * Load a configuration from the current chain of resource files.
 */
void mac_loadconfig(Config *cfg) {
    Handle h;
    struct pSET *s;

    h = GetResource('pSET', PREF_settings);
    if (h == NULL)
	fatalbox("Can't load settings");
    SetResAttrs(h, GetResAttrs(h) | resLocked);
    s = (struct pSET *)*h;
    /* Basic */
    get_string(&s->host, cfg->host, sizeof(cfg->host));
    cfg->port = s->port;
    cfg->protocol = s->protocol;
    cfg->close_on_exit = (s->basic_flags & CLOSE_ON_EXIT) != 0;
    /* SSH */
    cfg->nopty = (s->ssh_flags & NO_PTY) != 0;
    /* Telnet */
    get_string(&s->termtype, cfg->termtype, sizeof(cfg->termtype));
    get_string(&s->termspeed, cfg->termspeed, sizeof(cfg->termspeed));
    get_string(&s->environmt, cfg->environmt, sizeof(cfg->environmt));
    get_string(&s->username, cfg->username, sizeof(cfg->username));
    cfg->rfc_environ = (s->telnet_flags & RFC_ENVIRON) != 0;
    /* Keyboard */
    cfg->bksp_is_delete = (s->kbd_flags & BKSP_IS_DELETE) != 0;
    cfg->rxvt_homeend = (s->kbd_flags & RXVT_HOMEEND) != 0;
    cfg->linux_funkeys = (s->kbd_flags & LINUX_FUNKEYS) != 0;
    cfg->app_cursor = (s->kbd_flags & APP_CURSOR) != 0;
    cfg->app_keypad = (s->kbd_flags & APP_KEYPAD) != 0;
    /* Terminal */
    cfg->savelines = s->savelines;
    cfg->dec_om = (s->term_flags & DEC_OM) != 0;
    cfg->wrap_mode = (s->term_flags & WRAP_MODE) != 0;
    cfg->lfhascr = (s->term_flags & LFHASCR) != 0;
    cfg->win_name_always = (s->term_flags & WIN_NAME_ALWAYS) != 0;
    cfg->width = s->width;
    cfg->height = s->height;
    get_string(&s->font, cfg->font, sizeof(cfg->font));
    cfg->fontisbold = FALSE;		/* XXX */
    cfg->fontheight = s->font_height;
    cfg->vtmode = VT_POORMAN;		/* XXX */
    /* Colour */
    cfg->try_palette = FALSE;		/* XXX */
    cfg->bold_colour = (s->colour_flags & BOLD_COLOUR) != 0;
    cfg->colours = GetNewPalette(s->colours_id);
    if (cfg->colours == NULL)
	fatalbox("Failed to get default palette");
    /* Selection */
    get_wordness(s->wordness_id, cfg->wordness);
    SetResAttrs(h, GetResAttrs(h) & ~resLocked);
    ReleaseResource(h);
}
