#! /usr/bin/env python

# Convert OpenSSH known_hosts and known_hosts2 files to "new format" PuTTY
# host keys.
#   usage:
#     kh2reg.py [ --win ] known_hosts1 2 3 4 ... > hosts.reg
#       Creates a Windows .REG file (double-click to install).
#     kh2reg.py --unix    known_hosts1 2 3 4 ... > sshhostkeys
#       Creates data suitable for storing in ~/.putty/sshhostkeys (Unix).
# Line endings are someone else's problem as is traditional.
# Originally developed for Python 1.5.2, but probably won't run on that
# any more.

import fileinput
import base64
import struct
import string
import re
import sys
import getopt

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

def strtolong(s):
    "Convert arbitrary-length big-endian binary data to a Python long"
    bytes = struct.unpack(">%luB" % len(s), s)
    return reduce ((lambda a, b: (long(a) << 8) + long(b)), bytes)

def strtolong_le(s):
    "Convert arbitrary-length little-endian binary data to a Python long"
    bytes = reversed(struct.unpack(">%luB" % len(s), s))
    return reduce ((lambda a, b: (long(a) << 8) + long(b)), bytes)

def longtohex(n):
    """Convert long int to lower-case hex.

    Ick, Python (at least in 1.5.2) doesn't appear to have a way to
    turn a long int into an unadorned hex string -- % gets upset if the
    number is too big, and raw hex() uses uppercase (sometimes), and
    adds unwanted "0x...L" around it."""

    plain=string.lower(re.match(r"0x([0-9A-Fa-f]*)l?$", hex(n), re.I).group(1))
    return "0x" + plain

def warn(s):
    "Warning with file/line number"
    sys.stderr.write("%s:%d: %s\n"
                     % (fileinput.filename(), fileinput.filelineno(), s))

output_type = 'windows'

try:
    optlist, args = getopt.getopt(sys.argv[1:], '', [ 'win', 'unix' ])
    if filter(lambda x: x[0] == '--unix', optlist):
        output_type = 'unix'
except getopt.error, e:
    sys.stderr.write(str(e) + "\n")
    sys.exit(1)

if output_type == 'windows':
    # Output REG file header.
    sys.stdout.write("""REGEDIT4

[HKEY_CURRENT_USER\Software\SimonTatham\PuTTY\SshHostKeys]
""")

class BlankInputLine(Exception):
    pass

class UnknownKeyType(Exception):
    def __init__(self, keytype):
        self.keytype = keytype

class KeyFormatError(Exception):
    def __init__(self, msg):
        self.msg = msg

# Now process all known_hosts input.
for line in fileinput.input(args):

    try:
        # Remove leading/trailing whitespace (should zap CR and LF)
        line = string.strip (line)

        # Skip blanks and comments
        if line == '' or line[0] == '#':
            raise BlankInputLine

        # Split line on spaces.
        fields = string.split (line, ' ')

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
            keyparams = map (long, fields[2:4])
            keytype = "rsa"

        else:

            # Treat as SSH-2-type host key.
            # Format: hostpat keytype keyblob64 comment...
            sshkeytype, blob = fields[1], base64.decodestring (fields[2])

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
            if subfields[0] != sshkeytype:
                raise KeyFormatError("""
                    outer and embedded key types do not match: '%s', '%s'
                    """ % (sshkeytype, subfields[1]))

            # Translate key type string into something PuTTY can use, and
            # munge the rest of the data.
            if sshkeytype == "ssh-rsa":
                keytype = "rsa2"
                # The rest of the subfields we can treat as an opaque list
                # of bignums (same numbers and order as stored by PuTTY).
                keyparams = map (strtolong, subfields[1:])

            elif sshkeytype == "ssh-dss":
                keytype = "dss"
                # Same again.
                keyparams = map (strtolong, subfields[1:])

            elif sshkeytype == "ecdsa-sha2-nistp256" \
              or sshkeytype == "ecdsa-sha2-nistp384" \
              or sshkeytype == "ecdsa-sha2-nistp521":
                keytype = sshkeytype
                # Have to parse this a bit.
                if len(subfields) > 3:
                    raise KeyFormatError("too many subfields in blob")
                (curvename, Q) = subfields[1:]
                # First is yet another copy of the key name.
                if not re.match("ecdsa-sha2-" + re.escape(curvename),
                                sshkeytype):
                    raise KeyFormatError("key type mismatch ('%s' vs '%s')"
                            % (sshkeytype, curvename))
                # Second contains key material X and Y (hopefully).
                # First a magic octet indicating point compression.
                if struct.unpack("B", Q[0])[0] != 4:
                    # No-one seems to use this.
                    raise KeyFormatError("can't convert point-compressed ECDSA")
                # Then two equal-length bignums (X and Y).
                bnlen = len(Q)-1
                if (bnlen % 1) != 0:
                    raise KeyFormatError("odd-length X+Y")
                bnlen = bnlen / 2
                (x,y) = Q[1:bnlen+1], Q[bnlen+1:2*bnlen+1]
                keyparams = [curvename] + map (strtolong, [x,y])

            elif sshkeytype == "ssh-ed25519":
                keytype = sshkeytype

                if len(subfields) != 2:
                    raise KeyFormatError("wrong number of subfields in blob")
                if subfields[0] != sshkeytype:
                    raise KeyFormatError("key type mismatch ('%s' vs '%s')"
                            % (sshkeytype, subfields[0]))
                # Key material y, with the top bit being repurposed as
                # the expected parity of the associated x (point
                # compression).
                y = strtolong_le(subfields[1])
                x_parity = y >> 255
                y &= ~(1 << 255)

                # Standard Ed25519 parameters.
                p = 2**255 - 19
                d = 0x52036cee2b6ffe738cc740797779e89800700a4d4141d8ab75eb4dca135978a3

                # Recover x^2 = (y^2 - 1) / (d y^2 + 1).
                #
                # With no real time constraints here, it's easier to
                # take the inverse of the denominator by raising it to
                # the power p-2 (by Fermat's Little Theorem) than
                # faffing about with the properly efficient Euclid
                # method.
                xx = (y*y - 1) * pow(d*y*y + 1, p-2, p) % p

                # Take the square root, which may require trying twice.
                x = pow(xx, (p+3)/8, p)
                if pow(x, 2, p) != xx:
                    x = x * pow(2, (p-1)/4, p) % p
                    assert pow(x, 2, p) == xx

                # Pick the square root of the correct parity.
                if (x % 2) != x_parity:
                    x = p - x

                keyparams = [x, y]
            else:
                raise UnknownKeyType(sshkeytype)

        # Now print out one line per host pattern, discarding wildcards.
        for host in string.split (hostpat, ','):
            if re.search (r"[*?!]", host):
                warn("skipping wildcard host pattern '%s'" % host)
                continue
            elif re.match (r"\|", host):
                warn("skipping hashed hostname '%s'" % host)
                continue
            else:
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
                value = string.join (map (
                    lambda x: x if isinstance(x, basestring) else longtohex(x),
                    keyparams), ',')
                if output_type == 'unix':
                    # Unix format.
                    sys.stdout.write('%s %s\n' % (key, value))
                else:
                    # Windows format.
                    # XXX: worry about double quotes?
                    sys.stdout.write("\"%s\"=\"%s\"\n"
                                     % (winmungestr(key), value))

    except UnknownKeyType, k:
        warn("unknown SSH key type '%s', skipping" % k.keytype)
    except KeyFormatError, k:
        warn("trouble parsing key (%s), skipping" % k.msg)
    except BlankInputLine:
        pass

# The spec at http://support.microsoft.com/kb/310516 says we need
# a blank line at the end of the reg file:
#
#   Note the registry file should contain a blank line at the
#   bottom of the file.
#
if output_type == 'windows':
    # Output REG file header.
    sys.stdout.write("\n")
            
