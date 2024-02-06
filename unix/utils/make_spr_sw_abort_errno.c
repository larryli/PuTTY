/*
 * Constructor function for a SeatPromptResult of the 'software abort'
 * category, whose error message includes the translation of an OS
 * error code.
 */

#include "putty.h"

static void spr_errno_errfn(SeatPromptResult spr, BinarySink *bs)
{
    put_fmt(bs, "%s: %s", spr.errdata_lit, strerror(spr.errdata_u));
}

SeatPromptResult make_spr_sw_abort_errno(const char *prefix, int errno_value)
{
    SeatPromptResult spr;
    spr.kind = SPRK_SW_ABORT;
    spr.errfn = spr_errno_errfn;
    spr.errdata_lit = prefix;
    spr.errdata_u = errno_value;
    return spr;
}
