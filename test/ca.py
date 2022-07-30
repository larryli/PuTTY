#!/usr/bin/env python3
#
# Implementation of OpenSSH certificate creation. Used in
# cryptsuite.py to construct certificates for test purposes.
#
# Can also be run standalone to function as an actual CA, though I
# don't currently know of any reason you'd want to use it in place of
# ssh-keygen. In that mode, it depends on having an SSH agent
# available to do the signing.

import argparse
import base64
import enum
import hashlib
import io
import os

import ssh

class Container:
    pass

class CertType(enum.Enum):
    user = 1
    host = 2

def maybe_encode(s):
    if isinstance(s, bytes):
        return s
    return s.encode('UTF-8')

def make_signature_preimage(
        key_to_certify, ca_key, certtype, keyid, serial, principals,
        valid_after=0, valid_before=0xFFFFFFFFFFFFFFFF,
        critical_options={}, extensions={},
        reserved=b'', nonce=None):

    alg, pubkeydata = ssh.ssh_decode_string(key_to_certify, True)

    if nonce is None:
        nonce = os.urandom(32)

    buf = io.BytesIO()
    buf.write(ssh.ssh_string(alg + b"-cert-v01@openssh.com"))
    buf.write(ssh.ssh_string(nonce))
    buf.write(pubkeydata)
    buf.write(ssh.ssh_uint64(serial))
    buf.write(ssh.ssh_uint32(certtype.value if isinstance(certtype, CertType)
                             else certtype))
    buf.write(ssh.ssh_string(maybe_encode(keyid)))
    buf.write(ssh.ssh_string(b''.join(
        ssh.ssh_string(maybe_encode(principal))
        for principal in principals)))
    buf.write(ssh.ssh_uint64(valid_after))
    buf.write(ssh.ssh_uint64(valid_before))
    buf.write(ssh.ssh_string(b''.join(
        ssh.ssh_string(opt) + ssh.ssh_string(val)
        for opt, val in sorted([(maybe_encode(opt), maybe_encode(val))
                                for opt, val in critical_options.items()]))))
    buf.write(ssh.ssh_string(b''.join(
        ssh.ssh_string(opt) + ssh.ssh_string(val)
        for opt, val in sorted([(maybe_encode(opt), maybe_encode(val))
                                for opt, val in extensions.items()]))))
    buf.write(ssh.ssh_string(reserved))
    # The CA key here can be a raw 'bytes', or an ssh_key object
    # exposed via testcrypt
    if type(ca_key) != bytes:
        ca_key = ca_key.public_blob()
    buf.write(ssh.ssh_string(ca_key))

    return buf.getvalue()

def make_full_cert(preimage, signature):
    return preimage + ssh.ssh_string(signature)

def sign_cert_via_testcrypt(preimage, ca_key, signflags=None):
    # Expects ca_key to be a testcrypt ssh_key object
    signature = ca_key.sign(preimage, 0 if signflags is None else signflags)
    return make_full_cert(preimage, signature)

def sign_cert_via_agent(preimage, ca_key, signflags=None):
    # Expects ca_key to be a binary public key blob, and for a
    # currently running SSH agent to contain the corresponding private
    # key.
    import agenttest
    sign_request = (ssh.ssh_byte(ssh.SSH2_AGENTC_SIGN_REQUEST) +
                    ssh.ssh_string(ca_key) + ssh.ssh_string(preimage))
    if signflags is not None:
        sign_request += ssh.ssh_uint32(signflags)
    sign_response = agenttest.agent_query(sign_request)
    msgtype, sign_response = ssh.ssh_decode_byte(sign_response, True)
    if msgtype == ssh.SSH2_AGENT_SIGN_RESPONSE:
        signature, sign_response = ssh.ssh_decode_string(sign_response, True)
        return make_full_cert(preimage, signature)
    elif msgtype == ssh.SSH2_AGENT_FAILURE:
        raise IOError("Agent refused to return a signature")
    else:
        raise IOError("Agent returned unexpecteed message type {:d}"
                      .format(msgtype))

def read_pubkey_file(fh):
    b64buf = io.StringIO()
    comment = None

    lines = (line.rstrip("\r\n") for line in iter(fh.readline, ""))
    line = next(lines)

    if line == "---- BEGIN SSH2 PUBLIC KEY ----":
        # RFC 4716 public key. Read headers like Comment:
        line = next(lines)
        while ":" in line:
            key, val = line.split(":", 1)
            if key == "Comment":
                comment = val.strip("\r\n")
            line = next(lines)
        # Now expect lines of base64 data.
        while line != "---- END SSH2 PUBLIC KEY ----":
            b64buf.write(line)
            line = next(lines)

    else:
        # OpenSSH public key. Expect the b64buf blob to be the second word.
        fields = line.split(" ", 2)
        b64buf.write(fields[1])
        if len(fields) > 1:
            comment = fields[2]

    return base64.b64decode(b64buf.getvalue()), comment

def write_pubkey_file(fh, key, comment=None):
    alg = ssh.ssh_decode_string(key)
    fh.write(alg.decode('ASCII'))
    fh.write(" " + base64.b64encode(key).decode('ASCII'))
    if comment is not None:
        fh.write(" " + comment)
    fh.write("\n")

def default_signflags(key):
    alg = ssh.ssh_decode_string(key)
    if alg == b'ssh-rsa':
        return 4 # RSA-SHA-512

def main():
    parser = argparse.ArgumentParser(
        description='Create and sign OpenSSH certificates.')
    parser.add_argument("key_to_certify", help="Public key to be certified.")
    parser.add_argument("--ca-key", required=True,
                        help="Public key of the CA. Must be present in a "
                        "currently accessible SSH agent.")
    parser.add_argument("-o", "--output", required=True,
                        help="File to write output OpenSSH key to.")
    parser.add_argument("--type", required=True, choices={'user', 'host'},
                        help="Type of certificate to make.")
    parser.add_argument("--principal", "--user", "--host",
                        required=True, action="append",
                        help="User names or host names to authorise.")
    parser.add_argument("--key-id", "--keyid", required=True,
                        help="Human-readable key ID string for log files.")
    parser.add_argument("--serial", type=int, required=True,
                        help="Serial number to write into certificate.")
    parser.add_argument("--signflags", type=int, help="Signature flags "
                        "(e.g. 2 = RSA-SHA-256, 4 = RSA-SHA-512).")
    args = parser.parse_args()

    with open(args.key_to_certify) as fh:
        key_to_certify, comment = read_pubkey_file(fh)
    with open(args.ca_key) as fh:
        ca_key, _ = read_pubkey_file(fh)

    extensions = {
        'permit-X11-forwarding': '',
        'permit-agent-forwarding': '',
        'permit-port-forwarding': '',
        'permit-pty': '',
        'permit-user-rc': '',
    }

    # FIXME: for a full-featured command-line CA we'd need to add
    # command-line options for crit opts, extensions and validity
    # period
    preimage = make_signature_preimage(
        key_to_certify = key_to_certify,
        ca_key = ca_key,
        certtype = getattr(CertType, args.type),
        keyid = args.key_id,
        serial = args.serial,
        principals = args.principal,
        extensions = extensions)

    signflags = (args.signflags if args.signflags is not None
                 else default_signflags(ca_key))
    cert = sign_cert_via_agent(preimage, ca_key, signflags)

    with open(args.output, "w") as fh:
        write_pubkey_file(fh, cert, comment)

if __name__ == '__main__':
    main()
