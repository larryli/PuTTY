#define SUPERSEDE_FONTSPEC_FOR_TESTING

#include "putty.h"
#include "storage.h"

void modalfatalbox(const char *p, ...)
{
    va_list ap;
    fprintf(stderr, "FATAL ERROR: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

char *platform_default_s(const char *name)
{ return NULL; }
bool platform_default_b(const char *name, bool def)
{ return def; }
int platform_default_i(const char *name, int def)
{ return def; }
FontSpec *platform_default_fontspec(const char *name)
{ return fontspec_new_default(); }
Filename *platform_default_filename(const char *name)
{ return filename_from_str(""); }
char *platform_get_x_display(void) { return NULL; }

void read_random_seed(noise_consumer_t consumer) {}
void write_random_seed(void *data, int len)
{ unreachable("no random seed in this application"); }
bool have_ssh_host_key(const char *hostname, int port,
                       const char *keytype) { return false; }
int check_stored_host_key(const char *hostname, int port,
                          const char *keytype, const char *key) { return 1; }
void store_host_key(Seat *seat, const char *hostname, int port,
                    const char *keytype, const char *key)
{ unreachable("no actual host keys in this application"); }

host_ca_enum *enum_host_ca_start(void) { return NULL; }
bool enum_host_ca_next(host_ca_enum *handle, strbuf *out) { return false; }
void enum_host_ca_finish(host_ca_enum *handle) {}
host_ca *host_ca_load(const char *name) { return NULL; }

void old_keyfile_warning(void) { }

const bool share_can_be_upstream = false;
const bool share_can_be_downstream = false;

struct FontSpec {
    char *name;
};

FontSpec *fontspec_new(const char *name)
{
    FontSpec *f = snew(FontSpec);
    f->name = dupstr(name);
    return f;
}

FontSpec *fontspec_new_default(void)
{
    return fontspec_new("");
}

FontSpec *fontspec_copy(const FontSpec *f)
{
    return fontspec_new(f->name);
}

void fontspec_free(FontSpec *f)
{
    sfree(f->name);
    sfree(f);
}

void fontspec_serialise(BinarySink *bs, FontSpec *f)
{
    put_asciz(bs, f->name);
}

FontSpec *fontspec_deserialise(BinarySource *src)
{
    return fontspec_new(get_asciz(src));
}

#define MAXKEY 16

typedef enum {
    SAVE_UNSET, SAVE_S, SAVE_I, SAVE_FONTSPEC, SAVE_FILENAME
} SaveType;

typedef struct SaveItem {
    const char *key;
    SaveType type;
    union {
        char sval[4096];
        int ival;
    };
} SaveItem;

struct settings_w {
    size_t n;
    SaveItem si[MAXKEY];
};

settings_w *open_settings_w(const char *sessionname, char **errmsg)
{ return NULL; }
void close_settings_w(settings_w *sw)
{ unreachable("we don't open and close in this test program"); }
settings_r *open_settings_r(const char *sessionname)
{ return NULL; }
void close_settings_r(settings_r *sr) { }

/* Work around lack of true snprintf before VS2015 */
#if defined _WINDOWS && \
    !defined __MINGW32__ && \
    !defined __WINE__ && \
    _MSC_VER < 1900
#define snprintf _snprintf
#endif

void write_setting_s(settings_w *sw, const char *key, const char *value)
{
    for (size_t i = 0; i < sw->n; i++) {
        if (!strcmp(key, sw->si[i].key)) {
            sw->si[i].type = SAVE_S;
            snprintf(sw->si[i].sval, sizeof(sw->si[i].sval), "%s", value);
            break;
        }
    }
}

void write_setting_i(settings_w *sw, const char *key, int value)
{
    for (size_t i = 0; i < sw->n; i++) {
        if (!strcmp(key, sw->si[i].key)) {
            sw->si[i].type = SAVE_I;
            sw->si[i].ival = value;
            break;
        }
    }
}

void write_setting_fontspec(settings_w *sw, const char *key, FontSpec *fs)
{
    for (size_t i = 0; i < sw->n; i++) {
        if (!strcmp(key, sw->si[i].key)) {
            sw->si[i].type = SAVE_FONTSPEC;
            snprintf(sw->si[i].sval, sizeof(sw->si[i].sval), "%s", fs->name);
            break;
        }
    }
}

void write_setting_filename(settings_w *sw, const char *key, Filename *fn)
{
    for (size_t i = 0; i < sw->n; i++) {
        if (!strcmp(key, sw->si[i].key)) {
            sw->si[i].type = SAVE_FILENAME;
            snprintf(sw->si[i].sval, sizeof(sw->si[i].sval), "%s",
                     filename_to_str(fn));
            break;
        }
    }
}

struct settings_r {
    size_t n;
    SaveItem si[MAXKEY];
};

char *read_setting_s(settings_r *sr, const char *key)
{
    if (sr)
        for (size_t i = 0; i < sr->n; i++)
            if (!strcmp(key, sr->si[i].key) && sr->si[i].type == SAVE_S)
                return dupstr(sr->si[i].sval);
    return NULL;
}

int read_setting_i(settings_r *sr, const char *key, int defvalue)
{
    if (sr)
        for (size_t i = 0; i < sr->n; i++)
            if (!strcmp(key, sr->si[i].key) && sr->si[i].type == SAVE_I)
                return sr->si[i].ival;
    return defvalue;
}

FontSpec *read_setting_fontspec(settings_r *sr, const char *key)
{
    if (sr)
        for (size_t i = 0; i < sr->n; i++)
            if (!strcmp(key, sr->si[i].key) && sr->si[i].type == SAVE_FONTSPEC)
                return fontspec_new(sr->si[i].sval);
    return NULL;
}

Filename *read_setting_filename(settings_r *sr, const char *key)
{
    if (sr)
        for (size_t i = 0; i < sr->n; i++)
            if (!strcmp(key, sr->si[i].key) && sr->si[i].type == SAVE_FILENAME)
                return filename_from_str(sr->si[i].sval);
    return NULL;
}

void del_settings(const char *sessionname) {}

settings_e *enum_settings_start(void)
{ return NULL; }
bool enum_settings_next(settings_e *handle, strbuf *out)
{ unreachable("where did you get a settings_e from?"); }
void enum_settings_finish(settings_e *handle)
{ unreachable("where did you get a settings_e from?"); }

static int nfails = 0;

void test_str_simple(int confid, const char *saveid, const char *defexp)
{
    Conf *conf = conf_new();

    do_defaults(NULL, conf);
    const char *defgot = conf_get_str(conf, confid);
    if (0 != strcmp(defgot, defexp)) {
        printf("fail test_str_simple(%s): default = '%s', expected '%s'\n",
               saveid, defgot, defexp);
        nfails++;
    }

    for (int i = 0; i < 2; i++) {
        settings_w sw = {
            .n = 1,
            .si[0].key = saveid,
            .si[0].type = SAVE_UNSET,
        };
        static const char *const teststrings[] = { "foo", "bar" };
        const char *teststring = teststrings[i];
        conf_set_str(conf, confid, teststring);
        save_open_settings(&sw, conf);
        if (sw.si[0].type != SAVE_S) {
            printf("fail test_str_simple(%s): saved type = %d, expected %d\n",
                   saveid, sw.si[0].type, SAVE_S);
            nfails++;
        } else if (0 != strcmp(sw.si[0].sval, teststring)) {
            printf("fail test_str_simple(%s): "
                   "saved string = '%s', expected '%s'\n",
                   saveid, sw.si[0].sval, teststring);
            nfails++;
        }

        conf_clear(conf);
        settings_r sr = {
            .n = 1,
            .si[0].key = saveid,
            .si[0].type = SAVE_S,
        };
        snprintf(sr.si[0].sval, sizeof(sr.si[0].sval), "%s", teststring);
        load_open_settings(&sr, conf);
        const char *loaded = conf_get_str(conf, confid);
        if (0 != strcmp(loaded, teststring)) {
            printf("fail test_str_simple(%s): "
                   "loaded string = '%s', expected '%s'\n",
                   saveid, loaded, teststring);
            nfails++;
        }
    }
            
    conf_free(conf);
}

void test_utf8_simple(int confid, const char *saveid, const char *defexp)
{
    Conf *conf = conf_new();

    do_defaults(NULL, conf);
    const char *defgot = conf_get_utf8(conf, confid);
    if (0 != strcmp(defgot, defexp)) {
        printf("fail test_utf8_simple(%s): default = '%s', expected '%s'\n",
               saveid, defgot, defexp);
        nfails++;
    }

    for (int i = 0; i < 2; i++) {
        settings_w sw = {
            .n = 1,
            .si[0].key = saveid,
            .si[0].type = SAVE_UNSET,
        };
        static const char *const teststrings[] = { "foo", "bar" };
        const char *teststring = teststrings[i];
        conf_set_utf8(conf, confid, teststring);
        save_open_settings(&sw, conf);
        if (sw.si[0].type != SAVE_S) {
            printf("fail test_utf8_simple(%s): saved type = %d, expected %d\n",
                   saveid, sw.si[0].type, SAVE_S);
            nfails++;
        } else if (0 != strcmp(sw.si[0].sval, teststring)) {
            printf("fail test_utf8_simple(%s): "
                   "saved string = '%s', expected '%s'\n",
                   saveid, sw.si[0].sval, teststring);
            nfails++;
        }

        conf_clear(conf);
        settings_r sr = {
            .n = 1,
            .si[0].key = saveid,
            .si[0].type = SAVE_S,
        };
        snprintf(sr.si[0].sval, sizeof(sr.si[0].sval), "%s", teststring);
        load_open_settings(&sr, conf);
        const char *loaded = conf_get_utf8(conf, confid);
        if (0 != strcmp(loaded, teststring)) {
            printf("fail test_utf8_simple(%s): "
                   "loaded string = '%s', expected '%s'\n",
                   saveid, loaded, teststring);
            nfails++;
        }
    }

    conf_free(conf);
}

void test_str_ambi_simple(int confid, const char *saveid,
                          const char *defexp, bool defutf8)
{
    Conf *conf = conf_new();
    bool utf8;

    do_defaults(NULL, conf);
    const char *defgot = conf_get_str_ambi(conf, confid, &utf8);
    if (0 != strcmp(defgot, defexp) || utf8 != defutf8) {
        printf("fail test_str_ambi_simple(%s): "
               "default = '%s' (%s), expected '%s' (%s)\n",
               saveid, defgot, utf8 ? "native" : "UTF-8",
               defexp, defutf8 ? "native" : "UTF-8");
        nfails++;
    }

    for (int i = 0; i < 2; i++) {
        settings_w sw = {
            .n = 1,
            .si[0].key = saveid,
            .si[0].type = SAVE_UNSET,
        };
        static const char *const teststrings[] = { "foo", "bar" };
        const char *teststring = teststrings[i];
        conf_set_str(conf, confid, teststring);
        save_open_settings(&sw, conf);
        if (sw.si[0].type != SAVE_S) {
            printf("fail test_str_ambi_simple(%s): "
                   "saved type = %d, expected %d\n",
                   saveid, sw.si[0].type, SAVE_S);
            nfails++;
        } else if (0 != strcmp(sw.si[0].sval, teststring)) {
            printf("fail test_str_ambi_simple(%s): "
                   "saved string = '%s', expected '%s'\n",
                   saveid, sw.si[0].sval, teststring);
            nfails++;
        }

        conf_clear(conf);
        settings_r sr = {
            .n = 1,
            .si[0].key = saveid,
            .si[0].type = SAVE_S,
        };
        snprintf(sr.si[0].sval, sizeof(sr.si[0].sval), "%s", teststring);
        load_open_settings(&sr, conf);
        const char *loaded = conf_get_str_ambi(conf, confid, &utf8);
        if (0 != strcmp(loaded, teststring) || utf8) {
            printf("fail test_str_ambi_simple(%s): "
                   "loaded string = '%s' (%s), expected '%s' (native)\n",
                   saveid, loaded, utf8 ? "native" : "UTF-8", teststring);
            nfails++;
        }
    }

    conf_free(conf);
}

void test_int_simple(int confid, const char *saveid, int defexp)
{
    Conf *conf = conf_new();

    do_defaults(NULL, conf);
    int defgot = conf_get_int(conf, confid);
    if (defgot != defexp) {
        printf("fail test_int_simple(%s): default = %d, expected %d\n",
               saveid, defgot, defexp);
        nfails++;
    }

    for (int i = 0; i < 2; i++) {
        settings_w sw = {
            .n = 1,
            .si[0].key = saveid,
            .si[0].type = SAVE_UNSET,
        };
        static const int testints[] = { 12345, 54321 };
        int testint = testints[i];
        conf_set_int(conf, confid, testint);
        save_open_settings(&sw, conf);
        if (sw.si[0].type != SAVE_I) {
            printf("fail test_int_simple(%s): saved type = %d, expected %d\n",
                   saveid, sw.si[0].type, SAVE_I);
            nfails++;
        } else if (sw.si[0].ival != testint) {
            printf("fail test_int_simple(%s): "
                   "saved integer = %d, expected %d\n",
                   saveid, sw.si[0].ival, testint);
            nfails++;
        }

        conf_clear(conf);
        settings_r sr = {
            .n = 1,
            .si[0].key = saveid,
            .si[0].type = SAVE_I,
            .si[0].ival = testint,
        };
        load_open_settings(&sr, conf);
        int loaded = conf_get_int(conf, confid);
        if (loaded != testint) {
            printf("fail test_int_simple(%s): "
                   "loaded integer = %d, expected %d\n",
                   saveid, loaded, testint);
            nfails++;
        }
    }
            
    conf_free(conf);
}

void test_int_translated_internal(
    int confid, const char *saveid, bool test_save, bool test_load,
    void (*load_prepare)(settings_r *), int defexp, va_list ap)
{
    Conf *conf = conf_new();

    do_defaults(NULL, conf);
    int defgot = conf_get_int(conf, confid);
    if (defgot != defexp) {
        printf("fail test_int_translated(%s): default = %d, expected %d\n",
               saveid, defgot, defexp);
        nfails++;
    }

    int confval = va_arg(ap, int);
    while (confval != -1) {
        int storageval = va_arg(ap, int);

        if (test_save) {
            settings_w sw = {
                .n = 1,
                .si[0].key = saveid,
                .si[0].type = SAVE_UNSET,
            };
            conf_set_int(conf, confid, confval);
            save_open_settings(&sw, conf);
            if (sw.si[0].type != SAVE_I) {
                printf("fail test_int_translated(%s): "
                       "saved type = %d, expected %d\n",
                       saveid, sw.si[0].type, SAVE_I);
                nfails++;
            } else if (sw.si[0].ival != storageval) {
                printf("fail test_int_translated(%s.%d.%d): "
                       "saved integer = %d, expected %d\n",
                       saveid, confval, storageval, sw.si[0].ival, storageval);
                nfails++;
            }
        }

        if (test_load) {
            conf_clear(conf);
            settings_r sr = {
                .n = 1,
                .si[0].key = saveid,
                .si[0].type = SAVE_I,
                .si[0].ival = storageval,
            };
            if (load_prepare)
                load_prepare(&sr);
            load_open_settings(&sr, conf);
            int loaded = conf_get_int(conf, confid);
            if (loaded != confval) {
                printf("fail test_int_translated(%s.%d.%d): "
                       "loaded integer = %d, expected %d\n",
                       saveid, confval, storageval, loaded, confval);
                nfails++;
            }
        }

        confval = va_arg(ap, int);
    }
            
    conf_free(conf);
}

void test_int_translated(int confid, const char *saveid, int defexp, ...)
{
    va_list ap;
    va_start(ap, defexp);
    test_int_translated_internal(confid, saveid, true, true, NULL, defexp, ap);
    va_end(ap);
}

void test_int_translated_load_legacy(
    int confid, const char *saveid, void (*load_prepare)(settings_r *),
    int defexp, ...)
{
    va_list ap;
    va_start(ap, defexp);
    test_int_translated_internal(confid, saveid, false, true, load_prepare,
                                 defexp, ap);
    va_end(ap);
}

void test_bool_simple(int confid, const char *saveid, bool defexp)
{
    Conf *conf = conf_new();

    do_defaults(NULL, conf);
    bool defgot = conf_get_bool(conf, confid);
    if (defgot != defexp) {
        printf("fail test_bool_simple(%s): default = %d, expected %d\n",
               saveid, defgot, defexp);
        nfails++;
    }

    for (int i = 0; i < 2; i++) {
        settings_w sw = {
            .n = 1,
            .si[0].key = saveid,
            .si[0].type = SAVE_UNSET,
        };
        static const bool testbools[] = { false, true };
        bool testbool = testbools[i];
        conf_set_bool(conf, confid, testbool);
        save_open_settings(&sw, conf);
        if (sw.si[0].type != SAVE_I) {
            printf("fail test_bool_simple(%s): saved type = %d, expected %d\n",
                   saveid, sw.si[0].type, SAVE_I);
            nfails++;
        } else if (sw.si[0].ival != testbool) {
            printf("fail test_bool_simple(%s): "
                   "saved integer = %d, expected %d\n",
                   saveid, sw.si[0].ival, testbool);
            nfails++;
        }

        conf_clear(conf);
        settings_r sr = {
            .n = 1,
            .si[0].key = saveid,
            .si[0].type = SAVE_I,
            .si[0].ival = testbool,
        };
        load_open_settings(&sr, conf);
        bool loaded = conf_get_bool(conf, confid);
        if (loaded != testbool) {
            printf("fail test_bool_simple(%s): "
                   "loaded boolean = %d, expected %d\n",
                   saveid, loaded, testbool);
            nfails++;
        }
    }
            
    conf_free(conf);
}

void test_file_simple(int confid, const char *saveid)
{
    Conf *conf = conf_new();

    do_defaults(NULL, conf);

    for (int i = 0; i < 2; i++) {
        settings_w sw = {
            .n = 1,
            .si[0].key = saveid,
            .si[0].type = SAVE_UNSET,
        };
        static const char *const teststrings[] = { "foo", "bar" };
        const char *teststring = teststrings[i];
        Filename *testfn = filename_from_str(teststring);
        conf_set_filename(conf, confid, testfn);
        filename_free(testfn);
        save_open_settings(&sw, conf);
        if (sw.si[0].type != SAVE_FILENAME) {
            printf("fail test_file_simple(%s): saved type = %d, expected %d\n",
                   saveid, sw.si[0].type, SAVE_FILENAME);
            nfails++;
        } else if (0 != strcmp(sw.si[0].sval, teststring)) {
            printf("fail test_file_simple(%s): "
                   "saved string = '%s', expected '%s'\n",
                   saveid, sw.si[0].sval, teststring);
            nfails++;
        }

        conf_clear(conf);
        settings_r sr = {
            .n = 1,
            .si[0].key = saveid,
            .si[0].type = SAVE_FILENAME,
        };
        snprintf(sr.si[0].sval, sizeof(sr.si[0].sval), "%s", teststring);
        load_open_settings(&sr, conf);
        const char *loaded = filename_to_str(conf_get_filename(conf, confid));
        if (0 != strcmp(loaded, teststring)) {
            printf("fail test_file_simple(%s): "
                   "loaded string = '%s', expected '%s'\n",
                   saveid, loaded, teststring);
            nfails++;
        }
    }
            
    conf_free(conf);
}

void test_font_simple(int confid, const char *saveid)
{
    Conf *conf = conf_new();

    do_defaults(NULL, conf);

    for (int i = 0; i < 2; i++) {
        settings_w sw = {
            .n = 1,
            .si[0].key = saveid,
            .si[0].type = SAVE_UNSET,
        };
        static const char *const teststrings[] = { "foo", "bar" };
        const char *teststring = teststrings[i];
        FontSpec *testfs = fontspec_new(teststring);
        conf_set_fontspec(conf, confid, testfs);
        fontspec_free(testfs);
        save_open_settings(&sw, conf);
        if (sw.si[0].type != SAVE_FONTSPEC) {
            printf("fail test_font_simple(%s): saved type = %d, expected %d\n",
                   saveid, sw.si[0].type, SAVE_FONTSPEC);
            nfails++;
        } else if (0 != strcmp(sw.si[0].sval, teststring)) {
            printf("fail test_font_simple(%s): "
                   "saved string = '%s', expected '%s'\n",
                   saveid, sw.si[0].sval, teststring);
            nfails++;
        }

        conf_clear(conf);
        settings_r sr = {
            .n = 1,
            .si[0].key = saveid,
            .si[0].type = SAVE_FONTSPEC,
        };
        snprintf(sr.si[0].sval, sizeof(sr.si[0].sval), "%s", teststring);
        load_open_settings(&sr, conf);
        const char *loaded = conf_get_fontspec(conf, confid)->name;
        if (0 != strcmp(loaded, teststring)) {
            printf("fail test_file_simple(%s): "
                   "loaded string = '%s', expected '%s'\n",
                   saveid, loaded, teststring);
            nfails++;
        }
    }

    conf_free(conf);
}

static void load_prepare_socks4(settings_r *sr)
{
    size_t pos = sr->n++;
    sr->si[pos].key = "ProxySOCKSVersion";
    sr->si[pos].type = SAVE_I;
    sr->si[pos].ival = 4;
}

void test_simple(void)
{
    test_str_simple(CONF_host, "HostName", "");
    test_int_translated(CONF_addressfamily, "AddressFamily", ADDRTYPE_UNSPEC,
                        ADDRTYPE_UNSPEC, 0, ADDRTYPE_IPV4, 1,
                        ADDRTYPE_IPV6, 2, -1);
    test_bool_simple(CONF_warn_on_close, "WarnOnClose", true);
    test_bool_simple(CONF_tcp_nodelay, "TCPNoDelay", true);
    test_bool_simple(CONF_tcp_keepalives, "TCPKeepalives", false);
    test_str_simple(CONF_loghost, "LogHost", "");
    test_str_simple(CONF_proxy_exclude_list, "ProxyExcludeList", "");
    test_bool_simple(CONF_even_proxy_localhost, "ProxyLocalhost", false);
    test_str_simple(CONF_proxy_host, "ProxyHost", "proxy");
    test_int_simple(CONF_proxy_port, "ProxyPort", 80);
    test_str_simple(CONF_proxy_username, "ProxyUsername", "");
    test_str_simple(CONF_proxy_password, "ProxyPassword", "");
    test_str_simple(CONF_proxy_telnet_command, "ProxyTelnetCommand", "connect %host %port\\n");
    test_int_translated(CONF_proxy_log_to_term, "ProxyLogToTerm", FORCE_OFF,
                        FORCE_ON, 0, FORCE_OFF, 1, AUTO, 2, -1);
    test_str_ambi_simple(CONF_remote_cmd, "RemoteCommand", "", false);
    test_bool_simple(CONF_nopty, "NoPTY", false);
    test_bool_simple(CONF_compression, "Compression", false);
    test_bool_simple(CONF_ssh_prefer_known_hostkeys, "PreferKnownHostKeys", true);
    test_int_simple(CONF_ssh_rekey_time, "RekeyTime", 60);
    test_str_simple(CONF_ssh_rekey_data, "RekeyBytes", "1G");
    test_bool_simple(CONF_tryagent, "TryAgent", true);
    test_bool_simple(CONF_agentfwd, "AgentFwd", false);
    test_bool_simple(CONF_change_username, "ChangeUsername", false);
    test_file_simple(CONF_keyfile, "PublicKeyFile");
    test_file_simple(CONF_detached_cert, "DetachedCertificate");
    test_str_simple(CONF_auth_plugin, "AuthPlugin", "");
    test_bool_simple(CONF_ssh2_des_cbc, "SSH2DES", false);
    test_bool_simple(CONF_ssh_no_userauth, "SshNoAuth", false);
    test_bool_simple(CONF_ssh_no_trivial_userauth, "SshNoTrivialAuth", false);
    test_bool_simple(CONF_ssh_show_banner, "SshBanner", true);
    test_bool_simple(CONF_try_tis_auth, "AuthTIS", false);
    test_bool_simple(CONF_try_ki_auth, "AuthKI", true);
    test_bool_simple(CONF_ssh_no_shell, "SshNoShell", false);
    test_str_simple(CONF_termtype, "TerminalType", "xterm");
    test_str_simple(CONF_termspeed, "TerminalSpeed", "38400,38400");
    test_str_ambi_simple(CONF_username, "UserName", "", false);
    test_bool_simple(CONF_username_from_env, "UserNameFromEnvironment", false);
    test_str_simple(CONF_localusername, "LocalUserName", "");
    test_bool_simple(CONF_rfc_environ, "RFCEnviron", false);
    test_bool_simple(CONF_passive_telnet, "PassiveTelnet", false);
    test_str_simple(CONF_serline, "SerialLine", "");
    test_int_simple(CONF_serspeed, "SerialSpeed", 9600);
    test_int_simple(CONF_serdatabits, "SerialDataBits", 8);
    test_int_simple(CONF_serstopbits, "SerialStopHalfbits", 2);
    test_int_translated(CONF_serparity, "SerialParity", SER_PAR_NONE,
                        SER_PAR_NONE, 0, SER_PAR_ODD, 1, SER_PAR_EVEN, 2,
                        SER_PAR_MARK, 3, SER_PAR_SPACE, 4, -1);
    test_int_translated(CONF_serflow, "SerialFlowControl", SER_FLOW_XONXOFF,
                        SER_FLOW_NONE, 0, SER_FLOW_XONXOFF, 1,
                        SER_FLOW_RTSCTS, 2, SER_FLOW_DSRDTR, 3, -1);
    test_str_simple(CONF_supdup_location, "SUPDUPLocation", "The Internet");
    test_int_translated(CONF_supdup_ascii_set, "SUPDUPCharset",
                        SUPDUP_CHARSET_ASCII,
                        SUPDUP_CHARSET_ASCII, 0,
                        SUPDUP_CHARSET_ITS, 1,
                        SUPDUP_CHARSET_WAITS, 2, -1);
    test_bool_simple(CONF_supdup_more, "SUPDUPMoreProcessing", false);
    test_bool_simple(CONF_supdup_scroll, "SUPDUPScrolling", false);
    test_bool_simple(CONF_bksp_is_delete, "BackspaceIsDelete", true);
    test_bool_simple(CONF_rxvt_homeend, "RXVTHomeEnd", false);
    test_int_translated(CONF_funky_type, "LinuxFunctionKeys", FUNKY_TILDE,
                        FUNKY_TILDE, 0, FUNKY_LINUX, 1, FUNKY_XTERM, 2,
                        FUNKY_VT400, 3, FUNKY_VT100P, 4, FUNKY_SCO, 5,
                        FUNKY_XTERM_216, 6, -1);
    test_int_translated(CONF_sharrow_type, "ShiftedArrowKeys",
                        SHARROW_APPLICATION,
                        SHARROW_APPLICATION, 0, SHARROW_BITMAP, 1, -1);
    test_bool_simple(CONF_no_applic_c, "NoApplicationCursors", false);
    test_bool_simple(CONF_no_applic_k, "NoApplicationKeys", false);
    test_bool_simple(CONF_no_mouse_rep, "NoMouseReporting", false);
    test_bool_simple(CONF_no_remote_resize, "NoRemoteResize", false);
    test_bool_simple(CONF_no_alt_screen, "NoAltScreen", false);
    test_bool_simple(CONF_no_remote_wintitle, "NoRemoteWinTitle", false);
    test_bool_simple(CONF_no_remote_clearscroll, "NoRemoteClearScroll", false);
    test_bool_simple(CONF_no_dbackspace, "NoDBackspace", false);
    test_bool_simple(CONF_no_remote_charset, "NoRemoteCharset", false);
    /* note we have no test for CONF_remote_qtitle_action because no default */
    test_bool_simple(CONF_app_cursor, "ApplicationCursorKeys", false);
    test_bool_simple(CONF_app_keypad, "ApplicationKeypad", false);
    test_bool_simple(CONF_nethack_keypad, "NetHackKeypad", false);
    test_bool_simple(CONF_telnet_keyboard, "TelnetKey", false);
    test_bool_simple(CONF_telnet_newline, "TelnetRet", true);
    test_bool_simple(CONF_alt_f4, "AltF4", true);
    test_bool_simple(CONF_alt_space, "AltSpace", false);
    test_bool_simple(CONF_alt_only, "AltOnly", false);
    test_int_translated(CONF_localecho, "LocalEcho", AUTO,
                        FORCE_ON, 0, FORCE_OFF, 1, AUTO, 2, -1);
    test_int_translated(CONF_localedit, "LocalEdit", AUTO,
                        FORCE_ON, 0, FORCE_OFF, 1, AUTO, 2, -1);
    test_bool_simple(CONF_alwaysontop, "AlwaysOnTop", false);
    test_bool_simple(CONF_fullscreenonaltenter, "FullScreenOnAltEnter", false);
    test_bool_simple(CONF_scroll_on_key, "ScrollOnKey", false);
    test_bool_simple(CONF_scroll_on_disp, "ScrollOnDisp", true);
    test_bool_simple(CONF_erase_to_scrollback, "EraseToScrollback", true);
    test_bool_simple(CONF_compose_key, "ComposeKey", false);
    test_bool_simple(CONF_ctrlaltkeys, "CtrlAltKeys", true);
    test_str_simple(CONF_wintitle, "WinTitle", "");
    test_int_simple(CONF_savelines, "ScrollbackLines", 2000);
    test_bool_simple(CONF_dec_om, "DECOriginMode", false);
    test_bool_simple(CONF_wrap_mode, "AutoWrapMode", true);
    test_bool_simple(CONF_lfhascr, "LFImpliesCR", false);
    test_int_translated(CONF_cursor_type, "CurType", CURSOR_BLOCK,
                        CURSOR_BLOCK, 0, CURSOR_UNDERLINE, 1,
                        CURSOR_VERTICAL_LINE, 2, -1);
    test_bool_simple(CONF_blink_cur, "BlinkCur", false);
    test_int_translated(CONF_beep, "Beep", 1,
                        BELL_DISABLED, 0, BELL_DEFAULT, 1, BELL_VISUAL, 2,
                        BELL_WAVEFILE, 3, BELL_PCSPEAKER, 4, -1);
    test_int_translated(CONF_beep_ind, "BeepInd", 0,
                        B_IND_DISABLED, 0, B_IND_FLASH, 1, B_IND_STEADY, 2, -1);
    test_bool_simple(CONF_bellovl, "BellOverload", true);
    test_int_simple(CONF_bellovl_n, "BellOverloadN", 5);
    test_file_simple(CONF_bell_wavefile, "BellWaveFile");
    test_bool_simple(CONF_scrollbar, "ScrollBar", true);
    test_bool_simple(CONF_scrollbar_in_fullscreen, "ScrollBarFullScreen", false);
    test_int_translated(CONF_resize_action, "LockSize", RESIZE_TERM,
                        RESIZE_TERM, 0, RESIZE_DISABLED, 1, RESIZE_FONT, 2,
                        RESIZE_EITHER, 3, -1);
    test_bool_simple(CONF_bce, "BCE", true);
    test_bool_simple(CONF_blinktext, "BlinkText", false);
    test_bool_simple(CONF_win_name_always, "WinNameAlways", true);
    test_int_simple(CONF_width, "TermWidth", 80);
    test_int_simple(CONF_height, "TermHeight", 24);
    test_font_simple(CONF_font, "Font");
    test_int_translated(CONF_font_quality, "FontQuality", FQ_DEFAULT,
                        FQ_DEFAULT, 0, FQ_ANTIALIASED, 1, FQ_NONANTIALIASED, 2,
                        FQ_CLEARTYPE, 3, -1);
    test_file_simple(CONF_logfilename, "LogFileName");
    test_int_translated(CONF_logtype, "LogType", LGTYP_NONE,
                        LGTYP_NONE, 0, LGTYP_ASCII, 1, LGTYP_DEBUG, 2,
                        LGTYP_PACKETS, 3, LGTYP_SSHRAW, 4, -1);
    /* FIXME: this won't work because -1 is also the terminator, darn */
    test_int_translated(CONF_logxfovr, "LogFileClash", LGXF_ASK,
                        LGXF_OVR, 1, LGXF_APN, 0, LGXF_ASK, -1, -1);
    test_bool_simple(CONF_logflush, "LogFlush", true);
    test_bool_simple(CONF_logheader, "LogHeader", true);
    test_bool_simple(CONF_logomitpass, "SSHLogOmitPasswords", true);
    test_bool_simple(CONF_logomitdata, "SSHLogOmitData", false);
    test_bool_simple(CONF_hide_mouseptr, "HideMousePtr", false);
    test_bool_simple(CONF_sunken_edge, "SunkenEdge", false);
    test_int_simple(CONF_window_border, "WindowBorder", 1);
    test_str_simple(CONF_answerback, "Answerback", "PuTTY");
    test_str_simple(CONF_printer, "Printer", "");
    test_bool_simple(CONF_no_arabicshaping, "DisableArabicShaping", false);
    test_bool_simple(CONF_no_bidi, "DisableBidi", false);
    test_bool_simple(CONF_ansi_colour, "ANSIColour", true);
    test_bool_simple(CONF_xterm_256_colour, "Xterm256Colour", true);
    test_bool_simple(CONF_true_colour, "TrueColour", true);
    test_bool_simple(CONF_system_colour, "UseSystemColours", false);
    test_bool_simple(CONF_try_palette, "TryPalette", false);
    test_int_translated(CONF_mouse_is_xterm, "MouseIsXterm", 0,
                        MOUSE_COMPROMISE, 0, MOUSE_XTERM, 1,
                        MOUSE_WINDOWS, 2, -1);
    test_bool_simple(CONF_rect_select, "RectSelect", false);
    test_bool_simple(CONF_paste_controls, "PasteControls", false);
    test_bool_simple(CONF_rawcnp, "RawCNP", false);
    test_bool_simple(CONF_utf8linedraw, "UTF8linedraw", false);
    test_bool_simple(CONF_rtf_paste, "PasteRTF", false);
    test_bool_simple(CONF_mouse_override, "MouseOverride", true);
    test_bool_simple(CONF_mouseautocopy, "MouseAutocopy", CLIPUI_DEFAULT_AUTOCOPY);
    test_int_translated(CONF_vtmode, "FontVTMode", VT_UNICODE,
                        VT_XWINDOWS, 0,
                        VT_OEMANSI, 1,
                        VT_OEMONLY, 2,
                        VT_POORMAN, 3,
                        VT_UNICODE, 4,
                        -1);
    test_str_simple(CONF_line_codepage, "LineCodePage", "");
    test_bool_simple(CONF_cjk_ambig_wide, "CJKAmbigWide", false);
    test_bool_simple(CONF_utf8_override, "UTF8Override", true);
    test_bool_simple(CONF_xlat_capslockcyr, "CapsLockCyr", false);
    test_bool_simple(CONF_x11_forward, "X11Forward", false);
    test_str_simple(CONF_x11_display, "X11Display", "");
    test_int_translated(CONF_x11_auth, "X11AuthType", X11_MIT,
                        X11_NO_AUTH, 0, X11_MIT, 1, X11_XDM, 2, -1);
    test_file_simple(CONF_xauthfile, "X11AuthFile");
    test_bool_simple(CONF_lport_acceptall, "LocalPortAcceptAll", false);
    test_bool_simple(CONF_rport_acceptall, "RemotePortAcceptAll", false);
    test_bool_simple(CONF_ssh_connection_sharing, "ConnectionSharing", false);
    test_bool_simple(CONF_ssh_connection_sharing_upstream, "ConnectionSharingUpstream", true);
    test_bool_simple(CONF_ssh_connection_sharing_downstream, "ConnectionSharingDownstream", true);
    test_bool_simple(CONF_stamp_utmp, "StampUtmp", true);
    test_bool_simple(CONF_login_shell, "LoginShell", true);
    test_bool_simple(CONF_scrollbar_on_left, "ScrollbarOnLeft", false);
    test_bool_simple(CONF_shadowbold, "ShadowBold", false);
    test_font_simple(CONF_boldfont, "BoldFont");
    test_font_simple(CONF_widefont, "WideFont");
    test_font_simple(CONF_wideboldfont, "WideBoldFont");
    test_int_simple(CONF_shadowboldoffset, "ShadowBoldOffset", 1);
    test_bool_simple(CONF_crhaslf, "CRImpliesLF", false);
    test_str_simple(CONF_winclass, "WindowClass", "");
    test_int_translated(CONF_close_on_exit, "CloseOnExit", AUTO,
                        FORCE_OFF, 0, AUTO, 1, FORCE_ON, 2, -1);
    test_int_translated(CONF_proxy_dns, "ProxyDNS", AUTO,
                        FORCE_OFF, 0, AUTO, 1, FORCE_ON, 2, -1);
    test_int_translated(CONF_bold_style, "BoldAsColour", AUTO,
                        1, 0, 2, 1, 3, 2, -1);
    test_int_translated(CONF_sshbug_ignore1, "BugIgnore1", AUTO,
                        AUTO, 0, FORCE_OFF, 1, FORCE_ON, 2, -1);
    test_int_translated(CONF_sshbug_plainpw1, "BugPlainPW1", AUTO,
                        AUTO, 0, FORCE_OFF, 1, FORCE_ON, 2, -1);
    test_int_translated(CONF_sshbug_rsa1, "BugRSA1", AUTO,
                        AUTO, 0, FORCE_OFF, 1, FORCE_ON, 2, -1);
    test_int_translated(CONF_sshbug_ignore2, "BugIgnore2", AUTO,
                        AUTO, 0, FORCE_OFF, 1, FORCE_ON, 2, -1);
    test_int_translated(CONF_sshbug_derivekey2, "BugDeriveKey2", AUTO,
                        AUTO, 0, FORCE_OFF, 1, FORCE_ON, 2, -1);
    test_int_translated(CONF_sshbug_rsapad2, "BugRSAPad2", AUTO,
                        AUTO, 0, FORCE_OFF, 1, FORCE_ON, 2, -1);
    test_int_translated(CONF_sshbug_pksessid2, "BugPKSessID2", AUTO,
                        AUTO, 0, FORCE_OFF, 1, FORCE_ON, 2, -1);
    test_int_translated(CONF_sshbug_rekey2, "BugRekey2", AUTO,
                        AUTO, 0, FORCE_OFF, 1, FORCE_ON, 2, -1);
    test_int_translated(CONF_sshbug_maxpkt2, "BugMaxPkt2", AUTO,
                        AUTO, 0, FORCE_OFF, 1, FORCE_ON, 2, -1);
    test_int_translated(CONF_sshbug_oldgex2, "BugOldGex2", AUTO,
                        AUTO, 0, FORCE_OFF, 1, FORCE_ON, 2, -1);
    test_int_translated(CONF_sshbug_winadj, "BugWinadj", AUTO,
                        AUTO, 0, FORCE_OFF, 1, FORCE_ON, 2, -1);
    test_int_translated(CONF_sshbug_chanreq, "BugChanReq", AUTO,
                        AUTO, 0, FORCE_OFF, 1, FORCE_ON, 2, -1);
    test_int_translated(CONF_sshbug_dropstart, "BugDropStart", FORCE_OFF,
                        FORCE_OFF, 1, FORCE_ON, 2, -1);
    test_int_translated(CONF_sshbug_filter_kexinit, "BugFilterKexinit", FORCE_OFF,
                        FORCE_OFF, 1, FORCE_ON, 2, -1);
    test_int_translated(CONF_sshbug_rsa_sha2_cert_userauth, "BugRSASHA2CertUserauth", AUTO,
                        AUTO, 0, FORCE_OFF, 1, FORCE_ON, 2, -1);
    test_int_translated(CONF_proxy_type, "ProxyMethod", PROXY_NONE,
                        PROXY_NONE, 0, PROXY_SOCKS4, 1, PROXY_SOCKS5, 2,
                        PROXY_HTTP, 3, PROXY_TELNET, 4, PROXY_CMD, 5,
                        PROXY_SSH_TCPIP, 6, PROXY_SSH_EXEC, 7,
                        PROXY_SSH_SUBSYSTEM, 8, -1);
    test_int_translated_load_legacy(
        CONF_proxy_type, "ProxyType", NULL, PROXY_NONE,
        PROXY_HTTP, 1, PROXY_SOCKS5, 2, PROXY_TELNET, 3, PROXY_CMD, 4, -1);
    test_int_translated_load_legacy(
        CONF_proxy_type, "ProxyType", load_prepare_socks4, PROXY_NONE,
        PROXY_HTTP, 1, PROXY_SOCKS4, 2, PROXY_TELNET, 3, PROXY_CMD, 4, -1);
    test_int_translated(CONF_remote_qtitle_action, "RemoteQTitleAction", TITLE_EMPTY,
                        TITLE_NONE, 0, TITLE_EMPTY, 1, TITLE_REAL, 2, -1);
    test_int_translated_load_legacy(
        CONF_remote_qtitle_action, "NoRemoteQTitle", NULL, TITLE_EMPTY,
        TITLE_REAL, 0, TITLE_EMPTY, 1, -1);
}

void test_conf_key_info(void)
{
    struct test_data {
        const char *name;
        bool got_value_type : 1;
        bool got_subkey_type : 1;
        bool got_default : 1;
        bool got_default_int : 1;
        bool got_default_str : 1;
        bool got_default_bool : 1;
        bool got_save_keyword : 1;
        bool got_storage_enum : 1;
        bool save_custom : 1;
        bool load_custom : 1;
        bool not_saved : 1;
    };

#define CONF_OPTION(id, ...) { .name = "CONF_" #id, __VA_ARGS__ },
#define VALUE_TYPE(x) .got_value_type = true
#define SUBKEY_TYPE(x) .got_subkey_type = true
#define DEFAULT_INT(x) .got_default_int = true, .got_default = true
#define DEFAULT_STR(x) .got_default_str = true, .got_default = true
#define DEFAULT_BOOL(x) .got_default_bool = true, .got_default = true
#define SAVE_KEYWORD(x) .got_save_keyword = true
#define STORAGE_ENUM(x) .got_storage_enum = true
#define SAVE_CUSTOM .save_custom = true
#define LOAD_CUSTOM .load_custom = true
#define NOT_SAVED .not_saved = true

    static const struct test_data conf_key_test_data[] = {
        #include "conf.h"
    };

    for (size_t key = 0; key < N_CONFIG_OPTIONS; key++) {
        const ConfKeyInfo *info = &conf_key_info[key];
        const struct test_data *td = &conf_key_test_data[key];

        if (!td->got_value_type) {
            fprintf(stderr, "%s: no value type\n", td->name);
            nfails++;
        }

        if (td->got_default && info->subkey_type != CONF_TYPE_NONE) {
            fprintf(stderr, "%s: is a mapping but has a default\n", td->name);
            nfails++;
        }

        if ((td->got_default_int && info->value_type != CONF_TYPE_INT) ||
            (td->got_default_str &&
             (info->value_type != CONF_TYPE_STR &&
              info->value_type != CONF_TYPE_STR_AMBI &&
              info->value_type != CONF_TYPE_UTF8)) ||
            (td->got_default_bool && info->value_type != CONF_TYPE_BOOL)) {
            fprintf(stderr, "%s: default doesn't match type\n", td->name);
            nfails++;
        }

        if (td->got_storage_enum && info->value_type != CONF_TYPE_INT) {
            fprintf(stderr, "%s: has STORAGE_ENUM but isn't an int\n",
                    td->name);
            nfails++;
        }

        if (td->not_saved) {
            if (!td->got_default && info->subkey_type == CONF_TYPE_NONE) {
                fprintf(stderr, "%s: simple unsaved setting but has no "
                        "default\n", td->name);
                nfails++;
            }

            if (td->got_save_keyword) {
                fprintf(stderr, "%s: not saved but has SAVE_KEYWORD\n",
                        td->name);
                nfails++;
            }

            if (td->save_custom) {
                fprintf(stderr, "%s: not saved but has SAVE_CUSTOM\n",
                        td->name);
                nfails++;
            }

            if (td->load_custom) {
                fprintf(stderr, "%s: not saved but has LOAD_CUSTOM\n",
                        td->name);
                nfails++;
            }

            if (td->got_storage_enum) {
                fprintf(stderr, "%s: not saved but has STORAGE_ENUM\n",
                        td->name);
                nfails++;
            }

        } else {
            if (td->load_custom && td->save_custom) {
                if (td->got_save_keyword) {
                    fprintf(stderr, "%s: no automatic save or load but has "
                            "SAVE_KEYWORD\n", td->name);
                    nfails++;
                }

                if (td->got_storage_enum) {
                    fprintf(stderr, "%s: no automatic save or load but has "
                            "STORAGE_ENUM\n", td->name);
                    nfails++;
                }
            } else {
                if (!td->got_save_keyword) {
                    fprintf(stderr, "%s: missing SAVE_KEYWORD\n", td->name);
                    nfails++;
                }
            }
        }
    }
}

int main(void)
{
    test_conf_key_info();
    test_simple();
    return nfails != 0;
}
