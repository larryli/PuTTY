/*
 * Define and handle the configuration dialog box for SSH host CAs,
 * using the same portable dialog specification API as config.c.
 */

#include "putty.h"
#include "dialog.h"
#include "storage.h"
#include "tree234.h"
#include "ssh.h"

const bool has_ca_config_box = true;

#define NRSATYPES 3

struct ca_state {
    dlgcontrol *ca_name_edit;
    dlgcontrol *ca_reclist;
    dlgcontrol *ca_pubkey_edit;
    dlgcontrol *ca_wclist;
    dlgcontrol *ca_wc_edit;
    dlgcontrol *rsa_type_checkboxes[NRSATYPES];
    char *name, *pubkey, *wc;
    tree234 *ca_names;                 /* stores plain 'char *' */
    tree234 *host_wcs;                 /* stores plain 'char *' */
    ca_options opts;
};

static int ca_name_compare(void *av, void *bv)
{
    return strcmp((const char *)av, (const char *)bv);
}

static inline void clear_string_tree(tree234 *t)
{
    char *p;
    while ((p = delpos234(t, 0)) != NULL)
        sfree(p);
}

static void ca_state_free(void *vctx)
{
    struct ca_state *st = (struct ca_state *)vctx;
    clear_string_tree(st->ca_names);
    freetree234(st->ca_names);
    clear_string_tree(st->host_wcs);
    freetree234(st->host_wcs);
    sfree(st->name);
    sfree(st->wc);
    sfree(st);
}

static void ca_refresh_name_list(struct ca_state *st)
{
    clear_string_tree(st->ca_names);

    host_ca_enum *hce = enum_host_ca_start();
    if (hce) {
        strbuf *namebuf = strbuf_new();

        while (strbuf_clear(namebuf), enum_host_ca_next(hce, namebuf)) {
            char *name = dupstr(namebuf->s);
            char *added = add234(st->ca_names, name);
            /* Just imaginable that concurrent filesystem access might
             * cause a repetition; avoid leaking memory if so */
            if (added != name)
                sfree(name);
        }

        strbuf_free(namebuf);
        enum_host_ca_finish(hce);
    }
}

static void set_from_hca(struct ca_state *st, host_ca *hca)
{
    sfree(st->name);
    st->name = dupstr(hca->name);

    sfree(st->pubkey);
    if (hca->ca_public_key)
        st->pubkey = strbuf_to_str(
            base64_encode_sb(ptrlen_from_strbuf(hca->ca_public_key), 0));
    else
        st->pubkey = dupstr("");

    clear_string_tree(st->host_wcs);
    for (size_t i = 0; i < hca->n_hostname_wildcards; i++) {
        char *name = dupstr(hca->hostname_wildcards[i]);
        char *added = add234(st->host_wcs, name);
        if (added != name)
            sfree(name);               /* de-duplicate, just in case */
    }

    st->opts = hca->opts;              /* structure copy */
}

static void ca_load_selected_record(struct ca_state *st, dlgparam *dp)
{
    int i = dlg_listbox_index(st->ca_reclist, dp);
    if (i < 0) {
        dlg_beep(dp);
        return;
    }
    const char *name = index234(st->ca_names, i);
    if (!name) { /* in case the list box and the tree got out of sync */
        dlg_beep(dp);
        return;
    }
    host_ca *hca = host_ca_load(name);
    if (!hca) {
        char *msg = dupprintf("Unable to load host CA record '%s'", name);
        dlg_error_msg(dp, msg);
        sfree(msg);
        return;
    }

    set_from_hca(st, hca);
    host_ca_free(hca);

    dlg_refresh(st->ca_name_edit, dp);
    dlg_refresh(st->ca_pubkey_edit, dp);
    dlg_refresh(st->ca_wclist, dp);
    for (size_t i = 0; i < NRSATYPES; i++)
        dlg_refresh(st->rsa_type_checkboxes[i], dp);
}

static void ca_ok_handler(dlgcontrol *ctrl, dlgparam *dp,
                          void *data, int event)
{
    if (event == EVENT_ACTION)
        dlg_end(dp, 0);
}

static void ca_name_handler(dlgcontrol *ctrl, dlgparam *dp,
                            void *data, int event)
{
    struct ca_state *st = (struct ca_state *)ctrl->context.p;
    if (event == EVENT_REFRESH) {
        dlg_editbox_set(ctrl, dp, st->name);
    } else if (event == EVENT_VALCHANGE) {
        sfree(st->name);
        st->name = dlg_editbox_get(ctrl, dp);

        /*
         * Try to auto-select the typed name in the list.
         */
        int index;
        if (!findrelpos234(st->ca_names, st->name, NULL, REL234_GE, &index))
            index = count234(st->ca_names) - 1;
        if (index >= 0)
            dlg_listbox_select(st->ca_reclist, dp, index);
    }
}

static void ca_reclist_handler(dlgcontrol *ctrl, dlgparam *dp,
                               void *data, int event)
{
    struct ca_state *st = (struct ca_state *)ctrl->context.p;
    if (event == EVENT_REFRESH) {
        dlg_update_start(ctrl, dp);
        dlg_listbox_clear(ctrl, dp);
        const char *name;
        for (int i = 0; (name = index234(st->ca_names, i)) != NULL; i++)
            dlg_listbox_add(ctrl, dp, name);
        dlg_update_done(ctrl, dp);
    } else if (event == EVENT_ACTION) {
        /* Double-clicking a session loads it */
        ca_load_selected_record(st, dp);
    }
}

static void ca_load_handler(dlgcontrol *ctrl, dlgparam *dp,
                            void *data, int event)
{
    struct ca_state *st = (struct ca_state *)ctrl->context.p;
    if (event == EVENT_ACTION) {
        ca_load_selected_record(st, dp);
    }
}

static strbuf *decode_pubkey(ptrlen data, const char **error)
{
    /*
     * See if we have a plain base64-encoded public key blob.
     */
    if (base64_valid(data))
        return base64_decode_sb(data);

    /*
     * Otherwise, try to decode as if it was a public key _file_.
     */
    BinarySource src[1];
    BinarySource_BARE_INIT_PL(src, data);
    strbuf *blob = strbuf_new();
    if (ppk_loadpub_s(src, NULL, BinarySink_UPCAST(blob), NULL, error))
        return blob;

    return NULL;
}

static void ca_save_handler(dlgcontrol *ctrl, dlgparam *dp,
                            void *data, int event)
{
    struct ca_state *st = (struct ca_state *)ctrl->context.p;
    if (event == EVENT_ACTION) {
        if (!count234(st->host_wcs)) {
            dlg_error_msg(dp, "No hostnames configured for this key");
            return;
        }

        strbuf *pubkey;
        {
            const char *error;
            pubkey = decode_pubkey(ptrlen_from_asciz(st->pubkey), &error);
            if (!pubkey) {
                char *msg = dupprintf("CA public key invalid: %s", error);
                dlg_error_msg(dp, msg);
                sfree(msg);
                return;
            }
        }

        host_ca *hca = snew(host_ca);
        memset(hca, 0, sizeof(*hca));
        hca->name = dupstr(st->name);
        hca->ca_public_key = pubkey;
        hca->n_hostname_wildcards = count234(st->host_wcs);
        hca->hostname_wildcards = snewn(hca->n_hostname_wildcards, char *);
        for (size_t i = 0; i < hca->n_hostname_wildcards; i++)
            hca->hostname_wildcards[i] = dupstr(index234(st->host_wcs, i));
        hca->opts = st->opts;          /* structure copy */

        char *error = host_ca_save(hca);
        host_ca_free(hca);

        if (error) {
            dlg_error_msg(dp, error);
            sfree(error);
        } else {
            ca_refresh_name_list(st);
            dlg_refresh(st->ca_reclist, dp);
        }
    }
}

static void ca_delete_handler(dlgcontrol *ctrl, dlgparam *dp,
                              void *data, int event)
{
    struct ca_state *st = (struct ca_state *)ctrl->context.p;
    if (event == EVENT_ACTION) {
        int i = dlg_listbox_index(st->ca_reclist, dp);
        if (i < 0) {
            dlg_beep(dp);
            return;
        }
        const char *name = index234(st->ca_names, i);
        if (!name) { /* in case the list box and the tree got out of sync */
            dlg_beep(dp);
            return;
        }

        char *error = host_ca_delete(name);
        if (error) {
            dlg_error_msg(dp, error);
            sfree(error);
        } else {
            ca_refresh_name_list(st);
            dlg_refresh(st->ca_reclist, dp);
        }
    }
}

static void ca_pubkey_edit_handler(dlgcontrol *ctrl, dlgparam *dp,
                                   void *data, int event)
{
    struct ca_state *st = (struct ca_state *)ctrl->context.p;
    if (event == EVENT_REFRESH) {
        dlg_editbox_set(ctrl, dp, st->pubkey);
    } else if (event == EVENT_VALCHANGE) {
        sfree(st->pubkey);
        st->pubkey = dlg_editbox_get(ctrl, dp);
    }
}

static void ca_pubkey_file_handler(dlgcontrol *ctrl, dlgparam *dp,
                                   void *data, int event)
{
    struct ca_state *st = (struct ca_state *)ctrl->context.p;
    if (event == EVENT_ACTION) {
        Filename *filename = dlg_filesel_get(ctrl, dp);
        strbuf *keyblob = strbuf_new();
        const char *load_error;
        bool ok = ppk_loadpub_f(filename, NULL, BinarySink_UPCAST(keyblob),
                                NULL, &load_error);
        if (!ok) {
            char *message = dupprintf(
                "Unable to load public key from '%s': %s",
                filename_to_str(filename), load_error);
            dlg_error_msg(dp, message);
            sfree(message);
        } else {
            sfree(st->pubkey);
            st->pubkey = strbuf_to_str(
                base64_encode_sb(ptrlen_from_strbuf(keyblob), 0));
            dlg_refresh(st->ca_pubkey_edit, dp);
        }
        filename_free(filename);
        strbuf_free(keyblob);
    }
}

static void ca_wclist_handler(dlgcontrol *ctrl, dlgparam *dp,
                              void *data, int event)
{
    struct ca_state *st = (struct ca_state *)ctrl->context.p;
    if (event == EVENT_REFRESH) {
        dlg_update_start(ctrl, dp);
        dlg_listbox_clear(ctrl, dp);
        const char *name;
        for (int i = 0; (name = index234(st->host_wcs, i)) != NULL; i++)
            dlg_listbox_add(ctrl, dp, name);
        dlg_update_done(ctrl, dp);
    }
}

static void ca_wc_edit_handler(dlgcontrol *ctrl, dlgparam *dp,
                               void *data, int event)
{
    struct ca_state *st = (struct ca_state *)ctrl->context.p;
    if (event == EVENT_REFRESH) {
        dlg_editbox_set(ctrl, dp, st->wc);
    } else if (event == EVENT_VALCHANGE) {
        sfree(st->wc);
        st->wc = dlg_editbox_get(ctrl, dp);
    }
}

static void ca_wc_add_handler(dlgcontrol *ctrl, dlgparam *dp,
                              void *data, int event)
{
    struct ca_state *st = (struct ca_state *)ctrl->context.p;
    if (event == EVENT_ACTION) {
        if (!st->wc) {
            dlg_beep(dp);
            return;
        }

        if (add234(st->host_wcs, st->wc) == st->wc) {
            dlg_refresh(st->ca_wclist, dp);
        } else {
            sfree(st->wc);
        }

        st->wc = dupstr("");
        dlg_refresh(st->ca_wc_edit, dp);
    }
}

static void ca_wc_rem_handler(dlgcontrol *ctrl, dlgparam *dp,
                              void *data, int event)
{
    struct ca_state *st = (struct ca_state *)ctrl->context.p;
    if (event == EVENT_ACTION) {
        int i = dlg_listbox_index(st->ca_wclist, dp);
        if (i < 0) {
            dlg_beep(dp);
            return;
        }
        char *wc = delpos234(st->host_wcs, i);
        if (!wc) {
            dlg_beep(dp);
            return;
        }

        sfree(st->wc);
        st->wc = wc;
        dlg_refresh(st->ca_wclist, dp);
        dlg_refresh(st->ca_wc_edit, dp);
    }
}

static void ca_rsa_type_handler(dlgcontrol *ctrl, dlgparam *dp,
                                void *data, int event)
{
    struct ca_state *st = (struct ca_state *)ctrl->context.p;
    size_t offset = ctrl->context2.i;
    bool *option = (bool *)((char *)&st->opts + offset);

    if (event == EVENT_REFRESH) {
        dlg_checkbox_set(ctrl, dp, *option);
    } else if (event == EVENT_VALCHANGE) {
        *option = dlg_checkbox_get(ctrl, dp);
    }
}

void setup_ca_config_box(struct controlbox *b)
{
    struct controlset *s;
    dlgcontrol *c;

    /* Internal state for manipulating the host CA system */
    struct ca_state *st = (struct ca_state *)ctrl_alloc_with_free(
        b, sizeof(struct ca_state), ca_state_free);
    memset(st, 0, sizeof(*st));
    st->ca_names = newtree234(ca_name_compare);
    st->host_wcs = newtree234(ca_name_compare);
    ca_refresh_name_list(st);

    /* Initialise the settings to a default blank host_ca */
    {
        host_ca *hca = host_ca_new();
        set_from_hca(st, hca);
        host_ca_free(hca);
    }

    /* Action area, with the Done button in it */
    s = ctrl_getset(b, "", "", "");
    ctrl_columns(s, 5, 20, 20, 20, 20, 20);
    c = ctrl_pushbutton(s, "Done", 'o', HELPCTX(no_help),
                        ca_ok_handler, P(st));
    c->button.iscancel = true;
    c->column = 4;

    /* Load/save box, as similar as possible to the main saved sessions one */
    s = ctrl_getset(b, "Main", "loadsave",
                    "Load, save or delete a host CA record");
    ctrl_columns(s, 2, 75, 25);
    c = ctrl_editbox(s, "Name for this CA (shown in log messages)",
                     'n', 100, HELPCTX(no_help),
                     ca_name_handler, P(st), P(NULL));
    c->column = 0;
    st->ca_name_edit = c;
    /* Reset columns so that the buttons are alongside the list, rather
     * than alongside that edit box. */
    ctrl_columns(s, 1, 100);
    ctrl_columns(s, 2, 75, 25);
    c = ctrl_listbox(s, NULL, NO_SHORTCUT, HELPCTX(no_help),
                     ca_reclist_handler, P(st));
    c->column = 0;
    c->listbox.height = 6;
    st->ca_reclist = c;
    c = ctrl_pushbutton(s, "Load", 'l', HELPCTX(no_help),
                        ca_load_handler, P(st));
    c->column = 1;
    c = ctrl_pushbutton(s, "Save", 'v', HELPCTX(no_help),
                        ca_save_handler, P(st));
    c->column = 1;
    c = ctrl_pushbutton(s, "Delete", 'd', HELPCTX(no_help),
                        ca_delete_handler, P(st));
    c->column = 1;

    /* Box containing the details of a specific CA record */
    s = ctrl_getset(b, "Main", "details", "Details of a host CA record");
    ctrl_columns(s, 2, 75, 25);
    c = ctrl_editbox(s, "Public key of certification authority", 'k', 100,
                     HELPCTX(no_help), ca_pubkey_edit_handler, P(st), P(NULL));
    c->column = 0;
    st->ca_pubkey_edit = c;
    c = ctrl_filesel(s, "Read from file", NO_SHORTCUT, NULL, false,
                     "Select public key file of certification authority",
                     HELPCTX(no_help), ca_pubkey_file_handler, P(st));
    c->fileselect.just_button = true;
    c->column = 1;
    ctrl_columns(s, 1, 100);
    c = ctrl_listbox(s, "Hostname patterns this key is trusted to certify",
                     NO_SHORTCUT, HELPCTX(no_help), ca_wclist_handler, P(st));
    c->listbox.height = 3;
    st->ca_wclist = c;
    ctrl_columns(s, 3, 70, 15, 15);
    c = ctrl_editbox(s, "Hostname pattern to add", 'h', 100,
                     HELPCTX(no_help), ca_wc_edit_handler, P(st), P(NULL));
    c->column = 0;
    st->ca_wc_edit = c;
    c = ctrl_pushbutton(s, "Add", NO_SHORTCUT, HELPCTX(no_help),
                        ca_wc_add_handler, P(st));
    c->column = 1;
    c = ctrl_pushbutton(s, "Remove", NO_SHORTCUT, HELPCTX(no_help),
                        ca_wc_rem_handler, P(st));
    c->column = 2;
    ctrl_columns(s, 1, 100);

    ctrl_columns(s, 4, 44, 18, 18, 18);
    c = ctrl_text(s, "Signature types (RSA keys only):", HELPCTX(no_help));
    c->column = 0;
    c = ctrl_checkbox(s, "SHA-1", NO_SHORTCUT, HELPCTX(no_help),
                      ca_rsa_type_handler, P(st));
    c->column = 1;
    c->context2 = I(offsetof(ca_options, permit_rsa_sha1));
    st->rsa_type_checkboxes[0] = c;
    c = ctrl_checkbox(s, "SHA-256", NO_SHORTCUT, HELPCTX(no_help),
                      ca_rsa_type_handler, P(st));
    c->column = 2;
    c->context2 = I(offsetof(ca_options, permit_rsa_sha256));
    st->rsa_type_checkboxes[1] = c;
    c = ctrl_checkbox(s, "SHA-512", NO_SHORTCUT, HELPCTX(no_help),
                      ca_rsa_type_handler, P(st));
    c->column = 3;
    c->context2 = I(offsetof(ca_options, permit_rsa_sha512));
    st->rsa_type_checkboxes[2] = c;
    ctrl_columns(s, 1, 100);
}
