/*
 * config.c - the platform-independent parts of the PuTTY
 * configuration box.
 */

#include <assert.h>
#include <stdlib.h>

#include "putty.h"
#include "dialog.h"
#include "storage.h"

#define PRINTER_DISABLED_STRING "无 (禁止打印)"

#define HOST_BOX_TITLE "主机名称(或 IP 地址)(N)"
#define PORT_BOX_TITLE "端口(P)"

void conf_radiobutton_handler(union control *ctrl, void *dlg,
			      void *data, int event)
{
    int button;
    Conf *conf = (Conf *)data;

    /*
     * For a standard radio button set, the context parameter gives
     * the primary key (CONF_foo), and the extra data per button
     * gives the value the target field should take if that button
     * is the one selected.
     */
    if (event == EVENT_REFRESH) {
	int val = conf_get_int(conf, ctrl->radio.context.i);
	for (button = 0; button < ctrl->radio.nbuttons; button++)
	    if (val == ctrl->radio.buttondata[button].i)
		break;
	/* We expected that `break' to happen, in all circumstances. */
	assert(button < ctrl->radio.nbuttons);
	dlg_radiobutton_set(ctrl, dlg, button);
    } else if (event == EVENT_VALCHANGE) {
	button = dlg_radiobutton_get(ctrl, dlg);
	assert(button >= 0 && button < ctrl->radio.nbuttons);
	conf_set_int(conf, ctrl->radio.context.i,
		     ctrl->radio.buttondata[button].i);
    }
}

#define CHECKBOX_INVERT (1<<30)
void conf_checkbox_handler(union control *ctrl, void *dlg,
			   void *data, int event)
{
    int key, invert;
    Conf *conf = (Conf *)data;

    /*
     * For a standard checkbox, the context parameter gives the
     * primary key (CONF_foo), optionally ORed with CHECKBOX_INVERT.
     */
    key = ctrl->checkbox.context.i;
    if (key & CHECKBOX_INVERT) {
	key &= ~CHECKBOX_INVERT;
	invert = 1;
    } else
	invert = 0;

    /*
     * C lacks a logical XOR, so the following code uses the idiom
     * (!a ^ !b) to obtain the logical XOR of a and b. (That is, 1
     * iff exactly one of a and b is nonzero, otherwise 0.)
     */

    if (event == EVENT_REFRESH) {
	int val = conf_get_int(conf, key);
	dlg_checkbox_set(ctrl, dlg, (!val ^ !invert));
    } else if (event == EVENT_VALCHANGE) {
	conf_set_int(conf, key, !dlg_checkbox_get(ctrl,dlg) ^ !invert);
    }
}

void conf_editbox_handler(union control *ctrl, void *dlg,
			  void *data, int event)
{
    /*
     * The standard edit-box handler expects the main `context'
     * field to contain the primary key. The secondary `context2'
     * field indicates the type of this field:
     *
     *  - if context2 > 0, the field is a string.
     *  - if context2 == -1, the field is an int and the edit box
     *    is numeric.
     *  - if context2 < -1, the field is an int and the edit box is
     *    _floating_, and (-context2) gives the scale. (E.g. if
     *    context2 == -1000, then typing 1.2 into the box will set
     *    the field to 1200.)
     */
    int key = ctrl->editbox.context.i;
    int length = ctrl->editbox.context2.i;
    Conf *conf = (Conf *)data;

    if (length > 0) {
	if (event == EVENT_REFRESH) {
	    char *field = conf_get_str(conf, key);
	    dlg_editbox_set(ctrl, dlg, field);
	} else if (event == EVENT_VALCHANGE) {
	    char *field = dlg_editbox_get(ctrl, dlg);
	    conf_set_str(conf, key, field);
	    sfree(field);
	}
    } else if (length < 0) {
	if (event == EVENT_REFRESH) {
	    char str[80];
	    int value = conf_get_int(conf, key);
	    if (length == -1)
		sprintf(str, "%d", value);
	    else
		sprintf(str, "%g", (double)value / (double)(-length));
	    dlg_editbox_set(ctrl, dlg, str);
	} else if (event == EVENT_VALCHANGE) {
	    char *str = dlg_editbox_get(ctrl, dlg);
	    if (length == -1)
		conf_set_int(conf, key, atoi(str));
	    else
		conf_set_int(conf, key, (int)((-length) * atof(str)));
	    sfree(str);
	}
    }
}

void conf_filesel_handler(union control *ctrl, void *dlg,
			  void *data, int event)
{
    int key = ctrl->fileselect.context.i;
    Conf *conf = (Conf *)data;

    if (event == EVENT_REFRESH) {
	dlg_filesel_set(ctrl, dlg, conf_get_filename(conf, key));
    } else if (event == EVENT_VALCHANGE) {
	Filename *filename = dlg_filesel_get(ctrl, dlg);
	conf_set_filename(conf, key, filename);
        filename_free(filename);
    }
}

void conf_fontsel_handler(union control *ctrl, void *dlg,
			  void *data, int event)
{
    int key = ctrl->fontselect.context.i;
    Conf *conf = (Conf *)data;

    if (event == EVENT_REFRESH) {
	dlg_fontsel_set(ctrl, dlg, conf_get_fontspec(conf, key));
    } else if (event == EVENT_VALCHANGE) {
	FontSpec *fontspec = dlg_fontsel_get(ctrl, dlg);
	conf_set_fontspec(conf, key, fontspec);
        fontspec_free(fontspec);
    }
}

static void config_host_handler(union control *ctrl, void *dlg,
				void *data, int event)
{
    Conf *conf = (Conf *)data;

    /*
     * This function works just like the standard edit box handler,
     * only it has to choose the control's label and text from two
     * different places depending on the protocol.
     */
    if (event == EVENT_REFRESH) {
	if (conf_get_int(conf, CONF_protocol) == PROT_SERIAL) {
	    /*
	     * This label text is carefully chosen to contain an n,
	     * since that's the shortcut for the host name control.
	     */
	    dlg_label_change(ctrl, dlg, "串行口(N)");
	    dlg_editbox_set(ctrl, dlg, conf_get_str(conf, CONF_serline));
	} else {
	    dlg_label_change(ctrl, dlg, HOST_BOX_TITLE);
	    dlg_editbox_set(ctrl, dlg, conf_get_str(conf, CONF_host));
	}
    } else if (event == EVENT_VALCHANGE) {
	char *s = dlg_editbox_get(ctrl, dlg);
	if (conf_get_int(conf, CONF_protocol) == PROT_SERIAL)
	    conf_set_str(conf, CONF_serline, s);
	else
	    conf_set_str(conf, CONF_host, s);
	sfree(s);
    }
}

static void config_port_handler(union control *ctrl, void *dlg,
				void *data, int event)
{
    Conf *conf = (Conf *)data;
    char buf[80];

    /*
     * This function works similarly to the standard edit box handler,
     * only it has to choose the control's label and text from two
     * different places depending on the protocol.
     */
    if (event == EVENT_REFRESH) {
	if (conf_get_int(conf, CONF_protocol) == PROT_SERIAL) {
	    /*
	     * This label text is carefully chosen to contain a p,
	     * since that's the shortcut for the port control.
	     */
	    dlg_label_change(ctrl, dlg, "速度(P)");
	    sprintf(buf, "%d", conf_get_int(conf, CONF_serspeed));
	} else {
	    dlg_label_change(ctrl, dlg, PORT_BOX_TITLE);
	    if (conf_get_int(conf, CONF_port) != 0)
		sprintf(buf, "%d", conf_get_int(conf, CONF_port));
	    else
		/* Display an (invalid) port of 0 as blank */
		buf[0] = '\0';
	}
	dlg_editbox_set(ctrl, dlg, buf);
    } else if (event == EVENT_VALCHANGE) {
	char *s = dlg_editbox_get(ctrl, dlg);
	int i = atoi(s);
	sfree(s);

	if (conf_get_int(conf, CONF_protocol) == PROT_SERIAL)
	    conf_set_int(conf, CONF_serspeed, i);
	else
	    conf_set_int(conf, CONF_port, i);
    }
}

struct hostport {
    union control *host, *port;
};

/*
 * We export this function so that platform-specific config
 * routines can use it to conveniently identify the protocol radio
 * buttons in order to add to them.
 */
void config_protocolbuttons_handler(union control *ctrl, void *dlg,
				    void *data, int event)
{
    int button;
    Conf *conf = (Conf *)data;
    struct hostport *hp = (struct hostport *)ctrl->radio.context.p;

    /*
     * This function works just like the standard radio-button
     * handler, except that it also has to change the setting of
     * the port box, and refresh both host and port boxes when. We
     * expect the context parameter to point at a hostport
     * structure giving the `union control's for both.
     */
    if (event == EVENT_REFRESH) {
	int protocol = conf_get_int(conf, CONF_protocol);
	for (button = 0; button < ctrl->radio.nbuttons; button++)
	    if (protocol == ctrl->radio.buttondata[button].i)
		break;
	/* We expected that `break' to happen, in all circumstances. */
	assert(button < ctrl->radio.nbuttons);
	dlg_radiobutton_set(ctrl, dlg, button);
    } else if (event == EVENT_VALCHANGE) {
	int oldproto = conf_get_int(conf, CONF_protocol);
	int newproto, port;

	button = dlg_radiobutton_get(ctrl, dlg);
	assert(button >= 0 && button < ctrl->radio.nbuttons);
	newproto = ctrl->radio.buttondata[button].i;
	conf_set_int(conf, CONF_protocol, newproto);

	if (oldproto != newproto) {
	    Backend *ob = backend_from_proto(oldproto);
	    Backend *nb = backend_from_proto(newproto);
	    assert(ob);
	    assert(nb);
	    /* Iff the user hasn't changed the port from the old protocol's
	     * default, update it with the new protocol's default.
	     * (This includes a "default" of 0, implying that there is no
	     * sensible default for that protocol; in this case it's
	     * displayed as a blank.)
	     * This helps with the common case of tabbing through the
	     * controls in order and setting a non-default port before
	     * getting to the protocol; we want that non-default port
	     * to be preserved. */
	    port = conf_get_int(conf, CONF_port);
	    if (port == ob->default_port)
		conf_set_int(conf, CONF_port, nb->default_port);
	}
	dlg_refresh(hp->host, dlg);
	dlg_refresh(hp->port, dlg);
    }
}

static void loggingbuttons_handler(union control *ctrl, void *dlg,
				   void *data, int event)
{
    int button;
    Conf *conf = (Conf *)data;
    /* This function works just like the standard radio-button handler,
     * but it has to fall back to "no logging" in situations where the
     * configured logging type isn't applicable.
     */
    if (event == EVENT_REFRESH) {
	int logtype = conf_get_int(conf, CONF_logtype);

        for (button = 0; button < ctrl->radio.nbuttons; button++)
            if (logtype == ctrl->radio.buttondata[button].i)
	        break;

	/* We fell off the end, so we lack the configured logging type */
	if (button == ctrl->radio.nbuttons) {
	    button = 0;
	    conf_set_int(conf, CONF_logtype, LGTYP_NONE);
	}
	dlg_radiobutton_set(ctrl, dlg, button);
    } else if (event == EVENT_VALCHANGE) {
        button = dlg_radiobutton_get(ctrl, dlg);
        assert(button >= 0 && button < ctrl->radio.nbuttons);
        conf_set_int(conf, CONF_logtype, ctrl->radio.buttondata[button].i);
    }
}

static void numeric_keypad_handler(union control *ctrl, void *dlg,
				   void *data, int event)
{
    int button;
    Conf *conf = (Conf *)data;
    /*
     * This function works much like the standard radio button
     * handler, but it has to handle two fields in Conf.
     */
    if (event == EVENT_REFRESH) {
	if (conf_get_int(conf, CONF_nethack_keypad))
	    button = 2;
	else if (conf_get_int(conf, CONF_app_keypad))
	    button = 1;
	else
	    button = 0;
	assert(button < ctrl->radio.nbuttons);
	dlg_radiobutton_set(ctrl, dlg, button);
    } else if (event == EVENT_VALCHANGE) {
	button = dlg_radiobutton_get(ctrl, dlg);
	assert(button >= 0 && button < ctrl->radio.nbuttons);
	if (button == 2) {
	    conf_set_int(conf, CONF_app_keypad, FALSE);
	    conf_set_int(conf, CONF_nethack_keypad, TRUE);
	} else {
	    conf_set_int(conf, CONF_app_keypad, (button != 0));
	    conf_set_int(conf, CONF_nethack_keypad, FALSE);
	}
    }
}

static void cipherlist_handler(union control *ctrl, void *dlg,
			       void *data, int event)
{
    Conf *conf = (Conf *)data;
    if (event == EVENT_REFRESH) {
	int i;

	static const struct { const char *s; int c; } ciphers[] = {
            { "ChaCha20 (只限 SSH-2)",  CIPHER_CHACHA20 },
	    { "3DES",			CIPHER_3DES },
	    { "Blowfish",		CIPHER_BLOWFISH },
	    { "DES",			CIPHER_DES },
	    { "AES (只限 SSH-2)",	CIPHER_AES },
	    { "Arcfour (只限 SSH-2)",	CIPHER_ARCFOUR },
	    { "-- 下面为警告选项 --",	CIPHER_WARN }
	};

	/* Set up the "selected ciphers" box. */
	/* (cipherlist assumed to contain all ciphers) */
	dlg_update_start(ctrl, dlg);
	dlg_listbox_clear(ctrl, dlg);
	for (i = 0; i < CIPHER_MAX; i++) {
	    int c = conf_get_int_int(conf, CONF_ssh_cipherlist, i);
	    int j;
	    const char *cstr = NULL;
	    for (j = 0; j < (sizeof ciphers) / (sizeof ciphers[0]); j++) {
		if (ciphers[j].c == c) {
		    cstr = ciphers[j].s;
		    break;
		}
	    }
	    dlg_listbox_addwithid(ctrl, dlg, cstr, c);
	}
	dlg_update_done(ctrl, dlg);

    } else if (event == EVENT_VALCHANGE) {
	int i;

	/* Update array to match the list box. */
	for (i=0; i < CIPHER_MAX; i++)
	    conf_set_int_int(conf, CONF_ssh_cipherlist, i,
			     dlg_listbox_getid(ctrl, dlg, i));
    }
}

#ifndef NO_GSSAPI
static void gsslist_handler(union control *ctrl, void *dlg,
			    void *data, int event)
{
    Conf *conf = (Conf *)data;
    if (event == EVENT_REFRESH) {
	int i;

	dlg_update_start(ctrl, dlg);
	dlg_listbox_clear(ctrl, dlg);
	for (i = 0; i < ngsslibs; i++) {
	    int id = conf_get_int_int(conf, CONF_ssh_gsslist, i);
	    assert(id >= 0 && id < ngsslibs);
	    dlg_listbox_addwithid(ctrl, dlg, gsslibnames[id], id);
	}
	dlg_update_done(ctrl, dlg);

    } else if (event == EVENT_VALCHANGE) {
	int i;

	/* Update array to match the list box. */
	for (i=0; i < ngsslibs; i++)
	    conf_set_int_int(conf, CONF_ssh_gsslist, i,
			     dlg_listbox_getid(ctrl, dlg, i));
    }
}
#endif

static void kexlist_handler(union control *ctrl, void *dlg,
			    void *data, int event)
{
    Conf *conf = (Conf *)data;
    if (event == EVENT_REFRESH) {
	int i;

	static const struct { const char *s; int k; } kexes[] = {
	    { "Diffie-Hellman group 1",		KEX_DHGROUP1 },
	    { "Diffie-Hellman group 14",	KEX_DHGROUP14 },
	    { "Diffie-Hellman group exchange",	KEX_DHGEX },
	    { "RSA-based key exchange", 	KEX_RSA },
            { "ECDH key exchange",              KEX_ECDH },
	    { "-- 下面为警告选项 --",		KEX_WARN }
	};

	/* Set up the "kex preference" box. */
	/* (kexlist assumed to contain all algorithms) */
	dlg_update_start(ctrl, dlg);
	dlg_listbox_clear(ctrl, dlg);
	for (i = 0; i < KEX_MAX; i++) {
	    int k = conf_get_int_int(conf, CONF_ssh_kexlist, i);
	    int j;
	    const char *kstr = NULL;
	    for (j = 0; j < (sizeof kexes) / (sizeof kexes[0]); j++) {
		if (kexes[j].k == k) {
		    kstr = kexes[j].s;
		    break;
		}
	    }
	    dlg_listbox_addwithid(ctrl, dlg, kstr, k);
	}
	dlg_update_done(ctrl, dlg);

    } else if (event == EVENT_VALCHANGE) {
	int i;

	/* Update array to match the list box. */
	for (i=0; i < KEX_MAX; i++)
	    conf_set_int_int(conf, CONF_ssh_kexlist, i,
			     dlg_listbox_getid(ctrl, dlg, i));
    }
}

static void hklist_handler(union control *ctrl, void *dlg,
                            void *data, int event)
{
    Conf *conf = (Conf *)data;
    if (event == EVENT_REFRESH) {
        int i;

        static const struct { const char *s; int k; } hks[] = {
            { "Ed25519",               HK_ED25519 },
            { "ECDSA",                 HK_ECDSA },
            { "DSA",                   HK_DSA },
            { "RSA",                   HK_RSA },
            { "-- 下面为警告选项 --", HK_WARN }
        };

        /* Set up the "host key preference" box. */
        /* (hklist assumed to contain all algorithms) */
        dlg_update_start(ctrl, dlg);
        dlg_listbox_clear(ctrl, dlg);
        for (i = 0; i < HK_MAX; i++) {
            int k = conf_get_int_int(conf, CONF_ssh_hklist, i);
            int j;
            const char *kstr = NULL;
            for (j = 0; j < lenof(hks); j++) {
                if (hks[j].k == k) {
                    kstr = hks[j].s;
                    break;
                }
            }
            dlg_listbox_addwithid(ctrl, dlg, kstr, k);
        }
        dlg_update_done(ctrl, dlg);

    } else if (event == EVENT_VALCHANGE) {
        int i;

        /* Update array to match the list box. */
        for (i=0; i < HK_MAX; i++)
            conf_set_int_int(conf, CONF_ssh_hklist, i,
                             dlg_listbox_getid(ctrl, dlg, i));
    }
}

static void printerbox_handler(union control *ctrl, void *dlg,
			       void *data, int event)
{
    Conf *conf = (Conf *)data;
    if (event == EVENT_REFRESH) {
	int nprinters, i;
	printer_enum *pe;
	const char *printer;

	dlg_update_start(ctrl, dlg);
	/*
	 * Some backends may wish to disable the drop-down list on
	 * this edit box. Be prepared for this.
	 */
	if (ctrl->editbox.has_list) {
	    dlg_listbox_clear(ctrl, dlg);
	    dlg_listbox_add(ctrl, dlg, PRINTER_DISABLED_STRING);
	    pe = printer_start_enum(&nprinters);
	    for (i = 0; i < nprinters; i++)
		dlg_listbox_add(ctrl, dlg, printer_get_name(pe, i));
	    printer_finish_enum(pe);
	}
	printer = conf_get_str(conf, CONF_printer);
	if (!printer)
	    printer = PRINTER_DISABLED_STRING;
	dlg_editbox_set(ctrl, dlg, printer);
	dlg_update_done(ctrl, dlg);
    } else if (event == EVENT_VALCHANGE) {
	char *printer = dlg_editbox_get(ctrl, dlg);
	if (!strcmp(printer, PRINTER_DISABLED_STRING))
	    printer[0] = '\0';
	conf_set_str(conf, CONF_printer, printer);
	sfree(printer);
    }
}

static void codepage_handler(union control *ctrl, void *dlg,
			     void *data, int event)
{
    Conf *conf = (Conf *)data;
    if (event == EVENT_REFRESH) {
	int i;
	const char *cp, *thiscp;
	dlg_update_start(ctrl, dlg);
	thiscp = cp_name(decode_codepage(conf_get_str(conf,
						      CONF_line_codepage)));
	dlg_listbox_clear(ctrl, dlg);
	for (i = 0; (cp = cp_enumerate(i)) != NULL; i++)
	    dlg_listbox_add(ctrl, dlg, cp);
	dlg_editbox_set(ctrl, dlg, thiscp);
	conf_set_str(conf, CONF_line_codepage, thiscp);
	dlg_update_done(ctrl, dlg);
    } else if (event == EVENT_VALCHANGE) {
	char *codepage = dlg_editbox_get(ctrl, dlg);
	conf_set_str(conf, CONF_line_codepage,
		     cp_name(decode_codepage(codepage)));
	sfree(codepage);
    }
}

static void sshbug_handler(union control *ctrl, void *dlg,
			   void *data, int event)
{
    Conf *conf = (Conf *)data;
    if (event == EVENT_REFRESH) {
        /*
         * We must fetch the previously configured value from the Conf
         * before we start modifying the drop-down list, otherwise the
         * spurious SELCHANGE we trigger in the process will overwrite
         * the value we wanted to keep.
         */
        int oldconf = conf_get_int(conf, ctrl->listbox.context.i);
	dlg_update_start(ctrl, dlg);
	dlg_listbox_clear(ctrl, dlg);
	dlg_listbox_addwithid(ctrl, dlg, "自动", AUTO);
	dlg_listbox_addwithid(ctrl, dlg, "关", FORCE_OFF);
	dlg_listbox_addwithid(ctrl, dlg, "开", FORCE_ON);
	switch (oldconf) {
	  case AUTO:      dlg_listbox_select(ctrl, dlg, 0); break;
	  case FORCE_OFF: dlg_listbox_select(ctrl, dlg, 1); break;
	  case FORCE_ON:  dlg_listbox_select(ctrl, dlg, 2); break;
	}
	dlg_update_done(ctrl, dlg);
    } else if (event == EVENT_SELCHANGE) {
	int i = dlg_listbox_index(ctrl, dlg);
	if (i < 0)
	    i = AUTO;
	else
	    i = dlg_listbox_getid(ctrl, dlg, i);
	conf_set_int(conf, ctrl->listbox.context.i, i);
    }
}

struct sessionsaver_data {
    union control *editbox, *listbox, *loadbutton, *savebutton, *delbutton;
    union control *okbutton, *cancelbutton;
    struct sesslist sesslist;
    int midsession;
    char *savedsession;     /* the current contents of ssd->editbox */
};

static void sessionsaver_data_free(void *ssdv)
{
    struct sessionsaver_data *ssd = (struct sessionsaver_data *)ssdv;
    get_sesslist(&ssd->sesslist, FALSE);
    sfree(ssd->savedsession);
    sfree(ssd);
}

/* 
 * Helper function to load the session selected in the list box, if
 * any, as this is done in more than one place below. Returns 0 for
 * failure.
 */
static int load_selected_session(struct sessionsaver_data *ssd,
				 void *dlg, Conf *conf, int *maybe_launch)
{
    int i = dlg_listbox_index(ssd->listbox, dlg);
    int isdef;
    if (i < 0) {
	dlg_beep(dlg);
	return 0;
    }
    isdef = !strcmp(ssd->sesslist.sessions[i], "默认设置");
    load_settings(ssd->sesslist.sessions[i], conf);
    sfree(ssd->savedsession);
    ssd->savedsession = dupstr(isdef ? "" : ssd->sesslist.sessions[i]);
    if (maybe_launch)
        *maybe_launch = !isdef;
    dlg_refresh(NULL, dlg);
    /* Restore the selection, which might have been clobbered by
     * changing the value of the edit box. */
    dlg_listbox_select(ssd->listbox, dlg, i);
    return 1;
}

static void sessionsaver_handler(union control *ctrl, void *dlg,
				 void *data, int event)
{
    Conf *conf = (Conf *)data;
    struct sessionsaver_data *ssd =
	(struct sessionsaver_data *)ctrl->generic.context.p;

    if (event == EVENT_REFRESH) {
	if (ctrl == ssd->editbox) {
	    dlg_editbox_set(ctrl, dlg, ssd->savedsession);
	} else if (ctrl == ssd->listbox) {
	    int i;
	    dlg_update_start(ctrl, dlg);
	    dlg_listbox_clear(ctrl, dlg);
	    for (i = 0; i < ssd->sesslist.nsessions; i++)
		dlg_listbox_add(ctrl, dlg, ssd->sesslist.sessions[i]);
	    dlg_update_done(ctrl, dlg);
	}
    } else if (event == EVENT_VALCHANGE) {
        int top, bottom, halfway, i;
	if (ctrl == ssd->editbox) {
            sfree(ssd->savedsession);
            ssd->savedsession = dlg_editbox_get(ctrl, dlg);
	    top = ssd->sesslist.nsessions;
	    bottom = -1;
	    while (top-bottom > 1) {
	        halfway = (top+bottom)/2;
	        i = strcmp(ssd->savedsession, ssd->sesslist.sessions[halfway]);
	        if (i <= 0 ) {
		    top = halfway;
	        } else {
		    bottom = halfway;
	        }
	    }
	    if (top == ssd->sesslist.nsessions) {
	        top -= 1;
	    }
	    dlg_listbox_select(ssd->listbox, dlg, top);
	}
    } else if (event == EVENT_ACTION) {
	int mbl = FALSE;
	if (!ssd->midsession &&
	    (ctrl == ssd->listbox ||
	     (ssd->loadbutton && ctrl == ssd->loadbutton))) {
	    /*
	     * The user has double-clicked a session, or hit Load.
	     * We must load the selected session, and then
	     * terminate the configuration dialog _if_ there was a
	     * double-click on the list box _and_ that session
	     * contains a hostname.
	     */
	    if (load_selected_session(ssd, dlg, conf, &mbl) &&
		(mbl && ctrl == ssd->listbox && conf_launchable(conf))) {
		dlg_end(dlg, 1);       /* it's all over, and succeeded */
	    }
	} else if (ctrl == ssd->savebutton) {
	    int isdef = !strcmp(ssd->savedsession, "默认设置");
	    if (!ssd->savedsession[0]) {
		int i = dlg_listbox_index(ssd->listbox, dlg);
		if (i < 0) {
		    dlg_beep(dlg);
		    return;
		}
		isdef = !strcmp(ssd->sesslist.sessions[i], "默认设置");
                sfree(ssd->savedsession);
                ssd->savedsession = dupstr(isdef ? "" :
                                           ssd->sesslist.sessions[i]);
	    }
            {
                char *errmsg = save_settings(ssd->savedsession, conf);
                if (errmsg) {
                    dlg_error_msg(dlg, errmsg);
                    sfree(errmsg);
                }
            }
	    get_sesslist(&ssd->sesslist, FALSE);
	    get_sesslist(&ssd->sesslist, TRUE);
	    dlg_refresh(ssd->editbox, dlg);
	    dlg_refresh(ssd->listbox, dlg);
	} else if (!ssd->midsession &&
		   ssd->delbutton && ctrl == ssd->delbutton) {
	    int i = dlg_listbox_index(ssd->listbox, dlg);
	    if (i <= 0) {
		dlg_beep(dlg);
	    } else {
		del_settings(ssd->sesslist.sessions[i]);
		get_sesslist(&ssd->sesslist, FALSE);
		get_sesslist(&ssd->sesslist, TRUE);
		dlg_refresh(ssd->listbox, dlg);
	    }
	} else if (ctrl == ssd->okbutton) {
            if (ssd->midsession) {
                /* In a mid-session Change Settings, Apply is always OK. */
		dlg_end(dlg, 1);
                return;
            }
	    /*
	     * Annoying special case. If the `Open' button is
	     * pressed while no host name is currently set, _and_
	     * the session list previously had the focus, _and_
	     * there was a session selected in that which had a
	     * valid host name in it, then load it and go.
	     */
	    if (dlg_last_focused(ctrl, dlg) == ssd->listbox &&
		!conf_launchable(conf)) {
		Conf *conf2 = conf_new();
		int mbl = FALSE;
		if (!load_selected_session(ssd, dlg, conf2, &mbl)) {
		    dlg_beep(dlg);
		    conf_free(conf2);
		    return;
		}
		/* If at this point we have a valid session, go! */
		if (mbl && conf_launchable(conf2)) {
		    conf_copy_into(conf, conf2);
		    dlg_end(dlg, 1);
		} else
		    dlg_beep(dlg);

		conf_free(conf2);
                return;
	    }

	    /*
	     * Otherwise, do the normal thing: if we have a valid
	     * session, get going.
	     */
	    if (conf_launchable(conf)) {
		dlg_end(dlg, 1);
	    } else
		dlg_beep(dlg);
	} else if (ctrl == ssd->cancelbutton) {
	    dlg_end(dlg, 0);
	}
    }
}

struct charclass_data {
    union control *listbox, *editbox, *button;
};

static void charclass_handler(union control *ctrl, void *dlg,
			      void *data, int event)
{
    Conf *conf = (Conf *)data;
    struct charclass_data *ccd =
	(struct charclass_data *)ctrl->generic.context.p;

    if (event == EVENT_REFRESH) {
	if (ctrl == ccd->listbox) {
	    int i;
	    dlg_update_start(ctrl, dlg);
	    dlg_listbox_clear(ctrl, dlg);
	    for (i = 0; i < 128; i++) {
		char str[100];
		sprintf(str, "%d\t(0x%02X)\t%c\t%d", i, i,
			(i >= 0x21 && i != 0x7F) ? i : ' ',
			conf_get_int_int(conf, CONF_wordness, i));
		dlg_listbox_add(ctrl, dlg, str);
	    }
	    dlg_update_done(ctrl, dlg);
	}
    } else if (event == EVENT_ACTION) {
	if (ctrl == ccd->button) {
	    char *str;
	    int i, n;
	    str = dlg_editbox_get(ccd->editbox, dlg);
	    n = atoi(str);
	    sfree(str);
	    for (i = 0; i < 128; i++) {
		if (dlg_listbox_issel(ccd->listbox, dlg, i))
		    conf_set_int_int(conf, CONF_wordness, i, n);
	    }
	    dlg_refresh(ccd->listbox, dlg);
	}
    }
}

struct colour_data {
    union control *listbox, *redit, *gedit, *bedit, *button;
};

static const char *const colours[] = {
    "默认前景", "默认前景(粗)",
    "默认背景", "默认背景(粗)",
    "光标文本", "光标颜色",
    "ANSI 黑", "ANSI 黑(粗)",
    "ANSI 红", "ANSI 红(粗)",
    "ANSI 绿", "ANSI 绿(粗)",
    "ANSI 黄", "ANSI 黄(粗)",
    "ANSI 蓝", "ANSI 蓝(粗)",
    "ANSI 紫", "ANSI 紫(粗)",
    "ANSI 青", "ANSI 青(粗)",
    "ANSI 白", "ANSI 白(粗)"
};

static void colour_handler(union control *ctrl, void *dlg,
			    void *data, int event)
{
    Conf *conf = (Conf *)data;
    struct colour_data *cd =
	(struct colour_data *)ctrl->generic.context.p;
    int update = FALSE, clear = FALSE, r, g, b;

    if (event == EVENT_REFRESH) {
	if (ctrl == cd->listbox) {
	    int i;
	    dlg_update_start(ctrl, dlg);
	    dlg_listbox_clear(ctrl, dlg);
	    for (i = 0; i < lenof(colours); i++)
		dlg_listbox_add(ctrl, dlg, colours[i]);
	    dlg_update_done(ctrl, dlg);
	    clear = TRUE;
	    update = TRUE;
	}
    } else if (event == EVENT_SELCHANGE) {
	if (ctrl == cd->listbox) {
	    /* The user has selected a colour. Update the RGB text. */
	    int i = dlg_listbox_index(ctrl, dlg);
	    if (i < 0) {
		clear = TRUE;
	    } else {
		clear = FALSE;
		r = conf_get_int_int(conf, CONF_colours, i*3+0);
		g = conf_get_int_int(conf, CONF_colours, i*3+1);
		b = conf_get_int_int(conf, CONF_colours, i*3+2);
	    }
	    update = TRUE;
	}
    } else if (event == EVENT_VALCHANGE) {
	if (ctrl == cd->redit || ctrl == cd->gedit || ctrl == cd->bedit) {
	    /* The user has changed the colour using the edit boxes. */
	    char *str;
	    int i, cval;

	    str = dlg_editbox_get(ctrl, dlg);
	    cval = atoi(str);
	    sfree(str);
	    if (cval > 255) cval = 255;
	    if (cval < 0)   cval = 0;

	    i = dlg_listbox_index(cd->listbox, dlg);
	    if (i >= 0) {
		if (ctrl == cd->redit)
		    conf_set_int_int(conf, CONF_colours, i*3+0, cval);
		else if (ctrl == cd->gedit)
		    conf_set_int_int(conf, CONF_colours, i*3+1, cval);
		else if (ctrl == cd->bedit)
		    conf_set_int_int(conf, CONF_colours, i*3+2, cval);
	    }
	}
    } else if (event == EVENT_ACTION) {
	if (ctrl == cd->button) {
	    int i = dlg_listbox_index(cd->listbox, dlg);
	    if (i < 0) {
		dlg_beep(dlg);
		return;
	    }
	    /*
	     * Start a colour selector, which will send us an
	     * EVENT_CALLBACK when it's finished and allow us to
	     * pick up the results.
	     */
	    dlg_coloursel_start(ctrl, dlg,
				conf_get_int_int(conf, CONF_colours, i*3+0),
				conf_get_int_int(conf, CONF_colours, i*3+1),
				conf_get_int_int(conf, CONF_colours, i*3+2));
	}
    } else if (event == EVENT_CALLBACK) {
	if (ctrl == cd->button) {
	    int i = dlg_listbox_index(cd->listbox, dlg);
	    /*
	     * Collect the results of the colour selector. Will
	     * return nonzero on success, or zero if the colour
	     * selector did nothing (user hit Cancel, for example).
	     */
	    if (dlg_coloursel_results(ctrl, dlg, &r, &g, &b)) {
		conf_set_int_int(conf, CONF_colours, i*3+0, r);
		conf_set_int_int(conf, CONF_colours, i*3+1, g);
		conf_set_int_int(conf, CONF_colours, i*3+2, b);
		clear = FALSE;
		update = TRUE;
	    }
	}
    }

    if (update) {
	if (clear) {
	    dlg_editbox_set(cd->redit, dlg, "");
	    dlg_editbox_set(cd->gedit, dlg, "");
	    dlg_editbox_set(cd->bedit, dlg, "");
	} else {
	    char buf[40];
	    sprintf(buf, "%d", r); dlg_editbox_set(cd->redit, dlg, buf);
	    sprintf(buf, "%d", g); dlg_editbox_set(cd->gedit, dlg, buf);
	    sprintf(buf, "%d", b); dlg_editbox_set(cd->bedit, dlg, buf);
	}
    }
}

struct ttymodes_data {
    union control *valradio, *valbox, *setbutton, *listbox;
};

static void ttymodes_handler(union control *ctrl, void *dlg,
			     void *data, int event)
{
    Conf *conf = (Conf *)data;
    struct ttymodes_data *td =
	(struct ttymodes_data *)ctrl->generic.context.p;

    if (event == EVENT_REFRESH) {
	if (ctrl == td->listbox) {
	    char *key, *val;
	    dlg_update_start(ctrl, dlg);
	    dlg_listbox_clear(ctrl, dlg);
	    for (val = conf_get_str_strs(conf, CONF_ttymodes, NULL, &key);
		 val != NULL;
		 val = conf_get_str_strs(conf, CONF_ttymodes, key, &key)) {
		char *disp = dupprintf("%s\t%s", key,
				       (val[0] == 'A') ? "(自动)" :
				       ((val[0] == 'N') ? "(不发送)"
							: val+1));
		dlg_listbox_add(ctrl, dlg, disp);
		sfree(disp);
	    }
	    dlg_update_done(ctrl, dlg);
	} else if (ctrl == td->valradio) {
	    dlg_radiobutton_set(ctrl, dlg, 0);
	}
    } else if (event == EVENT_SELCHANGE) {
	if (ctrl == td->listbox) {
	    int ind = dlg_listbox_index(td->listbox, dlg);
	    char *val;
	    if (ind < 0) {
		return; /* no item selected */
	    }
	    val = conf_get_str_str(conf, CONF_ttymodes,
				   conf_get_str_nthstrkey(conf, CONF_ttymodes,
							  ind));
	    assert(val != NULL);
	    /* Do this first to defuse side-effects on radio buttons: */
	    dlg_editbox_set(td->valbox, dlg, val+1);
	    dlg_radiobutton_set(td->valradio, dlg,
				val[0] == 'A' ? 0 : (val[0] == 'N' ? 1 : 2));
	}
    } else if (event == EVENT_VALCHANGE) {
	if (ctrl == td->valbox) {
	    /* If they're editing the text box, we assume they want its
	     * value to be used. */
	    dlg_radiobutton_set(td->valradio, dlg, 2);
	}
    } else if (event == EVENT_ACTION) {
	if (ctrl == td->setbutton) {
	    int ind = dlg_listbox_index(td->listbox, dlg);
	    const char *key;
	    char *str, *val;
	    char type;

	    {
                const char types[] = {'A', 'N', 'V'};
		int button = dlg_radiobutton_get(td->valradio, dlg);
		assert(button >= 0 && button < lenof(types));
		type = types[button];
	    }

	    /* Construct new entry */
	    if (ind >= 0) {
		key = conf_get_str_nthstrkey(conf, CONF_ttymodes, ind);
		str = (type == 'V' ? dlg_editbox_get(td->valbox, dlg)
				   : dupstr(""));
		val = dupprintf("%c%s", type, str);
		sfree(str);
		conf_set_str_str(conf, CONF_ttymodes, key, val);
		sfree(val);
		dlg_refresh(td->listbox, dlg);
		dlg_listbox_select(td->listbox, dlg, ind);
	    } else {
		/* Not a multisel listbox, so this means nothing selected */
		dlg_beep(dlg);
	    }
	}
    }
}

struct environ_data {
    union control *varbox, *valbox, *addbutton, *rembutton, *listbox;
};

static void environ_handler(union control *ctrl, void *dlg,
			    void *data, int event)
{
    Conf *conf = (Conf *)data;
    struct environ_data *ed =
	(struct environ_data *)ctrl->generic.context.p;

    if (event == EVENT_REFRESH) {
	if (ctrl == ed->listbox) {
	    char *key, *val;
	    dlg_update_start(ctrl, dlg);
	    dlg_listbox_clear(ctrl, dlg);
	    for (val = conf_get_str_strs(conf, CONF_environmt, NULL, &key);
		 val != NULL;
		 val = conf_get_str_strs(conf, CONF_environmt, key, &key)) {
		char *p = dupprintf("%s\t%s", key, val);
		dlg_listbox_add(ctrl, dlg, p);
		sfree(p);
	    }
	    dlg_update_done(ctrl, dlg);
	}
    } else if (event == EVENT_ACTION) {
	if (ctrl == ed->addbutton) {
	    char *key, *val, *str;
	    key = dlg_editbox_get(ed->varbox, dlg);
	    if (!*key) {
		sfree(key);
		dlg_beep(dlg);
		return;
	    }
	    val = dlg_editbox_get(ed->valbox, dlg);
	    if (!*val) {
		sfree(key);
		sfree(val);
		dlg_beep(dlg);
		return;
	    }
	    conf_set_str_str(conf, CONF_environmt, key, val);
	    str = dupcat(key, "\t", val, NULL);
	    dlg_editbox_set(ed->varbox, dlg, "");
	    dlg_editbox_set(ed->valbox, dlg, "");
	    sfree(str);
	    sfree(key);
	    sfree(val);
	    dlg_refresh(ed->listbox, dlg);
	} else if (ctrl == ed->rembutton) {
	    int i = dlg_listbox_index(ed->listbox, dlg);
	    if (i < 0) {
		dlg_beep(dlg);
	    } else {
		char *key, *val;

		key = conf_get_str_nthstrkey(conf, CONF_environmt, i);
		if (key) {
		    /* Populate controls with the entry we're about to delete
		     * for ease of editing */
		    val = conf_get_str_str(conf, CONF_environmt, key);
		    dlg_editbox_set(ed->varbox, dlg, key);
		    dlg_editbox_set(ed->valbox, dlg, val);
		    /* And delete it */
		    conf_del_str_str(conf, CONF_environmt, key);
		}
	    }
	    dlg_refresh(ed->listbox, dlg);
	}
    }
}

struct portfwd_data {
    union control *addbutton, *rembutton, *listbox;
    union control *sourcebox, *destbox, *direction;
#ifndef NO_IPV6
    union control *addressfamily;
#endif
};

static void portfwd_handler(union control *ctrl, void *dlg,
			    void *data, int event)
{
    Conf *conf = (Conf *)data;
    struct portfwd_data *pfd =
	(struct portfwd_data *)ctrl->generic.context.p;

    if (event == EVENT_REFRESH) {
	if (ctrl == pfd->listbox) {
	    char *key, *val;
	    dlg_update_start(ctrl, dlg);
	    dlg_listbox_clear(ctrl, dlg);
	    for (val = conf_get_str_strs(conf, CONF_portfwd, NULL, &key);
		 val != NULL;
		 val = conf_get_str_strs(conf, CONF_portfwd, key, &key)) {
		char *p;
                if (!strcmp(val, "D")) {
                    char *L;
                    /*
                     * A dynamic forwarding is stored as L12345=D or
                     * 6L12345=D (since it's mutually exclusive with
                     * L12345=anything else), but displayed as D12345
                     * to match the fiction that 'Local', 'Remote' and
                     * 'Dynamic' are three distinct modes and also to
                     * align with OpenSSH's command line option syntax
                     * that people will already be used to. So, for
                     * display purposes, find the L in the key string
                     * and turn it into a D.
                     */
                    p = dupprintf("%s\t", key);
                    L = strchr(p, 'L');
                    if (L) *L = 'D';
                } else
                    p = dupprintf("%s\t%s", key, val);
		dlg_listbox_add(ctrl, dlg, p);
		sfree(p);
	    }
	    dlg_update_done(ctrl, dlg);
	} else if (ctrl == pfd->direction) {
	    /*
	     * Default is Local.
	     */
	    dlg_radiobutton_set(ctrl, dlg, 0);
#ifndef NO_IPV6
	} else if (ctrl == pfd->addressfamily) {
	    dlg_radiobutton_set(ctrl, dlg, 0);
#endif
	}
    } else if (event == EVENT_ACTION) {
	if (ctrl == pfd->addbutton) {
	    const char *family, *type;
            char *src, *key, *val;
	    int whichbutton;

#ifndef NO_IPV6
	    whichbutton = dlg_radiobutton_get(pfd->addressfamily, dlg);
	    if (whichbutton == 1)
		family = "4";
	    else if (whichbutton == 2)
		family = "6";
	    else
#endif
		family = "";

	    whichbutton = dlg_radiobutton_get(pfd->direction, dlg);
	    if (whichbutton == 0)
		type = "L";
	    else if (whichbutton == 1)
		type = "R";
	    else
		type = "D";

	    src = dlg_editbox_get(pfd->sourcebox, dlg);
	    if (!*src) {
		dlg_error_msg(dlg, "需要指定一个来源端口数字");
		sfree(src);
		return;
	    }
	    if (*type != 'D') {
		val = dlg_editbox_get(pfd->destbox, dlg);
		if (!*val || !host_strchr(val, ':')) {
		    dlg_error_msg(dlg,
				  "需要在表单指定一个目标地址\n"
				  "如 \"host.name:port\"");
		    sfree(src);
		    sfree(val);
		    return;
		}
	    } else {
                type = "L";
		val = dupstr("D");     /* special case */
            }

	    key = dupcat(family, type, src, NULL);
	    sfree(src);

	    if (conf_get_str_str_opt(conf, CONF_portfwd, key)) {
		dlg_error_msg(dlg, "指定的转向已经存在");
	    } else {
		conf_set_str_str(conf, CONF_portfwd, key, val);
	    }

	    sfree(key);
	    sfree(val);
	    dlg_refresh(pfd->listbox, dlg);
	} else if (ctrl == pfd->rembutton) {
	    int i = dlg_listbox_index(pfd->listbox, dlg);
	    if (i < 0) {
		dlg_beep(dlg);
	    } else {
		char *key, *p;
                const char *val;

		key = conf_get_str_nthstrkey(conf, CONF_portfwd, i);
		if (key) {
		    static const char *const afs = "A46";
		    static const char *const dirs = "LRD";
		    char *afp;
		    int dir;
#ifndef NO_IPV6
		    int idx;
#endif

		    /* Populate controls with the entry we're about to delete
		     * for ease of editing */
		    p = key;

		    afp = strchr(afs, *p);
#ifndef NO_IPV6
		    idx = afp ? afp-afs : 0;
#endif
		    if (afp)
			p++;
#ifndef NO_IPV6
		    dlg_radiobutton_set(pfd->addressfamily, dlg, idx);
#endif

		    dir = *p;

                    val = conf_get_str_str(conf, CONF_portfwd, key);
		    if (!strcmp(val, "D")) {
                        dir = 'D';
			val = "";
		    }

		    dlg_radiobutton_set(pfd->direction, dlg,
					strchr(dirs, dir) - dirs);
		    p++;

		    dlg_editbox_set(pfd->sourcebox, dlg, p);
		    dlg_editbox_set(pfd->destbox, dlg, val);
		    /* And delete it */
		    conf_del_str_str(conf, CONF_portfwd, key);
		}
	    }
	    dlg_refresh(pfd->listbox, dlg);
	}
    }
}

struct manual_hostkey_data {
    union control *addbutton, *rembutton, *listbox, *keybox;
};

static void manual_hostkey_handler(union control *ctrl, void *dlg,
                                   void *data, int event)
{
    Conf *conf = (Conf *)data;
    struct manual_hostkey_data *mh =
	(struct manual_hostkey_data *)ctrl->generic.context.p;

    if (event == EVENT_REFRESH) {
	if (ctrl == mh->listbox) {
	    char *key, *val;
	    dlg_update_start(ctrl, dlg);
	    dlg_listbox_clear(ctrl, dlg);
	    for (val = conf_get_str_strs(conf, CONF_ssh_manual_hostkeys,
                                         NULL, &key);
		 val != NULL;
		 val = conf_get_str_strs(conf, CONF_ssh_manual_hostkeys,
                                         key, &key)) {
		dlg_listbox_add(ctrl, dlg, key);
	    }
	    dlg_update_done(ctrl, dlg);
	}
    } else if (event == EVENT_ACTION) {
	if (ctrl == mh->addbutton) {
	    char *key;

	    key = dlg_editbox_get(mh->keybox, dlg);
	    if (!*key) {
		dlg_error_msg(dlg, "需要指定一个主机密钥或"
                              "指纹");
		sfree(key);
		return;
	    }

            if (!validate_manual_hostkey(key)) {
		dlg_error_msg(dlg, "主机密钥不是一个有效的格式");
            } else if (conf_get_str_str_opt(conf, CONF_ssh_manual_hostkeys,
                                            key)) {
		dlg_error_msg(dlg, "指定的主机密钥已存在");
	    } else {
		conf_set_str_str(conf, CONF_ssh_manual_hostkeys, key, "");
	    }

	    sfree(key);
	    dlg_refresh(mh->listbox, dlg);
	} else if (ctrl == mh->rembutton) {
	    int i = dlg_listbox_index(mh->listbox, dlg);
	    if (i < 0) {
		dlg_beep(dlg);
	    } else {
		char *key;

		key = conf_get_str_nthstrkey(conf, CONF_ssh_manual_hostkeys, i);
		if (key) {
		    dlg_editbox_set(mh->keybox, dlg, key);
		    /* And delete it */
		    conf_del_str_str(conf, CONF_ssh_manual_hostkeys, key);
		}
	    }
	    dlg_refresh(mh->listbox, dlg);
	}
    }
}

void setup_config_box(struct controlbox *b, int midsession,
		      int protocol, int protcfginfo)
{
    struct controlset *s;
    struct sessionsaver_data *ssd;
    struct charclass_data *ccd;
    struct colour_data *cd;
    struct ttymodes_data *td;
    struct environ_data *ed;
    struct portfwd_data *pfd;
    struct manual_hostkey_data *mh;
    union control *c;
    char *str;

    ssd = (struct sessionsaver_data *)
	ctrl_alloc_with_free(b, sizeof(struct sessionsaver_data),
                             sessionsaver_data_free);
    memset(ssd, 0, sizeof(*ssd));
    ssd->savedsession = dupstr("");
    ssd->midsession = midsession;

    /*
     * The standard panel that appears at the bottom of all panels:
     * Open, Cancel, Apply etc.
     */
    s = ctrl_getset(b, "", "", "");
    ctrl_columns(s, 5, 20, 20, 20, 20, 20);
    ssd->okbutton = ctrl_pushbutton(s,
				    (midsession ? "应用(A)" : "打开(O)"),
				    (char)(midsession ? 'a' : 'o'),
				    HELPCTX(no_help),
				    sessionsaver_handler, P(ssd));
    ssd->okbutton->button.isdefault = TRUE;
    ssd->okbutton->generic.column = 3;
    ssd->cancelbutton = ctrl_pushbutton(s, "取消(C)", 'c', HELPCTX(no_help),
					sessionsaver_handler, P(ssd));
    ssd->cancelbutton->button.iscancel = TRUE;
    ssd->cancelbutton->generic.column = 4;
    /* We carefully don't close the 5-column part, so that platform-
     * specific add-ons can put extra buttons alongside Open and Cancel. */

    /*
     * The Session panel.
     */
    str = dupprintf("%s 会话基本设置", appname);
    ctrl_settitle(b, "会话", str);
    sfree(str);

    if (!midsession) {
	struct hostport *hp = (struct hostport *)
	    ctrl_alloc(b, sizeof(struct hostport));

	s = ctrl_getset(b, "会话", "hostport",
			"指定要连接的目的地址");
	ctrl_columns(s, 2, 75, 25);
	c = ctrl_editbox(s, HOST_BOX_TITLE, 'n', 100,
			 HELPCTX(session_hostname),
			 config_host_handler, I(0), I(0));
	c->generic.column = 0;
	hp->host = c;
	c = ctrl_editbox(s, PORT_BOX_TITLE, 'p', 100,
			 HELPCTX(session_hostname),
			 config_port_handler, I(0), I(0));
	c->generic.column = 1;
	hp->port = c;
	ctrl_columns(s, 1, 100);

	if (!backend_from_proto(PROT_SSH)) {
	    ctrl_radiobuttons(s, "连接类型：", NO_SHORTCUT, 3,
			      HELPCTX(session_hostname),
			      config_protocolbuttons_handler, P(hp),
			      "Raw", 'w', I(PROT_RAW),
			      "Telnet", 't', I(PROT_TELNET),
			      "Rlogin", 'i', I(PROT_RLOGIN),
			      NULL);
	} else {
	    ctrl_radiobuttons(s, "连接类型：", NO_SHORTCUT, 4,
			      HELPCTX(session_hostname),
			      config_protocolbuttons_handler, P(hp),
			      "Raw", 'w', I(PROT_RAW),
			      "Telnet", 't', I(PROT_TELNET),
			      "Rlogin", 'i', I(PROT_RLOGIN),
			      "SSH", 's', I(PROT_SSH),
			      NULL);
	}
    }

    /*
     * The Load/Save panel is available even in mid-session.
     */
    s = ctrl_getset(b, "会话", "savedsessions",
		    midsession ? "保存当前会话设置" :
		    "载入、保存或删除已存在的会话");
    ctrl_columns(s, 2, 75, 25);
    get_sesslist(&ssd->sesslist, TRUE);
    ssd->editbox = ctrl_editbox(s, "保存的会话(E)", 'e', 100,
				HELPCTX(session_saved),
				sessionsaver_handler, P(ssd), P(NULL));
    ssd->editbox->generic.column = 0;
    /* Reset columns so that the buttons are alongside the list, rather
     * than alongside that edit box. */
    ctrl_columns(s, 1, 100);
    ctrl_columns(s, 2, 75, 25);
    ssd->listbox = ctrl_listbox(s, NULL, NO_SHORTCUT,
				HELPCTX(session_saved),
				sessionsaver_handler, P(ssd));
    ssd->listbox->generic.column = 0;
    ssd->listbox->listbox.height = 7;
    if (!midsession) {
	ssd->loadbutton = ctrl_pushbutton(s, "载入(L)", 'l',
					  HELPCTX(session_saved),
					  sessionsaver_handler, P(ssd));
	ssd->loadbutton->generic.column = 1;
    } else {
	/* We can't offer the Load button mid-session, as it would allow the
	 * user to load and subsequently save settings they can't see. (And
	 * also change otherwise immutable settings underfoot; that probably
	 * shouldn't be a problem, but.) */
	ssd->loadbutton = NULL;
    }
    /* "Save" button is permitted mid-session. */
    ssd->savebutton = ctrl_pushbutton(s, "保存(V)", 'v',
				      HELPCTX(session_saved),
				      sessionsaver_handler, P(ssd));
    ssd->savebutton->generic.column = 1;
    if (!midsession) {
	ssd->delbutton = ctrl_pushbutton(s, "删除(D)", 'd',
					 HELPCTX(session_saved),
					 sessionsaver_handler, P(ssd));
	ssd->delbutton->generic.column = 1;
    } else {
	/* Disable the Delete button mid-session too, for UI consistency. */
	ssd->delbutton = NULL;
    }
    ctrl_columns(s, 1, 100);

    s = ctrl_getset(b, "会话", "otheropts", NULL);
    ctrl_radiobuttons(s, "退出时关闭窗口(X)：", 'x', 4,
                      HELPCTX(session_coe),
                      conf_radiobutton_handler,
                      I(CONF_close_on_exit),
                      "总是", I(FORCE_ON),
                      "从不", I(FORCE_OFF),
                      "仅正常退出", I(AUTO), NULL);

    /*
     * The Session/Logging panel.
     */
    ctrl_settitle(b, "会话/日志记录", "会话日志记录选项");

    s = ctrl_getset(b, "会话/日志记录", "main", NULL);
    /*
     * The logging buttons change depending on whether SSH packet
     * logging can sensibly be available.
     */
    {
	const char *sshlogname, *sshrawlogname;
	if ((midsession && protocol == PROT_SSH) ||
	    (!midsession && backend_from_proto(PROT_SSH))) {
	    sshlogname = "SSH 包";
	    sshrawlogname = "SSH 包和 RAW 数据";
        } else {
	    sshlogname = NULL;	       /* this will disable both buttons */
	    sshrawlogname = NULL;      /* this will just placate optimisers */
        }
	ctrl_radiobuttons(s, "会话日志记录：", NO_SHORTCUT, 2,
			  HELPCTX(logging_main),
			  loggingbuttons_handler,
			  I(CONF_logtype),
			  "无(T)", 't', I(LGTYP_NONE),
			  "可打印输出(P)", 'p', I(LGTYP_ASCII),
			  "所有会话输出(L)", 'l', I(LGTYP_DEBUG),
			  sshlogname, 's', I(LGTYP_PACKETS),
			  sshrawlogname, 'r', I(LGTYP_SSHRAW),
			  NULL);
    }
    ctrl_filesel(s, "日志文件名(F)：", 'f',
		 NULL, TRUE, "选择会话的日志文件名",
		 HELPCTX(logging_filename),
		 conf_filesel_handler, I(CONF_logfilename));
    ctrl_text(s, "(日志文件名可以包含 &Y &M &D 表示年月日，"
	      "&T 表示时间，&H 表示主机名称)",
	      HELPCTX(logging_filename));
    ctrl_radiobuttons(s, "要记录的日志文件已存在时(E)：", 'e', 1,
		      HELPCTX(logging_exists),
		      conf_radiobutton_handler, I(CONF_logxfovr),
		      "总是覆盖", I(LGXF_OVR),
		      "总是添加到末尾", I(LGXF_APN),
		      "每次询问", I(LGXF_ASK), NULL);
    ctrl_checkbox(s, "快速刷新缓存到日志文件(U)", 'u',
		 HELPCTX(logging_flush),
		 conf_checkbox_handler, I(CONF_logflush));

    if ((midsession && protocol == PROT_SSH) ||
	(!midsession && backend_from_proto(PROT_SSH))) {
	s = ctrl_getset(b, "会话/日志记录", "ssh",
			"指定 SSH 包日志记录设置");
	ctrl_checkbox(s, "忽略已知的密码域(K)", 'k',
		      HELPCTX(logging_ssh_omit_password),
		      conf_checkbox_handler, I(CONF_logomitpass));
	ctrl_checkbox(s, "忽略会话数据(D)", 'd',
		      HELPCTX(logging_ssh_omit_data),
		      conf_checkbox_handler, I(CONF_logomitdata));
    }

    /*
     * The Terminal panel.
     */
    ctrl_settitle(b, "终端", "终端模拟设置");

    s = ctrl_getset(b, "终端", "general", "设置不同的终端选项");
    ctrl_checkbox(s, "初始开启自动换行(W)", 'w',
		  HELPCTX(terminal_autowrap),
		  conf_checkbox_handler, I(CONF_wrap_mode));
    ctrl_checkbox(s, "初始开启 DEC 原始模式", 'd',
		  HELPCTX(terminal_decom),
		  conf_checkbox_handler, I(CONF_dec_om));
    ctrl_checkbox(s, "在每个 LF 字符后增加 CR 字符", 'r',
		  HELPCTX(terminal_lfhascr),
		  conf_checkbox_handler, I(CONF_lfhascr));
    ctrl_checkbox(s, "在每个 CR 字符后增加 LF 字符", 'f',
		  HELPCTX(terminal_crhaslf),
		  conf_checkbox_handler, I(CONF_crhaslf));
    ctrl_checkbox(s, "使用背景颜色清除屏幕(E)", 'e',
		  HELPCTX(terminal_bce),
		  conf_checkbox_handler, I(CONF_bce));
    ctrl_checkbox(s, "开启闪烁字文本(N)", 'n',
		  HELPCTX(terminal_blink),
		  conf_checkbox_handler, I(CONF_blinktext));
    ctrl_editbox(s, "^E 回应(S)：", 's', 100,
		 HELPCTX(terminal_answerback),
		 conf_editbox_handler, I(CONF_answerback), I(1));

    s = ctrl_getset(b, "终端", "ldisc", "行规则选项");
    ctrl_radiobuttons(s, "本地回应(L)：", 'l', 3,
		      HELPCTX(terminal_localecho),
		      conf_radiobutton_handler,I(CONF_localecho),
		      "自动", I(AUTO),
		      "强制开", I(FORCE_ON),
		      "强制关", I(FORCE_OFF), NULL);
    ctrl_radiobuttons(s, "本地行编辑(T)：", 't', 3,
		      HELPCTX(terminal_localedit),
		      conf_radiobutton_handler,I(CONF_localedit),
		      "自动", I(AUTO),
		      "强制开", I(FORCE_ON),
		      "强制关", I(FORCE_OFF), NULL);

    s = ctrl_getset(b, "终端", "printing", "远程控制打印");
    ctrl_combobox(s, "打印发送 ANSI 打印机输出到(P)：", 'p', 100,
		  HELPCTX(terminal_printing),
		  printerbox_handler, P(NULL), P(NULL));

    /*
     * The Terminal/Keyboard panel.
     */
    ctrl_settitle(b, "终端/键盘",
		  "按键效果设置");

    s = ctrl_getset(b, "终端/键盘", "mappings",
		    "修改按键码发送序列：");
    ctrl_radiobuttons(s, "Backspace 回退键", 'b', 2,
		      HELPCTX(keyboard_backspace),
		      conf_radiobutton_handler,
		      I(CONF_bksp_is_delete),
		      "Control-H", I(0), "Control-? (127)", I(1), NULL);
    ctrl_radiobuttons(s, "Home 和 End 键", 'e', 2,
		      HELPCTX(keyboard_homeend),
		      conf_radiobutton_handler,
		      I(CONF_rxvt_homeend),
		      "标准", I(0), "rxvt", I(1), NULL);
    ctrl_radiobuttons(s, "Fn 功能键和小键盘", 'f', 3,
		      HELPCTX(keyboard_funkeys),
		      conf_radiobutton_handler,
		      I(CONF_funky_type),
		      "ESC[n~", I(0), "Linux", I(1), "Xterm R6", I(2),
		      "VT400", I(3), "VT100+", I(4), "SCO", I(5), NULL);

    s = ctrl_getset(b, "终端/键盘", "appkeypad",
		    "应用小键盘设置：");
    ctrl_radiobuttons(s, "光标键初始状态(R)：", 'r', 3,
		      HELPCTX(keyboard_appcursor),
		      conf_radiobutton_handler,
		      I(CONF_app_cursor),
		      "常规", I(0), "应用", I(1), NULL);
    ctrl_radiobuttons(s, "数字小键盘初始状态(N)：", 'n', 3,
		      HELPCTX(keyboard_appkeypad),
		      numeric_keypad_handler, P(NULL),
		      "常规", I(0), "应用", I(1), "NetHack", I(2),
		      NULL);

    /*
     * The Terminal/Bell panel.
     */
    ctrl_settitle(b, "终端/响铃",
		  "终端响铃设置");

    s = ctrl_getset(b, "终端/响铃", "style", "设置响铃类型");
    ctrl_radiobuttons(s, "发生响铃时动作(B)：", 'b', 1,
		      HELPCTX(bell_style),
		      conf_radiobutton_handler, I(CONF_beep),
		      "无 (禁止响铃)", I(BELL_DISABLED),
		      "使用系统默认警告声音", I(BELL_DEFAULT),
		      "可视响铃 (闪动窗口)", I(BELL_VISUAL), NULL);

    s = ctrl_getset(b, "终端/响铃", "overload",
		    "设置重复响铃处理");
    ctrl_checkbox(s, "大量重复响铃时临时禁止响铃(D)", 'd',
		  HELPCTX(bell_overload),
		  conf_checkbox_handler, I(CONF_bellovl));
    ctrl_editbox(s, "重复响铃最少数目(M)：", 'm', 20,
		 HELPCTX(bell_overload),
		 conf_editbox_handler, I(CONF_bellovl_n), I(-1));
    ctrl_editbox(s, "重复响铃计算时间(秒)(T)：", 't', 20,
		 HELPCTX(bell_overload),
		 conf_editbox_handler, I(CONF_bellovl_t),
		 I(-TICKSPERSEC));
    ctrl_text(s, "响铃将在被禁止一段时间后重新被允许。",
	      HELPCTX(bell_overload));
    ctrl_editbox(s, "被禁止响铃的时间(秒)(S)：", 's', 20,
		 HELPCTX(bell_overload),
		 conf_editbox_handler, I(CONF_bellovl_s),
		 I(-TICKSPERSEC));

    /*
     * The Terminal/Features panel.
     */
    ctrl_settitle(b, "终端/特性",
		  "允许或禁止高级终端特性");

    s = ctrl_getset(b, "终端/特性", "main", NULL);
    ctrl_checkbox(s, "禁止应用光标键模式(U)", 'u',
		  HELPCTX(features_application),
		  conf_checkbox_handler, I(CONF_no_applic_c));
    ctrl_checkbox(s, "禁止应用小键盘模式(K)", 'k',
		  HELPCTX(features_application),
		  conf_checkbox_handler, I(CONF_no_applic_k));
    ctrl_checkbox(s, "禁止 xterm 类型鼠标报告", 'x',
		  HELPCTX(features_mouse),
		  conf_checkbox_handler, I(CONF_no_mouse_rep));
    ctrl_checkbox(s, "禁止改变远程控制终端大小(S)", 's',
		  HELPCTX(features_resize),
		  conf_checkbox_handler,
		  I(CONF_no_remote_resize));
    ctrl_checkbox(s, "禁止切换终端屏幕(W)", 'w',
		  HELPCTX(features_altscreen),
		  conf_checkbox_handler, I(CONF_no_alt_screen));
    ctrl_checkbox(s, "禁止改变远程控制窗口标题(T)", 't',
		  HELPCTX(features_retitle),
		  conf_checkbox_handler,
		  I(CONF_no_remote_wintitle));
    ctrl_checkbox(s, "禁止远程控制清除回滚(E)", 'e',
		  HELPCTX(features_clearscroll),
		  conf_checkbox_handler,
		  I(CONF_no_remote_clearscroll));
    ctrl_radiobuttons(s, "远程标题查询回应(SECURITY)(Q)：", 'q', 3,
		      HELPCTX(features_qtitle),
		      conf_radiobutton_handler,
		      I(CONF_remote_qtitle_action),
		      "无", I(TITLE_NONE),
		      "空字符串", I(TITLE_EMPTY),
		      "窗口标题", I(TITLE_REAL), NULL);
    ctrl_checkbox(s, "禁止服务器发送 ^? 时破坏性回退删除(B)",'b',
		  HELPCTX(features_dbackspace),
		  conf_checkbox_handler, I(CONF_no_dbackspace));
    ctrl_checkbox(s, "禁止远程控制字符集设置(R)",
		  'r', HELPCTX(features_charset), conf_checkbox_handler,
		  I(CONF_no_remote_charset));
    ctrl_checkbox(s, "禁止修整阿拉伯文本(L)",
		  'l', HELPCTX(features_arabicshaping), conf_checkbox_handler,
		  I(CONF_arabicshaping));
    ctrl_checkbox(s, "禁止双向文本显示(D)",
		  'd', HELPCTX(features_bidi), conf_checkbox_handler,
		  I(CONF_bidi));

    /*
     * The Window panel.
     */
    str = dupprintf("%s 窗口设置", appname);
    ctrl_settitle(b, "窗口", str);
    sfree(str);

    s = ctrl_getset(b, "窗口", "size", "设置窗口大小");
    ctrl_columns(s, 2, 50, 50);
    c = ctrl_editbox(s, "列(M)", 'm', 100,
		     HELPCTX(window_size),
		     conf_editbox_handler, I(CONF_width), I(-1));
    c->generic.column = 0;
    c = ctrl_editbox(s, "行(R)", 'r', 100,
		     HELPCTX(window_size),
		     conf_editbox_handler, I(CONF_height),I(-1));
    c->generic.column = 1;
    ctrl_columns(s, 1, 100);

    s = ctrl_getset(b, "窗口", "scrollback",
		    "设置窗口回滚");
    ctrl_editbox(s, "回滚行数(S)", 's', 50,
		 HELPCTX(window_scrollback),
		 conf_editbox_handler, I(CONF_savelines), I(-1));
    ctrl_checkbox(s, "显示滚动条(D)", 'd',
		  HELPCTX(window_scrollback),
		  conf_checkbox_handler, I(CONF_scrollbar));
    ctrl_checkbox(s, "按键时重置回滚(K)", 'k',
		  HELPCTX(window_scrollback),
		  conf_checkbox_handler, I(CONF_scroll_on_key));
    ctrl_checkbox(s, "刷新显示时重置回滚(P)", 'p',
		  HELPCTX(window_scrollback),
		  conf_checkbox_handler, I(CONF_scroll_on_disp));
    ctrl_checkbox(s, "将清除的文本压入回滚(E)", 'e',
		  HELPCTX(window_erased),
		  conf_checkbox_handler,
		  I(CONF_erase_to_scrollback));

    /*
     * The Window/Appearance panel.
     */
    str = dupprintf("配置 %s 窗口外观", appname);
    ctrl_settitle(b, "窗口/外观", str);
    sfree(str);

    s = ctrl_getset(b, "窗口/外观", "cursor",
		    "调整光标");
    ctrl_radiobuttons(s, "光标显示：", NO_SHORTCUT, 3,
		      HELPCTX(appearance_cursor),
		      conf_radiobutton_handler,
		      I(CONF_cursor_type),
		      "显示块(L)", 'l', I(0),
		      "下划线(U)", 'u', I(1),
		      "垂直线(V)", 'v', I(2), NULL);
    ctrl_checkbox(s, "光标闪烁(B)", 'b',
		  HELPCTX(appearance_cursor),
		  conf_checkbox_handler, I(CONF_blink_cur));

    s = ctrl_getset(b, "窗口/外观", "font",
		    "字体设置");
    ctrl_fontsel(s, "终端窗口使用的字体(N)", 'n',
		 HELPCTX(appearance_font),
		 conf_fontsel_handler, I(CONF_font));

    s = ctrl_getset(b, "窗口/外观", "mouse",
		    "调整鼠标指针");
    ctrl_checkbox(s, "在窗口中输入时隐藏鼠标指针(P)", 'p',
		  HELPCTX(appearance_hidemouse),
		  conf_checkbox_handler, I(CONF_hide_mouseptr));

    s = ctrl_getset(b, "窗口/外观", "border",
		    "调整窗口边框");
    ctrl_editbox(s, "文本与窗口边界的距离(E)：", 'e', 20,
		 HELPCTX(appearance_border),
		 conf_editbox_handler,
		 I(CONF_window_border), I(-1));

    /*
     * The Window/Behaviour panel.
     */
    str = dupprintf("配置 %s 窗口行为", appname);
    ctrl_settitle(b, "窗口/行为", str);
    sfree(str);

    s = ctrl_getset(b, "窗口/行为", "title",
		    "调整窗口标题外观");
    ctrl_editbox(s, "窗口标题(T)：", 't', 100,
		 HELPCTX(appearance_title),
		 conf_editbox_handler, I(CONF_wintitle), I(1));
    ctrl_checkbox(s, "使用单独的客户区窗口(I)", 'i',
		  HELPCTX(appearance_title),
		  conf_checkbox_handler,
		  I(CHECKBOX_INVERT | CONF_win_name_always));

    s = ctrl_getset(b, "窗口/行为", "main", NULL);
    ctrl_checkbox(s, "关闭窗口时警告(W)", 'w',
		  HELPCTX(behaviour_closewarn),
		  conf_checkbox_handler, I(CONF_warn_on_close));

    /*
     * The Window/Translation panel.
     */
    ctrl_settitle(b, "窗口/转换",
		  "字符集转换设置");

    s = ctrl_getset(b, "窗口/转换", "trans",
		    "字符集转换");
    ctrl_combobox(s, "远程字符集(R)：",
		  'r', 100, HELPCTX(translation_codepage),
		  codepage_handler, P(NULL), P(NULL));

    s = ctrl_getset(b, "窗口/转换", "tweaks", NULL);
    ctrl_checkbox(s, "将不确定字符处理为 CJK 字符(W)", 'w',
		  HELPCTX(translation_cjk_ambig_wide),
		  conf_checkbox_handler, I(CONF_cjk_ambig_wide));

    str = dupprintf("调整 %s 如何重画字符行", appname);
    s = ctrl_getset(b, "窗口/转换", "linedraw", str);
    sfree(str);
    ctrl_radiobuttons(s, "重画字符行处理：", NO_SHORTCUT,1,
		      HELPCTX(translation_linedraw),
		      conf_radiobutton_handler,
		      I(CONF_vtmode),
		      "使用 Unicode 统一码重画代码",'u',I(VT_UNICODE),
		      "简单重画行(+、- 和 |)(P)",'p',I(VT_POORMAN),
		      NULL);
    ctrl_checkbox(s, "重画字符类似 lqqqk 复制粘贴行(D)",'d',
		  HELPCTX(selection_linedraw),
		  conf_checkbox_handler, I(CONF_rawcnp));

    /*
     * The Window/Selection panel.
     */
    ctrl_settitle(b, "窗口/选择", "复制粘贴设置");
	
    s = ctrl_getset(b, "窗口/选择", "mouse",
		    "鼠标使用控制");
    ctrl_checkbox(s, "Shift 上档键可与鼠标组合使用(P)", 'p',
		  HELPCTX(selection_shiftdrag),
		  conf_checkbox_handler, I(CONF_mouse_override));
    ctrl_radiobuttons(s,
		      "默认选择模式(Alt 换档键拖放为另一种模式)：",
		      NO_SHORTCUT, 2,
		      HELPCTX(selection_rect),
		      conf_radiobutton_handler,
		      I(CONF_rect_select),
		      "常规(N)", 'n', I(0),
		      "矩形框(R)", 'r', I(1), NULL);

    s = ctrl_getset(b, "窗口/选择", "charclass",
		    "设置自动选取单词模式");
    ccd = (struct charclass_data *)
	ctrl_alloc(b, sizeof(struct charclass_data));
    ccd->listbox = ctrl_listbox(s, "字符类别(E)：", 'e',
				HELPCTX(selection_charclasses),
				charclass_handler, P(ccd));
    ccd->listbox->listbox.multisel = 1;
    ccd->listbox->listbox.ncols = 4;
    ccd->listbox->listbox.percentages = snewn(4, int);
    ccd->listbox->listbox.percentages[0] = 15;
    ccd->listbox->listbox.percentages[1] = 25;
    ccd->listbox->listbox.percentages[2] = 20;
    ccd->listbox->listbox.percentages[3] = 40;
    ctrl_columns(s, 2, 67, 33);
    ccd->editbox = ctrl_editbox(s, "设置到类别(T)", 't', 50,
				HELPCTX(selection_charclasses),
				charclass_handler, P(ccd), P(NULL));
    ccd->editbox->generic.column = 0;
    ccd->button = ctrl_pushbutton(s, "设置(S)", 's',
				  HELPCTX(selection_charclasses),
				  charclass_handler, P(ccd));
    ccd->button->generic.column = 1;
    ctrl_columns(s, 1, 100);

    /*
     * The Window/Colours panel.
     */
    ctrl_settitle(b, "窗口/颜色", "颜色使用设置");

    s = ctrl_getset(b, "窗口/颜色", "general",
		    "颜色使用常规设置");
    ctrl_checkbox(s, "允许终端指定 ANSI 颜色", 'i',
		  HELPCTX(colours_ansi),
		  conf_checkbox_handler, I(CONF_ansi_colour));
    ctrl_checkbox(s, "允许终端使用 xterm 256 色模式", '2',
		  HELPCTX(colours_xterm256), conf_checkbox_handler,
		  I(CONF_xterm_256_colour));
    ctrl_radiobuttons(s, "粗体文字表现(B)：", 'b', 3,
                      HELPCTX(colours_bold),
                      conf_radiobutton_handler, I(CONF_bold_style),
                      "字体", I(1),
                      "颜色", I(2),
                      "两者", I(3),
                      NULL);

    str = dupprintf("调整 %s 显示的精确颜色", appname);
    s = ctrl_getset(b, "窗口/颜色", "adjust", str);
    sfree(str);
    ctrl_text(s, "选择列表中的颜色，然后点击"
	      "“修改”按钮改变其具体数值。",
	      HELPCTX(colours_config));
    ctrl_columns(s, 2, 67, 33);
    cd = (struct colour_data *)ctrl_alloc(b, sizeof(struct colour_data));
    cd->listbox = ctrl_listbox(s, "选择颜色进行修改(U)：", 'u',
			       HELPCTX(colours_config), colour_handler, P(cd));
    cd->listbox->generic.column = 0;
    cd->listbox->listbox.height = 7;
    c = ctrl_text(s, "RGB 值：", HELPCTX(colours_config));
    c->generic.column = 1;
    cd->redit = ctrl_editbox(s, "红(R)", 'r', 50, HELPCTX(colours_config),
			     colour_handler, P(cd), P(NULL));
    cd->redit->generic.column = 1;
    cd->gedit = ctrl_editbox(s, "绿(N)", 'n', 50, HELPCTX(colours_config),
			     colour_handler, P(cd), P(NULL));
    cd->gedit->generic.column = 1;
    cd->bedit = ctrl_editbox(s, "蓝(E)", 'e', 50, HELPCTX(colours_config),
			     colour_handler, P(cd), P(NULL));
    cd->bedit->generic.column = 1;
    cd->button = ctrl_pushbutton(s, "修改(M)", 'm', HELPCTX(colours_config),
				 colour_handler, P(cd));
    cd->button->generic.column = 1;
    ctrl_columns(s, 1, 100);

    /*
     * The Connection panel. This doesn't show up if we're in a
     * non-network utility such as pterm. We tell this by being
     * passed a protocol < 0.
     */
    if (protocol >= 0) {
	ctrl_settitle(b, "连接", "连接设置");

	s = ctrl_getset(b, "连接", "keepalive",
			"发送空数据包保持会话活动");
	ctrl_editbox(s, "空包发送时间间隔(秒，0 表示关闭)(K)：", 'k', 20,
		     HELPCTX(connection_keepalive),
		     conf_editbox_handler, I(CONF_ping_interval),
		     I(-1));

	if (!midsession) {
	    s = ctrl_getset(b, "连接", "tcp",
			    "底层 TCP 连接选项");
	    ctrl_checkbox(s, "禁止 Nagle 算法(TCP_NODELAY 参数)",
			  'n', HELPCTX(connection_nodelay),
			  conf_checkbox_handler,
			  I(CONF_tcp_nodelay));
	    ctrl_checkbox(s, "允许 TCP 保持活动连接(SO_KEEPALIVE 参数)",
			  'p', HELPCTX(connection_tcpkeepalive),
			  conf_checkbox_handler,
			  I(CONF_tcp_keepalives));
#ifndef NO_IPV6
	    s = ctrl_getset(b, "连接", "ipversion",
			  "互联网协议版本");
	    ctrl_radiobuttons(s, NULL, NO_SHORTCUT, 3,
			  HELPCTX(connection_ipversion),
			  conf_radiobutton_handler,
			  I(CONF_addressfamily),
			  "自动(U)", 'u', I(ADDRTYPE_UNSPEC),
			  "IPv4", '4', I(ADDRTYPE_IPV4),
			  "IPv6", '6', I(ADDRTYPE_IPV6),
			  NULL);
#endif

	    {
		const char *label = backend_from_proto(PROT_SSH) ?
		    "远程主机的注册名字（比如用 ssh 密钥寻找）(M)：" :
		    "Logical 远程主机的注册名字(M)：";
		s = ctrl_getset(b, "连接", "identity",
				"远程主机的注册名字");
		ctrl_editbox(s, label, 'm', 100,
			     HELPCTX(connection_loghost),
			     conf_editbox_handler, I(CONF_loghost), I(1));
	    }
	}

	/*
	 * A sub-panel Connection/Data, containing options that
	 * decide on data to send to the server.
	 */
	if (!midsession) {
	    ctrl_settitle(b, "连接/数据", "传送到服务器的数据");

	    s = ctrl_getset(b, "连接/数据", "login",
			    "登录详细资料");
	    ctrl_editbox(s, "自动登录用户名(U)：", 'u', 50,
			 HELPCTX(connection_username),
			 conf_editbox_handler, I(CONF_username), I(1));
	    {
		/* We assume the local username is sufficiently stable
		 * to include on the dialog box. */
		char *user = get_username();
		char *userlabel = dupprintf("使用系统用户名 (%s)",
					    user ? user : "");
		sfree(user);
		ctrl_radiobuttons(s, "未指定用户名时(N)：", 'n', 4,
				  HELPCTX(connection_username_from_env),
				  conf_radiobutton_handler,
				  I(CONF_username_from_env),
				  "提示", I(FALSE),
				  userlabel, I(TRUE),
				  NULL);
		sfree(userlabel);
	    }

	    s = ctrl_getset(b, "连接/数据", "term",
			    "终端详细资料");
	    ctrl_editbox(s, "终端类型字符串(T)：", 't', 50,
			 HELPCTX(connection_termtype),
			 conf_editbox_handler, I(CONF_termtype), I(1));
	    ctrl_editbox(s, "终端速度(S)：", 's', 50,
			 HELPCTX(connection_termspeed),
			 conf_editbox_handler, I(CONF_termspeed), I(1));

	    s = ctrl_getset(b, "连接/数据", "env",
			    "环境变量");
	    ctrl_columns(s, 2, 80, 20);
	    ed = (struct environ_data *)
		ctrl_alloc(b, sizeof(struct environ_data));
	    ed->varbox = ctrl_editbox(s, "变量(V)：", 'v', 60,
				      HELPCTX(telnet_environ),
				      environ_handler, P(ed), P(NULL));
	    ed->varbox->generic.column = 0;
	    ed->valbox = ctrl_editbox(s, "值(L)：", 'l', 60,
				      HELPCTX(telnet_environ),
				      environ_handler, P(ed), P(NULL));
	    ed->valbox->generic.column = 0;
	    ed->addbutton = ctrl_pushbutton(s, "增加(D)", 'd',
					    HELPCTX(telnet_environ),
					    environ_handler, P(ed));
	    ed->addbutton->generic.column = 1;
	    ed->rembutton = ctrl_pushbutton(s, "删除(R)", 'r',
					    HELPCTX(telnet_environ),
					    environ_handler, P(ed));
	    ed->rembutton->generic.column = 1;
	    ctrl_columns(s, 1, 100);
	    ed->listbox = ctrl_listbox(s, NULL, NO_SHORTCUT,
				       HELPCTX(telnet_environ),
				       environ_handler, P(ed));
	    ed->listbox->listbox.height = 3;
	    ed->listbox->listbox.ncols = 2;
	    ed->listbox->listbox.percentages = snewn(2, int);
	    ed->listbox->listbox.percentages[0] = 30;
	    ed->listbox->listbox.percentages[1] = 70;
	}

    }

    if (!midsession) {
	/*
	 * The Connection/Proxy panel.
	 */
	ctrl_settitle(b, "连接/代理",
		      "代理使用设置");

	s = ctrl_getset(b, "连接/代理", "basics", NULL);
	ctrl_radiobuttons(s, "代理类型(T)：", 't', 3,
			  HELPCTX(proxy_type),
			  conf_radiobutton_handler,
			  I(CONF_proxy_type),
			  "无", I(PROXY_NONE),
			  "SOCKS 4", I(PROXY_SOCKS4),
			  "SOCKS 5", I(PROXY_SOCKS5),
			  "HTTP", I(PROXY_HTTP),
			  "Telnet", I(PROXY_TELNET),
			  NULL);
	ctrl_columns(s, 2, 80, 20);
	c = ctrl_editbox(s, "代理地址(Y)", 'y', 100,
			 HELPCTX(proxy_main),
			 conf_editbox_handler,
			 I(CONF_proxy_host), I(1));
	c->generic.column = 0;
	c = ctrl_editbox(s, "端口(P)", 'p', 100,
			 HELPCTX(proxy_main),
			 conf_editbox_handler,
			 I(CONF_proxy_port),
			 I(-1));
	c->generic.column = 1;
	ctrl_columns(s, 1, 100);
	ctrl_editbox(s, "排除在外的地址(E)", 'e', 100,
		     HELPCTX(proxy_exclude),
		     conf_editbox_handler,
		     I(CONF_proxy_exclude_list), I(1));
	ctrl_checkbox(s, "对于本地地址不使用代理(X)", 'x',
		      HELPCTX(proxy_exclude),
		      conf_checkbox_handler,
		      I(CONF_even_proxy_localhost));
	ctrl_radiobuttons(s, "在代理端进行 DNS 域名解析：", 'd', 3,
			  HELPCTX(proxy_dns),
			  conf_radiobutton_handler,
			  I(CONF_proxy_dns),
			  "否", I(FORCE_OFF),
			  "自动", I(AUTO),
			  "是", I(FORCE_ON), NULL);
	ctrl_editbox(s, "用户名(U)：", 'u', 60,
		     HELPCTX(proxy_auth),
		     conf_editbox_handler,
		     I(CONF_proxy_username), I(1));
	c = ctrl_editbox(s, "密码(W)：", 'w', 60,
			 HELPCTX(proxy_auth),
			 conf_editbox_handler,
			 I(CONF_proxy_password), I(1));
	c->editbox.password = 1;
	ctrl_editbox(s, "Telnet 命令(M)", 'm', 100,
		     HELPCTX(proxy_command),
		     conf_editbox_handler,
		     I(CONF_proxy_telnet_command), I(1));

	ctrl_radiobuttons(s, "在终端窗口"
                          "输出代理诊断信息(R)", 'r', 5,
			  HELPCTX(proxy_logging),
			  conf_radiobutton_handler,
			  I(CONF_proxy_log_to_term),
			  "否", I(FORCE_OFF),
			  "是", I(FORCE_ON),
			  "只在会话开始时", I(AUTO), NULL);
    }

    /*
     * The Telnet panel exists in the base config box, and in a
     * mid-session reconfig box _if_ we're using Telnet.
     */
    if (!midsession || protocol == PROT_TELNET) {
	/*
	 * The Connection/Telnet panel.
	 */
	ctrl_settitle(b, "连接/Telnet",
		      "Telnet 连接设置");

	s = ctrl_getset(b, "连接/Telnet", "protocol",
			"Telnet 协议调整");

	if (!midsession) {
	    ctrl_radiobuttons(s, "处理含糊的 OLD_ENVIRON 参数：",
			      NO_SHORTCUT, 2,
			      HELPCTX(telnet_oldenviron),
			      conf_radiobutton_handler,
			      I(CONF_rfc_environ),
			      "BSD (常用)", 'b', I(0),
			      "RFC 1408 (不常用)", 'f', I(1), NULL);
	    ctrl_radiobuttons(s, "Telnet 通讯模式：", 't', 2,
			      HELPCTX(telnet_passive),
			      conf_radiobutton_handler,
			      I(CONF_passive_telnet),
			      "被动", I(1), "主动", I(0), NULL);
	}
	ctrl_checkbox(s, "键盘直接发送 Telnet 特殊命令(K)", 'k',
		      HELPCTX(telnet_specialkeys),
		      conf_checkbox_handler,
		      I(CONF_telnet_keyboard));
	ctrl_checkbox(s, "发送 Telnet 新行命令替换 ^M 字符",
		      'm', HELPCTX(telnet_newline),
		      conf_checkbox_handler,
		      I(CONF_telnet_newline));
    }

    if (!midsession) {

	/*
	 * The Connection/Rlogin panel.
	 */
	ctrl_settitle(b, "连接/Rlogin",
		      "Rlogin 连接设置");

	s = ctrl_getset(b, "连接/Rlogin", "data",
			"传送到服务器的数据");
	ctrl_editbox(s, "本地用户名(L)：", 'l', 50,
		     HELPCTX(rlogin_localuser),
		     conf_editbox_handler, I(CONF_localusername), I(1));

    }

    /*
     * All the SSH stuff is omitted in PuTTYtel, or in a reconfig
     * when we're not doing SSH.
     */

    if (backend_from_proto(PROT_SSH) && (!midsession || protocol == PROT_SSH)) {

	/*
	 * The Connection/SSH panel.
	 */
	ctrl_settitle(b, "连接/SSH",
		      "SSH 连接设置");

	/* SSH-1 or connection-sharing downstream */
	if (midsession && (protcfginfo == 1 || protcfginfo == -1)) {
	    s = ctrl_getset(b, "连接/SSH", "disclaimer", NULL);
	    ctrl_text(s, "Nothing on this panel may be reconfigured in mid-"
		      "session; it is only here so that sub-panels of it can "
		      "exist without looking strange.", HELPCTX(no_help));
	}

	if (!midsession) {

	    s = ctrl_getset(b, "连接/SSH", "data",
			    "传送到服务器的数据");
	    ctrl_editbox(s, "远程命令(R)：", 'r', 100,
			 HELPCTX(ssh_command),
			 conf_editbox_handler, I(CONF_remote_cmd), I(1));

	    s = ctrl_getset(b, "连接/SSH", "protocol", "协议选项");
	    ctrl_checkbox(s, "完全不运行 Shell 或命令(N)", 'n',
			  HELPCTX(ssh_noshell),
			  conf_checkbox_handler,
			  I(CONF_ssh_no_shell));
	}

	if (!midsession || !(protcfginfo == 1 || protcfginfo == -1)) {
	    s = ctrl_getset(b, "连接/SSH", "protocol", "协议选项");

	    ctrl_checkbox(s, "开启压缩(E)", 'e',
			  HELPCTX(ssh_compress),
			  conf_checkbox_handler,
			  I(CONF_compression));
	}

	if (!midsession) {
	    s = ctrl_getset(b, "连接/SSH", "sharing", "在 PuTTY 工具之间共享 SSH 连接");

	    ctrl_checkbox(s, "如果可能共享 SSH 连接", 's',
			  HELPCTX(ssh_share),
			  conf_checkbox_handler,
			  I(CONF_ssh_connection_sharing));

            ctrl_text(s, "共享连接中允许角色：",
                      HELPCTX(ssh_share));
	    ctrl_checkbox(s, "上游 (连接到真实服务器)(U)", 'u',
			  HELPCTX(ssh_share),
			  conf_checkbox_handler,
			  I(CONF_ssh_connection_sharing_upstream));
	    ctrl_checkbox(s, "下游 (连接到上游 PuTTY)(D)", 'd',
			  HELPCTX(ssh_share),
			  conf_checkbox_handler,
			  I(CONF_ssh_connection_sharing_downstream));
	}

	if (!midsession) {
	    s = ctrl_getset(b, "连接/SSH", "protocol", "协议选项");

	    ctrl_radiobuttons(s, "首选的 SSH 协议版本：", NO_SHORTCUT, 2,
			      HELPCTX(ssh_protocol),
			      conf_radiobutton_handler,
			      I(CONF_sshprot),
			      "2", '2', I(3),
			      "1 (不安全)", '1', I(0), NULL);
	}

	/*
	 * The Connection/SSH/Kex panel. (Owing to repeat key
	 * exchange, much of this is meaningful in mid-session _if_
	 * we're using SSH-2 and are not a connection-sharing
	 * downstream, or haven't decided yet.)
	 */
	if (protcfginfo != 1 && protcfginfo != -1) {
	    ctrl_settitle(b, "连接/SSH/密钥",
			  "SSH 密钥验证设置");

	    s = ctrl_getset(b, "连接/SSH/密钥", "main",
			    "密钥验证算法选项");
	    c = ctrl_draglist(s, "算法选择顺序(S)：", 's',
			      HELPCTX(ssh_kexlist),
			      kexlist_handler, P(NULL));
	    c->listbox.height = 5;

	    s = ctrl_getset(b, "连接/SSH/密钥", "repeat",
			    "密钥再次验证设置");

	    ctrl_editbox(s, "重新验证最长时间(分钟，0 不限制)(T)：", 't', 20,
			 HELPCTX(ssh_kex_repeat),
			 conf_editbox_handler,
			 I(CONF_ssh_rekey_time),
			 I(-1));
	    ctrl_editbox(s, "重新验证最大数据量(0 为不限制)(X)：", 'x', 20,
			 HELPCTX(ssh_kex_repeat),
			 conf_editbox_handler,
			 I(CONF_ssh_rekey_data),
			 I(16));
	    ctrl_text(s, "(使用 1M 表示 1 兆字节，1G 表示 1 吉字节))",
		      HELPCTX(ssh_kex_repeat));
	}

	/*
	 * The 'Connection/SSH/Host keys' panel.
	 */
	if (protcfginfo != 1 && protcfginfo != -1) {
	    ctrl_settitle(b, "连接/SSH/主机密钥",
			  "控制 SSH 主机密钥选项");

	    s = ctrl_getset(b, "连接/SSH/主机密钥", "main",
			    "主机密钥算法偏好");
	    c = ctrl_draglist(s, "算法选择优先级(S)：", 's',
			      HELPCTX(ssh_hklist),
			      hklist_handler, P(NULL));
	    c->listbox.height = 5;
	}

	/*
	 * Manual host key configuration is irrelevant mid-session,
	 * as we enforce that the host key for rekeys is the
	 * same as that used at the start of the session.
	 */
	if (!midsession) {
	    s = ctrl_getset(b, "连接/SSH/主机密钥", "hostkeys",
			    "手动配置本连接的主机密钥");

            ctrl_columns(s, 2, 75, 25);
            c = ctrl_text(s, "可接受的主机密钥或指纹：",
                          HELPCTX(ssh_kex_manual_hostkeys));
            c->generic.column = 0;
            /* You want to select from the list, _then_ hit Remove. So
             * tab order should be that way round. */
            mh = (struct manual_hostkey_data *)
                ctrl_alloc(b,sizeof(struct manual_hostkey_data));
            mh->rembutton = ctrl_pushbutton(s, "删除(R)", 'r',
                                            HELPCTX(ssh_kex_manual_hostkeys),
                                            manual_hostkey_handler, P(mh));
            mh->rembutton->generic.column = 1;
            mh->rembutton->generic.tabdelay = 1;
            mh->listbox = ctrl_listbox(s, NULL, NO_SHORTCUT,
                                       HELPCTX(ssh_kex_manual_hostkeys),
                                       manual_hostkey_handler, P(mh));
            /* This list box can't be very tall, because there's not
             * much room in the pane on Windows at least. This makes
             * it become really unhelpful if a horizontal scrollbar
             * appears, so we suppress that. */
            mh->listbox->listbox.height = 2;
            mh->listbox->listbox.hscroll = FALSE;
            ctrl_tabdelay(s, mh->rembutton);
	    mh->keybox = ctrl_editbox(s, "密钥(K)", 'k', 75,
                                      HELPCTX(ssh_kex_manual_hostkeys),
                                      manual_hostkey_handler, P(mh), P(NULL));
            mh->keybox->generic.column = 0;
            mh->addbutton = ctrl_pushbutton(s, "增加(Y)", 'y',
                                            HELPCTX(ssh_kex_manual_hostkeys),
                                            manual_hostkey_handler, P(mh));
            mh->addbutton->generic.column = 1;
            ctrl_columns(s, 1, 100);
	}

	if (!midsession || !(protcfginfo == 1 || protcfginfo == -1)) {
	    /*
	     * The Connection/SSH/Cipher panel.
	     */
	    ctrl_settitle(b, "连接/SSH/加密",
			  "控制 SSH 加密选项");

	    s = ctrl_getset(b, "连接/SSH/加密",
                            "encryption", "加密选项");
	    c = ctrl_draglist(s, "加密方法选择顺序(S)：", 's',
			      HELPCTX(ssh_ciphers),
			      cipherlist_handler, P(NULL));
	    c->listbox.height = 6;

	    ctrl_checkbox(s, "允许 SSH-2 兼容使用单一 DES 算法(I)", 'i',
			  HELPCTX(ssh_ciphers),
			  conf_checkbox_handler,
			  I(CONF_ssh2_des_cbc));
	}

	if (!midsession) {

	    /*
	     * The Connection/SSH/Auth panel.
	     */
	    ctrl_settitle(b, "连接/SSH/认证",
			  "SSH 认证设置");

	    s = ctrl_getset(b, "连接/SSH/认证", "main", NULL);
	    ctrl_checkbox(s, "显示预认证提示，只限于 SSH-2(D)",
			  'd', HELPCTX(ssh_auth_banner),
			  conf_checkbox_handler,
			  I(CONF_ssh_show_banner));
	    ctrl_checkbox(s, "完全绕过认证，只限于 SSH-2(B)", 'b',
			  HELPCTX(ssh_auth_bypass),
			  conf_checkbox_handler,
			  I(CONF_ssh_no_userauth));

	    s = ctrl_getset(b, "连接/SSH/认证", "methods",
			    "认证方式");
	    ctrl_checkbox(s, "尝试使用 Pageant 认证", 'p',
			  HELPCTX(ssh_auth_pageant),
			  conf_checkbox_handler,
			  I(CONF_tryagent));
	    ctrl_checkbox(s, "尝试 TIS 或 CryptoCard 认证 (SSH-1) (M)", 'm',
			  HELPCTX(ssh_auth_tis),
			  conf_checkbox_handler,
			  I(CONF_try_tis_auth));
	    ctrl_checkbox(s, "尝试“智能键盘”认证 (SSH-2) (I)",
			  'i', HELPCTX(ssh_auth_ki),
			  conf_checkbox_handler,
			  I(CONF_try_ki_auth));

	    s = ctrl_getset(b, "连接/SSH/认证", "params",
			    "认证参数");
	    ctrl_checkbox(s, "允许代理映射(F)", 'f',
			  HELPCTX(ssh_auth_agentfwd),
			  conf_checkbox_handler, I(CONF_agentfwd));
	    ctrl_checkbox(s, "允许尝试在 SSH-2 中修改用户名", NO_SHORTCUT,
			  HELPCTX(ssh_auth_changeuser),
			  conf_checkbox_handler,
			  I(CONF_change_username));
	    ctrl_filesel(s, "认证私钥文件(K)：", 'k',
			 FILTER_KEY_FILES, FALSE, "选择私钥文件",
			 HELPCTX(ssh_auth_privkey),
			 conf_filesel_handler, I(CONF_keyfile));

#ifndef NO_GSSAPI
	    /*
	     * Connection/SSH/Auth/GSSAPI, which sadly won't fit on
	     * the main Auth panel.
	     */
	    ctrl_settitle(b, "连接/SSH/认证/GSSAPI",
			  "控制 GSSAPI 认证选项");
	    s = ctrl_getset(b, "连接/SSH/认证/GSSAPI", "gssapi", NULL);

	    ctrl_checkbox(s, "尝试使用 GSSAPI 认证，只限于 SSH-2(T)",
			  't', HELPCTX(ssh_gssapi),
			  conf_checkbox_handler,
			  I(CONF_try_gssapi_auth));

	    ctrl_checkbox(s, "允许 GSSAPI 凭据委托(L)", 'l',
			  HELPCTX(ssh_gssapi_delegation),
			  conf_checkbox_handler,
			  I(CONF_gssapifwd));

	    /*
	     * GSSAPI library selection.
	     */
	    if (ngsslibs > 1) {
		c = ctrl_draglist(s, "GSSAPI 库优先级：",
				  'p', HELPCTX(ssh_gssapi_libraries),
				  gsslist_handler, P(NULL));
		c->listbox.height = ngsslibs;

		/*
		 * I currently assume that if more than one GSS
		 * library option is available, then one of them is
		 * 'user-supplied' and so we should present the
		 * following file selector. This is at least half-
		 * reasonable, because if we're using statically
		 * linked GSSAPI then there will only be one option
		 * and no way to load from a user-supplied library,
		 * whereas if we're using dynamic libraries then
		 * there will almost certainly be some default
		 * option in addition to a user-supplied path. If
		 * anyone ever ports PuTTY to a system on which
		 * dynamic-library GSSAPI is available but there is
		 * absolutely no consensus on where to keep the
		 * libraries, there'll need to be a flag alongside
		 * ngsslibs to control whether the file selector is
		 * displayed. 
		 */

		ctrl_filesel(s, "用户支持的 GSSAPI 库路径：", 's',
			     FILTER_DYNLIB_FILES, FALSE, "选择库文件",
			     HELPCTX(ssh_gssapi_libraries),
			     conf_filesel_handler,
			     I(CONF_ssh_gss_custom));
	    }
#endif
	}

	if (!midsession) {
	    /*
	     * The Connection/SSH/TTY panel.
	     */
	    ctrl_settitle(b, "连接/SSH/TTY", "远程终端设置");

	    s = ctrl_getset(b, "连接/SSH/TTY", "sshtty", NULL);
	    ctrl_checkbox(s, "不分配假终端(P)", 'p',
			  HELPCTX(ssh_nopty),
			  conf_checkbox_handler,
			  I(CONF_nopty));

	    s = ctrl_getset(b, "连接/SSH/TTY", "ttymodes",
			    "终端模式");
	    td = (struct ttymodes_data *)
		ctrl_alloc(b, sizeof(struct ttymodes_data));
	    c = ctrl_text(s, "终端模式用于发送：", HELPCTX(ssh_ttymodes));
	    td->listbox = ctrl_listbox(s, NULL, NO_SHORTCUT,
				       HELPCTX(ssh_ttymodes),
				       ttymodes_handler, P(td));
	    td->listbox->listbox.height = 8;
	    td->listbox->listbox.ncols = 2;
	    td->listbox->listbox.percentages = snewn(2, int);
	    td->listbox->listbox.percentages[0] = 40;
	    td->listbox->listbox.percentages[1] = 60;
	    ctrl_columns(s, 2, 75, 25);
	    c = ctrl_text(s, "选择的模式，发送：", HELPCTX(ssh_ttymodes));
	    c->generic.column = 0;
	    td->setbutton = ctrl_pushbutton(s, "设置(S)", 's',
					    HELPCTX(ssh_ttymodes),
					    ttymodes_handler, P(td));
	    td->setbutton->generic.column = 1;
	    td->setbutton->generic.tabdelay = 1;
	    ctrl_columns(s, 1, 100);	    /* column break */
	    /* Bit of a hack to get the value radio buttons and
	     * edit-box on the same row. */
	    ctrl_columns(s, 2, 75, 25);
	    td->valradio = ctrl_radiobuttons(s, NULL, NO_SHORTCUT, 3,
					     HELPCTX(ssh_ttymodes),
					     ttymodes_handler, P(td),
					     "自动", NO_SHORTCUT, P(NULL),
					     "无", NO_SHORTCUT, P(NULL),
					     "指定：", NO_SHORTCUT, P(NULL),
					     NULL);
	    td->valradio->generic.column = 0;
	    td->valbox = ctrl_editbox(s, NULL, NO_SHORTCUT, 100,
				      HELPCTX(ssh_ttymodes),
				      ttymodes_handler, P(td), P(NULL));
	    td->valbox->generic.column = 1;
	    ctrl_tabdelay(s, td->setbutton);
	}

	if (!midsession) {
	    /*
	     * The Connection/SSH/X11 panel.
	     */
	    ctrl_settitle(b, "连接/SSH/X11",
			  "SSH X11 映射设置");

	    s = ctrl_getset(b, "连接/SSH/X11", "x11", "X11 映射");
	    ctrl_checkbox(s, "允许 X11 映射(E)", 'e',
			  HELPCTX(ssh_tunnels_x11),
			  conf_checkbox_handler,I(CONF_x11_forward));
	    ctrl_editbox(s, "X 显示位置：", 'x', 50,
			 HELPCTX(ssh_tunnels_x11),
			 conf_editbox_handler, I(CONF_x11_display), I(1));
	    ctrl_radiobuttons(s, "远程 X11 认证协议(U)", 'u', 1,
			      HELPCTX(ssh_tunnels_x11auth),
			      conf_radiobutton_handler,
			      I(CONF_x11_auth),
			      "MIT-Magic-Cookie-1", I(X11_MIT),
			      "XDM-Authorization-1", I(X11_XDM), NULL);
	}

	/*
	 * The Tunnels panel _is_ still available in mid-session.
	 */
	ctrl_settitle(b, "连接/SSH/通道",
		      "SSH 端口映射设置");

	s = ctrl_getset(b, "连接/SSH/通道", "portfwd",
			"端口映射");
	ctrl_checkbox(s, "本地端口接受其他主机的连接(T)",'t',
		      HELPCTX(ssh_tunnels_portfwd_localhost),
		      conf_checkbox_handler,
		      I(CONF_lport_acceptall));
	ctrl_checkbox(s, "远程端口接受连接(只限 SSH-2)(P)", 'p',
		      HELPCTX(ssh_tunnels_portfwd_localhost),
		      conf_checkbox_handler,
		      I(CONF_rport_acceptall));

	ctrl_columns(s, 3, 55, 20, 25);
	c = ctrl_text(s, "映射的端口：", HELPCTX(ssh_tunnels_portfwd));
	c->generic.column = COLUMN_FIELD(0,2);
	/* You want to select from the list, _then_ hit Remove. So tab order
	 * should be that way round. */
	pfd = (struct portfwd_data *)ctrl_alloc(b,sizeof(struct portfwd_data));
	pfd->rembutton = ctrl_pushbutton(s, "删除(R)", 'r',
					 HELPCTX(ssh_tunnels_portfwd),
					 portfwd_handler, P(pfd));
	pfd->rembutton->generic.column = 2;
	pfd->rembutton->generic.tabdelay = 1;
	pfd->listbox = ctrl_listbox(s, NULL, NO_SHORTCUT,
				    HELPCTX(ssh_tunnels_portfwd),
				    portfwd_handler, P(pfd));
	pfd->listbox->listbox.height = 3;
	pfd->listbox->listbox.ncols = 2;
	pfd->listbox->listbox.percentages = snewn(2, int);
	pfd->listbox->listbox.percentages[0] = 20;
	pfd->listbox->listbox.percentages[1] = 80;
	ctrl_tabdelay(s, pfd->rembutton);
	ctrl_text(s, "增加新的映射端口：", HELPCTX(ssh_tunnels_portfwd));
	/* You want to enter source, destination and type, _then_ hit Add.
	 * Again, we adjust the tab order to reflect this. */
	pfd->addbutton = ctrl_pushbutton(s, "增加(D)", 'd',
					 HELPCTX(ssh_tunnels_portfwd),
					 portfwd_handler, P(pfd));
	pfd->addbutton->generic.column = 2;
	pfd->addbutton->generic.tabdelay = 1;
	pfd->sourcebox = ctrl_editbox(s, "源端口(S)：", 's', 40,
				      HELPCTX(ssh_tunnels_portfwd),
				      portfwd_handler, P(pfd), P(NULL));
	pfd->sourcebox->generic.column = 0;
	pfd->destbox = ctrl_editbox(s, "目的地(I)", 'i', 67,
				    HELPCTX(ssh_tunnels_portfwd),
				    portfwd_handler, P(pfd), P(NULL));
	pfd->direction = ctrl_radiobuttons(s, NULL, NO_SHORTCUT, 3,
					   HELPCTX(ssh_tunnels_portfwd),
					   portfwd_handler, P(pfd),
					   "本地(L)", 'l', P(NULL),
					   "远程(M)", 'm', P(NULL),
					   "动态(Y)", 'y', P(NULL),
					   NULL);
#ifndef NO_IPV6
	pfd->addressfamily =
	    ctrl_radiobuttons(s, NULL, NO_SHORTCUT, 3,
			      HELPCTX(ssh_tunnels_portfwd_ipversion),
			      portfwd_handler, P(pfd),
			      "自动(U)", 'u', I(ADDRTYPE_UNSPEC),
			      "IPv4", '4', I(ADDRTYPE_IPV4),
			      "IPv6", '6', I(ADDRTYPE_IPV6),
			      NULL);
#endif
	ctrl_tabdelay(s, pfd->addbutton);
	ctrl_columns(s, 1, 100);

	if (!midsession) {
	    /*
	     * The Connection/SSH/Bugs panels.
	     */
	    ctrl_settitle(b, "连接/SSH/查错",
			  "处理 SSH 服务器错误设置");

	    s = ctrl_getset(b, "连接/SSH/查错", "main",
			    "检测已知的 SSH 服务器错误");
	    ctrl_droplist(s, "阻塞 SSH-2 忽略信息", '2', 20,
			  HELPCTX(ssh_bugs_ignore2),
			  sshbug_handler, I(CONF_sshbug_ignore2));
	    ctrl_droplist(s, "严格 SSH-2 密钥再次验证操作(K)", 'k', 20,
			  HELPCTX(ssh_bugs_rekey2),
			  sshbug_handler, I(CONF_sshbug_rekey2));
	    ctrl_droplist(s, "阻塞 PuTTY's SSH-2 'winadj' 请求", 'j',
                          20, HELPCTX(ssh_bugs_winadj),
			  sshbug_handler, I(CONF_sshbug_winadj));
	    ctrl_droplist(s, "回复已关闭通道的请求(Q)", 'q', 20,
			  HELPCTX(ssh_bugs_chanreq),
			  sshbug_handler, I(CONF_sshbug_chanreq));
	    ctrl_droplist(s, "忽略 SSH-2 最大包大小(X)", 'x', 20,
			  HELPCTX(ssh_bugs_maxpkt2),
			  sshbug_handler, I(CONF_sshbug_maxpkt2));

	    ctrl_settitle(b, "连接/SSH/更多查错",
			  "处理 SSH 服务器更多错误设置");

	    s = ctrl_getset(b, "连接/SSH/更多查错", "main",
			    "检测已知的 SSH 服务器错误");
	    ctrl_droplist(s, "SSH-2 RSA 签名附加请求(P)", 'p', 20,
			  HELPCTX(ssh_bugs_rsapad2),
			  sshbug_handler, I(CONF_sshbug_rsapad2));
	    ctrl_droplist(s, "只支持 pre-RFC4419 SSH-2 DH GEX", 'd', 20,
			  HELPCTX(ssh_bugs_oldgex2),
			  sshbug_handler, I(CONF_sshbug_oldgex2));
	    ctrl_droplist(s, "混算 SSH-2 HMAC 密钥", 'm', 20,
			  HELPCTX(ssh_bugs_hmac2),
			  sshbug_handler, I(CONF_sshbug_hmac2));
	    ctrl_droplist(s, "错误 SSH-2 PK 认证会话 ID(N)", 'n', 20,
			  HELPCTX(ssh_bugs_pksessid2),
			  sshbug_handler, I(CONF_sshbug_pksessid2));
	    ctrl_droplist(s, "混算 SSH-2 加密密钥(E)", 'e', 20,
			  HELPCTX(ssh_bugs_derivekey2),
			  sshbug_handler, I(CONF_sshbug_derivekey2));
	    ctrl_droplist(s, "阻塞 SSH-1 忽略信息(I)", 'i', 20,
			  HELPCTX(ssh_bugs_ignore1),
			  sshbug_handler, I(CONF_sshbug_ignore1));
	    ctrl_droplist(s, "拒绝所有 SSH-1 密码伪装", 's', 20,
			  HELPCTX(ssh_bugs_plainpw1),
			  sshbug_handler, I(CONF_sshbug_plainpw1));
	    ctrl_droplist(s, "阻塞 SSH-1 RSA 认证", 'r', 20,
			  HELPCTX(ssh_bugs_rsa1),
			  sshbug_handler, I(CONF_sshbug_rsa1));
	}
    }
}
