#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "marshal.h"
#include "misc.h"
#include "int64.h"

void BinarySink_put_data(BinarySink *bs, const void *data, size_t len)
{
    bs->write(bs, data, len);
}

void BinarySink_put_byte(BinarySink *bs, unsigned char val)
{
    bs->write(bs, &val, 1);
}

void BinarySink_put_bool(BinarySink *bs, int val)
{
    unsigned char cval = val ? 1 : 0;
    bs->write(bs, &cval, 1);
}

void BinarySink_put_uint16(BinarySink *bs, unsigned long val)
{
    unsigned char data[2];
    PUT_16BIT_MSB_FIRST(data, val);
    bs->write(bs, data, sizeof(data));
}

void BinarySink_put_uint32(BinarySink *bs, unsigned long val)
{
    unsigned char data[4];
    PUT_32BIT_MSB_FIRST(data, val);
    bs->write(bs, data, sizeof(data));
}

void BinarySink_put_uint64(BinarySink *bs, uint64 val)
{
    BinarySink_put_uint32(bs, val.hi);
    BinarySink_put_uint32(bs, val.lo);
}

void BinarySink_put_string(BinarySink *bs, const void *data, size_t len)
{
    /* Check that the string length fits in a uint32, without doing a
     * potentially implementation-defined shift of more than 31 bits */
    assert((len >> 31) < 2);

    BinarySink_put_uint32(bs, len);
    bs->write(bs, data, len);
}

void BinarySink_put_stringpl(BinarySink *bs, ptrlen pl)
{
    BinarySink_put_string(bs, pl.ptr, pl.len);
}

void BinarySink_put_stringz(BinarySink *bs, const char *str)
{
    BinarySink_put_string(bs, str, strlen(str));
}

void BinarySink_put_stringsb(BinarySink *bs, struct strbuf *buf)
{
    BinarySink_put_string(bs, buf->s, buf->len);
    strbuf_free(buf);
}

void BinarySink_put_asciz(BinarySink *bs, const char *str)
{
    bs->write(bs, str, strlen(str) + 1);
}

int BinarySink_put_pstring(BinarySink *bs, const char *str)
{
    size_t len = strlen(str);
    if (len > 255)
        return FALSE; /* can't write a Pascal-style string this long */
    BinarySink_put_byte(bs, len);
    bs->write(bs, str, len);
    return TRUE;
}
