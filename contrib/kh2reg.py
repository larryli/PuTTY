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

def longtohex(n):
    """Convert long int to lower-case hex.

    Ick, Python (at least in 1.5.2) doesn't appear to have a way to
    turn a long int into an unadorned hex string -- % gets upset if the
    number is too big, and raw hex() uses uppercase (sometimes), and
    adds unwanted "0x...L" around it."""

    plain=string.lower(re.match(r"0x([0-9A-Fa-f]*)l?$", hex(n), re.I).group(1))
    return "0x" + plain

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
        magicnumbers = []   # placeholder
        keytype = ""        # placeholder

        # Grotty heuristic to distinguish known_hosts from known_hosts2:
        # is second field entirely decimal digits?
        if re.match (r"\d*$", fields[1]):

            # Treat as SSH-1-type host key.
            # Format: hostpat bits10 exp10 mod10 comment...
            # (PuTTY doesn't store the number of bits.)
            magicnumbers = map (long, fields[2:4])
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

            # The first field is keytype again, and the rest we can treat as
            # an opaque list of bignums (same numbers and order as stored
            # by PuTTY). (currently embedded keytype is ignored entirely)
            magicnumbers = map (strtolong, subfields[1:])

            # Translate key type into something PuTTY can use.
            if   sshkeytype == "ssh-rsa":   keytype = "rsa2"
            elif sshkeytype == "ssh-dss":   keytype = "dss"
            else:
                raise UnknownKeyType(sshkeytype)

        # Now print out one line per host pattern, discarding wildcards.
        for host in string.split (hostpat, ','):
            if re.search (r"[*?!]", host):
                sys.stderr.write("Skipping wildcard host pattern '%s'\n"
                                 % host)
                continue
            elif re.match (r"\|", host):
                sys.stderr.write("Skipping hashed hostname '%s'\n" % host)
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
                value = string.join (map (longtohex, magicnumbers), ',')
                if output_type == 'unix':
                    # Unix format.
                    sys.stdout.write('%s %s\n' % (key, value))
                else:
                    # Windows format.
                    # XXX: worry about double quotes?
                    sys.stdout.write("\"%s\"=\"%s\"\n"
                                     % (winmungestr(key), value))

    except UnknownKeyType, k:
        sys.stderr.write("Unknown SSH key type '%s', skipping\n" % k.keytype)
    except BlankInputLine:
        pass
