import numbers
import itertools
import unittest

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

class CyclicGroupRootFinder(object):
    """Class for finding rth roots in a cyclic group. r must be prime."""

    # Basic strategy:
    #
    # We write |G| = r^k u, with u coprime to r. This gives us a
    # nested sequence of subgroups G = G_0 > G_1 > ... > G_k, each
    # with index 3 in its predecessor. G_0 is the whole group, and the
    # innermost G_k has order u.
    #
    # Within G_k, you can take an rth root by raising an element to
    # the power of (r^{-1} mod u). If k=0 (so G = G_0 = G_k) then
    # that's all that's needed: every element has a unique rth root.
    # But if k>0, then things go differently.
    #
    # Define the 'rank' of an element g as the highest i such that
    # g \in G_i. Elements of rank 0 are the non-rth-powers: they don't
    # even _have_ an rth root. Elements of rank k are the easy ones to
    # take rth roots of, as above.
    #
    # In between, you can follow an inductive process, as long as you
    # know one element z of index 0. Suppose we're trying to take the
    # rth root of some g with index i. Repeatedly multiply g by
    # z^{r^i} until its index increases; then take the root of that
    # (recursively), and divide off z^{r^{i-1}} once you're done.

    def __init__(self, r, order):
        self.order = order # order of G
        self.r = r
        self.k = next(k for k in itertools.count()
                      if self.order % (r**(k+1)) != 0)
        self.u = self.order // (r**self.k)
        self.z = next(z for z in self.iter_elements()
                      if self.index(z) == 0)
        self.zinv = self.inverse(self.z)
        self.root_power = invert(self.r, self.u) if self.u > 1 else 0

        self.roots_of_unity = {self.identity()}
        if self.k > 0:
            exponent = self.order // self.r
            for z in self.iter_elements():
                root_of_unity = self.pow(z, exponent)
                if root_of_unity not in self.roots_of_unity:
                    self.roots_of_unity.add(root_of_unity)
                    if len(self.roots_of_unity) == r:
                        break

    def index(self, g):
        h = self.pow(g, self.u)
        for i in range(self.k+1):
            if h == self.identity():
                return self.k - i
            h = self.pow(h, self.r)
        assert False, ("Not a cyclic group! Raising {} to u r^k should give e."
                       .format(g))

    def all_roots(self, g):
        try:
            r = self.root(g)
        except ValueError:
            return []
        return {r * rou for rou in self.roots_of_unity}

    def root(self, g):
        i = self.index(g)
        if i == 0 and self.k > 0:
            raise ValueError("{} has no {}th root".format(g, self.r))
        out = self.root_recurse(g, i)
        assert self.pow(out, self.r) == g
        return out

    def root_recurse(self, g, i):
        if i == self.k:
            return self.pow(g, self.root_power)
        z_in = self.pow(self.z, self.r**i)
        z_out = self.pow(self.zinv, self.r**(i-1))
        adjust = self.identity()
        while True:
            g = self.mul(g, z_in)
            adjust = self.mul(adjust, z_out)
            i2 = self.index(g)
            if i2 > i:
                return self.mul(self.root_recurse(g, i2), adjust)

class AdditiveGroupRootFinder(CyclicGroupRootFinder):
    """Trivial test subclass for CyclicGroupRootFinder.

    Represents a cyclic group of any order additively, as the integers
    mod n under addition. This makes root-finding trivial without
    having to use the complicated algorithm above, and therefore it's
    a good way to test the complicated algorithm under conditions
    where the right answers are obvious."""

    def __init__(self, r, order):
        super().__init__(r, order)

    def mul(self, x, y):
        return (x + y) % self.order
    def pow(self, x, n):
        return (x * n) % self.order
    def inverse(self, x):
        return (-x) % self.order
    def identity(self):
        return 0
    def iter_elements(self):
        return range(self.order)

class TestCyclicGroupRootFinder(unittest.TestCase):
    def testRootFinding(self):
        for order in 10, 11, 12, 18:
            grf = AdditiveGroupRootFinder(3, order)
            for i in range(order):
                try:
                    r = grf.root(i)
                except ValueError:
                    r = None

                if order % 3 == 0 and i % 3 != 0:
                    self.assertEqual(r, None)
                else:
                    self.assertEqual(r*3 % order, i)

class RootModP(CyclicGroupRootFinder):
    """The live class that can take rth roots mod a prime."""

    def __init__(self, r, p):
        self.modulus = p
        super().__init__(r, p-1)

    def mul(self, x, y):
        return (x * y) % self.modulus
    def pow(self, x, n):
        return pow(x, n, self.modulus)
    def inverse(self, x):
        return invert(x, self.modulus)
    def identity(self):
        return 1
    def iter_elements(self):
        return range(1, self.modulus)

    def root(self, g):
        return 0 if g == 0 else super().root(g)

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

if __name__ == "__main__":
    import sys
    if sys.argv[1:] == ["--test"]:
        sys.argv[1:2] = []
        unittest.main()
