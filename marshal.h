#ifndef PUTTY_MARSHAL_H
#define PUTTY_MARSHAL_H

#include "defs.h"

/*
 * A sort of 'abstract base class' or 'interface' or 'trait' which is
 * the common feature of all types that want to accept data formatted
 * using the SSH binary conventions of uint32, string, mpint etc.
 */
struct BinarySink {
    void (*write)(BinarySink *sink, const void *data, size_t len);
    BinarySink *binarysink_;
};

/*
 * To define a structure type as a valid target for binary formatted
 * data, put 'BinarySink_IMPLEMENTATION' in its declaration, and when
 * an instance is set up, use 'BinarySink_INIT' to initialise the
 * 'base class' state, providing a function pointer to be the
 * implementation of the write() call above.
 */
#define BinarySink_IMPLEMENTATION BinarySink binarysink_[1]
#define BinarySink_INIT(obj, writefn) \
    ((obj)->binarysink_->write = (writefn), \
     (obj)->binarysink_->binarysink_ = (obj)->binarysink_)

/*
 * The implementing type's write function will want to downcast its
 * 'BinarySink *' parameter back to the more specific type. Also,
 * sometimes you'll want to upcast a pointer to a particular
 * implementing type into an abstract 'BinarySink *' to pass to
 * generic subroutines not defined in this file. These macros do that
 * job.
 *
 * Importantly, BinarySink_UPCAST can also be applied to a BinarySink
 * * itself (and leaves it unchanged). That's achieved by a small
 * piece of C trickery: implementing structures and the BinarySink
 * structure itself both contain a field called binarysink_, but in
 * implementing objects it's a BinarySink[1] whereas in the abstract
 * type it's a 'BinarySink *' pointing back to the same structure,
 * meaning that you can say 'foo->binarysink_' in either case and get
 * a pointer type by different methods.
 */
#define BinarySink_DOWNCAST(object, type)                               \
    TYPECHECK((object) == ((type *)0)->binarysink_,                     \
              ((type *)(((char *)(object)) - offsetof(type, binarysink_))))
#define BinarySink_UPCAST(object)                                       \
    TYPECHECK((object)->binarysink_ == (BinarySink *)0,                 \
              (object)->binarysink_)

/*
 * If you structure-copy an object that's implementing BinarySink,
 * then that tricky self-pointer in its trait subobject will point to
 * the wrong place. You could call BinarySink_INIT again, but this
 * macro is terser and does all that's needed to fix up the copied
 * object.
 */
#define BinarySink_COPIED(obj) \
    ((obj)->binarysink_->binarysink_ = (obj)->binarysink_)

/*
 * The put_* macros are the main client to this system. Any structure
 * which implements the BinarySink 'trait' is valid for use as the
 * first parameter of any of these put_* macros.
 */

/* Basic big-endian integer types. uint64 is the structure type
 * defined in int64.h, not the C99 built-in type. */
#define put_byte(bs, val) \
    BinarySink_put_byte(BinarySink_UPCAST(bs), val)
#define put_uint16(bs, val) \
    BinarySink_put_uint16(BinarySink_UPCAST(bs), val)
#define put_uint32(bs, val) \
    BinarySink_put_uint32(BinarySink_UPCAST(bs), val)
#define put_uint64(bs, val) \
    BinarySink_put_uint64(BinarySink_UPCAST(bs), val)

/* SSH booleans, encoded as a single byte storing either 0 or 1. */
#define put_bool(bs, val) \
    BinarySink_put_bool(BinarySink_UPCAST(bs), val)

/* SSH strings, with a leading uint32 length field. 'stringz' is a
 * convenience function that takes an ordinary C zero-terminated
 * string as input. 'stringsb' takes a strbuf * as input, and
 * finalises it as a side effect (handy for multi-level marshalling in
 * which you use these same functions to format an inner blob of data
 * that then gets wrapped into a string container in an outer one). */
#define put_string(bs, val, len) \
    BinarySink_put_string(BinarySink_UPCAST(bs),val,len)
#define put_stringz(bs, val) \
    BinarySink_put_stringz(BinarySink_UPCAST(bs), val)
#define put_stringsb(bs, val) \
    BinarySink_put_stringsb(BinarySink_UPCAST(bs), val)

/* Other string outputs: 'asciz' emits the string data directly into
 * the output including the terminating \0, and 'pstring' emits the
 * string in Pascal style with a leading _one_-byte length field.
 * pstring can fail if the string is too long. */
#define put_asciz(bs, val) \
    BinarySink_put_asciz(BinarySink_UPCAST(bs), val)
#define put_pstring(bs, val) \
    BinarySink_put_pstring(BinarySink_UPCAST(bs), val)

/* Multiprecision integers, in both the SSH-1 and SSH-2 formats. */
#define put_mp_ssh1(bs, val) \
    BinarySink_put_mp_ssh1(BinarySink_UPCAST(bs), val)
#define put_mp_ssh2(bs, val) \
    BinarySink_put_mp_ssh2(BinarySink_UPCAST(bs), val)

/* Fallback: just emit raw data bytes, using a syntax that matches the
 * rest of these macros. */
#define put_data(bs, val, len) \
    BinarySink_put_data(BinarySink_UPCAST(bs), val, len)

/*
 * The underlying real C functions that implement most of those
 * macros. Generally you won't want to call these directly, because
 * they have such cumbersome names; you call the wrapper macros above
 * instead.
 *
 * A few functions whose wrapper macros are defined above are actually
 * declared in other headers, so as to guarantee that the
 * declaration(s) of their other parameter type(s) are in scope.
 */
void BinarySink_put_data(BinarySink *, const void *data, size_t len);
void BinarySink_put_byte(BinarySink *, unsigned char);
void BinarySink_put_bool(BinarySink *, int);
void BinarySink_put_uint16(BinarySink *, unsigned long);
void BinarySink_put_uint32(BinarySink *, unsigned long);
void BinarySink_put_string(BinarySink *, const void *data, size_t len);
void BinarySink_put_stringz(BinarySink *, const char *str);
struct strbuf;
void BinarySink_put_stringsb(BinarySink *, struct strbuf *);
void BinarySink_put_asciz(BinarySink *, const char *str);
int BinarySink_put_pstring(BinarySink *, const char *str);

#endif /* PUTTY_MARSHAL_H */
