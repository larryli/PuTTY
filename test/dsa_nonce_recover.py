#!/usr/bin/env python3

'''
Recover the nonce value k used in integer DSA or NIST-style ECDSA,
starting from the private key and the signature.

_Without_ the private key, recovering the nonce is equivalent to
recovering the private key itself. But with it, it's a trivial piece
of modular arithmetic.

This script generates a load of test signatures from various keys,
recovers the nonces used, and prints them. This allows an eyeball
check of whether they're evenly distributed.
'''

import argparse

from base64 import b64decode as b64

from eccref import *
from testcrypt import *
from ssh import *
from agenttest import agent_query

def recover_nonce(order, hashalg, privint, transform_hash, r, s, message):
    w = int(mp_invert(s, order))

    h = ssh_hash_new(hashalg)
    ssh_hash_update(h, message)
    z = int(mp_from_bytes_be(ssh_hash_final(h)))
    z = int(transform_hash(z))

    return w * (z + r * privint) % order

def dsa_decode_sig(signature):
    _, signature = ssh_decode_string(signature, return_rest=True)
    signature = ssh_decode_string(signature)
    assert len(signature) == 40
    r = int(mp_from_bytes_be(signature[:20]))
    s = int(mp_from_bytes_be(signature[20:]))
    return r, s

def ecdsa_decode_sig(signature):
    _, signature = ssh_decode_string(signature, return_rest=True)
    signature = ssh_decode_string(signature)
    r, signature = ssh_decode_string(signature, return_rest=True)
    s, signature = ssh_decode_string(signature, return_rest=True)
    r = int(mp_from_bytes_be(r))
    s = int(mp_from_bytes_be(s))
    return r, s

class SignerBase:
    def test(self, privkey, decode_sig, transform_hash, order, hashalg,
             algid, obits):
        print("----", algid)
        print("k=0x{{:0{}b}}".format(obits).format(order))
        privblob = ssh_key_private_blob(privkey)
        privint = int(mp_from_bytes_be(ssh_decode_string(privblob)))
        self.setup_key(privkey)
        for message in (f"msg{i}".encode('ASCII') for i in range(100)):
            signature = self.sign(privkey, message)
            r, s = decode_sig(signature)
            nonce = recover_nonce(order, hashalg, privint, transform_hash,
                                  r, s, message)
            print("k=0x{{:0{}b}}".format(obits).format(nonce))
        self.cleanup_key(privkey)

    def test_dsa(self, pubblob, privblob):
        privkey = ssh_key_new_priv('dsa', pubblob, privblob)
        _, buf = ssh_decode_string(pubblob, return_rest=True)
        p, buf = ssh_decode_string(buf, return_rest=True)
        q, buf = ssh_decode_string(buf, return_rest=True)
        g, buf = ssh_decode_string(buf, return_rest=True)
        p = int(mp_from_bytes_be(p))
        q = int(mp_from_bytes_be(q))
        g = int(mp_from_bytes_be(g))
        transform_hash = lambda h: h
        self.test(privkey, dsa_decode_sig, transform_hash, q, 'sha1', 'dsa',
                  160)

    def test_ecdsa(self, algid, curve, hashalg, pubblob, privblob):
        privkey = ssh_key_new_priv(algid, pubblob, privblob)
        obits = int(mp_get_nbits(curve.G_order))
        def transform_hash(z):
            shift = max(0, mp_get_nbits(z) - obits)
            return mp_rshift_safe(z, shift)
        self.test(privkey, ecdsa_decode_sig, transform_hash, curve.G_order,
                  hashalg, algid, obits)

class TestcryptSigner(SignerBase):
    def setup_key(self, key):
        pass
    def cleanup_key(self, key):
        pass
    def sign(self, key, message):
        return ssh_key_sign(key, message, 0)

class AgentSigner(SignerBase):
    def setup_key(self, key):
        alg = ssh_decode_string(key.public_blob())
        msg = (ssh_byte(SSH2_AGENTC_ADD_IDENTITY) +
               ssh_string(alg) +
               key.openssh_blob() +
               ssh_string(b"dsa_nonce_recover test key"))
        result = agent_query(msg)
        assert result == ssh_byte(SSH_AGENT_SUCCESS)

    def cleanup_key(self, key):
        msg = (ssh_byte(SSH2_AGENTC_REMOVE_IDENTITY) +
               ssh_string(key.public_blob()))
        result = agent_query(msg)
        assert result == ssh_byte(SSH_AGENT_SUCCESS)

    def sign(self, key, message):
        msg = (ssh_byte(SSH2_AGENTC_SIGN_REQUEST) +
               ssh_string(key.public_blob()) +
               ssh_string(message))
        rsp = agent_query(msg)
        t, rsp = ssh_decode_byte(rsp, True)
        assert t == SSH2_AGENT_SIGN_RESPONSE
        sig, rsp = ssh_decode_string(rsp, True)
        assert len(rsp) == 0
        return sig

def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--agent", action="store_true",
                        help="Test an SSH agent instead of testcrypt. "
                        "(Still needs testcrypt.)")
    args = parser.parse_args()

    signer = AgentSigner() if args.agent else TestcryptSigner()

    signer.test_dsa(b64('AAAAB3NzaC1kc3MAAABhAJyWZzjVddGdyc5JPu/WPrC07vKRAmlqO6TUi49ah96iRcM7/D1aRMVAdYBepQ2mf1fsQTmvoC9KgQa79nN3kHhz0voQBKOuKI1ZAodfVOgpP4xmcXgjaA73Vjz22n4newAAABUA6l7/vIveaiA33YYv+SKcKLQaA8cAAABgbErc8QLw/WDz7mhVRZrU+9x3Tfs68j3eW+B/d7Rz1ZCqMYDk7r/F8dlBdQlYhpQvhuSBgzoFa0+qPvSSxPmutgb94wNqhHlVIUb9ZOJNloNr2lXiPP//Wu51TxXAEvAAAAAAYQCcQ9mufXtZa5RyfwT4NuLivdsidP4HRoLXdlnppfFAbNdbhxE0Us8WZt+a/443bwKnYxgif8dgxv5UROnWTngWu0jbJHpaDcTc9lRyTeSUiZZK312s/Sl7qDk3/Du7RUI='), b64('AAAAFGx3ft7G8AQzFsjhle7PWardUXh3'))
    signer.test_ecdsa('p256', p256, 'sha256', b64('AAAAE2VjZHNhLXNoYTItbmlzdHAyNTYAAAAIbmlzdHAyNTYAAABBBHkYQ0sQoq5LbJI1VMWhw3bV43TSYi3WVpqIgKcBKK91TcFFlAMZgceOHQ0xAFYcSczIttLvFu+xkcLXrRd4N7Q='), b64('AAAAIQCV/1VqiCsHZm/n+bq7lHEHlyy7KFgZBEbzqYaWtbx48Q=='))
    signer.test_ecdsa('p384', p384, 'sha384', b64('AAAAE2VjZHNhLXNoYTItbmlzdHAzODQAAAAIbmlzdHAzODQAAABhBMYK8PUtfAlJwKaBTIGEuCzH0vqOMa4UbcjrBbTbkGVSUnfo+nuC80NCdj9JJMs1jvfF8GzKLc5z8H3nZyM741/BUFjV7rEHsQFDek4KyWvKkEgKiTlZid19VukNo1q2Hg=='), b64('AAAAMGsfTmdB4zHdbiQ2euTSdzM6UKEOnrVjMAWwHEYvmG5qUOcBnn62fJDRJy67L+QGdg=='))
    signer.test_ecdsa('p521', p521, 'sha512', b64('AAAAE2VjZHNhLXNoYTItbmlzdHA1MjEAAAAIbmlzdHA1MjEAAACFBAFrGthlKM152vu2Ghk+R7iO9/M6e+hTehNZ6+FBwof4HPkPB2/HHXj5+w5ynWyUrWiX5TI2riuJEIrJErcRH5LglADnJDX2w4yrKZ+wDHSz9lwh9p2F+B5R952es6gX3RJRkGA+qhKpKup8gKx78RMbleX8wgRtIu+4YMUnKb1edREiRg=='), b64('AAAAQgFh7VNJFUljWhhyAEiL0z+UPs/QggcMTd3Vv2aKDeBdCRl5di8r+BMm39L7bRzxRMEtW5NSKlDtE8MFEGdIE9khsw=='))

if __name__ == '__main__':
    main()
