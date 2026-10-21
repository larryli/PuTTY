/*
 * Check a UTF-8 string to ensure every character in it is part of the
 * version of Unicode that we understand.
 *
 * (If it isn't, then we don't know what combining properties it has,
 * so we can't safely NFC it and rely on the result not changing when
 * we later update our Unicode version.)
 */

#include "misc.h"
#include "unicode/version.h"

static bool known(unsigned c)
{
    struct range {
        unsigned start, end;
    };
    static const struct range ranges[] = {
        #include "unicode/known_chars.h"
    };

    const struct range *start = ranges, *end = start + lenof(ranges);

    while (end > start) {
        const struct range *curr = start + (end-start) / 2;
        if (c < curr->start)
            end = curr;
        else if (c > curr->end)
            start = curr + 1;
        else
            return true;
    }

    return false;
};

char *utf8_unknown_char(ptrlen input)
{
    BinarySource src[1];
    BinarySource_BARE_INIT_PL(src, input);

    for (size_t nchars = 0; get_avail(src); nchars++) {
        DecodeUTF8Failure err;
        unsigned c = decode_utf8(src, &err);
        if (err != DUTF8_SUCCESS)
            return dupprintf(
                "无法规范化此字符串: UTF-8 解码错误"
                "（字符位置 %"SIZEu"，字节位置 %"SIZEu": %s",
                nchars, src->pos, decode_utf8_error_strings[err]);
        if (!known(c))
            return dupprintf(
                "无法稳定规范化此字符串: 码点 %04X"
                "（字符位置 %"SIZEu"，字节位置 %"SIZEu") "
                "不在 Unicode %s 中", c, nchars, src->pos,
                UNICODE_VERSION_SHORT);
    }

    return NULL;
}
