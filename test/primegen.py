#!/usr/bin/env python3

from testcrypt import *
import base64
import argparse
import itertools

assert sys.version_info[:2] >= (3,0), "This is Python 3 code"

def main():
    opener = lambda mode: lambda fname: lambda: argparse.FileType(mode)(fname)
    parser = argparse.ArgumentParser(description='')
    IntArg = lambda x: int(x, 0)
    parser.add_argument("bits", type=IntArg, nargs="?", default=1024)
    parser.add_argument("-s", "--seed")
    parser.add_argument("-f", "--firstbits", type=IntArg, default=1)
    parser.add_argument("--fast", action='store_const',
                        dest='policy', const='provable_fast')
    parser.add_argument("--complex", action='store_const',
                        dest='policy', const='provable_maurer_complex')
    parser.add_argument("-q", "--quiet", action='store_true')
    parser.add_argument("-b", "--binary", action='store_const',
                        dest='fmt', const='{:b}')
    parser.add_argument("-x", "--hex", action='store_const',
                        dest='fmt', const='{:x}')
    parser.add_argument("-o", "--output", type=opener("w"),
                        default=opener("w")("-"),
                        help="file to write the prime to")
    parser.add_argument("--mpu", type=opener("w"),
                        help="MPU certificate output file")
    parser.add_argument("--safe", action='store_true')
    parser.set_defaults(fmt='{:d}', policy='provable_maurer_simple')
    args = parser.parse_args()

    seed = args.seed
    if seed is None:
        with open("/dev/urandom", "rb") as f:
            seed = base64.b64encode(f.read(32)).decode("ASCII")

    if not args.quiet:
        print("seed =", seed)
    random_make_prng('sha256', seed)
    assert args.firstbits > 0
    nfirst = next(i for i in itertools.count() if (args.firstbits >> i) == 0)
    pgc = primegen_new_context(args.policy)
    if args.safe:
        while True:
            pcs_q = pcs_new_with_firstbits(args.bits - 1,
                                           args.firstbits, nfirst)
            pcs_try_sophie_germain(pcs_q)
            q = primegen_generate(pgc, pcs_q)
            pcs = pcs_new(args.bits)
            pcs_require_residue_1_mod_prime(pcs, q)
            pcs_set_oneshot(pcs)
            p = primegen_generate(pgc, pcs)
            if p is not None:
                break
    else:
        pcs = pcs_new_with_firstbits(args.bits, args.firstbits, nfirst)
        p = primegen_generate(pgc, pcs)

    with args.output() as f:
        print(args.fmt.format(int(p)), file=f)

    if args.mpu is not None:
        s = primegen_mpu_certificate(pgc, p)
        with args.mpu() as f:
            f.write(s.decode("ASCII"))

if __name__ == '__main__':
    main()
