/*
 * pterm - a fusion of the PuTTY terminal emulator with a Unix pty
 * back end, all running as a GTK application. Wish me luck.
 */

#define _GNU_SOURCE

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define PUTTY_DO_GLOBALS	       /* actually _define_ globals */

#include "putty.h"
#include "terminal.h"

#define CAT2(x,y) x ## y
#define CAT(x,y) CAT2(x,y)
#define ASSERT(x) enum {CAT(assertion_,__LINE__) = 1 / (x)}

#define NCOLOURS (lenof(((Config *)0)->colours))

struct gui_data {
    GtkWidget *window, *area, *sbar;
    GtkBox *hbox;
    GtkAdjustment *sbar_adjust;
    GdkPixmap *pixmap;
    GdkFont *fonts[4];                 /* normal, bold, wide, widebold */
    struct {
	int charset;
	int is_wide;
    } fontinfo[4];
    GdkCursor *rawcursor, *textcursor, *blankcursor, *currcursor;
    GdkColor cols[NCOLOURS];
    GdkColormap *colmap;
    wchar_t *pastein_data;
    int pastein_data_len;
    char *pasteout_data, *pasteout_data_utf8;
    int pasteout_data_len, pasteout_data_utf8_len;
    int font_width, font_height;
    int ignore_sbar;
    int mouseptr_visible;
    guint term_paste_idle_id;
    GdkAtom compound_text_atom, utf8_string_atom;
    int alt_keycode;
    int alt_digits;
    char wintitle[sizeof(((Config *)0)->wintitle)];
    char icontitle[sizeof(((Config *)0)->wintitle)];
    int master_fd, master_func_id, exited;
    void *ldisc;
    Backend *back;
    void *backhandle;
    Terminal *term;
    void *logctx;
};

struct draw_ctx {
    GdkGC *gc;
    struct gui_data *inst;
};

static int send_raw_mouse;

static char *app_name = "pterm";

char *x_get_default(char *key)
{
    return XGetDefault(GDK_DISPLAY(), app_name, key);
}

void ldisc_update(void *frontend, int echo, int edit)
{
    /*
     * This is a stub in pterm. If I ever produce a Unix
     * command-line ssh/telnet/rlogin client (i.e. a port of plink)
     * then it will require some termios manoeuvring analogous to
     * that in the Windows plink.c, but here it's meaningless.
     */
}

int askappend(void *frontend, char *filename)
{
    /*
     * Logging in an xterm-alike is liable to be something you only
     * do at serious diagnostic need. Hence, I'm going to take the
     * easy option for now and assume we always want to overwrite
     * log files. I can always make it properly configurable later.
     */
    return 2;
}

void logevent(void *frontend, char *string)
{
    /*
     * This is not a very helpful function: events are logged
     * pretty much exclusively by the back end, and our pty back
     * end is self-contained. So we need do nothing.
     */
}

int font_dimension(void *frontend, int which)/* 0 for width, 1 for height */
{
    struct gui_data *inst = (struct gui_data *)frontend;

    if (which)
	return inst->font_height;
    else
	return inst->font_width;
}

/*
 * Translate a raw mouse button designation (LEFT, MIDDLE, RIGHT)
 * into a cooked one (SELECT, EXTEND, PASTE).
 * 
 * In Unix, this is not configurable; the X button arrangement is
 * rock-solid across all applications, everyone has a three-button
 * mouse or a means of faking it, and there is no need to switch
 * buttons around at all.
 */
Mouse_Button translate_button(void *frontend, Mouse_Button button)
{
    /* struct gui_data *inst = (struct gui_data *)frontend; */

    if (button == MBT_LEFT)
	return MBT_SELECT;
    if (button == MBT_MIDDLE)
	return MBT_PASTE;
    if (button == MBT_RIGHT)
	return MBT_EXTEND;
    return 0;			       /* shouldn't happen */
}

/*
 * Minimise or restore the window in response to a server-side
 * request.
 */
void set_iconic(void *frontend, int iconic)
{
    /*
     * GTK 1.2 doesn't know how to do this.
     */
#if GTK_CHECK_VERSION(2,0,0)
    struct gui_data *inst = (struct gui_data *)frontend;
    if (iconic)
	gtk_window_iconify(GTK_WINDOW(inst->window));
    else
	gtk_window_deiconify(GTK_WINDOW(inst->window));
#endif
}

/*
 * Move the window in response to a server-side request.
 */
void move_window(void *frontend, int x, int y)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    /*
     * I assume that when the GTK version of this call is available
     * we should use it. Not sure how it differs from the GDK one,
     * though.
     */
#if GTK_CHECK_VERSION(2,0,0)
    gtk_window_move(GTK_WINDOW(inst->window), x, y);
#else
    gdk_window_move(inst->window->window, x, y);
#endif
}

/*
 * Move the window to the top or bottom of the z-order in response
 * to a server-side request.
 */
void set_zorder(void *frontend, int top)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    if (top)
	gdk_window_raise(inst->window->window);
    else
	gdk_window_lower(inst->window->window);
}

/*
 * Refresh the window in response to a server-side request.
 */
void refresh_window(void *frontend)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    term_invalidate(inst->term);
}

/*
 * Maximise or restore the window in response to a server-side
 * request.
 */
void set_zoomed(void *frontend, int zoomed)
{
    /*
     * GTK 1.2 doesn't know how to do this.
     */
#if GTK_CHECK_VERSION(2,0,0)
    struct gui_data *inst = (struct gui_data *)frontend;
    if (iconic)
	gtk_window_maximize(GTK_WINDOW(inst->window));
    else
	gtk_window_unmaximize(GTK_WINDOW(inst->window));
#endif
}

/*
 * Report whether the window is iconic, for terminal reports.
 */
int is_iconic(void *frontend)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    return !gdk_window_is_viewable(inst->window->window);
}

/*
 * Report the window's position, for terminal reports.
 */
void get_window_pos(void *frontend, int *x, int *y)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    /*
     * I assume that when the GTK version of this call is available
     * we should use it. Not sure how it differs from the GDK one,
     * though.
     */
#if GTK_CHECK_VERSION(2,0,0)
    gtk_window_get_position(GTK_WINDOW(inst->window), x, y);
#else
    gdk_window_get_position(inst->window->window, x, y);
#endif
}

/*
 * Report the window's pixel size, for terminal reports.
 */
void get_window_pixels(void *frontend, int *x, int *y)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    /*
     * I assume that when the GTK version of this call is available
     * we should use it. Not sure how it differs from the GDK one,
     * though.
     */
#if GTK_CHECK_VERSION(2,0,0)
    gtk_window_get_size(GTK_WINDOW(inst->window), x, y);
#else
    gdk_window_get_size(inst->window->window, x, y);
#endif
}

/*
 * Return the window or icon title.
 */
char *get_window_title(void *frontend, int icon)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    return icon ? inst->wintitle : inst->icontitle;
}

gint delete_window(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    /*
     * We could implement warn-on-close here if we really wanted
     * to.
     */
    return FALSE;
}

static void show_mouseptr(struct gui_data *inst, int show)
{
    if (!cfg.hide_mouseptr)
	show = 1;
    if (show)
	gdk_window_set_cursor(inst->area->window, inst->currcursor);
    else
	gdk_window_set_cursor(inst->area->window, inst->blankcursor);
    inst->mouseptr_visible = show;
}

gint configure_area(GtkWidget *widget, GdkEventConfigure *event, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    int w, h, need_size = 0;

    w = (event->width - 2*cfg.window_border) / inst->font_width;
    h = (event->height - 2*cfg.window_border) / inst->font_height;

    if (w != cfg.width || h != cfg.height) {
	if (inst->pixmap) {
	    gdk_pixmap_unref(inst->pixmap);
	    inst->pixmap = NULL;
	}
	cfg.width = w;
	cfg.height = h;
	need_size = 1;
    }
    if (!inst->pixmap) {
	GdkGC *gc;

	inst->pixmap = gdk_pixmap_new(widget->window,
				      (cfg.width * inst->font_width +
				       2*cfg.window_border),
				      (cfg.height * inst->font_height +
				       2*cfg.window_border), -1);

	gc = gdk_gc_new(inst->area->window);
	gdk_gc_set_foreground(gc, &inst->cols[18]);   /* default background */
	gdk_draw_rectangle(inst->pixmap, gc, 1, 0, 0,
			   cfg.width * inst->font_width + 2*cfg.window_border,
			   cfg.height * inst->font_height + 2*cfg.window_border);
	gdk_gc_unref(gc);
    }

    if (need_size) {
	term_size(inst->term, h, w, cfg.savelines);
    }

    return TRUE;
}

gint expose_area(GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;

    /*
     * Pass the exposed rectangle to terminal.c, which will call us
     * back to do the actual painting.
     */
    if (inst->pixmap) {
	gdk_draw_pixmap(widget->window,
			widget->style->fg_gc[GTK_WIDGET_STATE(widget)],
			inst->pixmap,
			event->area.x, event->area.y,
			event->area.x, event->area.y,
			event->area.width, event->area.height);
    }
    return TRUE;
}

#define KEY_PRESSED(k) \
    (inst->keystate[(k) / 32] & (1 << ((k) % 32)))

gint key_event(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    char output[32];
    int start, end;

    /* By default, nothing is generated. */
    end = start = 0;

    /*
     * If Alt is being released after typing an Alt+numberpad
     * sequence, we should generate the code that was typed.
     * 
     * Note that we only do this if more than one key was actually
     * pressed - I don't think Alt+NumPad4 should be ^D or that
     * Alt+NumPad3 should be ^C, for example. There's no serious
     * inconvenience in having to type a zero before a single-digit
     * character code.
     */
    if (event->type == GDK_KEY_RELEASE &&
	(event->keyval == GDK_Meta_L || event->keyval == GDK_Alt_L ||
	 event->keyval == GDK_Meta_R || event->keyval == GDK_Alt_R) &&
	inst->alt_keycode >= 0 && inst->alt_digits > 1) {
#ifdef KEY_DEBUGGING
	printf("Alt key up, keycode = %d\n", inst->alt_keycode);
#endif
	output[0] = inst->alt_keycode;
	end = 1;
	goto done;
    }

    if (event->type == GDK_KEY_PRESS) {
#ifdef KEY_DEBUGGING
	{
	    int i;
	    printf("keypress: keyval = %04x, state = %08x; string =",
		   event->keyval, event->state);
	    for (i = 0; event->string[i]; i++)
		printf(" %02x", (unsigned char) event->string[i]);
	    printf("\n");
	}
#endif

	/*
	 * NYI: Compose key (!!! requires Unicode faff before even trying)
	 */

	/*
	 * If Alt has just been pressed, we start potentially
	 * accumulating an Alt+numberpad code. We do this by
	 * setting alt_keycode to -1 (nothing yet but plausible).
	 */
	if ((event->keyval == GDK_Meta_L || event->keyval == GDK_Alt_L ||
	     event->keyval == GDK_Meta_R || event->keyval == GDK_Alt_R)) {
	    inst->alt_keycode = -1;
            inst->alt_digits = 0;
	    goto done;		       /* this generates nothing else */
	}

	/*
	 * If we're seeing a numberpad key press with Mod1 down,
	 * consider adding it to alt_keycode if that's sensible.
	 * Anything _else_ with Mod1 down cancels any possibility
	 * of an ALT keycode: we set alt_keycode to -2.
	 */
	if ((event->state & GDK_MOD1_MASK) && inst->alt_keycode != -2) {
	    int digit = -1;
	    switch (event->keyval) {
	      case GDK_KP_0: case GDK_KP_Insert: digit = 0; break;
	      case GDK_KP_1: case GDK_KP_End: digit = 1; break;
	      case GDK_KP_2: case GDK_KP_Down: digit = 2; break;
	      case GDK_KP_3: case GDK_KP_Page_Down: digit = 3; break;
	      case GDK_KP_4: case GDK_KP_Left: digit = 4; break;
	      case GDK_KP_5: case GDK_KP_Begin: digit = 5; break;
	      case GDK_KP_6: case GDK_KP_Right: digit = 6; break;
	      case GDK_KP_7: case GDK_KP_Home: digit = 7; break;
	      case GDK_KP_8: case GDK_KP_Up: digit = 8; break;
	      case GDK_KP_9: case GDK_KP_Page_Up: digit = 9; break;
	    }
	    if (digit < 0)
		inst->alt_keycode = -2;   /* it's invalid */
	    else {
#ifdef KEY_DEBUGGING
		printf("Adding digit %d to keycode %d", digit,
		       inst->alt_keycode);
#endif
		if (inst->alt_keycode == -1)
		    inst->alt_keycode = digit;   /* one-digit code */
		else
		    inst->alt_keycode = inst->alt_keycode * 10 + digit;
                inst->alt_digits++;
#ifdef KEY_DEBUGGING
		printf(" gives new code %d\n", inst->alt_keycode);
#endif
		/* Having used this digit, we now do nothing more with it. */
		goto done;
	    }
	}

	/*
	 * Shift-PgUp and Shift-PgDn don't even generate keystrokes
	 * at all.
	 */
	if (event->keyval == GDK_Page_Up && (event->state & GDK_SHIFT_MASK)) {
	    term_scroll(inst->term, 0, -cfg.height/2);
	    return TRUE;
	}
	if (event->keyval == GDK_Page_Down && (event->state & GDK_SHIFT_MASK)) {
	    term_scroll(inst->term, 0, +cfg.height/2);
	    return TRUE;
	}

	/*
	 * Neither does Shift-Ins.
	 */
	if (event->keyval == GDK_Insert && (event->state & GDK_SHIFT_MASK)) {
	    request_paste(inst);
	    return TRUE;
	}

	/* ALT+things gives leading Escape. */
	output[0] = '\033';
	strncpy(output+1, event->string, 31);
	output[31] = '\0';
	end = strlen(output);
	if (event->state & GDK_MOD1_MASK) {
	    start = 0;
	    if (end == 1) end = 0;
	} else
	    start = 1;

	/* Control-` is the same as Control-\ (unless gtk has a better idea) */
	if (!event->string[0] && event->keyval == '`' &&
	    (event->state & GDK_CONTROL_MASK)) {
	    output[1] = '\x1C';
	    end = 2;
	}

	/* Control-Break is the same as Control-C */
	if (event->keyval == GDK_Break &&
	    (event->state & GDK_CONTROL_MASK)) {
	    output[1] = '\003';
	    end = 2;
	}

	/* Control-2, Control-Space and Control-@ are NUL */
	if (!event->string[0] &&
	    (event->keyval == ' ' || event->keyval == '2' ||
	     event->keyval == '@') &&
	    (event->state & (GDK_SHIFT_MASK |
			     GDK_CONTROL_MASK)) == GDK_CONTROL_MASK) {
	    output[1] = '\0';
	    end = 2;
	}

	/* Control-Shift-Space is 160 (ISO8859 nonbreaking space) */
	if (!event->string[0] && event->keyval == ' ' &&
	    (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK)) ==
	    (GDK_SHIFT_MASK | GDK_CONTROL_MASK)) {
	    output[1] = '\240';
	    end = 2;
	}

	/* We don't let GTK tell us what Backspace is! We know better. */
	if (event->keyval == GDK_BackSpace &&
	    !(event->state & GDK_SHIFT_MASK)) {
	    output[1] = cfg.bksp_is_delete ? '\x7F' : '\x08';
	    end = 2;
	}
	/* For Shift Backspace, do opposite of what is configured. */
	if (event->keyval == GDK_BackSpace &&
	    (event->state & GDK_SHIFT_MASK)) {
	    output[1] = cfg.bksp_is_delete ? '\x08' : '\x7F';
	    end = 2;
	}

	/* Shift-Tab is ESC [ Z */
	if (event->keyval == GDK_ISO_Left_Tab ||
	    (event->keyval == GDK_Tab && (event->state & GDK_SHIFT_MASK))) {
	    end = 1 + sprintf(output+1, "\033[Z");
	}

	/*
	 * NetHack keypad mode.
	 */
	if (cfg.nethack_keypad) {
	    char *keys = NULL;
	    switch (event->keyval) {
	      case GDK_KP_1: case GDK_KP_End: keys = "bB"; break;
	      case GDK_KP_2: case GDK_KP_Down: keys = "jJ"; break;
	      case GDK_KP_3: case GDK_KP_Page_Down: keys = "nN"; break;
	      case GDK_KP_4: case GDK_KP_Left: keys = "hH"; break;
	      case GDK_KP_5: case GDK_KP_Begin: keys = ".."; break;
	      case GDK_KP_6: case GDK_KP_Right: keys = "lL"; break;
	      case GDK_KP_7: case GDK_KP_Home: keys = "yY"; break;
	      case GDK_KP_8: case GDK_KP_Up: keys = "kK"; break;
	      case GDK_KP_9: case GDK_KP_Page_Up: keys = "uU"; break;
	    }
	    if (keys) {
		end = 2;
		if (event->state & GDK_SHIFT_MASK)
		    output[1] = keys[1];
		else
		    output[1] = keys[0];
		goto done;
	    }
	}

	/*
	 * Application keypad mode.
	 */
	if (inst->term->app_keypad_keys && !cfg.no_applic_k) {
	    int xkey = 0;
	    switch (event->keyval) {
	      case GDK_Num_Lock: xkey = 'P'; break;
	      case GDK_KP_Divide: xkey = 'Q'; break;
	      case GDK_KP_Multiply: xkey = 'R'; break;
	      case GDK_KP_Subtract: xkey = 'S'; break;
		/*
		 * Keypad + is tricky. It covers a space that would
		 * be taken up on the VT100 by _two_ keys; so we
		 * let Shift select between the two. Worse still,
		 * in xterm function key mode we change which two...
		 */
	      case GDK_KP_Add:
		if (cfg.funky_type == 2) {
		    if (event->state & GDK_SHIFT_MASK)
			xkey = 'l';
		    else
			xkey = 'k';
		} else if (event->state & GDK_SHIFT_MASK)
			xkey = 'm';
		else
		    xkey = 'l';
		break;
	      case GDK_KP_Enter: xkey = 'M'; break;
	      case GDK_KP_0: case GDK_KP_Insert: xkey = 'p'; break;
	      case GDK_KP_1: case GDK_KP_End: xkey = 'q'; break;
	      case GDK_KP_2: case GDK_KP_Down: xkey = 'r'; break;
	      case GDK_KP_3: case GDK_KP_Page_Down: xkey = 's'; break;
	      case GDK_KP_4: case GDK_KP_Left: xkey = 't'; break;
	      case GDK_KP_5: case GDK_KP_Begin: xkey = 'u'; break;
	      case GDK_KP_6: case GDK_KP_Right: xkey = 'v'; break;
	      case GDK_KP_7: case GDK_KP_Home: xkey = 'w'; break;
	      case GDK_KP_8: case GDK_KP_Up: xkey = 'x'; break;
	      case GDK_KP_9: case GDK_KP_Page_Up: xkey = 'y'; break;
	      case GDK_KP_Decimal: case GDK_KP_Delete: xkey = 'n'; break;
	    }
	    if (xkey) {
		if (inst->term->vt52_mode) {
		    if (xkey >= 'P' && xkey <= 'S')
			end = 1 + sprintf(output+1, "\033%c", xkey);
		    else
			end = 1 + sprintf(output+1, "\033?%c", xkey);
		} else
		    end = 1 + sprintf(output+1, "\033O%c", xkey);
		goto done;
	    }
	}

	/*
	 * Next, all the keys that do tilde codes. (ESC '[' nn '~',
	 * for integer decimal nn.)
	 *
	 * We also deal with the weird ones here. Linux VCs replace F1
	 * to F5 by ESC [ [ A to ESC [ [ E. rxvt doesn't do _that_, but
	 * does replace Home and End (1~ and 4~) by ESC [ H and ESC O w
	 * respectively.
	 */
	{
	    int code = 0;
	    switch (event->keyval) {
	      case GDK_F1:
		code = (event->state & GDK_SHIFT_MASK ? 23 : 11);
		break;
	      case GDK_F2:
		code = (event->state & GDK_SHIFT_MASK ? 24 : 12);
		break;
	      case GDK_F3:
		code = (event->state & GDK_SHIFT_MASK ? 25 : 13);
		break;
	      case GDK_F4:
		code = (event->state & GDK_SHIFT_MASK ? 26 : 14);
		break;
	      case GDK_F5:
		code = (event->state & GDK_SHIFT_MASK ? 28 : 15);
		break;
	      case GDK_F6:
		code = (event->state & GDK_SHIFT_MASK ? 29 : 17);
		break;
	      case GDK_F7:
		code = (event->state & GDK_SHIFT_MASK ? 31 : 18);
		break;
	      case GDK_F8:
		code = (event->state & GDK_SHIFT_MASK ? 32 : 19);
		break;
	      case GDK_F9:
		code = (event->state & GDK_SHIFT_MASK ? 33 : 20);
		break;
	      case GDK_F10:
		code = (event->state & GDK_SHIFT_MASK ? 34 : 21);
		break;
	      case GDK_F11:
		code = 23;
		break;
	      case GDK_F12:
		code = 24;
		break;
	      case GDK_F13:
		code = 25;
		break;
	      case GDK_F14:
		code = 26;
		break;
	      case GDK_F15:
		code = 28;
		break;
	      case GDK_F16:
		code = 29;
		break;
	      case GDK_F17:
		code = 31;
		break;
	      case GDK_F18:
		code = 32;
		break;
	      case GDK_F19:
		code = 33;
		break;
	      case GDK_F20:
		code = 34;
		break;
	    }
	    if (!(event->state & GDK_CONTROL_MASK)) switch (event->keyval) {
	      case GDK_Home: case GDK_KP_Home:
		code = 1;
		break;
	      case GDK_Insert: case GDK_KP_Insert:
		code = 2;
		break;
	      case GDK_Delete: case GDK_KP_Delete:
		code = 3;
		break;
	      case GDK_End: case GDK_KP_End:
		code = 4;
		break;
	      case GDK_Page_Up: case GDK_KP_Page_Up:
		code = 5;
		break;
	      case GDK_Page_Down: case GDK_KP_Page_Down:
		code = 6;
		break;
	    }
	    /* Reorder edit keys to physical order */
	    if (cfg.funky_type == 3 && code <= 6)
		code = "\0\2\1\4\5\3\6"[code];

	    if (inst->term->vt52_mode && code > 0 && code <= 6) {
		end = 1 + sprintf(output+1, "\x1B%c", " HLMEIG"[code]);
		goto done;
	    }

	    if (cfg.funky_type == 5 &&     /* SCO function keys */
		code >= 11 && code <= 34) {
		char codes[] = "MNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz@[\\]^_`{";
		int index = 0;
		switch (event->keyval) {
		  case GDK_F1: index = 0; break;
		  case GDK_F2: index = 1; break;
		  case GDK_F3: index = 2; break;
		  case GDK_F4: index = 3; break;
		  case GDK_F5: index = 4; break;
		  case GDK_F6: index = 5; break;
		  case GDK_F7: index = 6; break;
		  case GDK_F8: index = 7; break;
		  case GDK_F9: index = 8; break;
		  case GDK_F10: index = 9; break;
		  case GDK_F11: index = 10; break;
		  case GDK_F12: index = 11; break;
		}
		if (event->state & GDK_SHIFT_MASK) index += 12;
		if (event->state & GDK_CONTROL_MASK) index += 24;
		end = 1 + sprintf(output+1, "\x1B[%c", codes[index]);
		goto done;
	    }
	    if (cfg.funky_type == 5 &&     /* SCO small keypad */
		code >= 1 && code <= 6) {
		char codes[] = "HL.FIG";
		if (code == 3) {
		    output[1] = '\x7F';
		    end = 2;
		} else {
		    end = 1 + sprintf(output+1, "\x1B[%c", codes[code-1]);
		}
		goto done;
	    }
	    if ((inst->term->vt52_mode || cfg.funky_type == 4) &&
		code >= 11 && code <= 24) {
		int offt = 0;
		if (code > 15)
		    offt++;
		if (code > 21)
		    offt++;
		if (inst->term->vt52_mode)
		    end = 1 + sprintf(output+1,
				      "\x1B%c", code + 'P' - 11 - offt);
		else
		    end = 1 + sprintf(output+1,
				      "\x1BO%c", code + 'P' - 11 - offt);
		goto done;
	    }
	    if (cfg.funky_type == 1 && code >= 11 && code <= 15) {
		end = 1 + sprintf(output+1, "\x1B[[%c", code + 'A' - 11);
		goto done;
	    }
	    if (cfg.funky_type == 2 && code >= 11 && code <= 14) {
		if (inst->term->vt52_mode)
		    end = 1 + sprintf(output+1, "\x1B%c", code + 'P' - 11);
		else
		    end = 1 + sprintf(output+1, "\x1BO%c", code + 'P' - 11);
		goto done;
	    }
	    if (cfg.rxvt_homeend && (code == 1 || code == 4)) {
		end = 1 + sprintf(output+1, code == 1 ? "\x1B[H" : "\x1BOw");
		goto done;
	    }
	    if (code) {
		end = 1 + sprintf(output+1, "\x1B[%d~", code);
		goto done;
	    }
	}

	/*
	 * Cursor keys. (This includes the numberpad cursor keys,
	 * if we haven't already done them due to app keypad mode.)
	 * 
	 * Here we also process un-numlocked un-appkeypadded KP5,
	 * which sends ESC [ G.
	 */
	{
	    int xkey = 0;
	    switch (event->keyval) {
	      case GDK_Up: case GDK_KP_Up: xkey = 'A'; break;
	      case GDK_Down: case GDK_KP_Down: xkey = 'B'; break;
	      case GDK_Right: case GDK_KP_Right: xkey = 'C'; break;
	      case GDK_Left: case GDK_KP_Left: xkey = 'D'; break;
	      case GDK_Begin: case GDK_KP_Begin: xkey = 'G'; break;
	    }
	    if (xkey) {
		/*
		 * The arrow keys normally do ESC [ A and so on. In
		 * app cursor keys mode they do ESC O A instead.
		 * Ctrl toggles the two modes.
		 */
		if (inst->term->vt52_mode) {
		    end = 1 + sprintf(output+1, "\033%c", xkey);
		} else if (!inst->term->app_cursor_keys ^
			   !(event->state & GDK_CONTROL_MASK)) {
		    end = 1 + sprintf(output+1, "\033O%c", xkey);
		} else {		    
		    end = 1 + sprintf(output+1, "\033[%c", xkey);
		}
		goto done;
	    }
	}
	goto done;
    }

    done:

    if (end-start > 0) {
#ifdef KEY_DEBUGGING
	int i;
	printf("generating sequence:");
	for (i = start; i < end; i++)
	    printf(" %02x", (unsigned char) output[i]);
	printf("\n");
#endif

	/*
	 * The stuff we've just generated is assumed to be
	 * ISO-8859-1! This sounds insane, but `man XLookupString'
	 * agrees: strings of this type returned from the X server
	 * are hardcoded to 8859-1. Strictly speaking we should be
	 * doing this using some sort of GtkIMContext, which (if
	 * we're lucky) would give us our data directly in Unicode;
	 * but that's not supported in GTK 1.2 as far as I can
	 * tell, and it's poorly documented even in 2.0, so it'll
	 * have to wait.
	 */
	lpage_send(inst->ldisc, CS_ISO8859_1, output+start, end-start, 1);

	show_mouseptr(inst, 0);
	term_seen_key_event(inst->term);
	term_out(inst->term);
    }

    return TRUE;
}

gint button_event(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    int shift, ctrl, alt, x, y, button, act;

    show_mouseptr(inst, 1);

    if (event->button == 4 && event->type == GDK_BUTTON_PRESS) {
	term_scroll(inst->term, 0, -5);
	return TRUE;
    }
    if (event->button == 5 && event->type == GDK_BUTTON_PRESS) {
	term_scroll(inst->term, 0, +5);
	return TRUE;
    }

    shift = event->state & GDK_SHIFT_MASK;
    ctrl = event->state & GDK_CONTROL_MASK;
    alt = event->state & GDK_MOD1_MASK;
    if (event->button == 1)
	button = MBT_LEFT;
    else if (event->button == 2)
	button = MBT_MIDDLE;
    else if (event->button == 3)
	button = MBT_RIGHT;
    else
	return FALSE;		       /* don't even know what button! */

    switch (event->type) {
      case GDK_BUTTON_PRESS: act = MA_CLICK; break;
      case GDK_BUTTON_RELEASE: act = MA_RELEASE; break;
      case GDK_2BUTTON_PRESS: act = MA_2CLK; break;
      case GDK_3BUTTON_PRESS: act = MA_3CLK; break;
      default: return FALSE;	       /* don't know this event type */
    }

    if (send_raw_mouse && !(cfg.mouse_override && shift) &&
	act != MA_CLICK && act != MA_RELEASE)
	return TRUE;		       /* we ignore these in raw mouse mode */

    x = (event->x - cfg.window_border) / inst->font_width;
    y = (event->y - cfg.window_border) / inst->font_height;

    term_mouse(inst->term, button, act, x, y, shift, ctrl, alt);

    return TRUE;
}

gint motion_event(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    int shift, ctrl, alt, x, y, button;

    show_mouseptr(inst, 1);

    shift = event->state & GDK_SHIFT_MASK;
    ctrl = event->state & GDK_CONTROL_MASK;
    alt = event->state & GDK_MOD1_MASK;
    if (event->state & GDK_BUTTON1_MASK)
	button = MBT_LEFT;
    else if (event->state & GDK_BUTTON2_MASK)
	button = MBT_MIDDLE;
    else if (event->state & GDK_BUTTON3_MASK)
	button = MBT_RIGHT;
    else
	return FALSE;		       /* don't even know what button! */

    x = (event->x - cfg.window_border) / inst->font_width;
    y = (event->y - cfg.window_border) / inst->font_height;

    term_mouse(inst->term, button, MA_DRAG, x, y, shift, ctrl, alt);

    return TRUE;
}

void done_with_pty(struct gui_data *inst)
{
    extern void pty_close(void);

    if (inst->master_fd >= 0) {
	pty_close();
	inst->master_fd = -1;
	gtk_input_remove(inst->master_func_id);
    }

    if (!inst->exited && inst->back->exitcode(inst->backhandle) >= 0) {
	int exitcode = inst->back->exitcode(inst->backhandle);
	int clean;

	clean = WIFEXITED(exitcode) && (WEXITSTATUS(exitcode) == 0);

	/*
	 * Terminate now, if the Close On Exit setting is
	 * appropriate.
	 */
	if (cfg.close_on_exit == COE_ALWAYS ||
	    (cfg.close_on_exit == COE_NORMAL && clean))
	    exit(0);

	/*
	 * Otherwise, output an indication that the session has
	 * closed.
	 */
	{
	    char message[512];
	    if (WIFEXITED(exitcode))
		sprintf(message, "\r\n[pterm: process terminated with exit"
			" code %d]\r\n", WEXITSTATUS(exitcode));
	    else if (WIFSIGNALED(exitcode))
#ifdef HAVE_NO_STRSIGNAL
		sprintf(message, "\r\n[pterm: process terminated on signal"
			" %d]\r\n", WTERMSIG(exitcode));
#else
		sprintf(message, "\r\n[pterm: process terminated on signal"
			" %d (%.400s)]\r\n", WTERMSIG(exitcode),
			strsignal(WTERMSIG(exitcode)));
#endif
	    from_backend((void *)inst->term, 0, message, strlen(message));
	}
	inst->exited = 1;
    }
}

void frontend_keypress(void *handle)
{
    struct gui_data *inst = (struct gui_data *)handle;

    /*
     * If our child process has exited but not closed, terminate on
     * any keypress.
     */
    if (inst->exited)
	exit(0);
}

gint timer_func(gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;

    if (inst->back->exitcode(inst->backhandle) >= 0) {
	/*
	 * The primary child process died. We could keep the
	 * terminal open for remaining subprocesses to output to,
	 * but conventional wisdom seems to feel that that's the
	 * Wrong Thing for an xterm-alike, so we bail out now
	 * (though we don't necessarily _close_ the window,
	 * depending on the state of Close On Exit). This would be
	 * easy enough to change or make configurable if necessary.
	 */
	done_with_pty(inst);
    }

    term_update(inst->term);
    term_blink(inst->term, 0);
    return TRUE;
}

void pty_input_func(gpointer data, gint sourcefd, GdkInputCondition condition)
{
    struct gui_data *inst = (struct gui_data *)data;
    char buf[4096];
    int ret;

    ret = read(sourcefd, buf, sizeof(buf));

    /*
     * Clean termination condition is that either ret == 0, or ret
     * < 0 and errno == EIO. Not sure why the latter, but it seems
     * to happen. Boo.
     */
    if (ret == 0 || (ret < 0 && errno == EIO)) {
	done_with_pty(inst);
    } else if (ret < 0) {
	perror("read pty master");
	exit(1);
    } else if (ret > 0)
	from_backend(inst->term, 0, buf, ret);
    term_blink(inst->term, 1);
    term_out(inst->term);
}

void destroy(GtkWidget *widget, gpointer data)
{
    gtk_main_quit();
}

gint focus_event(GtkWidget *widget, GdkEventFocus *event, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    inst->term->has_focus = event->in;
    term_out(inst->term);
    term_update(inst->term);
    show_mouseptr(inst, 1);
    return FALSE;
}

/*
 * set or clear the "raw mouse message" mode
 */
void set_raw_mouse_mode(void *frontend, int activate)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    activate = activate && !cfg.no_mouse_rep;
    send_raw_mouse = activate;
    if (send_raw_mouse)
	inst->currcursor = inst->rawcursor;
    else
	inst->currcursor = inst->textcursor;
    show_mouseptr(inst, inst->mouseptr_visible);
}

void request_resize(void *frontend, int w, int h)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    int large_x, large_y;
    int offset_x, offset_y;
    int area_x, area_y;
    GtkRequisition inner, outer;

    /*
     * This is a heinous hack dreamed up by the gnome-terminal
     * people to get around a limitation in gtk. The problem is
     * that in order to set the size correctly we really need to be
     * calling gtk_window_resize - but that needs to know the size
     * of the _whole window_, not the drawing area. So what we do
     * is to set an artificially huge size request on the drawing
     * area, recompute the resulting size request on the window,
     * and look at the difference between the two. That gives us
     * the x and y offsets we need to translate drawing area size
     * into window size for real, and then we call
     * gtk_window_resize.
     */

    /*
     * We start by retrieving the current size of the whole window.
     * Adding a bit to _that_ will give us a value we can use as a
     * bogus size request which guarantees to be bigger than the
     * current size of the drawing area.
     */
    get_window_pixels(inst, &large_x, &large_y);
    large_x += 32;
    large_y += 32;

#if GTK_CHECK_VERSION(2,0,0)
    gtk_widget_set_size_request(inst->area, large_x, large_y);
#else
    gtk_widget_set_usize(inst->area, large_x, large_y);
#endif
    gtk_widget_size_request(inst->area, &inner);
    gtk_widget_size_request(inst->window, &outer);

    offset_x = outer.width - inner.width;
    offset_y = outer.height - inner.height;

    area_x = inst->font_width * w + 2*cfg.window_border;
    area_y = inst->font_height * h + 2*cfg.window_border;

    /*
     * Now we must set the size request on the drawing area back to
     * something sensible before we commit the real resize. Best
     * way to do this, I think, is to set it to what the size is
     * really going to end up being.
     */
#if GTK_CHECK_VERSION(2,0,0)
    gtk_widget_set_size_request(inst->area, area_x, area_y);
#else
    gtk_widget_set_usize(inst->area, area_x, area_y);
#endif

#if GTK_CHECK_VERSION(2,0,0)
    gtk_window_resize(GTK_WINDOW(inst->window),
		      area_x + offset_x, area_y + offset_y);
#else
    gdk_window_resize(inst->window->window,
		      area_x + offset_x, area_y + offset_y);
#endif
}

static void real_palette_set(struct gui_data *inst, int n, int r, int g, int b)
{
    gboolean success[1];

    inst->cols[n].red = r * 0x0101;
    inst->cols[n].green = g * 0x0101;
    inst->cols[n].blue = b * 0x0101;

    gdk_colormap_alloc_colors(inst->colmap, inst->cols + n, 1,
			      FALSE, FALSE, success);
    if (!success[0])
	g_error("pterm: couldn't allocate colour %d (#%02x%02x%02x)\n",
		n, r, g, b);
}

void set_window_background(struct gui_data *inst)
{
    if (inst->area && inst->area->window)
	gdk_window_set_background(inst->area->window, &inst->cols[18]);
    if (inst->window && inst->window->window)
	gdk_window_set_background(inst->window->window, &inst->cols[18]);
}

void palette_set(void *frontend, int n, int r, int g, int b)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    static const int first[21] = {
	0, 2, 4, 6, 8, 10, 12, 14,
	1, 3, 5, 7, 9, 11, 13, 15,
	16, 17, 18, 20, 22
    };
    real_palette_set(inst, first[n], r, g, b);
    if (first[n] >= 18)
	real_palette_set(inst, first[n] + 1, r, g, b);
    if (first[n] == 18)
	set_window_background(inst);
}

void palette_reset(void *frontend)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    /* This maps colour indices in cfg to those used in inst->cols. */
    static const int ww[] = {
	6, 7, 8, 9, 10, 11, 12, 13,
        14, 15, 16, 17, 18, 19, 20, 21,
	0, 1, 2, 3, 4, 5
    };
    gboolean success[NCOLOURS];
    int i;

    assert(lenof(ww) == NCOLOURS);

    if (!inst->colmap) {
	inst->colmap = gdk_colormap_get_system();
    } else {
	gdk_colormap_free_colors(inst->colmap, inst->cols, NCOLOURS);
    }

    for (i = 0; i < NCOLOURS; i++) {
	inst->cols[i].red = cfg.colours[ww[i]][0] * 0x0101;
	inst->cols[i].green = cfg.colours[ww[i]][1] * 0x0101;
	inst->cols[i].blue = cfg.colours[ww[i]][2] * 0x0101;
    }

    gdk_colormap_alloc_colors(inst->colmap, inst->cols, NCOLOURS,
			      FALSE, FALSE, success);
    for (i = 0; i < NCOLOURS; i++) {
	if (!success[i])
	    g_error("pterm: couldn't allocate colour %d (#%02x%02x%02x)\n",
		    i, cfg.colours[i][0], cfg.colours[i][1], cfg.colours[i][2]);
    }

    set_window_background(inst);
}

void write_clip(void *frontend, wchar_t * data, int len, int must_deselect)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    if (inst->pasteout_data)
	sfree(inst->pasteout_data);
    if (inst->pasteout_data_utf8)
	sfree(inst->pasteout_data_utf8);

    inst->pasteout_data_utf8 = smalloc(len*6);
    inst->pasteout_data_utf8_len = len*6;
    {
	wchar_t *tmp = data;
	int tmplen = len;
	inst->pasteout_data_utf8_len =
	    charset_from_unicode(&tmp, &tmplen, inst->pasteout_data_utf8,
				 inst->pasteout_data_utf8_len,
				 CS_UTF8, NULL, NULL, 0);
	inst->pasteout_data_utf8 =
	    srealloc(inst->pasteout_data_utf8, inst->pasteout_data_utf8_len);
    }

    inst->pasteout_data = smalloc(len);
    inst->pasteout_data_len = len;
    wc_to_mb(line_codepage, 0, data, len,
	     inst->pasteout_data, inst->pasteout_data_len,
	     NULL, NULL);

    if (gtk_selection_owner_set(inst->area, GDK_SELECTION_PRIMARY,
				GDK_CURRENT_TIME)) {
	gtk_selection_add_target(inst->area, GDK_SELECTION_PRIMARY,
				 GDK_SELECTION_TYPE_STRING, 1);
	gtk_selection_add_target(inst->area, GDK_SELECTION_PRIMARY,
				 inst->compound_text_atom, 1);
	gtk_selection_add_target(inst->area, GDK_SELECTION_PRIMARY,
				 inst->utf8_string_atom, 1);
    }
}

void selection_get(GtkWidget *widget, GtkSelectionData *seldata,
		   guint info, guint time_stamp, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    if (seldata->target == inst->utf8_string_atom)
	gtk_selection_data_set(seldata, seldata->target, 8,
			       inst->pasteout_data_utf8,
			       inst->pasteout_data_utf8_len);
    else
	gtk_selection_data_set(seldata, seldata->target, 8,
			       inst->pasteout_data, inst->pasteout_data_len);
}

gint selection_clear(GtkWidget *widget, GdkEventSelection *seldata,
		     gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    term_deselect(inst->term);
    if (inst->pasteout_data)
	sfree(inst->pasteout_data);
    if (inst->pasteout_data_utf8)
	sfree(inst->pasteout_data_utf8);
    inst->pasteout_data = NULL;
    inst->pasteout_data_len = 0;
    inst->pasteout_data_utf8 = NULL;
    inst->pasteout_data_utf8_len = 0;
    return TRUE;
}

void request_paste(void *frontend)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    /*
     * In Unix, pasting is asynchronous: all we can do at the
     * moment is to call gtk_selection_convert(), and when the data
     * comes back _then_ we can call term_do_paste().
     */

    /*
     * First we attempt to retrieve the selection as a UTF-8 string
     * (which we will convert to the correct code page before
     * sending to the session, of course). If that fails,
     * selection_received() will be informed and will fall back to
     * an ordinary string.
     */
    gtk_selection_convert(inst->area, GDK_SELECTION_PRIMARY,
			  inst->utf8_string_atom, GDK_CURRENT_TIME);
}

gint idle_paste_func(gpointer data);   /* forward ref */

void selection_received(GtkWidget *widget, GtkSelectionData *seldata,
			guint time, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;

    if (seldata->target == inst->utf8_string_atom && seldata->length <= 0) {
	/*
	 * Failed to get a UTF-8 selection string. Try an ordinary
	 * string.
	 */
	gtk_selection_convert(inst->area, GDK_SELECTION_PRIMARY,
			      GDK_SELECTION_TYPE_STRING, GDK_CURRENT_TIME);
	return;
    }

    /*
     * Any other failure should just go foom.
     */
    if (seldata->length <= 0 ||
	(seldata->type != GDK_SELECTION_TYPE_STRING &&
	 seldata->type != inst->utf8_string_atom))
	return;			       /* Nothing happens. */

    if (inst->pastein_data)
	sfree(inst->pastein_data);

    inst->pastein_data = smalloc(seldata->length * sizeof(wchar_t));
    inst->pastein_data_len = seldata->length;
    inst->pastein_data_len =
	mb_to_wc((seldata->type == inst->utf8_string_atom ?
		  CS_UTF8 : line_codepage),
		 0, seldata->data, seldata->length,
		 inst->pastein_data, inst->pastein_data_len);

    term_do_paste(inst->term);

    if (term_paste_pending(inst->term))
	inst->term_paste_idle_id = gtk_idle_add(idle_paste_func, inst);
}

gint idle_paste_func(gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;

    if (term_paste_pending(inst->term))
	term_paste(inst->term);
    else
	gtk_idle_remove(inst->term_paste_idle_id);

    return TRUE;
}


void get_clip(void *frontend, wchar_t ** p, int *len)
{
    struct gui_data *inst = (struct gui_data *)frontend;

    if (p) {
	*p = inst->pastein_data;
	*len = inst->pastein_data_len;
    }
}

void set_title(void *frontend, char *title)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    strncpy(inst->wintitle, title, lenof(inst->wintitle));
    inst->wintitle[lenof(inst->wintitle)-1] = '\0';
    gtk_window_set_title(GTK_WINDOW(inst->window), inst->wintitle);
}

void set_icon(void *frontend, char *title)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    strncpy(inst->icontitle, title, lenof(inst->icontitle));
    inst->icontitle[lenof(inst->icontitle)-1] = '\0';
    gdk_window_set_icon_name(inst->window->window, inst->icontitle);
}

void set_sbar(void *frontend, int total, int start, int page)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    if (!cfg.scrollbar)
	return;
    inst->sbar_adjust->lower = 0;
    inst->sbar_adjust->upper = total;
    inst->sbar_adjust->value = start;
    inst->sbar_adjust->page_size = page;
    inst->sbar_adjust->step_increment = 1;
    inst->sbar_adjust->page_increment = page/2;
    inst->ignore_sbar = TRUE;
    gtk_adjustment_changed(inst->sbar_adjust);
    inst->ignore_sbar = FALSE;
}

void scrollbar_moved(GtkAdjustment *adj, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;

    if (!cfg.scrollbar)
	return;
    if (!inst->ignore_sbar)
	term_scroll(inst->term, 1, (int)adj->value);
}

void sys_cursor(void *frontend, int x, int y)
{
    /*
     * This is meaningless under X.
     */
}

/*
 * This is still called when mode==BELL_VISUAL, even though the
 * visual bell is handled entirely within terminal.c, because we
 * may want to perform additional actions on any kind of bell (for
 * example, taskbar flashing in Windows).
 */
void beep(void *frontend, int mode)
{
    if (mode != BELL_VISUAL)
	gdk_beep();
}

int char_width(Context ctx, int uc)
{
    /*
     * Under X, any fixed-width font really _is_ fixed-width.
     * Double-width characters will be dealt with using a separate
     * font. For the moment we can simply return 1.
     */
    return 1;
}

Context get_ctx(void *frontend)
{
    struct gui_data *inst = (struct gui_data *)frontend;
    struct draw_ctx *dctx;

    if (!inst->area->window)
	return NULL;

    dctx = smalloc(sizeof(*dctx));
    dctx->inst = inst;
    dctx->gc = gdk_gc_new(inst->area->window);
    return dctx;
}

void free_ctx(Context ctx)
{
    struct draw_ctx *dctx = (struct draw_ctx *)ctx;
    /* struct gui_data *inst = dctx->inst; */
    GdkGC *gc = dctx->gc;
    gdk_gc_unref(gc);
    sfree(dctx);
}

/*
 * Draw a line of text in the window, at given character
 * coordinates, in given attributes.
 *
 * We are allowed to fiddle with the contents of `text'.
 */
void do_text_internal(Context ctx, int x, int y, char *text, int len,
		      unsigned long attr, int lattr)
{
    struct draw_ctx *dctx = (struct draw_ctx *)ctx;
    struct gui_data *inst = dctx->inst;
    GdkGC *gc = dctx->gc;

    int nfg, nbg, t, fontid, shadow, rlen, widefactor;

    nfg = 2 * ((attr & ATTR_FGMASK) >> ATTR_FGSHIFT);
    nbg = 2 * ((attr & ATTR_BGMASK) >> ATTR_BGSHIFT);
    if (attr & ATTR_REVERSE) {
	t = nfg;
	nfg = nbg;
	nbg = t;
    }
    if (cfg.bold_colour && (attr & ATTR_BOLD))
	nfg++;
    if (cfg.bold_colour && (attr & ATTR_BLINK))
	nbg++;
    if (attr & TATTR_ACTCURS) {
	nfg = NCOLOURS-2;
	nbg = NCOLOURS-1;
    }

    fontid = shadow = 0;

    if (attr & ATTR_WIDE) {
	widefactor = 2;
	fontid |= 2;
    } else {
	widefactor = 1;
    }

    if ((attr & ATTR_BOLD) && !cfg.bold_colour) {
	if (inst->fonts[fontid | 1])
	    fontid |= 1;
	else
	    shadow = 1;
    }

    if (lattr != LATTR_NORM) {
	x *= 2;
	if (x >= inst->term->cols)
	    return;
	if (x + len*2*widefactor > inst->term->cols)
	    len = (inst->term->cols-x)/2/widefactor;/* trim to LH half */
	rlen = len * 2;
    } else
	rlen = len;

    {
	GdkRectangle r;

	r.x = x*inst->font_width+cfg.window_border;
	r.y = y*inst->font_height+cfg.window_border;
	r.width = rlen*widefactor*inst->font_width;
	r.height = inst->font_height;
	gdk_gc_set_clip_rectangle(gc, &r);
    }

    gdk_gc_set_foreground(gc, &inst->cols[nbg]);
    gdk_draw_rectangle(inst->pixmap, gc, 1,
		       x*inst->font_width+cfg.window_border,
		       y*inst->font_height+cfg.window_border,
		       rlen*widefactor*inst->font_width, inst->font_height);

    gdk_gc_set_foreground(gc, &inst->cols[nfg]);
    {
	GdkWChar *gwcs;
	gchar *gcs;
	wchar_t *wcs;
	int i;

	wcs = smalloc(sizeof(wchar_t) * (len+1));
	for (i = 0; i < len; i++) {
	    wcs[i] = (wchar_t) ((attr & CSET_MASK) + (text[i] & CHAR_MASK));
	}

	if (inst->fonts[fontid] == NULL) {
	    /*
	     * The font for this contingency does not exist.
	     * Typically this means we've been given ATTR_WIDE
	     * character and have no wide font. So we display
	     * nothing at all; such is life.
	     */
	} else if (inst->fontinfo[fontid].is_wide) {
	    gwcs = smalloc(sizeof(GdkWChar) * (len+1));
	    /*
	     * FIXME: when we have a wide-char equivalent of
	     * from_unicode, use it instead of this.
	     */
	    for (i = 0; i <= len; i++)
		gwcs[i] = wcs[i];
	    gdk_draw_text_wc(inst->pixmap, inst->fonts[fontid], gc,
			     x*inst->font_width+cfg.window_border,
			     y*inst->font_height+cfg.window_border+inst->fonts[0]->ascent,
			     gwcs, len*2);
	    sfree(gwcs);
	} else {
	    wchar_t *wcstmp = wcs;
	    int lentmp = len;
	    gcs = smalloc(sizeof(GdkWChar) * (len+1));
	    charset_from_unicode(&wcstmp, &lentmp, gcs, len,
				 inst->fontinfo[fontid].charset,
				 NULL, ".", 1);
	    gdk_draw_text(inst->pixmap, inst->fonts[fontid], gc,
			  x*inst->font_width+cfg.window_border,
			  y*inst->font_height+cfg.window_border+inst->fonts[0]->ascent,
			  gcs, len);
	    sfree(gcs);
	}
	sfree(wcs);
    }

    if (shadow) {
	gdk_draw_text(inst->pixmap, inst->fonts[fontid], gc,
		      x*inst->font_width+cfg.window_border + cfg.shadowboldoffset,
		      y*inst->font_height+cfg.window_border+inst->fonts[0]->ascent,
		      text, len);
    }

    if (attr & ATTR_UNDER) {
	int uheight = inst->fonts[0]->ascent + 1;
	if (uheight >= inst->font_height)
	    uheight = inst->font_height - 1;
	gdk_draw_line(inst->pixmap, gc, x*inst->font_width+cfg.window_border,
		      y*inst->font_height + uheight + cfg.window_border,
		      (x+len)*widefactor*inst->font_width-1+cfg.window_border,
		      y*inst->font_height + uheight + cfg.window_border);
    }

    if (lattr != LATTR_NORM) {
	/*
	 * I can't find any plausible StretchBlt equivalent in the
	 * X server, so I'm going to do this the slow and painful
	 * way. This will involve repeated calls to
	 * gdk_draw_pixmap() to stretch the text horizontally. It's
	 * O(N^2) in time and O(N) in network bandwidth, but you
	 * try thinking of a better way. :-(
	 */
	int i;
	for (i = 0; i < len * widefactor * inst->font_width; i++) {
	    gdk_draw_pixmap(inst->pixmap, gc, inst->pixmap,
			    x*inst->font_width+cfg.window_border + 2*i,
			    y*inst->font_height+cfg.window_border,
			    x*inst->font_width+cfg.window_border + 2*i+1,
			    y*inst->font_height+cfg.window_border,
			    len * inst->font_width - i, inst->font_height);
	}
	len *= 2;
	if (lattr != LATTR_WIDE) {
	    int dt, db;
	    /* Now stretch vertically, in the same way. */
	    if (lattr == LATTR_BOT)
		dt = 0, db = 1;
	    else
		dt = 1, db = 0;
	    for (i = 0; i < inst->font_height; i+=2) {
		gdk_draw_pixmap(inst->pixmap, gc, inst->pixmap,
				x*inst->font_width+cfg.window_border,
				y*inst->font_height+cfg.window_border+dt*i+db,
				x*widefactor*inst->font_width+cfg.window_border,
				y*inst->font_height+cfg.window_border+dt*(i+1),
				len * inst->font_width, inst->font_height-i-1);
	    }
	}
    }
}

void do_text(Context ctx, int x, int y, char *text, int len,
	     unsigned long attr, int lattr)
{
    struct draw_ctx *dctx = (struct draw_ctx *)ctx;
    struct gui_data *inst = dctx->inst;
    GdkGC *gc = dctx->gc;
    int widefactor;

    do_text_internal(ctx, x, y, text, len, attr, lattr);

    if (attr & ATTR_WIDE) {
	widefactor = 2;
    } else {
	widefactor = 1;
    }

    if (lattr != LATTR_NORM) {
	x *= 2;
	if (x >= inst->term->cols)
	    return;
	if (x + len*2*widefactor > inst->term->cols)
	    len = (inst->term->cols-x)/2/widefactor;/* trim to LH half */
	len *= 2;
    }

    gdk_draw_pixmap(inst->area->window, gc, inst->pixmap,
		    x*inst->font_width+cfg.window_border,
		    y*inst->font_height+cfg.window_border,
		    x*inst->font_width+cfg.window_border,
		    y*inst->font_height+cfg.window_border,
		    len*widefactor*inst->font_width, inst->font_height);
}

void do_cursor(Context ctx, int x, int y, char *text, int len,
	       unsigned long attr, int lattr)
{
    struct draw_ctx *dctx = (struct draw_ctx *)ctx;
    struct gui_data *inst = dctx->inst;
    GdkGC *gc = dctx->gc;

    int passive, widefactor;

    if (attr & TATTR_PASCURS) {
	attr &= ~TATTR_PASCURS;
	passive = 1;
    } else
	passive = 0;
    if ((attr & TATTR_ACTCURS) && cfg.cursor_type != 0) {
	attr &= ~TATTR_ACTCURS;
    }
    do_text_internal(ctx, x, y, text, len, attr, lattr);

    if (attr & ATTR_WIDE) {
	widefactor = 2;
    } else {
	widefactor = 1;
    }

    if (lattr != LATTR_NORM) {
	x *= 2;
	if (x >= inst->term->cols)
	    return;
	if (x + len*2*widefactor > inst->term->cols)
	    len = (inst->term->cols-x)/2/widefactor;/* trim to LH half */
	len *= 2;
    }

    if (cfg.cursor_type == 0) {
	/*
	 * An active block cursor will already have been done by
	 * the above do_text call, so we only need to do anything
	 * if it's passive.
	 */
	if (passive) {
	    gdk_gc_set_foreground(gc, &inst->cols[NCOLOURS-1]);
	    gdk_draw_rectangle(inst->pixmap, gc, 0,
			       x*inst->font_width+cfg.window_border,
			       y*inst->font_height+cfg.window_border,
			       len*inst->font_width-1, inst->font_height-1);
	}
    } else {
	int uheight;
	int startx, starty, dx, dy, length, i;

	int char_width;

	if ((attr & ATTR_WIDE) || lattr != LATTR_NORM)
	    char_width = 2*inst->font_width;
	else
	    char_width = inst->font_width;

	if (cfg.cursor_type == 1) {
	    uheight = inst->fonts[0]->ascent + 1;
	    if (uheight >= inst->font_height)
		uheight = inst->font_height - 1;

	    startx = x * inst->font_width + cfg.window_border;
	    starty = y * inst->font_height + cfg.window_border + uheight;
	    dx = 1;
	    dy = 0;
	    length = len * char_width;
	} else {
	    int xadjust = 0;
	    if (attr & TATTR_RIGHTCURS)
		xadjust = char_width - 1;
	    startx = x * inst->font_width + cfg.window_border + xadjust;
	    starty = y * inst->font_height + cfg.window_border;
	    dx = 0;
	    dy = 1;
	    length = inst->font_height;
	}

	gdk_gc_set_foreground(gc, &inst->cols[NCOLOURS-1]);
	if (passive) {
	    for (i = 0; i < length; i++) {
		if (i % 2 == 0) {
		    gdk_draw_point(inst->pixmap, gc, startx, starty);
		}
		startx += dx;
		starty += dy;
	    }
	} else {
	    gdk_draw_line(inst->pixmap, gc, startx, starty,
			  startx + (length-1) * dx, starty + (length-1) * dy);
	}
    }

    gdk_draw_pixmap(inst->area->window, gc, inst->pixmap,
		    x*inst->font_width+cfg.window_border,
		    y*inst->font_height+cfg.window_border,
		    x*inst->font_width+cfg.window_border,
		    y*inst->font_height+cfg.window_border,
		    len*widefactor*inst->font_width, inst->font_height);
}

GdkCursor *make_mouse_ptr(struct gui_data *inst, int cursor_val)
{
    /*
     * Truly hideous hack: GTK doesn't allow us to set the mouse
     * cursor foreground and background colours unless we've _also_
     * created our own cursor from bitmaps. Therefore, I need to
     * load the `cursor' font and draw glyphs from it on to
     * pixmaps, in order to construct my cursors with the fg and bg
     * I want. This is a gross hack, but it's more self-contained
     * than linking in Xlib to find the X window handle to
     * inst->area and calling XRecolorCursor, and it's more
     * futureproof than hard-coding the shapes as bitmap arrays.
     */
    static GdkFont *cursor_font = NULL;
    GdkPixmap *source, *mask;
    GdkGC *gc;
    GdkColor cfg = { 0, 65535, 65535, 65535 };
    GdkColor cbg = { 0, 0, 0, 0 };
    GdkColor dfg = { 1, 65535, 65535, 65535 };
    GdkColor dbg = { 0, 0, 0, 0 };
    GdkCursor *ret;
    gchar text[2];
    gint lb, rb, wid, asc, desc, w, h, x, y;

    if (cursor_val == -2) {
	gdk_font_unref(cursor_font);
	return NULL;
    }

    if (cursor_val >= 0 && !cursor_font)
	cursor_font = gdk_font_load("cursor");

    /*
     * Get the text extent of the cursor in question. We use the
     * mask character for this, because it's typically slightly
     * bigger than the main character.
     */
    if (cursor_val >= 0) {
	text[1] = '\0';
	text[0] = (char)cursor_val + 1;
	gdk_string_extents(cursor_font, text, &lb, &rb, &wid, &asc, &desc);
	w = rb-lb; h = asc+desc; x = -lb; y = asc;
    } else {
	w = h = 1;
	x = y = 0;
    }

    source = gdk_pixmap_new(NULL, w, h, 1);
    mask = gdk_pixmap_new(NULL, w, h, 1);

    /*
     * Draw the mask character on the mask pixmap.
     */
    gc = gdk_gc_new(mask);
    gdk_gc_set_foreground(gc, &dbg);
    gdk_draw_rectangle(mask, gc, 1, 0, 0, w, h);
    if (cursor_val >= 0) {
	text[1] = '\0';
	text[0] = (char)cursor_val + 1;
	gdk_gc_set_foreground(gc, &dfg);
	gdk_draw_text(mask, cursor_font, gc, x, y, text, 1);
    }
    gdk_gc_unref(gc);

    /*
     * Draw the main character on the source pixmap.
     */
    gc = gdk_gc_new(source);
    gdk_gc_set_foreground(gc, &dbg);
    gdk_draw_rectangle(source, gc, 1, 0, 0, w, h);
    if (cursor_val >= 0) {
	text[1] = '\0';
	text[0] = (char)cursor_val;
	gdk_gc_set_foreground(gc, &dfg);
	gdk_draw_text(source, cursor_font, gc, x, y, text, 1);
    }
    gdk_gc_unref(gc);

    /*
     * Create the cursor.
     */
    ret = gdk_cursor_new_from_pixmap(source, mask, &cfg, &cbg, x, y);

    /*
     * Clean up.
     */
    gdk_pixmap_unref(source);
    gdk_pixmap_unref(mask);

    return ret;
}

void modalfatalbox(char *p, ...)
{
    va_list ap;
    fprintf(stderr, "FATAL ERROR: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

char *get_x_display(void *frontend)
{
    return gdk_get_display();
}

static void help(FILE *fp) {
    if(fprintf(fp,
"pterm option summary:\n"
"\n"
"  --display DISPLAY         Specify X display to use (note '--')\n"
"  -name PREFIX              Prefix when looking up resources (default: pterm)\n"
"  -fn FONT                  Normal text font\n"
"  -fb FONT                  Bold text font\n"
"  -geometry WIDTHxHEIGHT    Size of terminal in characters\n"
"  -sl LINES                 Number of lines of scrollback\n"
"  -fg COLOUR, -bg COLOUR    Foreground/background colour\n"
"  -bfg COLOUR, -bbg COLOUR  Foreground/background bold colour\n"
"  -cfg COLOUR, -bfg COLOUR  Foreground/background cursor colour\n"
"  -T TITLE                  Window title\n"
"  -ut, +ut                  Do(default) or do not update utmp\n"
"  -ls, +ls                  Do(default) or do not make shell a login shell\n"
"  -sb, +sb                  Do(default) or do not display a scrollbar\n"
"  -log PATH                 Log all output to a file\n"
"  -nethack                  Map numeric keypad to hjklyubn direction keys\n"
"  -xrm RESOURCE-STRING      Set an X resource\n"
"  -e COMMAND [ARGS...]      Execute command (consumes all remaining args)\n"
	 ) < 0 || fflush(fp) < 0) {
	perror("output error");
	exit(1);
    }
}

int do_cmdline(int argc, char **argv, int do_everything)
{
    int err = 0;
    extern char **pty_argv;	       /* declared in pty.c */

    /*
     * Macros to make argument handling easier. Note that because
     * they need to call `continue', they cannot be contained in
     * the usual do {...} while (0) wrapper to make them
     * syntactically single statements; hence it is not legal to
     * use one of these macros as an unbraced statement between
     * `if' and `else'.
     */
#define EXPECTS_ARG { \
    if (--argc <= 0) { \
	err = 1; \
	fprintf(stderr, "pterm: %s expects an argument\n", p); \
        continue; \
    } else \
	val = *++argv; \
}
#define SECOND_PASS_ONLY { if (!do_everything) continue; }

    /*
     * TODO:
     * 
     * finish -geometry
     */

    char *val;
    while (--argc > 0) {
	char *p = *++argv;
	if (!strcmp(p, "-fn") || !strcmp(p, "-font")) {
	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;
	    strncpy(cfg.font, val, sizeof(cfg.font));
	    cfg.font[sizeof(cfg.font)-1] = '\0';

	} else if (!strcmp(p, "-fb")) {
	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;
	    strncpy(cfg.boldfont, val, sizeof(cfg.boldfont));
	    cfg.boldfont[sizeof(cfg.boldfont)-1] = '\0';

	} else if (!strcmp(p, "-fw")) {
	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;
	    strncpy(cfg.widefont, val, sizeof(cfg.widefont));
	    cfg.widefont[sizeof(cfg.widefont)-1] = '\0';

	} else if (!strcmp(p, "-fwb")) {
	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;
	    strncpy(cfg.wideboldfont, val, sizeof(cfg.wideboldfont));
	    cfg.wideboldfont[sizeof(cfg.wideboldfont)-1] = '\0';

	} else if (!strcmp(p, "-cs")) {
	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;
	    strncpy(cfg.line_codepage, val, sizeof(cfg.line_codepage));
	    cfg.line_codepage[sizeof(cfg.line_codepage)-1] = '\0';

	} else if (!strcmp(p, "-geometry")) {
	    int flags, x, y, w, h;
	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;

	    flags = XParseGeometry(val, &x, &y, &w, &h);
	    if (flags & WidthValue)
		cfg.width = w;
	    if (flags & HeightValue)
		cfg.height = h;

	    /*
	     * Apparently setting the initial window position is
	     * difficult in GTK 1.2. Not entirely sure why this
	     * should be. 2.0 has gtk_window_parse_geometry(),
	     * which would help... For the moment, though, I can't
	     * be bothered with this.
	     */

	} else if (!strcmp(p, "-sl")) {
	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;
	    cfg.savelines = atoi(val);

	} else if (!strcmp(p, "-fg") || !strcmp(p, "-bg") ||
		   !strcmp(p, "-bfg") || !strcmp(p, "-bbg") ||
		   !strcmp(p, "-cfg") || !strcmp(p, "-cbg")) {
	    GdkColor col;

	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;
	    if (!gdk_color_parse(val, &col)) {
		err = 1;
		fprintf(stderr, "pterm: unable to parse colour \"%s\"\n", val);
	    } else {
		int index;
		index = (!strcmp(p, "-fg") ? 0 :
			 !strcmp(p, "-bg") ? 2 :
			 !strcmp(p, "-bfg") ? 1 :
			 !strcmp(p, "-bbg") ? 3 :
			 !strcmp(p, "-cfg") ? 4 :
			 !strcmp(p, "-cbg") ? 5 : -1);
		assert(index != -1);
		cfg.colours[index][0] = col.red / 256;
		cfg.colours[index][1] = col.green / 256;
		cfg.colours[index][2] = col.blue / 256;
	    }

	} else if (!strcmp(p, "-e")) {
	    /* This option swallows all further arguments. */
	    if (!do_everything)
		break;

	    if (--argc > 0) {
		int i;
		pty_argv = smalloc((argc+1) * sizeof(char *));
		++argv;
		for (i = 0; i < argc; i++)
		    pty_argv[i] = argv[i];
		pty_argv[argc] = NULL;
		break;		       /* finished command-line processing */
	    } else
		err = 1, fprintf(stderr, "pterm: -e expects an argument\n");

	} else if (!strcmp(p, "-T")) {
	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;
	    strncpy(cfg.wintitle, val, sizeof(cfg.wintitle));
	    cfg.wintitle[sizeof(cfg.wintitle)-1] = '\0';

	} else if (!strcmp(p, "-log")) {
	    EXPECTS_ARG;
	    SECOND_PASS_ONLY;
	    strncpy(cfg.logfilename, val, sizeof(cfg.logfilename));
	    cfg.logfilename[sizeof(cfg.logfilename)-1] = '\0';
	    cfg.logtype = LGTYP_DEBUG;

	} else if (!strcmp(p, "-ut-") || !strcmp(p, "+ut")) {
	    SECOND_PASS_ONLY;
	    cfg.stamp_utmp = 0;

	} else if (!strcmp(p, "-ut")) {
	    SECOND_PASS_ONLY;
	    cfg.stamp_utmp = 1;

	} else if (!strcmp(p, "-ls-") || !strcmp(p, "+ls")) {
	    SECOND_PASS_ONLY;
	    cfg.login_shell = 0;

	} else if (!strcmp(p, "-ls")) {
	    SECOND_PASS_ONLY;
	    cfg.login_shell = 1;

	} else if (!strcmp(p, "-nethack")) {
	    SECOND_PASS_ONLY;
	    cfg.nethack_keypad = 1;

	} else if (!strcmp(p, "-sb-") || !strcmp(p, "+sb")) {
	    SECOND_PASS_ONLY;
	    cfg.scrollbar = 0;

	} else if (!strcmp(p, "-sb")) {
	    SECOND_PASS_ONLY;
	    cfg.scrollbar = 0;

	} else if (!strcmp(p, "-name")) {
	    EXPECTS_ARG;
	    app_name = val;

	} else if (!strcmp(p, "-xrm")) {
	    EXPECTS_ARG;
	    provide_xrm_string(val);

	} else if(!strcmp(p, "-help") || !strcmp(p, "--help")) {
	    help(stdout);
	    exit(0);
	    
	} else {
	    err = 1;
	    fprintf(stderr, "pterm: unrecognized option '%s'\n", p);
	}
    }

    return err;
}

static void block_signal(int sig, int block_it) {
  sigset_t ss;

  sigemptyset(&ss);
  sigaddset(&ss, sig);
  if(sigprocmask(block_it ? SIG_BLOCK : SIG_UNBLOCK, &ss, 0) < 0) {
    perror("sigprocmask");
    exit(1);
  }
}

static void set_font_info(struct gui_data *inst, int fontid)
{
    GdkFont *font = inst->fonts[fontid];
    XFontStruct *xfs = GDK_FONT_XFONT(font);
    Display *disp = GDK_FONT_XDISPLAY(font);
    Atom charset_registry, charset_encoding;
    unsigned long registry_ret, encoding_ret;
    charset_registry = XInternAtom(disp, "CHARSET_REGISTRY", False);
    charset_encoding = XInternAtom(disp, "CHARSET_ENCODING", False);
    inst->fontinfo[fontid].charset = CS_NONE;
    inst->fontinfo[fontid].is_wide = 0;
    if (XGetFontProperty(xfs, charset_registry, &registry_ret) &&
	XGetFontProperty(xfs, charset_encoding, &encoding_ret)) {
	char *reg, *enc;
	reg = XGetAtomName(disp, (Atom)registry_ret);
	enc = XGetAtomName(disp, (Atom)encoding_ret);
	if (reg && enc) {
	    char *encoding = dupcat(reg, "-", enc, NULL);
	    inst->fontinfo[fontid].charset = charset_from_xenc(encoding);
	    /* FIXME: when libcharset supports wide encodings fix this. */
	    if (!strcasecmp(encoding, "iso10646-1"))
		inst->fontinfo[fontid].is_wide = 1;

	    /*
	     * Hack for X line-drawing characters: if the primary
	     * font is encoded as ISO-8859-anything, and has valid
	     * glyphs in the first 32 char positions, it is assumed
	     * that those glyphs are the VT100 line-drawing
	     * character set.
	     * 
	     * Actually, we'll hack even harder by only checking
	     * position 0x19 (vertical line, VT100 linedrawing
	     * `x'). Then we can check it easily by seeing if the
	     * ascent and descent differ.
	     */
	    if (inst->fontinfo[fontid].charset == CS_ISO8859_1) {
		int lb, rb, wid, asc, desc;
		gchar text[2];

		text[1] = '\0';
		text[0] = '\x12';
		gdk_string_extents(inst->fonts[fontid], text,
				   &lb, &rb, &wid, &asc, &desc);
		if (asc != desc)
		    inst->fontinfo[fontid].charset = CS_ISO8859_1_X11;
	    }

	    /*
	     * FIXME: this is a hack. Currently fonts with
	     * incomprehensible encodings are dealt with by
	     * pretending they're 8859-1. It's ugly, but it's good
	     * enough to stop things crashing. Should do something
	     * better here.
	     */
	    if (inst->fontinfo[fontid].charset == CS_NONE)
		inst->fontinfo[fontid].charset = CS_ISO8859_1;

	    sfree(encoding);
	}
    }
}

int main(int argc, char **argv)
{
    extern int pty_master_fd;	       /* declared in pty.c */
    extern void pty_pre_init(void);    /* declared in pty.c */
    struct gui_data *inst;

    /* defer any child exit handling until we're ready to deal with
     * it */
    block_signal(SIGCHLD, 1);

    pty_pre_init();

    gtk_init(&argc, &argv);

    if (do_cmdline(argc, argv, 0))     /* pre-defaults pass to get -class */
	exit(1);
    do_defaults(NULL, &cfg);
    if (do_cmdline(argc, argv, 1))     /* post-defaults, do everything */
	exit(1);

    /*
     * Create an instance structure and initialise to zeroes
     */
    inst = smalloc(sizeof(*inst));
    memset(inst, 0, sizeof(*inst));
    inst->alt_keycode = -1;            /* this one needs _not_ to be zero */

    inst->fonts[0] = gdk_font_load(cfg.font);
    if (!inst->fonts[0]) {
	fprintf(stderr, "pterm: unable to load font \"%s\"\n", cfg.font);
	exit(1);
    }
    set_font_info(inst, 0);
    if (cfg.boldfont[0]) {
	inst->fonts[1] = gdk_font_load(cfg.boldfont);
	if (!inst->fonts[1]) {
	    fprintf(stderr, "pterm: unable to load bold font \"%s\"\n",
		    cfg.boldfont);
	    exit(1);
	}
	set_font_info(inst, 1);
    } else
	inst->fonts[1] = NULL;
    if (cfg.widefont[0]) {
	inst->fonts[2] = gdk_font_load(cfg.widefont);
	if (!inst->fonts[2]) {
	    fprintf(stderr, "pterm: unable to load wide font \"%s\"\n",
		    cfg.boldfont);
	    exit(1);
	}
	set_font_info(inst, 2);
    } else
	inst->fonts[2] = NULL;
    if (cfg.wideboldfont[0]) {
	inst->fonts[3] = gdk_font_load(cfg.wideboldfont);
	if (!inst->fonts[3]) {
	    fprintf(stderr, "pterm: unable to load wide/bold font \"%s\"\n",
		    cfg.boldfont);
	    exit(1);
	}
	set_font_info(inst, 3);
    } else
	inst->fonts[3] = NULL;

    inst->font_width = gdk_char_width(inst->fonts[0], ' ');
    inst->font_height = inst->fonts[0]->ascent + inst->fonts[0]->descent;

    inst->compound_text_atom = gdk_atom_intern("COMPOUND_TEXT", FALSE);
    inst->utf8_string_atom = gdk_atom_intern("UTF8_STRING", FALSE);

    init_ucs();

    inst->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    if (cfg.wintitle[0])
	set_title(inst, cfg.wintitle);
    else
	set_title(inst, "pterm");

    /*
     * Set up the colour map.
     */
    palette_reset(inst);

    inst->area = gtk_drawing_area_new();
    gtk_drawing_area_size(GTK_DRAWING_AREA(inst->area),
			  inst->font_width * cfg.width + 2*cfg.window_border,
			  inst->font_height * cfg.height + 2*cfg.window_border);
    if (cfg.scrollbar) {
	inst->sbar_adjust = GTK_ADJUSTMENT(gtk_adjustment_new(0,0,0,0,0,0));
	inst->sbar = gtk_vscrollbar_new(inst->sbar_adjust);
    }
    inst->hbox = GTK_BOX(gtk_hbox_new(FALSE, 0));
    if (cfg.scrollbar) {
	if (cfg.scrollbar_on_left)
	    gtk_box_pack_start(inst->hbox, inst->sbar, FALSE, FALSE, 0);
	else
	    gtk_box_pack_end(inst->hbox, inst->sbar, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(inst->hbox, inst->area, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(inst->window), GTK_WIDGET(inst->hbox));

    {
	GdkGeometry geom;
	geom.min_width = inst->font_width + 2*cfg.window_border;
	geom.min_height = inst->font_height + 2*cfg.window_border;
	geom.max_width = geom.max_height = -1;
	geom.base_width = 2*cfg.window_border;
	geom.base_height = 2*cfg.window_border;
	geom.width_inc = inst->font_width;
	geom.height_inc = inst->font_height;
	geom.min_aspect = geom.max_aspect = 0;
	gtk_window_set_geometry_hints(GTK_WINDOW(inst->window), inst->area, &geom,
				      GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE |
				      GDK_HINT_RESIZE_INC);
    }

    gtk_signal_connect(GTK_OBJECT(inst->window), "destroy",
		       GTK_SIGNAL_FUNC(destroy), inst);
    gtk_signal_connect(GTK_OBJECT(inst->window), "delete_event",
		       GTK_SIGNAL_FUNC(delete_window), inst);
    gtk_signal_connect(GTK_OBJECT(inst->window), "key_press_event",
		       GTK_SIGNAL_FUNC(key_event), inst);
    gtk_signal_connect(GTK_OBJECT(inst->window), "key_release_event",
		       GTK_SIGNAL_FUNC(key_event), inst);
    gtk_signal_connect(GTK_OBJECT(inst->window), "focus_in_event",
		       GTK_SIGNAL_FUNC(focus_event), inst);
    gtk_signal_connect(GTK_OBJECT(inst->window), "focus_out_event",
		       GTK_SIGNAL_FUNC(focus_event), inst);
    gtk_signal_connect(GTK_OBJECT(inst->area), "configure_event",
		       GTK_SIGNAL_FUNC(configure_area), inst);
    gtk_signal_connect(GTK_OBJECT(inst->area), "expose_event",
		       GTK_SIGNAL_FUNC(expose_area), inst);
    gtk_signal_connect(GTK_OBJECT(inst->area), "button_press_event",
		       GTK_SIGNAL_FUNC(button_event), inst);
    gtk_signal_connect(GTK_OBJECT(inst->area), "button_release_event",
		       GTK_SIGNAL_FUNC(button_event), inst);
    gtk_signal_connect(GTK_OBJECT(inst->area), "motion_notify_event",
		       GTK_SIGNAL_FUNC(motion_event), inst);
    gtk_signal_connect(GTK_OBJECT(inst->area), "selection_received",
		       GTK_SIGNAL_FUNC(selection_received), inst);
    gtk_signal_connect(GTK_OBJECT(inst->area), "selection_get",
		       GTK_SIGNAL_FUNC(selection_get), inst);
    gtk_signal_connect(GTK_OBJECT(inst->area), "selection_clear_event",
		       GTK_SIGNAL_FUNC(selection_clear), inst);
    if (cfg.scrollbar)
	gtk_signal_connect(GTK_OBJECT(inst->sbar_adjust), "value_changed",
			   GTK_SIGNAL_FUNC(scrollbar_moved), inst);
    gtk_timeout_add(20, timer_func, inst);
    gtk_widget_add_events(GTK_WIDGET(inst->area),
			  GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK |
			  GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
			  GDK_POINTER_MOTION_MASK | GDK_BUTTON_MOTION_MASK);

    gtk_widget_show(inst->area);
    if (cfg.scrollbar)
	gtk_widget_show(inst->sbar);
    gtk_widget_show(GTK_WIDGET(inst->hbox));
    gtk_widget_show(inst->window);

    set_window_background(inst);

    inst->textcursor = make_mouse_ptr(inst, GDK_XTERM);
    inst->rawcursor = make_mouse_ptr(inst, GDK_LEFT_PTR);
    inst->blankcursor = make_mouse_ptr(inst, -1);
    make_mouse_ptr(inst, -2);	       /* clean up cursor font */
    inst->currcursor = inst->textcursor;
    show_mouseptr(inst, 1);

    inst->term = term_init(&cfg, inst);
    inst->logctx = log_init(inst);
    term_provide_logctx(inst->term, inst->logctx);

    inst->back = &pty_backend;
    inst->back->init((void *)inst->term, &inst->backhandle, NULL, 0, NULL, 0);
    inst->back->provide_logctx(inst->backhandle, inst->logctx);

    term_provide_resize_fn(inst->term, inst->back->size, inst->backhandle);

    term_size(inst->term, cfg.height, cfg.width, cfg.savelines);

    inst->ldisc =
	ldisc_create(&cfg, inst->term, inst->back, inst->backhandle, inst);
    ldisc_send(inst->ldisc, NULL, 0, 0);/* cause ldisc to notice changes */

    inst->master_fd = pty_master_fd;
    inst->exited = FALSE;
    inst->master_func_id = gdk_input_add(pty_master_fd, GDK_INPUT_READ,
					 pty_input_func, inst);

    /* now we're reday to deal with the child exit handler being
     * called */
    block_signal(SIGCHLD, 0);
    
    gtk_main();

    return 0;
}
