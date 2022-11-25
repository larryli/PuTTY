/*
 * dup_wc_to_mb: memory-allocating wrapper on wc_to_mb.
 *
 * Also dup_wc_to_mb_c: same but you already know the length of the
 * wide string, and you get told the length of the returned string.
 * (But it's still NUL-terminated, for convenience.).
 */

#include <wchar.h>

#include "putty.h"
#include "misc.h"

char *dup_wc_to_mb_c(int codepage, int flags, const wchar_t *string,
                     size_t inlen, const char *defchr, size_t *outlen_p)
{
    assert(inlen <= INT_MAX);

    size_t outsize = inlen+1;
    char *out = snewn(outsize, char);

    while (true) {
        size_t outlen = wc_to_mb(codepage, flags, string, inlen, out, outsize,
                                 defchr);
        /* We can only be sure we've consumed the whole input if the
         * output is not within a multibyte-character-length of the
         * end of the buffer! */
        if (outlen < outsize && outsize - outlen > MB_LEN_MAX) {
            if (outlen_p)
                *outlen_p = outlen;
            out[outlen] = '\0';
            return out;
        }

        sgrowarray(out, outsize, outsize);
    }
}

char *dup_wc_to_mb(int codepage, int flags, const wchar_t *string,
                   const char *defchr)
{
    return dup_wc_to_mb_c(codepage, flags, string, wcslen(string),
                          defchr, NULL);
}
