/*
 * pterm - a fusion of the PuTTY terminal emulator with a Unix pty
 * back end, all running as a GTK application. Wish me luck.
 */

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#define PUTTY_DO_GLOBALS	       /* actually _define_ globals */
#include "putty.h"

#define CAT2(x,y) x ## y
#define CAT(x,y) CAT2(x,y)
#define ASSERT(x) enum {CAT(assertion_,__LINE__) = 1 / (x)}

#define NCOLOURS (lenof(((Config *)0)->colours))

struct gui_data {
    GtkWidget *window, *area, *sbar;
    GtkBox *hbox;
    GtkAdjustment *sbar_adjust;
    GdkPixmap *pixmap;
    GdkFont *fonts[2];                 /* normal and bold (for now!) */
    GdkCursor *rawcursor, *textcursor, *blankcursor, *currcursor;
    GdkColor cols[NCOLOURS];
    GdkColormap *colmap;
    wchar_t *pastein_data;
    int pastein_data_len;
    char *pasteout_data;
    int pasteout_data_len;
    int font_width, font_height;
    int ignore_sbar;
    int mouseptr_visible;
    guint term_paste_idle_id;
    GdkAtom compound_text_atom;
    char wintitle[sizeof(((Config *)0)->wintitle)];
    char icontitle[sizeof(((Config *)0)->wintitle)];
};

static struct gui_data the_inst;
static struct gui_data *inst = &the_inst;   /* so we always write `inst->' */
static int send_raw_mouse;

void ldisc_update(int echo, int edit)
{
    /*
     * This is a stub in pterm. If I ever produce a Unix
     * command-line ssh/telnet/rlogin client (i.e. a port of plink)
     * then it will require some termios manoeuvring analogous to
     * that in the Windows plink.c, but here it's meaningless.
     */
}

int askappend(char *filename)
{
    /*
     * Logging in an xterm-alike is liable to be something you only
     * do at serious diagnostic need. Hence, I'm going to take the
     * easy option for now and assume we always want to overwrite
     * log files. I can always make it properly configurable later.
     */
    return 2;
}

void logevent(char *string)
{
    /*
     * This is not a very helpful function: events are logged
     * pretty much exclusively by the back end, and our pty back
     * end is self-contained. So we need do nothing.
     */
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
Mouse_Button translate_button(Mouse_Button button)
{
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
void set_iconic(int iconic)
{
    /*
     * GTK 1.2 doesn't know how to do this.
     */
#if GTK_CHECK_VERSION(2,0,0)
    if (iconic)
	gtk_window_iconify(GTK_WINDOW(inst->window));
    else
	gtk_window_deiconify(GTK_WINDOW(inst->window));
#endif
}

/*
 * Move the window in response to a server-side request.
 */
void move_window(int x, int y)
{
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
void set_zorder(int top)
{
    if (top)
	gdk_window_raise(inst->window->window);
    else
	gdk_window_lower(inst->window->window);
}

/*
 * Refresh the window in response to a server-side request.
 */
void refresh_window(void)
{
    term_invalidate();
}

/*
 * Maximise or restore the window in response to a server-side
 * request.
 */
void set_zoomed(int zoomed)
{
    /*
     * GTK 1.2 doesn't know how to do this.
     */
#if GTK_CHECK_VERSION(2,0,0)
    if (iconic)
	gtk_window_maximize(GTK_WINDOW(inst->window));
    else
	gtk_window_unmaximize(GTK_WINDOW(inst->window));
#endif
}

/*
 * Report whether the window is iconic, for terminal reports.
 */
int is_iconic(void)
{
    return !gdk_window_is_viewable(inst->window->window);
}

/*
 * Report the window's position, for terminal reports.
 */
void get_window_pos(int *x, int *y)
{
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
void get_window_pixels(int *x, int *y)
{
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
char *get_window_title(int icon)
{
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

void show_mouseptr(int show)
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

printf("configure %d x %d\n", event->width, event->height);
    w = (event->width - 2*cfg.window_border) / inst->font_width;
    h = (event->height - 2*cfg.window_border) / inst->font_height;
printf("        = %d x %d\n", w, h);

    if (w != cfg.width || h != cfg.height) {
	if (inst->pixmap) {
	    gdk_pixmap_unref(inst->pixmap);
	    inst->pixmap = NULL;
	}
	cfg.width = w;
	cfg.height = h;
	need_size = 1;
printf("need size\n");
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
	term_size(h, w, cfg.savelines);
    }

    return TRUE;
}

gint expose_area(GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
    /* struct gui_data *inst = (struct gui_data *)data; */

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
    /* struct gui_data *inst = (struct gui_data *)data; */
    char output[32];
    int start, end;

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
	 * NYI:
	 *  - alt+numpad
	 *  - Compose key (!!! requires Unicode faff before even trying)
	 */

	/*
	 * Shift-PgUp and Shift-PgDn don't even generate keystrokes
	 * at all.
	 */
	if (event->keyval == GDK_Page_Up && (event->state & GDK_SHIFT_MASK)) {
	    term_scroll(0, -cfg.height/2);
	    return TRUE;
	}
	if (event->keyval == GDK_Page_Down && (event->state & GDK_SHIFT_MASK)) {
	    term_scroll(0, +cfg.height/2);
	    return TRUE;
	}

	/*
	 * Neither does Shift-Ins.
	 */
	if (event->keyval == GDK_Insert && (event->state & GDK_SHIFT_MASK)) {
	    request_paste();
	    return TRUE;
	}

	/* ALT+things gives leading Escape. */
	output[0] = '\033';
	strncpy(output+1, event->string, 31);
	output[31] = '\0';
	end = strlen(output);
	if (event->state & GDK_MOD1_MASK)
	    start = 0;
	else
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
	if (app_keypad_keys && !cfg.no_applic_k) {
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
		if (vt52_mode) {
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

	    if (vt52_mode && code > 0 && code <= 6) {
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
	    if ((vt52_mode || cfg.funky_type == 4) && code >= 11 && code <= 24) {
		int offt = 0;
		if (code > 15)
		    offt++;
		if (code > 21)
		    offt++;
		if (vt52_mode)
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
		if (vt52_mode)
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
		if (vt52_mode) {
		    end = 1 + sprintf(output+1, "\033%c", xkey);
		} else if (!app_cursor_keys ^
			   !(event->state & GDK_CONTROL_MASK)) {
		    end = 1 + sprintf(output+1, "\033O%c", xkey);
		} else {		    
		    end = 1 + sprintf(output+1, "\033[%c", xkey);
		}
		goto done;
	    }
	}

	done:

#ifdef KEY_DEBUGGING
	{
	    int i;
	    printf("generating sequence:");
	    for (i = start; i < end; i++)
		printf(" %02x", (unsigned char) output[i]);
	    printf("\n");
	}
#endif
	if (end-start > 0) {
	    ldisc_send(output+start, end-start, 1);
	    show_mouseptr(0);
	    term_out();
	}
    }

    return TRUE;
}

gint button_event(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    int shift, ctrl, alt, x, y, button, act;

    show_mouseptr(1);

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

    term_mouse(button, act, x, y, shift, ctrl, alt);

    return TRUE;
}

gint motion_event(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;
    int shift, ctrl, alt, x, y, button;

    show_mouseptr(1);

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

    term_mouse(button, MA_DRAG, x, y, shift, ctrl, alt);

    return TRUE;
}

gint timer_func(gpointer data)
{
    /* struct gui_data *inst = (struct gui_data *)data; */
    extern int pty_child_is_dead();  /* declared in pty.c */

    if (pty_child_is_dead()) {
	/*
	 * The primary child process died. We could keep the
	 * terminal open for remaining subprocesses to output to,
	 * but conventional wisdom seems to feel that that's the
	 * Wrong Thing for an xterm-alike, so we bail out now. This
	 * would be easy enough to change or make configurable if
	 * necessary.
	 */
	exit(0);
    }

    term_update();
    return TRUE;
}

void pty_input_func(gpointer data, gint sourcefd, GdkInputCondition condition)
{
    /* struct gui_data *inst = (struct gui_data *)data; */
    char buf[4096];
    int ret;

    ret = read(sourcefd, buf, sizeof(buf));

    /*
     * Clean termination condition is that either ret == 0, or ret
     * < 0 and errno == EIO. Not sure why the latter, but it seems
     * to happen. Boo.
     */
    if (ret == 0 || (ret < 0 && errno == EIO)) {
	exit(0);
    }

    if (ret < 0) {
	perror("read pty master");
	exit(1);
    }
    if (ret > 0)
	from_backend(0, buf, ret);
    term_out();
}

void destroy(GtkWidget *widget, gpointer data)
{
    gtk_main_quit();
}

gint focus_event(GtkWidget *widget, GdkEventFocus *event, gpointer data)
{
    has_focus = event->in;
    term_out();
    term_update();
    show_mouseptr(1);
    return FALSE;
}

/*
 * set or clear the "raw mouse message" mode
 */
void set_raw_mouse_mode(int activate)
{
    activate = activate && !cfg.no_mouse_rep;
    send_raw_mouse = activate;
    if (send_raw_mouse)
	inst->currcursor = inst->rawcursor;
    else
	inst->currcursor = inst->textcursor;
    show_mouseptr(inst->mouseptr_visible);
}

void request_resize(int w, int h)
{
    gtk_drawing_area_size(GTK_DRAWING_AREA(inst->area),
			  inst->font_width * w + 2*cfg.window_border,
			  inst->font_height * h + 2*cfg.window_border);
}

void real_palette_set(int n, int r, int g, int b)
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

void palette_set(int n, int r, int g, int b)
{
    static const int first[21] = {
	0, 2, 4, 6, 8, 10, 12, 14,
	1, 3, 5, 7, 9, 11, 13, 15,
	16, 17, 18, 20, 22
    };
    real_palette_set(first[n], r, g, b);
    if (first[n] >= 18)
	real_palette_set(first[n] + 1, r, g, b);
}

void palette_reset(void)
{
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
}

void write_clip(wchar_t * data, int len, int must_deselect)
{
    if (inst->pasteout_data)
	sfree(inst->pasteout_data);
    inst->pasteout_data = smalloc(len);
    inst->pasteout_data_len = len;
    wc_to_mb(0, 0, data, len, inst->pasteout_data, inst->pasteout_data_len,
	     NULL, NULL);

    if (gtk_selection_owner_set(inst->area, GDK_SELECTION_PRIMARY,
				GDK_CURRENT_TIME)) {
	gtk_selection_add_target(inst->area, GDK_SELECTION_PRIMARY,
				 GDK_SELECTION_TYPE_STRING, 1);
	gtk_selection_add_target(inst->area, GDK_SELECTION_PRIMARY,
				 inst->compound_text_atom, 1);
    }
}

void selection_get(GtkWidget *widget, GtkSelectionData *seldata,
		   guint info, guint time_stamp, gpointer data)
{
    gtk_selection_data_set(seldata, GDK_SELECTION_TYPE_STRING, 8,
			   inst->pasteout_data, inst->pasteout_data_len);
}

gint selection_clear(GtkWidget *widget, GdkEventSelection *seldata,
		     gpointer data)
{
    term_deselect();
    if (inst->pasteout_data)
	sfree(inst->pasteout_data);
    inst->pasteout_data = NULL;
    inst->pasteout_data_len = 0;
    return TRUE;
}

void request_paste(void)
{
    /*
     * In Unix, pasting is asynchronous: all we can do at the
     * moment is to call gtk_selection_convert(), and when the data
     * comes back _then_ we can call term_do_paste().
     */
    gtk_selection_convert(inst->area, GDK_SELECTION_PRIMARY,
			  GDK_SELECTION_TYPE_STRING, GDK_CURRENT_TIME);
}

gint idle_paste_func(gpointer data);   /* forward ref */

void selection_received(GtkWidget *widget, GtkSelectionData *seldata,
			gpointer data)
{
    if (seldata->length <= 0 ||
	seldata->type != GDK_SELECTION_TYPE_STRING)
	return;			       /* Nothing happens. */

    if (inst->pastein_data)
	sfree(inst->pastein_data);

    inst->pastein_data = smalloc(seldata->length * sizeof(wchar_t));
    inst->pastein_data_len = seldata->length;
    mb_to_wc(0, 0, seldata->data, seldata->length,
	     inst->pastein_data, inst->pastein_data_len);

    term_do_paste();

    if (term_paste_pending())
	inst->term_paste_idle_id = gtk_idle_add(idle_paste_func, inst);
}

gint idle_paste_func(gpointer data)
{
    struct gui_data *inst = (struct gui_data *)data;

    if (term_paste_pending())
	term_paste();
    else
	gtk_idle_remove(inst->term_paste_idle_id);

    return TRUE;
}


void get_clip(wchar_t ** p, int *len)
{
    if (p) {
	*p = inst->pastein_data;
	*len = inst->pastein_data_len;
    }
}

void set_title(char *title)
{
    strncpy(inst->wintitle, title, lenof(inst->wintitle));
    inst->wintitle[lenof(inst->wintitle)-1] = '\0';
    gtk_window_set_title(GTK_WINDOW(inst->window), inst->wintitle);
}

void set_icon(char *title)
{
    strncpy(inst->icontitle, title, lenof(inst->icontitle));
    inst->icontitle[lenof(inst->icontitle)-1] = '\0';
    gdk_window_set_icon_name(inst->window->window, inst->icontitle);
}

void set_sbar(int total, int start, int page)
{
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
    if (!cfg.scrollbar)
	return;
    if (!inst->ignore_sbar)
	term_scroll(1, (int)adj->value);
}

void sys_cursor(int x, int y)
{
    /*
     * This is meaningless under X.
     */
}

void beep(int mode)
{
    gdk_beep();
}

int CharWidth(Context ctx, int uc)
{
    /*
     * Under X, any fixed-width font really _is_ fixed-width.
     * Double-width characters will be dealt with using a separate
     * font. For the moment we can simply return 1.
     */
    return 1;
}

Context get_ctx(void)
{
    GdkGC *gc;
    if (!inst->area->window)
	return NULL;
    gc = gdk_gc_new(inst->area->window);
    return gc;
}

void free_ctx(Context ctx)
{
    GdkGC *gc = (GdkGC *)ctx;
    gdk_gc_unref(gc);
}

/*
 * Draw a line of text in the window, at given character
 * coordinates, in given attributes.
 *
 * We are allowed to fiddle with the contents of `text'.
 */
void do_text(Context ctx, int x, int y, char *text, int len,
	     unsigned long attr, int lattr)
{
    int nfg, nbg, t;
    GdkGC *gc = (GdkGC *)ctx;

    /*
     * NYI:
     *  - Unicode, code pages, and ATTR_WIDE for CJK support.
     *  - cursor shapes other than block
     *  - shadow bolding
     */

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

    if (lattr != LATTR_NORM) {
	x *= 2;
	if (x >= cols)
	    return;
	if (x + len*2 > cols)
	    len = (cols-x)/2;	       /* trim to LH half */
    }

    gdk_gc_set_foreground(gc, &inst->cols[nbg]);
    gdk_draw_rectangle(inst->pixmap, gc, 1,
		       x*inst->font_width+cfg.window_border,
		       y*inst->font_height+cfg.window_border,
		       len*inst->font_width, inst->font_height);

    gdk_gc_set_foreground(gc, &inst->cols[nfg]);
    gdk_draw_text(inst->pixmap, inst->fonts[0], gc,
		  x*inst->font_width+cfg.window_border,
		  y*inst->font_height+cfg.window_border+inst->fonts[0]->ascent,
		  text, len);

    if (attr & ATTR_UNDER) {
	int uheight = inst->fonts[0]->ascent + 1;
	if (uheight >= inst->font_height)
	    uheight = inst->font_height - 1;
	gdk_draw_line(inst->pixmap, gc, x*inst->font_width+cfg.window_border,
		      y*inst->font_height + uheight + cfg.window_border,
		      (x+len)*inst->font_width-1+cfg.window_border,
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
	for (i = 0; i < len * inst->font_width; i++) {
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
				x*inst->font_width+cfg.window_border,
				y*inst->font_height+cfg.window_border+dt*(i+1),
				len * inst->font_width, inst->font_height-i-1);
	    }
	}
	len *= 2;
    }

    gdk_draw_pixmap(inst->area->window, gc, inst->pixmap,
		    x*inst->font_width+cfg.window_border,
		    y*inst->font_height+cfg.window_border,
		    x*inst->font_width+cfg.window_border,
		    y*inst->font_height+cfg.window_border,
		    len*inst->font_width, inst->font_height);
}

void do_cursor(Context ctx, int x, int y, char *text, int len,
	       unsigned long attr, int lattr)
{
    int passive;
    GdkGC *gc = (GdkGC *)ctx;

    /*
     * NYI: cursor shapes other than block
     */
    if (attr & TATTR_PASCURS) {
	attr &= ~TATTR_PASCURS;
	passive = 1;
    } else
	passive = 0;
    do_text(ctx, x, y, text, len, attr, lattr);
    if (passive) {
	gdk_gc_set_foreground(gc, &inst->cols[NCOLOURS-1]);
	gdk_draw_rectangle(inst->pixmap, gc, 0,
			   x*inst->font_width+cfg.window_border,
			   y*inst->font_height+cfg.window_border,
			   len*inst->font_width-1, inst->font_height-1);
	gdk_draw_pixmap(inst->area->window, gc, inst->pixmap,
			x*inst->font_width+cfg.window_border,
			y*inst->font_height+cfg.window_border,
			x*inst->font_width+cfg.window_border,
			y*inst->font_height+cfg.window_border,
			len*inst->font_width, inst->font_height);
    }
}

GdkCursor *make_mouse_ptr(int cursor_val)
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

char *get_x_display(void)
{
    return gdk_get_display();
}

int main(int argc, char **argv)
{
    extern int pty_master_fd;	       /* declared in pty.c */
    extern char **pty_argv;	       /* declared in pty.c */
    int err = 0;

    gtk_init(&argc, &argv);

    do_defaults(NULL, &cfg);

    while (--argc > 0) {
	char *p = *++argv;
	if (!strcmp(p, "-fn")) {
	    if (--argc > 0) {
		strncpy(cfg.font, *++argv, sizeof(cfg.font));
		cfg.font[sizeof(cfg.font)-1] = '\0';
	    } else
		err = 1, fprintf(stderr, "pterm: -fn expects an argument\n");
	}
	if (!strcmp(p, "-e")) {
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
	}
	if (!strcmp(p, "-T")) {
	    if (--argc > 0) {
		strncpy(cfg.wintitle, *++argv, sizeof(cfg.wintitle));
		cfg.wintitle[sizeof(cfg.wintitle)-1] = '\0';
	    } else
		err = 1, fprintf(stderr, "pterm: -T expects an argument\n");
	}
	if (!strcmp(p, "-log")) {
	    if (--argc > 0) {
		strncpy(cfg.logfilename, *++argv, sizeof(cfg.logfilename));
		cfg.logfilename[sizeof(cfg.logfilename)-1] = '\0';
		cfg.logtype = LGTYP_DEBUG;
	    } else
		err = 1, fprintf(stderr, "pterm: -log expects an argument\n");
	}
	if (!strcmp(p, "-hide")) {
	    cfg.hide_mouseptr = 1;
	}
	if (!strcmp(p, "-ut-")) {
	    cfg.stamp_utmp = 0;
	}
	if (!strcmp(p, "-ls-")) {
	    cfg.login_shell = 0;
	}
	if (!strcmp(p, "-nethack")) {
	    cfg.nethack_keypad = 1;
	}
	if (!strcmp(p, "-sb-")) {
	    cfg.scrollbar = 0;
	}
    }

    inst->fonts[0] = gdk_font_load(cfg.font);
    inst->fonts[1] = NULL;             /* FIXME: what about bold font? */
    inst->font_width = gdk_char_width(inst->fonts[0], ' ');
    inst->font_height = inst->fonts[0]->ascent + inst->fonts[0]->descent;

    inst->compound_text_atom = gdk_atom_intern("COMPOUND_TEXT", FALSE);

    init_ucs();

    inst->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    if (cfg.wintitle[0])
	set_title(cfg.wintitle);
    else
	set_title("pterm");

    /*
     * Set up the colour map.
     */
    palette_reset();

    inst->area = gtk_drawing_area_new();
    gtk_drawing_area_size(GTK_DRAWING_AREA(inst->area),
			  inst->font_width * cfg.width + 2*cfg.window_border,
			  inst->font_height * cfg.height + 2*cfg.window_border);
    if (cfg.scrollbar) {
	inst->sbar_adjust = GTK_ADJUSTMENT(gtk_adjustment_new(0,0,0,0,0,0));
	inst->sbar = gtk_vscrollbar_new(inst->sbar_adjust);
    }
    inst->hbox = GTK_BOX(gtk_hbox_new(FALSE, 0));
    gtk_box_pack_start(inst->hbox, inst->area, TRUE, TRUE, 0);
    if (cfg.scrollbar)
	gtk_box_pack_start(inst->hbox, inst->sbar, FALSE, FALSE, 0);

    gtk_window_set_policy(GTK_WINDOW(inst->window), FALSE, TRUE, TRUE);

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

    inst->textcursor = make_mouse_ptr(GDK_XTERM);
    inst->rawcursor = make_mouse_ptr(GDK_LEFT_PTR);
    inst->blankcursor = make_mouse_ptr(-1);
    make_mouse_ptr(-2);		       /* clean up cursor font */
    inst->currcursor = inst->textcursor;
    show_mouseptr(1);

    back = &pty_backend;
    back->init(NULL, 0, NULL, 0);

    gdk_input_add(pty_master_fd, GDK_INPUT_READ, pty_input_func, inst);

    term_init();
    term_size(cfg.height, cfg.width, cfg.savelines);

    gtk_main();

    return 0;
}
