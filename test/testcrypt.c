/*
 * testcrypt: a standalone test program that provides direct access to
 * PuTTY's cryptography and mp_int code.
 */

/*
 * This program speaks a line-oriented protocol on standard input and
 * standard output. It's a half-duplex protocol: it expects to read
 * one line of command, and then produce a fixed amount of output
 * (namely a line containing a decimal integer, followed by that many
 * lines each containing one return value).
 *
 * The protocol is human-readable enough to make it debuggable, but
 * verbose enough that you probably wouldn't want to speak it by hand
 * at any great length. The Python program test/testcrypt.py wraps it
 * to give a more useful user-facing API, by invoking this binary as a
 * subprocess.
 *
 * (I decided that was a better idea than making this program an
 * actual Python module, partly because you can rewrap the same binary
 * in another scripting language if you prefer, but mostly because
 * it's easy to attach a debugger to testcrypt or to run it under
 * sanitisers or valgrind or what have you.)
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "defs.h"
#include "ssh.h"
#include "sshkeygen.h"
#include "misc.h"
#include "mpint.h"
#include "crypto/ecc.h"
#include "crypto/ntru.h"
#include "proxy/cproxy.h"

static NORETURN PRINTF_LIKE(1, 2) void fatal_error(const char *p, ...)
{
    va_list ap;
    fprintf(stderr, "testcrypt: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

void out_of_memory(void) { fatal_error("out of memory"); }

static bool old_keyfile_warning_given;
void old_keyfile_warning(void) { old_keyfile_warning_given = true; }

static bufchain random_data_queue;
static prng *test_prng;
void random_read(void *buf, size_t size)
{
    if (test_prng) {
        prng_read(test_prng, buf, size);
    } else {
        if (!bufchain_try_fetch_consume(&random_data_queue, buf, size))
            fatal_error("No random data in queue");
    }
}

uint64_t prng_reseed_time_ms(void)
{
    static uint64_t previous_time = 0;
    return previous_time += 200;
}

#define VALUE_TYPES(X)                                                  \
    X(string, strbuf *, strbuf_free(v))                                 \
    X(mpint, mp_int *, mp_free(v))                                      \
    X(modsqrt, ModsqrtContext *, modsqrt_free(v))                       \
    X(monty, MontyContext *, monty_free(v))                             \
    X(wcurve, WeierstrassCurve *, ecc_weierstrass_curve_free(v))        \
    X(wpoint, WeierstrassPoint *, ecc_weierstrass_point_free(v))        \
    X(mcurve, MontgomeryCurve *, ecc_montgomery_curve_free(v))          \
    X(mpoint, MontgomeryPoint *, ecc_montgomery_point_free(v))          \
    X(ecurve, EdwardsCurve *, ecc_edwards_curve_free(v))                \
    X(epoint, EdwardsPoint *, ecc_edwards_point_free(v))                \
    X(hash, ssh_hash *, ssh_hash_free(v))                               \
    X(key, ssh_key *, ssh_key_free(v))                                  \
    X(cipher, ssh_cipher *, ssh_cipher_free(v))                         \
    X(mac, ssh2_mac *, ssh2_mac_free(v))                                \
    X(dh, dh_ctx *, dh_cleanup(v))                                      \
    X(ecdh, ecdh_key *, ecdh_key_free(v))                               \
    X(rsakex, RSAKey *, ssh_rsakex_freekey(v))                          \
    X(rsa, RSAKey *, rsa_free(v))                                       \
    X(prng, prng *, prng_free(v))                                       \
    X(keycomponents, key_components *, key_components_free(v))          \
    X(pcs, PrimeCandidateSource *, pcs_free(v))                         \
    X(pgc, PrimeGenerationContext *, primegen_free_context(v))          \
    X(pockle, Pockle *, pockle_free(v))                                 \
    X(millerrabin, MillerRabin *, miller_rabin_free(v))                 \
    X(ntrukeypair, NTRUKeyPair *, ntru_keypair_free(v))                 \
    X(ntruencodeschedule, NTRUEncodeSchedule *, ntru_encode_schedule_free(v)) \
    /* end of list */

typedef struct Value Value;

enum ValueType {
#define VALTYPE_ENUM(n,t,f) VT_##n,
    VALUE_TYPES(VALTYPE_ENUM)
#undef VALTYPE_ENUM
};

typedef enum ValueType ValueType;

static const char *const type_names[] = {
#define VALTYPE_NAME(n,t,f) #n,
    VALUE_TYPES(VALTYPE_NAME)
#undef VALTYPE_NAME
};

#define VALTYPE_TYPEDEF(n,t,f)                  \
    typedef t TD_val_##n;                       \
    typedef t *TD_out_val_##n;
VALUE_TYPES(VALTYPE_TYPEDEF)
#undef VALTYPE_TYPEDEF

struct Value {
    /*
     * Protocol identifier assigned to this value when it was created.
     * Lives in the same malloced block as this Value object itself.
     */
    ptrlen id;

    /*
     * Type of the value.
     */
    ValueType type;

    /*
     * Union of all the things it could hold.
     */
    union {
#define VALTYPE_UNION(n,t,f) t vu_##n;
        VALUE_TYPES(VALTYPE_UNION)
#undef VALTYPE_UNION

        char *bare_string;
    };
};

static int valuecmp(void *av, void *bv)
{
    Value *a = (Value *)av, *b = (Value *)bv;
    return ptrlen_strcmp(a->id, b->id);
}

static int valuefind(void *av, void *bv)
{
    ptrlen *a = (ptrlen *)av;
    Value *b = (Value *)bv;
    return ptrlen_strcmp(*a, b->id);
}

static tree234 *values;

static Value *value_new(ValueType vt)
{
    static uint64_t next_index = 0;

    char *name = dupprintf("%s%"PRIu64, type_names[vt], next_index++);
    size_t namelen = strlen(name);

    Value *val = snew_plus(Value, namelen+1);
    memcpy(snew_plus_get_aux(val), name, namelen+1);
    val->id.ptr = snew_plus_get_aux(val);
    val->id.len = namelen;
    val->type = vt;

    Value *added = add234(values, val);
    assert(added == val);

    sfree(name);

    return val;
}

#define VALTYPE_RETURNFN(n,t,f)                                 \
    void return_val_##n(strbuf *out, t v) {                     \
        Value *val = value_new(VT_##n);                         \
        val->vu_##n = v;                                        \
        put_datapl(out, val->id);                               \
        put_byte(out, '\n');                                    \
    }
VALUE_TYPES(VALTYPE_RETURNFN)
#undef VALTYPE_RETURNFN

static ptrlen get_word(BinarySource *in)
{
    ptrlen toret;
    toret.ptr = get_ptr(in);
    toret.len = 0;
    while (get_avail(in) && get_byte(in) != ' ')
        toret.len++;
    return toret;
}

typedef uintmax_t TD_uint;
typedef bool TD_boolean;
typedef ptrlen TD_val_string_ptrlen;
typedef char *TD_val_string_asciz;
typedef BinarySource *TD_val_string_binarysource;
typedef unsigned *TD_out_uint;
typedef BinarySink *TD_out_val_string_binarysink;
typedef const char *TD_opt_val_string_asciz;
typedef char **TD_out_val_string_asciz;
typedef char **TD_out_opt_val_string_asciz;
typedef const char **TD_out_opt_val_string_asciz_const;
typedef const ssh_hashalg *TD_hashalg;
typedef const ssh2_macalg *TD_macalg;
typedef const ssh_keyalg *TD_keyalg;
typedef const ssh_cipheralg *TD_cipheralg;
typedef const ssh_kex *TD_dh_group;
typedef const ssh_kex *TD_ecdh_alg;
typedef RsaSsh1Order TD_rsaorder;
typedef key_components *TD_keycomponents;
typedef const PrimeGenerationPolicy *TD_primegenpolicy;
typedef struct mpint_list TD_mpint_list;
typedef struct int16_list *TD_int16_list;
typedef PockleStatus TD_pocklestatus;
typedef struct mr_result TD_mr_result;
typedef Argon2Flavour TD_argon2flavour;
typedef FingerprintType TD_fptype;
typedef HttpDigestHash TD_httpdigesthash;

#define BEGIN_ENUM_TYPE(name)                                           \
    static bool enum_translate_##name(ptrlen valname, TD_##name *out) { \
        static const struct {                                           \
            const char *key;                                            \
            TD_##name value;                                            \
        } mapping[] = {
#define ENUM_VALUE(name, value) {name, value},
#define END_ENUM_TYPE(name)                                             \
        };                                                              \
        for (size_t i = 0; i < lenof(mapping); i++)                     \
            if (ptrlen_eq_string(valname, mapping[i].key)) {            \
                if (out)                                                \
                    *out = mapping[i].value;                            \
                return true;                                            \
            }                                                           \
        return false;                                                   \
    }                                                                   \
                                                                        \
    static TD_##name get_##name(BinarySource *in) {                     \
        ptrlen valname = get_word(in);                                  \
        TD_##name out;                                                  \
        if (enum_translate_##name(valname, &out))                       \
            return out;                                                 \
        else                                                            \
            fatal_error("%s '%.*s': not found",                         \
                        #name, PTRLEN_PRINTF(valname));                 \
    }
#include "testcrypt-enum.h"
#undef BEGIN_ENUM_TYPE
#undef ENUM_VALUE
#undef END_ENUM_TYPE

static uintmax_t get_uint(BinarySource *in)
{
    ptrlen word = get_word(in);
    char *string = mkstr(word);
    uintmax_t toret = strtoumax(string, NULL, 0);
    sfree(string);
    return toret;
}

static bool get_boolean(BinarySource *in)
{
    return ptrlen_eq_string(get_word(in), "true");
}

static Value *lookup_value(ptrlen word)
{
    Value *val = find234(values, &word, valuefind);
    if (!val)
        fatal_error("id '%.*s': not found", PTRLEN_PRINTF(word));
    return val;
}

static Value *get_value(BinarySource *in)
{
    return lookup_value(get_word(in));
}

typedef void (*finaliser_fn_t)(strbuf *out, void *ctx);
struct finaliser {
    finaliser_fn_t fn;
    void *ctx;
};

static struct finaliser *finalisers;
static size_t nfinalisers, finalisersize;

static void add_finaliser(finaliser_fn_t fn, void *ctx)
{
    sgrowarray(finalisers, finalisersize, nfinalisers);
    finalisers[nfinalisers].fn = fn;
    finalisers[nfinalisers].ctx = ctx;
    nfinalisers++;
}

static void run_finalisers(strbuf *out)
{
    for (size_t i = 0; i < nfinalisers; i++)
        finalisers[i].fn(out, finalisers[i].ctx);
    nfinalisers = 0;
}

static void finaliser_return_value(strbuf *out, void *ctx)
{
    Value *val = (Value *)ctx;
    put_datapl(out, val->id);
    put_byte(out, '\n');
}

static void finaliser_sfree(strbuf *out, void *ctx)
{
    sfree(ctx);
}

#define VALTYPE_GETFN(n,t,f)                                            \
    static Value *unwrap_value_##n(Value *val) {                        \
        ValueType expected = VT_##n;                                    \
        if (expected != val->type)                                      \
            fatal_error("id '%.*s': expected %s, got %s",             \
                          PTRLEN_PRINTF(val->id),                       \
                          type_names[expected], type_names[val->type]); \
        return val;                                                     \
    }                                                                   \
    static Value *get_value_##n(BinarySource *in) {                     \
        return unwrap_value_##n(get_value(in));                         \
    }                                                                   \
    static t get_val_##n(BinarySource *in) {                            \
        return get_value_##n(in)->vu_##n;                               \
    }
VALUE_TYPES(VALTYPE_GETFN)
#undef VALTYPE_GETFN

static ptrlen get_val_string_ptrlen(BinarySource *in)
{
    return ptrlen_from_strbuf(get_val_string(in));
}

static char *get_val_string_asciz(BinarySource *in)
{
    return get_val_string(in)->s;
}

static strbuf *get_opt_val_string(BinarySource *in);

static char *get_opt_val_string_asciz(BinarySource *in)
{
    strbuf *sb = get_opt_val_string(in);
    return sb ? sb->s : NULL;
}

static mp_int **get_out_val_mpint(BinarySource *in)
{
    Value *val = value_new(VT_mpint);
    add_finaliser(finaliser_return_value, val);
    return &val->vu_mpint;
}

struct mpint_list {
    size_t n;
    mp_int **integers;
};

static struct mpint_list get_mpint_list(BinarySource *in)
{
    size_t n = get_uint(in);

    struct mpint_list mpl;
    mpl.n = n;

    mpl.integers = snewn(n, mp_int *);
    for (size_t i = 0; i < n; i++)
        mpl.integers[i] = get_val_mpint(in);

    add_finaliser(finaliser_sfree, mpl.integers);
    return mpl;
}

typedef struct int16_list {
    size_t n;
    uint16_t *integers;
} int16_list;

static void finaliser_int16_list_free(strbuf *out, void *vlist)
{
    int16_list *list = (int16_list *)vlist;
    sfree(list->integers);
    sfree(list);
}

static int16_list *make_int16_list(size_t n)
{
    int16_list *list = snew(int16_list);
    list->n = n;
    list->integers = snewn(n, uint16_t);
    add_finaliser(finaliser_int16_list_free, list);
    return list;
}

static int16_list *get_int16_list(BinarySource *in)
{
    size_t n = get_uint(in);
    int16_list *list = make_int16_list(n);
    for (size_t i = 0; i < n; i++)
        list->integers[i] = get_uint(in);
    return list;
}

static void return_int16_list(strbuf *out, int16_list *list)
{
    for (size_t i = 0; i < list->n; i++) {
        if (i > 0)
            put_byte(out, ',');
        put_fmt(out, "%d", (int)(int16_t)list->integers[i]);
    }
    put_byte(out, '\n');
}

static void finaliser_return_uint(strbuf *out, void *ctx)
{
    unsigned *uval = (unsigned *)ctx;
    put_fmt(out, "%u\n", *uval);
    sfree(uval);
}

static unsigned *get_out_uint(BinarySource *in)
{
    unsigned *uval = snew(unsigned);
    add_finaliser(finaliser_return_uint, uval);
    return uval;
}

static BinarySink *get_out_val_string_binarysink(BinarySource *in)
{
    Value *val = value_new(VT_string);
    val->vu_string = strbuf_new();
    add_finaliser(finaliser_return_value, val);
    return BinarySink_UPCAST(val->vu_string);
}

static void return_val_string_asciz_const(strbuf *out, const char *s);
static void return_val_string_asciz(strbuf *out, char *s);

static void finaliser_return_opt_string_asciz(strbuf *out, void *ctx)
{
    char **valp = (char **)ctx;
    char *val = *valp;
    sfree(valp);
    if (!val)
        put_fmt(out, "NULL\n");
    else
        return_val_string_asciz(out, val);
}

static char **get_out_opt_val_string_asciz(BinarySource *in)
{
    char **valp = snew(char *);
    *valp = NULL;
    add_finaliser(finaliser_return_opt_string_asciz, valp);
    return valp;
}

static void finaliser_return_opt_string_asciz_const(strbuf *out, void *ctx)
{
    const char **valp = (const char **)ctx;
    const char *val = *valp;
    sfree(valp);
    if (!val)
        put_fmt(out, "NULL\n");
    else
        return_val_string_asciz_const(out, val);
}

static const char **get_out_opt_val_string_asciz_const(BinarySource *in)
{
    const char **valp = snew(const char *);
    *valp = NULL;
    add_finaliser(finaliser_return_opt_string_asciz_const, valp);
    return valp;
}

static BinarySource *get_val_string_binarysource(BinarySource *in)
{
    strbuf *sb = get_val_string(in);
    BinarySource *src = snew(BinarySource);
    BinarySource_BARE_INIT(src, sb->u, sb->len);
    add_finaliser(finaliser_sfree, src);
    return src;
}

#define GET_CONSUMED_FN(type)                                           \
    typedef TD_val_##type TD_consumed_val_##type;                       \
    static TD_val_##type get_consumed_val_##type(BinarySource *in)      \
    {                                                                   \
        Value *val = get_value_##type(in);                              \
        TD_val_##type toret = val->vu_##type;                           \
        del234(values, val);                                            \
        sfree(val);                                                     \
        return toret;                                                   \
    }
GET_CONSUMED_FN(hash)
GET_CONSUMED_FN(pcs)

static void return_int(strbuf *out, intmax_t u)
{
    put_fmt(out, "%"PRIdMAX"\n", u);
}

static void return_uint(strbuf *out, uintmax_t u)
{
    put_fmt(out, "0x%"PRIXMAX"\n", u);
}

static void return_boolean(strbuf *out, bool b)
{
    put_fmt(out, "%s\n", b ? "true" : "false");
}

static void return_pocklestatus(strbuf *out, PockleStatus status)
{
    switch (status) {
      default:
        put_fmt(out, "POCKLE_BAD_STATUS_VALUE\n");
        break;

#define STATUS_CASE(id)                         \
      case id:                                  \
        put_fmt(out, "%s\n", #id);          \
        break;

        POCKLE_STATUSES(STATUS_CASE);

#undef STATUS_CASE

    }
}

static void return_mr_result(strbuf *out, struct mr_result result)
{
    if (!result.passed)
        put_fmt(out, "failed\n");
    else if (!result.potential_primitive_root)
        put_fmt(out, "passed\n");
    else
        put_fmt(out, "passed+ppr\n");
}

static void return_val_string_asciz_const(strbuf *out, const char *s)
{
    strbuf *sb = strbuf_new();
    put_data(sb, s, strlen(s));
    return_val_string(out, sb);
}

static void return_val_string_asciz(strbuf *out, char *s)
{
    return_val_string_asciz_const(out, s);
    sfree(s);
}

#define NULLABLE_RETURN_WRAPPER(type_name, c_type)                      \
    static void return_opt_##type_name(strbuf *out, c_type ptr)         \
    {                                                                   \
        if (!ptr)                                                       \
            put_fmt(out, "NULL\n");                                     \
        else                                                            \
            return_##type_name(out, ptr);                               \
    }

NULLABLE_RETURN_WRAPPER(val_string, strbuf *)
NULLABLE_RETURN_WRAPPER(val_string_asciz, char *)
NULLABLE_RETURN_WRAPPER(val_string_asciz_const, const char *)
NULLABLE_RETURN_WRAPPER(val_cipher, ssh_cipher *)
NULLABLE_RETURN_WRAPPER(val_hash, ssh_hash *)
NULLABLE_RETURN_WRAPPER(val_key, ssh_key *)
NULLABLE_RETURN_WRAPPER(val_mpint, mp_int *)
NULLABLE_RETURN_WRAPPER(int16_list, int16_list *)

static void handle_hello(BinarySource *in, strbuf *out)
{
    put_fmt(out, "hello, world\n");
}

static void rsa_free(RSAKey *rsa)
{
    freersakey(rsa);
    sfree(rsa);
}

static void free_value(Value *val)
{
    switch (val->type) {
#define VALTYPE_FREE(n,t,f) case VT_##n: { t v = val->vu_##n; (f); break; }
        VALUE_TYPES(VALTYPE_FREE)
#undef VALTYPE_FREE
    }
    sfree(val);
}

static void handle_free(BinarySource *in, strbuf *out)
{
    Value *val = get_value(in);
    del234(values, val);
    free_value(val);
}

static void handle_newstring(BinarySource *in, strbuf *out)
{
    strbuf *sb = strbuf_new();
    while (get_avail(in)) {
        char c = get_byte(in);
        if (c == '%') {
            char hex[3];
            hex[0] = get_byte(in);
            if (hex[0] != '%') {
                hex[1] = get_byte(in);
                hex[2] = '\0';
                c = strtoul(hex, NULL, 16);
            }
        }
        put_byte(sb, c);
    }
    return_val_string(out, sb);
}

static void handle_getstring(BinarySource *in, strbuf *out)
{
    strbuf *sb = get_val_string(in);
    for (size_t i = 0; i < sb->len; i++) {
        char c = sb->s[i];
        if (c > ' ' && c < 0x7F && c != '%') {
            put_byte(out, c);
        } else {
            put_fmt(out, "%%%02X", 0xFFU & (unsigned)c);
        }
    }
    put_byte(out, '\n');
}

static void handle_mp_literal(BinarySource *in, strbuf *out)
{
    ptrlen pl = get_word(in);
    char *str = mkstr(pl);
    mp_int *mp = mp__from_string_literal(str);
    sfree(str);
    return_val_mpint(out, mp);
}

static void handle_mp_dump(BinarySource *in, strbuf *out)
{
    mp_int *mp = get_val_mpint(in);
    for (size_t i = mp_max_bytes(mp); i-- > 0 ;)
        put_fmt(out, "%02X", mp_get_byte(mp, i));
    put_byte(out, '\n');
}

static void handle_checkenum(BinarySource *in, strbuf *out)
{
    ptrlen type = get_word(in);
    ptrlen value = get_word(in);
    bool ok = false;

    #define BEGIN_ENUM_TYPE(name) \
    if (ptrlen_eq_string(type, #name))                  \
        ok = enum_translate_##name(value, NULL);
    #define ENUM_VALUE(name, value)
    #define END_ENUM_TYPE(name)
    #include "testcrypt-enum.h"
    #undef BEGIN_ENUM_TYPE
    #undef ENUM_VALUE
    #undef END_ENUM_TYPE

    put_dataz(out, ok ? "ok\n" : "bad\n");
}

static void random_queue(ptrlen pl)
{
    bufchain_add(&random_data_queue, pl.ptr, pl.len);
}

static size_t random_queue_len(void)
{
    return bufchain_size(&random_data_queue);
}

static void random_clear(void)
{
    if (test_prng) {
        prng_free(test_prng);
        test_prng = NULL;
    }

    bufchain_clear(&random_data_queue);
}

static void random_make_prng(const ssh_hashalg *hashalg, ptrlen seed)
{
    random_clear();

    test_prng = prng_new(hashalg);
    prng_seed_begin(test_prng);
    put_datapl(test_prng, seed);
    prng_seed_finish(test_prng);
}

mp_int *monty_identity_wrapper(MontyContext *mc)
{
    return mp_copy(monty_identity(mc));
}

mp_int *monty_modulus_wrapper(MontyContext *mc)
{
    return mp_copy(monty_modulus(mc));
}

strbuf *ssh_hash_digest_wrapper(ssh_hash *h)
{
    strbuf *sb = strbuf_new();
    void *p = strbuf_append(sb, ssh_hash_alg(h)->hlen);
    ssh_hash_digest(h, p);
    return sb;
}

strbuf *ssh_hash_final_wrapper(ssh_hash *h)
{
    strbuf *sb = strbuf_new();
    void *p = strbuf_append(sb, ssh_hash_alg(h)->hlen);
    ssh_hash_final(h, p);
    return sb;
}

void ssh_cipher_setiv_wrapper(ssh_cipher *c, ptrlen iv)
{
    if (iv.len != ssh_cipher_alg(c)->blksize)
        fatal_error("ssh_cipher_setiv: needs exactly %d bytes",
                    ssh_cipher_alg(c)->blksize);
    ssh_cipher_setiv(c, iv.ptr);
}

void ssh_cipher_setkey_wrapper(ssh_cipher *c, ptrlen key)
{
    if (key.len != ssh_cipher_alg(c)->padded_keybytes)
        fatal_error("ssh_cipher_setkey: needs exactly %d bytes",
                    ssh_cipher_alg(c)->padded_keybytes);
    ssh_cipher_setkey(c, key.ptr);
}

strbuf *ssh_cipher_encrypt_wrapper(ssh_cipher *c, ptrlen input)
{
    if (input.len % ssh_cipher_alg(c)->blksize)
        fatal_error("ssh_cipher_encrypt: needs a multiple of %d bytes",
                    ssh_cipher_alg(c)->blksize);
    strbuf *sb = strbuf_dup(input);
    ssh_cipher_encrypt(c, sb->u, sb->len);
    return sb;
}

strbuf *ssh_cipher_decrypt_wrapper(ssh_cipher *c, ptrlen input)
{
    if (input.len % ssh_cipher_alg(c)->blksize)
        fatal_error("ssh_cipher_decrypt: needs a multiple of %d bytes",
                    ssh_cipher_alg(c)->blksize);
    strbuf *sb = strbuf_dup(input);
    ssh_cipher_decrypt(c, sb->u, sb->len);
    return sb;
}

strbuf *ssh_cipher_encrypt_length_wrapper(ssh_cipher *c, ptrlen input,
                                          unsigned long seq)
{
    if (input.len != 4)
        fatal_error("ssh_cipher_encrypt_length: needs exactly 4 bytes");
    strbuf *sb = strbuf_dup(input);
    ssh_cipher_encrypt_length(c, sb->u, sb->len, seq);
    return sb;
}

strbuf *ssh_cipher_decrypt_length_wrapper(ssh_cipher *c, ptrlen input,
                                          unsigned long seq)
{
    if (input.len != 4)
        fatal_error("ssh_cipher_decrypt_length: needs exactly 4 bytes");
    strbuf *sb = strbuf_dup(input);
    ssh_cipher_decrypt_length(c, sb->u, sb->len, seq);
    return sb;
}

strbuf *ssh2_mac_genresult_wrapper(ssh2_mac *m)
{
    strbuf *sb = strbuf_new();
    void *u = strbuf_append(sb, ssh2_mac_alg(m)->len);
    ssh2_mac_genresult(m, u);
    return sb;
}

ssh_key *ssh_key_base_key_wrapper(ssh_key *key)
{
    /* To avoid having to explain the borrowed reference to Python,
     * just clone the key unconditionally */
    return ssh_key_clone(ssh_key_base_key(key));
}

void ssh_key_ca_public_blob_wrapper(ssh_key *key, BinarySink *out)
{
    /* Wrap to avoid null-pointer dereference */
    if (!key->vt->is_certificate)
        fatal_error("ssh_key_ca_public_blob: needs a certificate");
    ssh_key_ca_public_blob(key, out);
}

void ssh_key_cert_id_string_wrapper(ssh_key *key, BinarySink *out)
{
    /* Wrap to avoid null-pointer dereference */
    if (!key->vt->is_certificate)
        fatal_error("ssh_key_cert_id_string: needs a certificate");
    ssh_key_cert_id_string(key, out);
}

static bool ssh_key_check_cert_wrapper(
    ssh_key *key, bool host, ptrlen principal, uint64_t time, ptrlen optstr,
    BinarySink *error)
{
    /* Wrap to avoid null-pointer dereference */
    if (!key->vt->is_certificate)
        fatal_error("ssh_key_cert_id_string: needs a certificate");

    ca_options opts;
    opts.permit_rsa_sha1 = true;
    opts.permit_rsa_sha256 = true;
    opts.permit_rsa_sha512 = true;

    while (optstr.len) {
        ptrlen word = ptrlen_get_word(&optstr, ",");
        ptrlen key = word, value = PTRLEN_LITERAL("");
        const char *comma = memchr(word.ptr, '=', word.len);
        if (comma) {
            key.len = comma - (const char *)word.ptr;
            value.ptr = comma + 1;
            value.len = word.len - key.len - 1;
        }

        if (ptrlen_eq_string(key, "permit_rsa_sha1"))
            opts.permit_rsa_sha1 = ptrlen_eq_string(value, "true");
        if (ptrlen_eq_string(key, "permit_rsa_sha256"))
            opts.permit_rsa_sha256 = ptrlen_eq_string(value, "true");
        if (ptrlen_eq_string(key, "permit_rsa_sha512"))
            opts.permit_rsa_sha512 = ptrlen_eq_string(value, "true");
    }

    return ssh_key_check_cert(key, host, principal, time, &opts, error);
}

bool dh_validate_f_wrapper(dh_ctx *dh, mp_int *f)
{
    return dh_validate_f(dh, f) == NULL;
}

void ssh_hash_update(ssh_hash *h, ptrlen pl)
{
    put_datapl(h, pl);
}

void ssh2_mac_update(ssh2_mac *m, ptrlen pl)
{
    put_datapl(m, pl);
}

static RSAKey *rsa_new(void)
{
    RSAKey *rsa = snew(RSAKey);
    memset(rsa, 0, sizeof(RSAKey));
    return rsa;
}

strbuf *ecdh_key_getkey_wrapper(ecdh_key *ek, ptrlen remoteKey)
{
    /* Fold the boolean return value in C into the string return value
     * for this purpose, by returning NULL on failure */
    strbuf *sb = strbuf_new();
    if (!ecdh_key_getkey(ek, remoteKey, BinarySink_UPCAST(sb))) {
        strbuf_free(sb);
        return NULL;
    }
    return sb;
}

static void int16_list_resize(int16_list *list, unsigned p)
{
    list->integers = sresize(list->integers, p, uint16_t);
    for (size_t i = list->n; i < p; i++)
        list->integers[i] = 0;
}

#if 0
static int16_list ntru_ring_to_list_and_free(uint16_t *out, unsigned p)
{
    struct mpint_list mpl;
    mpl.n = p;
    mpl->integers = snewn(p, mp_int *);
    for (unsigned i = 0; i < p; i++)
        mpl->integers[i] = mp_from_integer((int16_t)out[i]);
    sfree(out);
    add_finaliser(finaliser_sfree, mpl->integers);
    return mpl;
}
#endif

int16_list *ntru_ring_multiply_wrapper(
    int16_list *a, int16_list *b, unsigned p, unsigned q)
{
    int16_list_resize(a, p);
    int16_list_resize(b, p);
    int16_list *out = make_int16_list(p);
    ntru_ring_multiply(out->integers, a->integers, b->integers, p, q);
    return out;
}

int16_list *ntru_ring_invert_wrapper(int16_list *in, unsigned p, unsigned q)
{
    int16_list_resize(in, p);
    int16_list *out = make_int16_list(p);
    unsigned success = ntru_ring_invert(out->integers, in->integers, p, q);
    if (!success)
        return NULL;
    return out;
}

int16_list *ntru_mod3_wrapper(int16_list *in, unsigned p, unsigned q)
{
    int16_list_resize(in, p);
    int16_list *out = make_int16_list(p);
    ntru_mod3(out->integers, in->integers, p, q);
    return out;
}

int16_list *ntru_round3_wrapper(int16_list *in, unsigned p, unsigned q)
{
    int16_list_resize(in, p);
    int16_list *out = make_int16_list(p);
    ntru_round3(out->integers, in->integers, p, q);
    return out;
}

int16_list *ntru_bias_wrapper(int16_list *in, unsigned bias,
                              unsigned p, unsigned q)
{
    int16_list_resize(in, p);
    int16_list *out = make_int16_list(p);
    ntru_bias(out->integers, in->integers, bias, p, q);
    return out;
}

int16_list *ntru_scale_wrapper(int16_list *in, unsigned scale,
                               unsigned p, unsigned q)
{
    int16_list_resize(in, p);
    int16_list *out = make_int16_list(p);
    ntru_scale(out->integers, in->integers, scale, p, q);
    return out;
}

NTRUEncodeSchedule *ntru_encode_schedule_wrapper(int16_list *in)
{
    return ntru_encode_schedule(in->integers, in->n);
}

void ntru_encode_wrapper(NTRUEncodeSchedule *sched, int16_list *rs,
                         BinarySink *bs)
{
    ntru_encode(sched, rs->integers, bs);
}

int16_list *ntru_decode_wrapper(NTRUEncodeSchedule *sched, ptrlen data)
{
    int16_list *out = make_int16_list(ntru_encode_schedule_nvals(sched));
    ntru_decode(sched, out->integers, data);
    return out;
}

int16_list *ntru_gen_short_wrapper(unsigned p, unsigned w)
{
    int16_list *out = make_int16_list(p);
    ntru_gen_short(out->integers, p, w);
    return out;
}

int16_list *ntru_pubkey_wrapper(NTRUKeyPair *keypair)
{
    unsigned p = ntru_keypair_p(keypair);
    int16_list *out = make_int16_list(p);
    memcpy(out->integers, ntru_pubkey(keypair), p*sizeof(uint16_t));
    return out;
}

int16_list *ntru_encrypt_wrapper(int16_list *plaintext, int16_list *pubkey,
                                 unsigned p, unsigned q)
{
    int16_list *out = make_int16_list(p);
    ntru_encrypt(out->integers, plaintext->integers, pubkey->integers, p, q);
    return out;
}

int16_list *ntru_decrypt_wrapper(int16_list *ciphertext, NTRUKeyPair *keypair)
{
    unsigned p = ntru_keypair_p(keypair);
    int16_list *out = make_int16_list(p);
    ntru_decrypt(out->integers, ciphertext->integers, keypair);
    return out;
}

strbuf *rsa_ssh1_encrypt_wrapper(ptrlen input, RSAKey *key)
{
    /* Fold the boolean return value in C into the string return value
     * for this purpose, by returning NULL on failure */
    strbuf *sb = strbuf_new();
    put_datapl(sb, input);
    put_padding(sb, key->bytes - input.len, 0);
    if (!rsa_ssh1_encrypt(sb->u, input.len, key)) {
        strbuf_free(sb);
        return NULL;
    }
    return sb;
}

strbuf *rsa_ssh1_decrypt_pkcs1_wrapper(mp_int *input, RSAKey *key)
{
    /* Again, return "" on failure */
    strbuf *sb = strbuf_new();
    if (!rsa_ssh1_decrypt_pkcs1(input, key, sb))
        strbuf_clear(sb);
    return sb;
}

strbuf *des_encrypt_xdmauth_wrapper(ptrlen key, ptrlen data)
{
    if (key.len != 7)
        fatal_error("des_encrypt_xdmauth: key must be 7 bytes long");
    if (data.len % 8 != 0)
        fatal_error("des_encrypt_xdmauth: data must be a multiple of 8 bytes");
    strbuf *sb = strbuf_dup(data);
    des_encrypt_xdmauth(key.ptr, sb->u, sb->len);
    return sb;
}

strbuf *des_decrypt_xdmauth_wrapper(ptrlen key, ptrlen data)
{
    if (key.len != 7)
        fatal_error("des_decrypt_xdmauth: key must be 7 bytes long");
    if (data.len % 8 != 0)
        fatal_error("des_decrypt_xdmauth: data must be a multiple of 8 bytes");
    strbuf *sb = strbuf_dup(data);
    des_decrypt_xdmauth(key.ptr, sb->u, sb->len);
    return sb;
}

strbuf *des3_encrypt_pubkey_wrapper(ptrlen key, ptrlen data)
{
    if (key.len != 16)
        fatal_error("des3_encrypt_pubkey: key must be 16 bytes long");
    if (data.len % 8 != 0)
        fatal_error("des3_encrypt_pubkey: data must be a multiple of 8 bytes");
    strbuf *sb = strbuf_dup(data);
    des3_encrypt_pubkey(key.ptr, sb->u, sb->len);
    return sb;
}

strbuf *des3_decrypt_pubkey_wrapper(ptrlen key, ptrlen data)
{
    if (key.len != 16)
        fatal_error("des3_decrypt_pubkey: key must be 16 bytes long");
    if (data.len % 8 != 0)
        fatal_error("des3_decrypt_pubkey: data must be a multiple of 8 bytes");
    strbuf *sb = strbuf_dup(data);
    des3_decrypt_pubkey(key.ptr, sb->u, sb->len);
    return sb;
}

strbuf *des3_encrypt_pubkey_ossh_wrapper(ptrlen key, ptrlen iv, ptrlen data)
{
    if (key.len != 24)
        fatal_error("des3_encrypt_pubkey_ossh: key must be 24 bytes long");
    if (iv.len != 8)
        fatal_error("des3_encrypt_pubkey_ossh: iv must be 8 bytes long");
    if (data.len % 8 != 0)
        fatal_error("des3_encrypt_pubkey_ossh: data must be a multiple of 8 bytes");
    strbuf *sb = strbuf_dup(data);
    des3_encrypt_pubkey_ossh(key.ptr, iv.ptr, sb->u, sb->len);
    return sb;
}

strbuf *des3_decrypt_pubkey_ossh_wrapper(ptrlen key, ptrlen iv, ptrlen data)
{
    if (key.len != 24)
        fatal_error("des3_decrypt_pubkey_ossh: key must be 24 bytes long");
    if (iv.len != 8)
        fatal_error("des3_encrypt_pubkey_ossh: iv must be 8 bytes long");
    if (data.len % 8 != 0)
        fatal_error("des3_decrypt_pubkey_ossh: data must be a multiple of 8 bytes");
    strbuf *sb = strbuf_dup(data);
    des3_decrypt_pubkey_ossh(key.ptr, iv.ptr, sb->u, sb->len);
    return sb;
}

strbuf *aes256_encrypt_pubkey_wrapper(ptrlen key, ptrlen iv, ptrlen data)
{
    if (key.len != 32)
        fatal_error("aes256_encrypt_pubkey: key must be 32 bytes long");
    if (iv.len != 16)
        fatal_error("aes256_encrypt_pubkey: iv must be 16 bytes long");
    if (data.len % 16 != 0)
        fatal_error("aes256_encrypt_pubkey: data must be a multiple of 16 bytes");
    strbuf *sb = strbuf_dup(data);
    aes256_encrypt_pubkey(key.ptr, iv.ptr, sb->u, sb->len);
    return sb;
}

strbuf *aes256_decrypt_pubkey_wrapper(ptrlen key, ptrlen iv, ptrlen data)
{
    if (key.len != 32)
        fatal_error("aes256_decrypt_pubkey: key must be 32 bytes long");
    if (iv.len != 16)
        fatal_error("aes256_encrypt_pubkey: iv must be 16 bytes long");
    if (data.len % 16 != 0)
        fatal_error("aes256_decrypt_pubkey: data must be a multiple of 16 bytes");
    strbuf *sb = strbuf_dup(data);
    aes256_decrypt_pubkey(key.ptr, iv.ptr, sb->u, sb->len);
    return sb;
}

strbuf *prng_read_wrapper(prng *pr, size_t size)
{
    strbuf *sb = strbuf_new();
    prng_read(pr, strbuf_append(sb, size), size);
    return sb;
}

void prng_seed_update(prng *pr, ptrlen data)
{
    put_datapl(pr, data);
}

bool crcda_detect(ptrlen packet, ptrlen iv)
{
    if (iv.len != 0 && iv.len != 8)
        fatal_error("crcda_detect: iv must be empty or 8 bytes long");
    if (packet.len % 8 != 0)
        fatal_error("crcda_detect: packet must be a multiple of 8 bytes");
    struct crcda_ctx *ctx = crcda_make_context();
    bool toret = detect_attack(ctx, packet.ptr, packet.len,
                               iv.len ? iv.ptr : NULL);
    crcda_free_context(ctx);
    return toret;
}

ssh_key *ppk_load_s_wrapper(BinarySource *src, char **comment,
                            const char *passphrase, const char **errorstr)
{
    ssh2_userkey *uk = ppk_load_s(src, passphrase, errorstr);
    if (uk == SSH2_WRONG_PASSPHRASE) {
        /* Fudge this special return value */
        *errorstr = "SSH2_WRONG_PASSPHRASE";
        return NULL;
    }
    if (uk == NULL)
        return NULL;
    ssh_key *toret = uk->key;
    *comment = uk->comment;
    sfree(uk);
    return toret;
}

int rsa1_load_s_wrapper(BinarySource *src, RSAKey *rsa, char **comment,
                        const char *passphrase, const char **errorstr)
{
    int toret = rsa1_load_s(src, rsa, passphrase, errorstr);
    *comment = rsa->comment;
    rsa->comment = NULL;
    return toret;
}

strbuf *ppk_save_sb_wrapper(
    ssh_key *key, const char *comment, const char *passphrase,
    unsigned fmt_version, Argon2Flavour flavour,
    uint32_t mem, uint32_t passes, uint32_t parallel)
{
    /*
     * For repeatable testing purposes, we never want a timing-dependent
     * choice of password hashing parameters, so this is easy.
     */
    ppk_save_parameters save_params;
    memset(&save_params, 0, sizeof(save_params));
    save_params.fmt_version = fmt_version;
    save_params.argon2_flavour = flavour;
    save_params.argon2_mem = mem;
    save_params.argon2_passes_auto = false;
    save_params.argon2_passes = passes;
    save_params.argon2_parallelism = parallel;

    ssh2_userkey uk;
    uk.key = key;
    uk.comment = dupstr(comment);
    strbuf *toret = ppk_save_sb(&uk, passphrase, &save_params);
    sfree(uk.comment);
    return toret;
}

strbuf *rsa1_save_sb_wrapper(RSAKey *key, const char *comment,
                             const char *passphrase)
{
    key->comment = dupstr(comment);
    strbuf *toret = rsa1_save_sb(key, passphrase);
    sfree(key->comment);
    key->comment = NULL;
    return toret;
}

#define return_void(out, expression) (expression)

static ProgressReceiver null_progress = { .vt = &null_progress_vt };

mp_int *primegen_generate_wrapper(
    PrimeGenerationContext *ctx, PrimeCandidateSource *pcs)
{
    return primegen_generate(ctx, pcs, &null_progress);
}

RSAKey *rsa1_generate(int bits, bool strong, PrimeGenerationContext *pgc)
{
    RSAKey *rsakey = snew(RSAKey);
    rsa_generate(rsakey, bits, strong, pgc, &null_progress);
    rsakey->comment = NULL;
    return rsakey;
}

ssh_key *rsa_generate_wrapper(int bits, bool strong,
                              PrimeGenerationContext *pgc)
{
    return &rsa1_generate(bits, strong, pgc)->sshk;
}

ssh_key *dsa_generate_wrapper(int bits, PrimeGenerationContext *pgc)
{
    struct dsa_key *dsakey = snew(struct dsa_key);
    dsa_generate(dsakey, bits, pgc, &null_progress);
    return &dsakey->sshk;
}

ssh_key *ecdsa_generate_wrapper(int bits)
{
    struct ecdsa_key *ek = snew(struct ecdsa_key);
    if (!ecdsa_generate(ek, bits)) {
        sfree(ek);
        return NULL;
    }
    return &ek->sshk;
}

ssh_key *eddsa_generate_wrapper(int bits)
{
    struct eddsa_key *ek = snew(struct eddsa_key);
    if (!eddsa_generate(ek, bits)) {
        sfree(ek);
        return NULL;
    }
    return &ek->sshk;
}

size_t key_components_count(key_components *kc) { return kc->ncomponents; }
const char *key_components_nth_name(key_components *kc, size_t n)
{
    return (n >= kc->ncomponents ? NULL :
            kc->components[n].name);
}
strbuf *key_components_nth_str(key_components *kc, size_t n)
{
    if (n >= kc->ncomponents)
        return NULL;
    if (kc->components[n].type != KCT_TEXT &&
        kc->components[n].type != KCT_BINARY)
        return NULL;
    return strbuf_dup(ptrlen_from_strbuf(kc->components[n].str));
}
mp_int *key_components_nth_mp(key_components *kc, size_t n)
{
    return (n >= kc->ncomponents ? NULL :
            kc->components[n].type != KCT_MPINT ? NULL :
            mp_copy(kc->components[n].mp));
}

PockleStatus pockle_add_prime_wrapper(Pockle *pockle, mp_int *p,
                                      struct mpint_list mpl, mp_int *witness)
{
    return pockle_add_prime(pockle, p, mpl.integers, mpl.n, witness);
}

strbuf *argon2_wrapper(Argon2Flavour flavour, uint32_t mem, uint32_t passes,
                       uint32_t parallel, uint32_t taglen,
                       ptrlen P, ptrlen S, ptrlen K, ptrlen X)
{
    strbuf *out = strbuf_new();
    argon2(flavour, mem, passes, parallel, taglen, P, S, K, X, out);
    return out;
}

strbuf *openssh_bcrypt_wrapper(ptrlen passphrase, ptrlen salt,
                               unsigned rounds, unsigned outbytes)
{
    strbuf *out = strbuf_new();
    openssh_bcrypt(passphrase, salt, rounds,
                   strbuf_append(out, outbytes), outbytes);
    return out;
}

strbuf *get_implementations_commasep(ptrlen alg)
{
    strbuf *out = strbuf_new();
    put_datapl(out, alg);

    if (ptrlen_startswith(alg, PTRLEN_LITERAL("aesgcm"), NULL)) {
        put_fmt(out, ",%.*s_sw", PTRLEN_PRINTF(alg));
        put_fmt(out, ",%.*s_ref_poly", PTRLEN_PRINTF(alg));
#if HAVE_CLMUL
        put_fmt(out, ",%.*s_clmul", PTRLEN_PRINTF(alg));
#endif
#if HAVE_NEON_PMULL
        put_fmt(out, ",%.*s_neon", PTRLEN_PRINTF(alg));
#endif
    } else if (ptrlen_startswith(alg, PTRLEN_LITERAL("aes"), NULL)) {
        put_fmt(out, ",%.*s_sw", PTRLEN_PRINTF(alg));
#if HAVE_AES_NI
        put_fmt(out, ",%.*s_ni", PTRLEN_PRINTF(alg));
#endif
#if HAVE_NEON_CRYPTO
        put_fmt(out, ",%.*s_neon", PTRLEN_PRINTF(alg));
#endif
    } else if (ptrlen_startswith(alg, PTRLEN_LITERAL("sha256"), NULL) ||
               ptrlen_startswith(alg, PTRLEN_LITERAL("sha1"), NULL)) {
        put_fmt(out, ",%.*s_sw", PTRLEN_PRINTF(alg));
#if HAVE_SHA_NI
        put_fmt(out, ",%.*s_ni", PTRLEN_PRINTF(alg));
#endif
#if HAVE_NEON_CRYPTO
        put_fmt(out, ",%.*s_neon", PTRLEN_PRINTF(alg));
#endif
    } else if (ptrlen_startswith(alg, PTRLEN_LITERAL("sha512"), NULL)) {
        put_fmt(out, ",%.*s_sw", PTRLEN_PRINTF(alg));
#if HAVE_NEON_SHA512
        put_fmt(out, ",%.*s_neon", PTRLEN_PRINTF(alg));
#endif
    }

    return out;
}

#define OPTIONAL_PTR_FUNC(type)                                         \
    typedef TD_val_##type TD_opt_val_##type;                            \
    static TD_opt_val_##type get_opt_val_##type(BinarySource *in) {     \
        ptrlen word = get_word(in);                                     \
        if (ptrlen_eq_string(word, "NULL"))                             \
            return NULL;                                                \
        return unwrap_value_##type(lookup_value(word))->vu_##type;      \
    }
OPTIONAL_PTR_FUNC(cipher)
OPTIONAL_PTR_FUNC(mpint)
OPTIONAL_PTR_FUNC(string)

/*
 * HERE BE DRAGONS: the horrible C preprocessor business that reads
 * testcrypt-func.h and generates a marshalling wrapper for each
 * exported function.
 *
 * In an ideal world, we would start from a specification like this in
 * testcrypt-func.h
 *
 *    FUNC(val_foo, example, ARG(val_bar, bar), ARG(uint, n))
 *
 * and generate a wrapper function looking like this:
 *
 *    static void handle_example(BinarySource *in, strbuf *out) {
 *        TD_val_bar bar = get_val_bar(in);
 *        TD_uint    n   = get_uint(in);
 *        return_val_foo(out, example(bar, n));
 *    }
 *
 * which would read the marshalled form of each function argument in
 * turn from the input BinarySource via the get_<type>() function
 * family defined in this file; assign each argument to a local
 * variable; call the underlying C function with all those arguments;
 * and then call a function of the return_<type>() family to marshal
 * the output value into the output strbuf to be sent to standard
 * output.
 *
 * With a more general macro processor such as m4, or custom code in
 * Perl or Python, or a helper program like llvm-tblgen, we could just
 * do that directly, reading function specifications from
 * testcrypt-func.h and writing out exactly the above. But we don't
 * have a fully general macro processor (since everything in that
 * category introduces an extra build dependency that's awkward on
 * plain Windows, or requires compiling and running a helper program
 * which is awkward in a cross-compile). We only have cpp. And in cpp,
 * a macro can't expand one of its arguments differently in two parts
 * of its own expansion. So we have to be more clever.
 *
 * In place of the above code, I instead generate three successive
 * declarations for each function. In simplified form they would look
 * like this:
 *
 *    typedef struct ARGS_example {
 *        TD_val_bar bar;
 *        TD_uint n;
 *    } ARGS_example;
 *
 *    static inline ARGS_example get_args_example(BinarySource *in) {
 *        ARGS_example args;
 *        args.bar = get_val_bar(in);
 *        args.n   = get_uint(in);
 *        return args;
 *    }
 *
 *    static void handle_example(BinarySource *in, strbuf *out) {
 *        ARGS_example args = get_args_example(in);
 *        return_val_foo(out, example(args.bar, args.n));
 *    }
 *
 * Each of these mentions the arguments and their types just _once_,
 * so each one can be generated by a single expansion of the FUNC(...)
 * specification in testcrypt-func.h, with FUNC and ARG and VOID
 * defined to appropriate values.
 *
 * Or ... *nearly*. In fact, I left out several details there, but
 * it's a good starting point to understand the full version.
 *
 * To begin with, several of the variable names shown above are
 * actually named with an ugly leading underscore, to minimise the
 * chance of them colliding with real parameter names. (You could
 * easily imagine 'out' being the name of a parameter to one of the
 * wrapped functions.) Also, we memset the whole structure to zero at
 * the start of get_args_example() to avoid compiler warnings about
 * uninitialised stuff, and insert a precautionary '(void)args;' in
 * handle_example to avoid a similar warning about _unused_ stuff.
 *
 * The big problem is the commas that have to appear between arguments
 * in the final call to the actual C function. Those can't be
 * generated by expanding the ARG macro itself, or you'd get one too
 * many - either a leading comma or a trailing comma. Trailing commas
 * are legal in a Python function call, but unfortunately C is not yet
 * so enlightened. (C permits a trailing comma in a struct or array
 * initialiser, and is coming round to it in enums, but hasn't yet
 * seen the light about function calls or function prototypes.)
 *
 * So the commas must appear _between_ ARG(...) specifiers. And that
 * means they unavoidably appear in _every_ expansion of FUNC() (or
 * rather, every expansion that uses the variadic argument list at
 * all). Therefore, we need to ensure they're harmless in the other
 * two functions as well.
 *
 * In the get_args_example() function above, there's no real problem.
 * The list of assignments can perfectly well be separated by commas
 * instead of semicolons, so that it becomes a single expression-
 * statement instead of a sequence of them; the comma operator still
 * defines a sequence point, so it's fine.
 *
 * But what about the structure definition of ARGS_example?
 *
 * To get round that, we fill the structure with pointless extra
 * cruft, in the form of an extra 'int' field before and after each
 * actually useful argument field. So the real structure definition
 * ends up looking more like this:
 *
 *    typedef struct ARGS_example {
 *        int _predummy_bar;
 *        TD_val_bar bar;
 *        int _postdummy_bar, _predummy_n;
 *        TD_uint n;
 *        int _postdummy_n;
 *    } ARGS_example;
 *
 * Those extra 'int' fields are ignored completely at run time. They
 * might cause a runtime space cost if the struct doesn't get
 * completely optimised away when get_args_example is inlined into
 * handle_example, but even if so, that's OK, this is a test program
 * whose memory usage isn't critical. The real point is that, in
 * between each pair of real arguments, there's a declaration
 * containing *two* int variables, and in between them is the vital
 * comma that we need!
 *
 * So in that pass through testcrypt-func.h, the ARG(type, name) macro
 * has to expand to the weird piece of text
 *
 *             _predummy_name;   // terminating the previous int declaration
 *        TD_type name;          // declaring the thing we actually wanted
 *        int _postdummy_name    // new declaration ready to see a comma
 *
 * so that a comma-separated list of pieces of expansion like that
 * will fall into just the right form to be the core of the above
 * expanded structure definition. Then we just need to put in the
 * 'int' after the open brace, and the ';' before the closing brace,
 * and we've got everything we need to make it all syntactically legal.
 *
 * Finally, what if a wrapped function has _no_ arguments? Two out of
 * three uses of the argument list here need some kind of special case
 * for that. That's why you have to write 'VOID' explicitly in an
 * empty argument list in testcrypt-func.h: we make VOID expand to
 * whatever is needed to avoid a syntax error in that special case.
 */

/*
 * Workarounds for an awkwardness in Visual Studio's preprocessor,
 * which disagrees with everyone else about what happens if you expand
 * __VA_ARGS__ into the argument list of another macro. gcc and clang
 * will treat the commas expanding from __VA_ARGS__ as argument
 * separators, whereas VS will make them all part of a single argument
 * to the secondary macro. We want the former behaviour, so we use
 * the following workaround to enforce it.
 *
 * Each of these JUXTAPOSE macros simply places its arguments side by
 * side. But the arguments are macro-expanded before JUXTAPOSE is
 * called at all, so we can do this:
 *
 *      JUXTAPOSE(macroname, (__VA_ARGS__))
 * ->   JUXTAPOSE(macroname, (foo, bar, baz))
 * ->             macroname  (foo, bar, baz)
 *
 * and this preliminary expansion causes the commas to be treated
 * normally by the time VS gets round to expanding the inner macro.
 *
 * We need two differently named JUXTAPOSE macros, because we have to
 * do this trick twice: once to turn FUNC and FUNC_WRAPPED in
 * testcrypt-funcs.h into the underlying common FUNC_INNER, and again
 * to expand the final function call. And you can't expand a macro
 * inside text expanded from the _same_ macro, so we have to do the
 * outer and inner instances of this trick using macros of different
 * names.
 */
#define JUXTAPOSE1(first, second) first second
#define JUXTAPOSE2(first, second) first second

#define FUNC(outtype, fname, ...) \
    JUXTAPOSE1(FUNC_INNER, (outtype, fname, fname, __VA_ARGS__))
#define FUNC_WRAPPED(outtype, fname, ...) \
    JUXTAPOSE1(FUNC_INNER, (outtype, fname, fname##_wrapper, __VA_ARGS__))

#define ARG(type, arg)  _predummy_##arg; TD_##type arg; int _postdummy_##arg
#define VOID _voiddummy
#define FUNC_INNER(outtype, fname, realname, ...)       \
    typedef struct ARGS_##fname {                       \
        int __VA_ARGS__;                                \
    } ARGS_##fname;
#include "testcrypt-func.h"
#undef FUNC_INNER
#undef ARG
#undef VOID

#define ARG(type, arg) _args.arg = get_##type(_in)
#define VOID ((void)0)
#define FUNC_INNER(outtype, fname, realname, ...)                       \
    static inline ARGS_##fname get_args_##fname(BinarySource *_in) {    \
        ARGS_##fname _args;                                             \
        memset(&_args, 0, sizeof(_args));                               \
        __VA_ARGS__;                                                    \
        return _args;                                                   \
    }
#include "testcrypt-func.h"
#undef FUNC_INNER
#undef ARG
#undef VOID

#define ARG(type, arg) _args.arg
#define VOID
#define FUNC_INNER(outtype, fname, realname, ...)                       \
    static void handle_##fname(BinarySource *_in, strbuf *_out) {       \
        ARGS_##fname _args = get_args_##fname(_in);                     \
        (void)_args; /* suppress warning if no actual arguments */      \
        return_##outtype(_out, JUXTAPOSE2(realname, (__VA_ARGS__)));    \
    }
#include "testcrypt-func.h"
#undef FUNC_INNER
#undef ARG

static void process_line(BinarySource *in, strbuf *out)
{
    ptrlen id = get_word(in);

#define DISPATCH_INTERNAL(cmdname, handler) do {        \
        if (ptrlen_eq_string(id, cmdname)) {            \
            handler(in, out);                           \
            return;                                     \
        }                                               \
    } while (0)

#define DISPATCH_COMMAND(cmd) DISPATCH_INTERNAL(#cmd, handle_##cmd)
    DISPATCH_COMMAND(hello);
    DISPATCH_COMMAND(free);
    DISPATCH_COMMAND(newstring);
    DISPATCH_COMMAND(getstring);
    DISPATCH_COMMAND(mp_literal);
    DISPATCH_COMMAND(mp_dump);
    DISPATCH_COMMAND(checkenum);
#undef DISPATCH_COMMAND

#define FUNC_INNER(outtype, fname, realname, ...)       \
    DISPATCH_INTERNAL(#fname,handle_##fname);
#define ARG1(type, arg)
#define ARGN(type, arg)
#define VOID
#include "testcrypt-func.h"
#undef FUNC_INNER
#undef ARG
#undef VOID

#undef DISPATCH_INTERNAL

    fatal_error("command '%.*s': unrecognised", PTRLEN_PRINTF(id));
}

static void free_all_values(void)
{
    for (Value *val; (val = delpos234(values, 0)) != NULL ;)
        free_value(val);
    freetree234(values);
}

void dputs(const char *buf)
{
    fputs(buf, stderr);
}

int main(int argc, char **argv)
{
    const char *infile = NULL, *outfile = NULL;
    bool doing_opts = true;

    while (--argc > 0) {
        char *p = *++argv;

        if (p[0] == '-' && doing_opts) {
            if (!strcmp(p, "-o")) {
                if (--argc <= 0) {
                    fprintf(stderr, "'-o' expects a filename\n");
                    return 1;
                }
                outfile = *++argv;
            } else if (!strcmp(p, "--")) {
                doing_opts = false;
            } else if (!strcmp(p, "--help")) {
                printf("usage: testcrypt [INFILE] [-o OUTFILE]\n");
                printf(" also: testcrypt --help       display this text\n");
                return 0;
            } else {
                fprintf(stderr, "unknown command line option '%s'\n", p);
                return 1;
            }
        } else if (!infile) {
            infile = p;
        } else {
            fprintf(stderr, "can only handle one input file name\n");
            return 1;
        }
    }

    FILE *infp = stdin;
    if (infile) {
        infp = fopen(infile, "r");
        if (!infp) {
            fprintf(stderr, "%s: open: %s\n", infile, strerror(errno));
            return 1;
        }
    }

    FILE *outfp = stdout;
    if (outfile) {
        outfp = fopen(outfile, "w");
        if (!outfp) {
            fprintf(stderr, "%s: open: %s\n", outfile, strerror(errno));
            return 1;
        }
    }

    values = newtree234(valuecmp);

    atexit(free_all_values);

    for (char *line; (line = chomp(fgetline(infp))) != NULL ;) {
        BinarySource src[1];
        BinarySource_BARE_INIT(src, line, strlen(line));
        strbuf *sb = strbuf_new();
        process_line(src, sb);
        run_finalisers(sb);
        size_t lines = 0;
        for (size_t i = 0; i < sb->len; i++)
            if (sb->s[i] == '\n')
                lines++;
        fprintf(outfp, "%"SIZEu"\n%s", lines, sb->s);
        fflush(outfp);
        strbuf_free(sb);
        sfree(line);
    }

    if (infp != stdin)
        fclose(infp);
    if (outfp != stdin)
        fclose(outfp);

    return 0;
}
