import numbers
import itertools

def invert(a, b):
    "Multiplicative inverse of a mod b. a,b must be coprime."
    A = (a, 1, 0)
    B = (b, 0, 1)
    while B[0]:
        q = A[0] // B[0]
        A, B = B, tuple(Ai - q*Bi for Ai, Bi in zip(A, B))
    assert abs(A[0]) == 1
    return A[1]*A[0] % b

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
        return type(self)(self.p, (self.n * invert(rhs.n, self.p)) % self.p)
    def __rdiv__(self, rhs):
        rhs = self.coerce_to(rhs)
        return type(self)(self.p, (rhs.n * invert(self.n, self.p)) % self.p)
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
    def __hash__(self):
        return hash((type(self).__name__, self.p, self.n))
