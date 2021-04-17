/*
 * Return a string describing everything we know about how this
 * particular binary was built: from what source, for what target
 * platform, using what tools, with what settings, etc.
 */

#include "putty.h"

char *buildinfo(const char *newline)
{
    strbuf *buf = strbuf_new();

    strbuf_catf(buf, "Build platform: %d-bit %s",
                (int)(CHAR_BIT * sizeof(void *)),
                BUILDINFO_PLATFORM);

#ifdef __clang_version__
#define FOUND_COMPILER
    strbuf_catf(buf, "%sCompiler: clang %s", newline, __clang_version__);
#elif defined __GNUC__ && defined __VERSION__
#define FOUND_COMPILER
    strbuf_catf(buf, "%sCompiler: gcc %s", newline, __VERSION__);
#endif

#if defined _MSC_VER
#ifndef FOUND_COMPILER
#define FOUND_COMPILER
    strbuf_catf(buf, "%sCompiler: ", newline);
#else
    strbuf_catf(buf, ", emulating ");
#endif
    strbuf_catf(buf, "Visual Studio");

#if 0
    /*
     * List of _MSC_VER values and their translations taken from
     * https://docs.microsoft.com/en-us/cpp/preprocessor/predefined-macros
     *
     * The pointless #if 0 branch containing this comment is there so
     * that every real clause can start with #elif and there's no
     * anomalous first clause. That way the patch looks nicer when you
     * add extra ones.
     */
#elif _MSC_VER == 1928 && _MSC_FULL_VER >= 192829500
    /*
     * 16.9 and 16.8 have the same _MSC_VER value, and have to be
     * distinguished by _MSC_FULL_VER. As of 2021-03-04 that is not
     * mentioned on the above page, but see e.g.
     * https://developercommunity.visualstudio.com/t/the-169-cc-compiler-still-uses-the-same-version-nu/1335194#T-N1337120
     * which says that 16.9 builds will have versions starting at
     * 19.28.29500.* and going up. Hence, 19 28 29500 is what we
     * compare _MSC_FULL_VER against above.
     */
    strbuf_catf(buf, " 2019 (16.9)");
#elif _MSC_VER == 1928
    strbuf_catf(buf, " 2019 (16.8)");
#elif _MSC_VER == 1927
    strbuf_catf(buf, " 2019 (16.7)");
#elif _MSC_VER == 1926
    strbuf_catf(buf, " 2019 (16.6)");
#elif _MSC_VER == 1925
    strbuf_catf(buf, " 2019 (16.5)");
#elif _MSC_VER == 1924
    strbuf_catf(buf, " 2019 (16.4)");
#elif _MSC_VER == 1923
    strbuf_catf(buf, " 2019 (16.3)");
#elif _MSC_VER == 1922
    strbuf_catf(buf, " 2019 (16.2)");
#elif _MSC_VER == 1921
    strbuf_catf(buf, " 2019 (16.1)");
#elif _MSC_VER == 1920
    strbuf_catf(buf, " 2019 (16.0)");
#elif _MSC_VER == 1916
    strbuf_catf(buf, " 2017 version 15.9");
#elif _MSC_VER == 1915
    strbuf_catf(buf, " 2017 version 15.8");
#elif _MSC_VER == 1914
    strbuf_catf(buf, " 2017 version 15.7");
#elif _MSC_VER == 1913
    strbuf_catf(buf, " 2017 version 15.6");
#elif _MSC_VER == 1912
    strbuf_catf(buf, " 2017 version 15.5");
#elif _MSC_VER == 1911
    strbuf_catf(buf, " 2017 version 15.3");
#elif _MSC_VER == 1910
    strbuf_catf(buf, " 2017 RTW (15.0)");
#elif _MSC_VER == 1900
    strbuf_catf(buf, " 2015 (14.0)");
#elif _MSC_VER == 1800
    strbuf_catf(buf, " 2013 (12.0)");
#elif _MSC_VER == 1700
    strbuf_catf(buf, " 2012 (11.0)");
#elif _MSC_VER == 1600
    strbuf_catf(buf, " 2010 (10.0)");
#elif _MSC_VER == 1500
    strbuf_catf(buf, " 2008 (9.0)");
#elif _MSC_VER == 1400
    strbuf_catf(buf, " 2005 (8.0)");
#elif _MSC_VER == 1310
    strbuf_catf(buf, " .NET 2003 (7.1)");
#elif _MSC_VER == 1300
    strbuf_catf(buf, " .NET 2002 (7.0)");
#elif _MSC_VER == 1200
    strbuf_catf(buf, " 6.0");
#else
    strbuf_catf(buf, ", unrecognised version");
#endif
    strbuf_catf(buf, ", _MSC_VER=%d", (int)_MSC_VER);
#endif

#ifdef BUILDINFO_GTK
    {
        char *gtk_buildinfo = buildinfo_gtk_version();
        if (gtk_buildinfo) {
            strbuf_catf(buf, "%sCompiled against GTK version %s",
                        newline, gtk_buildinfo);
            sfree(gtk_buildinfo);
        }
    }
#endif
#if defined _WINDOWS
    {
        int echm = has_embedded_chm();
        if (echm >= 0)
            strbuf_catf(buf, "%sEmbedded HTML Help file: %s", newline,
                        echm ? "yes" : "no");
    }
#endif

#if defined _WINDOWS && defined MINEFIELD
    strbuf_catf(buf, "%sBuild option: MINEFIELD", newline);
#endif
#ifdef NO_IPV6
    strbuf_catf(buf, "%sBuild option: NO_IPV6", newline);
#endif
#ifdef NO_GSSAPI
    strbuf_catf(buf, "%sBuild option: NO_GSSAPI", newline);
#endif
#ifdef STATIC_GSSAPI
    strbuf_catf(buf, "%sBuild option: STATIC_GSSAPI", newline);
#endif
#ifdef UNPROTECT
    strbuf_catf(buf, "%sBuild option: UNPROTECT", newline);
#endif
#ifdef FUZZING
    strbuf_catf(buf, "%sBuild option: FUZZING", newline);
#endif
#ifdef DEBUG
    strbuf_catf(buf, "%sBuild option: DEBUG", newline);
#endif

    strbuf_catf(buf, "%sSource commit: %s", newline, commitid);

    return strbuf_to_str(buf);
}
