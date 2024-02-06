#!/usr/bin/env python3

# This is a demonstration example of how to write a
# keyboard-interactive authentication helper plugin using PuTTY's
# protocol for involving it in SSH connection setup.

import io
import os
import struct
import sys

# Exception class we'll use to get a clean exit on EOF.
class PluginEOF(Exception): pass

# ----------------------------------------------------------------------
#
# Marshalling and unmarshalling routines to write and read the
# necessary SSH data types to/from a binary file handle (which can
# include an io.BytesIO if you need to encode/decode in-process).
#
# Error handling is a totally ad-hoc mixture of 'assert' and just
# assuming things will have the right type, or be the right length of
# tuple, or be valid UTF-8. So it should be _robust_, in the sense
# that you'll get a Python exception if anything fails. But no
# sensible error reporting or recovery is implemented.
#
# That should be good enough, because PuTTY will log the plugin's
# standard error in its Event Log, so if the plugin crashes, you'll be
# able to retrieve the traceback.

def wr_byte(fh, b):
    assert 0 <= b < 0x100
    fh.write(bytes([b]))

def wr_boolean(fh, b):
    wr_byte(fh, 1 if b else 0)

def wr_uint32(fh, u):
    assert 0 <= u < 0x100000000
    fh.write(struct.pack(">I", u))

def wr_string(fh, s):
    wr_uint32(fh, len(s))
    fh.write(s)

def wr_string_utf8(fh, s):
    wr_string(fh, s.encode("UTF-8"))

def rd_n(fh, n):
    data = fh.read(n)
    if len(data) < n:
        raise PluginEOF()
    return data

def rd_byte(fh):
    return rd_n(fh, 1)[0]

def rd_boolean(fh):
    return rd_byte(fh) != 0

def rd_uint32(fh):
    return struct.unpack(">I", rd_n(fh, 4))[0]

def rd_string(fh):
    length = rd_uint32(fh)
    return rd_n(fh, length)

def rd_string_utf8(fh):
    return rd_string(fh).decode("UTF-8")

# ----------------------------------------------------------------------
#
# Protocol definitions.

our_max_version = 2

PLUGIN_INIT                 =  1
PLUGIN_INIT_RESPONSE        =  2
PLUGIN_PROTOCOL             =  3
PLUGIN_PROTOCOL_ACCEPT      =  4
PLUGIN_PROTOCOL_REJECT      =  5
PLUGIN_AUTH_SUCCESS         =  6
PLUGIN_AUTH_FAILURE         =  7
PLUGIN_INIT_FAILURE         =  8
PLUGIN_KI_SERVER_REQUEST    = 20
PLUGIN_KI_SERVER_RESPONSE   = 21
PLUGIN_KI_USER_REQUEST      = 22
PLUGIN_KI_USER_RESPONSE     = 23

# ----------------------------------------------------------------------
#
# Classes to make it easy to construct and receive messages.
#
# OutMessage is constructed with the message type; then you use the
# wr_foo() routines to add fields to it, and finally call its send()
# method.
#
# InMessage is constructed via the expect() class method, to which you
# give a list of message types you expect to see one of at this stage.
# Once you've got one, you can rd_foo() fields from it.

class OutMessage:
    def __init__(self, msgtype):
        self.buf = io.BytesIO()
        wr_byte(self.buf, msgtype)
        self.write = self.buf.write

    def send(self, fh=sys.stdout.buffer):
        wr_string(fh, self.buf.getvalue())
        fh.flush()

class InMessage:
    @classmethod
    def expect(cls, expected_types, fh=sys.stdin.buffer):
        self = cls()
        self.buf = io.BytesIO(rd_string(fh))
        self.msgtype = rd_byte(self.buf)
        self.read = self.buf.read

        if self.msgtype not in expected_types:
            raise ValueError("received packet type {:d}, expected {}".format(
                self.msgtype, ",".join(map("{:d}".format,
                                           sorted(expected_types)))))
        return self

# ----------------------------------------------------------------------
#
# The main implementation of the protocol.

def protocol():
    # Start by expecting PLUGIN_INIT.
    msg = InMessage.expect({PLUGIN_INIT})
    their_version = rd_uint32(msg)
    hostname = rd_string_utf8(msg)
    port = rd_uint32(msg)
    username = rd_string_utf8(msg)
    print(f"Got hostname {hostname!r}, port {port!r}", file=sys.stderr)

    # Decide which protocol version we're speaking.
    version = min(their_version, our_max_version)
    assert version != 0, "Protocol version 0 does not exist"

    if "TESTPLUGIN_INIT_FAIL" in os.environ:
        # Test the plugin failing at startup time.
        msg = OutMessage(PLUGIN_INIT_FAILURE)
        wr_string_utf8(msg, os.environ["TESTPLUGIN_INIT_FAIL"])
        msg.send()
        return

    # Send INIT_RESPONSE, with our protocol version and an overridden
    # username.
    #
    # By default this test plugin doesn't override the username, but
    # you can make it do so by setting TESTPLUGIN_USERNAME in the
    # environment.
    msg = OutMessage(PLUGIN_INIT_RESPONSE)
    wr_uint32(msg, version)
    wr_string_utf8(msg, os.environ.get("TESTPLUGIN_USERNAME", ""))
    msg.send()

    # Outer loop run once per authentication protocol.
    while True:
        # Expect a message telling us what the protocol is.
        msg = InMessage.expect({PLUGIN_PROTOCOL})
        method = rd_string(msg)

        if "TESTPLUGIN_PROTO_REJECT" in os.environ:
            # Test the plugin failing at PLUGIN_PROTOCOL time.
            msg = OutMessage(PLUGIN_PROTOCOL_REJECT)
            wr_string_utf8(msg, os.environ["TESTPLUGIN_PROTO_REJECT"])
            msg.send()
            continue

        # We only support keyboard-interactive. If we supported other
        # auth methods, this would be the place to add further clauses
        # to this if statement for them.
        if method == b"keyboard-interactive":
            msg = OutMessage(PLUGIN_PROTOCOL_ACCEPT)
            msg.send()

            # Inner loop run once per keyboard-interactive exchange
            # with the SSH server.
            while True:
                # Expect a set of prompts from the server, or
                # terminate the loop on SUCCESS or FAILURE.
                #
                # (We could also respond to SUCCESS or FAILURE by
                # updating caches of our own, if we had any that were
                # useful.)
                msg = InMessage.expect({PLUGIN_KI_SERVER_REQUEST,
                                        PLUGIN_AUTH_SUCCESS,
                                        PLUGIN_AUTH_FAILURE})
                if (msg.msgtype == PLUGIN_AUTH_SUCCESS or
                    msg.msgtype == PLUGIN_AUTH_FAILURE):
                    break

                # If we didn't just break, we're sitting on a
                # PLUGIN_KI_SERVER_REQUEST message. Get all its bits
                # and pieces out.
                name = rd_string_utf8(msg)
                instructions = rd_string_utf8(msg)
                language = rd_string(msg)
                nprompts = rd_uint32(msg)
                prompts = []
                for i in range(nprompts):
                    prompt = rd_string_utf8(msg)
                    echo = rd_boolean(msg)
                    prompts.append((prompt, echo))

                # Actually make up some answers for the prompts. This
                # is the part that a non-example implementation would
                # do very differently, of course!
                #
                # Here, we answer "foo" to every prompt, except that
                # if there are exactly two prompts in the packet then
                # we answer "stoat" to the first and "weasel" to the
                # second.
                #
                # (These answers are consistent with the ones required
                # by PuTTY's test SSH server Uppity in its own
                # keyboard-interactive test implementation: that
                # presents a two-prompt packet and expects
                # "stoat","weasel" as the answers, and then presents a
                # zero-prompt packet. So this test plugin will get you
                # through Uppity's k-i in a one-touch manner. The
                # "foo" in this code isn't used by Uppity at all; I
                # just include it because I had to have _some_
                # handling for the else clause.)
                #
                # If TESTPLUGIN_PROMPTS is set in the environment, we
                # ask the user questions of our own by sending them
                # back to PuTTY as USER_REQUEST messages.
                if nprompts == 2:
                    if "TESTPLUGIN_PROMPTS" in os.environ:
                        for i in range(2):
                            # Make up some questions to ask.
                            msg = OutMessage(PLUGIN_KI_USER_REQUEST)
                            wr_string_utf8(
                                msg, "Plugin request #{:d} (name)".format(i))
                            wr_string_utf8(
                                msg, "Plugin request #{:d} (instructions)"
                                .format(i))
                            wr_string(msg, b"")
                            wr_uint32(msg, 2)
                            wr_string_utf8(msg, "Prompt 1 of 2 (echo): ")
                            wr_boolean(msg, True)
                            wr_string_utf8(msg, "Prompt 2 of 2 (no echo): ")
                            wr_boolean(msg, False)
                            msg.send()

                            # Expect the answers.
                            msg = InMessage.expect({PLUGIN_KI_USER_RESPONSE})
                            user_nprompts = rd_uint32(msg)
                            assert user_nprompts == 2, (
                                "Should match what we just sent")
                            for i in range(nprompts):
                                user_response = rd_string_utf8(msg)
                                # We don't actually check these
                                # responses for anything.

                    answers = ["stoat", "weasel"]

                else:
                    answers = ["foo"] * nprompts

                # Send the answers to the SSH server's questions.
                msg = OutMessage(PLUGIN_KI_SERVER_RESPONSE)
                wr_uint32(msg, len(answers))
                for answer in answers:
                    wr_string_utf8(msg, answer)
                msg.send()

        else:
            # Default handler if we don't speak the offered protocol
            # at all.
            msg = OutMessage(PLUGIN_PROTOCOL_REJECT)
            wr_string_utf8(msg, "")
            msg.send()

# Demonstration write to stderr, to prove that it shows up in PuTTY's
# Event Log.
print("Hello from test plugin's stderr", file=sys.stderr)

try:
    protocol()
except PluginEOF:
    pass
