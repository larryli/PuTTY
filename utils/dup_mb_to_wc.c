/*
 * dup_mb_to_wc: memory-allocating wrapper on mb_to_wc.
 *
 * Also dup_mb_to_wc_c: same but you already know the length of the
 * string, and you get told the length of the returned wide string.
 * (But it's still NUL-terminated, for convenience.)
 */

#include "putty.h"
#include "misc.h"

wchar_t *dup_mb_to_wc_c(int codepage, int flags, const char *string,
                        size_t inlen, size_t *outlen_p)
{
    assert(inlen <= INT_MAX);
    size_t mult;
    for (mult = 1 ;; mult++) {
        wchar_t *ret = snewn(mult*inlen + 2, wchar_t);
        size_t outlen = mb_to_wc(codepage, flags, string, inlen, ret,
                                 mult*inlen + 1);
        if (outlen < mult*inlen+1) {
            if (outlen_p)
                *outlen_p = outlen;
            ret[outlen] = L'\0';
            return ret;
        }
        sfree(ret);
    }
}

wchar_t *dup_mb_to_wc(int codepage, int flags, const char *string)
{
    return dup_mb_to_wc_c(codepage, flags, string, strlen(string), NULL);
}
