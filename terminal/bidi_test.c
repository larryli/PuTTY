/*
 * Test program that reads the Unicode bidi algorithm test case lists
 * that form part of the Unicode Character Database:
 *
 * https://www.unicode.org/Public/UCD/latest/ucd/BidiTest.txt
 * https://www.unicode.org/Public/UCD/latest/ucd/BidiCharacterTest.txt
 */

#include <ctype.h>

#include "putty.h"
#include "misc.h"
#include "bidi.h"

static int pass = 0, fail = 0;

static BidiContext *ctx;

static const char *extract_word(char **ptr)
{
    char *p = *ptr;
    while (*p && isspace((unsigned char)*p)) p++;

    char *start = p;
    while (*p && !isspace((unsigned char)*p)) p++;

    if (*p) {
        *p++ = '\0';
        while (*p && isspace((unsigned char)*p)) p++;
    }

    *ptr = p;
    return start;
}

#define TYPETONAME(X) #X,
static const char *const typenames[] = { BIDI_CHAR_TYPE_LIST(TYPETONAME) };
#undef TYPETONAME

static void run_test(const char *filename, unsigned lineno,
                     bidi_char *bcs, size_t bcs_len,
                     const unsigned *order, size_t order_len,
                     int override)
{
    size_t bcs_orig_len = bcs_len;
    bidi_char *bcs_orig = snewn(bcs_orig_len, bidi_char);
    if (bcs_orig_len)
        memcpy(bcs_orig, bcs, bcs_orig_len * sizeof(bidi_char));

    bcs_len = do_bidi_test(ctx, bcs, bcs_len, override);

    /*
     * TR9 revision 44 rule X9 says we remove explicit embedding
     * controls and BN characters. So the test cases don't list them
     * in the expected outputs. Do the same to our own output - unless
     * we're testing the standard version of the algorithm, in which
     * case, we expect the output to be exactly as the test cases say.
     */
    unsigned *our_order = snewn(bcs_len, unsigned);
    size_t our_order_len = 0;
    for (size_t i = 0; i < bcs_len; i++) {
        BidiType t = bidi_getType(bcs[i].wc);
#ifndef REMOVE_FORMATTING_CHARS
        if (typeIsRemovedDuringProcessing(t))
            continue;
#endif
        our_order[our_order_len++] = bcs[i].index;
    }

    bool ok = false;
    if (our_order_len == order_len) {
        ok = true;
        for (size_t i = 0; i < our_order_len; i++)
            if (our_order[i] != order[i])
                ok = false;
    }
    if (ok) {
        pass++;
    } else {
        fail++;
        printf("%s:%u: failed order\n", filename, lineno);
        printf("  input chars:");
        for (size_t i = 0; i < bcs_orig_len; i++)
            printf(" %04x", bcs_orig[i].wc);
        printf("\n");
        printf("  classes:    ");
        for (size_t i = 0; i < bcs_orig_len; i++)
            printf(" %-4s", typenames[bidi_getType(bcs_orig[i].wc)]);
        printf("\n");
        printf("  para level = %s\n",
               override > 0 ? "LTR" : override < 0 ? "RTL" : "auto");
        printf("  expected:");
        for (size_t i = 0; i < order_len; i++)
            printf(" %u", order[i]);
        printf("\n");
        printf("  got:     ");
        for (size_t i = 0; i < our_order_len; i++)
            printf(" %u", our_order[i]);
        printf("\n");
    }

    /* Put the original data back so we can re-test with another override */
    memcpy(bcs, bcs_orig, bcs_orig_len * sizeof(bidi_char));

    sfree(bcs_orig);
    sfree(our_order);
}

static void class_test(const char *filename, FILE *fp)
{
    unsigned lineno = 0;
    size_t bcs_size = 0, bcs_len = 0;
    bidi_char *bcs = NULL;
    size_t order_size = 0, order_len = 0;
    unsigned *order = NULL;

    /* Preliminary: find a representative character of every bidi
     * type. Prefer positive-width ones if available. */
    unsigned representatives[N_BIDI_TYPES];
    for (size_t i = 0; i < N_BIDI_TYPES; i++)
        representatives[i] = 0;
    for (unsigned uc = 1; uc < 0x110000; uc++) {
        unsigned type = bidi_getType(uc);
        if (!representatives[type] ||
            (mk_wcwidth(representatives[type]) <= 0 && mk_wcwidth(uc) > 0))
            representatives[type] = uc;
    }

    while (true) {
        lineno++;
        char *line = chomp(fgetline(fp));
        if (!line)
            break;

        /* Skip blank lines and comments */
        if (!line[0] || line[0] == '#') {
            sfree(line);
            continue;
        }

        /* Parse @Reorder lines, which tell us the expected output
         * order for all following test cases (until superseded) */
        if (strstartswith(line, "@Reorder:")) {
            char *p = line;
            extract_word(&p); /* eat the "@Reorder:" header itself */
            order_len = 0;
            while (1) {
                const char *word = extract_word(&p);
                if (!*word)
                    break;
                sgrowarray(order, order_size, order_len);
                order[order_len++] = strtoul(word, NULL, 0);
            }

            sfree(line);
            continue;
        }

        /* Skip @Levels lines, which we don't (yet?) do anything with */
        if (strstartswith(line, "@Levels:")) {
            sfree(line);
            continue;
        }

        /* Everything remaining should be an actual test */
        char *semicolon = strchr(line, ';');
        if (!semicolon) {
            printf("%s:%u: bad test line': no bitmap\n", filename, lineno);
            sfree(line);
            continue;
        }
        *semicolon++ = '\0';
        unsigned bitmask = strtoul(semicolon, NULL, 0);
        char *p = line;
        bcs_len = 0;
        bool test_ok = true;
        while (1) {
            const char *word = extract_word(&p);
            if (!*word)
                break;
            unsigned type;
            for (type = 0; type < N_BIDI_TYPES; type++)
                if (!strcmp(word, typenames[type]))
                    break;
            if (type == N_BIDI_TYPES) {
                printf("%s:%u: bad test line: bad bidi type '%s'\n",
                       filename, lineno, word);
                test_ok = false;
                break;
            }
            sgrowarray(bcs, bcs_size, bcs_len);
            bcs[bcs_len].wc = representatives[type];
            bcs[bcs_len].origwc = bcs[bcs_len].wc;
            bcs[bcs_len].index = bcs_len;
            bcs[bcs_len].nchars = 1;
            bcs_len++;
        }

        if (!test_ok) {
            sfree(line);
            continue;
        }

        if (bitmask & 1)
            run_test(filename, lineno, bcs, bcs_len, order, order_len, 0);
        if (bitmask & 2)
            run_test(filename, lineno, bcs, bcs_len, order, order_len, +1);
        if (bitmask & 4)
            run_test(filename, lineno, bcs, bcs_len, order, order_len, -1);

        sfree(line);
    }

    sfree(bcs);
    sfree(order);
}

static void char_test(const char *filename, FILE *fp)
{
    unsigned lineno = 0;
    size_t bcs_size = 0, bcs_len = 0;
    bidi_char *bcs = NULL;
    size_t order_size = 0, order_len = 0;
    unsigned *order = NULL;

    while (true) {
        lineno++;
        char *line = chomp(fgetline(fp));
        if (!line)
            break;

        /* Skip blank lines and comments */
        if (!line[0] || line[0] == '#') {
            sfree(line);
            continue;
        }

        /* Break each test line up into its main fields */
        ptrlen input_pl, para_dir_pl, order_pl;
        {
            ptrlen pl = ptrlen_from_asciz(line);
            input_pl = ptrlen_get_word(&pl, ";");
            para_dir_pl = ptrlen_get_word(&pl, ";");
            ptrlen_get_word(&pl, ";"); /* paragraph level, which we ignore */
            ptrlen_get_word(&pl, ";"); /* embedding levels, which we ignore */
            order_pl = ptrlen_get_word(&pl, ";");
        }

        int override;
        {
            char *para_dir_str = mkstr(para_dir_pl);
            unsigned para_dir = strtoul(para_dir_str, NULL, 0);
            sfree(para_dir_str);

            override = (para_dir == 0 ? +1 : para_dir == 1 ? -1 : 0);
        }

        /* Break up the input into Unicode characters */
        bcs_len = 0;
        {
            ptrlen pl = input_pl;
            while (pl.len) {
                ptrlen chr = ptrlen_get_word(&pl, " ");
                char *chrstr = mkstr(chr);
                sgrowarray(bcs, bcs_size, bcs_len);
                bcs[bcs_len].wc = strtoul(chrstr, NULL, 16);
                bcs[bcs_len].origwc = bcs[bcs_len].wc;
                bcs[bcs_len].index = bcs_len;
                bcs[bcs_len].nchars = 1;
                bcs_len++;
                sfree(chrstr);
            }
        }

        /* Ditto the expected output order */
        order_len = 0;
        {
            ptrlen pl = order_pl;
            while (pl.len) {
                ptrlen chr = ptrlen_get_word(&pl, " ");
                char *chrstr = mkstr(chr);
                sgrowarray(order, order_size, order_len);
                order[order_len++] = strtoul(chrstr, NULL, 0);
                sfree(chrstr);
            }
        }

        run_test(filename, lineno, bcs, bcs_len, order, order_len, override);
        sfree(line);
    }

    sfree(bcs);
    sfree(order);
}

void out_of_memory(void)
{
    fprintf(stderr, "out of memory!\n");
    exit(2);
}

static void usage(FILE *fp)
{
    fprintf(fp, "\
usage:   bidi_test ( ( --class | --char ) infile... )...\n\
e.g.:    bidi_test --class BidiTest.txt --char BidiCharacterTest.txt\n\
also:    --help              display this text\n\
");
}

int main(int argc, char **argv)
{
    void (*testfn)(const char *, FILE *) = NULL;
    bool doing_opts = true;
    const char *filename = NULL;
    bool done_something = false;

    ctx = bidi_new_context();

    while (--argc > 0) {
        const char *arg = *++argv;
        if (doing_opts && arg[0] == '-' && arg[1]) {
            if (!strcmp(arg, "--")) {
                doing_opts = false;
            } else if (!strcmp(arg, "--class")) {
                testfn = class_test;
            } else if (!strcmp(arg, "--char")) {
                testfn = char_test;
            } else if (!strcmp(arg, "--help")) {
                usage(stdout);
                return 0;
            } else {
                fprintf(stderr, "unrecognised option '%s'\n", arg);
                return 1;
            }
        } else {
            const char *filename = arg;

            if (!testfn) {
                fprintf(stderr, "no mode argument provided before filename "
                        "'%s'\n", filename);
                return 1;
            }

            if (!strcmp(filename, "-")) {
                testfn("<standard input>", stdin);
            } else {
                FILE *fp = fopen(filename, "r");
                if (!fp) {
                    fprintf(stderr, "unable to open '%s'\n", filename);
                    return 1;
                }
                testfn(filename, fp);
                fclose(fp);
            }
            done_something = true;
        }
    }

    if (!done_something) {
        usage(stderr);
        return 1;
    }

    if (!filename)
        filename = "-";

    printf("pass %d fail %d total %d\n", pass, fail, pass + fail);

    bidi_free_context(ctx);
    return fail != 0;
}
