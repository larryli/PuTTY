#!/usr/bin/env python3

import argparse
import os
import random
import socket
import sys

from ssh import *

def make_connections(n):
    connections = []

    for _ in range(n):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(os.environ["SSH_AUTH_SOCK"])
        connections.append(s)

    return connections

def use_connection(s, idstring):
    print("Trying {}...".format(idstring), end="")
    sys.stdout.flush()

    s.send(ssh_string(ssh_byte(SSH2_AGENTC_EXTENSION) + ssh_string(
        b"nonexistent-agent-extension@putty.projects.tartarus.org")))
    length = ssh_decode_uint32(s.recv(4))
    assert length < AGENT_MAX_MSGLEN
    msg = s.recv(length)
    msgtype = msg[0]
    msgstring = (
        "SSH_AGENT_EXTENSION_FAILURE" if msgtype == SSH_AGENT_EXTENSION_FAILURE
        else "SSH_AGENT_FAILURE" if msgtype == SSH_AGENT_FAILURE
        else "type {:d}".format(msgtype))
    print("got", msgstring, "with {:d}-byte payload".format(len(msg)-1))

def randomly_use_connections(connections, iterations):
    for _ in range(iterations):
        index = random.randrange(0, len(connections))
        s = connections[index]
        use_connection(connections[index], "#{:d}".format(index))

def main():
    parser = argparse.ArgumentParser(
        description='Test handling of multiple agent connections.')
    parser.add_argument("--nsockets", type=int, default=128,
                        help="Number of simultaneous connections to make.")
    parser.add_argument("--ntries", type=int, default=1024,
                        help="Number of messages to send in total.")
    args = parser.parse_args()

    connections = make_connections(args.nsockets)
    randomly_use_connections(connections, args.ntries)

if __name__ == '__main__':
    main()
