# Python module to make it easy to manually encode SSH packets, by
# supporting the various uint32, string, mpint primitives.
#
# The idea of this is that you can use it to manually construct key
# exchange sequences of interesting kinds, for testing purposes.

import sys
import struct
import random

assert sys.version_info[:2] >= (3,0), "This is Python 3 code"

def tobytes(s):
    return s if isinstance(s, bytes) else s.encode('ASCII')

def boolean(b):
    return b"\1" if b else b"\0"

def byte(b):
    assert 0 <= b < 0x100
    return bytes([b])

def uint32(u):
    assert 0 <= u < 0x100000000
    return struct.pack(">I", u)

def uint64(u):
    assert 0 <= u < 0x10000000000000000
    return struct.pack(">L", u)

def string(s):
    return uint32(len(s)) + tobytes(s)

def mpint(m):
    s = []
    while m > 0:
        s.append(m & 0xFF)
        m >>= 8
    if len(s) > 0 and (s[-1] & 0x80):
        s.append(0)
    s.reverse()
    return string(bytes(s))

def name_list(ns):
    s = b""
    for n in map(tobytes, ns):
        assert b"," not in n
        if s != b"":
            s += b","
        s += n
    return string(s)

def ssh_rsa_key_blob(modulus, exponent):
    return string(string("ssh-rsa") + mpint(modulus) + mpint(exponent))

def ssh_rsa_signature_blob(signature):
    return string(string("ssh-rsa") + mpint(signature))

def greeting(string):
    # Greeting at the start of an SSH connection.
    return tobytes(string) + b"\r\n"

# Packet types.
SSH2_MSG_DISCONNECT = 1
SSH2_MSG_IGNORE = 2
SSH2_MSG_UNIMPLEMENTED = 3
SSH2_MSG_DEBUG = 4
SSH2_MSG_SERVICE_REQUEST = 5
SSH2_MSG_SERVICE_ACCEPT = 6
SSH2_MSG_KEXINIT = 20
SSH2_MSG_NEWKEYS = 21
SSH2_MSG_KEXDH_INIT = 30
SSH2_MSG_KEXDH_REPLY = 31
SSH2_MSG_KEX_DH_GEX_REQUEST_OLD = 30
SSH2_MSG_KEX_DH_GEX_GROUP = 31
SSH2_MSG_KEX_DH_GEX_INIT = 32
SSH2_MSG_KEX_DH_GEX_REPLY = 33
SSH2_MSG_KEX_DH_GEX_REQUEST = 34
SSH2_MSG_KEXRSA_PUBKEY = 30
SSH2_MSG_KEXRSA_SECRET = 31
SSH2_MSG_KEXRSA_DONE = 32
SSH2_MSG_USERAUTH_REQUEST = 50
SSH2_MSG_USERAUTH_FAILURE = 51
SSH2_MSG_USERAUTH_SUCCESS = 52
SSH2_MSG_USERAUTH_BANNER = 53
SSH2_MSG_USERAUTH_PK_OK = 60
SSH2_MSG_USERAUTH_PASSWD_CHANGEREQ = 60
SSH2_MSG_USERAUTH_INFO_REQUEST = 60
SSH2_MSG_USERAUTH_INFO_RESPONSE = 61
SSH2_MSG_GLOBAL_REQUEST = 80
SSH2_MSG_REQUEST_SUCCESS = 81
SSH2_MSG_REQUEST_FAILURE = 82
SSH2_MSG_CHANNEL_OPEN = 90
SSH2_MSG_CHANNEL_OPEN_CONFIRMATION = 91
SSH2_MSG_CHANNEL_OPEN_FAILURE = 92
SSH2_MSG_CHANNEL_WINDOW_ADJUST = 93
SSH2_MSG_CHANNEL_DATA = 94
SSH2_MSG_CHANNEL_EXTENDED_DATA = 95
SSH2_MSG_CHANNEL_EOF = 96
SSH2_MSG_CHANNEL_CLOSE = 97
SSH2_MSG_CHANNEL_REQUEST = 98
SSH2_MSG_CHANNEL_SUCCESS = 99
SSH2_MSG_CHANNEL_FAILURE = 100
SSH2_MSG_USERAUTH_GSSAPI_RESPONSE = 60
SSH2_MSG_USERAUTH_GSSAPI_TOKEN = 61
SSH2_MSG_USERAUTH_GSSAPI_EXCHANGE_COMPLETE = 63
SSH2_MSG_USERAUTH_GSSAPI_ERROR = 64
SSH2_MSG_USERAUTH_GSSAPI_ERRTOK = 65
SSH2_MSG_USERAUTH_GSSAPI_MIC = 66

def clearpkt(msgtype, *stuff):
    # SSH-2 binary packet, in the cleartext format used for initial
    # setup and kex.
    s = byte(msgtype)
    for thing in stuff:
        s += thing
    padlen = 0
    while padlen < 4 or len(s) % 8 != 3:
        padlen += 1
        s += byte(random.randint(0,255))
    s = byte(padlen) + s
    return string(s)

def decode_uint32(s):
    assert len(s) == 4
    return struct.unpack(">I", s)[0]

def read_clearpkt(fh):
    length_field = fh.read(4)
    s = fh.read(decode_uint32(length_field))
    padlen, msgtype = s[:2]
    return msgtype, s[2:-padlen]
