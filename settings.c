/*
 * settings.c: read and write saved sessions.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "putty.h"
#include "storage.h"

static void gpps(void *handle, char *name, char *def, char *val, int len) {
    if (!read_setting_s(handle, name, val, len)) {
	strncpy(val, def, len);
	val[len-1] = '\0';
    }
}

static void gppi(void *handle, char *name, int def, int *i) {
    *i = read_setting_i(handle, name, def);
}

void save_settings (char *section, int do_host, Config *cfg) {
    int i;
    char *p;
    void *sesskey;

    sesskey = open_settings_w(section);
    if (!sesskey)
        return;

    write_setting_i (sesskey, "Present", 1);
    if (do_host) {
	write_setting_s (sesskey, "HostName", cfg->host);
	write_setting_i (sesskey, "PortNumber", cfg->port);
	write_setting_s (sesskey, "LogFileName", cfg->logfilename);
	write_setting_i (sesskey, "LogType", cfg->logtype);
        p = "raw";
        for (i = 0; backends[i].name != NULL; i++)
            if (backends[i].protocol == cfg->protocol) {
                p = backends[i].name;
                break;
            }
        write_setting_s (sesskey, "Protocol", p);
    }
    write_setting_i (sesskey, "CloseOnExit", !!cfg->close_on_exit);
    write_setting_i (sesskey, "WarnOnClose", !!cfg->warn_on_close);
    write_setting_i (sesskey, "PingInterval", cfg->ping_interval);
    write_setting_s (sesskey, "TerminalType", cfg->termtype);
    write_setting_s (sesskey, "TerminalSpeed", cfg->termspeed);
    {
      char buf[2*sizeof(cfg->environmt)], *p, *q;
	p = buf;
      q = cfg->environmt;
	while (*q) {
	    while (*q) {
		int c = *q++;
		if (c == '=' || c == ',' || c == '\\')
		    *p++ = '\\';
		if (c == '\t')
		    c = '=';
		*p++ = c;
	    }
	    *p++ = ',';
	    q++;
	}
	*p = '\0';
	write_setting_s (sesskey, "Environment", buf);
    }
    write_setting_s (sesskey, "UserName", cfg->username);
    write_setting_i (sesskey, "NoPTY", cfg->nopty);
    write_setting_i (sesskey, "Compression", cfg->compression);
    write_setting_i (sesskey, "AgentFwd", cfg->agentfwd);
    write_setting_s (sesskey, "RemoteCmd", cfg->remote_cmd);
    write_setting_s (sesskey, "Cipher", cfg->cipher == CIPHER_BLOWFISH ? "blowfish" :
                             cfg->cipher == CIPHER_DES ? "des" : "3des");
    write_setting_i (sesskey, "AuthTIS", cfg->try_tis_auth);
    write_setting_i (sesskey, "SshProt", cfg->sshprot);
    write_setting_i (sesskey, "BuggyMAC", cfg->buggymac);
    write_setting_s (sesskey, "PublicKeyFile", cfg->keyfile);
    write_setting_s (sesskey, "RemoteCommand", cfg->remote_cmd);
    write_setting_i (sesskey, "RFCEnviron", cfg->rfc_environ);
    write_setting_i (sesskey, "BackspaceIsDelete", cfg->bksp_is_delete);
    write_setting_i (sesskey, "RXVTHomeEnd", cfg->rxvt_homeend);
    write_setting_i (sesskey, "LinuxFunctionKeys", cfg->funky_type);
    write_setting_i (sesskey, "NoApplicationKeys", cfg->no_applic_k);
    write_setting_i (sesskey, "NoApplicationCursors", cfg->no_applic_c);
    write_setting_i (sesskey, "ApplicationCursorKeys", cfg->app_cursor);
    write_setting_i (sesskey, "ApplicationKeypad", cfg->app_keypad);
    write_setting_i (sesskey, "NetHackKeypad", cfg->nethack_keypad);
    write_setting_i (sesskey, "AltF4", cfg->alt_f4);
    write_setting_i (sesskey, "AltSpace", cfg->alt_space);
    write_setting_i (sesskey, "AltOnly", cfg->alt_only);
    write_setting_i (sesskey, "ComposeKey", cfg->compose_key);
    write_setting_i (sesskey, "LdiscTerm", cfg->ldisc_term);
    write_setting_i (sesskey, "AlwaysOnTop", cfg->alwaysontop);
    write_setting_i (sesskey, "CurType", cfg->cursor_type);
    write_setting_i (sesskey, "BlinkCur", cfg->blink_cur);
    write_setting_i (sesskey, "Beep", cfg->beep);
    write_setting_i (sesskey, "ScrollbackLines", cfg->savelines);
    write_setting_i (sesskey, "DECOriginMode", cfg->dec_om);
    write_setting_i (sesskey, "AutoWrapMode", cfg->wrap_mode);
    write_setting_i (sesskey, "LFImpliesCR", cfg->lfhascr);
    write_setting_i (sesskey, "WinNameAlways", cfg->win_name_always);
    write_setting_s (sesskey, "WinTitle", cfg->wintitle);
    write_setting_i (sesskey, "TermWidth", cfg->width);
    write_setting_i (sesskey, "TermHeight", cfg->height);
    write_setting_s (sesskey, "Font", cfg->font);
    write_setting_i (sesskey, "FontIsBold", cfg->fontisbold);
    write_setting_i (sesskey, "FontCharSet", cfg->fontcharset);
    write_setting_i (sesskey, "FontHeight", cfg->fontheight);
    write_setting_i (sesskey, "FontVTMode", cfg->vtmode);
    write_setting_i (sesskey, "TryPalette", cfg->try_palette);
    write_setting_i (sesskey, "BoldAsColour", cfg->bold_colour);
    for (i=0; i<22; i++) {
	char buf[20], buf2[30];
	sprintf(buf, "Colour%d", i);
	sprintf(buf2, "%d,%d,%d", cfg->colours[i][0],
		cfg->colours[i][1], cfg->colours[i][2]);
	write_setting_s (sesskey, buf, buf2);
    }
    write_setting_i (sesskey, "MouseIsXterm", cfg->mouse_is_xterm);
    for (i=0; i<256; i+=32) {
	char buf[20], buf2[256];
	int j;
	sprintf(buf, "Wordness%d", i);
	*buf2 = '\0';
	for (j=i; j<i+32; j++) {
	    sprintf(buf2+strlen(buf2), "%s%d",
		    (*buf2 ? "," : ""), cfg->wordness[j]);
	}
	write_setting_s (sesskey, buf, buf2);
    }
    write_setting_i (sesskey, "KoiWinXlat", cfg->xlat_enablekoiwin);
    write_setting_i (sesskey, "88592Xlat", cfg->xlat_88592w1250);
    write_setting_i (sesskey, "88592-CP852", cfg->xlat_88592cp852);
    write_setting_i (sesskey, "CapsLockCyr", cfg->xlat_capslockcyr);
    write_setting_i (sesskey, "ScrollBar", cfg->scrollbar);
    write_setting_i (sesskey, "ScrollOnKey", cfg->scroll_on_key);
    write_setting_i (sesskey, "ScrollOnDisp", cfg->scroll_on_disp);
    write_setting_i (sesskey, "LockSize", cfg->locksize);
    write_setting_i (sesskey, "BCE", cfg->bce);
    write_setting_i (sesskey, "BlinkText", cfg->blinktext);

    close_settings_w(sesskey);
}

void load_settings (char *section, int do_host, Config *cfg) {
    int i;
    char prot[10];
    void *sesskey;

    sesskey = open_settings_r(section);

    gpps (sesskey, "HostName", "", cfg->host, sizeof(cfg->host));
    gppi (sesskey, "PortNumber", default_port, &cfg->port);
    gpps (sesskey, "LogFileName", "putty.log",
	  cfg->logfilename, sizeof(cfg->logfilename));
    gppi (sesskey, "LogType", 0, &cfg->logtype);

    gpps (sesskey, "Protocol", "default", prot, 10);
    cfg->protocol = default_protocol;
    for (i = 0; backends[i].name != NULL; i++)
        if (!strcmp(prot, backends[i].name)) {
            cfg->protocol = backends[i].protocol;
            break;
        }

    gppi (sesskey, "CloseOnExit", 1, &cfg->close_on_exit);
    gppi (sesskey, "WarnOnClose", 1, &cfg->warn_on_close);
    gppi (sesskey, "PingInterval", 0, &cfg->ping_interval);
    gpps (sesskey, "TerminalType", "xterm", cfg->termtype,
	  sizeof(cfg->termtype));
    gpps (sesskey, "TerminalSpeed", "38400,38400", cfg->termspeed,
	  sizeof(cfg->termspeed));
    {
      char buf[2*sizeof(cfg->environmt)], *p, *q;
	gpps (sesskey, "Environment", "", buf, sizeof(buf));
	p = buf;
	q = cfg->environmt;
	while (*p) {
	    while (*p && *p != ',') {
		int c = *p++;
		if (c == '=')
		    c = '\t';
		if (c == '\\')
		    c = *p++;
		*q++ = c;
	    }
	    if (*p == ',') p++;
	    *q++ = '\0';
	}
	*q = '\0';
    }
    gpps (sesskey, "UserName", "", cfg->username, sizeof(cfg->username));
    gppi (sesskey, "NoPTY", 0, &cfg->nopty);
    gppi (sesskey, "Compression", 0, &cfg->compression);
    gppi (sesskey, "AgentFwd", 0, &cfg->agentfwd);
    gpps (sesskey, "RemoteCmd", "", cfg->remote_cmd, sizeof(cfg->remote_cmd));
    {
	char cipher[10];
	gpps (sesskey, "Cipher", "3des", cipher, 10);
	if (!strcmp(cipher, "blowfish"))
	    cfg->cipher = CIPHER_BLOWFISH;
	else if (!strcmp(cipher, "des"))
	    cfg->cipher = CIPHER_DES;
	else
	    cfg->cipher = CIPHER_3DES;
    }
    gppi (sesskey, "SshProt", 1, &cfg->sshprot);
    gppi (sesskey, "BuggyMAC", 0, &cfg->buggymac);
    gppi (sesskey, "AuthTIS", 0, &cfg->try_tis_auth);
    gpps (sesskey, "PublicKeyFile", "", cfg->keyfile, sizeof(cfg->keyfile));
    gpps (sesskey, "RemoteCommand", "", cfg->remote_cmd,
          sizeof(cfg->remote_cmd));
    gppi (sesskey, "RFCEnviron", 0, &cfg->rfc_environ);
    gppi (sesskey, "BackspaceIsDelete", 1, &cfg->bksp_is_delete);
    gppi (sesskey, "RXVTHomeEnd", 0, &cfg->rxvt_homeend);
    gppi (sesskey, "LinuxFunctionKeys", 0, &cfg->funky_type);
    gppi (sesskey, "NoApplicationKeys", 0, &cfg->no_applic_k);
    gppi (sesskey, "NoApplicationCursors", 0, &cfg->no_applic_c);
    gppi (sesskey, "ApplicationCursorKeys", 0, &cfg->app_cursor);
    gppi (sesskey, "ApplicationKeypad", 0, &cfg->app_keypad);
    gppi (sesskey, "NetHackKeypad", 0, &cfg->nethack_keypad);
    gppi (sesskey, "AltF4", 1, &cfg->alt_f4);
    gppi (sesskey, "AltSpace", 0, &cfg->alt_space);
    gppi (sesskey, "AltOnly", 0, &cfg->alt_only);
    gppi (sesskey, "ComposeKey", 0, &cfg->compose_key);
    gppi (sesskey, "LdiscTerm", 0, &cfg->ldisc_term);
    gppi (sesskey, "AlwaysOnTop", 0, &cfg->alwaysontop);
    gppi (sesskey, "CurType", 0, &cfg->cursor_type);
    gppi (sesskey, "BlinkCur", 0, &cfg->blink_cur);
    gppi (sesskey, "Beep", 1, &cfg->beep);
    gppi (sesskey, "ScrollbackLines", 200, &cfg->savelines);
    gppi (sesskey, "DECOriginMode", 0, &cfg->dec_om);
    gppi (sesskey, "AutoWrapMode", 1, &cfg->wrap_mode);
    gppi (sesskey, "LFImpliesCR", 0, &cfg->lfhascr);
    gppi (sesskey, "WinNameAlways", 0, &cfg->win_name_always);
    gpps (sesskey, "WinTitle", "", cfg->wintitle, sizeof(cfg->wintitle));
    gppi (sesskey, "TermWidth", 80, &cfg->width);
    gppi (sesskey, "TermHeight", 24, &cfg->height);
    gpps (sesskey, "Font", "Courier", cfg->font, sizeof(cfg->font));
    gppi (sesskey, "FontIsBold", 0, &cfg->fontisbold);
    gppi (sesskey, "FontCharSet", ANSI_CHARSET, &cfg->fontcharset);
    gppi (sesskey, "FontHeight", 10, &cfg->fontheight);
    gppi (sesskey, "FontVTMode", VT_OEMANSI, (int *)&cfg->vtmode);
    gppi (sesskey, "TryPalette", 0, &cfg->try_palette);
    gppi (sesskey, "BoldAsColour", 1, &cfg->bold_colour);
    for (i=0; i<22; i++) {
	static char *defaults[] = {
	    "187,187,187", "255,255,255", "0,0,0", "85,85,85", "0,0,0",
	    "0,255,0", "0,0,0", "85,85,85", "187,0,0", "255,85,85",
	    "0,187,0", "85,255,85", "187,187,0", "255,255,85", "0,0,187",
	    "85,85,255", "187,0,187", "255,85,255", "0,187,187",
	    "85,255,255", "187,187,187", "255,255,255"
 	};
	char buf[20], buf2[30];
	int c0, c1, c2;
	sprintf(buf, "Colour%d", i);
	gpps (sesskey, buf, defaults[i], buf2, sizeof(buf2));
	if(sscanf(buf2, "%d,%d,%d", &c0, &c1, &c2) == 3) {
	    cfg->colours[i][0] = c0;
	    cfg->colours[i][1] = c1;
	    cfg->colours[i][2] = c2;
	}
    }
    gppi (sesskey, "MouseIsXterm", 0, &cfg->mouse_is_xterm);
    for (i=0; i<256; i+=32) {
	static char *defaults[] = {
	    "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0",
	    "0,1,2,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,1",
	    "1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,2",
	    "1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1",
	    "1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1",
	    "1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1",
	    "2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2",
	    "2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2"
	};
	char buf[20], buf2[256], *p;
	int j;
	sprintf(buf, "Wordness%d", i);
	gpps (sesskey, buf, defaults[i/32], buf2, sizeof(buf2));
	p = buf2;
	for (j=i; j<i+32; j++) {
	    char *q = p;
	    while (*p && *p != ',') p++;
	    if (*p == ',') *p++ = '\0';
	    cfg->wordness[j] = atoi(q);
	}
    }
    gppi (sesskey, "KoiWinXlat", 0, &cfg->xlat_enablekoiwin);
    gppi (sesskey, "88592Xlat", 0, &cfg->xlat_88592w1250);
    gppi (sesskey, "88592-CP852", 0, &cfg->xlat_88592cp852);
    gppi (sesskey, "CapsLockCyr", 0, &cfg->xlat_capslockcyr);
    gppi (sesskey, "ScrollBar", 1, &cfg->scrollbar);
    gppi (sesskey, "ScrollOnKey", 0, &cfg->scroll_on_key);
    gppi (sesskey, "ScrollOnDisp", 1, &cfg->scroll_on_disp);
    gppi (sesskey, "LockSize", 0, &cfg->locksize);
    gppi (sesskey, "BCE", 0, &cfg->bce);
    gppi (sesskey, "BlinkText", 0, &cfg->blinktext);

    close_settings_r(sesskey);
}

void do_defaults (char *session, Config *cfg) {
    if (session)
	load_settings (session, TRUE, cfg);
    else
	load_settings ("Default Settings", FALSE, cfg);
}

static int sessioncmp(const void *av, const void *bv) {
    const char *a = *(const char *const *)av;
    const char *b = *(const char *const *)bv;

    /*
     * Alphabetical order, except that "Default Settings" is a
     * special case and comes first.
     */
    if (!strcmp(a, "Default Settings"))
        return -1;                     /* a comes first */
    if (!strcmp(b, "Default Settings"))
        return +1;                     /* b comes first */
    return strcmp(a, b);               /* otherwise, compare normally */
}

void get_sesslist(int allocate) {
    static char otherbuf[2048];
    static char *buffer;
    int buflen, bufsize, i;
    char *p, *ret;
    void *handle;

    if (allocate) {
        
	if ((handle = enum_settings_start()) == NULL)
	    return;

	buflen = bufsize = 0;
	buffer = NULL;
	do {
            ret = enum_settings_next(handle, otherbuf, sizeof(otherbuf));
	    if (ret) {
                int len = strlen(otherbuf)+1;
                if (bufsize < buflen+len) {
                    bufsize = buflen + len + 2048;
                    buffer = srealloc(buffer, bufsize);
                }
		strcpy(buffer+buflen, otherbuf);
		buflen += strlen(buffer+buflen)+1;
	    }
	} while (ret);
        enum_settings_finish(handle);
	buffer = srealloc(buffer, buflen+1);
	buffer[buflen] = '\0';

	/*
	 * Now set up the list of sessions. Note that "Default
	 * Settings" must always be claimed to exist, even if it
	 * doesn't really.
	 */

	p = buffer;
	nsessions = 1;		       /* "Default Settings" counts as one */
	while (*p) {
            if (strcmp(p, "Default Settings"))
		nsessions++;
	    while (*p) p++;
	    p++;
	}

	sessions = smalloc((nsessions+1) * sizeof(char *));
	sessions[0] = "Default Settings";
	p = buffer;
	i = 1;
	while (*p) {
            if (strcmp(p, "Default Settings"))
		sessions[i++] = p;
	    while (*p) p++;
	    p++;
	}

        qsort(sessions, i, sizeof(char *), sessioncmp);
    } else {
	sfree (buffer);
	sfree (sessions);
    }
}
