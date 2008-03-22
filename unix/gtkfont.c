/*
 * Unified font management for GTK.
 * 
 * PuTTY is willing to use both old-style X server-side bitmap
 * fonts _and_ GTK2/Pango client-side fonts. This requires us to
 * do a bit of work to wrap the two wildly different APIs into
 * forms the rest of the code can switch between seamlessly, and
 * also requires a custom font selector capable of handling both
 * types of font.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include "putty.h"
#include "gtkfont.h"

/*
 * To do:
 * 
 *  - import flags to do VT100 double-width; import the icky
 *    pixmap stretch code on to the X11 side, and do something
 *    nicer in Pango.
 * 
 *  - unified font selector dialog, arrgh!
 */

/*
 * Future work:
 * 
 *  - all the GDK font functions used in the x11font subclass are
 *    deprecated, so one day they may go away. When this happens -
 *    or before, if I'm feeling proactive - it oughtn't to be too
 *    difficult in principle to convert the whole thing to use
 *    actual Xlib font calls.
 */

/*
 * Ad-hoc vtable mechanism to allow font structures to be
 * polymorphic.
 * 
 * Any instance of `unifont' used in the vtable functions will
 * actually be the first element of a larger structure containing
 * data specific to the subtype. This is permitted by the ISO C
 * provision that one may safely cast between a pointer to a
 * structure and a pointer to its first element.
 */

struct unifont_vtable {
    /*
     * `Methods' of the `class'.
     */
    unifont *(*create)(GtkWidget *widget, char *name, int wide, int bold,
		       int shadowoffset, int shadowalways);
    void (*destroy)(unifont *font);
    void (*draw_text)(GdkDrawable *target, GdkGC *gc, unifont *font,
		      int x, int y, const char *string, int len, int wide,
		      int bold, int cellwidth);
    /*
     * `Static data members' of the `class'.
     */
    const char *prefix;
};

/* ----------------------------------------------------------------------
 * GDK-based X11 font implementation.
 */

static void x11font_draw_text(GdkDrawable *target, GdkGC *gc, unifont *font,
			      int x, int y, const char *string, int len,
			      int wide, int bold, int cellwidth);
static unifont *x11font_create(GtkWidget *widget, char *name,
			       int wide, int bold,
			       int shadowoffset, int shadowalways);
static void x11font_destroy(unifont *font);

struct x11font {
    struct unifont u;
    /*
     * Actual font objects. We store a number of these, for
     * automatically guessed bold and wide variants.
     * 
     * The parallel array `allocated' indicates whether we've
     * tried to fetch a subfont already (thus distinguishing NULL
     * because we haven't tried yet from NULL because we tried and
     * failed, so that we don't keep trying and failing
     * subsequently).
     */
    GdkFont *fonts[4];
    int allocated[4];
    /*
     * `sixteen_bit' is true iff the font object is indexed by
     * values larger than a byte. That is, this flag tells us
     * whether we use gdk_draw_text_wc() or gdk_draw_text().
     */
    int sixteen_bit;
    /*
     * Data passed in to unifont_create().
     */
    int wide, bold, shadowoffset, shadowalways;
};

static const struct unifont_vtable x11font_vtable = {
    x11font_create,
    x11font_destroy,
    x11font_draw_text,
    "x11"
};

char *x11_guess_derived_font_name(GdkFont *font, int bold, int wide)
{
    XFontStruct *xfs = GDK_FONT_XFONT(font);
    Display *disp = GDK_FONT_XDISPLAY(font);
    Atom fontprop = XInternAtom(disp, "FONT", False);
    unsigned long ret;
    if (XGetFontProperty(xfs, fontprop, &ret)) {
	char *name = XGetAtomName(disp, (Atom)ret);
	if (name && name[0] == '-') {
	    char *strings[13];
	    char *dupname, *extrafree = NULL, *ret;
	    char *p, *q;
	    int nstr;

	    p = q = dupname = dupstr(name); /* skip initial minus */
	    nstr = 0;

	    while (*p && nstr < lenof(strings)) {
		if (*p == '-') {
		    *p = '\0';
		    strings[nstr++] = p+1;
		}
		p++;
	    }

	    if (nstr < lenof(strings))
		return NULL;	       /* XLFD was malformed */

	    if (bold)
		strings[2] = "bold";

	    if (wide) {
		/* 4 is `wideness', which obviously may have changed. */
		/* 5 is additional style, which may be e.g. `ja' or `ko'. */
		strings[4] = strings[5] = "*";
		strings[11] = extrafree = dupprintf("%d", 2*atoi(strings[11]));
	    }

	    ret = dupcat("-", strings[ 0], "-", strings[ 1], "-", strings[ 2],
			 "-", strings[ 3], "-", strings[ 4], "-", strings[ 5],
			 "-", strings[ 6], "-", strings[ 7], "-", strings[ 8],
			 "-", strings[ 9], "-", strings[10], "-", strings[11],
			 "-", strings[12], NULL);
	    sfree(extrafree);
	    sfree(dupname);

	    return ret;
	}
    }
    return NULL;
}

static int x11_font_width(GdkFont *font, int sixteen_bit)
{
    if (sixteen_bit) {
	XChar2b space;
	space.byte1 = 0;
	space.byte2 = ' ';
	return gdk_text_width(font, (const gchar *)&space, 2);
    } else {
	return gdk_char_width(font, ' ');
    }
}

static unifont *x11font_create(GtkWidget *widget, char *name,
			       int wide, int bold,
			       int shadowoffset, int shadowalways)
{
    struct x11font *xfont;
    GdkFont *font;
    XFontStruct *xfs;
    Display *disp;
    Atom charset_registry, charset_encoding;
    unsigned long registry_ret, encoding_ret;
    int pubcs, realcs, sixteen_bit;
    int i;

    font = gdk_font_load(name);
    if (!font)
	return NULL;

    xfs = GDK_FONT_XFONT(font);
    disp = GDK_FONT_XDISPLAY(font);

    charset_registry = XInternAtom(disp, "CHARSET_REGISTRY", False);
    charset_encoding = XInternAtom(disp, "CHARSET_ENCODING", False);

    pubcs = realcs = CS_NONE;
    sixteen_bit = FALSE;

    if (XGetFontProperty(xfs, charset_registry, &registry_ret) &&
	XGetFontProperty(xfs, charset_encoding, &encoding_ret)) {
	char *reg, *enc;
	reg = XGetAtomName(disp, (Atom)registry_ret);
	enc = XGetAtomName(disp, (Atom)encoding_ret);
	if (reg && enc) {
	    char *encoding = dupcat(reg, "-", enc, NULL);
	    pubcs = realcs = charset_from_xenc(encoding);

	    /*
	     * iso10646-1 is the only wide font encoding we
	     * support. In this case, we expect clients to give us
	     * UTF-8, which this module must internally convert
	     * into 16-bit Unicode.
	     */
	    if (!strcasecmp(encoding, "iso10646-1")) {
		sixteen_bit = TRUE;
		pubcs = realcs = CS_UTF8;
	    }

	    /*
	     * Hack for X line-drawing characters: if the primary
	     * font is encoded as ISO-8859-1, and has valid glyphs
	     * in the first 32 char positions, it is assumed that
	     * those glyphs are the VT100 line-drawing character
	     * set.
	     * 
	     * Actually, we'll hack even harder by only checking
	     * position 0x19 (vertical line, VT100 linedrawing
	     * `x'). Then we can check it easily by seeing if the
	     * ascent and descent differ.
	     */
	    if (pubcs == CS_ISO8859_1) {
		int lb, rb, wid, asc, desc;
		gchar text[2];

		text[1] = '\0';
		text[0] = '\x12';
		gdk_string_extents(font, text, &lb, &rb, &wid, &asc, &desc);
		if (asc != desc)
		    realcs = CS_ISO8859_1_X11;
	    }

	    sfree(encoding);
	}
    }

    xfont = snew(struct x11font);
    xfont->u.vt = &x11font_vtable;
    xfont->u.width = x11_font_width(font, sixteen_bit);
    xfont->u.ascent = font->ascent;
    xfont->u.descent = font->descent;
    xfont->u.height = xfont->u.ascent + xfont->u.descent;
    xfont->u.public_charset = pubcs;
    xfont->u.real_charset = realcs;
    xfont->fonts[0] = font;
    xfont->allocated[0] = TRUE;
    xfont->sixteen_bit = sixteen_bit;
    xfont->wide = wide;
    xfont->bold = bold;
    xfont->shadowoffset = shadowoffset;
    xfont->shadowalways = shadowalways;

    for (i = 1; i < lenof(xfont->fonts); i++) {
	xfont->fonts[i] = NULL;
	xfont->allocated[i] = FALSE;
    }

    return (unifont *)xfont;
}

static void x11font_destroy(unifont *font)
{
    struct x11font *xfont = (struct x11font *)font;
    int i;

    for (i = 0; i < lenof(xfont->fonts); i++)
	if (xfont->fonts[i])
	    gdk_font_unref(xfont->fonts[i]);
    sfree(font);
}

static void x11_alloc_subfont(struct x11font *xfont, int sfid)
{
    char *derived_name = x11_guess_derived_font_name
	(xfont->fonts[0], sfid & 1, !!(sfid & 2));
    xfont->fonts[sfid] = gdk_font_load(derived_name);   /* may be NULL */
    xfont->allocated[sfid] = TRUE;
    sfree(derived_name);
}

static void x11font_draw_text(GdkDrawable *target, GdkGC *gc, unifont *font,
			      int x, int y, const char *string, int len,
			      int wide, int bold, int cellwidth)
{
    struct x11font *xfont = (struct x11font *)font;
    int sfid;
    int shadowbold = FALSE;

    wide -= xfont->wide;
    bold -= xfont->bold;

    /*
     * Decide which subfont we're using, and whether we have to
     * use shadow bold.
     */
    if (xfont->shadowalways && bold) {
	shadowbold = TRUE;
	bold = 0;
    }
    sfid = 2 * wide + bold;
    if (!xfont->allocated[sfid])
	x11_alloc_subfont(xfont, sfid);
    if (bold && !xfont->fonts[sfid]) {
	bold = 0;
	shadowbold = TRUE;
	sfid = 2 * wide + bold;
	if (!xfont->allocated[sfid])
	    x11_alloc_subfont(xfont, sfid);
    }

    if (!xfont->fonts[sfid])
	return;			       /* we've tried our best, but no luck */

    if (xfont->sixteen_bit) {
	/*
	 * This X font has 16-bit character indices, which means
	 * we expect our string to have been passed in UTF-8.
	 */
	XChar2b *xcs;
	wchar_t *wcs;
	int nchars, maxchars, i;

	/*
	 * Convert the input string to wide-character Unicode.
	 */
	maxchars = 0;
	for (i = 0; i < len; i++)
	    if ((unsigned char)string[i] <= 0x7F ||
		(unsigned char)string[i] >= 0xC0)
		maxchars++;
	wcs = snewn(maxchars+1, wchar_t);
	nchars = charset_to_unicode((char **)&string, &len, wcs, maxchars,
				    CS_UTF8, NULL, NULL, 0);
	assert(nchars <= maxchars);
	wcs[nchars] = L'\0';

	xcs = snewn(nchars, XChar2b);
	for (i = 0; i < nchars; i++) {
	    xcs[i].byte1 = wcs[i] >> 8;
	    xcs[i].byte2 = wcs[i];
	}

	gdk_draw_text(target, xfont->fonts[sfid], gc,
		      x, y, (gchar *)xcs, nchars*2);
	if (shadowbold)
	    gdk_draw_text(target, xfont->fonts[sfid], gc,
			  x + xfont->shadowoffset, y, (gchar *)xcs, nchars*2);
	sfree(xcs);
	sfree(wcs);
    } else {
	gdk_draw_text(target, xfont->fonts[sfid], gc, x, y, string, len);
	if (shadowbold)
	    gdk_draw_text(target, xfont->fonts[sfid], gc,
			  x + xfont->shadowoffset, y, string, len);
    }
}

/* ----------------------------------------------------------------------
 * Pango font implementation.
 */

static void pangofont_draw_text(GdkDrawable *target, GdkGC *gc, unifont *font,
				int x, int y, const char *string, int len,
				int wide, int bold, int cellwidth);
static unifont *pangofont_create(GtkWidget *widget, char *name,
				 int wide, int bold,
				 int shadowoffset, int shadowalways);
static void pangofont_destroy(unifont *font);

struct pangofont {
    struct unifont u;
    /*
     * Pango objects.
     */
    PangoFontDescription *desc;
    PangoFontset *fset;
    /*
     * The containing widget.
     */
    GtkWidget *widget;
    /*
     * Data passed in to unifont_create().
     */
    int bold, shadowoffset, shadowalways;
};

static const struct unifont_vtable pangofont_vtable = {
    pangofont_create,
    pangofont_destroy,
    pangofont_draw_text,
    "pango"
};

static unifont *pangofont_create(GtkWidget *widget, char *name,
				 int wide, int bold,
				 int shadowoffset, int shadowalways)
{
    struct pangofont *pfont;
    PangoContext *ctx;
    PangoFontMap *map;
    PangoFontDescription *desc;
    PangoFontset *fset;
    PangoFontMetrics *metrics;

    desc = pango_font_description_from_string(name);
    if (!desc)
	return NULL;
    ctx = gtk_widget_get_pango_context(widget);
    if (!ctx) {
	pango_font_description_free(desc);
	return NULL;
    }
    map = pango_context_get_font_map(ctx);
    if (!map) {
	pango_font_description_free(desc);
	return NULL;
    }
    fset = pango_font_map_load_fontset(map, ctx, desc,
				       pango_context_get_language(ctx));
    if (!fset) {
	pango_font_description_free(desc);
	return NULL;
    }
    metrics = pango_fontset_get_metrics(fset);
    if (!metrics ||
	pango_font_metrics_get_approximate_digit_width(metrics) == 0) {
	pango_font_description_free(desc);
	g_object_unref(fset);
	return NULL;
    }

    pfont = snew(struct pangofont);
    pfont->u.vt = &pangofont_vtable;
    pfont->u.width =
	PANGO_PIXELS(pango_font_metrics_get_approximate_digit_width(metrics));
    pfont->u.ascent = PANGO_PIXELS(pango_font_metrics_get_ascent(metrics));
    pfont->u.descent = PANGO_PIXELS(pango_font_metrics_get_descent(metrics));
    pfont->u.height = pfont->u.ascent + pfont->u.descent;
    /* The Pango API is hardwired to UTF-8 */
    pfont->u.public_charset = CS_UTF8;
    pfont->u.real_charset = CS_UTF8;
    pfont->desc = desc;
    pfont->fset = fset;
    pfont->widget = widget;
    pfont->bold = bold;
    pfont->shadowoffset = shadowoffset;
    pfont->shadowalways = shadowalways;

    return (unifont *)pfont;
}

static void pangofont_destroy(unifont *font)
{
    struct pangofont *pfont = (struct pangofont *)font;
    pfont = pfont;		       /* FIXME */
    pango_font_description_free(pfont->desc);
    g_object_unref(pfont->fset);
    sfree(font);
}

static void pangofont_draw_text(GdkDrawable *target, GdkGC *gc, unifont *font,
				int x, int y, const char *string, int len,
				int wide, int bold, int cellwidth)
{
    struct pangofont *pfont = (struct pangofont *)font;
    PangoLayout *layout;
    PangoRectangle rect;
    int shadowbold = FALSE;

    if (wide)
	cellwidth *= 2;

    y -= pfont->u.ascent;

    layout = pango_layout_new(gtk_widget_get_pango_context(pfont->widget));
    pango_layout_set_font_description(layout, pfont->desc);
    if (bold > pfont->bold) {
	if (pfont->shadowalways)
	    shadowbold = TRUE;
	else {
	    PangoFontDescription *desc2 =
		pango_font_description_copy_static(pfont->desc);
	    pango_font_description_set_weight(desc2, PANGO_WEIGHT_BOLD);
	    pango_layout_set_font_description(layout, desc2);
	}
    }

    while (len > 0) {
	int clen;

	/*
	 * Extract a single UTF-8 character from the string.
	 */
	clen = 1;
	while (clen < len &&
	       (unsigned char)string[clen] >= 0x80 &&
	       (unsigned char)string[clen] < 0xC0)
	    clen++;

	pango_layout_set_text(layout, string, clen);
	pango_layout_get_pixel_extents(layout, NULL, &rect);
	gdk_draw_layout(target, gc, x + (cellwidth - rect.width)/2,
			y + (pfont->u.height - rect.height)/2, layout);
	if (shadowbold)
	    gdk_draw_layout(target, gc, x + (cellwidth - rect.width)/2 + pfont->shadowoffset,
			    y + (pfont->u.height - rect.height)/2, layout);

	len -= clen;
	string += clen;
	x += cellwidth;
    }

    g_object_unref(layout);
}

/* ----------------------------------------------------------------------
 * Outermost functions which do the vtable dispatch.
 */

/*
 * This function is the only one which needs to know the full set
 * of font implementations available, because it has to try each
 * in turn until one works, or condition on an override prefix in
 * the font name.
 */
static const struct unifont_vtable *unifont_types[] = {
    &pangofont_vtable,
    &x11font_vtable,
};
unifont *unifont_create(GtkWidget *widget, char *name, int wide, int bold,
			int shadowoffset, int shadowalways)
{
    int colonpos = strcspn(name, ":");
    int i;

    if (name[colonpos]) {
	/*
	 * There's a colon prefix on the font name. Use it to work
	 * out which subclass to try to create.
	 */
	for (i = 0; i < lenof(unifont_types); i++) {
	    if (strlen(unifont_types[i]->prefix) == colonpos &&
		!strncmp(unifont_types[i]->prefix, name, colonpos))
		break;
	}
	if (i == lenof(unifont_types))
	    return NULL;	       /* prefix not recognised */
	return unifont_types[i]->create(widget, name+colonpos+1, wide, bold,
					shadowoffset, shadowalways);
    } else {
	/*
	 * No colon prefix, so just go through all the subclasses.
	 */
	for (i = 0; i < lenof(unifont_types); i++) {
	    unifont *ret = unifont_types[i]->create(widget, name, wide, bold,
						    shadowoffset,
						    shadowalways);
	    if (ret)
		return ret;
	}
	return NULL;		       /* font not found in any scheme */
    }
}

void unifont_destroy(unifont *font)
{
    font->vt->destroy(font);
}

void unifont_draw_text(GdkDrawable *target, GdkGC *gc, unifont *font,
		       int x, int y, const char *string, int len,
		       int wide, int bold, int cellwidth)
{
    font->vt->draw_text(target, gc, font, x, y, string, len,
			wide, bold, cellwidth);
}
