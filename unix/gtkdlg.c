/*
 * gtkdlg.c - GTK implementation of the PuTTY configuration box.
 */

#include <assert.h>
#include <gtk/gtk.h>

#include "gtkcols.h"
#include "gtkpanel.h"

#ifdef TESTMODE
#define PUTTY_DO_GLOBALS	       /* actually _define_ globals */
#endif

#include "putty.h"
#include "dialog.h"


void *dlg_get_privdata(union control *ctrl, void *dlg)
{
    return NULL;		       /* FIXME */
}

void dlg_set_privdata(union control *ctrl, void *dlg, void *ptr)
{
    /* FIXME */
}

void *dlg_alloc_privdata(union control *ctrl, void *dlg, size_t size)
{
    return NULL;		       /* FIXME */
}

union control *dlg_last_focused(void *dlg)
{
    return NULL;                       /* FIXME */
}

void dlg_radiobutton_set(union control *ctrl, void *dlg, int whichbutton)
{
    /* FIXME */
}

int dlg_radiobutton_get(union control *ctrl, void *dlg)
{
    return 0;                          /* FIXME */
}

void dlg_checkbox_set(union control *ctrl, void *dlg, int checked)
{
    /* FIXME */
}

int dlg_checkbox_get(union control *ctrl, void *dlg)
{
    return 0;                          /* FIXME */
}

void dlg_editbox_set(union control *ctrl, void *dlg, char const *text)
{
    /* FIXME */
}

void dlg_editbox_get(union control *ctrl, void *dlg, char *buffer, int length)
{
    /* FIXME */
}

/* The `listbox' functions can also apply to combo boxes. */
void dlg_listbox_clear(union control *ctrl, void *dlg)
{
    /* FIXME */
}

void dlg_listbox_del(union control *ctrl, void *dlg, int index)
{
    /* FIXME */
}

void dlg_listbox_add(union control *ctrl, void *dlg, char const *text)
{
    /* FIXME */
}

/*
 * Each listbox entry may have a numeric id associated with it.
 * Note that some front ends only permit a string to be stored at
 * each position, which means that _if_ you put two identical
 * strings in any listbox then you MUST not assign them different
 * IDs and expect to get meaningful results back.
 */
void dlg_listbox_addwithindex(union control *ctrl, void *dlg,
			      char const *text, int id)
{
    /* FIXME */
}

int dlg_listbox_getid(union control *ctrl, void *dlg, int index)
{
    return -1;                         /* FIXME */
}

/* dlg_listbox_index returns <0 if no single element is selected. */
int dlg_listbox_index(union control *ctrl, void *dlg)
{
    return -1;                         /* FIXME */
}

int dlg_listbox_issel(union control *ctrl, void *dlg, int index)
{
    return 0;                          /* FIXME */
}

void dlg_listbox_select(union control *ctrl, void *dlg, int index)
{
    /* FIXME */
}

void dlg_text_set(union control *ctrl, void *dlg, char const *text)
{
    /* FIXME */
}

void dlg_filesel_set(union control *ctrl, void *dlg, Filename fn)
{
    /* FIXME */
}

void dlg_filesel_get(union control *ctrl, void *dlg, Filename *fn)
{
    /* FIXME */
}

void dlg_fontsel_set(union control *ctrl, void *dlg, FontSpec fs)
{
    /* FIXME */
}

void dlg_fontsel_get(union control *ctrl, void *dlg, FontSpec *fs)
{
    /* FIXME */
}

/*
 * Bracketing a large set of updates in these two functions will
 * cause the front end (if possible) to delay updating the screen
 * until it's all complete, thus avoiding flicker.
 */
void dlg_update_start(union control *ctrl, void *dlg)
{
    /* FIXME */
}

void dlg_update_done(union control *ctrl, void *dlg)
{
    /* FIXME */
}

void dlg_set_focus(union control *ctrl, void *dlg)
{
    /* FIXME */
}

/*
 * During event processing, you might well want to give an error
 * indication to the user. dlg_beep() is a quick and easy generic
 * error; dlg_error() puts up a message-box or equivalent.
 */
void dlg_beep(void *dlg)
{
    /* FIXME */
}

void dlg_error_msg(void *dlg, char *msg)
{
    /* FIXME */
}

/*
 * This function signals to the front end that the dialog's
 * processing is completed, and passes an integer value (typically
 * a success status).
 */
void dlg_end(void *dlg, int value)
{
    /* FIXME */
}

void dlg_refresh(union control *ctrl, void *dlg)
{
    /* FIXME */
}

void dlg_coloursel_start(union control *ctrl, void *dlg, int r, int g, int b)
{
    /* FIXME */
}

int dlg_coloursel_results(union control *ctrl, void *dlg,
			  int *r, int *g, int *b)
{
    return 0;                          /* FIXME */
}

/*
 * This function does the main layout work: it reads a controlset,
 * it creates the relevant GTK controls, and returns a GtkWidget
 * containing the result. (This widget might be a title of some
 * sort, it might be a Columns containing many controls, or it
 * might be a GtkFrame containing a Columns; whatever it is, it's
 * definitely a GtkWidget and should probably be added to a
 * GtkVbox.)
 */
GtkWidget *layout_ctrls(struct controlset *s)
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
            /* FIXME: we can do columns_taborder_last easily enough, but
             * we need to be able to remember which GtkWidget(s) correspond
             * to ctrl->tabdelay.ctrl. */
            continue;                  /* no actual control created */
          case CTRL_BUTTON:
            w = gtk_button_new_with_label(ctrl->generic.label);
            break;
          case CTRL_CHECKBOX:
            w = gtk_check_button_new_with_label(ctrl->generic.label);
            break;
          case CTRL_RADIO:
            /*
             * Radio buttons get to go inside their own Columns, no
             * matter what.
             */
            {
                gint i, *percentages;
                GSList *group;

                w = columns_new(1);
                if (ctrl->generic.label) {
                    GtkWidget *label = gtk_label_new(ctrl->generic.label);
                    columns_add(COLUMNS(w), label, 0, 1);
		    columns_force_left_align(COLUMNS(w), label);
                    gtk_widget_show(label);
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
                for (i = 0; i < ctrl->radio.nbuttons; i++) {
                    GtkWidget *b;
                    gint colstart;

                    b = (gtk_radio_button_new_with_label
                         (group, ctrl->radio.buttons[i]));
                    group = gtk_radio_button_group(GTK_RADIO_BUTTON(b));
                    colstart = i % ctrl->radio.ncolumns;
                    columns_add(COLUMNS(w), b, colstart,
                                (i == ctrl->radio.nbuttons-1 ?
                                 ctrl->radio.ncolumns - colstart : 1));
                    gtk_widget_show(b);
                }
            }
            break;
          case CTRL_EDITBOX:
            if (ctrl->editbox.has_list) {
                w = gtk_combo_new();
            } else {
                w = gtk_entry_new();
                if (ctrl->editbox.password)
                    gtk_entry_set_visibility(GTK_ENTRY(w), FALSE);
            }
            /*
             * Edit boxes, for some strange reason, have a minimum
             * width of 150 in GTK 1.2. We don't want this - we'd
             * rather the edit boxes acquired their natural width
             * from the column layout of the rest of the box.
             */
            {
                GtkRequisition req;
                gtk_widget_size_request(w, &req);
                gtk_widget_set_usize(w, 10, req.height);
            }
            if (ctrl->generic.label) {
                GtkWidget *label, *container;

                label = gtk_label_new(ctrl->generic.label);

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
                }
                gtk_widget_show(label);
                gtk_widget_show(w);

                w = container;
            }
            break;
          case CTRL_TEXT:
            w = gtk_label_new(ctrl->generic.label);
            gtk_label_set_line_wrap(GTK_LABEL(w), TRUE);
	    gtk_label_set_justify(GTK_LABEL(w), GTK_JUSTIFY_FILL);
            break;
        }
        if (w) {
            columns_add(cols, w,
                        COLUMN_START(ctrl->generic.column),
                        COLUMN_SPAN(ctrl->generic.column));
            gtk_widget_show(w);
        }
    }

    return ret;
}

struct selparam {
    Panels *panels;
    GtkWidget *panel, *treeitem;
};

static void treeitem_sel(GtkItem *item, gpointer data)
{
    struct selparam *sp = (struct selparam *)data;

    panels_switch_to(sp->panels, sp->panel);
}

void destroy(GtkWidget *widget, gpointer data)
{
    gtk_main_quit();
}

void do_config_box(void)
{
    GtkWidget *window, *hbox, *vbox, *cols, *label,
	*tree, *treescroll, *panels, *panelvbox;
    int index, level;
    struct controlbox *ctrlbox;
    char *path;
    GtkTreeItem *treeitemlevels[8];
    GtkTree *treelevels[8];
    Config cfg;

    struct selparam *selparams = NULL;
    int nselparams = 0, selparamsize = 0;

    do_defaults(NULL, &cfg);

    ctrlbox = ctrl_new_box();
    setup_config_box(ctrlbox, NULL, FALSE, 0);

    window = gtk_dialog_new();
    gtk_container_set_border_width(GTK_CONTAINER(GTK_DIALOG(window)->vbox), 4);
    hbox = gtk_hbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(window)->vbox), hbox, TRUE, TRUE, 0);
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
    gtk_tree_set_view_mode(GTK_TREE(tree), GTK_TREE_VIEW_ITEM);
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(treescroll),
					  tree);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(treescroll),
				   GTK_POLICY_NEVER,
				   GTK_POLICY_AUTOMATIC);
    gtk_widget_show(tree);
    gtk_widget_show(treescroll);
    gtk_box_pack_start(GTK_BOX(vbox), treescroll, TRUE, TRUE, 0);
    panels = panels_new();
    gtk_box_pack_start(GTK_BOX(hbox), panels, TRUE, TRUE, 0);
    gtk_widget_show(panels);

    panelvbox = NULL;
    path = NULL;
    level = 0;
    for (index = 0; index < ctrlbox->nctrlsets; index++) {
	struct controlset *s = ctrlbox->ctrlsets[index];
	GtkWidget *w = layout_ctrls(s);

	if (!*s->pathname) {
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

		gtk_widget_show(treeitem);

		path = s->pathname;

		first = (panelvbox == NULL);

		panelvbox = gtk_vbox_new(FALSE, 4);
		gtk_container_add(GTK_CONTAINER(panels), panelvbox);
		if (first) {
		    panels_switch_to(PANELS(panels), panelvbox);
		    gtk_tree_select_child(GTK_TREE(tree), treeitem);
		}

		if (nselparams >= selparamsize) {
		    selparamsize += 16;
		    selparams = srealloc(selparams,
					 selparamsize * sizeof(*selparams));
		}
		selparams[nselparams].panels = PANELS(panels);
		selparams[nselparams].panel = panelvbox;
		selparams[nselparams].treeitem = treeitem;
		nselparams++;

	    }

	    gtk_box_pack_start(GTK_BOX(panelvbox), w, FALSE, FALSE, 0);
	}
    }

    for (index = 0; index < nselparams; index++) {
	gtk_signal_connect(GTK_OBJECT(selparams[index].treeitem), "select",
			   GTK_SIGNAL_FUNC(treeitem_sel),
			   &selparams[index]);
    }

    gtk_widget_show(window);

    gtk_signal_connect(GTK_OBJECT(window), "destroy",
		       GTK_SIGNAL_FUNC(destroy), NULL);

    gtk_main();

    sfree(selparams);
}

/* ======================================================================
 * Below here is a stub main program which allows the dialog box
 * code to be compiled and tested with a minimal amount of the rest
 * of PuTTY.
 */

#ifdef TESTMODE

/* Compile command for testing:

   gcc -o gtkdlg gtk{dlg,cols,panel}.c ../{config,dialog,settings}.c \
                 ../{misc,tree234,be_none}.c ux{store,misc,print}.c \
                 -I. -I.. -I../charset -DTESTMODE `gtk-config --cflags --libs`
 */

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

char *cp_name(int codepage)
{
    return (codepage == 123 ? "testing123" :
            codepage == 234 ? "testing234" :
            codepage == 345 ? "testing345" :
            "unknown");
}

char *cp_enumerate(int index)
{
    return (index == 0 ? "testing123" :
            index == 1 ? "testing234" :
            NULL);
}

int decode_codepage(char *cp_name)
{
    return (!strcmp(cp_name, "testing123") ? 123 :
            !strcmp(cp_name, "testing234") ? 234 :
            !strcmp(cp_name, "testing345") ? 345 :
            -2);
}

struct printer_enum_tag { int dummy; } printer_test;

printer_enum *printer_start_enum(int *nprinters_ptr) {
    *nprinters_ptr = 2;
    return &printer_test;
}
char *printer_get_name(printer_enum *pe, int i) {
    return (i==0 ? "lpr" : i==1 ? "lpr -Pfoobar" : NULL);
}
void printer_finish_enum(printer_enum *pe) { }

char *platform_default_s(const char *name)
{
    return NULL;
}

int platform_default_i(const char *name, int def)
{
    return def;
}

FontSpec platform_default_fontspec(const char *name)
{
    FontSpec ret;
    *ret.name = '\0';
    return ret;
}

Filename platform_default_filename(const char *name)
{
    Filename ret;
    *ret.path = '\0';
    return ret;
}

char *x_get_default(const char *key)
{
    return NULL;
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    do_config_box();
    return 0;
}

#endif
