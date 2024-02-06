import sys
import numbers
import itertools
import unittest

assert sys.version_info[:2] >= (3,0), "This is Python 3 code"

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
    # with index r in its predecessor. G_0 is the whole group, and the
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
    # know one element z of rank 0. Suppose we're trying to take the
    # rth root of some g with rank i. Repeatedly multiply g by z^{r^i}
    # until its rank increases; then take the root of that
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

class QuadraticFieldExtensionModP(object):
    """Class representing Z_p[sqrt(d)] for a given non-square d.
    """
    def __init__(self, p, d, n=0, m=0):
        self.p = p
        self.d = d
        if isinstance(n, ModP):
            assert self.p == n.p
            n = n.n
        if isinstance(m, ModP):
            assert self.p == m.p
            m = m.n
        if isinstance(n, type(self)):
            self.check(n)
            m += n.m
            n = n.n
        self.n = n % p
        self.m = m % p

    @classmethod
    def constructor(cls, p, d):
        return lambda *args: cls(p, d, *args)

    def check(self, other):
        assert isinstance(other, type(self))
        assert isinstance(self, type(other))
        assert self.p == other.p
        assert self.d == other.d
    def coerce_to(self, other):
        if not isinstance(other, type(self)):
            other = type(self)(self.p, self.d, other)
        else:
            self.check(other)
        return other
    def __int__(self):
        if self.m != 0:
            raise ValueError("Can't coerce a non-element of Z_{} to integer"
                             .format(self.p))
        return int(self.n)
    def __add__(self, rhs):
        rhs = self.coerce_to(rhs)
        return type(self)(self.p, self.d,
                          (self.n + rhs.n) % self.p,
                          (self.m + rhs.m) % self.p)
    def __neg__(self):
        return type(self)(self.p, self.d,
                          -self.n % self.p,
                          -self.m % self.p)
    def __radd__(self, rhs):
        rhs = self.coerce_to(rhs)
        return type(self)(self.p, self.d,
                          (self.n + rhs.n) % self.p,
                          (self.m + rhs.m) % self.p)
    def __sub__(self, rhs):
        rhs = self.coerce_to(rhs)
        return type(self)(self.p, self.d,
                          (self.n - rhs.n) % self.p,
                          (self.m - rhs.m) % self.p)
    def __rsub__(self, rhs):
        rhs = self.coerce_to(rhs)
        return type(self)(self.p, self.d,
                          (rhs.n - self.n) % self.p,
                          (rhs.m - self.m) % self.p)
    def __mul__(self, rhs):
        rhs = self.coerce_to(rhs)
        n, m, N, M = self.n, self.m, rhs.n, rhs.m
        return type(self)(self.p, self.d,
                          (n*N + self.d*m*M) % self.p,
                          (n*M + m*N) % self.p)
    def __rmul__(self, rhs):
        return self.__mul__(rhs)
    def __div__(self, rhs):
        rhs = self.coerce_to(rhs)
        n, m, N, M = self.n, self.m, rhs.n, rhs.m
        # (n+m sqrt d)/(N+M sqrt d) = (n+m sqrt d)(N-M sqrt d)/(N^2-dM^2)
        denom = (N*N - self.d*M*M) % self.p
        if denom == 0:
            raise ValueError("division by zero")
        recipdenom = invert(denom, self.p)
        return type(self)(self.p, self.d,
                          (n*N - self.d*m*M) * recipdenom % self.p,
                          (m*N - n*M) * recipdenom % self.p)
    def __rdiv__(self, rhs):
        rhs = self.coerce_to(rhs)
        return rhs.__div__(self)
    def __truediv__(self, rhs): return self.__div__(rhs)
    def __rtruediv__(self, rhs): return self.__rdiv__(rhs)
    def __pow__(self, exponent):
        assert exponent >= 0
        n, b_to_n = 1, self
        total = type(self)(self.p, self.d, 1)
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
        return cmp((self.n, self.m), (rhs.n, rhs.m))
    def __eq__(self, rhs):
        rhs = self.coerce_to(rhs)
        return self.n == rhs.n and self.m == rhs.m
    def __ne__(self, rhs):
        rhs = self.coerce_to(rhs)
        return self.n != rhs.n or self.m != rhs.m
    def __lt__(self, rhs):
        raise ValueError("Elements of a modular ring have no ordering")
    def __le__(self, rhs):
        raise ValueError("Elements of a modular ring have no ordering")
    def __gt__(self, rhs):
        raise ValueError("Elements of a modular ring have no ordering")
    def __ge__(self, rhs):
        raise ValueError("Elements of a modular ring have no ordering")
    def __str__(self):
        if self.m == 0:
            return "0x{:x}".format(self.n)
        else:
            return "0x{:x}+0x{:x}*sqrt({:d})".format(self.n, self.m, self.d)
    def __repr__(self):
        return "{}(0x{:x},0x{:x},0x{:x},0x{:x})".format(
            type(self).__name__, self.p, self.d, self.n, self.m)
    def __hash__(self):
        return hash((type(self).__name__, self.p, self.d, self.n, self.m))

class RootInQuadraticExtension(CyclicGroupRootFinder):
    """Take rth roots in the quadratic extension of Z_p."""

    def __init__(self, r, p, d):
        self.modulus = p
        self.constructor = QuadraticFieldExtensionModP.constructor(p, d)
        super().__init__(r, p*p-1)

    def mul(self, x, y):
        return x * y
    def pow(self, x, n):
        return x ** n
    def inverse(self, x):
        return 1/x
    def identity(self):
        return self.constructor(1, 0)
    def iter_elements(self):
        p = self.modulus
        for n_plus_m in range(1, 2*p-1):
            n_min = max(0, n_plus_m-(p-1))
            n_max = min(p-1, n_plus_m)
            for n in range(n_min, n_max + 1):
                m = n_plus_m - n
                assert(0 <= n < p)
                assert(0 <= m < p)
                assert(n != 0 or m != 0)
                yield self.constructor(n, m)

    def root(self, g):
        return 0 if g == 0 else super().root(g)

class EquationSolverModP(object):
    """Class that can solve quadratics, cubics and quartics over Z_p.

    p must be a nontrivial prime (bigger than 3).
    """

    # This is a port to Z_p of reasonably standard algorithms for
    # solving quadratics, cubics and quartics over the reals.
    #
    # When you solve a cubic in R, you sometimes have to deal with
    # intermediate results that are complex numbers. In particular,
    # you have to solve a quadratic whose coefficients are in R but
    # its roots may be complex, and then having solved that quadratic,
    # you need to iterate over all three cube roots of the solution in
    # order to recover all the roots of your cubic. (Even if the cubic
    # ends up having three real roots, you can't calculate them
    # without going through those complex intermediate values.)
    #
    # So over Z_p, the same thing applies: we're going to need to be
    # able to solve any quadratic with coefficients in Z_p, even if
    # its discriminant turns out not to be a quadratic residue mod p,
    # and then we'll need to find _three_ cube roots of the result,
    # even if p == 2 (mod 3) so that numbers only have one cube root
    # each.
    #
    # Both of these problems can be solved at once if we work in the
    # finite field GF(p^2), i.e. make a quadratic field extension of
    # Z_p by adjoining a square root of some non-square d. The
    # multiplicative group of GF(p^2) is cyclic and has order p^2-1 =
    # (p-1)(p+1), with the mult group of Z_p forming the unique
    # subgroup of order (p-1) within it. So we've multiplied the group
    # order by p+1, which is even (since by assumption p > 3), and
    # therefore a square root is now guaranteed to exist for every
    # number in the Z_p subgroup. Moreover, no matter whether p itself
    # was congruent to 1 or 2 mod 3, p^2 is always congruent to 1,
    # which means that the mult group of GF(p^2) has order divisible
    # by 3. So there are guaranteed to be three distinct cube roots of
    # unity, and hence, three cube roots of any number that's a cube
    # at all.
    #
    # Quartics don't introduce any additional problems. To solve a
    # quartic, you factorise it into two quadratic factors, by solving
    # a cubic to find one of the coefficients. So if you can already
    # solve cubics, then you're more or less done. The only wrinkle is
    # that the two quadratic factors will have coefficients in GF(p^2)
    # but not necessarily in Z_p. But that doesn't stop us at least
    # _trying_ to solve them by taking square roots in GF(p^2) - and
    # if the discriminant of one of those quadratics has is not a
    # square even in GF(p^2), then its solutions will only exist if
    # you escalate further to GF(p^4), in which case the answer is
    # simply that there aren't any solutions in Z_p to that quadratic.

    def __init__(self, p):
        self.p = p
        self.nonsquare_mod_p = d = RootModP(2, p).z
        self.constructor = QuadraticFieldExtensionModP.constructor(p, d)
        self.sqrt = RootInQuadraticExtension(2, p, d)
        self.cbrt = RootInQuadraticExtension(3, p, d)

    def solve_quadratic(self, a, b, c):
        "Solve ax^2 + bx + c = 0."
        a, b, c = map(self.constructor, (a, b, c))
        assert a != 0
        return self.solve_monic_quadratic(b/a, c/a)

    def solve_monic_quadratic(self, b, c):
        "Solve x^2 + bx + c = 0."
        b, c = map(self.constructor, (b, c))
        s = b/2
        return [y - s for y in self.solve_depressed_quadratic(c - s*s)]

    def solve_depressed_quadratic(self, c):
        "Solve x^2 + c = 0."
        return self.sqrt.all_roots(-c)

    def solve_cubic(self, a, b, c, d):
        "Solve ax^3 + bx^2 + cx + d = 0."
        a, b, c, d = map(self.constructor, (a, b, c, d))
        assert a != 0
        return self.solve_monic_cubic(b/a, c/a, d/a)

    def solve_monic_cubic(self, b, c, d):
        "Solve x^3 + bx^2 + cx + d = 0."
        b, c, d = map(self.constructor, (b, c, d))
        s = b/3
        return [y - s for y in self.solve_depressed_cubic(
            c - 3*s*s, 2*s*s*s - c*s + d)]

    def solve_depressed_cubic(self, c, d):
        "Solve x^3 + cx + d = 0."
        c, d = map(self.constructor, (c, d))
        solutions = set()
        # To solve x^3 + cx + d = 0, set p = -c/3, then
        # substitute x = z + p/z to get z^6 + d z^3 + p^3 = 0.
        # Solve that quadratic for z^3, then take cube roots.
        p = -c/3
        for z3 in self.solve_monic_quadratic(d, p**3):
            # As I understand the theory, we _should_ only need to
            # take cube roots of one root of that quadratic: the other
            # one should give the same set of answers after you map
            # each one through z |-> z+p/z. But speed isn't at a
            # premium here, so I'll do this the way that must work.
            for z in self.cbrt.all_roots(z3):
                solutions.add(z + p/z)
        return solutions

    def solve_quartic(self, a, b, c, d, e):
        "Solve ax^4 + bx^3 + cx^2 + dx + e = 0."
        a, b, c, d, e = map(self.constructor, (a, b, c, d, e))
        assert a != 0
        return self.solve_monic_quartic(b/a, c/a, d/a, e/a)

    def solve_monic_quartic(self, b, c, d, e):
        "Solve x^4 + bx^3 + cx^2 + dx + e = 0."
        b, c, d, e = map(self.constructor, (b, c, d, e))
        s = b/4
        return [y - s for y in self.solve_depressed_quartic(
            c - 6*s*s, d - 2*c*s + 8*s*s*s, e - d*s + c*s*s - 3*s*s*s*s)]

    def solve_depressed_quartic(self, c, d, e):
        "Solve x^4 + cx^2 + dx + e = 0."
        c, d, e = map(self.constructor, (c, d, e))
        solutions = set()
        # To solve an equation of this form, we search for a value y
        # such that subtracting the original polynomial from (x^2+y)^2
        # yields a quadratic of the special form (ux+v)^2.
        #
        # Then our equation is rewritten as (x^2+y)^2 - (ux+v)^2 = 0
        # i.e. ((x^2+y) + (ux+v)) ((x^2+y) - (ux+v)) = 0
        # i.e. the product of two quadratics, each of which we then solve.
        #
        # To find y, we write down the discriminant of the quadratic
        # (x^2+y)^2 - (x^4 + cx^2 + dx + e) and set it to 0, which
        # gives a cubic in y. Maxima gives the coefficients as
        # (-8)y^3 + (4c)y^2 + (8e)y + (d^2-4ce).
        #
        # As above, we _should_ only need one value of y. But I go
        # through them all just in case, because I don't care about
        # speed, and because checking the assertions inside this loop
        # for every value is extra reassurance that I've done all of
        # this right.
        for y in self.solve_cubic(-8, 4*c, 8*e, d*d-4*c*e):
            # Subtract the original equation from (x^2+y)^2 to get the
            # coefficients of our quadratic residual.
            A, B, C = 2*y-c, -d, y*y-e
            # Expect that to have zero discriminant, i.e. a repeated root.
            assert B*B - 4*A*C == 0
            # If (Ax^2+Bx+C) == (ux+v)^2 then we have u^2=A, 2uv=B, v^2=C.
            # So we can either recover u as sqrt(A) or v as sqrt(C), and
            # whichever we did, find the other from B by division. But
            # either of the end coefficients might be zero, so we have
            # to be prepared to try either option.
            try:
                if A != 0:
                    u = self.sqrt.root(A)
                    v = B/(2*u)
                elif C != 0:
                    v = self.sqrt.root(C)
                    u = B/(2*v)
                else:
                    # One last possibility is that all three coefficients
                    # of our residual quadratic are 0, in which case,
                    # obviously, u=v=0 as well.
                    u = v = 0
            except ValueError:
                # If Ax^2+Bx+C looked like a perfect square going by
                # its discriminant, but actually taking the square
                # root of A or C threw an exception, that means that
                # it's the square of a polynomial whose coefficients
                # live in a yet-higher field extension of Z_p. In that
                # case we're not going to end up with roots of the
                # original quartic in Z_p if we start from here!
                continue
            # So now our quartic is factorised into the form
            # (x^2 - ux - v + y) (x^2 + ux + v + y).
            for x in self.solve_monic_quadratic(-u, y-v):
                solutions.add(x)
            for x in self.solve_monic_quadratic(u, y+v):
                solutions.add(x)
        return solutions

class EquationSolverTest(unittest.TestCase):
    def testQuadratic(self):
        E = EquationSolverModP(11)
        solns = E.solve_quadratic(3, 2, 6)
        self.assertEqual(sorted(map(str, solns)), ["0x1", "0x2"])

    def testCubic(self):
        E = EquationSolverModP(11)
        solns = E.solve_cubic(7, 2, 0, 2)
        self.assertEqual(sorted(map(str, solns)), ["0x1", "0x2", "0x3"])

    def testQuartic(self):
        E = EquationSolverModP(11)
        solns = E.solve_quartic(9, 9, 7, 1, 7)
        self.assertEqual(sorted(map(str, solns)), ["0x1", "0x2", "0x3", "0x4"])

if __name__ == "__main__":
    import sys
    if sys.argv[1:] == ["--test"]:
        sys.argv[1:2] = []
        unittest.main()
