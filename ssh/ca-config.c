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
    dlgcontrol *ca_pubkey_info;
    dlgcontrol *ca_validity_edit;
    dlgcontrol *rsa_type_checkboxes[NRSATYPES];
    char *name, *pubkey, *validity;
    tree234 *ca_names;                 /* stores plain 'char *' */
    ca_options opts;
    strbuf *ca_pubkey_blob;
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
    sfree(st->name);
    sfree(st->validity);
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
    st->name = dupstr(hca->name ? hca->name : "");

    sfree(st->pubkey);
    if (hca->ca_public_key)
        st->pubkey = strbuf_to_str(
            base64_encode_sb(ptrlen_from_strbuf(hca->ca_public_key), 0));
    else
        st->pubkey = dupstr("");

    st->validity = dupstr(hca->validity_expression ?
                          hca->validity_expression : "");

    st->opts = hca->opts;              /* structure copy */
}

static void ca_refresh_pubkey_info(struct ca_state *st, dlgparam *dp)
{
    char *text = NULL;
    ssh_key *key = NULL;
    strbuf *blob = strbuf_new();

    ptrlen data = ptrlen_from_asciz(st->pubkey);

    if (st->ca_pubkey_blob)
        strbuf_free(st->ca_pubkey_blob);
    st->ca_pubkey_blob = NULL;

    if (!data.len) {
        text = dupstr(" ");
        goto out;
    }

    /*
     * See if we have a plain base64-encoded public key blob.
     */
    if (base64_valid(data)) {
        base64_decode_bs(BinarySink_UPCAST(blob), data);
    } else {
        /*
         * Otherwise, try to decode as if it was a public key _file_.
         */
        BinarySource src[1];
        BinarySource_BARE_INIT_PL(src, data);
        const char *error;
        if (!ppk_loadpub_s(src, NULL, BinarySink_UPCAST(blob), NULL, &error)) {
            text = dupprintf("Cannot decode key: %s", error);
            goto out;
        }
    }

    ptrlen alg_name = pubkey_blob_to_alg_name(ptrlen_from_strbuf(blob));
    if (!alg_name.len) {
        text = dupstr("Invalid key (no key type)");
        goto out;
    }

    const ssh_keyalg *alg = find_pubkey_alg_len(alg_name);
    if (!alg) {
        text = dupprintf("Unrecognised key type '%.*s'",
                         PTRLEN_PRINTF(alg_name));
        goto out;
    }
    if (alg->is_certificate) {
        text = dupprintf("CA key may not be a certificate (type is '%.*s')",
                         PTRLEN_PRINTF(alg_name));
        goto out;
    }

    key = ssh_key_new_pub(alg, ptrlen_from_strbuf(blob));
    if (!key) {
        text = dupprintf("Invalid '%.*s' key data", PTRLEN_PRINTF(alg_name));
        goto out;
    }

    text = ssh2_fingerprint(key, SSH_FPTYPE_DEFAULT);
    st->ca_pubkey_blob = blob;
    blob = NULL; /* prevent free */

  out:
    dlg_text_set(st->ca_pubkey_info, dp, text);
    if (key)
        ssh_key_free(key);
    sfree(text);
    if (blob)
        strbuf_free(blob);
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
    dlg_refresh(st->ca_validity_edit, dp);
    for (size_t i = 0; i < NRSATYPES; i++)
        dlg_refresh(st->rsa_type_checkboxes[i], dp);
    ca_refresh_pubkey_info(st, dp);
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

static void ca_save_handler(dlgcontrol *ctrl, dlgparam *dp,
                            void *data, int event)
{
    struct ca_state *st = (struct ca_state *)ctrl->context.p;
    if (event == EVENT_ACTION) {
        if (!*st->validity) {
            dlg_error_msg(dp, "No validity expression configured "
                          "for this key");
            return;
        }

        char *error_msg;
        ptrlen error_loc;
        if (!cert_expr_valid(st->validity, &error_msg, &error_loc)) {
            char *error_full = dupprintf("Error in expression: %s", error_msg);
            dlg_error_msg(dp, error_full);
            dlg_set_focus(st->ca_validity_edit, dp);
            dlg_editbox_select_range(
                st->ca_validity_edit, dp,
                (const char *)error_loc.ptr - st->validity, error_loc.len);
            sfree(error_msg);
            sfree(error_full);
            return;
        }

        if (!st->ca_pubkey_blob) {
            dlg_error_msg(dp, "No valid CA public key entered");
            return;
        }

        host_ca *hca = snew(host_ca);
        memset(hca, 0, sizeof(*hca));
        hca->name = dupstr(st->name);
        hca->ca_public_key = strbuf_dup(ptrlen_from_strbuf(
                                            st->ca_pubkey_blob));
        hca->validity_expression = dupstr(st->validity);
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
        ca_refresh_pubkey_info(st, dp);
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

static void ca_validity_handler(dlgcontrol *ctrl, dlgparam *dp,
                                void *data, int event)
{
    struct ca_state *st = (struct ca_state *)ctrl->context.p;
    if (event == EVENT_REFRESH) {
        dlg_editbox_set(ctrl, dp, st->validity);
    } else if (event == EVENT_VALCHANGE) {
        sfree(st->validity);
        st->validity = dlg_editbox_get(ctrl, dp);
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
    st->validity = dupstr("");
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
    c = ctrl_pushbutton(s, "Done", 'o', HELPCTX(ssh_kex_cert),
                        ca_ok_handler, P(st));
    c->button.iscancel = true;
    c->column = 4;

    /* Load/save box, as similar as possible to the main saved sessions one */
    s = ctrl_getset(b, "Main", "loadsave",
                    "Load, save or delete a host CA record");
    ctrl_columns(s, 2, 75, 25);
    c = ctrl_editbox(s, "Name for this CA (shown in log messages)",
                     'n', 100, HELPCTX(ssh_kex_cert),
                     ca_name_handler, P(st), P(NULL));
    c->column = 0;
    st->ca_name_edit = c;
    /* Reset columns so that the buttons are alongside the list, rather
     * than alongside that edit box. */
    ctrl_columns(s, 1, 100);
    ctrl_columns(s, 2, 75, 25);
    c = ctrl_listbox(s, NULL, NO_SHORTCUT, HELPCTX(ssh_kex_cert),
                     ca_reclist_handler, P(st));
    c->column = 0;
    c->listbox.height = 6;
    st->ca_reclist = c;
    c = ctrl_pushbutton(s, "Load", 'l', HELPCTX(ssh_kex_cert),
                        ca_load_handler, P(st));
    c->column = 1;
    c = ctrl_pushbutton(s, "Save", 'v', HELPCTX(ssh_kex_cert),
                        ca_save_handler, P(st));
    c->column = 1;
    c = ctrl_pushbutton(s, "Delete", 'd', HELPCTX(ssh_kex_cert),
                        ca_delete_handler, P(st));
    c->column = 1;

    s = ctrl_getset(b, "Main", "pubkey", "Public key for this CA record");

    ctrl_columns(s, 2, 75, 25);
    c = ctrl_editbox(s, "Public key of certification authority", 'k', 100,
                     HELPCTX(ssh_kex_cert), ca_pubkey_edit_handler,
                     P(st), P(NULL));
    c->column = 0;
    st->ca_pubkey_edit = c;
    c = ctrl_filesel(s, "Read from file", NO_SHORTCUT, NULL, false,
                     "Select public key file of certification authority",
                     HELPCTX(ssh_kex_cert), ca_pubkey_file_handler, P(st));
    c->fileselect.just_button = true;
    c->align_next_to = st->ca_pubkey_edit;
    c->column = 1;
    ctrl_columns(s, 1, 100);
    st->ca_pubkey_info = c = ctrl_text(s, " ", HELPCTX(ssh_kex_cert));
    c->text.wrap = false;

    s = ctrl_getset(b, "Main", "options", "What this CA is trusted to do");

    c = ctrl_editbox(s, "Valid hosts this key is trusted to certify", 'h', 100,
                     HELPCTX(ssh_cert_valid_expr), ca_validity_handler,
                     P(st), P(NULL));
    st->ca_validity_edit = c;

    ctrl_columns(s, 4, 44, 18, 18, 18);
    c = ctrl_text(s, "Signature types (RSA keys only):",
                  HELPCTX(ssh_cert_rsa_hash));
    c->column = 0;
    dlgcontrol *sigtypelabel = c;
    c = ctrl_checkbox(s, "SHA-1", NO_SHORTCUT, HELPCTX(ssh_cert_rsa_hash),
                      ca_rsa_type_handler, P(st));
    c->column = 1;
    c->align_next_to = sigtypelabel;
    c->context2 = I(offsetof(ca_options, permit_rsa_sha1));
    st->rsa_type_checkboxes[0] = c;
    c = ctrl_checkbox(s, "SHA-256", NO_SHORTCUT, HELPCTX(ssh_cert_rsa_hash),
                      ca_rsa_type_handler, P(st));
    c->column = 2;
    c->align_next_to = sigtypelabel;
    c->context2 = I(offsetof(ca_options, permit_rsa_sha256));
    st->rsa_type_checkboxes[1] = c;
    c = ctrl_checkbox(s, "SHA-512", NO_SHORTCUT, HELPCTX(ssh_cert_rsa_hash),
                      ca_rsa_type_handler, P(st));
    c->column = 3;
    c->align_next_to = sigtypelabel;
    c->context2 = I(offsetof(ca_options, permit_rsa_sha512));
    st->rsa_type_checkboxes[2] = c;
    ctrl_columns(s, 1, 100);
}
