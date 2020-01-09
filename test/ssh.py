import struct
import itertools

def nbits(n):
    # Mimic mp_get_nbits for ordinary Python integers.
    assert 0 <= n
    smax = next(s for s in itertools.count() if (n >> (1 << s)) == 0)
    toret = 0
    for shift in reversed([1 << s for s in range(smax)]):
        if n >> shift != 0:
            n >>= shift
            toret += shift
    assert n <= 1
    if n == 1:
        toret += 1
    return toret

def ssh_byte(n):
    return struct.pack("B", n)

def ssh_uint32(n):
    return struct.pack(">L", n)

def ssh_string(s):
    return ssh_uint32(len(s)) + s

def ssh1_mpint(x):
    bits = nbits(x)
    bytevals = [0xFF & (x >> (8*n)) for n in range((bits-1)//8, -1, -1)]
    return struct.pack(">H" + "B" * len(bytevals), bits, *bytevals)

def ssh2_mpint(x):
    bytevals = [0xFF & (x >> (8*n)) for n in range(nbits(x)//8, -1, -1)]
    return struct.pack(">L" + "B" * len(bytevals), len(bytevals), *bytevals)

def decoder(fn):
    def decode(s, return_rest = False):
        item, length_consumed = fn(s)
        if return_rest:
            return item, s[length_consumed:]
        else:
            return item
    return decode

@decoder
def ssh_decode_byte(s):
    return struct.unpack_from("B", s, 0)[0], 1

@decoder
def ssh_decode_uint32(s):
    return struct.unpack_from(">L", s, 0)[0], 4

@decoder
def ssh_decode_string(s):
    length = ssh_decode_uint32(s)
    assert length + 4 <= len(s)
    return s[4:length+4], length+4

@decoder
def ssh1_get_mpint(s): # returns it unconsumed, still in wire encoding
    nbits = struct.unpack_from(">H", s, 0)[0]
    nbytes = (nbits + 7) // 8
    assert nbytes + 2 <= len(s)
    return s[:nbytes+2], nbytes+2

@decoder
def ssh1_decode_mpint(s):
    nbits = struct.unpack_from(">H", s, 0)[0]
    nbytes = (nbits + 7) // 8
    assert nbytes + 2 <= len(s)
    data = s[2:nbytes+2]
    v = 0
    for b in struct.unpack("B" * len(data), data):
        v = (v << 8) | b
    return v, nbytes+2

AGENT_MAX_MSGLEN = 262144

SSH1_AGENTC_REQUEST_RSA_IDENTITIES = 1
SSH1_AGENT_RSA_IDENTITIES_ANSWER = 2
SSH1_AGENTC_RSA_CHALLENGE = 3
SSH1_AGENT_RSA_RESPONSE = 4
SSH1_AGENTC_ADD_RSA_IDENTITY = 7
SSH1_AGENTC_REMOVE_RSA_IDENTITY = 8
SSH1_AGENTC_REMOVE_ALL_RSA_IDENTITIES = 9
SSH_AGENT_FAILURE = 5
SSH_AGENT_SUCCESS = 6
SSH2_AGENTC_REQUEST_IDENTITIES = 11
SSH2_AGENT_IDENTITIES_ANSWER = 12
SSH2_AGENTC_SIGN_REQUEST = 13
SSH2_AGENT_SIGN_RESPONSE = 14
SSH2_AGENTC_ADD_IDENTITY = 17
SSH2_AGENTC_REMOVE_IDENTITY = 18
SSH2_AGENTC_REMOVE_ALL_IDENTITIES = 19
SSH2_AGENTC_EXTENSION = 27

SSH_AGENT_RSA_SHA2_256 = 2
SSH_AGENT_RSA_SHA2_512 = 4
