import numbers
import itertools

def jacobi(n,m):
    """Compute the Jacobi symbol.

    The special case of this when m is prime is the Legendre symbol,
    which is 0 if n is congruent to 0 mod m; 1 if n is congruent to a
    non-zero square number mod m; -1 if n is not congruent to any
    square mod m.

    """
    assert m & 1
    acc = 1
    while True:
        n %= m
        if n == 0:
            return 0
        while not (n & 1):
            n >>= 1
            if (m & 7) not in {1,7}:
                acc *= -1
        if n == 1:
            return acc
        if (n & 3) == 3 and (m & 3) == 3:
            acc *= -1
        n, m = m, n

class SqrtModP(object):
    """Class for finding square roots of numbers mod p.

    p must be an odd prime (but its primality is not checked)."""

    def __init__(self, p):
        p = abs(p)
        assert p & 1
        self.p = p

        # Decompose p as 2^e k + 1 for odd k.
        self.k = p-1
        self.e = 0
        while not (self.k & 1):
            self.k >>= 1
            self.e += 1

        # Find a non-square mod p.
        for self.z in itertools.count(1):
            if jacobi(self.z, self.p) == -1:
                break
        self.zinv = ModP(self.p, self.z).invert()

    def sqrt_recurse(self, a):
        ak = pow(a, self.k, self.p)
        for i in range(self.e, -1, -1):
            if ak == 1:
                break
            ak = ak*ak % self.p
        assert i > 0
        if i == self.e:
            return pow(a, (self.k+1) // 2, self.p)
        r_prime = self.sqrt_recurse(a * pow(self.z, 2**i, self.p))
        return r_prime * pow(self.zinv, 2**(i-1), self.p) % self.p

    def sqrt(self, a):
        j = jacobi(a, self.p)
        if j == 0:
            return 0
        if j < 0:
            raise ValueError("{} has no square root mod {}".format(a, self.p))
        a %= self.p
        r = self.sqrt_recurse(a)
        assert r*r % self.p == a
        # Normalise to the smaller (or 'positive') one of the two roots.
        return min(r, self.p - r)

    def __str__(self):
        return "{}({})".format(type(self).__name__, self.p)
    def __repr__(self):
        return self.__str__()

class ModP(object):
    """Class that represents integers mod p as a field.

    All the usual arithmetic operations are supported directly,
    including division, so you can write formulas in a natural way
    without having to keep saying '% p' everywhere or call a
    cumbersome modular_inverse() function.

    """
    def __init__(self, p, n=0):
        self.p = p
        if isinstance(n, type(self)):
            self.check(n)
            n = n.n
        self.n = n % p
    def check(self, other):
        assert isinstance(other, type(self))
        assert isinstance(self, type(other))
        assert self.p == other.p
    def coerce_to(self, other):
        if not isinstance(other, type(self)):
            other = type(self)(self.p, other)
        else:
            self.check(other)
        return other
    def invert(self):
        "Internal routine which returns the bare inverse."
        if self.n % self.p == 0:
            raise ZeroDivisionError("division by {!r}".format(self))
        a = self.n, 1, 0
        b = self.p, 0, 1
        while b[0]:
            q = a[0] // b[0]
            a = a[0] - q*b[0], a[1] - q*b[1], a[2] - q*b[2]
            b, a = a, b
        assert abs(a[0]) == 1
        return a[1]*a[0]
    def __int__(self):
        return self.n
    def __add__(self, rhs):
        rhs = self.coerce_to(rhs)
        return type(self)(self.p, (self.n + rhs.n) % self.p)
    def __neg__(self):
        return type(self)(self.p, -self.n % self.p)
    def __radd__(self, rhs):
        rhs = self.coerce_to(rhs)
        return type(self)(self.p, (self.n + rhs.n) % self.p)
    def __sub__(self, rhs):
        rhs = self.coerce_to(rhs)
        return type(self)(self.p, (self.n - rhs.n) % self.p)
    def __rsub__(self, rhs):
        rhs = self.coerce_to(rhs)
        return type(self)(self.p, (rhs.n - self.n) % self.p)
    def __mul__(self, rhs):
        rhs = self.coerce_to(rhs)
        return type(self)(self.p, (self.n * rhs.n) % self.p)
    def __rmul__(self, rhs):
        rhs = self.coerce_to(rhs)
        return type(self)(self.p, (self.n * rhs.n) % self.p)
    def __div__(self, rhs):
        rhs = self.coerce_to(rhs)
        return type(self)(self.p, (self.n * rhs.invert()) % self.p)
    def __rdiv__(self, rhs):
        rhs = self.coerce_to(rhs)
        return type(self)(self.p, (rhs.n * self.invert()) % self.p)
    def __truediv__(self, rhs): return self.__div__(rhs)
    def __rtruediv__(self, rhs): return self.__rdiv__(rhs)
    def __pow__(self, exponent):
        assert exponent >= 0
        n, b_to_n = 1, self
        total = type(self)(self.p, 1)
        while True:
            if exponent & n:
                exponent -= n
                total *= b_to_n
            n *= 2
            if n > exponent:
                break
            b_to_n *= b_to_n
        return total
    def __cmp__(self, rhs):
        rhs = self.coerce_to(rhs)
        return cmp(self.n, rhs.n)
    def __eq__(self, rhs):
        rhs = self.coerce_to(rhs)
        return self.n == rhs.n
    def __ne__(self, rhs):
        rhs = self.coerce_to(rhs)
        return self.n != rhs.n
    def __lt__(self, rhs):
        raise ValueError("Elements of a modular ring have no ordering")
    def __le__(self, rhs):
        raise ValueError("Elements of a modular ring have no ordering")
    def __gt__(self, rhs):
        raise ValueError("Elements of a modular ring have no ordering")
    def __ge__(self, rhs):
        raise ValueError("Elements of a modular ring have no ordering")
    def __str__(self):
        return "0x{:x}".format(self.n)
    def __repr__(self):
        return "{}(0x{:x},0x{:x})".format(type(self).__name__, self.p, self.n)

class AffinePoint(object):
    """Base class for points on an elliptic curve."""

    def __init__(self, curve, *args):
        self.curve = curve
        if len(args) == 0:
            self.infinite = True
            self.x = self.y = None
        else:
            assert len(args) == 2
            self.infinite = False
            self.x = ModP(self.curve.p, args[0])
            self.y = ModP(self.curve.p, args[1])
            self.check_equation()
    def __neg__(self):
        if self.infinite:
            return self
        return type(self)(self.curve, self.x, -self.y)
    def __mul__(self, rhs):
        if not isinstance(rhs, numbers.Integral):
            raise ValueError("Elliptic curve points can only be multiplied by integers")
        P = self
        if rhs < 0:
            rhs = -rhs
            P = -P
        toret = self.curve.point()
        n = 1
        nP = P
        while rhs != 0:
            if rhs & n:
                rhs -= n
                toret += nP
            n += n
            nP += nP
        return toret
    def __rmul__(self, rhs):
        return self * rhs
    def __sub__(self, rhs):
        return self + (-rhs)
    def __rsub__(self, rhs):
        return (-self) + rhs
    def __str__(self):
        if self.infinite:
            return "inf"
        else:
            return "({},{})".format(self.x, self.y)
    def __repr__(self):
        if self.infinite:
            args = ""
        else:
            args = ", {}, {}".format(self.x, self.y)
        return "{}.Point({}{})".format(type(self.curve).__name__,
                                       self.curve, args)
    def __eq__(self, rhs):
        if self.infinite or rhs.infinite:
            return self.infinite and rhs.infinite
        return (self.x, self.y) == (rhs.x, rhs.y)
    def __ne__(self, rhs):
        return not (self == rhs)
    def __lt__(self, rhs):
        raise ValueError("Elliptic curve points have no ordering")
    def __le__(self, rhs):
        raise ValueError("Elliptic curve points have no ordering")
    def __gt__(self, rhs):
        raise ValueError("Elliptic curve points have no ordering")
    def __ge__(self, rhs):
        raise ValueError("Elliptic curve points have no ordering")
    def __hash__(self):
        if self.infinite:
            return hash((True,))
        else:
            return hash((False, self.x, self.y))

class CurveBase(object):
    def point(self, *args):
        return self.Point(self, *args)

class WeierstrassCurve(CurveBase):
    class Point(AffinePoint):
        def check_equation(self):
            assert (self.y*self.y ==
                    self.x*self.x*self.x +
                    self.curve.a*self.x + self.curve.b)
        def __add__(self, rhs):
            if self.infinite:
                return rhs
            if rhs.infinite:
                return self
            if self.x == rhs.x and self.y != rhs.y:
                return self.curve.point()
            x1, x2, y1, y2 = self.x, rhs.x, self.y, rhs.y
            xdiff = x2-x1
            if xdiff != 0:
                slope = (y2-y1) / xdiff
            else:
                assert y1 == y2
                slope = (3*x1*x1 + self.curve.a) / (2*y1)
            xp = slope*slope - x1 - x2
            yp = -(y1 + slope * (xp-x1))
            return self.curve.point(xp, yp)

    def __init__(self, p, a, b):
        self.p = p
        self.a = ModP(p, a)
        self.b = ModP(p, b)

    def cpoint(self, x, yparity=0):
        if not hasattr(self, 'sqrtmodp'):
            self.sqrtmodp = SqrtModP(self.p)
        rhs = x**3 + self.a.n * x + self.b.n
        y = self.sqrtmodp.sqrt(rhs)
        if (y - yparity) % 2:
            y = -y
        return self.point(x, y)

    def __repr__(self):
        return "{}(0x{:x}, {}, {})".format(
            type(self).__name__, self.p, self.a, self.b)

class MontgomeryCurve(CurveBase):
    class Point(AffinePoint):
        def check_equation(self):
            assert (self.curve.b*self.y*self.y ==
                    self.x*self.x*self.x +
                    self.curve.a*self.x*self.x + self.x)
        def __add__(self, rhs):
            if self.infinite:
                return rhs
            if rhs.infinite:
                return self
            if self.x == rhs.x and self.y != rhs.y:
                return self.curve.point()
            x1, x2, y1, y2 = self.x, rhs.x, self.y, rhs.y
            xdiff = x2-x1
            if xdiff != 0:
                slope = (y2-y1) / xdiff
            else:
                assert y1 == y2
                slope = (3*x1*x1 + 2*self.curve.a*x1 + 1) / (2*self.curve.b*y1)
            xp = self.curve.b*slope*slope - self.curve.a - x1 - x2
            yp = -(y1 + slope * (xp-x1))
            return self.curve.point(xp, yp)

    def __init__(self, p, a, b):
        self.p = p
        self.a = ModP(p, a)
        self.b = ModP(p, b)

    def cpoint(self, x, yparity=0):
        if not hasattr(self, 'sqrtmodp'):
            self.sqrtmodp = SqrtModP(self.p)
        rhs = (x**3 + self.a.n * x**2 + x) / self.b
        y = self.sqrtmodp.sqrt(int(rhs))
        if (y - yparity) % 2:
            y = -y
        return self.point(x, y)

    def __repr__(self):
        return "{}(0x{:x}, {}, {})".format(
            type(self).__name__, self.p, self.a, self.b)

class TwistedEdwardsCurve(CurveBase):
    class Point(AffinePoint):
        def check_equation(self):
            x2, y2 = self.x*self.x, self.y*self.y
            assert (self.curve.a*x2 + y2 == 1 + self.curve.d*x2*y2)
        def __neg__(self):
            return type(self)(self.curve, -self.x, self.y)
        def __add__(self, rhs):
            x1, x2, y1, y2 = self.x, rhs.x, self.y, rhs.y
            x1y2, y1x2, y1y2, x1x2 = x1*y2, y1*x2, y1*y2, x1*x2
            dxxyy = self.curve.d*x1x2*y1y2
            return self.curve.point((x1y2+y1x2)/(1+dxxyy),
                                    (y1y2-self.curve.a*x1x2)/(1-dxxyy))

    def __init__(self, p, d, a):
        self.p = p
        self.d = ModP(p, d)
        self.a = ModP(p, a)

    def point(self, *args):
        # This curve form represents the identity using finite
        # numbers, so it doesn't need the special infinity flag.
        # Detect a no-argument call to point() and substitute the pair
        # of integers that gives the identity.
        if len(args) == 0:
            args = [0, 1]
        return super(TwistedEdwardsCurve, self).point(*args)

    def cpoint(self, y, xparity=0):
        if not hasattr(self, 'sqrtmodp'):
            self.sqrtmodp = SqrtModP(self.p)
        y = ModP(self.p, y)
        y2 = y**2
        radicand = (y2 - 1) / (self.d * y2 - self.a)
        x = self.sqrtmodp.sqrt(radicand.n)
        if (x - xparity) % 2:
            x = -x
        return self.point(x, y)

    def __repr__(self):
        return "{}(0x{:x}, {}, {})".format(
            type(self).__name__, self.p, self.d, self.a)

p256 = WeierstrassCurve(0xffffffff00000001000000000000000000000000ffffffffffffffffffffffff, -3, 0x5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b)
p256.G = p256.point(0x6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296,0x4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5)
p256.G_order = 0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551

p384 = WeierstrassCurve(0xfffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffeffffffff0000000000000000ffffffff, -3, 0xb3312fa7e23ee7e4988e056be3f82d19181d9c6efe8141120314088f5013875ac656398d8a2ed19d2a85c8edd3ec2aef)
p384.G = p384.point(0xaa87ca22be8b05378eb1c71ef320ad746e1d3b628ba79b9859f741e082542a385502f25dbf55296c3a545e3872760ab7, 0x3617de4a96262c6f5d9e98bf9292dc29f8f41dbd289a147ce9da3113b5f0b8c00a60b1ce1d7e819d7a431d7c90ea0e5f)
p384.G_order = 0xffffffffffffffffffffffffffffffffffffffffffffffffc7634d81f4372ddf581a0db248b0a77aecec196accc52973

p521 = WeierstrassCurve(0x01ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff, -3, 0x0051953eb9618e1c9a1f929a21a0b68540eea2da725b99b315f3b8b489918ef109e156193951ec7e937b1652c0bd3bb1bf073573df883d2c34f1ef451fd46b503f00)
p521.G = p521.point(0x00c6858e06b70404e9cd9e3ecb662395b4429c648139053fb521f828af606b4d3dbaa14b5e77efe75928fe1dc127a2ffa8de3348b3c1856a429bf97e7e31c2e5bd66,0x011839296a789a3bc0045c8a5fb42c7d1bd998f54449579b446817afbd17273e662c97ee72995ef42640c550b9013fad0761353c7086a272c24088be94769fd16650)
p521.G_order = 0x01fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffa51868783bf2f966b7fcc0148f709a5d03bb5c9b8899c47aebb6fb71e91386409

curve25519 = MontgomeryCurve(2**255-19, 0x76d06, 1)
curve25519.G = curve25519.cpoint(9)

ed25519 = TwistedEdwardsCurve(2**255-19, 0x52036cee2b6ffe738cc740797779e89800700a4d4141d8ab75eb4dca135978a3, -1)
ed25519.G = ed25519.point(0x216936d3cd6e53fec0a4e231fdd6dc5c692cc7609525a7b2c9562d608f25d51a,0x6666666666666666666666666666666666666666666666666666666666666658)
ed25519.G_order = 0x1000000000000000000000000000000014def9dea2f79cd65812631a5cf5d3ed

