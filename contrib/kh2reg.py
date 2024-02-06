#!/usr/bin/env python3

# Convert OpenSSH known_hosts and known_hosts2 files to "new format" PuTTY
# host keys.
#   usage:
#     kh2reg.py [ --win ] known_hosts1 2 3 4 ... > hosts.reg
#       Creates a Windows .REG file (double-click to install).
#     kh2reg.py --unix    known_hosts1 2 3 4 ... > sshhostkeys
#       Creates data suitable for storing in ~/.putty/sshhostkeys (Unix).
# Line endings are someone else's problem as is traditional.
# Should run under either Python 2 or 3.

import fileinput
import base64
import struct
import string
import re
import sys
import argparse
import itertools
import collections
import hashlib
from functools import reduce

def winmungestr(s):
    "Duplicate of PuTTY's mungestr() in winstore.c:1.10 for Registry keys"
    candot = 0
    r = ""
    for c in s:
        if c in ' \*?%~' or ord(c)<ord(' ') or (c == '.' and not candot):
            r = r + ("%%%02X" % ord(c))
        else:
            r = r + c
        candot = 1
    return r

def strtoint(s):
    "Convert arbitrary-length big-endian binary data to a Python int"
    bytes = struct.unpack(">{:d}B".format(len(s)), s)
    return reduce ((lambda a, b: (int(a) << 8) + int(b)), bytes)

def strtoint_le(s):
    "Convert arbitrary-length little-endian binary data to a Python int"
    bytes = reversed(struct.unpack(">{:d}B".format(len(s)), s))
    return reduce ((lambda a, b: (int(a) << 8) + int(b)), bytes)

def inttohex(n):
    "Convert int to lower-case hex."
    return "0x{:x}".format(n)

def warn(s):
    "Warning with file/line number"
    sys.stderr.write("%s:%d: %s\n"
                     % (fileinput.filename(), fileinput.filelineno(), s))

class HMAC(object):
    def __init__(self, hashclass, blocksize):
        self.hashclass = hashclass
        self.blocksize = blocksize
        self.struct = struct.Struct(">{:d}B".format(self.blocksize))
    def pad_key(self, key):
        return key + b'\0' * (self.blocksize - len(key))
    def xor_key(self, key, xor):
        return self.struct.pack(*[b ^ xor for b in self.struct.unpack(key)])
    def keyed_hash(self, key, padbyte, string):
        return self.hashclass(self.xor_key(key, padbyte) + string).digest()
    def compute(self, key, string):
        if len(key) > self.blocksize:
            key = self.hashclass(key).digest()
        key = self.pad_key(key)
        return self.keyed_hash(key, 0x5C, self.keyed_hash(key, 0x36, string))

def openssh_hashed_host_match(hashed_host, try_host):
    if hashed_host.startswith(b'|1|'):
        salt, expected = hashed_host[3:].split(b'|')
        salt = base64.decodebytes(salt)
        expected = base64.decodebytes(expected)
        mac = HMAC(hashlib.sha1, 64)
    else:
        return False # unrecognised magic number prefix

    return mac.compute(salt, try_host) == expected

def invert(n, p):
    """Compute inverse mod p."""
    if n % p == 0:
        raise ZeroDivisionError()
    a = n, 1, 0
    b = p, 0, 1
    while b[0]:
        q = a[0] // b[0]
        a = a[0] - q*b[0], a[1] - q*b[1], a[2] - q*b[2]
        b, a = a, b
    assert abs(a[0]) == 1
    return a[1]*a[0]

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
        self.zinv = invert(self.z, self.p)

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

    instances = {}

    @classmethod
    def make(cls, p):
        if p not in cls.instances:
            cls.instances[p] = cls(p)
        return cls.instances[p]

    @classmethod
    def root(cls, n, p):
        return cls.make(p).sqrt(n)

NistCurve = collections.namedtuple("NistCurve", "p a b")
nist_curves = {
    "ecdsa-sha2-nistp256": NistCurve(0xffffffff00000001000000000000000000000000ffffffffffffffffffffffff, 0xffffffff00000001000000000000000000000000fffffffffffffffffffffffc, 0x5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b),
    "ecdsa-sha2-nistp384": NistCurve(0xfffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffeffffffff0000000000000000ffffffff, 0xfffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffeffffffff0000000000000000fffffffc, 0xb3312fa7e23ee7e4988e056be3f82d19181d9c6efe8141120314088f5013875ac656398d8a2ed19d2a85c8edd3ec2aef),
    "ecdsa-sha2-nistp521": NistCurve(0x01ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff, 0x01fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffc, 0x0051953eb9618e1c9a1f929a21a0b68540eea2da725b99b315f3b8b489918ef109e156193951ec7e937b1652c0bd3bb1bf073573df883d2c34f1ef451fd46b503f00),
}

class BlankInputLine(Exception):
    pass

class UnknownKeyType(Exception):
    def __init__(self, keytype):
        self.keytype = keytype

class KeyFormatError(Exception):
    def __init__(self, msg):
        self.msg = msg

def handle_line(line, output_formatter, try_hosts):
    try:
        # Remove leading/trailing whitespace (should zap CR and LF)
        line = line.strip()

        # Skip blanks and comments
        if line == '' or line[0] == '#':
            raise BlankInputLine

        # Split line on spaces.
        fields = line.split(' ')

        # Common fields
        hostpat = fields[0]
        keyparams = []      # placeholder
        keytype = ""        # placeholder

        # Grotty heuristic to distinguish known_hosts from known_hosts2:
        # is second field entirely decimal digits?
        if re.match (r"\d*$", fields[1]):

            # Treat as SSH-1-type host key.
            # Format: hostpat bits10 exp10 mod10 comment...
            # (PuTTY doesn't store the number of bits.)
            keyparams = list(map(int, fields[2:4]))
            keytype = "rsa"

        else:

            # Treat as SSH-2-type host key.
            # Format: hostpat keytype keyblob64 comment...
            sshkeytype, blob = fields[1], base64.decodebytes(
                fields[2].encode("ASCII"))

            # 'blob' consists of a number of
            #   uint32    N (big-endian)
            #   uint8[N]  field_data
            subfields = []
            while blob:
                sizefmt = ">L"
                (size,) = struct.unpack (sizefmt, blob[0:4])
                size = int(size)   # req'd for slicage
                (data,) = struct.unpack (">%lus" % size, blob[4:size+4])
                subfields.append(data)
                blob = blob [struct.calcsize(sizefmt) + size : ]

            # The first field is keytype again.
            if subfields[0].decode("ASCII") != sshkeytype:
                raise KeyFormatError("""
                    outer and embedded key types do not match: '%s', '%s'
                    """ % (sshkeytype, subfields[1]))

            # Translate key type string into something PuTTY can use, and
            # munge the rest of the data.
            if sshkeytype == "ssh-rsa":
                keytype = "rsa2"
                # The rest of the subfields we can treat as an opaque list
                # of bignums (same numbers and order as stored by PuTTY).
                keyparams = list(map(strtoint, subfields[1:]))

            elif sshkeytype == "ssh-dss":
                keytype = "dss"
                # Same again.
                keyparams = list(map(strtoint, subfields[1:]))

            elif sshkeytype in nist_curves:
                keytype = sshkeytype
                # Have to parse this a bit.
                if len(subfields) > 3:
                    raise KeyFormatError("too many subfields in blob")
                (curvename, Q) = subfields[1:]
                # First is yet another copy of the key name.
                if not re.match("ecdsa-sha2-" + re.escape(
                        curvename.decode("ASCII")), sshkeytype):
                    raise KeyFormatError("key type mismatch ('%s' vs '%s')"
                            % (sshkeytype, curvename))
                # Second contains key material X and Y (hopefully).
                # First a magic octet indicating point compression.
                point_type = struct.unpack_from("B", Q, 0)[0]
                Qrest = Q[1:]
                if point_type == 4:
                    # Then two equal-length bignums (X and Y).
                    bnlen = len(Qrest)
                    if (bnlen % 1) != 0:
                        raise KeyFormatError("odd-length X+Y")
                    bnlen = bnlen // 2
                    x = strtoint(Qrest[:bnlen])
                    y = strtoint(Qrest[bnlen:])
                elif 2 <= point_type <= 3:
                    # A compressed point just specifies X, and leaves
                    # Y implicit except for parity, so we have to
                    # recover it from the curve equation.
                    curve = nist_curves[sshkeytype]
                    x = strtoint(Qrest)
                    yy = (x*x*x + curve.a*x + curve.b) % curve.p
                    y = SqrtModP.root(yy, curve.p)
                    if y % 2 != point_type % 2:
                        y = curve.p - y

                keyparams = [curvename, x, y]

            elif sshkeytype in { "ssh-ed25519",  "ssh-ed448" }:
                keytype = sshkeytype

                if len(subfields) != 2:
                    raise KeyFormatError("wrong number of subfields in blob")
                # Key material y, with the top bit being repurposed as
                # the expected parity of the associated x (point
                # compression).
                y = strtoint_le(subfields[1])
                x_parity = y >> 255
                y &= ~(1 << 255)

                # Curve parameters.
                p, d, a = {
                    "ssh-ed25519": (2**255 - 19, 0x52036cee2b6ffe738cc740797779e89800700a4d4141d8ab75eb4dca135978a3, -1),
                    "ssh-ed448": (2**448-2**224-1, -39081, +1),
                }[sshkeytype]

                # Recover x^2 = (y^2 - 1) / (d y^2 - a).
                xx = (y*y - 1) * invert(d*y*y - a, p) % p

                # Take the square root.
                x = SqrtModP.root(xx, p)

                # Pick the square root of the correct parity.
                if (x % 2) != x_parity:
                    x = p - x

                keyparams = [x, y]
            else:
                raise UnknownKeyType(sshkeytype)

        # Now print out one line per host pattern, discarding wildcards.
        for host in hostpat.split(','):
            if re.search (r"[*?!]", host):
                warn("skipping wildcard host pattern '%s'" % host)
                continue

            if re.match (r"\|", host):
                for try_host in try_hosts:
                    if openssh_hashed_host_match(host.encode('ASCII'),
                                                 try_host.encode('UTF-8')):
                        host = try_host
                        break
                else:
                    warn("unable to match hashed hostname '%s'" % host)
                    continue

            m = re.match (r"\[([^]]*)\]:(\d*)$", host)
            if m:
                (host, port) = m.group(1,2)
                port = int(port)
            else:
                port = 22
            # Slightly bizarre output key format: 'type@port:hostname'
            # XXX: does PuTTY do anything useful with literal IP[v4]s?
            key = keytype + ("@%d:%s" % (port, host))
            # Most of these are numbers, but there's the occasional
            # string that needs passing through
            value = ",".join(map(
                lambda x: x if isinstance(x, str)
                else x.decode('ASCII') if isinstance(x, bytes)
                else inttohex(x), keyparams))
            output_formatter.key(key, value)

    except UnknownKeyType as k:
        warn("unknown SSH key type '%s', skipping" % k.keytype)
    except KeyFormatError as k:
        warn("trouble parsing key (%s), skipping" % k.msg)
    except BlankInputLine:
        pass

class OutputFormatter(object):
    def __init__(self, fh):
        self.fh = fh
    def header(self):
        pass
    def trailer(self):
        pass

class WindowsOutputFormatter(OutputFormatter):
    def header(self):
        # Output REG file header.
        self.fh.write("""REGEDIT4

[HKEY_CURRENT_USER\Software\SimonTatham\PuTTY\SshHostKeys]
""")

    def key(self, key, value):
        # XXX: worry about double quotes?
        self.fh.write("\"%s\"=\"%s\"\n" % (winmungestr(key), value))

    def trailer(self):
        # The spec at http://support.microsoft.com/kb/310516 says we need
        # a blank line at the end of the reg file:
        #
        #   Note the registry file should contain a blank line at the
        #   bottom of the file.
        #
        self.fh.write("\n")

class UnixOutputFormatter(OutputFormatter):
    def key(self, key, value):
        self.fh.write('%s %s\n' % (key, value))

def main():
    parser = argparse.ArgumentParser(
        description="Convert OpenSSH known hosts files to PuTTY's format.")
    group = parser.add_mutually_exclusive_group()
    group.add_argument(
        "--windows", "--win", action='store_const',
        dest="output_formatter_class", const=WindowsOutputFormatter,
        help="Produce Windows .reg file output that regedit.exe can import"
        " (default).")
    group.add_argument(
        "--unix", action='store_const',
        dest="output_formatter_class", const=UnixOutputFormatter,
        help="Produce a file suitable for use as ~/.putty/sshhostkeys.")
    parser.add_argument("-o", "--output", type=argparse.FileType("w"),
                        default=argparse.FileType("w")("-"),
                        help="Output file to write to (default stdout).")
    parser.add_argument("--hostname", action="append",
                        help="Host name(s) to try matching against hashed "
                        "host entries in input.")
    parser.add_argument("infile", nargs="*",
                        help="Input file(s) to read from (default stdin).")
    parser.set_defaults(output_formatter_class=WindowsOutputFormatter,
                        hostname=[])
    args = parser.parse_args()

    output_formatter = args.output_formatter_class(args.output)
    output_formatter.header()
    for line in fileinput.input(args.infile):
        handle_line(line, output_formatter, args.hostname)
    output_formatter.trailer()

if __name__ == "__main__":
    main()
