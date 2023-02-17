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
                "cannot normalise this string: UTF-8 decoding error "
                "at character position %"SIZEu", byte position %"SIZEu": %s",
                nchars, src->pos, decode_utf8_error_strings[err]);
        if (!known(c))
            return dupprintf(
                "cannot stably normalise this string: code point %04X "
                "(at character position %"SIZEu", byte position %"SIZEu") "
                "is not in Unicode %s", c, nchars, src->pos,
                UNICODE_VERSION_SHORT);
    }

    return NULL;
}
