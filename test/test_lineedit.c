#include "putty.h"
#include "terminal.h"

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

const char *const appname = "test_lineedit";

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

struct SpecialRecord {
    SessionSpecialCode code;
    int arg;
};

typedef struct Mock {
    Terminal *term;
    Ldisc *ldisc;
    Conf *conf;
    struct unicode_data ucsdata[1];

    bool echo, edit;
    strbuf *to_terminal, *to_backend;

    struct SpecialRecord *specials;
    size_t nspecials, specialsize;

    strbuf *context;                   /* for printing in failed tests */

    bool any_test_failed;

    TermWin tw;
    Seat seat;
    Backend backend;
} Mock;

static size_t mock_output(Seat *seat, SeatOutputType type,
                          const void *data, size_t len)
{
    Mock *mk = container_of(seat, Mock, seat);
    put_data(mk->to_terminal, data, len);
    return 0;
}

static void mock_send(Backend *be, const char *buf, size_t len)
{
    Mock *mk = container_of(be, Mock, backend);
    put_data(mk->to_backend, buf, len);
}

static void mock_special(Backend *be, SessionSpecialCode code, int arg)
{
    Mock *mk = container_of(be, Mock, backend);
    sgrowarray(mk->specials, mk->specialsize, mk->nspecials);
    mk->specials[mk->nspecials].code = code;
    mk->specials[mk->nspecials].arg = arg;
    mk->nspecials++;
}

static bool mock_ldisc_option_state(Backend *be, int option)
{
    Mock *mk = container_of(be, Mock, backend);
    switch (option) {
      case LD_ECHO:
        return mk->echo;
      case LD_EDIT:
        return mk->edit;
      default:
        unreachable("bad ldisc option");
    }
}

static void mock_provide_ldisc(Backend *be, Ldisc *ldisc)
{
    Mock *mk = container_of(be, Mock, backend);
    mk->ldisc = ldisc;
}

static bool mock_sendok(Backend *be)
{
    Mock *mk = container_of(be, Mock, backend);
    (void)mk;
    /* FIXME: perhaps make this settable, to test the input_queue system? */
    return true;
}

static void mock_set_raw_mouse_mode(TermWin *win, bool enable) {}
static void mock_palette_get_overrides(TermWin *tw, Terminal *term) {}

static const TermWinVtable mock_termwin_vt = {
    .set_raw_mouse_mode = mock_set_raw_mouse_mode,
    .palette_get_overrides = mock_palette_get_overrides,
};

static const SeatVtable mock_seat_vt = {
    .output = mock_output,
    .echoedit_update = nullseat_echoedit_update,
};

static const BackendVtable mock_backend_vt = {
    .sendok = mock_sendok,
    .send = mock_send,
    .special = mock_special,
    .ldisc_option_state = mock_ldisc_option_state,
    .provide_ldisc = mock_provide_ldisc,
    .id = "mock",
};

static Mock *mock_new(void)
{
    Mock *mk = snew(Mock);
    memset(mk, 0, sizeof(*mk));

    mk->conf = conf_new();
    do_defaults(NULL, mk->conf);

    init_ucs_generic(mk->conf, mk->ucsdata);
    mk->ucsdata->line_codepage = CP_437;

    mk->context = strbuf_new();
    mk->to_terminal = strbuf_new();
    mk->to_backend = strbuf_new();

    mk->tw.vt = &mock_termwin_vt;
    mk->seat.vt = &mock_seat_vt;
    mk->backend.vt = &mock_backend_vt;

    return mk;
}

static void mock_free(Mock *mk)
{
    strbuf_free(mk->context);
    strbuf_free(mk->to_terminal);
    strbuf_free(mk->to_backend);
    conf_free(mk->conf);
    term_free(mk->term);
    sfree(mk->specials);
    sfree(mk);
}

static void reset(Mock *mk)
{
    strbuf_clear(mk->context);
    strbuf_clear(mk->to_terminal);
    strbuf_clear(mk->to_backend);
    mk->nspecials = 0;
}

static void test_context(Mock *mk, const char *fmt, ...)
{
    strbuf_clear(mk->context);
    va_list ap;
    va_start(ap, fmt);
    put_fmtv(mk->context, fmt, ap);
    va_end(ap);
}

static void print_context(Mock *mk, const char *file, int line)
{
    printf("%s:%d", file, line);
    if (mk->context->len)
        printf(" (%s)", mk->context->s);
    printf(": ");
}

#define EXPECT(mk, what, ...)                                   \
    expect_ ## what(mk, __FILE__, __LINE__, __VA_ARGS__)

static void expect_backend(Mock *mk, const char *file, int line,
                           ptrlen expected)
{
    ptrlen actual = ptrlen_from_strbuf(mk->to_backend);
    if (!ptrlen_eq_ptrlen(expected, actual)) {
        print_context(mk, file, line);
        printf("expected backend output \"");
        write_c_string_literal(stdout, expected);
        printf("\", got \"");
        write_c_string_literal(stdout, actual);
        printf("\"\n");
        mk->any_test_failed = true;
    }
}

static void expect_terminal(Mock *mk, const char *file, int line,
                            ptrlen expected)
{
    ptrlen actual = ptrlen_from_strbuf(mk->to_terminal);
    if (!ptrlen_eq_ptrlen(expected, actual)) {
        print_context(mk, file, line);
        printf("expected terminal output \"");
        write_c_string_literal(stdout, expected);
        printf("\", got \"");
        write_c_string_literal(stdout, actual);
        printf("\"\n");
        mk->any_test_failed = true;
    }
}

static void expect_specials(Mock *mk, const char *file, int line,
                            size_t nspecials, ...)
{
    va_list ap;

    static const char *const special_names[] = {
#define SPECIAL(x) #x,
#include "specials.h"
#undef SPECIAL
    };

    bool match;
    if (nspecials != mk->nspecials) {
        match = false;
    } else {
        match = true;
        va_start(ap, nspecials);
        for (size_t i = 0; i < nspecials; i++) {
            SessionSpecialCode code = va_arg(ap, SessionSpecialCode);
            int arg = va_arg(ap, int);
            if (code != mk->specials[i].code || arg != mk->specials[i].arg)
                match = false;
        }
        va_end(ap);
    }

    if (!match) {
        print_context(mk, file, line);
        printf("expected specials [");
        va_start(ap, nspecials);
        for (size_t i = 0; i < nspecials; i++) {
            SessionSpecialCode code = va_arg(ap, SessionSpecialCode);
            int arg = va_arg(ap, int);
            printf(" %s.%d", special_names[code], arg);
        }
        va_end(ap);
        printf(" ], got [");
        for (size_t i = 0; i < mk->nspecials; i++) {
            printf(" %s.%d", special_names[mk->specials[i].code],
                   mk->specials[i].arg);
        }
        printf(" ]\n");
        mk->any_test_failed = true;
    }
}

static void test_noedit(Mock *mk)
{
    mk->edit = false;
    mk->echo = false;

    /*
     * In non-echo and non-edit mode, the channel is 8-bit clean
     */
    for (unsigned c = 0; c < 256; c++) {
        char buf[1];

        test_context(mk, "c=%02x", c);
        buf[0] = c;
        ldisc_send(mk->ldisc, buf, 1, false);
        EXPECT(mk, backend, make_ptrlen(buf, 1));
        reset(mk);
    }
    /* ... regardless of the 'interactive' flag */
    for (unsigned c = 0; c < 256; c++) {
        char buf[1];

        test_context(mk, "c=%02x", c);
        buf[0] = c;
        ldisc_send(mk->ldisc, buf, 1, true);
        EXPECT(mk, backend, make_ptrlen(buf, 1));
        reset(mk);
    }
    /* ... and any nonzero character does the same thing even if sent
     * with the magic -1 length flag */
    for (unsigned c = 1; c < 256; c++) {
        char buf[2];

        test_context(mk, "c=%02x", c);
        buf[0] = c;
        buf[1] = '\0';
        ldisc_send(mk->ldisc, buf, -1, true);
        EXPECT(mk, backend, make_ptrlen(buf, 1));
        reset(mk);
    }

    /*
     * Test the special-character cases for Telnet.
     */
    conf_set_int(mk->conf, CONF_protocol, PROT_TELNET);
    conf_set_bool(mk->conf, CONF_telnet_newline, false);
    conf_set_bool(mk->conf, CONF_telnet_keyboard, false);
    ldisc_configure(mk->ldisc, mk->conf);

    /* Without telnet_newline or telnet_keyboard, these all do the
     * normal thing */
    ldisc_send(mk->ldisc, "\x0D", -1, true);
    EXPECT(mk, backend, PTRLEN_LITERAL("\x0D"));
    reset(mk);
    ldisc_send(mk->ldisc, "\x08", -1, true);
    EXPECT(mk, backend, PTRLEN_LITERAL("\x08"));
    reset(mk);
    ldisc_send(mk->ldisc, "\x7F", -1, true);
    EXPECT(mk, backend, PTRLEN_LITERAL("\x7F"));
    reset(mk);
    ldisc_send(mk->ldisc, "\x03", -1, true);
    EXPECT(mk, backend, PTRLEN_LITERAL("\x03"));
    reset(mk);
    ldisc_send(mk->ldisc, "\x1A", -1, true);
    EXPECT(mk, backend, PTRLEN_LITERAL("\x1A"));
    reset(mk);

    /* telnet_newline controls translation of CR into SS_EOL */
    conf_set_bool(mk->conf, CONF_telnet_newline, true);
    ldisc_configure(mk->ldisc, mk->conf);
    ldisc_send(mk->ldisc, "\x0D", -1, true);
    EXPECT(mk, specials, 1, SS_EOL, 0);
    reset(mk);

    /* And telnet_keyboard controls the others */
    conf_set_bool(mk->conf, CONF_telnet_newline, false);
    conf_set_bool(mk->conf, CONF_telnet_keyboard, true);
    ldisc_configure(mk->ldisc, mk->conf);
    ldisc_send(mk->ldisc, "\x08", -1, true);
    EXPECT(mk, specials, 1, SS_EC, 0);
    reset(mk);
    ldisc_send(mk->ldisc, "\x7F", -1, true);
    EXPECT(mk, specials, 1, SS_EC, 0);
    reset(mk);
    ldisc_send(mk->ldisc, "\x03", -1, true);
    EXPECT(mk, specials, 1, SS_IP, 0);
    reset(mk);
    ldisc_send(mk->ldisc, "\x1A", -1, true);
    EXPECT(mk, specials, 1, SS_SUSP, 0);
    reset(mk);

    /*
     * In echo-but-no-edit mode, we also expect that every character
     * is echoed back to the display as a side effect, including when
     * it's sent as a special -1 keystroke.
     *
     * This state only comes up in Telnet, because that has protocol
     * options to independently configure echo and edit. Telnet is
     * also the most complicated of the protocols because of the above
     * special cases, so we stay in Telnet mode for this test.
     */
    mk->echo = true;
    for (unsigned c = 0; c < 256; c++) {
        char buf[1];

        test_context(mk, "c=%02x", c);
        buf[0] = c;
        ldisc_send(mk->ldisc, buf, 1, false);
        EXPECT(mk, terminal, make_ptrlen(buf, 1));
        reset(mk);
    }
    for (unsigned c = 1; c < 256; c++) {
        char buf[2];

        test_context(mk, "c=%02x", c);
        buf[0] = c;
        buf[1] = '\0';
        ldisc_send(mk->ldisc, buf, -1, true);
        EXPECT(mk, terminal, make_ptrlen(buf, 1));
        reset(mk);
    }

    do_defaults(NULL, mk->conf);
    ldisc_configure(mk->ldisc, mk->conf);
}

static void test_edit(Mock *mk, bool echo)
{
    static const char *const ctls = "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_";

    mk->edit = true;
    mk->echo = echo;

#define EXPECT_TERMINAL(mk, val) do {                           \
        if (!echo) EXPECT(mk, terminal, PTRLEN_LITERAL(""));    \
        else EXPECT(mk, terminal, val);                         \
    } while (0)

    /* ASCII printing characters all print when entered, but don't go
     * to the terminal until Return is pressed */
    for (unsigned c = 0x20; c < 0x7F; c++) {
        char buf[3];

        test_context(mk, "c=%02x", c);
        buf[0] = c;
        ldisc_send(mk->ldisc, buf, 1, false);
        EXPECT(mk, backend, PTRLEN_LITERAL(""));
        EXPECT_TERMINAL(mk, make_ptrlen(buf, 1));
        ldisc_send(mk->ldisc, "\015", 1, false);
        buf[1] = '\015';
        buf[2] = '\012';
        EXPECT(mk, backend, make_ptrlen(buf, 3));
        EXPECT_TERMINAL(mk, make_ptrlen(buf, 3));
        reset(mk);
    }

    /* C0 control characters mostly show up as ^X or similar */
    for (unsigned c = 0; c < 0x1F; c++) {
        char backbuf[3];
        char termbuf[4];

        switch (ctls[c]) {
          case 'D': continue;          /* ^D sends EOF */
          case 'M': continue;          /* ^M is Return */
          case 'R': continue;          /* ^R redisplays line */
          case 'U': continue;          /* ^U deletes the line */
          case 'V': continue;          /* ^V makes the next char literal */
          case 'W': continue;          /* ^W deletes a word */
            /*
             * ^H / ^? are not included here. Those are treated
             * literally if sent as plain input bytes. Only sending
             * them as special via length==-1 causes them to act as
             * backspace, which I think was simply because there _is_
             * a dedicated key that can do that function, so there's
             * no need to also eat the Ctrl+thing combo.
             */

            /*
             * Also, ^C, ^Z and ^\ self-insert (when not in Telnet
             * mode) but have the side effect of erasing the line
             * buffer so far. In this loop, that doesn't show up,
             * because the line buffer is empty already. However, I
             * don't test that, because it's silly, and probably
             * doesn't want to keep happening!
             */
        }

        test_context(mk, "c=%02x", c);
        backbuf[0] = c;
        ldisc_send(mk->ldisc, backbuf, 1, false);
        EXPECT(mk, backend, PTRLEN_LITERAL(""));
        termbuf[0] = '^';
        termbuf[1] = ctls[c];
        EXPECT_TERMINAL(mk, make_ptrlen(termbuf, 2));
        ldisc_send(mk->ldisc, "\015", 1, false);
        backbuf[1] = '\015';
        backbuf[2] = '\012';
        EXPECT(mk, backend, make_ptrlen(backbuf, 3));
        termbuf[2] = '\015';
        termbuf[3] = '\012';
        EXPECT_TERMINAL(mk, make_ptrlen(termbuf, 4));
        reset(mk);
    }

    /* Prefixed with ^V, the same is true of _all_ C0 controls */
    for (unsigned c = 0; c < 0x1F; c++) {
        char backbuf[3];
        char termbuf[4];

        test_context(mk, "c=%02x", c);
        backbuf[0] = 'V' & 0x1F;
        ldisc_send(mk->ldisc, backbuf, 1, false);
        backbuf[0] = c;
        ldisc_send(mk->ldisc, backbuf, 1, false);
        EXPECT(mk, backend, PTRLEN_LITERAL(""));
        termbuf[0] = '^';
        termbuf[1] = ctls[c];
        EXPECT_TERMINAL(mk, make_ptrlen(termbuf, 2));
        ldisc_send(mk->ldisc, "\015", 1, false);
        backbuf[1] = '\015';
        backbuf[2] = '\012';
        EXPECT(mk, backend, make_ptrlen(backbuf, 3));
        termbuf[2] = '\015';
        termbuf[3] = '\012';
        EXPECT_TERMINAL(mk, make_ptrlen(termbuf, 4));
        reset(mk);
    }

    /* Deleting an ASCII character sends a single BSB and deletes just
     * that byte from the buffer */
    ldisc_send(mk->ldisc, "ab", 2, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("ab"));
    ldisc_send(mk->ldisc, "\x08", -1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("ab\x08 \x08"));
    ldisc_send(mk->ldisc, "\x0D", -1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL("a\x0D\x0A"));
    reset(mk);

    /* Deleting a character written as a ^X code sends two BSBs to
     * wipe out the two-character display sequence */
    ldisc_send(mk->ldisc, "a\x02", 2, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("a^B"));
    ldisc_send(mk->ldisc, "\x7F", -1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("a^B\x08 \x08\x08 \x08"));
    ldisc_send(mk->ldisc, "\x0D", -1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL("a\x0D\x0A"));
    reset(mk);

    /* ^D sends the line editing buffer without a trailing Return, if
     * it's non-empty */
    ldisc_send(mk->ldisc, "abc\x04", 4, false);
    EXPECT(mk, backend, PTRLEN_LITERAL("abc"));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("abc"));
    ldisc_send(mk->ldisc, "\x0D", -1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL("abc\x0D\x0A"));
    reset(mk);

    /* But if the buffer is empty, ^D sends SS_EOF */
    ldisc_send(mk->ldisc, "\x04", 1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL(""));
    EXPECT(mk, specials, 1, SS_EOF, 0);
    reset(mk);

    /* ^R redraws the current line, after printing "^R" at the end of
     * the previous attempt to make it clear that that's what
     * happened */
    ldisc_send(mk->ldisc, "a\x01", 2, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("a^A"));
    ldisc_send(mk->ldisc, "\x12", 1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("a^A^R\x0D\x0A" "a^A"));
    ldisc_send(mk->ldisc, "\x0D", -1, false);
    reset(mk);

    /* ^U deletes the whole line */
    ldisc_send(mk->ldisc, "a b c", 5, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("a b c"));
    ldisc_send(mk->ldisc, "\x15", 1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(
        mk, PTRLEN_LITERAL(
            "a b c\x08 \x08\x08 \x08\x08 \x08\x08 \x08\x08 \x08"));
    ldisc_send(mk->ldisc, "\x0D", -1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL("\x0D\x0A"));
    EXPECT_TERMINAL(
        mk, PTRLEN_LITERAL(
            "a b c\x08 \x08\x08 \x08\x08 \x08\x08 \x08\x08 \x08\x0D\x0A"));
    reset(mk);
    /* And it still knows that a control character written as ^X takes
     * two BSBs to delete */
    ldisc_send(mk->ldisc, "a\x02" "c", 3, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("a^Bc"));
    ldisc_send(mk->ldisc, "\x15", 1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(
        mk, PTRLEN_LITERAL("a^Bc\x08 \x08\x08 \x08\x08 \x08\x08 \x08"));
    ldisc_send(mk->ldisc, "\x0D", -1, false);
    reset(mk);

    /* ^W deletes a word, which means that it deletes to the most
     * recent boundary with a space on the left and a nonspace on the
     * right. (Or the beginning of the string, whichever comes first.) */
    ldisc_send(mk->ldisc, "hello, world\x17", 13, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(
        mk, PTRLEN_LITERAL(
            "hello, world\x08 \x08\x08 \x08\x08 \x08\x08 \x08\x08 \x08"));
    ldisc_send(mk->ldisc, "\x0D", 1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL("hello, \x0D\x0A"));
    reset(mk);
    ldisc_send(mk->ldisc, "hello, world \x17", 14, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(
        mk, PTRLEN_LITERAL(
            "hello, world "
            "\x08 \x08\x08 \x08\x08 \x08\x08 \x08\x08 \x08\x08 \x08"));
    ldisc_send(mk->ldisc, "\x0D", 1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL("hello, \x0D\x0A"));
    reset(mk);
    ldisc_send(mk->ldisc, " hello \x17", 8, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(
        mk, PTRLEN_LITERAL(
            " hello \x08 \x08\x08 \x08\x08 \x08\x08 \x08\x08 \x08\x08 \x08"));
    ldisc_send(mk->ldisc, "\x0D", 1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(" \x0D\x0A"));
    reset(mk);
    ldisc_send(mk->ldisc, "hello \x17", 7, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(
        mk, PTRLEN_LITERAL(
            "hello \x08 \x08\x08 \x08\x08 \x08\x08 \x08\x08 \x08\x08 \x08"));
    ldisc_send(mk->ldisc, "\x0D", 1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL("\x0D\x0A"));
    reset(mk);
    /* And this too knows that a control character written as ^X takes
     * two BSBs to delete */
    ldisc_send(mk->ldisc, "a\x02" "c", 3, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("a^Bc"));
    ldisc_send(mk->ldisc, "\x17", 1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(
        mk, PTRLEN_LITERAL("a^Bc\x08 \x08\x08 \x08\x08 \x08\x08 \x08"));
    ldisc_send(mk->ldisc, "\x0D", -1, false);
    reset(mk);

    /* Test handling of ^C and friends in non-telnet_keyboard mode */
    ldisc_send(mk->ldisc, "abc\x03", 4, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("abc\x08 \x08\x08 \x08\x08 \x08^C"));
    EXPECT(mk, specials, 1, SS_EL, 0);
    ldisc_send(mk->ldisc, "\x0D", -1, false);
    reset(mk);
    ldisc_send(mk->ldisc, "abc\x1a", 4, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("abc\x08 \x08\x08 \x08\x08 \x08^Z"));
    EXPECT(mk, specials, 1, SS_EL, 0);
    ldisc_send(mk->ldisc, "\x0D", -1, false);
    reset(mk);
    ldisc_send(mk->ldisc, "abc\x1c", 4, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("abc\x08 \x08\x08 \x08\x08 \x08^\\"));
    EXPECT(mk, specials, 1, SS_EL, 0);
    ldisc_send(mk->ldisc, "\x0D", -1, false);
    reset(mk);

    /* And in telnet_keyboard mode */
    conf_set_bool(mk->conf, CONF_telnet_keyboard, true);
    ldisc_configure(mk->ldisc, mk->conf);

    /* FIXME: should we _really_ be sending EL before each of these? */
    ldisc_send(mk->ldisc, "abc\x03", 4, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("abc\x08 \x08\x08 \x08\x08 \x08"));
    EXPECT(mk, specials, 2, SS_EL, 0, SS_IP, 0);
    ldisc_send(mk->ldisc, "\x0D", -1, false);
    reset(mk);
    ldisc_send(mk->ldisc, "abc\x1a", 4, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("abc\x08 \x08\x08 \x08\x08 \x08"));
    EXPECT(mk, specials, 2, SS_EL, 0, SS_SUSP, 0);
    ldisc_send(mk->ldisc, "\x0D", -1, false);
    reset(mk);
    ldisc_send(mk->ldisc, "abc\x1c", 4, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("abc\x08 \x08\x08 \x08\x08 \x08"));
    EXPECT(mk, specials, 2, SS_EL, 0, SS_ABORT, 0);
    ldisc_send(mk->ldisc, "\x0D", -1, false);
    reset(mk);

    conf_set_bool(mk->conf, CONF_telnet_keyboard, false);
    ldisc_configure(mk->ldisc, mk->conf);

    /* Test UTF-8 characters of various lengths and ensure deleting
     * one deletes the whole character from the buffer (by pressing
     * Return and seeing what gets sent) but sends a number of BSBs
     * corresponding to the character's terminal width */
    mk->term->utf = true;

    ldisc_send(mk->ldisc, "\xC2\xA0\xC2\xA1", 4, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("\xC2\xA0\xC2\xA1"));
    ldisc_send(mk->ldisc, "\x08", -1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("\xC2\xA0\xC2\xA1\x08 \x08"));
    ldisc_send(mk->ldisc, "\x0D", -1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL("\xC2\xA0\x0D\x0A"));
    reset(mk);

    ldisc_send(mk->ldisc, "\xE2\xA0\x80\xE2\xA0\x81", 6, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("\xE2\xA0\x80\xE2\xA0\x81"));
    ldisc_send(mk->ldisc, "\x08", -1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("\xE2\xA0\x80\xE2\xA0\x81\x08 \x08"));
    ldisc_send(mk->ldisc, "\x0D", -1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL("\xE2\xA0\x80\x0D\x0A"));
    reset(mk);

    ldisc_send(mk->ldisc, "\xF0\x90\x80\x80\xF0\x90\x80\x81", 8, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("\xF0\x90\x80\x80\xF0\x90\x80\x81"));
    ldisc_send(mk->ldisc, "\x08", -1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("\xF0\x90\x80\x80\xF0\x90\x80\x81"
                                       "\x08 \x08"));
    ldisc_send(mk->ldisc, "\x0D", -1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL("\xF0\x90\x80\x80\x0D\x0A"));
    reset(mk);

    /* Double-width characters (Hangul, as it happens) */
    ldisc_send(mk->ldisc, "\xEA\xB0\x80\xEA\xB0\x81", 6, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("\xEA\xB0\x80\xEA\xB0\x81"));
    ldisc_send(mk->ldisc, "\x08", -1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("\xEA\xB0\x80\xEA\xB0\x81"
                                       "\x08 \x08\x08 \x08"));
    ldisc_send(mk->ldisc, "\x0D", -1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL("\xEA\xB0\x80\x0D\x0A"));
    reset(mk);

    /* Zero-width characters */
    ldisc_send(mk->ldisc, "\xE2\x80\x8B\xE2\x80\x8B", 6, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("\xE2\x80\x8B\xE2\x80\x8B"));
    ldisc_send(mk->ldisc, "\x08", -1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("\xE2\x80\x8B\xE2\x80\x8B"));
    ldisc_send(mk->ldisc, "\x0D", -1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL("\xE2\x80\x8B\x0D\x0A"));
    reset(mk);

    /* And reset back to non-UTF-8 mode and expect high-bit-set bytes
     * to be treated individually, as characters in a single-byte
     * charset. (In our case, given the test config, that will be
     * CP437, but it makes no difference to the editing behaviour.) */
    mk->term->utf = false;
    ldisc_send(mk->ldisc, "\xC2\xA0\xC2\xA1", 4, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("\xC2\xA0\xC2\xA1"));
    ldisc_send(mk->ldisc, "\x08", -1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL(""));
    EXPECT_TERMINAL(mk, PTRLEN_LITERAL("\xC2\xA0\xC2\xA1\x08 \x08"));
    ldisc_send(mk->ldisc, "\x0D", -1, false);
    EXPECT(mk, backend, PTRLEN_LITERAL("\xC2\xA0\xC2\x0D\x0A"));
    reset(mk);

    /* Make sure we flush all the terminal contents at the end of this
     * function */
    ldisc_send(mk->ldisc, "\x0D", 1, false);
    reset(mk);

#undef EXPECT_TERMINAL

}

const struct BackendVtable *const backends[] = { &mock_backend_vt, NULL };

int main(void)
{
    Mock *mk = mock_new();
    mk->term = term_init(mk->conf, mk->ucsdata, &mk->tw);
    Ldisc *ldisc = ldisc_create(mk->conf, mk->term, &mk->backend, &mk->seat);
    term_size(mk->term, 80, 24, 0);

    test_noedit(mk);
    test_edit(mk, true);
    test_edit(mk, false);

    ldisc_free(ldisc);

    bool failed = mk->any_test_failed;
    mock_free(mk);

    if (failed) {
        printf("Test suite FAILED!\n");
        return 1;
    } else {
        printf("Test suite passed\n");
        return 0;
    }
}
