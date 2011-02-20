# Generate test cases for a bignum implementation.

import sys
import mathlib

def findprod(target, dir = +1, ratio=(1,1)):
    # Return two numbers whose product is as close as we can get to
    # 'target', with any deviation having the sign of 'dir', and in
    # the same approximate ratio as 'ratio'.

    r = mathlib.sqrt(target * ratio[0] * ratio[1])
    a = r / ratio[1]
    b = r / ratio[0]
    if a*b * dir < target * dir:
        a = a + 1
        b = b + 1
    assert a*b * dir >= target * dir

    best = (a,b,a*b)

    while 1:
        improved = 0
        a, b = best[:2]

        terms = mathlib.confracr(a, b, output=None)
        coeffs = [(1,0),(0,1)]
        for t in terms:
            coeffs.append((coeffs[-2][0]-t*coeffs[-1][0],
                           coeffs[-2][1]-t*coeffs[-1][1]))
        for c in coeffs:
            # a*c[0]+b*c[1] is as close as we can get it to zero. So
            # if we replace a and b with a+c[1] and b+c[0], then that
            # will be added to our product, along with c[0]*c[1].
            da, db = c[1], c[0]

            # Flip signs as appropriate.
            if (a+da) * (b+db) * dir < target * dir:
                da, db = -da, -db

            # Multiply up. We want to get as close as we can to a
            # solution of the quadratic equation in n
            #
            #    (a + n da) (b + n db) = target
            # => n^2 da db + n (b da + a db) + (a b - target) = 0
            A,B,C = da*db, b*da+a*db, a*b-target
            discrim = B^2-4*A*C
            if discrim > 0 and A != 0:
                root = mathlib.sqrt(discrim)
                vals = []
                vals.append((-B + root) / (2*A))
                vals.append((-B - root) / (2*A))
                if root * root != discrim:
                    root = root + 1
                    vals.append((-B + root) / (2*A))
                    vals.append((-B - root) / (2*A))

                for n in vals:
                    ap = a + da*n
                    bp = b + db*n
                    pp = ap*bp
                    if pp * dir >= target * dir and pp * dir < best[2]*dir:
                        best = (ap, bp, pp)
                        improved = 1

        if not improved:
            break

    return best

def hexstr(n):
    s = hex(n)
    if s[:2] == "0x": s = s[2:]
    if s[-1:] == "L": s = s[:-1]
    return s

# Tests of multiplication which exercise the propagation of the last
# carry to the very top of the number.
for i in range(1,4200):
    a, b, p = findprod((1<<i)+1, +1, (i, i*i+1))
    print "mul", hexstr(a), hexstr(b), hexstr(p)
    a, b, p = findprod((1<<i)+1, +1, (i, i+1))
    print "mul", hexstr(a), hexstr(b), hexstr(p)

# Simple tests of modpow.
for i in range(64, 4097, 63):
    modulus = mathlib.sqrt(1<<(2*i-1)) | 1
    base = mathlib.sqrt(3*modulus*modulus) % modulus
    expt = mathlib.sqrt(modulus*modulus*2/5)
    print "pow", hexstr(base), hexstr(expt), hexstr(modulus), hexstr(pow(base, expt, modulus))
    if i <= 1024:
        # Test even moduli, which can't be done by Montgomery.
        modulus = modulus - 1
        print "pow", hexstr(base), hexstr(expt), hexstr(modulus), hexstr(pow(base, expt, modulus))
