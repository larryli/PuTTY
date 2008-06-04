/*
 * Header file for gtkfont.c. Has to be separate from unix.h
 * because it depends on GTK data types, hence can't be included
 * from cross-platform code (which doesn't go near GTK).
 */

#ifndef PUTTY_GTKFONT_H
#define PUTTY_GTKFONT_H

/*
 * Exports from gtkfont.c.
 */
struct unifont_vtable;		       /* contents internal to gtkfont.c */
typedef struct unifont {
    const struct unifont_vtable *vt;
    /*
     * `Non-static data members' of the `class', accessible to
     * external code.
     */

    /*
     * public_charset is the charset used when the user asks for
     * `Use font encoding'.
     * 
     * real_charset is the charset used when translating text into
     * a form suitable for sending to unifont_draw_text().
     * 
     * They can differ. For example, public_charset might be
     * CS_ISO8859_1 while real_charset is CS_ISO8859_1_X11.
     */
    int public_charset, real_charset;

    /*
     * Font dimensions needed by clients.
     */
    int width, height, ascent, descent;
} unifont;

unifont *unifont_create(GtkWidget *widget, const char *name,
			int wide, int bold,
			int shadowoffset, int shadowalways);
void unifont_destroy(unifont *font);
void unifont_draw_text(GdkDrawable *target, GdkGC *gc, unifont *font,
		       int x, int y, const char *string, int len,
		       int wide, int bold, int cellwidth);

/*
 * Unified font selector dialog. I can't be bothered to do a
 * proper GTK subclassing today, so this will just be an ordinary
 * data structure with some useful members.
 * 
 * (Of course, these aren't the only members; this structure is
 * contained within a bigger one which holds data visible only to
 * the implementation.)
 */
typedef struct unifontsel {
    void *user_data;		       /* settable by the user */
    GtkWindow *window;
    GtkWidget *ok_button, *cancel_button;
} unifontsel;

unifontsel *unifontsel_new(const char *wintitle);
void unifontsel_destroy(unifontsel *fontsel);
void unifontsel_set_name(unifontsel *fontsel, const char *fontname);
char *unifontsel_get_name(unifontsel *fontsel);

#endif /* PUTTY_GTKFONT_H */
