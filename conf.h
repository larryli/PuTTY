/*
 * Master list of configuration options living in the Conf data
 * structure.
 *
 * Each CONF_OPTION directive defines a single CONF_foo primary key in
 * Conf, and can be equipped with the following properties:
 *
 *  - VALUE_TYPE: the type of data associated with that key
 *  - SUBKEY_TYPE: if the primary key goes with a subkey (that is, the
 *    primary key identifies some mapping from subkeys to values), the
 *    data type of the subkey
 */

CONF_OPTION(host,
    VALUE_TYPE(STR),
)
CONF_OPTION(port,
    VALUE_TYPE(INT),
)
CONF_OPTION(protocol,
    VALUE_TYPE(INT), /* PROT_SSH, PROT_TELNET etc */
)
CONF_OPTION(addressfamily,
    VALUE_TYPE(INT),
)
CONF_OPTION(close_on_exit,
    VALUE_TYPE(INT),
)
CONF_OPTION(warn_on_close,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(ping_interval,
    VALUE_TYPE(INT), /* in seconds */
)
CONF_OPTION(tcp_nodelay,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(tcp_keepalives,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(loghost, /* logical host being contacted, for host key check */
    VALUE_TYPE(STR),
)

/* Proxy options */
CONF_OPTION(proxy_exclude_list,
    VALUE_TYPE(STR),
)
CONF_OPTION(proxy_dns,
    VALUE_TYPE(INT),
)
CONF_OPTION(even_proxy_localhost,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(proxy_type,
    VALUE_TYPE(INT), /* PROXY_NONE, PROXY_SOCKS4, ... */
)
CONF_OPTION(proxy_host,
    VALUE_TYPE(STR),
)
CONF_OPTION(proxy_port,
    VALUE_TYPE(INT),
)
CONF_OPTION(proxy_username,
    VALUE_TYPE(STR),
)
CONF_OPTION(proxy_password,
    VALUE_TYPE(STR),
)
CONF_OPTION(proxy_telnet_command,
    VALUE_TYPE(STR),
)
CONF_OPTION(proxy_log_to_term,
    VALUE_TYPE(INT),
)

/* SSH options */
CONF_OPTION(remote_cmd,
    VALUE_TYPE(STR),
)
CONF_OPTION(remote_cmd2,
    /*
     * Fallback command to try to run if remote_cmd fails.
     */
    VALUE_TYPE(STR),
)
CONF_OPTION(nopty,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(compression,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(ssh_kexlist,
    SUBKEY_TYPE(INT), /* indices in preference order: 0,...,KEX_MAX-1
                       * (lower is more preferred) */
    VALUE_TYPE(INT),  /* KEX_* enum values */
)
CONF_OPTION(ssh_hklist,
    SUBKEY_TYPE(INT), /* indices in preference order: 0,...,HK_MAX-1
                       * (lower is more preferred) */
    VALUE_TYPE(INT),  /* HK_* enum values */
)
CONF_OPTION(ssh_prefer_known_hostkeys,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(ssh_rekey_time,
    VALUE_TYPE(INT), /* in minutes */
)
CONF_OPTION(ssh_rekey_data,
    VALUE_TYPE(STR), /* string encoding e.g. "100K", "2M", "1G" */
)
CONF_OPTION(tryagent,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(agentfwd,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(change_username, /* allow username switching in SSH-2 */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(ssh_cipherlist,
    SUBKEY_TYPE(INT), /* indices in preference order: 0,...,CIPHER_MAX-1
                       * (lower is more preferred) */
    VALUE_TYPE(INT),  /* CIPHER_* enum values */
)
CONF_OPTION(keyfile,
    VALUE_TYPE(FILENAME),
)
CONF_OPTION(detached_cert,
    VALUE_TYPE(FILENAME),
)
CONF_OPTION(auth_plugin,
    VALUE_TYPE(STR),
)
CONF_OPTION(sshprot,
    /*
     * Which SSH protocol to use.
     *
     * For historical reasons, the current legal values for CONF_sshprot
     * are:
     *  0 = SSH-1 only
     *  3 = SSH-2 only
     *
     * We used to also support
     *  1 = SSH-1 with fallback to SSH-2
     *  2 = SSH-2 with fallback to SSH-1
     *
     * and we continue to use 0/3 in storage formats rather than the more
     * obvious 1/2 to avoid surprises if someone saves a session and later
     * downgrades PuTTY. So it's easier to use these numbers internally too.
     */
    VALUE_TYPE(INT),
)
CONF_OPTION(ssh_simple,
    /*
     * This means that we promise never to open any channel other
     * than the main one, which means it can safely use a very large
     * window in SSH-2.
     */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(ssh_connection_sharing,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(ssh_connection_sharing_upstream,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(ssh_connection_sharing_downstream,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(ssh_manual_hostkeys,
    /*
     * Manually configured host keys to accept regardless of the state
     * of the host key cache.
     *
     * This is conceptually a set rather than a dictionary: every
     * value in this map is the empty string, and the set of subkeys
     * that exist is the important data.
     */
    SUBKEY_TYPE(STR),
    VALUE_TYPE(STR),
)
CONF_OPTION(ssh2_des_cbc, /* "des-cbc" unrecommended SSH-2 cipher */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(ssh_no_userauth, /* bypass "ssh-userauth" (SSH-2 only) */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(ssh_no_trivial_userauth, /* disable trivial types of auth */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(ssh_show_banner, /* show USERAUTH_BANNERs (SSH-2 only) */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(try_tis_auth,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(try_ki_auth,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(try_gssapi_auth, /* attempt gssapi via ssh userauth */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(try_gssapi_kex, /* attempt gssapi via ssh kex */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(gssapifwd, /* forward tgt via gss */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(gssapirekey, /* KEXGSS refresh interval (mins) */
    VALUE_TYPE(INT),
)
CONF_OPTION(ssh_gsslist,
    SUBKEY_TYPE(INT), /* indices in preference order: 0,...,ngsslibs
                       * (lower is more preferred; ngsslibs is a platform-
                       * dependent value) */
    VALUE_TYPE(INT),  /* indices of GSSAPI lib types (platform-dependent) */
)
CONF_OPTION(ssh_gss_custom,
    VALUE_TYPE(FILENAME),
)
CONF_OPTION(ssh_subsys, /* run a subsystem rather than a command */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(ssh_subsys2, /* fallback to go with remote_cmd_ptr2 */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(ssh_no_shell, /* avoid running a shell */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(ssh_nc_host, /* host to connect to in `nc' mode */
    VALUE_TYPE(STR),
)
CONF_OPTION(ssh_nc_port, /* port to connect to in `nc' mode */
    VALUE_TYPE(INT),
)

/* Telnet options */
CONF_OPTION(termtype,
    VALUE_TYPE(STR),
)
CONF_OPTION(termspeed,
    VALUE_TYPE(STR),
)
CONF_OPTION(ttymodes,
    SUBKEY_TYPE(STR), /* subkeys are listed in ttymodes[] in settings.c */
    VALUE_TYPE(STR),  /* values are "Vvalue" or "A" */
)
CONF_OPTION(environmt,
    SUBKEY_TYPE(STR), /* environment variable name */
    VALUE_TYPE(STR),  /* environment variable value */
)
CONF_OPTION(username,
    VALUE_TYPE(STR),
)
CONF_OPTION(username_from_env,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(localusername,
    VALUE_TYPE(STR),
)
CONF_OPTION(rfc_environ,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(passive_telnet,
    VALUE_TYPE(BOOL),
)

/* Serial port options */
CONF_OPTION(serline,
    VALUE_TYPE(STR),
)
CONF_OPTION(serspeed,
    VALUE_TYPE(INT),
)
CONF_OPTION(serdatabits,
    VALUE_TYPE(INT),
)
CONF_OPTION(serstopbits,
    VALUE_TYPE(INT),
)
CONF_OPTION(serparity,
    VALUE_TYPE(INT),
)
CONF_OPTION(serflow,
    VALUE_TYPE(INT),
)

/* SUPDUP options */
CONF_OPTION(supdup_location,
    VALUE_TYPE(STR),
)
CONF_OPTION(supdup_ascii_set,
    VALUE_TYPE(INT),
)
CONF_OPTION(supdup_more,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(supdup_scroll,
    VALUE_TYPE(BOOL),
)

/* Keyboard options */
CONF_OPTION(bksp_is_delete,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(rxvt_homeend,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(funky_type,
    VALUE_TYPE(INT),
)
CONF_OPTION(sharrow_type,
    VALUE_TYPE(INT),
)
CONF_OPTION(no_applic_c, /* totally disable app cursor keys */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(no_applic_k, /* totally disable app keypad */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(no_mouse_rep, /* totally disable mouse reporting */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(no_remote_resize, /* disable remote resizing */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(no_alt_screen, /* disable alternate screen */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(no_remote_wintitle, /* disable remote retitling */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(no_remote_clearscroll, /* disable ESC[3J */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(no_dbackspace, /* disable destructive backspace */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(no_remote_charset, /* disable remote charset config */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(remote_qtitle_action, /* handling of remote window title queries */
    VALUE_TYPE(INT),
)
CONF_OPTION(app_cursor,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(app_keypad,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(nethack_keypad,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(telnet_keyboard,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(telnet_newline,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(alt_f4, /* is it special? */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(alt_space, /* is it special? */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(alt_only, /* is it special? */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(localecho,
    VALUE_TYPE(INT),
)
CONF_OPTION(localedit,
    VALUE_TYPE(INT),
)
CONF_OPTION(alwaysontop,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(fullscreenonaltenter,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(scroll_on_key,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(scroll_on_disp,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(erase_to_scrollback,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(compose_key,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(ctrlaltkeys,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(osx_option_meta,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(osx_command_meta,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(wintitle, /* initial window title */
    VALUE_TYPE(STR),
)
/* Terminal options */
CONF_OPTION(savelines,
    VALUE_TYPE(INT),
)
CONF_OPTION(dec_om,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(wrap_mode,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(lfhascr,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(cursor_type,
    VALUE_TYPE(INT),
)
CONF_OPTION(blink_cur,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(beep,
    VALUE_TYPE(INT),
)
CONF_OPTION(beep_ind,
    VALUE_TYPE(INT),
)
CONF_OPTION(bellovl, /* bell overload protection active? */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(bellovl_n, /* number of bells to cause overload */
    VALUE_TYPE(INT),
)
CONF_OPTION(bellovl_t, /* time interval for overload (ticks) */
    VALUE_TYPE(INT),
)
CONF_OPTION(bellovl_s, /* period of silence to re-enable bell (s) */
    VALUE_TYPE(INT),
)
CONF_OPTION(bell_wavefile,
    VALUE_TYPE(FILENAME),
)
CONF_OPTION(scrollbar,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(scrollbar_in_fullscreen,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(resize_action,
    VALUE_TYPE(INT),
)
CONF_OPTION(bce,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(blinktext,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(win_name_always,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(width,
    VALUE_TYPE(INT),
)
CONF_OPTION(height,
    VALUE_TYPE(INT),
)
CONF_OPTION(font,
    VALUE_TYPE(FONT),
)
CONF_OPTION(font_quality,
    VALUE_TYPE(INT),
)
CONF_OPTION(logfilename,
    VALUE_TYPE(FILENAME),
)
CONF_OPTION(logtype,
    VALUE_TYPE(INT),
)
CONF_OPTION(logxfovr,
    VALUE_TYPE(INT),
)
CONF_OPTION(logflush,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(logheader,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(logomitpass,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(logomitdata,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(hide_mouseptr,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(sunken_edge,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(window_border,
    VALUE_TYPE(INT), /* in pixels */
)
CONF_OPTION(answerback,
    VALUE_TYPE(STR),
)
CONF_OPTION(printer,
    VALUE_TYPE(STR),
)
CONF_OPTION(no_arabicshaping,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(no_bidi,
    VALUE_TYPE(BOOL),
)

/* Colour options */
CONF_OPTION(ansi_colour,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(xterm_256_colour,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(true_colour,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(system_colour,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(try_palette,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(bold_style,
    VALUE_TYPE(INT),
)
CONF_OPTION(colours,
    SUBKEY_TYPE(INT), /* indexed by CONF_COLOUR_* enum encoding */
    VALUE_TYPE(INT),
)

/* Selection options */
CONF_OPTION(mouse_is_xterm,
    VALUE_TYPE(INT),
)
CONF_OPTION(rect_select,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(paste_controls,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(rawcnp,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(utf8linedraw,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(rtf_paste,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(mouse_override,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(wordness,
    SUBKEY_TYPE(INT), /* ASCII character codes (literally, just 00-7F) */
    VALUE_TYPE(INT),  /* arbitrary equivalence-class value for that char */
)
CONF_OPTION(mouseautocopy,
    /*
     * What clipboard (if any) to copy text to as soon as it's
     * selected with the mouse.
     */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(mousepaste, /* clipboard used by one-mouse-click paste actions */
    VALUE_TYPE(INT),
)
CONF_OPTION(ctrlshiftins, /* clipboard used by Ctrl+Ins and Shift+Ins */
    VALUE_TYPE(INT),
)
CONF_OPTION(ctrlshiftcv, /* clipboard used by Ctrl+Shift+C and Ctrl+Shift+V */
    VALUE_TYPE(INT),
)
CONF_OPTION(mousepaste_custom,
    /* Custom clipboard name if CONF_mousepaste is set to CLIPUI_CUSTOM */
    VALUE_TYPE(STR),
)
CONF_OPTION(ctrlshiftins_custom,
    /* Custom clipboard name if CONF_ctrlshiftins is set to CLIPUI_CUSTOM */
    VALUE_TYPE(STR),
)
CONF_OPTION(ctrlshiftcv_custom,
    /* Custom clipboard name if CONF_ctrlshiftcv is set to CLIPUI_CUSTOM */
    VALUE_TYPE(STR),
)

/* Character-set translation */
CONF_OPTION(vtmode,
    VALUE_TYPE(INT),
)
CONF_OPTION(line_codepage,
    VALUE_TYPE(STR),
)
CONF_OPTION(cjk_ambig_wide,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(utf8_override,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(xlat_capslockcyr,
    VALUE_TYPE(BOOL),
)

/* X11 forwarding */
CONF_OPTION(x11_forward,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(x11_display,
    VALUE_TYPE(STR),
)
CONF_OPTION(x11_auth,
    VALUE_TYPE(INT),
)
CONF_OPTION(xauthfile,
    VALUE_TYPE(FILENAME),
)

/* Port forwarding */
CONF_OPTION(lport_acceptall, /* accept conns from hosts other than localhost */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(rport_acceptall, /* same for remote forwarded ports */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(portfwd,
    /*
     * Subkeys for 'portfwd' can have the following forms:
     *
     *   [LR]localport
     *   [LR]localaddr:localport
     *
     * Dynamic forwardings are indicated by an 'L' key, and the
     * special value "D". For all other forwardings, the value should
     * be of the form 'host:port'.
     */
    SUBKEY_TYPE(STR),
    VALUE_TYPE(STR),
)

/* SSH bug compatibility modes. All FORCE_ON/FORCE_OFF/AUTO */
CONF_OPTION(sshbug_ignore1,
    VALUE_TYPE(INT),
)
CONF_OPTION(sshbug_plainpw1,
    VALUE_TYPE(INT),
)
CONF_OPTION(sshbug_rsa1,
    VALUE_TYPE(INT),
)
CONF_OPTION(sshbug_ignore2,
    VALUE_TYPE(INT),
)
CONF_OPTION(sshbug_derivekey2,
    VALUE_TYPE(INT),
)
CONF_OPTION(sshbug_rsapad2,
    VALUE_TYPE(INT),
)
CONF_OPTION(sshbug_pksessid2,
    VALUE_TYPE(INT),
)
CONF_OPTION(sshbug_rekey2,
    VALUE_TYPE(INT),
)
CONF_OPTION(sshbug_maxpkt2,
    VALUE_TYPE(INT),
)
CONF_OPTION(sshbug_oldgex2,
    VALUE_TYPE(INT),
)
CONF_OPTION(sshbug_winadj,
    VALUE_TYPE(INT),
)
CONF_OPTION(sshbug_chanreq,
    VALUE_TYPE(INT),
)
CONF_OPTION(sshbug_dropstart,
    VALUE_TYPE(INT),
)
CONF_OPTION(sshbug_filter_kexinit,
    VALUE_TYPE(INT),
)
CONF_OPTION(sshbug_rsa_sha2_cert_userauth,
    VALUE_TYPE(INT),
)
CONF_OPTION(sshbug_hmac2,
    VALUE_TYPE(INT),
)

/* Options for Unix. Should split out into platform-dependent part. */
CONF_OPTION(stamp_utmp, /* used by Unix pterm */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(login_shell, /* used by Unix pterm */
    VALUE_TYPE(BOOL),
)
CONF_OPTION(scrollbar_on_left,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(shadowbold,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(boldfont,
    VALUE_TYPE(FONT),
)
CONF_OPTION(widefont,
    VALUE_TYPE(FONT),
)
CONF_OPTION(wideboldfont,
    VALUE_TYPE(FONT),
)
CONF_OPTION(shadowboldoffset,
    VALUE_TYPE(INT), /* in pixels */
)
CONF_OPTION(crhaslf,
    VALUE_TYPE(BOOL),
)
CONF_OPTION(winclass,
    VALUE_TYPE(STR),
)
