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

unifont *unifont_create(char *name, int wide, int bold,
			int shadowoffset, int shadowalways);
void unifont_destroy(unifont *font);
void unifont_draw_text(GdkDrawable *target, GdkGC *gc, unifont *font,
		       int x, int y, const char *string, int len,
		       int wide, int bold);

#endif /* PUTTY_GTKFONT_H */
