#!/usr/bin/python3

import sys
import os
import socket
import base64
import itertools
import collections

from ssh import *
import agenttestdata

test_session_id = b'Test16ByteSessId'
assert len(test_session_id) == 16
test_message_to_sign = b'test message to sign'

TestSig2 = collections.namedtuple("TestSig2", "flags sig")

class Key2(collections.namedtuple("Key2", "comment public sigs openssh")):
    def public_only(self):
        return Key2(self.comment, self.public, None, None)

    def Add(self):
        alg = ssh_decode_string(self.public)
        msg = (ssh_byte(SSH2_AGENTC_ADD_IDENTITY) +
               ssh_string(alg) +
               self.openssh +
               ssh_string(self.comment))
        return agent_query(msg)

    verb = "sign"
    def Use(self, flags):
        msg = (ssh_byte(SSH2_AGENTC_SIGN_REQUEST) +
               ssh_string(self.public) +
               ssh_string(test_message_to_sign))
        if flags is not None:
            msg += ssh_uint32(flags)
        rsp = agent_query(msg)
        t, rsp = ssh_decode_byte(rsp, True)
        assert t == SSH2_AGENT_SIGN_RESPONSE
        sig, rsp = ssh_decode_string(rsp, True)
        assert len(rsp) == 0
        return sig

    def Del(self):
        msg = (ssh_byte(SSH2_AGENTC_REMOVE_IDENTITY) +
               ssh_string(self.public))
        return agent_query(msg)

    @staticmethod
    def DelAll():
        msg = (ssh_byte(SSH2_AGENTC_REMOVE_ALL_IDENTITIES))
        return agent_query(msg)

    @staticmethod
    def List():
        msg = (ssh_byte(SSH2_AGENTC_REQUEST_IDENTITIES))
        rsp = agent_query(msg)
        t, rsp = ssh_decode_byte(rsp, True)
        assert t == SSH2_AGENT_IDENTITIES_ANSWER
        nk, rsp = ssh_decode_uint32(rsp, True)
        keylist = []
        for _ in range(nk):
            p, rsp = ssh_decode_string(rsp, True)
            c, rsp = ssh_decode_string(rsp, True)
            keylist.append(Key2(c, p, None, None))
        assert len(rsp) == 0
        return keylist

    @classmethod
    def make_examples(cls):
        cls.examples = agenttestdata.key2examples(cls, TestSig2)

    def iter_testsigs(self):
        for testsig in self.sigs:
            if testsig.flags == 0:
                yield testsig._replace(flags=None)
            yield testsig

    def iter_tests(self):
        for testsig in self.iter_testsigs():
            yield ([testsig.flags],
                   " (flags={})".format(testsig.flags),
                   testsig.sig)

class Key1(collections.namedtuple(
        "Key1", "comment public challenge response private")):
    def public_only(self):
        return Key1(self.comment, self.public, None, None, None)

    def Add(self):
        msg = (ssh_byte(SSH1_AGENTC_ADD_RSA_IDENTITY) +
               self.private +
               ssh_string(self.comment))
        return agent_query(msg)

    verb = "decrypt"
    def Use(self, challenge):
        msg = (ssh_byte(SSH1_AGENTC_RSA_CHALLENGE) +
               self.public +
               ssh1_mpint(challenge) +
               test_session_id +
               ssh_uint32(1))
        rsp = agent_query(msg)
        t, rsp = ssh_decode_byte(rsp, True)
        assert t == SSH1_AGENT_RSA_RESPONSE
        assert len(rsp) == 16
        return rsp

    def Del(self):
        msg = (ssh_byte(SSH1_AGENTC_REMOVE_RSA_IDENTITY) +
               self.public)
        return agent_query(msg)

    @staticmethod
    def DelAll():
        msg = (ssh_byte(SSH1_AGENTC_REMOVE_ALL_RSA_IDENTITIES))
        return agent_query(msg)

    @staticmethod
    def List():
        msg = (ssh_byte(SSH1_AGENTC_REQUEST_RSA_IDENTITIES))
        rsp = agent_query(msg)
        t, rsp = ssh_decode_byte(rsp, True)
        assert t == SSH1_AGENT_RSA_IDENTITIES_ANSWER
        nk, rsp = ssh_decode_uint32(rsp, True)
        keylist = []
        for _ in range(nk):
            b, rsp = ssh_decode_uint32(rsp, True)
            e, rsp = ssh1_get_mpint(rsp, True)
            m, rsp = ssh1_get_mpint(rsp, True)
            c, rsp = ssh_decode_string(rsp, True)
            keylist.append(Key1(c, ssh_uint32(b)+e+m, None, None, None))
        assert len(rsp) == 0
        return keylist

    @classmethod
    def make_examples(cls):
        cls.examples = agenttestdata.key1examples(cls)

    def iter_tests(self):
        yield [self.challenge], "", self.response

def agent_query(msg):
    msg = ssh_string(msg)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(os.environ["SSH_AUTH_SOCK"])
    s.send(msg)
    length = ssh_decode_uint32(s.recv(4))
    assert length < AGENT_MAX_MSGLEN
    return s.recv(length)

def enumerate_bits(iterable):
    return ((1<<j, item) for j,item in enumerate(iterable))

def gray_code(nbits):
    old = 0
    for i in itertools.chain(range(1, 1 << nbits), [0]):
        new = i ^ (i>>1)
        diff = new ^ old
        assert diff != 0 and (diff & (diff-1)) == 0
        yield old, new, diff
        old = new
    assert old == 0

class TestRunner:
    def __init__(self):
        self.ok = True

    @staticmethod
    def fmt_response(response):
        return "'{}'".format(
            base64.encodebytes(response).decode("ASCII").replace("\n",""))

    @staticmethod
    def fmt_keylist(keys):
        return "{{{}}}".format(
            ",".join(key.comment.decode("ASCII") for key in sorted(keys)))

    def expect_success(self, text, response):
        if response == ssh_byte(SSH_AGENT_SUCCESS):
            print(text, "=> success")
        elif response == ssh_byte(SSH_AGENT_FAILURE):
            print("FAIL!", text, "=> failure")
            self.ok = False
        else:
            print("FAIL!", text, "=>", self.fmt_response(response))
            self.ok = False

    def check_keylist(self, K, expected_keys):
        keys = K.List()
        print("list keys =>", self.fmt_keylist(keys))
        if set(keys) != set(expected_keys):
            print("FAIL! Should have been", self.fmt_keylist(expected_keys))
            self.ok = False

    def gray_code_test(self, K):
        bks = list(enumerate_bits(K.examples))

        self.check_keylist(K, {})

        for old, new, diff in gray_code(len(K.examples)):
            bit, key = next((bit, key) for bit, key in bks if diff & bit)

            if new & bit:
                self.expect_success("insert " + key.comment.decode("ASCII"),
                                    key.Add())
            else:
                self.expect_success("delete " + key.comment.decode("ASCII"),
                                    key.Del())

            self.check_keylist(K, [key.public_only() for bit, key in bks
                                   if new & bit])

    def sign_test(self, K):
        for key in K.examples:
            for params, message, expected_answer in key.iter_tests():
                key.Add()
                actual_answer = key.Use(*params)
                key.Del()
                record = "{} with {}{}".format(
                    K.verb, key.comment.decode("ASCII"), message)
                if actual_answer == expected_answer:
                    print(record, "=> success")
                else:
                    print("FAIL!", record, "=> {} but expected {}".format(
                        self.fmt_response(actual_answer),
                        self.fmt_response(expected_answer)))
                    self.ok = False

    def run(self):
        self.expect_success("init: delete all ssh2 keys", Key2.DelAll())

        for K in [Key2, Key1]:
            self.gray_code_test(K)
            self.sign_test(K)

    # TODO: negative tests of all kinds.

def main():
    Key2.make_examples()
    Key1.make_examples()

    tr = TestRunner()
    tr.run()
    if tr.ok:
        print("Test run passed")
    else:
        sys.exit("Test run failed!")

if __name__ == "__main__":
    main()
