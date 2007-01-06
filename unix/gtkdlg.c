/*
 * gtkdlg.c - GTK implementation of the PuTTY configuration box.
 */

/*
 * TODO when porting to GTK 2.0:
 * 
 *  - GtkTree is apparently deprecated and we should switch to
 *    GtkTreeView instead.
 *  - GtkLabel has a built-in mnemonic scheme, so we should at
 *    least consider switching to that from the current adhockery.
 */

#include <assert.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "gtkcols.h"

#ifdef TESTMODE
#define PUTTY_DO_GLOBALS	       /* actually _define_ globals */
#endif

#include "putty.h"
#include "storage.h"
#include "dialog.h"
#include "tree234.h"

struct Shortcut {
    GtkWidget *widget;
    struct uctrl *uc;
    int action;
};

struct Shortcuts {
    struct Shortcut sc[128];
};

struct uctrl {
    union control *ctrl;
    GtkWidget *toplevel;
    void *privdata;
    int privdata_needs_free;
    GtkWidget **buttons; int nbuttons; /* for radio buttons */
    GtkWidget *entry;         /* for editbox, combobox, filesel, fontsel */
    GtkWidget *button;        /* for filesel, fontsel */
    GtkWidget *list;	      /* for combobox, listbox */
    GtkWidget *menu;	      /* for optionmenu (==droplist) */
    GtkWidget *optmenu;	      /* also for optionmenu */
    GtkWidget *text;	      /* for text */
    GtkWidget *label;         /* for dlg_label_change */
    GtkAdjustment *adj;       /* for the scrollbar in a list box */
    guint textsig;
};

struct dlgparam {
    tree234 *byctrl, *bywidget;
    void *data;
    struct { unsigned char r, g, b, ok; } coloursel_result;   /* 0-255 */
    /* `flags' are set to indicate when a GTK signal handler is being called
     * due to automatic processing and should not flag a user event. */
    int flags;
    struct Shortcuts *shortcuts;
    GtkWidget *window, *cancelbutton, *currtreeitem, **treeitems;
    union control *currfocus, *lastfocus;
    int ntreeitems;
    int retval;
};
#define FLAG_UPDATING_COMBO_LIST 1

enum {				       /* values for Shortcut.action */
    SHORTCUT_EMPTY,		       /* no shortcut on this key */
    SHORTCUT_TREE,		       /* focus a tree item */
    SHORTCUT_FOCUS,		       /* focus the supplied widget */
    SHORTCUT_UCTRL,		       /* do something sane with uctrl */
    SHORTCUT_UCTRL_UP,		       /* uctrl is a draglist, move Up */
    SHORTCUT_UCTRL_DOWN,	       /* uctrl is a draglist, move Down */
};

/*
 * Forward references.
 */
static gboolean widget_focus(GtkWidget *widget, GdkEventFocus *event,
                             gpointer data);
static void shortcut_add(struct Shortcuts *scs, GtkWidget *labelw,
			 int chr, int action, void *ptr);
static void shortcut_highlight(GtkWidget *label, int chr);
static int listitem_single_key(GtkWidget *item, GdkEventKey *event,
                               gpointer data);
static int listitem_multi_key(GtkWidget *item, GdkEventKey *event,
                                 gpointer data);
static int listitem_button(GtkWidget *item, GdkEventButton *event,
			    gpointer data);
static void menuitem_activate(GtkMenuItem *item, gpointer data);
static void coloursel_ok(GtkButton *button, gpointer data);
static void coloursel_cancel(GtkButton *button, gpointer data);
static void window_destroy(GtkWidget *widget, gpointer data);

static int uctrl_cmp_byctrl(void *av, void *bv)
{
    struct uctrl *a = (struct uctrl *)av;
    struct uctrl *b = (struct uctrl *)bv;
    if (a->ctrl < b->ctrl)
	return -1;
    else if (a->ctrl > b->ctrl)
	return +1;
    return 0;
}

static int uctrl_cmp_byctrl_find(void *av, void *bv)
{
    union control *a = (union control *)av;
    struct uctrl *b = (struct uctrl *)bv;
    if (a < b->ctrl)
	return -1;
    else if (a > b->ctrl)
	return +1;
    return 0;
}

static int uctrl_cmp_bywidget(void *av, void *bv)
{
    struct uctrl *a = (struct uctrl *)av;
    struct uctrl *b = (struct uctrl *)bv;
    if (a->toplevel < b->toplevel)
	return -1;
    else if (a->toplevel > b->toplevel)
	return +1;
    return 0;
}

static int uctrl_cmp_bywidget_find(void *av, void *bv)
{
    GtkWidget *a = (GtkWidget *)av;
    struct uctrl *b = (struct uctrl *)bv;
    if (a < b->toplevel)
	return -1;
    else if (a > b->toplevel)
	return +1;
    return 0;
}

static void dlg_init(struct dlgparam *dp)
{
    dp->byctrl = newtree234(uctrl_cmp_byctrl);
    dp->bywidget = newtree234(uctrl_cmp_bywidget);
    dp->coloursel_result.ok = FALSE;
    dp->treeitems = NULL;
    dp->window = dp->cancelbutton = dp->currtreeitem = NULL;
    dp->flags = 0;
    dp->currfocus = NULL;
}

static void dlg_cleanup(struct dlgparam *dp)
{
    struct uctrl *uc;

    freetree234(dp->byctrl);	       /* doesn't free the uctrls inside */
    dp->byctrl = NULL;
    while ( (uc = index234(dp->bywidget, 0)) != NULL) {
	del234(dp->bywidget, uc);
	if (uc->privdata_needs_free)
	    sfree(uc->privdata);
	sfree(uc->buttons);
	sfree(uc);
    }
    freetree234(dp->bywidget);
    dp->bywidget = NULL;
    sfree(dp->treeitems);
}

static void dlg_add_uctrl(struct dlgparam *dp, struct uctrl *uc)
{
    add234(dp->byctrl, uc);
    add234(dp->bywidget, uc);
}

static struct uctrl *dlg_find_byctrl(struct dlgparam *dp, union control *ctrl)
{
    if (!dp->byctrl)
	return NULL;
    return find234(dp->byctrl, ctrl, uctrl_cmp_byctrl_find);
}

static struct uctrl *dlg_find_bywidget(struct dlgparam *dp, GtkWidget *w)
{
    struct uctrl *ret = NULL;
    if (!dp->bywidget)
	return NULL;
    do {
	ret = find234(dp->bywidget, w, uctrl_cmp_bywidget_find);
	if (ret)
	    return ret;
	w = w->parent;
    } while (w);
    return ret;
}

void *dlg_get_privdata(union control *ctrl, void *dlg)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);
    return uc->privdata;
}

void dlg_set_privdata(union control *ctrl, void *dlg, void *ptr)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);
    uc->privdata = ptr;
    uc->privdata_needs_free = FALSE;
}

void *dlg_alloc_privdata(union control *ctrl, void *dlg, size_t size)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);
    /*
     * This is an internal allocation routine, so it's allowed to
     * use smalloc directly.
     */
    uc->privdata = smalloc(size);
    uc->privdata_needs_free = FALSE;
    return uc->privdata;
}

union control *dlg_last_focused(union control *ctrl, void *dlg)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    if (dp->currfocus != ctrl)
        return dp->currfocus;
    else
        return dp->lastfocus;
}

void dlg_radiobutton_set(union control *ctrl, void *dlg, int which)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);
    assert(uc->ctrl->generic.type == CTRL_RADIO);
    assert(uc->buttons != NULL);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(uc->buttons[which]), TRUE);
}

int dlg_radiobutton_get(union control *ctrl, void *dlg)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);
    int i;

    assert(uc->ctrl->generic.type == CTRL_RADIO);
    assert(uc->buttons != NULL);
    for (i = 0; i < uc->nbuttons; i++)
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(uc->buttons[i])))
	    return i;
    return 0;			       /* got to return something */
}

void dlg_checkbox_set(union control *ctrl, void *dlg, int checked)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);
    assert(uc->ctrl->generic.type == CTRL_CHECKBOX);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(uc->toplevel), checked);
}

int dlg_checkbox_get(union control *ctrl, void *dlg)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);
    assert(uc->ctrl->generic.type == CTRL_CHECKBOX);
    return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(uc->toplevel));
}

void dlg_editbox_set(union control *ctrl, void *dlg, char const *text)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);
    assert(uc->ctrl->generic.type == CTRL_EDITBOX);
    assert(uc->entry != NULL);
    gtk_entry_set_text(GTK_ENTRY(uc->entry), text);
}

void dlg_editbox_get(union control *ctrl, void *dlg, char *buffer, int length)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);
    assert(uc->ctrl->generic.type == CTRL_EDITBOX);
    assert(uc->entry != NULL);
    strncpy(buffer, gtk_entry_get_text(GTK_ENTRY(uc->entry)),
	    length);
    buffer[length-1] = '\0';
}

static void container_remove_and_destroy(GtkWidget *w, gpointer data)
{
    GtkContainer *cont = GTK_CONTAINER(data);
    /* gtk_container_remove will unref the widget for us; we need not. */
    gtk_container_remove(cont, w);
}

/* The `listbox' functions can also apply to combo boxes. */
void dlg_listbox_clear(union control *ctrl, void *dlg)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);

    assert(uc->ctrl->generic.type == CTRL_EDITBOX ||
	   uc->ctrl->generic.type == CTRL_LISTBOX);
    assert(uc->menu != NULL || uc->list != NULL);

    if (uc->menu) {
	gtk_container_foreach(GTK_CONTAINER(uc->menu),
			      container_remove_and_destroy,
			      GTK_CONTAINER(uc->menu));
    } else {
	gtk_list_clear_items(GTK_LIST(uc->list), 0, -1);
    }
}

void dlg_listbox_del(union control *ctrl, void *dlg, int index)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);

    assert(uc->ctrl->generic.type == CTRL_EDITBOX ||
	   uc->ctrl->generic.type == CTRL_LISTBOX);
    assert(uc->menu != NULL || uc->list != NULL);

    if (uc->menu) {
	gtk_container_remove
	    (GTK_CONTAINER(uc->menu),
	     g_list_nth_data(GTK_MENU_SHELL(uc->menu)->children, index));
    } else {
	gtk_list_clear_items(GTK_LIST(uc->list), index, index+1);
    }
}

void dlg_listbox_add(union control *ctrl, void *dlg, char const *text)
{
    dlg_listbox_addwithid(ctrl, dlg, text, 0);
}

/*
 * Each listbox entry may have a numeric id associated with it.
 * Note that some front ends only permit a string to be stored at
 * each position, which means that _if_ you put two identical
 * strings in any listbox then you MUST not assign them different
 * IDs and expect to get meaningful results back.
 */
void dlg_listbox_addwithid(union control *ctrl, void *dlg,
			   char const *text, int id)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);

    assert(uc->ctrl->generic.type == CTRL_EDITBOX ||
	   uc->ctrl->generic.type == CTRL_LISTBOX);
    assert(uc->menu != NULL || uc->list != NULL);

    dp->flags |= FLAG_UPDATING_COMBO_LIST;

    if (uc->menu) {
	/*
	 * List item in a drop-down (but non-combo) list. Tabs are
	 * ignored; we just provide a standard menu item with the
	 * text.
	 */
	GtkWidget *menuitem = gtk_menu_item_new_with_label(text);

	gtk_container_add(GTK_CONTAINER(uc->menu), menuitem);
	gtk_widget_show(menuitem);

	gtk_object_set_data(GTK_OBJECT(menuitem), "user-data",
			    GINT_TO_POINTER(id));
	gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
			   GTK_SIGNAL_FUNC(menuitem_activate), dp);
    } else if (!uc->entry) {
	/*
	 * List item in a non-combo-box list box. We make all of
	 * these Columns containing GtkLabels. This allows us to do
	 * the nasty force_left hack irrespective of whether there
	 * are tabs in the thing.
	 */
	GtkWidget *listitem = gtk_list_item_new();
	GtkWidget *cols = columns_new(10);
	gint *percents;
	int i, ncols;

	/* Count the tabs in the text, and hence determine # of columns. */
	ncols = 1;
	for (i = 0; text[i]; i++)
	    if (text[i] == '\t')
		ncols++;

	assert(ncols <=
	       (uc->ctrl->listbox.ncols ? uc->ctrl->listbox.ncols : 1));
	percents = snewn(ncols, gint);
	percents[ncols-1] = 100;
	for (i = 0; i < ncols-1; i++) {
	    percents[i] = uc->ctrl->listbox.percentages[i];
	    percents[ncols-1] -= percents[i];
	}
	columns_set_cols(COLUMNS(cols), ncols, percents);
	sfree(percents);

	for (i = 0; i < ncols; i++) {
	    int len = strcspn(text, "\t");
	    char *dup = dupprintf("%.*s", len, text);
	    GtkWidget *label;

	    text += len;
	    if (*text) text++;
	    label = gtk_label_new(dup);
	    sfree(dup);

	    columns_add(COLUMNS(cols), label, i, 1);
	    columns_force_left_align(COLUMNS(cols), label);
	    gtk_widget_show(label);
	}
	gtk_container_add(GTK_CONTAINER(listitem), cols);
	gtk_widget_show(cols);
	gtk_container_add(GTK_CONTAINER(uc->list), listitem);
	gtk_widget_show(listitem);

        if (ctrl->listbox.multisel) {
            gtk_signal_connect(GTK_OBJECT(listitem), "key_press_event",
                               GTK_SIGNAL_FUNC(listitem_multi_key), uc->adj);
        } else {
            gtk_signal_connect(GTK_OBJECT(listitem), "key_press_event",
                               GTK_SIGNAL_FUNC(listitem_single_key), uc->adj);
        }
        gtk_signal_connect(GTK_OBJECT(listitem), "focus_in_event",
                           GTK_SIGNAL_FUNC(widget_focus), dp);
	gtk_signal_connect(GTK_OBJECT(listitem), "button_press_event",
			   GTK_SIGNAL_FUNC(listitem_button), dp);
	gtk_object_set_data(GTK_OBJECT(listitem), "user-data",
			    GINT_TO_POINTER(id));
    } else {
	/*
	 * List item in a combo-box list, which means the sensible
	 * thing to do is make it a perfectly normal label. Hence
	 * tabs are disregarded.
	 */
	GtkWidget *listitem = gtk_list_item_new_with_label(text);

	gtk_container_add(GTK_CONTAINER(uc->list), listitem);
	gtk_widget_show(listitem);

	gtk_object_set_data(GTK_OBJECT(listitem), "user-data",
			    GINT_TO_POINTER(id));
    }

    dp->flags &= ~FLAG_UPDATING_COMBO_LIST;
}

int dlg_listbox_getid(union control *ctrl, void *dlg, int index)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);
    GList *children;
    GtkObject *item;

    assert(uc->ctrl->generic.type == CTRL_EDITBOX ||
	   uc->ctrl->generic.type == CTRL_LISTBOX);
    assert(uc->menu != NULL || uc->list != NULL);

    children = gtk_container_children(GTK_CONTAINER(uc->menu ? uc->menu :
						    uc->list));
    item = GTK_OBJECT(g_list_nth_data(children, index));
    g_list_free(children);

    return GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(item), "user-data"));
}

/* dlg_listbox_index returns <0 if no single element is selected. */
int dlg_listbox_index(union control *ctrl, void *dlg)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);
    GList *children;
    GtkWidget *item, *activeitem;
    int i;
    int selected = -1;

    assert(uc->ctrl->generic.type == CTRL_EDITBOX ||
	   uc->ctrl->generic.type == CTRL_LISTBOX);
    assert(uc->menu != NULL || uc->list != NULL);

    if (uc->menu)
	activeitem = gtk_menu_get_active(GTK_MENU(uc->menu));
    else
	activeitem = NULL;	       /* unnecessarily placate gcc */

    children = gtk_container_children(GTK_CONTAINER(uc->menu ? uc->menu :
						    uc->list));
    for (i = 0; children!=NULL && (item = GTK_WIDGET(children->data))!=NULL;
	 i++, children = children->next) {
	if (uc->menu ? activeitem == item :
	    GTK_WIDGET_STATE(item) == GTK_STATE_SELECTED) {
	    if (selected == -1)
		selected = i;
	    else
		selected = -2;
	}
    }
    g_list_free(children);
    return selected < 0 ? -1 : selected;
}

int dlg_listbox_issel(union control *ctrl, void *dlg, int index)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);
    GList *children;
    GtkWidget *item, *activeitem;

    assert(uc->ctrl->generic.type == CTRL_EDITBOX ||
	   uc->ctrl->generic.type == CTRL_LISTBOX);
    assert(uc->menu != NULL || uc->list != NULL);

    children = gtk_container_children(GTK_CONTAINER(uc->menu ? uc->menu :
						    uc->list));
    item = GTK_WIDGET(g_list_nth_data(children, index));
    g_list_free(children);

    if (uc->menu) {
	activeitem = gtk_menu_get_active(GTK_MENU(uc->menu));
	return item == activeitem;
    } else {
	return GTK_WIDGET_STATE(item) == GTK_STATE_SELECTED;
    }
}

void dlg_listbox_select(union control *ctrl, void *dlg, int index)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);

    assert(uc->ctrl->generic.type == CTRL_EDITBOX ||
	   uc->ctrl->generic.type == CTRL_LISTBOX);
    assert(uc->optmenu != NULL || uc->list != NULL);

    if (uc->optmenu) {
	gtk_option_menu_set_history(GTK_OPTION_MENU(uc->optmenu), index);
    } else {
        int nitems;
        GList *items;
        gdouble newtop, newbot;

	gtk_list_select_item(GTK_LIST(uc->list), index);

        /*
         * Scroll the list box if necessary to ensure the newly
         * selected item is visible.
         */
        items = gtk_container_children(GTK_CONTAINER(uc->list));
        nitems = g_list_length(items);
        if (nitems > 0) {
            int modified = FALSE;
            g_list_free(items);
            newtop = uc->adj->lower +
                (uc->adj->upper - uc->adj->lower) * index / nitems;
            newbot = uc->adj->lower +
                (uc->adj->upper - uc->adj->lower) * (index+1) / nitems;
            if (uc->adj->value > newtop) {
                modified = TRUE;
                uc->adj->value = newtop;
            } else if (uc->adj->value < newbot - uc->adj->page_size) {
                modified = TRUE;
                uc->adj->value = newbot - uc->adj->page_size;
            }
            if (modified)
                gtk_adjustment_value_changed(uc->adj);
        }
    }
}

void dlg_text_set(union control *ctrl, void *dlg, char const *text)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);

    assert(uc->ctrl->generic.type == CTRL_TEXT);
    assert(uc->text != NULL);

    gtk_label_set_text(GTK_LABEL(uc->text), text);
}

void dlg_label_change(union control *ctrl, void *dlg, char const *text)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);

    switch (uc->ctrl->generic.type) {
      case CTRL_BUTTON:
	gtk_label_set_text(GTK_LABEL(uc->toplevel), text);
	shortcut_highlight(uc->toplevel, ctrl->button.shortcut);
	break;
      case CTRL_CHECKBOX:
	gtk_label_set_text(GTK_LABEL(uc->toplevel), text);
	shortcut_highlight(uc->toplevel, ctrl->checkbox.shortcut);
	break;
      case CTRL_RADIO:
	gtk_label_set_text(GTK_LABEL(uc->label), text);
	shortcut_highlight(uc->label, ctrl->radio.shortcut);
	break;
      case CTRL_EDITBOX:
	gtk_label_set_text(GTK_LABEL(uc->label), text);
	shortcut_highlight(uc->label, ctrl->editbox.shortcut);
	break;
      case CTRL_FILESELECT:
	gtk_label_set_text(GTK_LABEL(uc->label), text);
	shortcut_highlight(uc->label, ctrl->fileselect.shortcut);
	break;
      case CTRL_FONTSELECT:
	gtk_label_set_text(GTK_LABEL(uc->label), text);
	shortcut_highlight(uc->label, ctrl->fontselect.shortcut);
	break;
      case CTRL_LISTBOX:
	gtk_label_set_text(GTK_LABEL(uc->label), text);
	shortcut_highlight(uc->label, ctrl->listbox.shortcut);
	break;
      default:
	assert(!"This shouldn't happen");
	break;
    }
}

void dlg_filesel_set(union control *ctrl, void *dlg, Filename fn)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);
    assert(uc->ctrl->generic.type == CTRL_FILESELECT);
    assert(uc->entry != NULL);
    gtk_entry_set_text(GTK_ENTRY(uc->entry), fn.path);
}

void dlg_filesel_get(union control *ctrl, void *dlg, Filename *fn)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);
    assert(uc->ctrl->generic.type == CTRL_FILESELECT);
    assert(uc->entry != NULL);
    strncpy(fn->path, gtk_entry_get_text(GTK_ENTRY(uc->entry)),
	    lenof(fn->path));
    fn->path[lenof(fn->path)-1] = '\0';
}

void dlg_fontsel_set(union control *ctrl, void *dlg, FontSpec fs)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);
    assert(uc->ctrl->generic.type == CTRL_FONTSELECT);
    assert(uc->entry != NULL);
    gtk_entry_set_text(GTK_ENTRY(uc->entry), fs.name);
}

void dlg_fontsel_get(union control *ctrl, void *dlg, FontSpec *fs)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);
    assert(uc->ctrl->generic.type == CTRL_FONTSELECT);
    assert(uc->entry != NULL);
    strncpy(fs->name, gtk_entry_get_text(GTK_ENTRY(uc->entry)),
	    lenof(fs->name));
    fs->name[lenof(fs->name)-1] = '\0';
}

/*
 * Bracketing a large set of updates in these two functions will
 * cause the front end (if possible) to delay updating the screen
 * until it's all complete, thus avoiding flicker.
 */
void dlg_update_start(union control *ctrl, void *dlg)
{
    /*
     * Apparently we can't do this at all in GTK. GtkCList supports
     * freeze and thaw, but not GtkList. Bah.
     */
}

void dlg_update_done(union control *ctrl, void *dlg)
{
    /*
     * Apparently we can't do this at all in GTK. GtkCList supports
     * freeze and thaw, but not GtkList. Bah.
     */
}

void dlg_set_focus(union control *ctrl, void *dlg)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);

    switch (ctrl->generic.type) {
      case CTRL_CHECKBOX:
      case CTRL_BUTTON:
        /* Check boxes and buttons get the focus _and_ get toggled. */
        gtk_widget_grab_focus(uc->toplevel);
        break;
      case CTRL_FILESELECT:
      case CTRL_FONTSELECT:
      case CTRL_EDITBOX:
        /* Anything containing an edit box gets that focused. */
        gtk_widget_grab_focus(uc->entry);
        break;
      case CTRL_RADIO:
        /*
         * Radio buttons: we find the currently selected button and
         * focus it.
         */
        {
            int i;
            for (i = 0; i < ctrl->radio.nbuttons; i++)
                if (gtk_toggle_button_get_active
                    (GTK_TOGGLE_BUTTON(uc->buttons[i]))) {
                    gtk_widget_grab_focus(uc->buttons[i]);
                }
        }
        break;
      case CTRL_LISTBOX:
        /*
         * If the list is really an option menu, we focus it.
         * Otherwise we tell it to focus one of its children, which
         * appears to do the Right Thing.
         */
        if (uc->optmenu) {
            gtk_widget_grab_focus(uc->optmenu);
        } else {
            assert(uc->list != NULL);
            gtk_container_focus(GTK_CONTAINER(uc->list), GTK_DIR_TAB_FORWARD);
        }
        break;
    }
}

/*
 * During event processing, you might well want to give an error
 * indication to the user. dlg_beep() is a quick and easy generic
 * error; dlg_error() puts up a message-box or equivalent.
 */
void dlg_beep(void *dlg)
{
    gdk_beep();
}

static void errmsg_button_clicked(GtkButton *button, gpointer data)
{
    gtk_widget_destroy(GTK_WIDGET(data));
}

static void set_transient_window_pos(GtkWidget *parent, GtkWidget *child)
{
    gint x, y, w, h, dx, dy;
    GtkRequisition req;
    gtk_window_set_position(GTK_WINDOW(child), GTK_WIN_POS_NONE);
    gtk_widget_size_request(GTK_WIDGET(child), &req);

    gdk_window_get_origin(GTK_WIDGET(parent)->window, &x, &y);
    gdk_window_get_size(GTK_WIDGET(parent)->window, &w, &h);

    /*
     * One corner of the transient will be offset inwards, by 1/4
     * of the parent window's size, from the corresponding corner
     * of the parent window. The corner will be chosen so as to
     * place the transient closer to the centre of the screen; this
     * should avoid transients going off the edge of the screen on
     * a regular basis.
     */
    if (x + w/2 < gdk_screen_width() / 2)
        dx = x + w/4;                  /* work from left edges */
    else
        dx = x + 3*w/4 - req.width;    /* work from right edges */
    if (y + h/2 < gdk_screen_height() / 2)
        dy = y + h/4;                  /* work from top edges */
    else
        dy = y + 3*h/4 - req.height;   /* work from bottom edges */
    gtk_widget_set_uposition(GTK_WIDGET(child), dx, dy);
}

void dlg_error_msg(void *dlg, char *msg)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    GtkWidget *window, *hbox, *text, *ok;

    window = gtk_dialog_new();
    text = gtk_label_new(msg);
    gtk_misc_set_alignment(GTK_MISC(text), 0.0, 0.0);
    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), text, FALSE, FALSE, 20);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(window)->vbox),
                       hbox, FALSE, FALSE, 20);
    gtk_widget_show(text);
    gtk_widget_show(hbox);
    gtk_window_set_title(GTK_WINDOW(window), "Error");
    gtk_label_set_line_wrap(GTK_LABEL(text), TRUE);
    ok = gtk_button_new_with_label("OK");
    gtk_box_pack_end(GTK_BOX(GTK_DIALOG(window)->action_area),
                     ok, FALSE, FALSE, 0);
    gtk_widget_show(ok);
    GTK_WIDGET_SET_FLAGS(ok, GTK_CAN_DEFAULT);
    gtk_window_set_default(GTK_WINDOW(window), ok);
    gtk_signal_connect(GTK_OBJECT(ok), "clicked",
                       GTK_SIGNAL_FUNC(errmsg_button_clicked), window);
    gtk_signal_connect(GTK_OBJECT(window), "destroy",
                       GTK_SIGNAL_FUNC(window_destroy), NULL);
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(dp->window));
    set_transient_window_pos(dp->window, window);
    gtk_widget_show(window);
    gtk_main();
}

/*
 * This function signals to the front end that the dialog's
 * processing is completed, and passes an integer value (typically
 * a success status).
 */
void dlg_end(void *dlg, int value)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    dp->retval = value;
    gtk_widget_destroy(dp->window);
}

void dlg_refresh(union control *ctrl, void *dlg)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc;

    if (ctrl) {
	if (ctrl->generic.handler != NULL)
	    ctrl->generic.handler(ctrl, dp, dp->data, EVENT_REFRESH);
    } else {
	int i;

	for (i = 0; (uc = index234(dp->byctrl, i)) != NULL; i++) {
	    assert(uc->ctrl != NULL);
	    if (uc->ctrl->generic.handler != NULL)
		uc->ctrl->generic.handler(uc->ctrl, dp,
					  dp->data, EVENT_REFRESH);
	}
    }
}

void dlg_coloursel_start(union control *ctrl, void *dlg, int r, int g, int b)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    struct uctrl *uc = dlg_find_byctrl(dp, ctrl);
    gdouble cvals[4];

    GtkWidget *coloursel =
	gtk_color_selection_dialog_new("Select a colour");
    GtkColorSelectionDialog *ccs = GTK_COLOR_SELECTION_DIALOG(coloursel);

    dp->coloursel_result.ok = FALSE;

    gtk_window_set_modal(GTK_WINDOW(coloursel), TRUE);
    gtk_color_selection_set_opacity(GTK_COLOR_SELECTION(ccs->colorsel), FALSE);
    cvals[0] = r / 255.0;
    cvals[1] = g / 255.0;
    cvals[2] = b / 255.0;
    cvals[3] = 1.0;		       /* fully opaque! */
    gtk_color_selection_set_color(GTK_COLOR_SELECTION(ccs->colorsel), cvals);

    gtk_object_set_data(GTK_OBJECT(ccs->ok_button), "user-data",
			(gpointer)coloursel);
    gtk_object_set_data(GTK_OBJECT(ccs->cancel_button), "user-data",
			(gpointer)coloursel);
    gtk_object_set_data(GTK_OBJECT(coloursel), "user-data", (gpointer)uc);
    gtk_signal_connect(GTK_OBJECT(ccs->ok_button), "clicked",
		       GTK_SIGNAL_FUNC(coloursel_ok), (gpointer)dp);
    gtk_signal_connect(GTK_OBJECT(ccs->cancel_button), "clicked",
		       GTK_SIGNAL_FUNC(coloursel_cancel), (gpointer)dp);
    gtk_signal_connect_object(GTK_OBJECT(ccs->ok_button), "clicked",
			      GTK_SIGNAL_FUNC(gtk_widget_destroy),
			      (gpointer)coloursel);
    gtk_signal_connect_object(GTK_OBJECT(ccs->cancel_button), "clicked",
			      GTK_SIGNAL_FUNC(gtk_widget_destroy),
			      (gpointer)coloursel);
    gtk_widget_show(coloursel);
}

int dlg_coloursel_results(union control *ctrl, void *dlg,
			  int *r, int *g, int *b)
{
    struct dlgparam *dp = (struct dlgparam *)dlg;
    if (dp->coloursel_result.ok) {
	*r = dp->coloursel_result.r;
	*g = dp->coloursel_result.g;
	*b = dp->coloursel_result.b;
	return 1;
    } else
	return 0;
}

/* ----------------------------------------------------------------------
 * Signal handlers while the dialog box is active.
 */

static gboolean widget_focus(GtkWidget *widget, GdkEventFocus *event,
                             gpointer data)
{
    struct dlgparam *dp = (struct dlgparam *)data;
    struct uctrl *uc = dlg_find_bywidget(dp, widget);
    union control *focus;

    if (uc && uc->ctrl)
        focus = uc->ctrl;
    else
        focus = NULL;

    if (focus != dp->currfocus) {
        dp->lastfocus = dp->currfocus;
        dp->currfocus = focus;
    }

    return FALSE;
}

static void button_clicked(GtkButton *button, gpointer data)
{
    struct dlgparam *dp = (struct dlgparam *)data;
    struct uctrl *uc = dlg_find_bywidget(dp, GTK_WIDGET(button));
    uc->ctrl->generic.handler(uc->ctrl, dp, dp->data, EVENT_ACTION);
}

static void button_toggled(GtkToggleButton *tb, gpointer data)
{
    struct dlgparam *dp = (struct dlgparam *)data;
    struct uctrl *uc = dlg_find_bywidget(dp, GTK_WIDGET(tb));
    uc->ctrl->generic.handler(uc->ctrl, dp, dp->data, EVENT_VALCHANGE);
}

static int editbox_key(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    /*
     * GtkEntry has a nasty habit of eating the Return key, which
     * is unhelpful since it doesn't actually _do_ anything with it
     * (it calls gtk_widget_activate, but our edit boxes never need
     * activating). So I catch Return before GtkEntry sees it, and
     * pass it straight on to the parent widget. Effect: hitting
     * Return in an edit box will now activate the default button
     * in the dialog just like it will everywhere else.
     */
    if (event->keyval == GDK_Return && widget->parent != NULL) {
	gint return_val;
	gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
	gtk_signal_emit_by_name(GTK_OBJECT(widget->parent), "key_press_event",
				event, &return_val);
	return return_val;
    }
    return FALSE;
}

static void editbox_changed(GtkEditable *ed, gpointer data)
{
    struct dlgparam *dp = (struct dlgparam *)data;
    if (!(dp->flags & FLAG_UPDATING_COMBO_LIST)) {
	struct uctrl *uc = dlg_find_bywidget(dp, GTK_WIDGET(ed));
	uc->ctrl->generic.handler(uc->ctrl, dp, dp->data, EVENT_VALCHANGE);
    }
}

static void editbox_lostfocus(GtkWidget *ed, GdkEventFocus *event,
			      gpointer data)
{
    struct dlgparam *dp = (struct dlgparam *)data;
    struct uctrl *uc = dlg_find_bywidget(dp, GTK_WIDGET(ed));
    uc->ctrl->generic.handler(uc->ctrl, dp, dp->data, EVENT_REFRESH);
}

static int listitem_key(GtkWidget *item, GdkEventKey *event, gpointer data,
                        int multiple)
{
    GtkAdjustment *adj = GTK_ADJUSTMENT(data);

    if (event->keyval == GDK_Up || event->keyval == GDK_KP_Up ||
        event->keyval == GDK_Down || event->keyval == GDK_KP_Down ||
        event->keyval == GDK_Page_Up || event->keyval == GDK_KP_Page_Up ||
        event->keyval == GDK_Page_Down || event->keyval == GDK_KP_Page_Down) {
        /*
         * Up, Down, PgUp or PgDn have been pressed on a ListItem
         * in a list box. So, if the list box is single-selection:
         * 
         *  - if the list item in question isn't already selected,
         *    we simply select it.
         *  - otherwise, we find the next one (or next
         *    however-far-away) in whichever direction we're going,
         *    and select that.
         *     + in this case, we must also fiddle with the
         *       scrollbar to ensure the newly selected item is
         *       actually visible.
         * 
         * If it's multiple-selection, we do all of the above
         * except actually selecting anything, so we move the focus
         * and fiddle the scrollbar to follow it.
         */
        GtkWidget *list = item->parent;

        gtk_signal_emit_stop_by_name(GTK_OBJECT(item), "key_press_event");

        if (!multiple &&
            GTK_WIDGET_STATE(item) != GTK_STATE_SELECTED) {
                gtk_list_select_child(GTK_LIST(list), item);
        } else {
            int direction =
                (event->keyval==GDK_Up || event->keyval==GDK_KP_Up ||
                 event->keyval==GDK_Page_Up || event->keyval==GDK_KP_Page_Up)
                ? -1 : +1;
            int step =
                (event->keyval==GDK_Page_Down || 
                 event->keyval==GDK_KP_Page_Down ||
                 event->keyval==GDK_Page_Up || event->keyval==GDK_KP_Page_Up)
                ? 2 : 1;
            int i, n;
            GList *children, *chead;

            chead = children = gtk_container_children(GTK_CONTAINER(list));

            n = g_list_length(children);

            if (step == 2) {
                /*
                 * Figure out how many list items to a screenful,
                 * and adjust the step appropriately.
                 */
                step = 0.5 + adj->page_size * n / (adj->upper - adj->lower);
                step--;                /* go by one less than that */
            }

            i = 0;
            while (children != NULL) {
                if (item == children->data)
                    break;
                children = children->next;
                i++;
            }

            while (step > 0) {
                if (direction < 0 && i > 0)
                    children = children->prev, i--;
                else if (direction > 0 && i < n-1)
                    children = children->next, i++;
                step--;
            }

            if (children && children->data) {
                if (!multiple)
                    gtk_list_select_child(GTK_LIST(list),
                                          GTK_WIDGET(children->data));
                gtk_widget_grab_focus(GTK_WIDGET(children->data));
                gtk_adjustment_clamp_page
                    (adj,
                     adj->lower + (adj->upper-adj->lower) * i / n,
                     adj->lower + (adj->upper-adj->lower) * (i+1) / n);
            }

            g_list_free(chead);
        }
        return TRUE;
    }

    return FALSE;
}

static int listitem_single_key(GtkWidget *item, GdkEventKey *event,
                               gpointer data)
{
    return listitem_key(item, event, data, FALSE);
}

static int listitem_multi_key(GtkWidget *item, GdkEventKey *event,
                                 gpointer data)
{
    return listitem_key(item, event, data, TRUE);
}

static int listitem_button(GtkWidget *item, GdkEventButton *event,
			    gpointer data)
{
    struct dlgparam *dp = (struct dlgparam *)data;
    if (event->type == GDK_2BUTTON_PRESS ||
	event->type == GDK_3BUTTON_PRESS) {
	struct uctrl *uc = dlg_find_bywidget(dp, GTK_WIDGET(item));
	uc->ctrl->generic.handler(uc->ctrl, dp, dp->data, EVENT_ACTION);
        return TRUE;
    }
    return FALSE;
}

static void list_selchange(GtkList *list, gpointer data)
{
    struct dlgparam *dp = (struct dlgparam *)data;
    struct uctrl *uc = dlg_find_bywidget(dp, GTK_WIDGET(list));
    if (!uc) return;
    uc->ctrl->generic.handler(uc->ctrl, dp, dp->data, EVENT_SELCHANGE);
}

static void menuitem_activate(GtkMenuItem *item, gpointer data)
{
    struct dlgparam *dp = (struct dlgparam *)data;
    GtkWidget *menushell = GTK_WIDGET(item)->parent;
    gpointer optmenu = gtk_object_get_data(GTK_OBJECT(menushell), "user-data");
    struct uctrl *uc = dlg_find_bywidget(dp, GTK_WIDGET(optmenu));
    uc->ctrl->generic.handler(uc->ctrl, dp, dp->data, EVENT_SELCHANGE);
}

static void draglist_move(struct dlgparam *dp, struct uctrl *uc, int direction)
{
    int index = dlg_listbox_index(uc->ctrl, dp);
    GList *children = gtk_container_children(GTK_CONTAINER(uc->list));
    GtkWidget *child;

    if ((index < 0) ||
	(index == 0 && direction < 0) ||
	(index == g_list_length(children)-1 && direction > 0)) {
	gdk_beep();
	return;
    }

    child = g_list_nth_data(children, index);
    gtk_widget_ref(child);
    gtk_list_clear_items(GTK_LIST(uc->list), index, index+1);
    g_list_free(children);

    children = NULL;
    children = g_list_append(children, child);
    gtk_list_insert_items(GTK_LIST(uc->list), children, index + direction);
    gtk_list_select_item(GTK_LIST(uc->list), index + direction);
    uc->ctrl->generic.handler(uc->ctrl, dp, dp->data, EVENT_VALCHANGE);
}

static void draglist_up(GtkButton *button, gpointer data)
{
    struct dlgparam *dp = (struct dlgparam *)data;
    struct uctrl *uc = dlg_find_bywidget(dp, GTK_WIDGET(button));
    draglist_move(dp, uc, -1);
}

static void draglist_down(GtkButton *button, gpointer data)
{
    struct dlgparam *dp = (struct dlgparam *)data;
    struct uctrl *uc = dlg_find_bywidget(dp, GTK_WIDGET(button));
    draglist_move(dp, uc, +1);
}

static void filesel_ok(GtkButton *button, gpointer data)
{
    /* struct dlgparam *dp = (struct dlgparam *)data; */
    gpointer filesel = gtk_object_get_data(GTK_OBJECT(button), "user-data");
    struct uctrl *uc = gtk_object_get_data(GTK_OBJECT(filesel), "user-data");
    char *name = gtk_file_selection_get_filename(GTK_FILE_SELECTION(filesel));
    gtk_entry_set_text(GTK_ENTRY(uc->entry), name);
}

static void fontsel_ok(GtkButton *button, gpointer data)
{
    /* struct dlgparam *dp = (struct dlgparam *)data; */
    gpointer fontsel = gtk_object_get_data(GTK_OBJECT(button), "user-data");
    struct uctrl *uc = gtk_object_get_data(GTK_OBJECT(fontsel), "user-data");
    char *name = gtk_font_selection_dialog_get_font_name
	(GTK_FONT_SELECTION_DIALOG(fontsel));
    gtk_entry_set_text(GTK_ENTRY(uc->entry), name);
}

static void coloursel_ok(GtkButton *button, gpointer data)
{
    struct dlgparam *dp = (struct dlgparam *)data;
    gpointer coloursel = gtk_object_get_data(GTK_OBJECT(button), "user-data");
    struct uctrl *uc = gtk_object_get_data(GTK_OBJECT(coloursel), "user-data");
    gdouble cvals[4];
    gtk_color_selection_get_color
	(GTK_COLOR_SELECTION(GTK_COLOR_SELECTION_DIALOG(coloursel)->colorsel),
	 cvals);
    dp->coloursel_result.r = (int) (255 * cvals[0]);
    dp->coloursel_result.g = (int) (255 * cvals[1]);
    dp->coloursel_result.b = (int) (255 * cvals[2]);
    dp->coloursel_result.ok = TRUE;
    uc->ctrl->generic.handler(uc->ctrl, dp, dp->data, EVENT_CALLBACK);
}

static void coloursel_cancel(GtkButton *button, gpointer data)
{
    struct dlgparam *dp = (struct dlgparam *)data;
    gpointer coloursel = gtk_object_get_data(GTK_OBJECT(button), "user-data");
    struct uctrl *uc = gtk_object_get_data(GTK_OBJECT(coloursel), "user-data");
    dp->coloursel_result.ok = FALSE;
    uc->ctrl->generic.handler(uc->ctrl, dp, dp->data, EVENT_CALLBACK);
}

static void filefont_clicked(GtkButton *button, gpointer data)
{
    struct dlgparam *dp = (struct dlgparam *)data;
    struct uctrl *uc = dlg_find_bywidget(dp, GTK_WIDGET(button));

    if (uc->ctrl->generic.type == CTRL_FILESELECT) {
	GtkWidget *filesel =
	    gtk_file_selection_new(uc->ctrl->fileselect.title);
	gtk_window_set_modal(GTK_WINDOW(filesel), TRUE);
	gtk_object_set_data
	    (GTK_OBJECT(GTK_FILE_SELECTION(filesel)->ok_button), "user-data",
	     (gpointer)filesel);
	gtk_object_set_data(GTK_OBJECT(filesel), "user-data", (gpointer)uc);
	gtk_signal_connect
	    (GTK_OBJECT(GTK_FILE_SELECTION(filesel)->ok_button), "clicked",
	     GTK_SIGNAL_FUNC(filesel_ok), (gpointer)dp);
	gtk_signal_connect_object
	    (GTK_OBJECT(GTK_FILE_SELECTION(filesel)->ok_button), "clicked",
	     GTK_SIGNAL_FUNC(gtk_widget_destroy), (gpointer)filesel);
	gtk_signal_connect_object
	    (GTK_OBJECT(GTK_FILE_SELECTION(filesel)->cancel_button), "clicked",
	     GTK_SIGNAL_FUNC(gtk_widget_destroy), (gpointer)filesel);
	gtk_widget_show(filesel);
    }

    if (uc->ctrl->generic.type == CTRL_FONTSELECT) {
	gchar *spacings[] = { "c", "m", NULL };
        gchar *fontname = gtk_entry_get_text(GTK_ENTRY(uc->entry));
	GtkWidget *fontsel =
	    gtk_font_selection_dialog_new("Select a font");
	gtk_window_set_modal(GTK_WINDOW(fontsel), TRUE);
	gtk_font_selection_dialog_set_filter
	    (GTK_FONT_SELECTION_DIALOG(fontsel),
	     GTK_FONT_FILTER_BASE, GTK_FONT_ALL,
	     NULL, NULL, NULL, NULL, spacings, NULL);
	if (!gtk_font_selection_dialog_set_font_name
	    (GTK_FONT_SELECTION_DIALOG(fontsel), fontname)) {
            /*
             * If the font name wasn't found as it was, try opening
             * it and extracting its FONT property. This should
             * have the effect of mapping short aliases into true
             * XLFDs.
             */
            GdkFont *font = gdk_font_load(fontname);
            if (font) {
                XFontStruct *xfs = GDK_FONT_XFONT(font);
                Display *disp = GDK_FONT_XDISPLAY(font);
                Atom fontprop = XInternAtom(disp, "FONT", False);
                unsigned long ret;
                if (XGetFontProperty(xfs, fontprop, &ret)) {
                    char *name = XGetAtomName(disp, (Atom)ret);
                    if (name)
                        gtk_font_selection_dialog_set_font_name
                        (GTK_FONT_SELECTION_DIALOG(fontsel), name);
                }
                gdk_font_unref(font);
            }
        }
	gtk_object_set_data
	    (GTK_OBJECT(GTK_FONT_SELECTION_DIALOG(fontsel)->ok_button),
	     "user-data", (gpointer)fontsel);
	gtk_object_set_data(GTK_OBJECT(fontsel), "user-data", (gpointer)uc);
	gtk_signal_connect
	    (GTK_OBJECT(GTK_FONT_SELECTION_DIALOG(fontsel)->ok_button),
	     "clicked", GTK_SIGNAL_FUNC(fontsel_ok), (gpointer)dp);
	gtk_signal_connect_object
	    (GTK_OBJECT(GTK_FONT_SELECTION_DIALOG(fontsel)->ok_button),
	     "clicked", GTK_SIGNAL_FUNC(gtk_widget_destroy),
	     (gpointer)fontsel);
	gtk_signal_connect_object
	    (GTK_OBJECT(GTK_FONT_SELECTION_DIALOG(fontsel)->cancel_button),
	     "clicked", GTK_SIGNAL_FUNC(gtk_widget_destroy),
	     (gpointer)fontsel);
	gtk_widget_show(fontsel);
    }
}

static void label_sizealloc(GtkWidget *widget, GtkAllocation *alloc,
			    gpointer data)
{
    struct dlgparam *dp = (struct dlgparam *)data;
    struct uctrl *uc = dlg_find_bywidget(dp, widget);

    gtk_widget_set_usize(uc->text, alloc->width, -1);
    gtk_label_set_text(GTK_LABEL(uc->text), uc->ctrl->generic.label);
    gtk_signal_disconnect(GTK_OBJECT(uc->text), uc->textsig);
}

/* ----------------------------------------------------------------------
 * This function does the main layout work: it reads a controlset,
 * it creates the relevant GTK controls, and returns a GtkWidget
 * containing the result. (This widget might be a title of some
 * sort, it might be a Columns containing many controls, or it
 * might be a GtkFrame containing a Columns; whatever it is, it's
 * definitely a GtkWidget and should probably be added to a
 * GtkVbox.)
 * 
 * `listitemheight' is used to calculate a usize for list boxes: it
 * should be the height from the size request of a GtkListItem.
 * 
 * `win' is required for setting the default button. If it is
 * non-NULL, all buttons created will be default-capable (so they
 * have extra space round them for the default highlight).
 */
GtkWidget *layout_ctrls(struct dlgparam *dp, struct Shortcuts *scs,
			struct controlset *s, int listitemheight,
			GtkWindow *win)
{
    Columns *cols;
    GtkWidget *ret;
    int i;

    if (!s->boxname && s->boxtitle) {
        /* This controlset is a panel title. */
        return gtk_label_new(s->boxtitle);
    }

    /*
     * Otherwise, we expect to be laying out actual controls, so
     * we'll start by creating a Columns for the purpose.
     */
    cols = COLUMNS(columns_new(4));
    ret = GTK_WIDGET(cols);
    gtk_widget_show(ret);

    /*
     * Create a containing frame if we have a box name.
     */
    if (*s->boxname) {
        ret = gtk_frame_new(s->boxtitle);   /* NULL is valid here */
        gtk_container_set_border_width(GTK_CONTAINER(cols), 4);
        gtk_container_add(GTK_CONTAINER(ret), GTK_WIDGET(cols));
        gtk_widget_show(ret);
    }

    /*
     * Now iterate through the controls themselves, create them,
     * and add them to the Columns.
     */
    for (i = 0; i < s->ncontrols; i++) {
	union control *ctrl = s->ctrls[i];
	struct uctrl *uc;
	int left = FALSE;
        GtkWidget *w = NULL;

        switch (ctrl->generic.type) {
          case CTRL_COLUMNS:
            {
                static const int simplecols[1] = { 100 };
                columns_set_cols(cols, ctrl->columns.ncols,
                                 (ctrl->columns.percentages ?
                                  ctrl->columns.percentages : simplecols));
            }
            continue;                  /* no actual control created */
          case CTRL_TABDELAY:
	    {
		struct uctrl *uc = dlg_find_byctrl(dp, ctrl->tabdelay.ctrl);
		if (uc)
		    columns_taborder_last(cols, uc->toplevel);
	    }
            continue;                  /* no actual control created */
	}

	uc = snew(struct uctrl);
	uc->ctrl = ctrl;
	uc->privdata = NULL;
	uc->privdata_needs_free = FALSE;
	uc->buttons = NULL;
	uc->entry = uc->list = uc->menu = NULL;
	uc->button = uc->optmenu = uc->text = NULL;
	uc->label = NULL;

        switch (ctrl->generic.type) {
          case CTRL_BUTTON:
            w = gtk_button_new_with_label(ctrl->generic.label);
	    if (win) {
		GTK_WIDGET_SET_FLAGS(w, GTK_CAN_DEFAULT);
		if (ctrl->button.isdefault)
		    gtk_window_set_default(win, w);
		if (ctrl->button.iscancel)
		    dp->cancelbutton = w;
	    }
	    gtk_signal_connect(GTK_OBJECT(w), "clicked",
			       GTK_SIGNAL_FUNC(button_clicked), dp);
            gtk_signal_connect(GTK_OBJECT(w), "focus_in_event",
                               GTK_SIGNAL_FUNC(widget_focus), dp);
	    shortcut_add(scs, GTK_BIN(w)->child, ctrl->button.shortcut,
			 SHORTCUT_UCTRL, uc);
            break;
          case CTRL_CHECKBOX:
            w = gtk_check_button_new_with_label(ctrl->generic.label);
	    gtk_signal_connect(GTK_OBJECT(w), "toggled",
			       GTK_SIGNAL_FUNC(button_toggled), dp);
            gtk_signal_connect(GTK_OBJECT(w), "focus_in_event",
                               GTK_SIGNAL_FUNC(widget_focus), dp);
	    shortcut_add(scs, GTK_BIN(w)->child, ctrl->checkbox.shortcut,
			 SHORTCUT_UCTRL, uc);
	    left = TRUE;
            break;
          case CTRL_RADIO:
            /*
             * Radio buttons get to go inside their own Columns, no
             * matter what.
             */
            {
                gint i, *percentages;
                GSList *group;

                w = columns_new(0);
                if (ctrl->generic.label) {
                    GtkWidget *label = gtk_label_new(ctrl->generic.label);
                    columns_add(COLUMNS(w), label, 0, 1);
		    columns_force_left_align(COLUMNS(w), label);
                    gtk_widget_show(label);
		    shortcut_add(scs, label, ctrl->radio.shortcut,
				 SHORTCUT_UCTRL, uc);
		    uc->label = label;
                }
                percentages = g_new(gint, ctrl->radio.ncolumns);
                for (i = 0; i < ctrl->radio.ncolumns; i++) {
                    percentages[i] =
                        ((100 * (i+1) / ctrl->radio.ncolumns) -
                         100 * i / ctrl->radio.ncolumns);
                }
                columns_set_cols(COLUMNS(w), ctrl->radio.ncolumns,
                                 percentages);
                g_free(percentages);
                group = NULL;

		uc->nbuttons = ctrl->radio.nbuttons;
		uc->buttons = snewn(uc->nbuttons, GtkWidget *);

                for (i = 0; i < ctrl->radio.nbuttons; i++) {
                    GtkWidget *b;
                    gint colstart;

                    b = (gtk_radio_button_new_with_label
                         (group, ctrl->radio.buttons[i]));
		    uc->buttons[i] = b;
                    group = gtk_radio_button_group(GTK_RADIO_BUTTON(b));
                    colstart = i % ctrl->radio.ncolumns;
                    columns_add(COLUMNS(w), b, colstart,
                                (i == ctrl->radio.nbuttons-1 ?
                                 ctrl->radio.ncolumns - colstart : 1));
		    columns_force_left_align(COLUMNS(w), b);
                    gtk_widget_show(b);
		    gtk_signal_connect(GTK_OBJECT(b), "toggled",
				       GTK_SIGNAL_FUNC(button_toggled), dp);
                    gtk_signal_connect(GTK_OBJECT(b), "focus_in_event",
                                       GTK_SIGNAL_FUNC(widget_focus), dp);
		    if (ctrl->radio.shortcuts) {
			shortcut_add(scs, GTK_BIN(b)->child,
				     ctrl->radio.shortcuts[i],
				     SHORTCUT_UCTRL, uc);
		    }
                }
            }
            break;
          case CTRL_EDITBOX:
	    {
                GtkRequisition req;

		if (ctrl->editbox.has_list) {
		    w = gtk_combo_new();
		    gtk_combo_set_value_in_list(GTK_COMBO(w), FALSE, TRUE);
		    uc->entry = GTK_COMBO(w)->entry;
		    uc->list = GTK_COMBO(w)->list;
		} else {
		    w = gtk_entry_new();
		    if (ctrl->editbox.password)
			gtk_entry_set_visibility(GTK_ENTRY(w), FALSE);
		    uc->entry = w;
		}
		gtk_signal_connect(GTK_OBJECT(uc->entry), "changed",
				   GTK_SIGNAL_FUNC(editbox_changed), dp);
		gtk_signal_connect(GTK_OBJECT(uc->entry), "key_press_event",
				   GTK_SIGNAL_FUNC(editbox_key), dp);
		gtk_signal_connect(GTK_OBJECT(uc->entry), "focus_in_event",
				   GTK_SIGNAL_FUNC(widget_focus), dp);
		/*
		 * Edit boxes, for some strange reason, have a minimum
		 * width of 150 in GTK 1.2. We don't want this - we'd
		 * rather the edit boxes acquired their natural width
		 * from the column layout of the rest of the box.
		 *
		 * Also, while we're here, we'll squirrel away the
		 * edit box height so we can use that to centre its
		 * label vertically beside it.
		 */
                gtk_widget_size_request(w, &req);
                gtk_widget_set_usize(w, 10, req.height);

		if (ctrl->generic.label) {
		    GtkWidget *label, *container;

		    label = gtk_label_new(ctrl->generic.label);

		    shortcut_add(scs, label, ctrl->editbox.shortcut,
				 SHORTCUT_FOCUS, uc->entry);

		    container = columns_new(4);
		    if (ctrl->editbox.percentwidth == 100) {
			columns_add(COLUMNS(container), label, 0, 1);
			columns_force_left_align(COLUMNS(container), label);
			columns_add(COLUMNS(container), w, 0, 1);
		    } else {
			gint percentages[2];
			percentages[1] = ctrl->editbox.percentwidth;
			percentages[0] = 100 - ctrl->editbox.percentwidth;
			columns_set_cols(COLUMNS(container), 2, percentages);
			columns_add(COLUMNS(container), label, 0, 1);
			columns_force_left_align(COLUMNS(container), label);
			columns_add(COLUMNS(container), w, 1, 1);
			/* Centre the label vertically. */
			gtk_widget_set_usize(label, -1, req.height);
			gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
		    }
		    gtk_widget_show(label);
		    gtk_widget_show(w);

		    w = container;
		    uc->label = label;
		}
		gtk_signal_connect(GTK_OBJECT(uc->entry), "focus_out_event",
				   GTK_SIGNAL_FUNC(editbox_lostfocus), dp);
	    }
            break;
          case CTRL_FILESELECT:
          case CTRL_FONTSELECT:
            {
                GtkWidget *ww;
                GtkRequisition req;
                char *browsebtn =
                    (ctrl->generic.type == CTRL_FILESELECT ?
                     "Browse..." : "Change...");

                gint percentages[] = { 75, 25 };
                w = columns_new(4);
                columns_set_cols(COLUMNS(w), 2, percentages);

                if (ctrl->generic.label) {
                    ww = gtk_label_new(ctrl->generic.label);
                    columns_add(COLUMNS(w), ww, 0, 2);
		    columns_force_left_align(COLUMNS(w), ww);
                    gtk_widget_show(ww);
		    shortcut_add(scs, ww,
				 (ctrl->generic.type == CTRL_FILESELECT ?
				  ctrl->fileselect.shortcut :
				  ctrl->fontselect.shortcut),
				 SHORTCUT_UCTRL, uc);
		    uc->label = ww;
                }

                uc->entry = ww = gtk_entry_new();
                gtk_widget_size_request(ww, &req);
                gtk_widget_set_usize(ww, 10, req.height);
                columns_add(COLUMNS(w), ww, 0, 1);
                gtk_widget_show(ww);

                uc->button = ww = gtk_button_new_with_label(browsebtn);
                columns_add(COLUMNS(w), ww, 1, 1);
                gtk_widget_show(ww);

		gtk_signal_connect(GTK_OBJECT(uc->entry), "key_press_event",
				   GTK_SIGNAL_FUNC(editbox_key), dp);
		gtk_signal_connect(GTK_OBJECT(uc->entry), "changed",
				   GTK_SIGNAL_FUNC(editbox_changed), dp);
                gtk_signal_connect(GTK_OBJECT(uc->entry), "focus_in_event",
                                   GTK_SIGNAL_FUNC(widget_focus), dp);
                gtk_signal_connect(GTK_OBJECT(uc->button), "focus_in_event",
                                   GTK_SIGNAL_FUNC(widget_focus), dp);
		gtk_signal_connect(GTK_OBJECT(ww), "clicked",
				   GTK_SIGNAL_FUNC(filefont_clicked), dp);
            }
            break;
          case CTRL_LISTBOX:
            if (ctrl->listbox.height == 0) {
                uc->optmenu = w = gtk_option_menu_new();
		uc->menu = gtk_menu_new();
		gtk_option_menu_set_menu(GTK_OPTION_MENU(w), uc->menu);
		gtk_object_set_data(GTK_OBJECT(uc->menu), "user-data",
				    (gpointer)uc->optmenu);
                gtk_signal_connect(GTK_OBJECT(uc->optmenu), "focus_in_event",
                                   GTK_SIGNAL_FUNC(widget_focus), dp);
            } else {
                uc->list = gtk_list_new();
                if (ctrl->listbox.multisel == 2) {
                    gtk_list_set_selection_mode(GTK_LIST(uc->list),
                                                GTK_SELECTION_EXTENDED);
                } else if (ctrl->listbox.multisel == 1) {
                    gtk_list_set_selection_mode(GTK_LIST(uc->list),
                                                GTK_SELECTION_MULTIPLE);
                } else {
                    gtk_list_set_selection_mode(GTK_LIST(uc->list),
                                                GTK_SELECTION_SINGLE);
                }
                w = gtk_scrolled_window_new(NULL, NULL);
                gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(w),
                                                      uc->list);
                gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(w),
                                               GTK_POLICY_NEVER,
                                               GTK_POLICY_AUTOMATIC);
                uc->adj = gtk_scrolled_window_get_vadjustment
                    (GTK_SCROLLED_WINDOW(w));

                gtk_widget_show(uc->list);
		gtk_signal_connect(GTK_OBJECT(uc->list), "selection-changed",
				   GTK_SIGNAL_FUNC(list_selchange), dp);
                gtk_signal_connect(GTK_OBJECT(uc->list), "focus_in_event",
                                   GTK_SIGNAL_FUNC(widget_focus), dp);

                /*
                 * Adjust the height of the scrolled window to the
                 * minimum given by the height parameter.
                 * 
                 * This piece of guesswork is a horrid hack based
                 * on looking inside the GTK 1.2 sources
                 * (specifically gtkviewport.c, which appears to be
                 * the widget which provides the border around the
                 * scrolling area). Anyone lets me know how I can
                 * do this in a way which isn't at risk from GTK
                 * upgrades, I'd be grateful.
                 */
		{
		    int edge = GTK_WIDGET(uc->list)->style->klass->ythickness;
                    gtk_widget_set_usize(w, 10,
                                         2*edge + (ctrl->listbox.height *
						   listitemheight));
		}

                if (ctrl->listbox.draglist) {
                    /*
                     * GTK doesn't appear to make it easy to
                     * implement a proper draggable list; so
                     * instead I'm just going to have to put an Up
                     * and a Down button to the right of the actual
                     * list box. Ah well.
                     */
                    GtkWidget *cols, *button;
                    static const gint percentages[2] = { 80, 20 };

                    cols = columns_new(4);
                    columns_set_cols(COLUMNS(cols), 2, percentages);
                    columns_add(COLUMNS(cols), w, 0, 1);
                    gtk_widget_show(w);
                    button = gtk_button_new_with_label("Up");
                    columns_add(COLUMNS(cols), button, 1, 1);
                    gtk_widget_show(button);
		    gtk_signal_connect(GTK_OBJECT(button), "clicked",
				       GTK_SIGNAL_FUNC(draglist_up), dp);
                    gtk_signal_connect(GTK_OBJECT(button), "focus_in_event",
                                       GTK_SIGNAL_FUNC(widget_focus), dp);
                    button = gtk_button_new_with_label("Down");
                    columns_add(COLUMNS(cols), button, 1, 1);
                    gtk_widget_show(button);
		    gtk_signal_connect(GTK_OBJECT(button), "clicked",
				       GTK_SIGNAL_FUNC(draglist_down), dp);
                    gtk_signal_connect(GTK_OBJECT(button), "focus_in_event",
                                       GTK_SIGNAL_FUNC(widget_focus), dp);

                    w = cols;
                }

            }
            if (ctrl->generic.label) {
                GtkWidget *label, *container;

                label = gtk_label_new(ctrl->generic.label);

		container = columns_new(4);
                if (ctrl->listbox.percentwidth == 100) {
                    columns_add(COLUMNS(container), label, 0, 1);
		    columns_force_left_align(COLUMNS(container), label);
                    columns_add(COLUMNS(container), w, 0, 1);
                } else {
                    gint percentages[2];
                    percentages[1] = ctrl->listbox.percentwidth;
                    percentages[0] = 100 - ctrl->listbox.percentwidth;
                    columns_set_cols(COLUMNS(container), 2, percentages);
                    columns_add(COLUMNS(container), label, 0, 1);
		    columns_force_left_align(COLUMNS(container), label);
                    columns_add(COLUMNS(container), w, 1, 1);
                }
                gtk_widget_show(label);
                gtk_widget_show(w);
		shortcut_add(scs, label, ctrl->listbox.shortcut,
			     SHORTCUT_UCTRL, uc);
                w = container;
		uc->label = label;
            }
            break;
          case CTRL_TEXT:
	    /*
	     * Wrapping text widgets don't sit well with the GTK
	     * layout model, in which widgets state a minimum size
	     * and the whole window then adjusts to the smallest
	     * size it can sensibly take given its contents. A
	     * wrapping text widget _has_ no clear minimum size;
	     * instead it has a range of possibilities. It can be
	     * one line deep but 2000 wide, or two lines deep and
	     * 1000 pixels, or three by 867, or four by 500 and so
	     * on. It can be as short as you like provided you
	     * don't mind it being wide, or as narrow as you like
	     * provided you don't mind it being tall.
	     * 
	     * Therefore, it fits very badly into the layout model.
	     * Hence the only thing to do is pick a width and let
	     * it choose its own number of lines. To do this I'm
	     * going to cheat a little. All new wrapping text
	     * widgets will be created with a minimal text content
	     * "X"; then, after the rest of the dialog box is set
	     * up and its size calculated, the text widgets will be
	     * told their width and given their real text, which
	     * will cause the size to be recomputed in the y
	     * direction (because many of them will expand to more
	     * than one line).
	     */
            uc->text = w = gtk_label_new("X");
            gtk_misc_set_alignment(GTK_MISC(w), 0.0, 0.0);
            gtk_label_set_line_wrap(GTK_LABEL(w), TRUE);
	    uc->textsig =
		gtk_signal_connect(GTK_OBJECT(w), "size-allocate",
				   GTK_SIGNAL_FUNC(label_sizealloc), dp);
            break;
        }

	assert(w != NULL);

	columns_add(cols, w,
		    COLUMN_START(ctrl->generic.column),
		    COLUMN_SPAN(ctrl->generic.column));
	if (left)
	    columns_force_left_align(cols, w);
	gtk_widget_show(w);

	uc->toplevel = w;
	dlg_add_uctrl(dp, uc);
    }

    return ret;
}

struct selparam {
    struct dlgparam *dp;
    GtkNotebook *panels;
    GtkWidget *panel, *treeitem;
    struct Shortcuts shortcuts;
};

static void treeitem_sel(GtkItem *item, gpointer data)
{
    struct selparam *sp = (struct selparam *)data;
    gint page_num;

    page_num = gtk_notebook_page_num(sp->panels, sp->panel);
    gtk_notebook_set_page(sp->panels, page_num);

    dlg_refresh(NULL, sp->dp);

    sp->dp->shortcuts = &sp->shortcuts;
    sp->dp->currtreeitem = sp->treeitem;
}

static void window_destroy(GtkWidget *widget, gpointer data)
{
    gtk_main_quit();
}

static int tree_grab_focus(struct dlgparam *dp)
{
    int i, f;

    /*
     * See if any of the treeitems has the focus.
     */
    f = -1;
    for (i = 0; i < dp->ntreeitems; i++)
        if (GTK_WIDGET_HAS_FOCUS(dp->treeitems[i])) {
            f = i;
            break;
        }

    if (f >= 0)
        return FALSE;
    else {
        gtk_widget_grab_focus(dp->currtreeitem);
        return TRUE;
    }
}

gint tree_focus(GtkContainer *container, GtkDirectionType direction,
                gpointer data)
{
    struct dlgparam *dp = (struct dlgparam *)data;

    gtk_signal_emit_stop_by_name(GTK_OBJECT(container), "focus");
    /*
     * If there's a focused treeitem, we return FALSE to cause the
     * focus to move on to some totally other control. If not, we
     * focus the selected one.
     */
    return tree_grab_focus(dp);
}

int win_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    struct dlgparam *dp = (struct dlgparam *)data;

    if (event->keyval == GDK_Escape && dp->cancelbutton) {
	gtk_signal_emit_by_name(GTK_OBJECT(dp->cancelbutton), "clicked");
	return TRUE;
    }

    if ((event->state & GDK_MOD1_MASK) &&
	(unsigned char)event->string[0] > 0 &&
	(unsigned char)event->string[0] <= 127) {
	int schr = (unsigned char)event->string[0];
	struct Shortcut *sc = &dp->shortcuts->sc[schr];

	switch (sc->action) {
	  case SHORTCUT_TREE:
	    tree_grab_focus(dp);
	    break;
	  case SHORTCUT_FOCUS:
	    gtk_widget_grab_focus(sc->widget);
	    break;
	  case SHORTCUT_UCTRL:
	    /*
	     * We must do something sensible with a uctrl.
	     * Precisely what this is depends on the type of
	     * control.
	     */
	    switch (sc->uc->ctrl->generic.type) {
	      case CTRL_CHECKBOX:
	      case CTRL_BUTTON:
		/* Check boxes and buttons get the focus _and_ get toggled. */
		gtk_widget_grab_focus(sc->uc->toplevel);
		gtk_signal_emit_by_name(GTK_OBJECT(sc->uc->toplevel),
					"clicked");
		break;
	      case CTRL_FILESELECT:
	      case CTRL_FONTSELECT:
		/* File/font selectors have their buttons pressed (ooer),
		 * and focus transferred to the edit box. */
		gtk_signal_emit_by_name(GTK_OBJECT(sc->uc->button),
					"clicked");
		gtk_widget_grab_focus(sc->uc->entry);
		break;
	      case CTRL_RADIO:
		/*
		 * Radio buttons are fun, because they have
		 * multiple shortcuts. We must find whether the
		 * activated shortcut is the shortcut for the whole
		 * group, or for a particular button. In the former
		 * case, we find the currently selected button and
		 * focus it; in the latter, we focus-and-click the
		 * button whose shortcut was pressed.
		 */
		if (schr == sc->uc->ctrl->radio.shortcut) {
		    int i;
		    for (i = 0; i < sc->uc->ctrl->radio.nbuttons; i++)
			if (gtk_toggle_button_get_active
			    (GTK_TOGGLE_BUTTON(sc->uc->buttons[i]))) {
			    gtk_widget_grab_focus(sc->uc->buttons[i]);
			}
		} else if (sc->uc->ctrl->radio.shortcuts) {
		    int i;
		    for (i = 0; i < sc->uc->ctrl->radio.nbuttons; i++)
			if (schr == sc->uc->ctrl->radio.shortcuts[i]) {
			    gtk_widget_grab_focus(sc->uc->buttons[i]);
			    gtk_signal_emit_by_name
				(GTK_OBJECT(sc->uc->buttons[i]), "clicked");
			}
		}
		break;
	      case CTRL_LISTBOX:
		/*
		 * If the list is really an option menu, we focus
		 * and click it. Otherwise we tell it to focus one
		 * of its children, which appears to do the Right
		 * Thing.
		 */
		if (sc->uc->optmenu) {
		    GdkEventButton bev;
		    gint returnval;

		    gtk_widget_grab_focus(sc->uc->optmenu);
		    /* Option menus don't work using the "clicked" signal.
		     * We need to manufacture a button press event :-/ */
		    bev.type = GDK_BUTTON_PRESS;
		    bev.button = 1;
		    gtk_signal_emit_by_name(GTK_OBJECT(sc->uc->optmenu),
					    "button_press_event",
					    &bev, &returnval);
		} else {
                    assert(sc->uc->list != NULL);

                    gtk_container_focus(GTK_CONTAINER(sc->uc->list),
                                        GTK_DIR_TAB_FORWARD);
		}
		break;
	    }
	    break;
	}
    }

    return FALSE;
}

int tree_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    struct dlgparam *dp = (struct dlgparam *)data;

    if (event->keyval == GDK_Up || event->keyval == GDK_KP_Up ||
        event->keyval == GDK_Down || event->keyval == GDK_KP_Down) {
        int dir, i, j = -1;
        for (i = 0; i < dp->ntreeitems; i++)
            if (widget == dp->treeitems[i])
		break;
	if (i < dp->ntreeitems) {
	    if (event->keyval == GDK_Up || event->keyval == GDK_KP_Up)
		dir = -1;
	    else
		dir = +1;

	    while (1) {
		i += dir;
		if (i < 0 || i >= dp->ntreeitems)
		    break;	       /* nothing in that dir to select */
		/*
		 * Determine if this tree item is visible.
		 */
		{
		    GtkWidget *w = dp->treeitems[i];
		    int vis = TRUE;
		    while (w && (GTK_IS_TREE_ITEM(w) || GTK_IS_TREE(w))) {
			if (!GTK_WIDGET_VISIBLE(w)) {
			    vis = FALSE;
			    break;
			}
			w = w->parent;
		    }
		    if (vis) {
			j = i;	       /* got one */
			break;
		    }
		}
	    }
	}
        gtk_signal_emit_stop_by_name(GTK_OBJECT(widget),
                                     "key_press_event");
        if (j >= 0) {
            gtk_signal_emit_by_name(GTK_OBJECT(dp->treeitems[j]), "toggle");
            gtk_widget_grab_focus(dp->treeitems[j]);
        }
        return TRUE;
    }

    /*
     * It's nice for Left and Right to expand and collapse tree
     * branches.
     */
    if (event->keyval == GDK_Left || event->keyval == GDK_KP_Left) {
        gtk_signal_emit_stop_by_name(GTK_OBJECT(widget),
                                     "key_press_event");
	gtk_tree_item_collapse(GTK_TREE_ITEM(widget));
	return TRUE;
    }
    if (event->keyval == GDK_Right || event->keyval == GDK_KP_Right) {
        gtk_signal_emit_stop_by_name(GTK_OBJECT(widget),
                                     "key_press_event");
	gtk_tree_item_expand(GTK_TREE_ITEM(widget));
	return TRUE;
    }

    return FALSE;
}

static void shortcut_highlight(GtkWidget *labelw, int chr)
{
    GtkLabel *label = GTK_LABEL(labelw);
    gchar *currstr, *pattern;
    int i;

    gtk_label_get(label, &currstr);
    for (i = 0; currstr[i]; i++)
	if (tolower((unsigned char)currstr[i]) == chr) {
	    GtkRequisition req;

	    pattern = dupprintf("%*s_", i, "");

	    gtk_widget_size_request(GTK_WIDGET(label), &req);
	    gtk_label_set_pattern(label, pattern);
	    gtk_widget_set_usize(GTK_WIDGET(label), -1, req.height);

	    sfree(pattern);
	    break;
	}
}

void shortcut_add(struct Shortcuts *scs, GtkWidget *labelw,
		  int chr, int action, void *ptr)
{
    if (chr == NO_SHORTCUT)
	return;

    chr = tolower((unsigned char)chr);

    assert(scs->sc[chr].action == SHORTCUT_EMPTY);

    scs->sc[chr].action = action;

    if (action == SHORTCUT_FOCUS) {
	scs->sc[chr].uc = NULL;
	scs->sc[chr].widget = (GtkWidget *)ptr;
    } else {
	scs->sc[chr].widget = NULL;
	scs->sc[chr].uc = (struct uctrl *)ptr;
    }

    shortcut_highlight(labelw, chr);
}

int get_listitemheight(void)
{
    GtkWidget *listitem = gtk_list_item_new_with_label("foo");
    GtkRequisition req;
    gtk_widget_size_request(listitem, &req);
    gtk_widget_unref(listitem);
    return req.height;
}

int do_config_box(const char *title, Config *cfg, int midsession,
		  int protcfginfo)
{
    GtkWidget *window, *hbox, *vbox, *cols, *label,
	*tree, *treescroll, *panels, *panelvbox;
    int index, level, listitemheight;
    struct controlbox *ctrlbox;
    char *path;
    GtkTreeItem *treeitemlevels[8];
    GtkTree *treelevels[8];
    struct dlgparam dp;
    struct Shortcuts scs;

    struct selparam *selparams = NULL;
    int nselparams = 0, selparamsize = 0;

    dlg_init(&dp);

    listitemheight = get_listitemheight();

    for (index = 0; index < lenof(scs.sc); index++) {
	scs.sc[index].action = SHORTCUT_EMPTY;
    }

    window = gtk_dialog_new();

    ctrlbox = ctrl_new_box();
    setup_config_box(ctrlbox, midsession, cfg->protocol, protcfginfo);
    unix_setup_config_box(ctrlbox, midsession, cfg->protocol);
    gtk_setup_config_box(ctrlbox, midsession, window);

    gtk_window_set_title(GTK_WINDOW(window), title);
    hbox = gtk_hbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(window)->vbox), hbox, TRUE, TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 10);
    gtk_widget_show(hbox);
    vbox = gtk_vbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);
    gtk_widget_show(vbox);
    cols = columns_new(4);
    gtk_box_pack_start(GTK_BOX(vbox), cols, FALSE, FALSE, 0);
    gtk_widget_show(cols);
    label = gtk_label_new("Category:");
    columns_add(COLUMNS(cols), label, 0, 1);
    columns_force_left_align(COLUMNS(cols), label);
    gtk_widget_show(label);
    treescroll = gtk_scrolled_window_new(NULL, NULL);
    tree = gtk_tree_new();
    gtk_signal_connect(GTK_OBJECT(tree), "focus_in_event",
                       GTK_SIGNAL_FUNC(widget_focus), &dp);
    shortcut_add(&scs, label, 'g', SHORTCUT_TREE, tree);
    gtk_tree_set_view_mode(GTK_TREE(tree), GTK_TREE_VIEW_ITEM);
    gtk_tree_set_selection_mode(GTK_TREE(tree), GTK_SELECTION_BROWSE);
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(treescroll),
					  tree);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(treescroll),
				   GTK_POLICY_NEVER,
				   GTK_POLICY_AUTOMATIC);
    gtk_signal_connect(GTK_OBJECT(tree), "focus",
		       GTK_SIGNAL_FUNC(tree_focus), &dp);
    gtk_widget_show(tree);
    gtk_widget_show(treescroll);
    gtk_box_pack_start(GTK_BOX(vbox), treescroll, TRUE, TRUE, 0);
    panels = gtk_notebook_new();
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(panels), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(panels), FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), panels, TRUE, TRUE, 0);
    gtk_widget_show(panels);

    panelvbox = NULL;
    path = NULL;
    level = 0;
    for (index = 0; index < ctrlbox->nctrlsets; index++) {
	struct controlset *s = ctrlbox->ctrlsets[index];
	GtkWidget *w;

	if (!*s->pathname) {
	    w = layout_ctrls(&dp, &scs, s, listitemheight, GTK_WINDOW(window));
	    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(window)->action_area),
			       w, TRUE, TRUE, 0);
	} else {
	    int j = path ? ctrl_path_compare(s->pathname, path) : 0;
	    if (j != INT_MAX) {        /* add to treeview, start new panel */
		char *c;
		GtkWidget *treeitem;
		int first;

		/*
		 * We expect never to find an implicit path
		 * component. For example, we expect never to see
		 * A/B/C followed by A/D/E, because that would
		 * _implicitly_ create A/D. All our path prefixes
		 * are expected to contain actual controls and be
		 * selectable in the treeview; so we would expect
		 * to see A/D _explicitly_ before encountering
		 * A/D/E.
		 */
		assert(j == ctrl_path_elements(s->pathname) - 1);

		c = strrchr(s->pathname, '/');
		if (!c)
		    c = s->pathname;
		else
		    c++;

		treeitem = gtk_tree_item_new_with_label(c);
		assert(j-1 < level);
		if (j > 0) {
		    if (!treelevels[j-1]) {
			treelevels[j-1] = GTK_TREE(gtk_tree_new());
			gtk_tree_item_set_subtree
			    (treeitemlevels[j-1],
			     GTK_WIDGET(treelevels[j-1]));
			gtk_tree_item_expand(treeitemlevels[j-1]);
		    }
		    gtk_tree_append(treelevels[j-1], treeitem);
		} else {
		    gtk_tree_append(GTK_TREE(tree), treeitem);
		}
		treeitemlevels[j] = GTK_TREE_ITEM(treeitem);
		treelevels[j] = NULL;
		level = j+1;

                gtk_signal_connect(GTK_OBJECT(treeitem), "key_press_event",
                                   GTK_SIGNAL_FUNC(tree_key_press), &dp);
                gtk_signal_connect(GTK_OBJECT(treeitem), "focus_in_event",
                                   GTK_SIGNAL_FUNC(widget_focus), &dp);

		gtk_widget_show(treeitem);

		path = s->pathname;

		first = (panelvbox == NULL);

		panelvbox = gtk_vbox_new(FALSE, 4);
		gtk_widget_show(panelvbox);
		gtk_notebook_append_page(GTK_NOTEBOOK(panels), panelvbox,
					 NULL);
		if (first) {
		    gint page_num;

		    page_num = gtk_notebook_page_num(GTK_NOTEBOOK(panels),
						     panelvbox);
		    gtk_notebook_set_page(GTK_NOTEBOOK(panels), page_num);
		    gtk_tree_select_child(GTK_TREE(tree), treeitem);
		}

		if (nselparams >= selparamsize) {
		    selparamsize += 16;
		    selparams = sresize(selparams, selparamsize,
					struct selparam);
		}
		selparams[nselparams].dp = &dp;
		selparams[nselparams].panels = GTK_NOTEBOOK(panels);
		selparams[nselparams].panel = panelvbox;
		selparams[nselparams].shortcuts = scs;   /* structure copy */
		selparams[nselparams].treeitem = treeitem;
		nselparams++;

	    }

	    w = layout_ctrls(&dp,
			     &selparams[nselparams-1].shortcuts,
			     s, listitemheight, NULL);
	    gtk_box_pack_start(GTK_BOX(panelvbox), w, FALSE, FALSE, 0);
            gtk_widget_show(w);
	}
    }

    dp.ntreeitems = nselparams;
    dp.treeitems = snewn(dp.ntreeitems, GtkWidget *);

    for (index = 0; index < nselparams; index++) {
	gtk_signal_connect(GTK_OBJECT(selparams[index].treeitem), "select",
			   GTK_SIGNAL_FUNC(treeitem_sel),
			   &selparams[index]);
        dp.treeitems[index] = selparams[index].treeitem;
    }

    dp.data = cfg;
    dlg_refresh(NULL, &dp);

    dp.shortcuts = &selparams[0].shortcuts;
    dp.currtreeitem = dp.treeitems[0];
    dp.lastfocus = NULL;
    dp.retval = 0;
    dp.window = window;

    {
	/* in gtkwin.c */
	extern void set_window_icon(GtkWidget *window,
				    const char *const *const *icon,
				    int n_icon);
	extern const char *const *const cfg_icon[];
	extern const int n_cfg_icon;
	set_window_icon(window, cfg_icon, n_cfg_icon);
    }

    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_widget_show(window);

    /*
     * Set focus into the first available control.
     */
    for (index = 0; index < ctrlbox->nctrlsets; index++) {
	struct controlset *s = ctrlbox->ctrlsets[index];
        int done = 0;
        int j;

	if (*s->pathname) {
            for (j = 0; j < s->ncontrols; j++)
                if (s->ctrls[j]->generic.type != CTRL_TABDELAY &&
                    s->ctrls[j]->generic.type != CTRL_COLUMNS &&
                    s->ctrls[j]->generic.type != CTRL_TEXT) {
                    dlg_set_focus(s->ctrls[j], &dp);
                    dp.lastfocus = s->ctrls[j];
                    done = 1;
                    break;
                }
        }
        if (done)
            break;
    }

    gtk_signal_connect(GTK_OBJECT(window), "destroy",
		       GTK_SIGNAL_FUNC(window_destroy), NULL);
    gtk_signal_connect(GTK_OBJECT(window), "key_press_event",
		       GTK_SIGNAL_FUNC(win_key_press), &dp);

    gtk_main();

    dlg_cleanup(&dp);
    sfree(selparams);

    return dp.retval;
}

static void messagebox_handler(union control *ctrl, void *dlg,
			       void *data, int event)
{
    if (event == EVENT_ACTION)
	dlg_end(dlg, ctrl->generic.context.i);
}
int messagebox(GtkWidget *parentwin, char *title, char *msg, int minwid, ...)
{
    GtkWidget *window, *w0, *w1;
    struct controlbox *ctrlbox;
    struct controlset *s0, *s1;
    union control *c;
    struct dlgparam dp;
    struct Shortcuts scs;
    int index, ncols;
    va_list ap;

    dlg_init(&dp);

    for (index = 0; index < lenof(scs.sc); index++) {
	scs.sc[index].action = SHORTCUT_EMPTY;
    }

    ctrlbox = ctrl_new_box();

    ncols = 0;
    va_start(ap, minwid);
    while (va_arg(ap, char *) != NULL) {
	ncols++;
	(void) va_arg(ap, int);	       /* shortcut */
	(void) va_arg(ap, int);	       /* normal/default/cancel */
	(void) va_arg(ap, int);	       /* end value */
    }
    va_end(ap);

    s0 = ctrl_getset(ctrlbox, "", "", "");
    c = ctrl_columns(s0, 2, 50, 50);
    c->columns.ncols = s0->ncolumns = ncols;
    c->columns.percentages = sresize(c->columns.percentages, ncols, int);
    for (index = 0; index < ncols; index++)
	c->columns.percentages[index] = (index+1)*100/ncols - index*100/ncols;
    va_start(ap, minwid);
    index = 0;
    while (1) {
	char *title = va_arg(ap, char *);
	int shortcut, type, value;
	if (title == NULL)
	    break;
	shortcut = va_arg(ap, int);
	type = va_arg(ap, int);
	value = va_arg(ap, int);
	c = ctrl_pushbutton(s0, title, shortcut, HELPCTX(no_help),
			    messagebox_handler, I(value));
	c->generic.column = index++;
	if (type > 0)
	    c->button.isdefault = TRUE;
	else if (type < 0)
	    c->button.iscancel = TRUE;
    }
    va_end(ap);

    s1 = ctrl_getset(ctrlbox, "x", "", "");
    ctrl_text(s1, msg, HELPCTX(no_help));

    window = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(window), title);
    w0 = layout_ctrls(&dp, &scs, s0, 0, GTK_WINDOW(window));
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(window)->action_area),
		       w0, TRUE, TRUE, 0);
    gtk_widget_show(w0);
    w1 = layout_ctrls(&dp, &scs, s1, 0, GTK_WINDOW(window));
    gtk_container_set_border_width(GTK_CONTAINER(w1), 10);
    gtk_widget_set_usize(w1, minwid+20, -1);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(window)->vbox),
		       w1, TRUE, TRUE, 0);
    gtk_widget_show(w1);

    dp.shortcuts = &scs;
    dp.lastfocus = NULL;
    dp.retval = 0;
    dp.window = window;

    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    if (parentwin) {
        set_transient_window_pos(parentwin, window);
	gtk_window_set_transient_for(GTK_WINDOW(window),
				     GTK_WINDOW(parentwin));
    } else
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_widget_show(window);

    gtk_signal_connect(GTK_OBJECT(window), "destroy",
		       GTK_SIGNAL_FUNC(window_destroy), NULL);
    gtk_signal_connect(GTK_OBJECT(window), "key_press_event",
		       GTK_SIGNAL_FUNC(win_key_press), &dp);

    gtk_main();

    dlg_cleanup(&dp);
    ctrl_free_box(ctrlbox);

    return dp.retval;
}

static int string_width(char *text)
{
    GtkWidget *label = gtk_label_new(text);
    GtkRequisition req;
    gtk_widget_size_request(label, &req);
    gtk_widget_unref(label);
    return req.width;
}

int reallyclose(void *frontend)
{
    char *title = dupcat(appname, " Exit Confirmation", NULL);
    int ret = messagebox(GTK_WIDGET(get_window(frontend)),
			 title, "Are you sure you want to close this session?",
			 string_width("Most of the width of the above text"),
			 "Yes", 'y', +1, 1,
			 "No", 'n', -1, 0,
			 NULL);
    sfree(title);
    return ret;
}

int verify_ssh_host_key(void *frontend, char *host, int port, char *keytype,
                        char *keystr, char *fingerprint,
                        void (*callback)(void *ctx, int result), void *ctx)
{
    static const char absenttxt[] =
	"The server's host key is not cached. You have no guarantee "
	"that the server is the computer you think it is.\n"
	"The server's %s key fingerprint is:\n"
	"%s\n"
	"If you trust this host, press \"Accept\" to add the key to "
	"PuTTY's cache and carry on connecting.\n"
	"If you want to carry on connecting just once, without "
	"adding the key to the cache, press \"Connect Once\".\n"
	"If you do not trust this host, press \"Cancel\" to abandon the "
	"connection.";
    static const char wrongtxt[] =
	"WARNING - POTENTIAL SECURITY BREACH!\n"
	"The server's host key does not match the one PuTTY has "
	"cached. This means that either the server administrator "
	"has changed the host key, or you have actually connected "
	"to another computer pretending to be the server.\n"
	"The new %s key fingerprint is:\n"
	"%s\n"
	"If you were expecting this change and trust the new key, "
	"press \"Accept\" to update PuTTY's cache and continue connecting.\n"
	"If you want to carry on connecting but without updating "
	"the cache, press \"Connect Once\".\n"
	"If you want to abandon the connection completely, press "
	"\"Cancel\" to cancel. Pressing \"Cancel\" is the ONLY guaranteed "
	"safe choice.";
    char *text;
    int ret;

    /*
     * Verify the key.
     */
    ret = verify_host_key(host, port, keytype, keystr);

    if (ret == 0)		       /* success - key matched OK */
	return 1;

    text = dupprintf((ret == 2 ? wrongtxt : absenttxt), keytype, fingerprint);

    ret = messagebox(GTK_WIDGET(get_window(frontend)),
		     "PuTTY Security Alert", text,
		     string_width(fingerprint),
		     "Accept", 'a', 0, 2,
		     "Connect Once", 'o', 0, 1,
		     "Cancel", 'c', -1, 0,
		     NULL);

    sfree(text);

    if (ret == 2) {
	store_host_key(host, port, keytype, keystr);
	return 1;		       /* continue with connection */
    } else if (ret == 1)
	return 1;		       /* continue with connection */
    return 0;			       /* do not continue with connection */
}

/*
 * Ask whether the selected algorithm is acceptable (since it was
 * below the configured 'warn' threshold).
 */
int askalg(void *frontend, const char *algtype, const char *algname,
	   void (*callback)(void *ctx, int result), void *ctx)
{
    static const char msg[] =
	"The first %s supported by the server is "
	"%s, which is below the configured warning threshold.\n"
	"Continue with connection?";
    char *text;
    int ret;

    text = dupprintf(msg, algtype, algname);
    ret = messagebox(GTK_WIDGET(get_window(frontend)),
		     "PuTTY Security Alert", text,
		     string_width("Continue with connection?"),
		     "Yes", 'y', 0, 1,
		     "No", 'n', 0, 0,
		     NULL);
    sfree(text);

    if (ret) {
	return 1;
    } else {
	return 0;
    }
}

void old_keyfile_warning(void)
{
    /*
     * This should never happen on Unix. We hope.
     */
}

void fatal_message_box(void *window, char *msg)
{
    messagebox(window, "PuTTY Fatal Error", msg,
               string_width("REASONABLY LONG LINE OF TEXT FOR BASIC SANITY"),
               "OK", 'o', 1, 1, NULL);
}

void fatalbox(char *p, ...)
{
    va_list ap;
    char *msg;
    va_start(ap, p);
    msg = dupvprintf(p, ap);
    va_end(ap);
    fatal_message_box(NULL, msg);
    sfree(msg);
    cleanup_exit(1);
}

static GtkWidget *aboutbox = NULL;

static void about_close_clicked(GtkButton *button, gpointer data)
{
    gtk_widget_destroy(aboutbox);
    aboutbox = NULL;
}

static void licence_clicked(GtkButton *button, gpointer data)
{
    char *title;

    char *licence =
	"Copyright 1997-2007 Simon Tatham.\n\n"

	"Portions copyright Robert de Bath, Joris van Rantwijk, Delian "
	"Delchev, Andreas Schultz, Jeroen Massar, Wez Furlong, Nicolas "
	"Barry, Justin Bradford, Ben Harris, Malcolm Smith, Ahmad Khalifa, "
	"Markus Kuhn, and CORE SDI S.A.\n\n"

	"Permission is hereby granted, free of charge, to any person "
	"obtaining a copy of this software and associated documentation "
	"files (the ""Software""), to deal in the Software without restriction, "
	"including without limitation the rights to use, copy, modify, merge, "
	"publish, distribute, sublicense, and/or sell copies of the Software, "
	"and to permit persons to whom the Software is furnished to do so, "
	"subject to the following conditions:\n\n"

	"The above copyright notice and this permission notice shall be "
	"included in all copies or substantial portions of the Software.\n\n"

	"THE SOFTWARE IS PROVIDED ""AS IS"", WITHOUT "
	"WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, "
	"INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF "
	"MERCHANTABILITY, FITNESS FOR A PARTICULAR "
	"PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE "
	"COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES "
	"OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, "
	"TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN "
	"CONNECTION WITH THE SOFTWARE OR THE USE OR "
	"OTHER DEALINGS IN THE SOFTWARE.";

    title = dupcat(appname, " Licence", NULL);
    assert(aboutbox != NULL);
    messagebox(aboutbox, title, licence,
	       string_width("LONGISH LINE OF TEXT SO THE LICENCE"
			    " BOX ISN'T EXCESSIVELY TALL AND THIN"),
	       "OK", 'o', 1, 1, NULL);
    sfree(title);
}

void about_box(void *window)
{
    GtkWidget *w;
    char *title;

    if (aboutbox) {
        gtk_widget_grab_focus(aboutbox);
	return;
    }

    aboutbox = gtk_dialog_new();
    gtk_container_set_border_width(GTK_CONTAINER(aboutbox), 10);
    title = dupcat("About ", appname, NULL);
    gtk_window_set_title(GTK_WINDOW(aboutbox), title);
    sfree(title);

    w = gtk_button_new_with_label("Close");
    GTK_WIDGET_SET_FLAGS(w, GTK_CAN_DEFAULT);
    gtk_window_set_default(GTK_WINDOW(aboutbox), w);
    gtk_box_pack_end(GTK_BOX(GTK_DIALOG(aboutbox)->action_area),
		     w, FALSE, FALSE, 0);
    gtk_signal_connect(GTK_OBJECT(w), "clicked",
		       GTK_SIGNAL_FUNC(about_close_clicked), NULL);
    gtk_widget_show(w);

    w = gtk_button_new_with_label("View Licence");
    GTK_WIDGET_SET_FLAGS(w, GTK_CAN_DEFAULT);
    gtk_box_pack_end(GTK_BOX(GTK_DIALOG(aboutbox)->action_area),
		     w, FALSE, FALSE, 0);
    gtk_signal_connect(GTK_OBJECT(w), "clicked",
		       GTK_SIGNAL_FUNC(licence_clicked), NULL);
    gtk_widget_show(w);

    w = gtk_label_new(appname);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(aboutbox)->vbox),
		       w, FALSE, FALSE, 0);
    gtk_widget_show(w);

    w = gtk_label_new(ver);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(aboutbox)->vbox),
		       w, FALSE, FALSE, 5);
    gtk_widget_show(w);

    w = gtk_label_new("Copyright 1997-2007 Simon Tatham. All rights reserved");
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(aboutbox)->vbox),
		       w, FALSE, FALSE, 5);
    gtk_widget_show(w);

    set_transient_window_pos(GTK_WIDGET(window), aboutbox);
    gtk_widget_show(aboutbox);
}

struct eventlog_stuff {
    GtkWidget *parentwin, *window;
    struct controlbox *eventbox;
    struct Shortcuts scs;
    struct dlgparam dp;
    union control *listctrl;
    char **events;
    int nevents, negsize;
    char *seldata;
    int sellen;
    int ignore_selchange;
};

static void eventlog_destroy(GtkWidget *widget, gpointer data)
{
    struct eventlog_stuff *es = (struct eventlog_stuff *)data;

    es->window = NULL;
    sfree(es->seldata);
    dlg_cleanup(&es->dp);
    ctrl_free_box(es->eventbox);
}
static void eventlog_ok_handler(union control *ctrl, void *dlg,
				void *data, int event)
{
    if (event == EVENT_ACTION)
	dlg_end(dlg, 0);
}
static void eventlog_list_handler(union control *ctrl, void *dlg,
				  void *data, int event)
{
    struct eventlog_stuff *es = (struct eventlog_stuff *)data;

    if (event == EVENT_REFRESH) {
	int i;

	dlg_update_start(ctrl, dlg);
	dlg_listbox_clear(ctrl, dlg);
	for (i = 0; i < es->nevents; i++) {
	    dlg_listbox_add(ctrl, dlg, es->events[i]);
	}
	dlg_update_done(ctrl, dlg);
    } else if (event == EVENT_SELCHANGE) {
        int i;
        int selsize = 0;

        /*
         * If this SELCHANGE event is happening as a result of
         * deliberate deselection because someone else has grabbed
         * the selection, the last thing we want to do is pre-empt
         * them.
         */
        if (es->ignore_selchange)
            return;

        /*
         * Construct the data to use as the selection.
         */
        sfree(es->seldata);
        es->seldata = NULL;
        es->sellen = 0;
        for (i = 0; i < es->nevents; i++) {
            if (dlg_listbox_issel(ctrl, dlg, i)) {
                int extralen = strlen(es->events[i]);

                if (es->sellen + extralen + 2 > selsize) {
                    selsize = es->sellen + extralen + 512;
                    es->seldata = sresize(es->seldata, selsize, char);
                }

                strcpy(es->seldata + es->sellen, es->events[i]);
                es->sellen += extralen;
                es->seldata[es->sellen++] = '\n';
            }
        }

        if (gtk_selection_owner_set(es->window, GDK_SELECTION_PRIMARY,
                                    GDK_CURRENT_TIME)) {
            extern GdkAtom compound_text_atom;

            gtk_selection_add_target(es->window, GDK_SELECTION_PRIMARY,
                                     GDK_SELECTION_TYPE_STRING, 1);
            gtk_selection_add_target(es->window, GDK_SELECTION_PRIMARY,
                                     compound_text_atom, 1);
        }

    }
}

void eventlog_selection_get(GtkWidget *widget, GtkSelectionData *seldata,
                            guint info, guint time_stamp, gpointer data)
{
    struct eventlog_stuff *es = (struct eventlog_stuff *)data;

    gtk_selection_data_set(seldata, seldata->target, 8,
                           (unsigned char *)es->seldata, es->sellen);
}

gint eventlog_selection_clear(GtkWidget *widget, GdkEventSelection *seldata,
                              gpointer data)
{
    struct eventlog_stuff *es = (struct eventlog_stuff *)data;
    struct uctrl *uc;

    /*
     * Deselect everything in the list box.
     */
    uc = dlg_find_byctrl(&es->dp, es->listctrl);
    es->ignore_selchange = 1;
    gtk_list_unselect_all(GTK_LIST(uc->list));
    es->ignore_selchange = 0;

    sfree(es->seldata);
    es->sellen = 0;
    es->seldata = NULL;
    return TRUE;
}

void showeventlog(void *estuff, void *parentwin)
{
    struct eventlog_stuff *es = (struct eventlog_stuff *)estuff;
    GtkWidget *window, *w0, *w1;
    GtkWidget *parent = GTK_WIDGET(parentwin);
    struct controlset *s0, *s1;
    union control *c;
    int listitemheight, index;
    char *title;

    if (es->window) {
        gtk_widget_grab_focus(es->window);
	return;
    }

    dlg_init(&es->dp);

    for (index = 0; index < lenof(es->scs.sc); index++) {
	es->scs.sc[index].action = SHORTCUT_EMPTY;
    }

    es->eventbox = ctrl_new_box();

    s0 = ctrl_getset(es->eventbox, "", "", "");
    ctrl_columns(s0, 3, 33, 34, 33);
    c = ctrl_pushbutton(s0, "Close", 'c', HELPCTX(no_help),
			eventlog_ok_handler, P(NULL));
    c->button.column = 1;
    c->button.isdefault = TRUE;

    s1 = ctrl_getset(es->eventbox, "x", "", "");
    es->listctrl = c = ctrl_listbox(s1, NULL, NO_SHORTCUT, HELPCTX(no_help),
				    eventlog_list_handler, P(es));
    c->listbox.height = 10;
    c->listbox.multisel = 2;
    c->listbox.ncols = 3;
    c->listbox.percentages = snewn(3, int);
    c->listbox.percentages[0] = 25;
    c->listbox.percentages[1] = 10;
    c->listbox.percentages[2] = 65;

    listitemheight = get_listitemheight();

    es->window = window = gtk_dialog_new();
    title = dupcat(appname, " Event Log", NULL);
    gtk_window_set_title(GTK_WINDOW(window), title);
    sfree(title);
    w0 = layout_ctrls(&es->dp, &es->scs, s0,
		      listitemheight, GTK_WINDOW(window));
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(window)->action_area),
		       w0, TRUE, TRUE, 0);
    gtk_widget_show(w0);
    w1 = layout_ctrls(&es->dp, &es->scs, s1,
		      listitemheight, GTK_WINDOW(window));
    gtk_container_set_border_width(GTK_CONTAINER(w1), 10);
    gtk_widget_set_usize(w1, 20 +
			 string_width("LINE OF TEXT GIVING WIDTH OF EVENT LOG"
				      " IS QUITE LONG 'COS SSH LOG ENTRIES"
				      " ARE WIDE"), -1);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(window)->vbox),
		       w1, TRUE, TRUE, 0);
    gtk_widget_show(w1);

    es->dp.data = es;
    es->dp.shortcuts = &es->scs;
    es->dp.lastfocus = NULL;
    es->dp.retval = 0;
    es->dp.window = window;

    dlg_refresh(NULL, &es->dp);

    if (parent) {
        set_transient_window_pos(parent, window);
	gtk_window_set_transient_for(GTK_WINDOW(window),
				     GTK_WINDOW(parent));
    } else
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_widget_show(window);

    gtk_signal_connect(GTK_OBJECT(window), "destroy",
		       GTK_SIGNAL_FUNC(eventlog_destroy), es);
    gtk_signal_connect(GTK_OBJECT(window), "key_press_event",
		       GTK_SIGNAL_FUNC(win_key_press), &es->dp);
    gtk_signal_connect(GTK_OBJECT(window), "selection_get",
		       GTK_SIGNAL_FUNC(eventlog_selection_get), es);
    gtk_signal_connect(GTK_OBJECT(window), "selection_clear_event",
		       GTK_SIGNAL_FUNC(eventlog_selection_clear), es);
}

void *eventlogstuff_new(void)
{
    struct eventlog_stuff *es;
    es = snew(struct eventlog_stuff);
    memset(es, 0, sizeof(*es));
    return es;
}

void logevent_dlg(void *estuff, const char *string)
{
    struct eventlog_stuff *es = (struct eventlog_stuff *)estuff;

    char timebuf[40];
    struct tm tm;

    if (es->nevents >= es->negsize) {
	es->negsize += 64;
	es->events = sresize(es->events, es->negsize, char *);
    }

    tm=ltime();
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S\t", &tm);

    es->events[es->nevents] = snewn(strlen(timebuf) + strlen(string) + 1, char);
    strcpy(es->events[es->nevents], timebuf);
    strcat(es->events[es->nevents], string);
    if (es->window) {
	dlg_listbox_add(es->listctrl, &es->dp, es->events[es->nevents]);
    }
    es->nevents++;
}

int askappend(void *frontend, Filename filename,
	      void (*callback)(void *ctx, int result), void *ctx)
{
    static const char msgtemplate[] =
	"The session log file \"%.*s\" already exists. "
	"You can overwrite it with a new session log, "
	"append your session log to the end of it, "
	"or disable session logging for this session.";
    char *message;
    char *mbtitle;
    int mbret;

    message = dupprintf(msgtemplate, FILENAME_MAX, filename.path);
    mbtitle = dupprintf("%s Log to File", appname);

    mbret = messagebox(get_window(frontend), mbtitle, message,
		       string_width("LINE OF TEXT SUITABLE FOR THE"
				    " ASKAPPEND WIDTH"),
		       "Overwrite", 'o', 1, 2,
		       "Append", 'a', 0, 1,
		       "Disable", 'd', -1, 0,
		       NULL);

    sfree(message);
    sfree(mbtitle);

    return mbret;
}
