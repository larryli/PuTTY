/*
 * stripctrl.c: a facility for stripping control characters out of a
 * data stream (defined as any multibyte character in the system
 * locale which is neither printable nor \n), using the standard C
 * library multibyte character facilities.
 */

#include <assert.h>
#include <locale.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "defs.h"
#include "misc.h"
#include "marshal.h"

#define SCC_BUFSIZE 64

typedef struct StripCtrlCharsImpl StripCtrlCharsImpl;
struct StripCtrlCharsImpl {
    mbstate_t mbs_in, mbs_out;

    bool permit_cr;
    wchar_t substitution;

    char buf[SCC_BUFSIZE];
    size_t buflen;

    BinarySink *bs_out;

    StripCtrlChars public;
};

static void stripctrl_BinarySink_write(
    BinarySink *bs, const void *vp, size_t len);

StripCtrlChars *stripctrl_new(
    BinarySink *bs_out, bool permit_cr, wchar_t substitution)
{
    StripCtrlCharsImpl *scc = snew(StripCtrlCharsImpl);
    memset(scc, 0, sizeof(StripCtrlCharsImpl)); /* zeroes mbstates */
    scc->bs_out = bs_out;
    scc->permit_cr = permit_cr;
    scc->substitution = substitution;
    BinarySink_INIT(&scc->public, stripctrl_BinarySink_write);
    return &scc->public;
}

void stripctrl_free(StripCtrlChars *sccpub)
{
    StripCtrlCharsImpl *scc =
        container_of(sccpub, StripCtrlCharsImpl, public);
    smemclr(scc, sizeof(StripCtrlCharsImpl));
    sfree(scc);
}

static inline void stripctrl_put_wc(StripCtrlCharsImpl *scc, wchar_t wc)
{
    if (wc == L'\n' || (wc == L'\r' && scc->permit_cr) || iswprint(wc)) {
        /* Printable character, or one we're going to let through anyway. */
    } else if (scc->substitution) {
        wc = scc->substitution;
    } else {
        /* No defined substitution, so don't write any output wchar_t. */
        return;
    }

    char outbuf[MB_LEN_MAX];
    size_t produced = wcrtomb(outbuf, wc, &scc->mbs_out);
    if (produced > 0)
        put_data(scc->bs_out, outbuf, produced);
}

static inline size_t stripctrl_try_consume(
    StripCtrlCharsImpl *scc, const char *p, size_t len)
{
    wchar_t wc;
    mbstate_t mbs_orig = scc->mbs_in;
    size_t consumed = mbrtowc(&wc, p, len, &scc->mbs_in);

    if (consumed == (size_t)-2) {
        /*
         * The buffer is too short to see the end of the multibyte
         * character that it appears to be starting with. We return 0
         * for 'no data consumed', restore the conversion state from
         * before consuming the partial character, and our caller will
         * come back when it has more data available.
         */
        scc->mbs_in = mbs_orig;
        return 0;
    }

    if (consumed == (size_t)-1) {
        /*
         * The buffer contains an illegal multibyte sequence. There's
         * no really good way to recover from this, so we'll just
         * reset our input state, consume a single byte without
         * emitting anything, and hope we can resynchronise to
         * _something_ sooner or later.
         */
        memset(&scc->mbs_in, 0, sizeof(scc->mbs_in));
        return 1;
    }

    if (consumed == 0) {
        /*
         * A zero wide character is encoded by the data, but mbrtowc
         * hasn't told us how many input bytes it takes. There isn't
         * really anything good we can do here, so we just advance by
         * one byte in the hope that that was the NUL.
         *
         * (If it wasn't - that is, if we're in a multibyte encoding
         * in which the terminator of a normal C string is encoded in
         * some way other than a single zero byte - then probably lots
         * of other things will have gone wrong before we get here!)
         */
        stripctrl_put_wc(scc, L'\0');
        return 1;
    }

    /*
     * Otherwise, this is the easy case: consumed > 0, and we've eaten
     * a valid multibyte character.
     */
    stripctrl_put_wc(scc, wc);
    return consumed;
}

static void stripctrl_BinarySink_write(
    BinarySink *bs, const void *vp, size_t len)
{
    StripCtrlChars *sccpub = BinarySink_DOWNCAST(bs, StripCtrlChars);
    StripCtrlCharsImpl *scc =
        container_of(sccpub, StripCtrlCharsImpl, public);
    const char *p = (const char *)vp;

    const char *previous_locale = setlocale(LC_CTYPE, NULL);
    setlocale(LC_CTYPE, "");

    /*
     * Deal with any partial multibyte character buffered from last
     * time.
     */
    while (scc->buflen > 0) {
        size_t to_copy = SCC_BUFSIZE - scc->buflen;
        if (to_copy > len)
            to_copy = len;

        memcpy(scc->buf + scc->buflen, p, to_copy);
        size_t consumed = stripctrl_try_consume(
            scc, scc->buf, scc->buflen + to_copy);

        if (consumed >= scc->buflen) {
            /*
             * We've consumed a multibyte character that includes all
             * the data buffered from last time. So we can clear our
             * buffer and move on to processing the main input string
             * in situ, having first discarded whatever initial
             * segment of it completed our previous character.
             */
            size_t consumed_from_main_string = consumed - scc->buflen;
            assert(consumed_from_main_string <= len);
            p += consumed_from_main_string;
            len -= consumed_from_main_string;
            scc->buflen = 0;
            break;
        }

        if (consumed == 0) {
            /*
             * If we didn't manage to consume anything, i.e. the whole
             * buffer contains an incomplete sequence, it had better
             * be because our entire input string _this_ time plus
             * whatever leftover data we had from _last_ time still
             * comes to less than SCC_BUFSIZE. In other words, we've
             * already copied all the new data on to the end of our
             * buffer, and it still hasn't helped. So increment buflen
             * to reflect the new data, and return.
             */
            assert(to_copy == len);
            scc->buflen += to_copy;
            goto out;
        }

        /*
         * Otherwise, we've somehow consumed _less_ data than we had
         * buffered, and yet we weren't able to consume that data in
         * the last call to this function. That sounds impossible, but
         * I can think of one situation in which it could happen: if
         * we had an incomplete MB sequence last time, and now more
         * data has arrived, it turns out to be an _illegal_ one, so
         * we consume one byte in the hope of resynchronising.
         *
         * Anyway, in this case we move the buffer up and go back
         * round this initial loop.
         */
        scc->buflen -= consumed;
        memmove(scc->buf, scc->buf + consumed, scc->buflen);
    }

    /*
     * Now charge along the main string.
     */
    while (len > 0) {
        size_t consumed = stripctrl_try_consume(scc, p, len);
        if (consumed == 0)
            break;
        assert(consumed <= len);
        p += consumed;
        len -= consumed;
    }

    /*
     * Any data remaining should be copied into our buffer, to keep
     * for next time.
     */
    assert(len <= SCC_BUFSIZE);
    memcpy(scc->buf, p, len);
    scc->buflen = len;

  out:
    setlocale(LC_CTYPE, previous_locale);
}

char *stripctrl_string_ptrlen(ptrlen str)
{
    strbuf *out = strbuf_new();
    StripCtrlChars *scc = stripctrl_new(BinarySink_UPCAST(out), false, L'?');
    put_datapl(scc, str);
    stripctrl_free(scc);
    return strbuf_to_str(out);
}

#ifdef STRIPCTRL_TEST

/*
gcc -DSTRIPCTRL_TEST -o scctest stripctrl.c marshal.c utils.c memory.c
*/

void out_of_memory(void) { fprintf(stderr, "out of memory\n"); abort(); }

void stripctrl_write(BinarySink *bs, const void *vdata, size_t len)
{
    const uint8_t *p = vdata;
    printf("[");
    for (size_t i = 0; i < len; i++)
        printf("%*s%02x", i?1:0, "", (unsigned)p[i]);
    printf("]");
}

void stripctrl_test(StripCtrlChars *scc, ptrlen pl)
{
    stripctrl_write(NULL, pl.ptr, pl.len);
    printf(" -> ");
    put_datapl(scc, pl);
    printf("\n");
}

int main(void)
{
    struct foo { BinarySink_IMPLEMENTATION; } foo;
    BinarySink_INIT(&foo, stripctrl_write);
    StripCtrlChars *scc = stripctrl_new(BinarySink_UPCAST(&foo));
    stripctrl_test(scc, PTRLEN_LITERAL("a\033[1mb"));
    stripctrl_test(scc, PTRLEN_LITERAL("a\xC2\x9B[1mb"));
    stripctrl_test(scc, PTRLEN_LITERAL("a\xC2\xC2[1mb"));
    stripctrl_test(scc, PTRLEN_LITERAL("\xC3"));
    stripctrl_test(scc, PTRLEN_LITERAL("\xA9"));
    stripctrl_test(scc, PTRLEN_LITERAL("\xE2\x80\x8F"));
    stripctrl_test(scc, PTRLEN_LITERAL("a\0b"));
    stripctrl_free(scc);
    return 0;
}

#endif /* STRIPCTRL_TEST */
