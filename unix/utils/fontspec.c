/*
 * Implementation of FontSpec for Unix. This is more or less
 * degenerate - on this platform a font specification is just a
 * string.
 */

#include "putty.h"

FontSpec *fontspec_new(const char *name)
{
    FontSpec *f = snew(FontSpec);
    f->name = dupstr(name);
    return f;
}

FontSpec *fontspec_copy(const FontSpec *f)
{
    return fontspec_new(f->name);
}

void fontspec_free(FontSpec *f)
{
    sfree(f->name);
    sfree(f);
}

void fontspec_serialise(BinarySink *bs, FontSpec *f)
{
    put_asciz(bs, f->name);
}

FontSpec *fontspec_deserialise(BinarySource *src)
{
    return fontspec_new(get_asciz(src));
}
