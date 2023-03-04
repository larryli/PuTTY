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

const struct BackendVtable *const backends[] = { NULL };

typedef struct Mock {
    Terminal *term;
    Conf *conf;
    struct unicode_data ucsdata[1];

    strbuf *context;

    TermWin tw;
} Mock;

static bool mock_setup_draw_ctx(TermWin *win) { return false; }
static void mock_draw_text(TermWin *win, int x, int y, wchar_t *text, int len,
                           unsigned long attrs, int lattrs, truecolour tc) {}
static void mock_draw_cursor(TermWin *win, int x, int y, wchar_t *text,
                             int len, unsigned long attrs, int lattrs,
                             truecolour tc) {}
static void mock_set_raw_mouse_mode(TermWin *win, bool enable) {}
static void mock_set_raw_mouse_mode_pointer(TermWin *win, bool enable) {}
static void mock_palette_set(TermWin *win, unsigned start, unsigned ncolours,
                             const rgb *colours) {}
static void mock_palette_get_overrides(TermWin *tw, Terminal *term) {}

static const TermWinVtable mock_termwin_vt = {
    .setup_draw_ctx = mock_setup_draw_ctx,
    .draw_text = mock_draw_text,
    .draw_cursor = mock_draw_cursor,
    .set_raw_mouse_mode = mock_set_raw_mouse_mode,
    .set_raw_mouse_mode_pointer = mock_set_raw_mouse_mode_pointer,
    .palette_set = mock_palette_set,
    .palette_get_overrides = mock_palette_get_overrides,
};

static Mock *mock_new(void)
{
    Mock *mk = snew(Mock);
    memset(mk, 0, sizeof(*mk));

    mk->conf = conf_new();
    do_defaults(NULL, mk->conf);

    init_ucs_generic(mk->conf, mk->ucsdata);
    mk->ucsdata->line_codepage = CP_ISO8859_1;

    mk->context = strbuf_new();

    mk->tw.vt = &mock_termwin_vt;

    return mk;
}

static void mock_free(Mock *mk)
{
    strbuf_free(mk->context);
    conf_free(mk->conf);
    term_free(mk->term);
    sfree(mk);
}

static void reset(Mock *mk)
{
    term_pwron(mk->term, true);
    term_size(mk->term, 24, 80, 0);
    term_set_trust_status(mk->term, false);
    strbuf_clear(mk->context);
}

#if 0

static void test_context(Mock *mk, const char *fmt, ...)
{
    strbuf_clear(mk->context);
    va_list ap;
    va_start(ap, fmt);
    put_fmtv(mk->context, fmt, ap);
    va_end(ap);
}

#endif

static void report_fail(Mock *mk, const char *file, int line,
                        const char *fmt, ...)
{
    printf("%s:%d", file, line);
    if (mk->context->len)
        printf(" (%s)", mk->context->s);
    printf(": ");
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

static inline void check_iequal(Mock *mk, const char *file, int line,
                                long long lhs, long long rhs)
{
    if (lhs != rhs)
        report_fail(mk, file, line, "%lld != %lld / %#llx != %#llx",
                    lhs, rhs, lhs, rhs);
}

#define IEQUAL(lhs, rhs) check_iequal(mk, __FILE__, __LINE__, lhs, rhs)

static inline void term_datapl(Terminal *term, ptrlen pl)
{
    term_data(term, pl.ptr, pl.len);
}

static struct termchar get_termchar(Terminal *term, int x, int y)
{
    termline *tl = term_get_line(term, y);
    termchar tc;
    if (0 <= x && x < tl->cols)
        tc = tl->chars[x];
    else
        tc = term->erase_char;
    term_release_line(tl);
    return tc;
}

static void test_hello_world(Mock *mk)
{
    /* A trivial test just to kick off this test framework */
    mk->ucsdata->line_codepage = CP_ISO8859_1;

    reset(mk);
    term_datapl(mk->term, PTRLEN_LITERAL("hello, world"));
    IEQUAL(mk->term->curs.x, 12);
    IEQUAL(mk->term->curs.y, 0);
    IEQUAL(get_termchar(mk->term, 0, 0).chr, CSET_ASCII | 'h');
    IEQUAL(get_termchar(mk->term, 1, 0).chr, CSET_ASCII | 'e');
    IEQUAL(get_termchar(mk->term, 2, 0).chr, CSET_ASCII | 'l');
    IEQUAL(get_termchar(mk->term, 3, 0).chr, CSET_ASCII | 'l');
    IEQUAL(get_termchar(mk->term, 4, 0).chr, CSET_ASCII | 'o');
    IEQUAL(get_termchar(mk->term, 5, 0).chr, CSET_ASCII | ',');
    IEQUAL(get_termchar(mk->term, 6, 0).chr, CSET_ASCII | ' ');
    IEQUAL(get_termchar(mk->term, 7, 0).chr, CSET_ASCII | 'w');
    IEQUAL(get_termchar(mk->term, 8, 0).chr, CSET_ASCII | 'o');
    IEQUAL(get_termchar(mk->term, 9, 0).chr, CSET_ASCII | 'r');
    IEQUAL(get_termchar(mk->term, 10, 0).chr, CSET_ASCII | 'l');
    IEQUAL(get_termchar(mk->term, 11, 0).chr, CSET_ASCII | 'd');
}

int main(void)
{
    Mock *mk = mock_new();
    mk->term = term_init(mk->conf, mk->ucsdata, &mk->tw);

    test_hello_world(mk);

    mock_free(mk);
    return 0;
}
