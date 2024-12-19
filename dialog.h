/*
 * Exports and types from dialog.c.
 */

/*
 * This is the big union which defines a single control, of any
 * type.
 *
 * General principles:
 *  - _All_ pointers in this structure are expected to point to
 *    dynamically allocated things, unless otherwise indicated.
 *  - `char' fields giving keyboard shortcuts are expected to be
 *    NO_SHORTCUT if no shortcut is desired for a particular control.
 *  - The `label' field can often be NULL, which will cause the
 *    control to not have a label at all. This doesn't apply to
 *    checkboxes and push buttons, in which the label is not
 *    separate from the control.
 */

#define NO_SHORTCUT '\0'

enum {
    CTRL_TEXT,                         /* just a static line of text */
    CTRL_EDITBOX,                      /* label plus edit box */
    CTRL_RADIO,                        /* label plus radio buttons */
    CTRL_CHECKBOX,                     /* checkbox (contains own label) */
    CTRL_BUTTON,                       /* simple push button (no label) */
    CTRL_LISTBOX,                      /* label plus list box */
    CTRL_COLUMNS,                      /* divide window into columns */
    CTRL_FILESELECT,                   /* label plus filename selector */
    CTRL_FONTSELECT,                   /* label plus font selector */
    CTRL_TABDELAY                      /* see `tabdelay' below */
};

/*
 * Many controls have `intorptr' unions for storing user data,
 * since the user might reasonably want to store either an integer
 * or a void * pointer. Here I define a union, and two convenience
 * functions to create that union from actual integers or pointers.
 *
 * The convenience functions are declared as inline if possible.
 * Otherwise, they're declared here and defined when this header is
 * included with DEFINE_INTORPTR_FNS defined. This is a total pain,
 * but such is life.
 */
typedef union { void *p; const void *cp; int i; } intorptr;

#ifndef INLINE
intorptr I(int i);
intorptr P(void *p);
intorptr CP(const void *p);
#endif

#if defined DEFINE_INTORPTR_FNS || defined INLINE
#ifdef INLINE
#define PREFIX INLINE
#else
#define PREFIX
#endif
PREFIX intorptr I(int i) { intorptr ret; ret.i = i; return ret; }
PREFIX intorptr P(void *p) { intorptr ret; ret.p = p; return ret; }
PREFIX intorptr CP(const void *p) { intorptr ret; ret.cp = p; return ret; }
#undef PREFIX
#endif

/*
 * Each control has an `int' field specifying which columns it
 * occupies in a multi-column part of the dialog box. These macros
 * pack and unpack that field.
 *
 * If a control belongs in exactly one column, just specifying the
 * column number is perfectly adequate.
 */
#define COLUMN_FIELD(start, span) ( (((span)-1) << 16) + (start) )
#define COLUMN_START(field) ( (field) & 0xFFFF )
#define COLUMN_SPAN(field) ( (((field) >> 16) & 0xFFFF) + 1 )

/*
 * The number of event types is being deliberately kept small, on
 * the grounds that not all platforms might be able to report a
 * large number of subtle events. We have:
 *  - the special REFRESH event, called when a control's value
 *    needs setting
 *  - the ACTION event, called when the user does something that
 *    positively requests action (double-clicking a list box item,
 *    or pushing a push-button)
 *  - the VALCHANGE event, called when the user alters the setting
 *    of the control in a way that is usually considered to alter
 *    the underlying data (toggling a checkbox or radio button,
 *    moving the items around in a drag-list, editing an edit
 *    control)
 *  - the SELCHANGE event, called when the user alters the setting
 *    of the control in a more minor way (changing the selected
 *    item in a list box).
 *  - the CALLBACK event, which happens after the handler routine
 *    has requested a subdialog (file selector, font selector,
 *    colour selector) and it has come back with information.
 */
enum {
    EVENT_REFRESH,
    EVENT_ACTION,
    EVENT_VALCHANGE,
    EVENT_SELCHANGE,
    EVENT_CALLBACK
};
typedef void (*handler_fn)(dlgcontrol *ctrl, dlgparam *dp,
                           void *data, int event);

struct dlgcontrol {
    /*
     * Generic fields shared by all the control types.
     */
    int type;
    /*
     * Every control except CTRL_COLUMNS has _some_ sort of label. By
     * putting it in the `generic' union as well as everywhere else,
     * we avoid having to have an irritating switch statement when we
     * go through and deallocate all the memory in a config-box
     * structure.
     *
     * Yes, this does mean that any non-NULL value in this field is
     * expected to be dynamically allocated and freeable.
     *
     * For CTRL_COLUMNS, this field MUST be NULL.
     */
    char *label;
    /*
     * If `delay_taborder' is true, it indicates that this particular
     * control should not yet appear in the tab order. A subsequent
     * CTRL_TABDELAY entry will place it.
     */
    bool delay_taborder;
    /*
     * Indicate which column(s) this control occupies. This can be
     * unpacked into starting column and column span by the COLUMN
     * macros above.
     */
    int column;
    /*
     * Most controls need to provide a function which gets called when
     * that control's setting is changed, or when the control's
     * setting needs initialising.
     *
     * The `data' parameter points to the writable data being modified
     * as a result of the configuration activity; for example, the
     * PuTTY `Conf' structure, although not necessarily.
     *
     * The `dlg' parameter is passed back to the platform- specific
     * routines to read and write the actual control state.
     */
    handler_fn handler;
    /*
     * Almost all of the above functions will find it useful to be
     * able to store one or two pieces of `void *' or `int' data.
     */
    intorptr context, context2;
    /*
     * For any control, we also allow the storage of a piece of data
     * for use by context-sensitive help. For example, on Windows you
     * can click the magic question mark and then click a control, and
     * help for that control should spring up. Hence, here is a slot
     * in which to store per-control data that a particular
     * platform-specific driver can use to ensure it brings up the
     * right piece of help text.
     */
    HelpCtx helpctx;
    /*
     * Setting this to non-NULL coerces two or more controls to have
     * their y-coordinates adjusted so that they can sit alongside
     * each other and look nicely aligned, even if they're different
     * heights.
     *
     * Set this field on later controls (in terms of order in the data
     * structure), pointing back to earlier ones, so that when each
     * control is instantiated, the referred-to one is already there
     * to be referred to.
     *
     * Don't expect this to change the position of the _first_
     * control. Currently, the layout is done one control at a time,
     * so that once the first control has been placed, the second one
     * can't cause the first one to be retrospectively moved.
     */
    dlgcontrol *align_next_to;

    /*
     * Union of further fields specific to each control type.
     */
    union {
        struct { /* for CTRL_TABDELAY */
            dlgcontrol *ctrl;
        } tabdelay;
        struct { /* for CTRL_EDITBOX */
            char shortcut;                 /* keyboard shortcut */
            /*
             * Percentage of the dialog-box width used by the edit
             * box. If this is set to 100, the label is on its own
             * line; otherwise the label is on the same line as the
             * box itself.
             */
            int percentwidth;
            bool password;               /* details of input are hidden */
            /*
             * A special case of the edit box is the combo box, which
             * has a drop-down list built in. (Note that a _non_-
             * editable drop-down list is done as a special case of a
             * list box.)
             *
             * Don't try setting has_list and password on the same
             * control; front ends are not required to support that
             * combination.
             */
            bool has_list;
        } editbox;
        struct { /* for CTRL_RADIO */
            /*
             * `shortcut' here is a single keyboard shortcut which is
             * expected to select the whole group of radio buttons. It
             * can be NO_SHORTCUT if required, and there is also a way
             * to place individual shortcuts on each button; see
             * below.
             */
            char shortcut;
            /*
             * There are separate fields for `ncolumns' and `nbuttons'
             * for several reasons.
             *
             * Firstly, we sometimes want the last of a set of buttons
             * to have a longer label than the rest; we achieve this
             * by setting `ncolumns' higher than `nbuttons', and the
             * layout code is expected to understand that the final
             * button should be given all the remaining space on the
             * line. This sounds like a ludicrously specific special
             * case (if we're doing this sort of thing, why not have
             * the general ability to have a particular button span
             * more than one column whether it's the last one or not?)
             * but actually it's reasonably common for the sort of
             * three-way control you get a lot of in PuTTY: `yes'
             * versus `no' versus `some more complex way to decide'.
             *
             * Secondly, setting `nbuttons' higher than `ncolumns'
             * lets us have more than one line of radio buttons for a
             * single setting. A very important special case of this
             * is setting `ncolumns' to 1, so that each button is on
             * its own line.
             */
            int ncolumns;
            int nbuttons;
            /*
             * This points to a dynamically allocated array of `char *'
             * pointers, each of which points to a dynamically
             * allocated string.
             */
            char **buttons;            /* `nbuttons' button labels */
            /*
             * This points to a dynamically allocated array of `char'
             * giving the individual keyboard shortcuts for each radio
             * button. The array may be NULL if none are required.
             */
            char *shortcuts;   /* `nbuttons' shortcuts; may be NULL */
            /*
             * This points to a dynamically allocated array of
             * intorptr, giving helpful data for each button.
             */
            intorptr *buttondata; /* `nbuttons' entries; may be NULL */
        } radio;
        struct { /* for CTRL_CHECKBOX */
            char shortcut;
        } checkbox;
        struct { /* for CTRL_BUTTON */
            char shortcut;
            /*
             * At least Windows has the concept of a `default push
             * button', which gets implicitly pressed when you hit
             * Return even if it doesn't have the input focus.
             */
            bool isdefault;
            /*
             * Also, the reverse of this: a default cancel-type
             * button, which is implicitly pressed when you hit
             * Escape.
             */
            bool iscancel;
        } button;
        struct { /* for CTRL_LISTBOX */
            char shortcut;                 /* keyboard shortcut */
            /*
             * Height of the list box, in approximate number of lines.
             * If this is zero, the list is a drop-down list.
             */
            int height;                    /* height in lines */
            /*
             * If this is set, the list elements can be reordered by
             * the user (by drag-and-drop or by Up and Down buttons,
             * whatever the per-platform implementation feels
             * comfortable with). This is not guaranteed to work on a
             * drop-down list, so don't try it!
             */
            bool draglist;
            /*
             * If this is non-zero, the list can have more than one
             * element selected at a time. This is not guaranteed to
             * work on a drop-down list, so don't try it!
             *
             * Different non-zero values request slightly different
             * types of multi-selection (this may well be meaningful
             * only in GTK, so everyone else can ignore it if they
             * want). 1 means the list box expects to have individual
             * items selected, whereas 2 means it expects the user to
             * want to select a large contiguous range at a time.
             */
            int multisel;
            /*
             * Percentage of the dialog-box width used by the list
             * box. If this is set to 100, the label is on its own
             * line; otherwise the label is on the same line as the
             * box itself. Setting this to anything other than 100 is
             * not guaranteed to work on a _non_-drop-down list, so
             * don't try it!
             */
            int percentwidth;
            /*
             * Some list boxes contain strings that contain tab
             * characters. If `ncols' is greater than 0, then
             * `percentages' is expected to be non-zero and to contain
             * the respective widths of `ncols' columns, which
             * together will exactly fit the width of the list box.
             * Otherwise `percentages' must be NULL.
             *
             * There should never be more than one column in a
             * drop-down list (one with height==0), because front ends
             * may have to implement it as a special case of an
             * editable combo box.
             */
            int ncols;                     /* number of columns */
            int *percentages;              /* % width of each column */
            /*
             * Flag which can be set to false to suppress the
             * horizontal scroll bar if a list box entry goes off the
             * right-hand side.
             */
            bool hscroll;
        } listbox;
        struct { /* for CTRL_FILESELECT */
            char shortcut;
            /*
             * `filter' dictates what type of files will be selected
             * by default; for example, when selecting private key
             * files the file selector would do well to only show .PPK
             * files (on those systems where this is the chosen
             * extension).
             *
             * The precise contents of `filter' are platform-defined,
             * unfortunately. The special value NULL means `all files'
             * and is always a valid fallback.
             *
             * Unlike almost all strings in this structure, this value
             * is NOT expected to require freeing (although of course
             * you can always use ctrl_alloc if you do need to create
             * one on the fly). This is because the likely mode of use
             * is to define string constants in a platform-specific
             * header file, and directly reference those. Or worse, a
             * particular platform might choose to cast integers into
             * this pointer type...
             */
            FILESELECT_FILTER_TYPE filter;
            /*
             * Some systems like to know whether a file selector is
             * choosing a file to read or one to write (and possibly
             * create).
             */
            bool for_writing;
            /*
             * On at least some platforms, the file selector is a
             * separate dialog box, and contains a user-settable
             * title.
             *
             * This value _is_ expected to require freeing.
             */
            char *title;
            /*
             * Reduce the file selector to just a single browse
             * button.
             *
             * Normally, a file selector is used to set a config
             * option that consists of a file name, so that that file
             * will be read or written at run time. In that situation,
             * it makes sense to have an edit box showing the
             * currently selected file name, and a button to change it
             * interactively.
             *
             * But occasionally a file selector is used to load a file
             * _during_ configuration. For example, host CA public
             * keys are entered directly into the configuration as
             * strings, not stored by reference to a filename; but if
             * you have one in a file, you want to be able to load it
             * during the lifetime of the CA config box rather than
             * awkwardly copy-pasting it. So in that case you just
             * want a 'pop up a file chooser' button, and when that
             * delivers a file name, you'll deal with it there and
             * then and write some other thing (like the file's
             * contents) into a nearby edit box.
             *
             * If you set this flag, then you may not call
             * dlg_filesel_set on the file selector at all, because it
             * doesn't store a filename. And you can only call
             * dlg_filesel_get on it in the handler for EVENT_ACTION,
             * which is what will be sent to you when the user has
             * used it to choose a filename.
             */
            bool just_button;
        } fileselect;
        struct { /* for CTRL_COLUMNS */
            /* In this variant, `label' MUST be NULL. */
            int ncols;                     /* number of columns */
            int *percentages;              /* % width of each column */
            /*
             * Every time this control type appears, exactly one of
             * `ncols' and the previous number of columns MUST be one.
             * Attempting to allow a seamless transition from a four-
             * to a five-column layout, for example, would be way more
             * trouble than it was worth. If you must lay things out
             * like that, define eight unevenly sized columns and use
             * column-spanning a lot. But better still, just don't.
             *
             * `percentages' may be NULL if ncols==1, to save space.
             */
        } columns;
        struct { /* for CTRL_FONTSELECT */
            char shortcut;
        } fontselect;
        struct { /* for CTRL_TEXT */
            /*
             * If this is true (the default), the text will wrap on to
             * multiple lines. If false, it will stay on the same
             * line, with a horizontal scrollbar if necessary.
             */
            bool wrap;
        } text;
    };
};

#undef STANDARD_PREFIX

/*
 * `controlset' is a container holding an array of `dlgcontrol'
 * structures, together with a panel name and a title for the whole
 * set. In Windows and any similar-looking GUI, each `controlset'
 * in the config will be a container box within a panel.
 *
 * Special case: if `boxname' is NULL, the control set gives an
 * overall title for an entire panel of controls.
 */
struct controlset {
    char *pathname;                    /* panel path, e.g. "SSH/Tunnels" */
    char *boxname;                     /* internal short name of controlset */
    char *boxtitle;                    /* title of container box */
    int ncolumns;                      /* current no. of columns at bottom */
    size_t ncontrols;                  /* number of `dlgcontrol' in array */
    size_t ctrlsize;                   /* allocated size of array */
    dlgcontrol **ctrls;                /* actual array */
};

typedef void (*ctrl_freefn_t)(void *);    /* used by ctrl_alloc_with_free */

/*
 * This is the container structure which holds a complete set of
 * controls.
 */
struct controlbox {
    size_t nctrlsets;                  /* number of ctrlsets */
    size_t ctrlsetsize;                /* ctrlset size */
    struct controlset **ctrlsets;      /* actual array of ctrlsets */
    size_t nfrees;
    size_t freesize;
    void **frees;                      /* array of aux data areas to free */
    ctrl_freefn_t *freefuncs;          /* parallel array of free functions */
};

struct controlbox *ctrl_new_box(void);
void ctrl_free_box(struct controlbox *);

/*
 * Standard functions used for populating a controlbox structure.
 */

/* Set up a panel title. */
struct controlset *ctrl_settitle(struct controlbox *,
                                 const char *path, const char *title);
/* Retrieve a pointer to a controlset, creating it if absent. */
struct controlset *ctrl_getset(struct controlbox *, const char *path,
                               const char *name, const char *boxtitle);
void ctrl_free_set(struct controlset *);

void ctrl_free(dlgcontrol *);

/*
 * This function works like `malloc', but the memory it returns
 * will be automatically freed when the controlbox is freed. Note
 * that a controlbox is a dialog-box _template_, not an instance,
 * and so data allocated through this function is better not used
 * to hold modifiable per-instance things. It's mostly here for
 * allocating structures to be passed as control handler params.
 *
 * ctrl_alloc_with_free also allows you to provide a function to free
 * the structure, in case there are other dynamically allocated bits
 * and pieces dangling off it.
 */
void *ctrl_alloc(struct controlbox *b, size_t size);
void *ctrl_alloc_with_free(struct controlbox *b, size_t size,
                           ctrl_freefn_t freefunc);

/*
 * Individual routines to create `dlgcontrol' structures in a controlset.
 *
 * Most of these routines allow the most common fields to be set
 * directly, and put default values in the rest. Each one returns a
 * pointer to the `dlgcontrol' it created, so that final tweaks
 * can be made.
 */

/* `ncolumns' is followed by that many percentages, as integers. */
dlgcontrol *ctrl_columns(struct controlset *, int ncolumns, ...);
dlgcontrol *ctrl_editbox(struct controlset *, const char *label,
                         char shortcut, int percentage, HelpCtx helpctx,
                         handler_fn handler,
                         intorptr context, intorptr context2);
dlgcontrol *ctrl_combobox(struct controlset *, const char *label,
                          char shortcut, int percentage, HelpCtx helpctx,
                          handler_fn handler,
                          intorptr context, intorptr context2);
/*
 * `ncolumns' is followed by (alternately) radio button titles and
 * intorptrs, until a NULL in place of a title string is seen. Each
 * title is expected to be followed by a shortcut _iff_ `shortcut'
 * is NO_SHORTCUT.
 */
dlgcontrol *ctrl_radiobuttons_fn(struct controlset *, const char *label,
                                 char shortcut, int ncolumns, HelpCtx helpctx,
                                 handler_fn handler, intorptr context, ...);
#define ctrl_radiobuttons(...) \
    ctrl_radiobuttons_fn(__VA_ARGS__, (const char *)NULL)
dlgcontrol *ctrl_pushbutton(struct controlset *, const char *label,
                            char shortcut, HelpCtx helpctx,
                            handler_fn handler, intorptr context);
dlgcontrol *ctrl_listbox(struct controlset *, const char *label,
                         char shortcut, HelpCtx helpctx,
                         handler_fn handler, intorptr context);
dlgcontrol *ctrl_droplist(struct controlset *, const char *label,
                          char shortcut, int percentage, HelpCtx helpctx,
                          handler_fn handler, intorptr context);
dlgcontrol *ctrl_draglist(struct controlset *, const char *label,
                          char shortcut, HelpCtx helpctx,
                          handler_fn handler, intorptr context);
dlgcontrol *ctrl_filesel(struct controlset *, const char *label,
                         char shortcut, FILESELECT_FILTER_TYPE filter,
                         bool write, const char *title, HelpCtx helpctx,
                         handler_fn handler, intorptr context);
dlgcontrol *ctrl_fontsel(struct controlset *, const char *label,
                         char shortcut, HelpCtx helpctx,
                         handler_fn handler, intorptr context);
dlgcontrol *ctrl_text(struct controlset *, const char *text,
                      HelpCtx helpctx);
dlgcontrol *ctrl_checkbox(struct controlset *, const char *label,
                          char shortcut, HelpCtx helpctx,
                          handler_fn handler, intorptr context);
dlgcontrol *ctrl_tabdelay(struct controlset *, dlgcontrol *);

/*
 * Routines the platform-independent dialog code can call to read
 * and write the values of controls.
 */
void dlg_radiobutton_set(dlgcontrol *ctrl, dlgparam *dp, int whichbutton);
int dlg_radiobutton_get(dlgcontrol *ctrl, dlgparam *dp);
void dlg_checkbox_set(dlgcontrol *ctrl, dlgparam *dp, bool checked);
bool dlg_checkbox_get(dlgcontrol *ctrl, dlgparam *dp);
void dlg_editbox_set(dlgcontrol *ctrl, dlgparam *dp, char const *text);
void dlg_editbox_set_utf8(dlgcontrol *ctrl, dlgparam *dp, char const *text);
char *dlg_editbox_get(dlgcontrol *ctrl, dlgparam *dp);   /* result must be freed by caller */
char *dlg_editbox_get_utf8(dlgcontrol *ctrl, dlgparam *dp);   /* result must be freed by caller */
void dlg_editbox_select_range(dlgcontrol *ctrl, dlgparam *dp,
                              size_t start, size_t len);
/* The `listbox' functions can also apply to combo boxes. */
void dlg_listbox_clear(dlgcontrol *ctrl, dlgparam *dp);
void dlg_listbox_del(dlgcontrol *ctrl, dlgparam *dp, int index);
void dlg_listbox_add(dlgcontrol *ctrl, dlgparam *dp, char const *text);
/*
 * Each listbox entry may have a numeric id associated with it.
 * Note that some front ends only permit a string to be stored at
 * each position, which means that _if_ you put two identical
 * strings in any listbox then you MUST not assign them different
 * IDs and expect to get meaningful results back.
 */
void dlg_listbox_addwithid(dlgcontrol *ctrl, dlgparam *dp,
                           char const *text, int id);
int dlg_listbox_getid(dlgcontrol *ctrl, dlgparam *dp, int index);
/* dlg_listbox_index returns <0 if no single element is selected. */
int dlg_listbox_index(dlgcontrol *ctrl, dlgparam *dp);
bool dlg_listbox_issel(dlgcontrol *ctrl, dlgparam *dp, int index);
void dlg_listbox_select(dlgcontrol *ctrl, dlgparam *dp, int index);
void dlg_text_set(dlgcontrol *ctrl, dlgparam *dp, char const *text);
void dlg_filesel_set(dlgcontrol *ctrl, dlgparam *dp, Filename *fn);
Filename *dlg_filesel_get(dlgcontrol *ctrl, dlgparam *dp);
void dlg_fontsel_set(dlgcontrol *ctrl, dlgparam *dp, FontSpec *fn);
FontSpec *dlg_fontsel_get(dlgcontrol *ctrl, dlgparam *dp);
/*
 * Bracketing a large set of updates in these two functions will
 * cause the front end (if possible) to delay updating the screen
 * until it's all complete, thus avoiding flicker.
 */
void dlg_update_start(dlgcontrol *ctrl, dlgparam *dp);
void dlg_update_done(dlgcontrol *ctrl, dlgparam *dp);
/*
 * Set input focus into a particular control.
 */
void dlg_set_focus(dlgcontrol *ctrl, dlgparam *dp);
/*
 * Change the label text on a control.
 */
void dlg_label_change(dlgcontrol *ctrl, dlgparam *dp, char const *text);
/*
 * Return the `ctrl' structure for the most recent control that had
 * the input focus apart from the one mentioned. This is NOT
 * GUARANTEED to work on all platforms, so don't base any critical
 * functionality on it!
 */
dlgcontrol *dlg_last_focused(dlgcontrol *ctrl, dlgparam *dp);
/*
 * Find out whether a particular control is currently visible.
 */
bool dlg_is_visible(dlgcontrol *ctrl, dlgparam *dp);
/*
 * During event processing, you might well want to give an error
 * indication to the user. dlg_beep() is a quick and easy generic
 * error; dlg_error() puts up a message-box or equivalent.
 */
void dlg_beep(dlgparam *dp);
void dlg_error_msg(dlgparam *dp, const char *msg);
/*
 * This function signals to the front end that the dialog's
 * processing is completed, and passes an integer value (typically
 * a success status).
 */
void dlg_end(dlgparam *dp, int value);

/*
 * Routines to manage a (per-platform) colour selector.
 * dlg_coloursel_start() is called in an event handler, and
 * schedules the running of a colour selector after the event
 * handler returns. The colour selector will send EVENT_CALLBACK to
 * the control that spawned it, when it's finished;
 * dlg_coloursel_results() fetches the results, as integers from 0
 * to 255; it returns nonzero on success, or zero if the colour
 * selector was dismissed by hitting Cancel or similar.
 *
 * dlg_coloursel_start() accepts an RGB triple which is used to
 * initialise the colour selector to its starting value.
 */
void dlg_coloursel_start(dlgcontrol *ctrl, dlgparam *dp,
                         int r, int g, int b);
bool dlg_coloursel_results(dlgcontrol *ctrl, dlgparam *dp,
                           int *r, int *g, int *b);

/*
 * This routine is used by the platform-independent code to
 * indicate that the value of a particular control is likely to
 * have changed. It triggers a call of the handler for that control
 * with `event' set to EVENT_REFRESH.
 *
 * If `ctrl' is NULL, _all_ controls in the dialog get refreshed
 * (for loading or saving entire sets of settings).
 */
void dlg_refresh(dlgcontrol *ctrl, dlgparam *dp);

/*
 * Standard helper functions for reading a controlbox structure.
 */

/*
 * Find the index of next controlset in a controlbox for a given
 * path, or -1 if no such controlset exists. If -1 is passed as
 * input, finds the first. Intended usage is something like
 *
 *      for (index=-1; (index=ctrl_find_path(ctrlbox, index, path)) >= 0 ;) {
 *          ... process this controlset ...
 *      }
 */
int ctrl_find_path(struct controlbox *b, const char *path, int index);
int ctrl_path_elements(const char *path);
/* Return the number of matching path elements at the starts of p1 and p2,
 * or INT_MAX if the paths are identical. */
int ctrl_path_compare(const char *p1, const char *p2);

/*
 * Normalise the align_next_to fields in a controlset so that they
 * form a backwards linked list.
 */
void ctrlset_normalise_aligns(struct controlset *s);
