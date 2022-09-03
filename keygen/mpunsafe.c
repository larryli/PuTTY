#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include "defs.h"
#include "misc.h"
#include "puttymem.h"

#include "mpint.h"
#include "mpunsafe.h"
#include "crypto/mpint_i.h"

/*
 * This global symbol is also defined in ssh/kex2-client.c, to ensure
 * that these unsafe non-constant-time mp_int functions can't end up
 * accidentally linked in to any PuTTY tool that actually makes an SSH
 * client connection.
 *
 * (Only _client_ connections, however. Uppity, being a test server
 * only, is exempt.)
 */
const int deliberate_symbol_clash = 12345;

static size_t mp_unsafe_words_needed(mp_int *x)
{
    size_t words = x->nw;
    while (words > 1 && !x->w[words-1])
        words--;
    return words;
}

mp_int *mp_unsafe_shrink(mp_int *x)
{
    x->nw = mp_unsafe_words_needed(x);
    /* This potentially leaves some allocated words between the new
     * and old values of x->nw, which won't be wiped by mp_free now
     * that x->nw doesn't mention that they exist. But we've just
     * checked they're all zero, so we don't need to wipe them now
     * either. */
    return x;
}

mp_int *mp_unsafe_copy(mp_int *x)
{
    mp_int *copy = mp_make_sized(mp_unsafe_words_needed(x));
    mp_copy_into(copy, x);
    return copy;
}
