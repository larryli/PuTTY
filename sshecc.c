/*
 * Elliptic-curve crypto module for PuTTY
 * Implements the three required curves, no optional curves
 * NOTE: Only curves on prime field are handled by the maths functions
 */

/*
 * References:
 *
 * Elliptic curves in SSH are specified in RFC 5656:
 *   http://tools.ietf.org/html/rfc5656
 *
 * That specification delegates details of public key formatting and a
 * lot of underlying mechanism to SEC 1:
 *   http://www.secg.org/sec1-v2.pdf
 */

#include <stdlib.h>
#include <assert.h>

#include "ssh.h"

/* ----------------------------------------------------------------------
 * Elliptic curve definitions
 */

static int initialise_curve(struct ec_curve *curve, int bits, unsigned char *p,
                            unsigned char *a, unsigned char *b,
                            unsigned char *n, unsigned char *Gx,
                            unsigned char *Gy)
{
    int length = bits / 8;
    if (bits % 8) ++length;

    curve->fieldBits = bits;
    curve->p = bignum_from_bytes(p, length);
    if (!curve->p) goto error;

    /* Curve co-efficients */
    curve->a = bignum_from_bytes(a, length);
    if (!curve->a) goto error;
    curve->b = bignum_from_bytes(b, length);
    if (!curve->b) goto error;

    /* Group order and generator */
    curve->n = bignum_from_bytes(n, length);
    if (!curve->n) goto error;
    curve->G.x = bignum_from_bytes(Gx, length);
    if (!curve->G.x) goto error;
    curve->G.y = bignum_from_bytes(Gy, length);
    if (!curve->G.y) goto error;
    curve->G.curve = curve;
    curve->G.infinity = 0;

    return 1;
  error:
    if (curve->p) freebn(curve->p);
    if (curve->a) freebn(curve->a);
    if (curve->b) freebn(curve->b);
    if (curve->n) freebn(curve->n);
    if (curve->G.x) freebn(curve->G.x);
    return 0;
}

unsigned char nistp256_oid[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07};
int nistp256_oid_len = 8;
unsigned char nistp384_oid[] = {0x2b, 0x81, 0x04, 0x00, 0x22};
int nistp384_oid_len = 5;
unsigned char nistp521_oid[] = {0x2b, 0x81, 0x04, 0x00, 0x23};
int nistp521_oid_len = 5;

struct ec_curve *ec_p256(void)
{
    static struct ec_curve curve = { 0 };
    static unsigned char initialised = 0;

    if (!initialised)
    {
        unsigned char p[] = {
            0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x01,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
        };
        unsigned char a[] = {
            0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x01,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc
        };
        unsigned char b[] = {
            0x5a, 0xc6, 0x35, 0xd8, 0xaa, 0x3a, 0x93, 0xe7,
            0xb3, 0xeb, 0xbd, 0x55, 0x76, 0x98, 0x86, 0xbc,
            0x65, 0x1d, 0x06, 0xb0, 0xcc, 0x53, 0xb0, 0xf6,
            0x3b, 0xce, 0x3c, 0x3e, 0x27, 0xd2, 0x60, 0x4b
        };
        unsigned char n[] = {
            0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17, 0x9e, 0x84,
            0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x51
        };
        unsigned char Gx[] = {
            0x6b, 0x17, 0xd1, 0xf2, 0xe1, 0x2c, 0x42, 0x47,
            0xf8, 0xbc, 0xe6, 0xe5, 0x63, 0xa4, 0x40, 0xf2,
            0x77, 0x03, 0x7d, 0x81, 0x2d, 0xeb, 0x33, 0xa0,
            0xf4, 0xa1, 0x39, 0x45, 0xd8, 0x98, 0xc2, 0x96
        };
        unsigned char Gy[] = {
            0x4f, 0xe3, 0x42, 0xe2, 0xfe, 0x1a, 0x7f, 0x9b,
            0x8e, 0xe7, 0xeb, 0x4a, 0x7c, 0x0f, 0x9e, 0x16,
            0x2b, 0xce, 0x33, 0x57, 0x6b, 0x31, 0x5e, 0xce,
            0xcb, 0xb6, 0x40, 0x68, 0x37, 0xbf, 0x51, 0xf5
        };

        if (!initialise_curve(&curve, 256, p, a, b, n, Gx, Gy)) {
            return NULL;
        }

        /* Now initialised, no need to do it again */
        initialised = 1;
    }

    return &curve;
}

struct ec_curve *ec_p384(void)
{
    static struct ec_curve curve = { 0 };
    static unsigned char initialised = 0;

    if (!initialised)
    {
        unsigned char p[] = {
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
            0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff
        };
        unsigned char a[] = {
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
            0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xfc
        };
        unsigned char b[] = {
            0xb3, 0x31, 0x2f, 0xa7, 0xe2, 0x3e, 0xe7, 0xe4,
            0x98, 0x8e, 0x05, 0x6b, 0xe3, 0xf8, 0x2d, 0x19,
            0x18, 0x1d, 0x9c, 0x6e, 0xfe, 0x81, 0x41, 0x12,
            0x03, 0x14, 0x08, 0x8f, 0x50, 0x13, 0x87, 0x5a,
            0xc6, 0x56, 0x39, 0x8d, 0x8a, 0x2e, 0xd1, 0x9d,
            0x2a, 0x85, 0xc8, 0xed, 0xd3, 0xec, 0x2a, 0xef
        };
        unsigned char n[] = {
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xc7, 0x63, 0x4d, 0x81, 0xf4, 0x37, 0x2d, 0xdf,
            0x58, 0x1a, 0x0d, 0xb2, 0x48, 0xb0, 0xa7, 0x7a,
            0xec, 0xec, 0x19, 0x6a, 0xcc, 0xc5, 0x29, 0x73
        };
        unsigned char Gx[] = {
            0xaa, 0x87, 0xca, 0x22, 0xbe, 0x8b, 0x05, 0x37,
            0x8e, 0xb1, 0xc7, 0x1e, 0xf3, 0x20, 0xad, 0x74,
            0x6e, 0x1d, 0x3b, 0x62, 0x8b, 0xa7, 0x9b, 0x98,
            0x59, 0xf7, 0x41, 0xe0, 0x82, 0x54, 0x2a, 0x38,
            0x55, 0x02, 0xf2, 0x5d, 0xbf, 0x55, 0x29, 0x6c,
            0x3a, 0x54, 0x5e, 0x38, 0x72, 0x76, 0x0a, 0xb7
        };
        unsigned char Gy[] = {
            0x36, 0x17, 0xde, 0x4a, 0x96, 0x26, 0x2c, 0x6f,
            0x5d, 0x9e, 0x98, 0xbf, 0x92, 0x92, 0xdc, 0x29,
            0xf8, 0xf4, 0x1d, 0xbd, 0x28, 0x9a, 0x14, 0x7c,
            0xe9, 0xda, 0x31, 0x13, 0xb5, 0xf0, 0xb8, 0xc0,
            0x0a, 0x60, 0xb1, 0xce, 0x1d, 0x7e, 0x81, 0x9d,
            0x7a, 0x43, 0x1d, 0x7c, 0x90, 0xea, 0x0e, 0x5f
        };

        if (!initialise_curve(&curve, 384, p, a, b, n, Gx, Gy)) {
            return NULL;
        }

        /* Now initialised, no need to do it again */
        initialised = 1;
    }

    return &curve;
}

struct ec_curve *ec_p521(void)
{
    static struct ec_curve curve = { 0 };
    static unsigned char initialised = 0;

    if (!initialised)
    {
        unsigned char p[] = {
            0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff
        };
        unsigned char a[] = {
            0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xfc
        };
        unsigned char b[] = {
            0x00, 0x51, 0x95, 0x3e, 0xb9, 0x61, 0x8e, 0x1c,
            0x9a, 0x1f, 0x92, 0x9a, 0x21, 0xa0, 0xb6, 0x85,
            0x40, 0xee, 0xa2, 0xda, 0x72, 0x5b, 0x99, 0xb3,
            0x15, 0xf3, 0xb8, 0xb4, 0x89, 0x91, 0x8e, 0xf1,
            0x09, 0xe1, 0x56, 0x19, 0x39, 0x51, 0xec, 0x7e,
            0x93, 0x7b, 0x16, 0x52, 0xc0, 0xbd, 0x3b, 0xb1,
            0xbf, 0x07, 0x35, 0x73, 0xdf, 0x88, 0x3d, 0x2c,
            0x34, 0xf1, 0xef, 0x45, 0x1f, 0xd4, 0x6b, 0x50,
            0x3f, 0x00
        };
        unsigned char n[] = {
            0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xfa, 0x51, 0x86, 0x87, 0x83, 0xbf, 0x2f,
            0x96, 0x6b, 0x7f, 0xcc, 0x01, 0x48, 0xf7, 0x09,
            0xa5, 0xd0, 0x3b, 0xb5, 0xc9, 0xb8, 0x89, 0x9c,
            0x47, 0xae, 0xbb, 0x6f, 0xb7, 0x1e, 0x91, 0x38,
            0x64, 0x09
        };
        unsigned char Gx[] = {
            0x00, 0xc6, 0x85, 0x8e, 0x06, 0xb7, 0x04, 0x04,
            0xe9, 0xcd, 0x9e, 0x3e, 0xcb, 0x66, 0x23, 0x95,
            0xb4, 0x42, 0x9c, 0x64, 0x81, 0x39, 0x05, 0x3f,
            0xb5, 0x21, 0xf8, 0x28, 0xaf, 0x60, 0x6b, 0x4d,
            0x3d, 0xba, 0xa1, 0x4b, 0x5e, 0x77, 0xef, 0xe7,
            0x59, 0x28, 0xfe, 0x1d, 0xc1, 0x27, 0xa2, 0xff,
            0xa8, 0xde, 0x33, 0x48, 0xb3, 0xc1, 0x85, 0x6a,
            0x42, 0x9b, 0xf9, 0x7e, 0x7e, 0x31, 0xc2, 0xe5,
            0xbd, 0x66
        };
        unsigned char Gy[] = {
            0x01, 0x18, 0x39, 0x29, 0x6a, 0x78, 0x9a, 0x3b,
            0xc0, 0x04, 0x5c, 0x8a, 0x5f, 0xb4, 0x2c, 0x7d,
            0x1b, 0xd9, 0x98, 0xf5, 0x44, 0x49, 0x57, 0x9b,
            0x44, 0x68, 0x17, 0xaf, 0xbd, 0x17, 0x27, 0x3e,
            0x66, 0x2c, 0x97, 0xee, 0x72, 0x99, 0x5e, 0xf4,
            0x26, 0x40, 0xc5, 0x50, 0xb9, 0x01, 0x3f, 0xad,
            0x07, 0x61, 0x35, 0x3c, 0x70, 0x86, 0xa2, 0x72,
            0xc2, 0x40, 0x88, 0xbe, 0x94, 0x76, 0x9f, 0xd1,
            0x66, 0x50
        };

        if (!initialise_curve(&curve, 521, p, a, b, n, Gx, Gy)) {
            return NULL;
        }

        /* Now initialised, no need to do it again */
        initialised = 1;
    }

    return &curve;
}

static struct ec_curve *ec_name_to_curve(char *name, int len) {
    if (len == 8 && !memcmp(name, "nistp", 5)) {
        name += 5;
        if (!memcmp(name, "256", 3)) {
            return ec_p256();
        } else if (!memcmp(name, "384", 3)) {
            return ec_p384();
        } else if (!memcmp(name, "521", 3)) {
            return ec_p521();
        }
    }

    return NULL;
}

static int ec_curve_to_name(const struct ec_curve *curve, unsigned char *name, int len) {
    /* Return length of string */
    if (name == NULL) return 8;

    /* Not enough space for the name */
    if (len < 8) return 0;

    /* Put the name in the buffer */
    switch (curve->fieldBits) {
      case 256:
        memcpy(name+5, "256", 3);
        break;
      case 384:
        memcpy(name+5, "384", 3);
        break;
      case 521:
        memcpy(name+5, "521", 3);
        break;
      default:
        return 0;
    }

    memcpy(name, "nistp", 5);
    return 8;
}

/* Return 1 if a is -3 % p, otherwise return 0
 * This is used because there are some maths optimisations */
static int ec_aminus3(const struct ec_curve *curve)
{
    int ret;
    Bignum _p;

    _p = bignum_add_long(curve->a, 3);
    if (!_p) return 0;

    ret = !bignum_cmp(curve->p, _p);
    freebn(_p);
    return ret;
}

/* ----------------------------------------------------------------------
 * Elliptic curve field maths
 */

static Bignum ecf_add(const Bignum a, const Bignum b,
                      const struct ec_curve *curve)
{
    Bignum a1, b1, ab, ret;

    a1 = bigmod(a, curve->p);
    if (!a1) return NULL;
    b1 = bigmod(b, curve->p);
    if (!b1)
    {
        freebn(a1);
        return NULL;
    }

    ab = bigadd(a1, b1);
    freebn(a1);
    freebn(b1);
    if (!ab) return NULL;

    ret = bigmod(ab, curve->p);
    freebn(ab);

    return ret;
}

static Bignum ecf_square(const Bignum a, const struct ec_curve *curve)
{
    return modmul(a, a, curve->p);
}

static Bignum ecf_treble(const Bignum a, const struct ec_curve *curve)
{
    Bignum ret, tmp;

    /* Double */
    tmp = bignum_lshift(a, 1);
    if (!tmp) return NULL;

    /* Add itself (i.e. treble) */
    ret = bigadd(tmp, a);
    freebn(tmp);

    /* Normalise */
    while (ret != NULL && bignum_cmp(ret, curve->p) >= 0)
    {
        tmp = bigsub(ret, curve->p);
        freebn(ret);
        ret = tmp;
    }

    return ret;
}

static Bignum ecf_double(const Bignum a, const struct ec_curve *curve)
{
    Bignum ret = bignum_lshift(a, 1);
    if (!ret) return NULL;
    if (bignum_cmp(ret, curve->p) >= 0)
    {
        Bignum tmp = bigsub(ret, curve->p);
        freebn(ret);
        return tmp;
    }
    else
    {
        return ret;
    }
}

/* ----------------------------------------------------------------------
 * Memory functions
 */

void ec_point_free(struct ec_point *point)
{
    if (point == NULL) return;
    point->curve = 0;
    if (point->x) freebn(point->x);
    if (point->y) freebn(point->y);
    if (point->z) freebn(point->z);
    point->infinity = 0;
    sfree(point);
}

static struct ec_point *ec_point_new(const struct ec_curve *curve,
                                     const Bignum x, const Bignum y, const Bignum z,
                                     unsigned char infinity)
{
    struct ec_point *point = snewn(1, struct ec_point);
    point->curve = curve;
    point->x = x;
    point->y = y;
    point->z = z;
    point->infinity = infinity ? 1 : 0;
    return point;
}

static struct ec_point *ec_point_copy(const struct ec_point *a)
{
    if (a == NULL) return NULL;
    return ec_point_new(a->curve,
                        a->x ? copybn(a->x) : NULL,
                        a->y ? copybn(a->y) : NULL,
                        a->z ? copybn(a->z) : NULL,
                        a->infinity);
}

static int ec_point_verify(const struct ec_point *a)
{
    if (a->infinity)
    {
        return 1;
    }
    else
    {
        /* Verify y^2 = x^3 + ax + b */
        int ret = 0;

        Bignum lhs = NULL, x3 = NULL, ax = NULL, x3ax = NULL, x3axm = NULL, x3axb = NULL, rhs = NULL;

        Bignum Three = bignum_from_long(3);
        if (!Three) return 0;

        lhs = modmul(a->y, a->y, a->curve->p);
        if (!lhs) goto error;

        /* This uses montgomery multiplication to optimise */
        x3 = modpow(a->x, Three, a->curve->p);
        freebn(Three);
        if (!x3) goto error;
        ax = modmul(a->curve->a, a->x, a->curve->p);
        if (!ax) goto error;
        x3ax = bigadd(x3, ax);
        if (!x3ax) goto error;
        freebn(x3); x3 = NULL;
        freebn(ax); ax = NULL;
        x3axm = bigmod(x3ax, a->curve->p);
        if (!x3axm) goto error;
        freebn(x3ax); x3ax = NULL;
        x3axb = bigadd(x3axm, a->curve->b);
        if (!x3axb) goto error;
        freebn(x3axm); x3axm = NULL;
        rhs = bigmod(x3axb, a->curve->p);
        if (!rhs) goto error;
        freebn(x3axb);

        ret = bignum_cmp(lhs, rhs) ? 0 : 1;
        freebn(lhs);
        freebn(rhs);

        return ret;

      error:
        if (x3) freebn(x3);
        if (ax) freebn(ax);
        if (x3ax) freebn(x3ax);
        if (x3axm) freebn(x3axm);
        if (x3axb) freebn(x3axb);
        if (lhs) freebn(lhs);
        return 0;
    }
}

/* ----------------------------------------------------------------------
 * Elliptic curve point maths
 */

/* Returns 1 on success and 0 on memory error */
static int ecp_normalise(struct ec_point *a)
{
    Bignum Z2, Z2inv, Z3, Z3inv, tx, ty;

    /* In Jacobian Coordinates the triple (X, Y, Z) represents
       the affine point (X / Z^2, Y / Z^3) */
    if (!a) {
        /* No point */
        return 0;
    }
    if (a->infinity) {
        /* Point is at infinity - i.e. normalised */
        return 1;
    } else if (!a->x || !a->y) {
        /* No point defined */
        return 0;
    } else if (!a->z) {
        /* Already normalised */
        return 1;
    }

    Z2 = ecf_square(a->z, a->curve);
    if (!Z2) {
        return 0;
    }
    Z2inv = modinv(Z2, a->curve->p);
    if (!Z2inv) {
        freebn(Z2);
        return 0;
    }
    tx = modmul(a->x, Z2inv, a->curve->p);
    freebn(Z2inv);
    if (!tx) {
        freebn(Z2);
        return 0;
    }

    Z3 = modmul(Z2, a->z, a->curve->p);
    freebn(Z2);
    if (!Z3) {
        freebn(tx);
        return 0;
    }
    Z3inv = modinv(Z3, a->curve->p);
    freebn(Z3);
    if (!Z3inv) {
        freebn(tx);
        return 0;
    }
    ty = modmul(a->y, Z3inv, a->curve->p);
    freebn(Z3inv);
    if (!ty) {
        freebn(tx);
        return 0;
    }

    freebn(a->x);
    a->x = tx;
    freebn(a->y);
    a->y = ty;
    freebn(a->z);
    a->z = NULL;
    return 1;
}

static struct ec_point *ecp_double(const struct ec_point *a, const int aminus3)
{
    Bignum S, M, outx, outy, outz;

    if (a->infinity || bignum_cmp(a->y, Zero) == 0)
    {
        /* Identity */
        return ec_point_new(a->curve, NULL, NULL, NULL, 1);
    }

    /* S = 4*X*Y^2 */
    {
        Bignum Y2, XY2, _2XY2;

        Y2 = ecf_square(a->y, a->curve);
        if (!Y2) {
            return NULL;
        }
        XY2 = modmul(a->x, Y2, a->curve->p);
        freebn(Y2);
        if (!XY2) {
            return NULL;
        }

        _2XY2 = ecf_double(XY2, a->curve);
        freebn(XY2);
        if (!_2XY2) {
            return NULL;
        }
        S = ecf_double(_2XY2, a->curve);
        freebn(_2XY2);
        if (!S) {
            return NULL;
        }
    }

    /* Faster calculation if a = -3 */
    if (aminus3) {
        /* if a = -3, then M can also be calculated as M = 3*(X + Z^2)*(X - Z^2) */
        Bignum Z2, XpZ2, XmZ2, second;

        if (a->z == NULL) {
            Z2 = copybn(One);
        } else {
            Z2 = ecf_square(a->z, a->curve);
        }
        if (!Z2) {
            freebn(S);
            return NULL;
        }

        XpZ2 = ecf_add(a->x, Z2, a->curve);
        if (!XpZ2) {
            freebn(S);
            freebn(Z2);
            return NULL;
        }
        XmZ2 = modsub(a->x, Z2, a->curve->p);
        freebn(Z2);
        if (!XpZ2) {
            freebn(S);
            freebn(XpZ2);
            return NULL;
        }

        second = modmul(XpZ2, XmZ2, a->curve->p);
        freebn(XpZ2);
        freebn(XmZ2);
        if (!second) {
            freebn(S);
            return NULL;
        }

        M = ecf_treble(second, a->curve);
        freebn(second);
        if (!M) {
            freebn(S);
            return NULL;
        }
    } else {
        /* M = 3*X^2 + a*Z^4 */
        Bignum _3X2, X2, aZ4;

        if (a->z == NULL) {
            aZ4 = copybn(a->curve->a);
        } else {
            Bignum Z2, Z4;

            Z2 = ecf_square(a->z, a->curve);
            if (!Z2) {
                freebn(S);
                return NULL;
            }
            Z4 = ecf_square(Z2, a->curve);
            freebn(Z2);
            if (!Z4) {
                freebn(S);
                return NULL;
            }
            aZ4 = modmul(a->curve->a, Z4, a->curve->p);
            freebn(Z4);
        }
        if (!aZ4) {
            freebn(S);
            return NULL;
        }

        X2 = modmul(a->x, a->x, a->curve->p);
        if (!X2) {
            freebn(S);
            freebn(aZ4);
            return NULL;
        }
        _3X2 = ecf_treble(X2, a->curve);
        freebn(X2);
        if (!_3X2) {
            freebn(S);
            freebn(aZ4);
            return NULL;
        }
        M = ecf_add(_3X2, aZ4, a->curve);
        freebn(_3X2);
        freebn(aZ4);
        if (!M) {
            freebn(S);
            return NULL;
        }
    }

    /* X' = M^2 - 2*S */
    {
        Bignum M2, _2S;

        M2 = ecf_square(M, a->curve);
        if (!M2) {
            freebn(S);
            freebn(M);
            return NULL;
        }

        _2S = ecf_double(S, a->curve);
        if (!_2S) {
            freebn(M2);
            freebn(S);
            freebn(M);
            return NULL;
        }

        outx = modsub(M2, _2S, a->curve->p);
        freebn(M2);
        freebn(_2S);
        if (!outx) {
            freebn(S);
            freebn(M);
            return NULL;
        }
    }

    /* Y' = M*(S - X') - 8*Y^4 */
    {
        Bignum SX, MSX, Eight, Y2, Y4, _8Y4;

        SX = modsub(S, outx, a->curve->p);
        freebn(S);
        if (!SX) {
            freebn(M);
            freebn(outx);
            return NULL;
        }
        MSX = modmul(M, SX, a->curve->p);
        freebn(SX);
        freebn(M);
        if (!MSX) {
            freebn(outx);
            return NULL;
        }
        Y2 = ecf_square(a->y, a->curve);
        if (!Y2) {
            freebn(outx);
            freebn(MSX);
            return NULL;
        }
        Y4 = ecf_square(Y2, a->curve);
        freebn(Y2);
        if (!Y4) {
            freebn(outx);
            freebn(MSX);
            return NULL;
        }
        Eight = bignum_from_long(8);
        if (!Eight) {
            freebn(outx);
            freebn(MSX);
            freebn(Y4);
            return NULL;
        }
        _8Y4 = modmul(Eight, Y4, a->curve->p);
        freebn(Eight);
        freebn(Y4);
        if (!_8Y4) {
            freebn(outx);
            freebn(MSX);
            return NULL;
        }
        outy = modsub(MSX, _8Y4, a->curve->p);
        freebn(MSX);
        freebn(_8Y4);
        if (!outy) {
            freebn(outx);
            return NULL;
        }
    }

    /* Z' = 2*Y*Z */
    {
        Bignum YZ;

        if (a->z == NULL) {
            YZ = copybn(a->y);
        } else {
            YZ = modmul(a->y, a->z, a->curve->p);
        }
        if (!YZ) {
            freebn(outx);
            freebn(outy);
            return NULL;
        }

        outz = ecf_double(YZ, a->curve);
        freebn(YZ);
        if (!outz) {
            freebn(outx);
            freebn(outy);
            return NULL;
        }
    }

    return ec_point_new(a->curve, outx, outy, outz, 0);
}

static struct ec_point *ecp_add(const struct ec_point *a,
                                const struct ec_point *b,
                                const int aminus3)
{
    Bignum U1, U2, S1, S2, outx, outy, outz;

    /* Check if multiplying by infinity */
    if (a->infinity) return ec_point_copy(b);
    if (b->infinity) return ec_point_copy(a);

    /* U1 = X1*Z2^2 */
    /* S1 = Y1*Z2^3 */
    if (b->z) {
        Bignum Z2, Z3;

        Z2 = ecf_square(b->z, a->curve);
        if (!Z2) {
            return NULL;
        }
        U1 = modmul(a->x, Z2, a->curve->p);
        if (!U1) {
            freebn(Z2);
            return NULL;
        }
        Z3 = modmul(Z2, b->z, a->curve->p);
        freebn(Z2);
        if (!Z3) {
            freebn(U1);
            return NULL;
        }
        S1 = modmul(a->y, Z3, a->curve->p);
        freebn(Z3);
        if (!S1) {
            freebn(U1);
            return NULL;
        }
    } else {
        U1 = copybn(a->x);
        if (!U1) {
            return NULL;
        }
        S1 = copybn(a->y);
        if (!S1) {
            freebn(U1);
            return NULL;
        }
    }

    /* U2 = X2*Z1^2 */
    /* S2 = Y2*Z1^3 */
    if (a->z) {
        Bignum Z2, Z3;

        Z2 = ecf_square(a->z, b->curve);
        if (!Z2) {
            freebn(U1);
            freebn(S1);
            return NULL;
        }
        U2 = modmul(b->x, Z2, b->curve->p);
        if (!U2) {
            freebn(U1);
            freebn(S1);
            freebn(Z2);
            return NULL;
        }
        Z3 = modmul(Z2, a->z, b->curve->p);
        freebn(Z2);
        if (!Z3) {
            freebn(U1);
            freebn(S1);
            freebn(U2);
            return NULL;
        }
        S2 = modmul(b->y, Z3, b->curve->p);
        freebn(Z3);
        if (!S2) {
            freebn(U1);
            freebn(S1);
            freebn(U2);
            return NULL;
        }
    } else {
        U2 = copybn(b->x);
        if (!U2) {
            freebn(U1);
            freebn(S1);
            return NULL;
        }
        S2 = copybn(b->y);
        if (!S2) {
            freebn(U1);
            freebn(S1);
            freebn(U2);
            return NULL;
        }
    }

    /* Check if multiplying by self */
    if (bignum_cmp(U1, U2) == 0)
    {
        freebn(U1);
        freebn(U2);
        if (bignum_cmp(S1, S2) == 0)
        {
            freebn(S1);
            freebn(S2);
            return ecp_double(a, aminus3);
        }
        else
        {
            freebn(S1);
            freebn(S2);
            /* Infinity */
            return ec_point_new(a->curve, NULL, NULL, NULL, 1);
        }
    }

    {
        Bignum H, R, UH2, H3;

        /* H = U2 - U1 */
        H = modsub(U2, U1, a->curve->p);
        freebn(U2);
        if (!H) {
            freebn(U1);
            freebn(S1);
            freebn(S2);
            return NULL;
        }

        /* R = S2 - S1 */
        R = modsub(S2, S1, a->curve->p);
        freebn(S2);
        if (!R) {
            freebn(H);
            freebn(S1);
            freebn(U1);
            return NULL;
        }

        /* X3 = R^2 - H^3 - 2*U1*H^2 */
        {
            Bignum R2, H2, _2UH2, first;

            H2 = ecf_square(H, a->curve);
            if (!H2) {
                freebn(U1);
                freebn(S1);
                freebn(H);
                freebn(R);
                return NULL;
            }
            UH2 = modmul(U1, H2, a->curve->p);
            freebn(U1);
            if (!UH2) {
                freebn(H2);
                freebn(S1);
                freebn(H);
                freebn(R);
                return NULL;
            }
            H3 = modmul(H2, H, a->curve->p);
            freebn(H2);
            if (!H3) {
                freebn(UH2);
                freebn(S1);
                freebn(H);
                freebn(R);
                return NULL;
            }
            R2 = ecf_square(R, a->curve);
            if (!R2) {
                freebn(H3);
                freebn(UH2);
                freebn(S1);
                freebn(H);
                freebn(R);
                return NULL;
            }
            _2UH2 = ecf_double(UH2, a->curve);
            if (!_2UH2) {
                freebn(R2);
                freebn(H3);
                freebn(UH2);
                freebn(S1);
                freebn(H);
                freebn(R);
                return NULL;
            }
            first = modsub(R2, H3, a->curve->p);
            freebn(R2);
            if (!first) {
                freebn(H3);
                freebn(_2UH2);
                freebn(UH2);
                freebn(S1);
                freebn(H);
                freebn(R);
                return NULL;
            }
            outx = modsub(first, _2UH2, a->curve->p);
            freebn(first);
            freebn(_2UH2);
            if (!outx) {
                freebn(H3);
                freebn(UH2);
                freebn(S1);
                freebn(H);
                freebn(R);
                return NULL;
            }
        }

        /* Y3 = R*(U1*H^2 - X3) - S1*H^3 */
        {
            Bignum RUH2mX, UH2mX, SH3;

            UH2mX = modsub(UH2, outx, a->curve->p);
            freebn(UH2);
            if (!UH2mX) {
                freebn(outx);
                freebn(H3);
                freebn(S1);
                freebn(H);
                freebn(R);
                return NULL;
            }
            RUH2mX = modmul(R, UH2mX, a->curve->p);
            freebn(UH2mX);
            freebn(R);
            if (!RUH2mX) {
                freebn(outx);
                freebn(H3);
                freebn(S1);
                freebn(H);
                return NULL;
            }
            SH3 = modmul(S1, H3, a->curve->p);
            freebn(S1);
            freebn(H3);
            if (!SH3) {
                freebn(RUH2mX);
                freebn(outx);
                freebn(H);
                return NULL;
            }

            outy = modsub(RUH2mX, SH3, a->curve->p);
            freebn(RUH2mX);
            freebn(SH3);
            if (!outy) {
                freebn(outx);
                freebn(H);
                return NULL;
            }
        }

        /* Z3 = H*Z1*Z2 */
        if (a->z && b->z) {
            Bignum ZZ;

            ZZ = modmul(a->z, b->z, a->curve->p);
            if (!ZZ) {
                freebn(outx);
                freebn(outy);
                freebn(H);
                return NULL;
            }
            outz = modmul(H, ZZ, a->curve->p);
            freebn(H);
            freebn(ZZ);
            if (!outz) {
                freebn(outx);
                freebn(outy);
                return NULL;
            }
        } else if (a->z) {
            outz = modmul(H, a->z, a->curve->p);
            freebn(H);
            if (!outz) {
                freebn(outx);
                freebn(outy);
                return NULL;
            }
        } else if (b->z) {
            outz = modmul(H, b->z, a->curve->p);
            freebn(H);
            if (!outz) {
                freebn(outx);
                freebn(outy);
                return NULL;
            }
        } else {
            outz = H;
        }
    }

    return ec_point_new(a->curve, outx, outy, outz, 0);
}

static struct ec_point *ecp_mul_(const struct ec_point *a, const Bignum b, int aminus3)
{
    struct ec_point *A, *ret;
    int bits, i;

    A = ec_point_copy(a);
    ret = ec_point_new(a->curve, NULL, NULL, NULL, 1);

    bits = bignum_bitcount(b);
    for (i = 0; ret != NULL && A != NULL && i < bits; ++i)
    {
        if (bignum_bit(b, i))
        {
            struct ec_point *tmp = ecp_add(ret, A, aminus3);
            ec_point_free(ret);
            ret = tmp;
        }
        if (i+1 != bits)
        {
            struct ec_point *tmp = ecp_double(A, aminus3);
            ec_point_free(A);
            A = tmp;
        }
    }

    if (!A) {
        ec_point_free(ret);
        ret = NULL;
    } else {
        ec_point_free(A);
    }

    return ret;
}

/* Not static because it is used by sshecdsag.c to generate a new key */
struct ec_point *ecp_mul(const struct ec_point *a, const Bignum b)
{
    struct ec_point *ret = ecp_mul_(a, b, ec_aminus3(a->curve));

    if (!ecp_normalise(ret)) {
        ec_point_free(ret);
        return NULL;
    }

    return ret;
}

static struct ec_point *ecp_summul(const Bignum a, const Bignum b,
                                   const struct ec_point *point)
{
    struct ec_point *aG, *bP, *ret;
    int aminus3 = ec_aminus3(point->curve);

    aG = ecp_mul_(&point->curve->G, a, aminus3);
    if (!aG) return NULL;
    bP = ecp_mul_(point, b, aminus3);
    if (!bP) {
        ec_point_free(aG);
        return NULL;
    }

    ret = ecp_add(aG, bP, aminus3);

    ec_point_free(aG);
    ec_point_free(bP);

    if (!ecp_normalise(ret)) {
        ec_point_free(ret);
        return NULL;
    }

    return ret;
}

/* ----------------------------------------------------------------------
 * Basic sign and verify routines
 */

static int _ecdsa_verify(const struct ec_point *publicKey,
                         const unsigned char *data, const int dataLen,
                         const Bignum r, const Bignum s)
{
    int z_bits, n_bits;
    Bignum z;
    int valid = 0;

    /* Sanity checks */
    if (bignum_cmp(r, Zero) == 0 || bignum_cmp(r, publicKey->curve->n) >= 0
        || bignum_cmp(s, Zero) == 0 || bignum_cmp(s, publicKey->curve->n) >= 0)
    {
        return 0;
    }

    /* z = left most bitlen(curve->n) of data */
    z = bignum_from_bytes(data, dataLen);
    if (!z) return 0;
    n_bits = bignum_bitcount(publicKey->curve->n);
    z_bits = bignum_bitcount(z);
    if (z_bits > n_bits)
    {
        Bignum tmp = bignum_rshift(z, z_bits - n_bits);
        freebn(z);
        z = tmp;
        if (!z) return 0;
    }

    /* Ensure z in range of n */
    {
        Bignum tmp = bigmod(z, publicKey->curve->n);
        freebn(z);
        z = tmp;
        if (!z) return 0;
    }

    /* Calculate signature */
    {
        Bignum w, x, u1, u2;
        struct ec_point *tmp;

        w = modinv(s, publicKey->curve->n);
        if (!w) {
            freebn(z);
            return 0;
        }
        u1 = modmul(z, w, publicKey->curve->n);
        if (!u1) {
            freebn(z);
            freebn(w);
            return 0;
        }
        u2 = modmul(r, w, publicKey->curve->n);
        freebn(w);
        if (!u2) {
            freebn(z);
            freebn(u1);
            return 0;
        }

        tmp = ecp_summul(u1, u2, publicKey);
        freebn(u1);
        freebn(u2);
        if (!tmp) {
            freebn(z);
            return 0;
        }

        x = bigmod(tmp->x, publicKey->curve->n);
        ec_point_free(tmp);
        if (!x) {
            freebn(z);
            return 0;
        }

        valid = (bignum_cmp(r, x) == 0) ? 1 : 0;
        freebn(x);
    }

    freebn(z);

    return valid;
}

static void _ecdsa_sign(const Bignum privateKey, const struct ec_curve *curve,
                        const unsigned char *data, const int dataLen,
                        Bignum *r, Bignum *s)
{
    unsigned char digest[20];
    int z_bits, n_bits;
    Bignum z, k;
    struct ec_point *kG;

    *r = NULL;
    *s = NULL;

    /* z = left most bitlen(curve->n) of data */
    z = bignum_from_bytes(data, dataLen);
    if (!z) return;
    n_bits = bignum_bitcount(curve->n);
    z_bits = bignum_bitcount(z);
    if (z_bits > n_bits)
    {
        Bignum tmp;
        tmp = bignum_rshift(z, z_bits - n_bits);
        freebn(z);
        z = tmp;
        if (!z) return;
    }

    /* Generate k between 1 and curve->n, using the same deterministic
     * k generation system we use for conventional DSA. */
    SHA_Simple(data, dataLen, digest);
    k = dss_gen_k("ECDSA deterministic k generator", curve->n, privateKey,
                  digest, sizeof(digest));
    if (!k) return;

    kG = ecp_mul(&curve->G, k);
    if (!kG) {
        freebn(z);
        freebn(k);
        return;
    }

    /* r = kG.x mod n */
    *r = bigmod(kG->x, curve->n);
    ec_point_free(kG);
    if (!*r) {
        freebn(z);
        freebn(k);
        return;
    }

    /* s = (z + r * priv)/k mod n */
    {
        Bignum rPriv, zMod, first, firstMod, kInv;
        rPriv = modmul(*r, privateKey, curve->n);
        if (!rPriv) {
            freebn(*r);
            freebn(z);
            freebn(k);
            return;
        }
        zMod = bigmod(z, curve->n);
        freebn(z);
        if (!zMod) {
            freebn(rPriv);
            freebn(*r);
            freebn(k);
            return;
        }
        first = bigadd(rPriv, zMod);
        freebn(rPriv);
        freebn(zMod);
        if (!first) {
            freebn(*r);
            freebn(k);
            return;
        }
        firstMod = bigmod(first, curve->n);
        freebn(first);
        if (!firstMod) {
            freebn(*r);
            freebn(k);
            return;
        }
        kInv = modinv(k, curve->n);
        freebn(k);
        if (!kInv) {
            freebn(firstMod);
            freebn(*r);
            return;
        }
        *s = modmul(firstMod, kInv, curve->n);
        freebn(firstMod);
        freebn(kInv);
        if (!*s) {
            freebn(*r);
            return;
        }
    }
}

/* ----------------------------------------------------------------------
 * Misc functions
 */

static void getstring(char **data, int *datalen, char **p, int *length)
{
    *p = NULL;
    if (*datalen < 4)
        return;
    *length = toint(GET_32BIT(*data));
    if (*length < 0)
        return;
    *datalen -= 4;
    *data += 4;
    if (*datalen < *length)
        return;
    *p = *data;
    *data += *length;
    *datalen -= *length;
}

static Bignum getmp(char **data, int *datalen)
{
    char *p;
    int length;

    getstring(data, datalen, &p, &length);
    if (!p)
        return NULL;
    if (p[0] & 0x80)
        return NULL;                   /* negative mp */
    return bignum_from_bytes((unsigned char *)p, length);
}

static int decodepoint(char *p, int length, struct ec_point *point)
{
    if (length < 1 || p[0] != 0x04) /* Only support uncompressed point */
        return 0;
    /* Skip compression flag */
    ++p;
    --length;
    /* The two values must be equal length */
    if (length % 2 != 0) {
        point->x = NULL;
        point->y = NULL;
        point->z = NULL;
        return 0;
    }
    length = length / 2;
    point->x = bignum_from_bytes((unsigned char *)p, length);
    if (!point->x) return 0;
    p += length;
    point->y = bignum_from_bytes((unsigned char *)p, length);
    if (!point->y) {
        freebn(point->x);
        point->x = NULL;
        return 0;
    }
    point->z = NULL;

    /* Verify the point is on the curve */
    if (!ec_point_verify(point)) {
        ec_point_free(point);
        return 0;
    }

    return 1;
}

static int getmppoint(char **data, int *datalen, struct ec_point *point)
{
    char *p;
    int length;

    getstring(data, datalen, &p, &length);
    if (!p) return 0;
    return decodepoint(p, length, point);
}

/* ----------------------------------------------------------------------
 * Exposed ECDSA interface
 */

static void ecdsa_freekey(void *key)
{
    struct ec_key *ec = (struct ec_key *) key;
    if (!ec) return;

    if (ec->publicKey.x)
        freebn(ec->publicKey.x);
    if (ec->publicKey.y)
        freebn(ec->publicKey.y);
    if (ec->publicKey.z)
        freebn(ec->publicKey.z);
    if (ec->privateKey)
        freebn(ec->privateKey);
    sfree(ec);
}

static void *ecdsa_newkey(char *data, int len)
{
    char *p;
    int slen;
    struct ec_key *ec;
    struct ec_curve *curve;

    getstring(&data, &len, &p, &slen);

    if (!p || slen < 11 || memcmp(p, "ecdsa-sha2-", 11)) {
        return NULL;
    }
    curve = ec_name_to_curve(p+11, slen-11);
    if (!curve) return NULL;

    getstring(&data, &len, &p, &slen);

    if (curve != ec_name_to_curve(p, slen)) return NULL;

    ec = snew(struct ec_key);

    ec->publicKey.curve = curve;
    ec->publicKey.infinity = 0;
    ec->publicKey.x = NULL;
    ec->publicKey.y = NULL;
    ec->publicKey.z = NULL;
    if (!getmppoint(&data, &len, &ec->publicKey)) {
        ecdsa_freekey(ec);
        return NULL;
    }
    ec->privateKey = NULL;

    if (!ec->publicKey.x || !ec->publicKey.y ||
        bignum_cmp(ec->publicKey.x, curve->p) >= 0 ||
        bignum_cmp(ec->publicKey.y, curve->p) >= 0)
    {
        ecdsa_freekey(ec);
        ec = NULL;
    }

    return ec;
}

static char *ecdsa_fmtkey(void *key)
{
    struct ec_key *ec = (struct ec_key *) key;
    char *p;
    int len, i, pos, nibbles;
    static const char hex[] = "0123456789abcdef";
    if (!ec->publicKey.x || !ec->publicKey.y || !ec->publicKey.curve)
        return NULL;

    pos = ec_curve_to_name(ec->publicKey.curve, NULL, 0);
    if (pos == 0) return NULL;

    len = 4 + 2 + 1;                  /* 2 x "0x", punctuation, \0 */
    len += pos; /* Curve name */
    len += 4 * (bignum_bitcount(ec->publicKey.x) + 15) / 16;
    len += 4 * (bignum_bitcount(ec->publicKey.y) + 15) / 16;
    p = snewn(len, char);

    pos = ec_curve_to_name(ec->publicKey.curve, (unsigned char*)p, pos);
    pos += sprintf(p + pos, ",0x");
    nibbles = (3 + bignum_bitcount(ec->publicKey.x)) / 4;
    if (nibbles < 1)
        nibbles = 1;
    for (i = nibbles; i--;) {
        p[pos++] =
            hex[(bignum_byte(ec->publicKey.x, i / 2) >> (4 * (i % 2))) & 0xF];
    }
    pos += sprintf(p + pos, ",0x");
    nibbles = (3 + bignum_bitcount(ec->publicKey.y)) / 4;
    if (nibbles < 1)
        nibbles = 1;
    for (i = nibbles; i--;) {
        p[pos++] =
            hex[(bignum_byte(ec->publicKey.y, i / 2) >> (4 * (i % 2))) & 0xF];
    }
    p[pos] = '\0';
    return p;
}

static unsigned char *ecdsa_public_blob(void *key, int *len)
{
    struct ec_key *ec = (struct ec_key *) key;
    int pointlen, bloblen, namelen;
    int i;
    unsigned char *blob, *p;

    namelen = ec_curve_to_name(ec->publicKey.curve, NULL, 0);
    if (namelen == 0) return NULL;

    pointlen = (bignum_bitcount(ec->publicKey.curve->p) + 7) / 8;

    /*
     * string "ecdsa-sha2-<name>", string "<name>", 0x04 point x, y.
     */
    bloblen = 4 + 11 + namelen + 4 + namelen + 4 + 1 + (pointlen * 2);
    blob = snewn(bloblen, unsigned char);

    p = blob;
    PUT_32BIT(p, 11 + namelen);
    p += 4;
    memcpy(p, "ecdsa-sha2-", 11);
    p += 11;
    p += ec_curve_to_name(ec->publicKey.curve, p, namelen);
    PUT_32BIT(p, namelen);
    p += 4;
    p += ec_curve_to_name(ec->publicKey.curve, p, namelen);
    PUT_32BIT(p, (2 * pointlen) + 1);
    p += 4;
    *p++ = 0x04;
    for (i = pointlen; i--;)
        *p++ = bignum_byte(ec->publicKey.x, i);
    for (i = pointlen; i--;)
        *p++ = bignum_byte(ec->publicKey.y, i);

    assert(p == blob + bloblen);
    *len = bloblen;

    return blob;
}

static unsigned char *ecdsa_private_blob(void *key, int *len)
{
    struct ec_key *ec = (struct ec_key *) key;
    int keylen, bloblen;
    int i;
    unsigned char *blob, *p;

    if (!ec->privateKey) return NULL;

    keylen = (bignum_bitcount(ec->privateKey) + 8) / 8;

    /*
     * mpint privateKey. Total 4 + keylen.
     */
    bloblen = 4 + keylen;
    blob = snewn(bloblen, unsigned char);

    p = blob;
    PUT_32BIT(p, keylen);
    p += 4;
    for (i = keylen; i--;)
        *p++ = bignum_byte(ec->privateKey, i);

    assert(p == blob + bloblen);
    *len = bloblen;
    return blob;
}

static void *ecdsa_createkey(unsigned char *pub_blob, int pub_len,
                             unsigned char *priv_blob, int priv_len)
{
    struct ec_key *ec;
    struct ec_point *publicKey;
    char *pb = (char *) priv_blob;

    ec = (struct ec_key*)ecdsa_newkey((char *) pub_blob, pub_len);
    if (!ec) {
        return NULL;
    }

    ec->privateKey = getmp(&pb, &priv_len);
    if (!ec->privateKey) {
        ecdsa_freekey(ec);
        return NULL;
    }

    /* Check that private key generates public key */
    publicKey = ecp_mul(&ec->publicKey.curve->G, ec->privateKey);

    if (!publicKey ||
        bignum_cmp(publicKey->x, ec->publicKey.x) ||
        bignum_cmp(publicKey->y, ec->publicKey.y))
    {
        ecdsa_freekey(ec);
        ec = NULL;
    }
    ec_point_free(publicKey);

    return ec;
}

static void *ecdsa_openssh_createkey(unsigned char **blob, int *len)
{
    char **b = (char **) blob;
    char *p;
    int slen;
    struct ec_key *ec;
    struct ec_curve *curve;
    struct ec_point *publicKey;

    getstring(b, len, &p, &slen);

    if (!p) {
        return NULL;
    }
    curve = ec_name_to_curve(p, slen);
    if (!curve) return NULL;

    ec = snew(struct ec_key);

    ec->publicKey.curve = curve;
    ec->publicKey.infinity = 0;
    ec->publicKey.x = NULL;
    ec->publicKey.y = NULL;
    ec->publicKey.z = NULL;
    if (!getmppoint(b, len, &ec->publicKey)) {
        ecdsa_freekey(ec);
        return NULL;
    }
    ec->privateKey = NULL;

    if (!ec->publicKey.x || !ec->publicKey.y ||
        bignum_cmp(ec->publicKey.x, curve->p) >= 0 ||
        bignum_cmp(ec->publicKey.y, curve->p) >= 0)
    {
        ecdsa_freekey(ec);
        return NULL;
    }

    ec->privateKey = getmp(b, len);
    if (ec->privateKey == NULL)
    {
        ecdsa_freekey(ec);
        return NULL;
    }

    /* Now check that the private key makes the public key */
    publicKey = ecp_mul(&ec->publicKey.curve->G, ec->privateKey);
    if (!publicKey)
    {
        ecdsa_freekey(ec);
        return NULL;
    }

    if (bignum_cmp(ec->publicKey.x, publicKey->x) ||
        bignum_cmp(ec->publicKey.y, publicKey->y))
    {
        /* Private key doesn't make the public key on the given curve */
        ecdsa_freekey(ec);
        ec_point_free(publicKey);
    }

    ec_point_free(publicKey);

    return ec;
}

static int ecdsa_openssh_fmtkey(void *key, unsigned char *blob, int len)
{
    struct ec_key *ec = (struct ec_key *) key;

    int pointlen = (bignum_bitcount(ec->publicKey.curve->p) + 7) / 8;

    int namelen = ec_curve_to_name(ec->publicKey.curve, NULL, 0);

    int bloblen =
        4 + namelen /* <LEN> nistpXXX */
        + 4 + 1 + (pointlen * 2) /* <LEN> 0x04 pX pY */
        + ssh2_bignum_length(ec->privateKey);

    int i;

    if (bloblen > len)
        return bloblen;

    bloblen = 0;

    PUT_32BIT(blob+bloblen, namelen);
    bloblen += 4;

    bloblen += ec_curve_to_name(ec->publicKey.curve, blob+bloblen, namelen);

    PUT_32BIT(blob+bloblen, 1 + (pointlen * 2));
    bloblen += 4;
    blob[bloblen++] = 0x04;
    for (i = pointlen; i--; )
        blob[bloblen++] = bignum_byte(ec->publicKey.x, i);
    for (i = pointlen; i--; )
        blob[bloblen++] = bignum_byte(ec->publicKey.y, i);

    pointlen = (bignum_bitcount(ec->privateKey) + 8) / 8;
    PUT_32BIT(blob+bloblen, pointlen);
    bloblen += 4;
    for (i = pointlen; i--; )
        blob[bloblen++] = bignum_byte(ec->privateKey, i);

    return bloblen;
}

static int ecdsa_pubkey_bits(void *blob, int len)
{
    struct ec_key *ec;
    int ret;

    ec = (struct ec_key*)ecdsa_newkey((char *) blob, len);
    if (!ec)
        return -1;
    ret = ec->publicKey.curve->fieldBits;
    ecdsa_freekey(ec);

    return ret;
}

static char *ecdsa_fingerprint(void *key)
{
    struct ec_key *ec = (struct ec_key *) key;
    struct MD5Context md5c;
    unsigned char digest[16], lenbuf[4];
    char *ret;
    unsigned char *name;
    int pointlen, namelen, i, j;

    namelen = ec_curve_to_name(ec->publicKey.curve, NULL, 0);
    name = snewn(namelen, unsigned char);
    ec_curve_to_name(ec->publicKey.curve, name, namelen);

    MD5Init(&md5c);

    PUT_32BIT(lenbuf, namelen + 11);
    MD5Update(&md5c, lenbuf, 4);
    MD5Update(&md5c, (const unsigned char *)"ecdsa-sha2-", 11);
    MD5Update(&md5c, name, namelen);

    PUT_32BIT(lenbuf, namelen);
    MD5Update(&md5c, lenbuf, 4);
    MD5Update(&md5c, name, namelen);

    pointlen = (bignum_bitcount(ec->publicKey.curve->p) + 7) / 8;
    PUT_32BIT(lenbuf, 1 + (pointlen * 2));
    MD5Update(&md5c, lenbuf, 4);
    MD5Update(&md5c, (const unsigned char *)"\x04", 1);
    for (i = pointlen; i--; ) {
        unsigned char c = bignum_byte(ec->publicKey.x, i);
        MD5Update(&md5c, &c, 1);
    }
    for (i = pointlen; i--; ) {
        unsigned char c = bignum_byte(ec->publicKey.y, i);
        MD5Update(&md5c, &c, 1);
    }

    MD5Final(digest, &md5c);

    ret = snewn(11 + namelen + 1 + (16 * 3), char);

    i = 11;
    memcpy(ret, "ecdsa-sha2-", 11);
    memcpy(ret+i, name, namelen);
    i += namelen;
    sfree(name);
    ret[i++] = ' ';
    for (j = 0; j < 16; j++)
        i += sprintf(ret + i, "%s%02x", j ? ":" : "", digest[j]);

    return ret;
}

static int ecdsa_verifysig(void *key, char *sig, int siglen,
                           char *data, int datalen)
{
    struct ec_key *ec = (struct ec_key *) key;
    char *p;
    int slen;
    unsigned char digest[512 / 8];
    int digestLen;
    Bignum r, s;
    int ret;

    if (!ec->publicKey.x || !ec->publicKey.y || !ec->publicKey.curve)
        return 0;

    /* Check the signature curve matches the key curve */
    getstring(&sig, &siglen, &p, &slen);
    if (!p || slen < 11 || memcmp(p, "ecdsa-sha2-", 11)) {
        return 0;
    }
    if (ec->publicKey.curve != ec_name_to_curve(p+11, slen-11)) {
        return 0;
    }

    getstring(&sig, &siglen, &p, &slen);
    r = getmp(&p, &slen);
    if (!r) return 0;
    s = getmp(&p, &slen);
    if (!s) {
        freebn(r);
        return 0;
    }

    /* Perform correct hash function depending on curve size */
    if (ec->publicKey.curve->fieldBits <= 256) {
        SHA256_Simple(data, datalen, digest);
        digestLen = 256 / 8;
    } else if (ec->publicKey.curve->fieldBits <= 384) {
        SHA384_Simple(data, datalen, digest);
        digestLen = 384 / 8;
    } else {
        SHA512_Simple(data, datalen, digest);
        digestLen = 512 / 8;
    }

    /* Verify the signature */
    if (!_ecdsa_verify(&ec->publicKey, digest, digestLen, r, s)) {
        ret = 0;
    } else {
        ret = 1;
    }

    freebn(r);
    freebn(s);

    return ret;
}

static unsigned char *ecdsa_sign(void *key, char *data, int datalen,
                                 int *siglen)
{
    struct ec_key *ec = (struct ec_key *) key;
    unsigned char digest[512 / 8];
    int digestLen;
    Bignum r = NULL, s = NULL;
    unsigned char *buf, *p;
    int rlen, slen, namelen;
    int i;

    if (!ec->privateKey || !ec->publicKey.curve) {
        return NULL;
    }

    /* Perform correct hash function depending on curve size */
    if (ec->publicKey.curve->fieldBits <= 256) {
        SHA256_Simple(data, datalen, digest);
        digestLen = 256 / 8;
    } else if (ec->publicKey.curve->fieldBits <= 384) {
        SHA384_Simple(data, datalen, digest);
        digestLen = 384 / 8;
    } else {
        SHA512_Simple(data, datalen, digest);
        digestLen = 512 / 8;
    }

    /* Do the signature */
    _ecdsa_sign(ec->privateKey, ec->publicKey.curve, digest, digestLen, &r, &s);
    if (!r || !s) {
        if (r) freebn(r);
        if (s) freebn(s);
        return NULL;
    }

    rlen = (bignum_bitcount(r) + 8) / 8;
    slen = (bignum_bitcount(s) + 8) / 8;

    namelen = ec_curve_to_name(ec->publicKey.curve, NULL, 0);

    /* Format the output */
    *siglen = 8+11+namelen+rlen+slen+8;
    buf = snewn(*siglen, unsigned char);
    p = buf;
    PUT_32BIT(p, 11+namelen);
    p += 4;
    memcpy(p, "ecdsa-sha2-", 11);
    p += 11;
    p += ec_curve_to_name(ec->publicKey.curve, p, namelen);
    PUT_32BIT(p, rlen + slen + 8);
    p += 4;
    PUT_32BIT(p, rlen);
    p += 4;
    for (i = rlen; i--;)
        *p++ = bignum_byte(r, i);
    PUT_32BIT(p, slen);
    p += 4;
    for (i = slen; i--;)
        *p++ = bignum_byte(s, i);

    return buf;
}

const struct ssh_signkey ssh_ecdsa_nistp256 = {
    ecdsa_newkey,
    ecdsa_freekey,
    ecdsa_fmtkey,
    ecdsa_public_blob,
    ecdsa_private_blob,
    ecdsa_createkey,
    ecdsa_openssh_createkey,
    ecdsa_openssh_fmtkey,
    ecdsa_pubkey_bits,
    ecdsa_fingerprint,
    ecdsa_verifysig,
    ecdsa_sign,
    "ecdsa-sha2-nistp256",
    "ecdsa-sha2-nistp256",
};

const struct ssh_signkey ssh_ecdsa_nistp384 = {
    ecdsa_newkey,
    ecdsa_freekey,
    ecdsa_fmtkey,
    ecdsa_public_blob,
    ecdsa_private_blob,
    ecdsa_createkey,
    ecdsa_openssh_createkey,
    ecdsa_openssh_fmtkey,
    ecdsa_pubkey_bits,
    ecdsa_fingerprint,
    ecdsa_verifysig,
    ecdsa_sign,
    "ecdsa-sha2-nistp384",
    "ecdsa-sha2-nistp384",
};

const struct ssh_signkey ssh_ecdsa_nistp521 = {
    ecdsa_newkey,
    ecdsa_freekey,
    ecdsa_fmtkey,
    ecdsa_public_blob,
    ecdsa_private_blob,
    ecdsa_createkey,
    ecdsa_openssh_createkey,
    ecdsa_openssh_fmtkey,
    ecdsa_pubkey_bits,
    ecdsa_fingerprint,
    ecdsa_verifysig,
    ecdsa_sign,
    "ecdsa-sha2-nistp521",
    "ecdsa-sha2-nistp521",
};

/* ----------------------------------------------------------------------
 * Exposed ECDH interface
 */

static Bignum ecdh_calculate(const Bignum private,
                             const struct ec_point *public)
{
    struct ec_point *p;
    Bignum ret;
    p = ecp_mul(public, private);
    if (!p) return NULL;
    ret = p->x;
    p->x = NULL;
    ec_point_free(p);
    return ret;
}

void *ssh_ecdhkex_newkey(struct ec_curve *curve)
{
    struct ec_key *key = snew(struct ec_key);
    struct ec_point *publicKey;
    key->publicKey.curve = curve;
    key->privateKey = bignum_random_in_range(One, key->publicKey.curve->n);
    if (!key->privateKey) {
        sfree(key);
        return NULL;
    }
    publicKey = ecp_mul(&key->publicKey.curve->G, key->privateKey);
    if (!publicKey) {
        freebn(key->privateKey);
        sfree(key);
        return NULL;
    }
    key->publicKey.x = publicKey->x;
    key->publicKey.y = publicKey->y;
    key->publicKey.z = NULL;
    sfree(publicKey);
    return key;
}

char *ssh_ecdhkex_getpublic(void *key, int *len)
{
    struct ec_key *ec = (struct ec_key*)key;
    char *point, *p;
    int i;
    int pointlen = (bignum_bitcount(ec->publicKey.curve->p) + 7) / 8;

    *len = 1 + pointlen * 2;
    point = (char*)snewn(*len, char);

    p = point;
    *p++ = 0x04;
    for (i = pointlen; i--;)
        *p++ = bignum_byte(ec->publicKey.x, i);
    for (i = pointlen; i--;)
        *p++ = bignum_byte(ec->publicKey.y, i);

    return point;
}

Bignum ssh_ecdhkex_getkey(void *key, char *remoteKey, int remoteKeyLen)
{
    struct ec_key *ec = (struct ec_key*) key;
    struct ec_point remote;

    remote.curve = ec->publicKey.curve;
    remote.infinity = 0;
    if (!decodepoint(remoteKey, remoteKeyLen, &remote)) {
        return NULL;
    }

    return ecdh_calculate(ec->privateKey, &remote);
}

void ssh_ecdhkex_freekey(void *key)
{
    ecdsa_freekey(key);
}

static const struct ssh_kex ssh_ec_kex_nistp256 = {
    "ecdh-sha2-nistp256", NULL, KEXTYPE_ECDH, NULL, NULL, 0, 0, &ssh_sha256
};

static const struct ssh_kex ssh_ec_kex_nistp384 = {
    "ecdh-sha2-nistp384", NULL, KEXTYPE_ECDH, NULL, NULL, 0, 0, &ssh_sha384
};

static const struct ssh_kex ssh_ec_kex_nistp521 = {
    "ecdh-sha2-nistp521", NULL, KEXTYPE_ECDH, NULL, NULL, 0, 0, &ssh_sha512
};

static const struct ssh_kex *const ec_kex_list[] = {
    &ssh_ec_kex_nistp256,
    &ssh_ec_kex_nistp384,
    &ssh_ec_kex_nistp521
};

const struct ssh_kexes ssh_ecdh_kex = {
    sizeof(ec_kex_list) / sizeof(*ec_kex_list),
    ec_kex_list
};
