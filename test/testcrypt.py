import sys
import os
import numbers
import subprocess
import re
import struct
from binascii import hexlify

assert sys.version_info[:2] >= (3,0), "This is Python 3 code"

# Expect to be run from the 'test' subdirectory, one level down from
# the main source
putty_srcdir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def coerce_to_bytes(arg):
    return arg.encode("UTF-8") if isinstance(arg, str) else arg

class ChildProcessFailure(Exception):
    pass

class ChildProcess(object):
    def __init__(self):
        self.sp = None
        self.debug = None
        self.exitstatus = None
        self.exception = None

        dbg = os.environ.get("PUTTY_TESTCRYPT_DEBUG")
        if dbg is not None:
            if dbg == "stderr":
                self.debug = sys.stderr
            else:
                sys.stderr.write("Unknown value '{}' for PUTTY_TESTCRYPT_DEBUG"
                                 " (try 'stderr'\n")
    def start(self):
        assert self.sp is None
        override_command = os.environ.get("PUTTY_TESTCRYPT")
        if override_command is None:
            cmd = [os.path.join(putty_srcdir, "testcrypt")]
            shell = False
        else:
            cmd = override_command
            shell = True
        self.sp = subprocess.Popen(
            cmd, shell=shell, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    def write_line(self, line):
        if self.exception is not None:
            # Re-raise our fatal-error exception, if it previously
            # occurred in a context where it couldn't be propagated (a
            # __del__ method).
            raise self.exception
        if self.debug is not None:
            self.debug.write("send: {}\n".format(line))
        self.sp.stdin.write(line + b"\n")
        self.sp.stdin.flush()
    def read_line(self):
        line = self.sp.stdout.readline()
        if len(line) == 0:
            self.exception = ChildProcessFailure("received EOF from testcrypt")
            raise self.exception
        line = line.rstrip(b"\r\n")
        if self.debug is not None:
            self.debug.write("recv: {}\n".format(line))
        return line
    def already_terminated(self):
        return self.sp is None and self.exitstatus is not None
    def funcall(self, cmd, args):
        if self.sp is None:
            assert self.exitstatus is None
            self.start()
        self.write_line(coerce_to_bytes(cmd) + b" " + b" ".join(
            coerce_to_bytes(arg) for arg in args))
        argcount = int(self.read_line())
        return [self.read_line() for arg in range(argcount)]
    def wait_for_exit(self):
        if self.sp is not None:
            self.sp.stdin.close()
            self.exitstatus = self.sp.wait()
            self.sp = None
    def check_return_status(self):
        self.wait_for_exit()
        if self.exitstatus is not None and self.exitstatus != 0:
            raise ChildProcessFailure("testcrypt returned exit status {}"
                                      .format(self.exitstatus))

childprocess = ChildProcess()

method_prefixes = {
    'val_wpoint': 'ecc_weierstrass_',
    'val_mpoint': 'ecc_montgomery_',
    'val_epoint': 'ecc_edwards_',
    'val_hash': 'ssh_hash_',
    'val_mac': 'ssh2_mac_',
    'val_key': 'ssh_key_',
    'val_cipher': 'ssh_cipher_',
    'val_dh': 'dh_',
    'val_ecdh': 'ssh_ecdhkex_',
    'val_rsakex': 'ssh_rsakex_',
    'val_prng': 'prng_',
    'val_pcs': 'pcs_',
    'val_pockle': 'pockle_',
}
method_lists = {t: [] for t in method_prefixes}

class Value(object):
    def __init__(self, typename, ident):
        self._typename = typename
        self._ident = ident
        for methodname, function in method_lists.get(self._typename, []):
            setattr(self, methodname,
                    (lambda f: lambda *args: f(self, *args))(function))
    def _consumed(self):
        self._ident = None
    def __repr__(self):
        return "Value({!r}, {!r})".format(self._typename, self._ident)
    def __del__(self):
        if self._ident is not None and not childprocess.already_terminated():
            try:
                childprocess.funcall("free", [self._ident])
            except ChildProcessFailure:
                # If we see this exception now, we can't do anything
                # about it, because exceptions don't propagate out of
                # __del__ methods. Squelch it to prevent the annoying
                # runtime warning from Python, and the
                # 'self.exception' mechanism in the ChildProcess class
                # will raise it again at the next opportunity.
                #
                # (This covers both the case where testcrypt crashes
                # _during_ one of these free operations, and the
                # silencing of cascade failures when we try to send a
                # "free" command to testcrypt after it had already
                # crashed for some other reason.)
                pass
    def __long__(self):
        if self._typename != "val_mpint":
            raise TypeError("testcrypt values of types other than mpint"
                            " cannot be converted to integer")
        hexval = childprocess.funcall("mp_dump", [self._ident])[0]
        return 0 if len(hexval) == 0 else int(hexval, 16)
    def __int__(self):
        return int(self.__long__())

def marshal_string(val):
    val = coerce_to_bytes(val)
    assert isinstance(val, bytes), "Bad type for val_string input"
    return "".join(
        chr(b) if (0x20 <= b < 0x7F and b != 0x25)
        else "%{:02x}".format(b)
        for b in val)

def make_argword(arg, argtype, fnname, argindex, to_preserve):
    typename, consumed = argtype
    if typename.startswith("opt_"):
        if arg is None:
            return "NULL"
        typename = typename[4:]
    if typename == "val_string":
        retwords = childprocess.funcall("newstring", [marshal_string(arg)])
        arg = make_retvals([typename], retwords, unpack_strings=False)[0]
        to_preserve.append(arg)
    if typename == "val_mpint" and isinstance(arg, numbers.Integral):
        retwords = childprocess.funcall("mp_literal", ["0x{:x}".format(arg)])
        arg = make_retvals([typename], retwords)[0]
        to_preserve.append(arg)
    if isinstance(arg, Value):
        if arg._typename != typename:
            raise TypeError(
                "{}() argument {:d} should be {} ({} given)".format(
                fnname, argindex, typename, arg._typename))
        ident = arg._ident
        if consumed:
            arg._consumed()
        return ident
    if typename == "uint" and isinstance(arg, numbers.Integral):
        return "0x{:x}".format(arg)
    if typename == "boolean":
        return "true" if arg else "false"
    if typename in {
            "hashalg", "macalg", "keyalg", "cipheralg",
            "dh_group", "ecdh_alg", "rsaorder", "primegenpolicy",
            "argon2flavour", "fptype"}:
        arg = coerce_to_bytes(arg)
        if isinstance(arg, bytes) and b" " not in arg:
            return arg
    if typename == "mpint_list":
        sublist = [make_argword(len(arg), ("uint", False),
                                fnname, argindex, to_preserve)]
        for val in arg:
            sublist.append(make_argword(val, ("val_mpint", False),
                                        fnname, argindex, to_preserve))
        return b" ".join(coerce_to_bytes(sub) for sub in sublist)
    raise TypeError(
        "Can't convert {}() argument {:d} to {} (value was {!r})".format(
            fnname, argindex, typename, arg))

def unpack_string(identifier):
    retwords = childprocess.funcall("getstring", [identifier])
    childprocess.funcall("free", [identifier])
    return re.sub(b"%[0-9A-F][0-9A-F]",
                  lambda m: bytes([int(m.group(0)[1:], 16)]),
                  retwords[0])

def unpack_mp(identifier):
    retwords = childprocess.funcall("mp_dump", [identifier])
    childprocess.funcall("free", [identifier])
    return int(retwords[0], 16)

def make_retval(rettype, word, unpack_strings):
    if rettype.startswith("opt_"):
        if word == b"NULL":
            return None
        rettype = rettype[4:]
    if rettype == "val_string" and unpack_strings:
        return unpack_string(word)
    if rettype == "val_keycomponents":
        kc = {}
        retwords = childprocess.funcall("key_components_count", [word])
        for i in range(int(retwords[0], 0)):
            args = [word, "{:d}".format(i)]
            retwords = childprocess.funcall("key_components_nth_name", args)
            kc_key = unpack_string(retwords[0])
            retwords = childprocess.funcall("key_components_nth_str", args)
            if retwords[0] != b"NULL":
                kc_value = unpack_string(retwords[0]).decode("ASCII")
            else:
                retwords = childprocess.funcall("key_components_nth_mp", args)
                kc_value = unpack_mp(retwords[0])
            kc[kc_key.decode("ASCII")] = kc_value
        childprocess.funcall("free", [word])
        return kc
    if rettype.startswith("val_"):
        return Value(rettype, word)
    elif rettype == "int" or rettype == "uint":
        return int(word, 0)
    elif rettype == "boolean":
        assert word == b"true" or word == b"false"
        return word == b"true"
    elif rettype == "pocklestatus":
        return word.decode("ASCII")
    raise TypeError("Can't deal with return value {!r} of type {!r}"
                    .format(word, rettype))

def make_retvals(rettypes, retwords, unpack_strings=True):
    assert len(rettypes) == len(retwords) # FIXME: better exception
    return [make_retval(rettype, word, unpack_strings)
            for rettype, word in zip(rettypes, retwords)]

class Function(object):
    def __init__(self, fnname, rettypes, argtypes):
        self.fnname = fnname
        self.rettypes = rettypes
        self.argtypes = argtypes
    def __repr__(self):
        return "<Function {}>".format(self.fnname)
    def __call__(self, *args):
        if len(args) != len(self.argtypes):
            raise TypeError(
                "{}() takes exactly {} arguments ({} given)".format(
                    self.fnname, len(self.argtypes), len(args)))
        to_preserve = []
        retwords = childprocess.funcall(
            self.fnname, [make_argword(args[i], self.argtypes[i],
                                       self.fnname, i, to_preserve)
                          for i in range(len(args))])
        retvals = make_retvals(self.rettypes, retwords)
        if len(retvals) == 0:
            return None
        if len(retvals) == 1:
            return retvals[0]
        return tuple(retvals)

def _setup(scope):
    header_file = os.path.join(putty_srcdir, "testcrypt.h")

    linere = re.compile(r'^FUNC\d+\((.*)\)$')
    valprefix = "val_"
    outprefix = "out_"
    optprefix = "opt_"
    consprefix = "consumed_"

    def trim_argtype(arg):
        if arg.startswith(optprefix):
            return optprefix + trim_argtype(arg[len(optprefix):])

        if (arg.startswith(valprefix) and
            "_" in arg[len(valprefix):]):
            # Strip suffixes like val_string_asciz
            arg = arg[:arg.index("_", len(valprefix))]
        return arg

    with open(header_file) as f:
        for line in iter(f.readline, ""):
            line = line.rstrip("\r\n").replace(" ", "")
            m = linere.match(line)
            if m is not None:
                words = m.group(1).split(",")
                function = words[1]
                rettypes = []
                argtypes = []
                argsconsumed = []
                if words[0] != "void":
                    rettypes.append(trim_argtype(words[0]))
                for arg in words[2:]:
                    if arg.startswith(outprefix):
                        rettypes.append(trim_argtype(arg[len(outprefix):]))
                    else:
                        consumed = False
                        if arg.startswith(consprefix):
                            arg = arg[len(consprefix):]
                            consumed = True
                        arg = trim_argtype(arg)
                        argtypes.append((arg, consumed))
                func = Function(function, rettypes, argtypes)
                scope[function] = func
                if len(argtypes) > 0:
                    t = argtypes[0][0]
                    if (t in method_prefixes and
                        function.startswith(method_prefixes[t])):
                        methodname = function[len(method_prefixes[t]):]
                        method_lists[t].append((methodname, func))

_setup(globals())
del _setup
