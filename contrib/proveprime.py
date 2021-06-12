#!/usr/bin/env python3

import argparse
import functools
import math
import os
import re
import subprocess
import sys
import itertools

def gen_names():
    for i in itertools.count():
        name = "p{:d}".format(i)
        if name not in nameset:
            yield name
nameset=set()
names = gen_names()

class YafuError(Exception):
    pass

verbose = False
def diag(*args):
    if verbose:
        print(*args, file=sys.stderr)

factorcache = set()
factorcachefile = None
def cache_factor(f):
    if f not in factorcache:
        factorcache.add(f)
    if factorcachefile is not None:
        factorcachefile.write("{:d}\n".format(f))
        factorcachefile.flush()

yafu = None
yafu_pattern = re.compile(rb"^P\d+ = (\d+)$")
def call_yafu(n):
    n_orig = n
    diag("starting yafu", n_orig)
    p = subprocess.Popen([yafu, "-v", "-v"], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE)
    p.stdin.write("{:d}\n".format(n).encode("ASCII"))
    p.stdin.close()
    factors = []
    for line in iter(p.stdout.readline, b''):
        line = line.rstrip(b"\r\n")
        diag("yafu output:", line.decode())
        m = yafu_pattern.match(line)
        if m is not None:
            f = int(m.group(1))
            if n % f != 0:
                raise YafuError("bad yafu factor {:d}".format(f))
            factors.append(f)
            if f >> 64:
                cache_factor(f)
            n //= f
    p.wait()
    diag("done yafu", n_orig)
    return factors, n

def factorise(n):
    allfactors = []
    for f in factorcache:
        if n % f == 0:
            n //= f
            allfactors.append(f)
    while n > 1:
        factors, n = call_yafu(n)
        allfactors.extend(factors)
    return sorted(allfactors)

def product(ns):
    return functools.reduce(lambda a,b: a*b, ns, 1)

smallprimes = set()
commands = {}

def proveprime(p, name=None):
    if p >> 32 == 0:
        smallprimes.add(p)
        return "{:d}".format(p)

    if name is None:
        name = next(names)
    print("{} = {:d}".format(name, p))

    fs = factorise(p-1)
    fs.reverse()
    prod = product(fs)
    qs = []
    for q in fs:
        newprod = prod // q
        if newprod * newprod * newprod > p:
            prod = newprod
        else:
            qs.append(q)
    assert prod == product(qs)
    assert prod * prod * prod > p
    qset = set(qs)
    qnamedict = {q: proveprime(q) for q in qset}
    qnames = [qnamedict[q] for q in qs]
    for w in itertools.count(2):
        assert pow(w, p-1, p) == 1, "{}={:d} is not prime!".format(name, p)
        diag("trying witness", w, "for", p)
        for q in qset:
            wpower = pow(w, (p-1) // q, p) - 1
            if math.gcd(wpower, p) != 1:
                break
        else:
            diag("found witness", w, "for", p)
            break
    commands[p]= (name, w, qnames)
    return name

def main():
    parser = argparse.ArgumentParser(description='')
    parser.add_argument("prime", nargs="+",
                        help="Number to prove prime. Can be prefixed by a "
                        "variable name and '=', e.g. 'x=9999999967'.")
    parser.add_argument("--cryptsuite", action="store_true",
                        help="Generate abbreviated Pockle calls suitable "
                        "for the tests in cryptsuite.py.")
    parser.add_argument("--yafu", default="yafu",
                        help="yafu binary to help with factoring.")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Write diagnostics to standard error.")
    parser.add_argument("--cache", help="Cache of useful factors of things.")
    args = parser.parse_args()

    global verbose, yafu
    verbose = args.verbose
    yafu = args.yafu

    if args.cache is not None:
        with open(args.cache, "r") as fh:
            for line in iter(fh.readline, ""):
                factorcache.add(int(line.rstrip("\r\n")))
        global factorcachefile
        factorcachefile = open(args.cache, "a")

    for ps in args.prime:
        name, value = (ps.split("=", 1) if "=" in ps
                       else (None, ps))
        proveprime(int(value, 0), name)

    print("po = pockle_new()")
    if len(smallprimes) > 0:
        if args.cryptsuite:
            print("add_small(po, {})".format(
                ", ".join("{:d}".format(q) for q in sorted(smallprimes))))
        else:
            for q in sorted(smallprimes):
                print("pockle_add_small_prime(po, {:d})".format(q))
    for p, (name, w, qnames) in sorted(commands.items()):
        print("{cmd}(po, {name}, [{qs}], {w:d})".format(
            cmd = "add" if args.cryptsuite else "pockle_add_prime",
            name=name, w=w, qs=", ".join(qnames)))

if __name__ == '__main__':
    main()
