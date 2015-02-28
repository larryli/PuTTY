# Python module to make it easy to manually encode SSH packets, by
# supporting the various uint32, string, mpint primitives.
#
# The idea of this is that you can use it to manually construct key
# exchange sequences of interesting kinds, for testing purposes.

import struct, random

def boolean(b):
    return "\1" if b else "\0"

def byte(b):
    assert 0 <= b < 0x100
    return chr(b)

def uint32(u):
    assert 0 <= u < 0x100000000
    return struct.pack(">I", u)

def uint64(u):
    assert 0 <= u < 0x10000000000000000
    return struct.pack(">L", u)

def string(s):
    return uint32(len(s)) + s

def mpint(m):
    s = ""
    lastbyte = 0
    while m > 0:
        lastbyte = m & 0xFF
        s = chr(lastbyte) + s
        m >>= 8
    if lastbyte & 0x80:
        s = "\0" + s
    return string(s)

def name_list(ns):
    s = ""
    for n in ns:
        assert "," not in n
        if s != "":
            s += ","
        s += n
    return string(s)

def ssh_rsa_key_blob(modulus, exponent):
    return string(string("ssh-rsa") + mpint(modulus) + mpint(exponent))

def ssh_rsa_signature_blob(signature):
    return string(string("ssh-rsa") + mpint(signature))

def greeting(string):
    # Greeting at the start of an SSH connection.
    return string + "\r\n"

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
SSH2_MSG_KEX_DH_GEX_REQUEST = 30
SSH2_MSG_KEX_DH_GEX_GROUP = 31
SSH2_MSG_KEX_DH_GEX_INIT = 32
SSH2_MSG_KEX_DH_GEX_REPLY = 33
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
