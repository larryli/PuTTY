/*
 * Test program for split_into_argv.
 */

#include "putty.h"

const struct argv_test {
    const char *cmdline;
    const char *argv[10];
    bool include_program_name;
} argv_tests[] = {
    /*
     * We generate this set of tests by invoking ourself with
     * `-generate'.
     */
#if !MOD3
    /* Newer behaviour, with no weird mod-3 glitch. */
    {"ab c\" d", {"ab", "c d", NULL}},
    {"a\"b c\" d", {"ab c", "d", NULL}},
    {"a\"\"b c\" d", {"ab", "c d", NULL}},
    {"a\"\"\"b c\" d", {"a\"b c", "d", NULL}},
    {"a\"\"\"\"b c\" d", {"a\"b", "c d", NULL}},
    {"a\"\"\"\"\"b c\" d", {"a\"\"b c", "d", NULL}},
    {"a\"\"\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"a\"\"\"\"\"\"\"b c\" d", {"a\"\"\"b c", "d", NULL}},
    {"a\"\"\"\"\"\"\"\"b c\" d", {"a\"\"\"b", "c d", NULL}},
    {"a\\b c\" d", {"a\\b", "c d", NULL}},
    {"a\\\"b c\" d", {"a\"b", "c d", NULL}},
    {"a\\\"\"b c\" d", {"a\"b c", "d", NULL}},
    {"a\\\"\"\"b c\" d", {"a\"b", "c d", NULL}},
    {"a\\\"\"\"\"b c\" d", {"a\"\"b c", "d", NULL}},
    {"a\\\"\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"a\\\"\"\"\"\"\"b c\" d", {"a\"\"\"b c", "d", NULL}},
    {"a\\\"\"\"\"\"\"\"b c\" d", {"a\"\"\"b", "c d", NULL}},
    {"a\\\"\"\"\"\"\"\"\"b c\" d", {"a\"\"\"\"b c", "d", NULL}},
    {"a\\\\b c\" d", {"a\\\\b", "c d", NULL}},
    {"a\\\\\"b c\" d", {"a\\b c", "d", NULL}},
    {"a\\\\\"\"b c\" d", {"a\\b", "c d", NULL}},
    {"a\\\\\"\"\"b c\" d", {"a\\\"b c", "d", NULL}},
    {"a\\\\\"\"\"\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"a\\\\\"\"\"\"\"b c\" d", {"a\\\"\"b c", "d", NULL}},
    {"a\\\\\"\"\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"a\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b c", "d", NULL}},
    {"a\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b", "c d", NULL}},
    {"a\\\\\\b c\" d", {"a\\\\\\b", "c d", NULL}},
    {"a\\\\\\\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"a\\\\\\\"\"b c\" d", {"a\\\"b c", "d", NULL}},
    {"a\\\\\\\"\"\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"a\\\\\\\"\"\"\"b c\" d", {"a\\\"\"b c", "d", NULL}},
    {"a\\\\\\\"\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"a\\\\\\\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b c", "d", NULL}},
    {"a\\\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b", "c d", NULL}},
    {"a\\\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"\"b c", "d", NULL}},
    {"a\\\\\\\\b c\" d", {"a\\\\\\\\b", "c d", NULL}},
    {"a\\\\\\\\\"b c\" d", {"a\\\\b c", "d", NULL}},
    {"a\\\\\\\\\"\"b c\" d", {"a\\\\b", "c d", NULL}},
    {"a\\\\\\\\\"\"\"b c\" d", {"a\\\\\"b c", "d", NULL}},
    {"a\\\\\\\\\"\"\"\"b c\" d", {"a\\\\\"b", "c d", NULL}},
    {"a\\\\\\\\\"\"\"\"\"b c\" d", {"a\\\\\"\"b c", "d", NULL}},
    {"a\\\\\\\\\"\"\"\"\"\"b c\" d", {"a\\\\\"\"b", "c d", NULL}},
    {"a\\\\\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\\\"\"\"b c", "d", NULL}},
    {"a\\\\\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\\\"\"\"b", "c d", NULL}},
    {"\"ab c\" d", {"ab c", "d", NULL}},
    {"\"a\"b c\" d", {"ab", "c d", NULL}},
    {"\"a\"\"b c\" d", {"a\"b c", "d", NULL}},
    {"\"a\"\"\"b c\" d", {"a\"b", "c d", NULL}},
    {"\"a\"\"\"\"b c\" d", {"a\"\"b c", "d", NULL}},
    {"\"a\"\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"\"a\"\"\"\"\"\"b c\" d", {"a\"\"\"b c", "d", NULL}},
    {"\"a\"\"\"\"\"\"\"b c\" d", {"a\"\"\"b", "c d", NULL}},
    {"\"a\"\"\"\"\"\"\"\"b c\" d", {"a\"\"\"\"b c", "d", NULL}},
    {"\"a\\b c\" d", {"a\\b c", "d", NULL}},
    {"\"a\\\"b c\" d", {"a\"b c", "d", NULL}},
    {"\"a\\\"\"b c\" d", {"a\"b", "c d", NULL}},
    {"\"a\\\"\"\"b c\" d", {"a\"\"b c", "d", NULL}},
    {"\"a\\\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"\"a\\\"\"\"\"\"b c\" d", {"a\"\"\"b c", "d", NULL}},
    {"\"a\\\"\"\"\"\"\"b c\" d", {"a\"\"\"b", "c d", NULL}},
    {"\"a\\\"\"\"\"\"\"\"b c\" d", {"a\"\"\"\"b c", "d", NULL}},
    {"\"a\\\"\"\"\"\"\"\"\"b c\" d", {"a\"\"\"\"b", "c d", NULL}},
    {"\"a\\\\b c\" d", {"a\\\\b c", "d", NULL}},
    {"\"a\\\\\"b c\" d", {"a\\b", "c d", NULL}},
    {"\"a\\\\\"\"b c\" d", {"a\\\"b c", "d", NULL}},
    {"\"a\\\\\"\"\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"\"a\\\\\"\"\"\"b c\" d", {"a\\\"\"b c", "d", NULL}},
    {"\"a\\\\\"\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"\"a\\\\\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b c", "d", NULL}},
    {"\"a\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b", "c d", NULL}},
    {"\"a\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"\"b c", "d", NULL}},
    {"\"a\\\\\\b c\" d", {"a\\\\\\b c", "d", NULL}},
    {"\"a\\\\\\\"b c\" d", {"a\\\"b c", "d", NULL}},
    {"\"a\\\\\\\"\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"\"a\\\\\\\"\"\"b c\" d", {"a\\\"\"b c", "d", NULL}},
    {"\"a\\\\\\\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"\"a\\\\\\\"\"\"\"\"b c\" d", {"a\\\"\"\"b c", "d", NULL}},
    {"\"a\\\\\\\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b", "c d", NULL}},
    {"\"a\\\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"\"b c", "d", NULL}},
    {"\"a\\\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"\"b", "c d", NULL}},
    {"\"a\\\\\\\\b c\" d", {"a\\\\\\\\b c", "d", NULL}},
    {"\"a\\\\\\\\\"b c\" d", {"a\\\\b", "c d", NULL}},
    {"\"a\\\\\\\\\"\"b c\" d", {"a\\\\\"b c", "d", NULL}},
    {"\"a\\\\\\\\\"\"\"b c\" d", {"a\\\\\"b", "c d", NULL}},
    {"\"a\\\\\\\\\"\"\"\"b c\" d", {"a\\\\\"\"b c", "d", NULL}},
    {"\"a\\\\\\\\\"\"\"\"\"b c\" d", {"a\\\\\"\"b", "c d", NULL}},
    {"\"a\\\\\\\\\"\"\"\"\"\"b c\" d", {"a\\\\\"\"\"b c", "d", NULL}},
    {"\"a\\\\\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\\\"\"\"b", "c d", NULL}},
    {"\"a\\\\\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\\\"\"\"\"b c", "d", NULL}},
#else /* MOD3 */
    /* VS7 mod-3 behaviour. */
    {"ab c\" d", {"ab", "c d", NULL}},
    {"a\"b c\" d", {"ab c", "d", NULL}},
    {"a\"\"b c\" d", {"ab", "c d", NULL}},
    {"a\"\"\"b c\" d", {"a\"b", "c d", NULL}},
    {"a\"\"\"\"b c\" d", {"a\"b c", "d", NULL}},
    {"a\"\"\"\"\"b c\" d", {"a\"b", "c d", NULL}},
    {"a\"\"\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"a\"\"\"\"\"\"\"b c\" d", {"a\"\"b c", "d", NULL}},
    {"a\"\"\"\"\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"a\\b c\" d", {"a\\b", "c d", NULL}},
    {"a\\\"b c\" d", {"a\"b", "c d", NULL}},
    {"a\\\"\"b c\" d", {"a\"b c", "d", NULL}},
    {"a\\\"\"\"b c\" d", {"a\"b", "c d", NULL}},
    {"a\\\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"a\\\"\"\"\"\"b c\" d", {"a\"\"b c", "d", NULL}},
    {"a\\\"\"\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"a\\\"\"\"\"\"\"\"b c\" d", {"a\"\"\"b", "c d", NULL}},
    {"a\\\"\"\"\"\"\"\"\"b c\" d", {"a\"\"\"b c", "d", NULL}},
    {"a\\\\b c\" d", {"a\\\\b", "c d", NULL}},
    {"a\\\\\"b c\" d", {"a\\b c", "d", NULL}},
    {"a\\\\\"\"b c\" d", {"a\\b", "c d", NULL}},
    {"a\\\\\"\"\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"a\\\\\"\"\"\"b c\" d", {"a\\\"b c", "d", NULL}},
    {"a\\\\\"\"\"\"\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"a\\\\\"\"\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"a\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\"\"b c", "d", NULL}},
    {"a\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"a\\\\\\b c\" d", {"a\\\\\\b", "c d", NULL}},
    {"a\\\\\\\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"a\\\\\\\"\"b c\" d", {"a\\\"b c", "d", NULL}},
    {"a\\\\\\\"\"\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"a\\\\\\\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"a\\\\\\\"\"\"\"\"b c\" d", {"a\\\"\"b c", "d", NULL}},
    {"a\\\\\\\"\"\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"a\\\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b", "c d", NULL}},
    {"a\\\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b c", "d", NULL}},
    {"a\\\\\\\\b c\" d", {"a\\\\\\\\b", "c d", NULL}},
    {"a\\\\\\\\\"b c\" d", {"a\\\\b c", "d", NULL}},
    {"a\\\\\\\\\"\"b c\" d", {"a\\\\b", "c d", NULL}},
    {"a\\\\\\\\\"\"\"b c\" d", {"a\\\\\"b", "c d", NULL}},
    {"a\\\\\\\\\"\"\"\"b c\" d", {"a\\\\\"b c", "d", NULL}},
    {"a\\\\\\\\\"\"\"\"\"b c\" d", {"a\\\\\"b", "c d", NULL}},
    {"a\\\\\\\\\"\"\"\"\"\"b c\" d", {"a\\\\\"\"b", "c d", NULL}},
    {"a\\\\\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\\\"\"b c", "d", NULL}},
    {"a\\\\\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\\\"\"b", "c d", NULL}},
    {"\"ab c\" d", {"ab c", "d", NULL}},
    {"\"a\"b c\" d", {"ab", "c d", NULL}},
    {"\"a\"\"b c\" d", {"a\"b", "c d", NULL}},
    {"\"a\"\"\"b c\" d", {"a\"b c", "d", NULL}},
    {"\"a\"\"\"\"b c\" d", {"a\"b", "c d", NULL}},
    {"\"a\"\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"\"a\"\"\"\"\"\"b c\" d", {"a\"\"b c", "d", NULL}},
    {"\"a\"\"\"\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"\"a\"\"\"\"\"\"\"\"b c\" d", {"a\"\"\"b", "c d", NULL}},
    {"\"a\\b c\" d", {"a\\b c", "d", NULL}},
    {"\"a\\\"b c\" d", {"a\"b c", "d", NULL}},
    {"\"a\\\"\"b c\" d", {"a\"b", "c d", NULL}},
    {"\"a\\\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"\"a\\\"\"\"\"b c\" d", {"a\"\"b c", "d", NULL}},
    {"\"a\\\"\"\"\"\"b c\" d", {"a\"\"b", "c d", NULL}},
    {"\"a\\\"\"\"\"\"\"b c\" d", {"a\"\"\"b", "c d", NULL}},
    {"\"a\\\"\"\"\"\"\"\"b c\" d", {"a\"\"\"b c", "d", NULL}},
    {"\"a\\\"\"\"\"\"\"\"\"b c\" d", {"a\"\"\"b", "c d", NULL}},
    {"\"a\\\\b c\" d", {"a\\\\b c", "d", NULL}},
    {"\"a\\\\\"b c\" d", {"a\\b", "c d", NULL}},
    {"\"a\\\\\"\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"\"a\\\\\"\"\"b c\" d", {"a\\\"b c", "d", NULL}},
    {"\"a\\\\\"\"\"\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"\"a\\\\\"\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"\"a\\\\\"\"\"\"\"\"b c\" d", {"a\\\"\"b c", "d", NULL}},
    {"\"a\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"\"a\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b", "c d", NULL}},
    {"\"a\\\\\\b c\" d", {"a\\\\\\b c", "d", NULL}},
    {"\"a\\\\\\\"b c\" d", {"a\\\"b c", "d", NULL}},
    {"\"a\\\\\\\"\"b c\" d", {"a\\\"b", "c d", NULL}},
    {"\"a\\\\\\\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"\"a\\\\\\\"\"\"\"b c\" d", {"a\\\"\"b c", "d", NULL}},
    {"\"a\\\\\\\"\"\"\"\"b c\" d", {"a\\\"\"b", "c d", NULL}},
    {"\"a\\\\\\\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b", "c d", NULL}},
    {"\"a\\\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b c", "d", NULL}},
    {"\"a\\\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\"\"\"b", "c d", NULL}},
    {"\"a\\\\\\\\b c\" d", {"a\\\\\\\\b c", "d", NULL}},
    {"\"a\\\\\\\\\"b c\" d", {"a\\\\b", "c d", NULL}},
    {"\"a\\\\\\\\\"\"b c\" d", {"a\\\\\"b", "c d", NULL}},
    {"\"a\\\\\\\\\"\"\"b c\" d", {"a\\\\\"b c", "d", NULL}},
    {"\"a\\\\\\\\\"\"\"\"b c\" d", {"a\\\\\"b", "c d", NULL}},
    {"\"a\\\\\\\\\"\"\"\"\"b c\" d", {"a\\\\\"\"b", "c d", NULL}},
    {"\"a\\\\\\\\\"\"\"\"\"\"b c\" d", {"a\\\\\"\"b c", "d", NULL}},
    {"\"a\\\\\\\\\"\"\"\"\"\"\"b c\" d", {"a\\\\\"\"b", "c d", NULL}},
    {"\"a\\\\\\\\\"\"\"\"\"\"\"\"b c\" d", {"a\\\\\"\"\"b", "c d", NULL}},
#endif /* MOD3 */
    /* Common tests that check the special program-name rule. */
    {"\"a b\\\"c \"d e\" \"f g\"", {"a b\\c", "d e", "f g", NULL}, true},
    {"\"a b\\\"c \"d e\" \"f g\"", {"a b\"c d", "e f", "g", NULL}, false},
};

void out_of_memory(void)
{
    fprintf(stderr, "out of memory!\n");
    exit(2);
}

int main(int argc, char **argv)
{
    int i, j;

    if (argc > 1) {
        /*
         * Generation of tests.
         *
         * Given `-splat <args>', we print out a C-style
         * representation of each argument (in the form "a", "b",
         * NULL), backslash-escaping each backslash and double
         * quote.
         *
         * Given `-split <string>', we first doctor `string' by
         * turning forward slashes into backslashes, single quotes
         * into double quotes and underscores into spaces; and then
         * we feed the resulting string to ourself with `-splat'.
         *
         * Given `-generate', we concoct a variety of fun test
         * cases, encode them in quote-safe form (mapping \, " and
         * space to /, ' and _ respectively) and feed each one to
         * `-split'.
         */
        if (!strcmp(argv[1], "-splat")) {
            int i;
            char *p;
            for (i = 2; i < argc; i++) {
                putchar('"');
                for (p = argv[i]; *p; p++) {
                    if (*p == '\\' || *p == '"')
                        putchar('\\');
                    putchar(*p);
                }
                printf("\", ");
            }
            printf("NULL");
            return 0;
        }

        if (!strcmp(argv[1], "-split") && argc > 2) {
            strbuf *cmdline = strbuf_new();
            char *p;

            put_fmt(cmdline, "%s -splat ", argv[0]);
            printf("    {\"");
            size_t args_start = cmdline->len;
            for (p = argv[2]; *p; p++) {
                char c = (*p == '/' ? '\\' :
                          *p == '\'' ? '"' :
                          *p == '_' ? ' ' :
                          *p);
                put_byte(cmdline, c);
            }
            write_c_string_literal(stdout, ptrlen_from_asciz(
                                       cmdline->s + args_start));
            printf("\", {");
            fflush(stdout);

            system(cmdline->s);

            printf("}},\n");

            strbuf_free(cmdline);
            return 0;
        }

        if (!strcmp(argv[1], "-generate")) {
            char *teststr, *p;
            int i, initialquote, backslashes, quotes;

            teststr = malloc(200 + strlen(argv[0]));

            for (initialquote = 0; initialquote <= 1; initialquote++) {
                for (backslashes = 0; backslashes < 5; backslashes++) {
                    for (quotes = 0; quotes < 9; quotes++) {
                        p = teststr + sprintf(teststr, "%s -split ", argv[0]);
                        if (initialquote) *p++ = '\'';
                        *p++ = 'a';
                        for (i = 0; i < backslashes; i++) *p++ = '/';
                        for (i = 0; i < quotes; i++) *p++ = '\'';
                        *p++ = 'b';
                        *p++ = '_';
                        *p++ = 'c';
                        *p++ = '\'';
                        *p++ = '_';
                        *p++ = 'd';
                        *p = '\0';

                        system(teststr);
                    }
                }
            }
            return 0;
        }

        if (!strcmp(argv[1], "-tabulate")) {
            char table[] = "\
 *                      backslashes                   \n\
 *                                                    \n\
 *               0         1      2      3      4     \n\
 *                                                    \n\
 *         0          |                               \n\
 *            --------+-----------------------------  \n\
 *         1          |                               \n\
 *    q    2          |                               \n\
 *    u    3          |                               \n\
 *    o    4          |                               \n\
 *    t    5          |                               \n\
 *    e    6          |                               \n\
 *    s    7          |                               \n\
 *         8          |                               \n\
";
            char *linestarts[14];
            char *p = table;
            for (i = 0; i < lenof(linestarts); i++) {
                linestarts[i] = p;
                p += strcspn(p, "\n");
                if (*p) p++;
            }

            for (i = 0; i < lenof(argv_tests); i++) {
                const struct argv_test *test = &argv_tests[i];
                const char *q = test->cmdline;

                /* Skip tests that aren't telling us something about
                 * the behaviour _inside_ a quoted string */
                if (*q != '"')
                    continue;

                q++;

                assert(*q == 'a');
                q++;
                int backslashes_in = 0, quotes_in = 0;
                while (*q == '\\') {
                    q++;
                    backslashes_in++;
                }
                while (*q == '"') {
                    q++;
                    quotes_in++;
                }

                q = test->argv[0];
                assert(*q == 'a');
                q++;
                int backslashes_out = 0, quotes_out = 0;
                while (*q == '\\') {
                    q++;
                    backslashes_out++;
                }
                while (*q == '"') {
                    q++;
                    quotes_out++;
                }
                assert(*q == 'b');
                q++;
                bool in_quoted_string = (*q == ' ');

                int x = (backslashes_in == 0 ? 15 : 18 + 7 * backslashes_in);
                int y = (quotes_in == 0 ? 4 : 5 + quotes_in);
                char *buf = dupprintf("%d,%d,%c",
                                      backslashes_out, quotes_out,
                                      in_quoted_string ? 'y' : 'n');
                memcpy(linestarts[y] + x, buf, strlen(buf));
                sfree(buf);
            }

            fputs(table, stdout);
            return 0;
        }

        fprintf(stderr, "unrecognised option: \"%s\"\n", argv[1]);
        return 1;
    }

    /*
     * If we get here, we were invoked with no arguments, so just
     * run the tests.
     */
    int passes = 0, fails = 0;

    for (i = 0; i < lenof(argv_tests); i++) {
        int ac;
        char **av;
        bool failed = false;

        split_into_argv((char *)argv_tests[i].cmdline,
                        argv_tests[i].include_program_name, &ac, &av, NULL);

        for (j = 0; j < ac && argv_tests[i].argv[j]; j++) {
            if (strcmp(av[j], argv_tests[i].argv[j])) {
                printf("failed test %d (|%s|) arg %d: |%s| should be |%s|\n",
                       i, argv_tests[i].cmdline,
                       j, av[j], argv_tests[i].argv[j]);
                failed = true;
            }
#ifdef VERBOSE
            else {
                printf("test %d (|%s|) arg %d: |%s| == |%s|\n",
                       i, argv_tests[i].cmdline,
                       j, av[j], argv_tests[i].argv[j]);
            }
#endif
        }
        if (j < ac) {
            printf("failed test %d (|%s|): %d args returned, should be %d\n",
                   i, argv_tests[i].cmdline, ac, j);
            failed = true;
        }
        if (argv_tests[i].argv[j]) {
            printf("failed test %d (|%s|): %d args returned, should be more\n",
                   i, argv_tests[i].cmdline, ac);
            failed = true;
        }

        if (failed)
            fails++;
        else
            passes++;
    }

    printf("passed %d failed %d (%s mode)\n",
           passes, fails,
#if MOD3
           "mod 3"
#else
           "mod 2"
#endif
        );

    return fails != 0;
}
