#include <stdio.h>
#include <string.h>
#include "misc.h"

typedef uint32_t uchar;
typedef int cclass_t;

/* A local uchar-oriented analogue of strbuf */
typedef struct ucharbuf {
    uchar *buf;
    size_t len, size;
} ucharbuf;

static ucharbuf *ucharbuf_new(void)
{
    ucharbuf *ub = snew(ucharbuf);
    ub->buf = NULL;
    ub->len = ub->size = 0;
    return ub;
}

static void ucharbuf_append(ucharbuf *ub, uchar c)
{
    /* Use the _nm variant because this is used for passphrases */
    sgrowarray_nm(ub->buf, ub->size, ub->len);
    ub->buf[ub->len++] = c;
}

static void ucharbuf_free(ucharbuf *ub)
{
    if (ub->buf) {
        memset(ub->buf, 0, ub->size * sizeof(*ub->buf));
        sfree(ub->buf);
    }
    sfree(ub);
}

/*
 * Constants relating to the arithmetic decomposition mapping of
 * Hangul to jamo, from section 3.12 of Unicode 15.0.0. The following
 * constant names match those in the spec.
 */
enum {
    SBase = 0xAC00,  /* base index for precomposed Hangul */
    LBase = 0x1100,  /* base index for L (leading consonant) jamo */
    VBase = 0x1161,  /* base index for V (vowel) jamo */
    TBase = 0x11A7,  /* base index for T (trailing consonant) jamo */
    LCount = 19,     /* number of L jamo */
    VCount = 21,     /* number of V jamo */
    TCount = 28,     /* number of T jamo, including not having one at all */
    NCount = VCount * TCount,   /* number of Hangul for each L jamo */
    SCount = LCount * NCount,   /* number of Hangul in total */
};

static cclass_t combining_class(uchar c)
{
    struct range {
        uchar start, end;
        cclass_t cclass;
    };
    static const struct range ranges[] = {
        #include "unicode/combining_classes.h"
    };

    const struct range *start = ranges, *end = start + lenof(ranges);

    while (end > start) {
        const struct range *curr = start + (end-start) / 2;
        if (c < curr->start)
            end = curr;
        else if (c > curr->end)
            start = curr + 1;
        else
            return curr->cclass;
    }

    return 0;
};

static unsigned decompose_char(uchar c, uchar *out)
{
    struct decomp {
        uchar composed, dec0, dec1;
    };
    static const struct decomp decomps[] = {
        #include "unicode/canonical_decomp.h"
    };

    if (c - SBase < SCount) {
        /* Arithmetically decompose a Hangul character into jamo */
        uchar SIndex = c - SBase;
        uchar LIndex = SIndex / NCount;
        uchar VIndex = SIndex % NCount / TCount;
        uchar TIndex = SIndex % TCount;

        unsigned n = 0;
        out[n++] = LBase + LIndex;
        out[n++] = VBase + VIndex;
        if (TIndex)
            out[n++] = TBase + TIndex;
        return n;
    }

    const struct decomp *start = decomps, *end = start + lenof(decomps);

    while (end > start) {
        const struct decomp *curr = start + (end-start) / 2;
        if (c < curr->composed)
            end = curr;
        else if (c > curr->composed)
            start = curr + 1;
        else {
            out[0] = curr->dec0;
            if (curr->dec1) {
                out[1] = curr->dec1;
                return 2;
            } else {
                return 1;
            }
        }
    }

    return 0;
};

static uchar compose_chars(uchar S, uchar C)
{
    struct comp {
        uchar dec0, dec1, composed;
    };
    static const struct comp comps[] = {
        #include "unicode/canonical_comp.h"
    };

    if (S - LBase < LCount && C - VBase < VCount) {
        /* Arithmetically compose an L and V jamo into a Hangul LV
         * character */
        return SBase + (S - LBase) * NCount + (C - VBase) * TCount;
    }

    if (S - SBase < SCount && (S - SBase) % TCount == 0 &&
        C - TBase < TCount) {
        /* Arithmetically compose an LV Hangul character and a T jamo
         * into a Hangul LVT character */
        return S + C - TBase;
    }

    const struct comp *start = comps, *end = start + lenof(comps);

    while (end > start) {
        const struct comp *curr = start + (end-start) / 2;
        if (S < curr->dec0)
            end = curr;
        else if (S > curr->dec0)
            start = curr + 1;
        else if (C < curr->dec1)
            end = curr;
        else if (C > curr->dec1)
            start = curr + 1;
        else
            return curr->composed;
    }

    return 0;
};

/*
 * Recursively decompose a sequence of Unicode characters. The output
 * is written to 'out', as a sequence of native-byte-order uchar.
 */
static void recursively_decompose(const uchar *str, size_t len, ucharbuf *out)
{
    uchar decomposed[3];

    while (len-- > 0) {
        uchar c = *str++;
        unsigned n = decompose_char(c, decomposed);
        if (n == 0) {
            /* This character is indecomposable */
            ucharbuf_append(out, c);
        } else {
            /* This character has been decomposed into up to 3
             * characters, so we must now recursively decompose those */
            recursively_decompose(decomposed, n, out);
        }
    }
}

/*
 * Reorder combining marks according to the Canonical Ordering
 * Algorithm (definition D109 in Unicode 15.0.0 section 3.11).
 *
 * The algorithm is phrased mechanistically, but the essence is: among
 * any contiguous sequence of combining marks (that is, characters
 * with cclass > 0), sort them by their cclass - but _stably_, i.e.
 * breaking ties in cclass by preserving the original order of the
 * characters in question.
 */
static void canonical_ordering(uchar *str, size_t len)
{
    for (size_t i = 1; i < len; i++) {
        cclass_t cclass = combining_class(str[i]);
        if (cclass == 0)
            continue;

        size_t j = i;
        while (j > 0 && combining_class(str[j-1]) > cclass) {
            uchar tmp = str[j-1];
            str[j-1] = str[j];
            str[j] = tmp;

            j--;
        }
    }
}

/*
 * Canonically recompose characters according to the Canonical
 * Composition Algorithm (definition D117 in Unicode 15.0.0 section
 * 3.11).
 */
static size_t canonical_composition(uchar *str, size_t len)
{
    const uchar *in = str;
    uchar *out = str;
    uchar *last_starter = NULL;
    cclass_t highest_cclass_between = -1;

    while (len > 0) {
        len--;
        uchar c = *in++;
        cclass_t cclass = combining_class(c);

        if (last_starter && highest_cclass_between < cclass) {
            uchar composed = compose_chars(*last_starter, c);
            if (composed) {
                *last_starter = composed;
                continue;
            }
        }

        if (cclass == 0) {
            last_starter = out;
            highest_cclass_between = -1;
        } else if (cclass > highest_cclass_between) {
            highest_cclass_between = cclass;
        }

        *out++ = c;
    }

    return out - str;
}

/*
 * Render a string into NFD.
 */
static ucharbuf *nfd(ucharbuf *input)
{
    ucharbuf *output = ucharbuf_new();

    /*
     * Definition D118 in Unicode 15.0.0 section 3.11, referring to
     * D68 in section 3.7: recursively decompose characters, then
     * reorder combining marks.
     */
    recursively_decompose(input->buf, input->len, output);
    canonical_ordering(output->buf, output->len);

    return output;
}

/*
 * Render a string into NFC.
 */
static ucharbuf *nfc(ucharbuf *input)
{
    /*
     * Definition D120 in Unicode 15.0.0 section 3.11: render the
     * string into NFD, then apply the canonical composition algorithm.
     */
    ucharbuf *output = nfd(input);
    output->len = canonical_composition(output->buf, output->len);

    return output;
}

/*
 * Convert a UTF-8 string into NFC, returning it as UTF-8 again.
 */
strbuf *utf8_to_nfc(ptrlen input)
{
    BinarySource src[1];
    BinarySource_BARE_INIT_PL(src, input);

    ucharbuf *inbuf = ucharbuf_new();
    while (get_avail(src))
        ucharbuf_append(inbuf, decode_utf8(src, NULL));

    ucharbuf *outbuf = nfc(inbuf);

    strbuf *output = strbuf_new_nm();
    for (size_t i = 0; i < outbuf->len; i++)
        put_utf8_char(output, outbuf->buf[i]);

    ucharbuf_free(inbuf);
    ucharbuf_free(outbuf);

    return output;
}

#ifdef TEST
void out_of_memory(void)
{
    fprintf(stderr, "out of memory!\n");
    exit(2);
}

static int pass, fail;

static void subtest(const char *filename, int lineno, const char *subdesc,
                    char nftype, ucharbuf *input, ucharbuf *expected)
{
    /*
     * Convert input into either NFC or NFD, and check it's equal to
     * expected
     */
    ucharbuf *nf;
    switch (nftype) {
      case 'C':
        nf = nfc(input);
        break;
      case 'D':
        nf = nfd(input);
        break;
      default:
        unreachable("bad nftype");
    }

    if (nf->len == expected->len && !memcmp(nf->buf, expected->buf, nf->len)) {
        pass++;
    } else {
        printf("%s:%d: failed %s: NF%c([", filename, lineno, subdesc, nftype);
        for (size_t pos = 0; pos < input->len; pos += sizeof(uchar))
            printf("%s%04X", pos ? " " : "", (unsigned)input->buf[pos]);
        printf("]) -> [");
        for (size_t pos = 0; pos < nf->len; pos += sizeof(uchar))
            printf("%s%04X", pos ? " " : "", (unsigned)nf->buf[pos]);
        printf("] != [");
        for (size_t pos = 0; pos < expected->len; pos += sizeof(uchar))
            printf("%s%04X", pos ? " " : "", (unsigned)expected->buf[pos]);
        printf("]\n");
        fail++;
    }

    ucharbuf_free(nf);
}

static void run_tests(const char *filename, FILE *fp)
{
    for (int lineno = 1;; lineno++) {
        char *line = chomp(fgetline(fp));
        if (!line)
            break;

        /* Strip section dividers which begin with @ */
        if (*line == '@') {
            sfree(line);
            continue;
        }

        /* Strip comments, if any */
        ptrlen pl = ptrlen_from_asciz(line);
        {
            const char *p = memchr(pl.ptr, '#', pl.len);
            if (p)
                pl.len = p - (const char *)pl.ptr;
        }

        /* Strip trailing space */
        while (pl.len > 0 &&
               (((char *)pl.ptr)[pl.len-1] == ' ' ||
                ((char *)pl.ptr)[pl.len-1] == '\t'))
            pl.len--;

        /* Skip empty lines */
        if (!pl.len) {
            sfree(line);
            continue;
        }

        /* Break up at semicolons, expecting five fields, each of
         * which we decode into hex code points */
        ucharbuf *fields[5];
        for (size_t i = 0; i < lenof(fields); i++) {
            ptrlen field = ptrlen_get_word(&pl, ";");
            fields[i] = ucharbuf_new();

            ptrlen chr;
            while ((chr = ptrlen_get_word(&field, " ")).len) {
                char *chrstr = mkstr(chr);
                uchar c = strtoul(chrstr, NULL, 16);
                sfree(chrstr);
                ucharbuf_append(fields[i], c);
            }
        }

        subtest(filename, lineno, "NFC(c1) = c2", 'C', fields[0], fields[1]);
        subtest(filename, lineno, "NFC(c2) = c2", 'C', fields[1], fields[1]);
        subtest(filename, lineno, "NFC(c3) = c2", 'C', fields[2], fields[1]);
        subtest(filename, lineno, "NFC(c4) = c4", 'C', fields[3], fields[3]);
        subtest(filename, lineno, "NFC(c5) = c4", 'C', fields[4], fields[3]);
        subtest(filename, lineno, "NFD(c1) = c3", 'D', fields[0], fields[2]);
        subtest(filename, lineno, "NFD(c2) = c3", 'D', fields[1], fields[2]);
        subtest(filename, lineno, "NFD(c3) = c3", 'D', fields[2], fields[2]);
        subtest(filename, lineno, "NFD(c4) = c5", 'D', fields[3], fields[4]);
        subtest(filename, lineno, "NFD(c5) = c5", 'D', fields[4], fields[4]);

        for (size_t i = 0; i < lenof(fields); i++)
            ucharbuf_free(fields[i]);

        sfree(line);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "test_unicode_norm: give an input file "
                "of tests or '-'\n");
        return 1;
    }

    const char *filename = argv[1];

    if (!strcmp(filename, "-")) {
        run_tests("<standard input>", stdin);
    } else {
        FILE *fp = fopen(filename, "r");
        if (!fp) {
            fprintf(stderr, "test_unicode_norm: unable to open '%s'\n",
                    filename);
            return 1;
        }
        run_tests(filename, fp);
        fclose(fp);
    }

    printf("pass %d fail %d total %d\n", pass, fail, pass + fail);

    return fail != 0;
}
#endif
