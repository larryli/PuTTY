/*
 * sbcs.c - routines to handle single-byte character sets.
 */

#include "charset.h"
#include "internal.h"

/*
 * The charset_spec for any single-byte character set should
 * provide read_sbcs() as its read function, and its `data' field
 * should be a wchar_t string constant containing the 256 entries
 * of the translation table.
 */

void read_sbcs(charset_spec const *charset, long int input_chr,
	       charset_state *state,
	       void (*emit)(void *ctx, long int output), void *emitctx)
{
    const struct sbcs_data *sd = charset->data;

    UNUSEDARG(state);

    emit(emitctx, sd->sbcs2ucs[input_chr]);
}

void write_sbcs(charset_spec const *charset, long int input_chr,
		charset_state *state,
		void (*emit)(void *ctx, long int output), void *emitctx)
{
    const struct sbcs_data *sd = charset->data;
    int i;

    UNUSEDARG(state);

    /*
     * FIXME: this should work, but it's ludicrously inefficient.
     * We should be using the ucs2sbcs table.
     */
    for (i = 0; i < 256; i++)
	if (sd->sbcs2ucs[i] == input_chr) {
	    emit(emitctx, i);
	    return;
	}
    emit(emitctx, ERROR);
}
