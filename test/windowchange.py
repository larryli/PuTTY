#!/usr/bin/env python3

# Interactive test program for assorted ancillary changes to the
# terminal window - title, position, size, z-order, colour palette
# etc.

import argparse
import sys
import termios
import os
from time import sleep

def send(s):
    sys.stdout.write(s)
    sys.stdout.flush()

def query(s, lastchar=None):
    send(s)

    old_attrs = termios.tcgetattr(0)
    new_attrs = old_attrs.copy()
    new_attrs[3] &= ~(termios.ECHO | termios.ICANON)
    new_attrs[6][termios.VMIN] = 0
    new_attrs[6][termios.VTIME] = 1
    try:
        termios.tcsetattr(0, termios.TCSANOW, new_attrs)

        s = b""
        while True:
            c = os.read(0, 1)
            if len(c) == 0:
                break
            s += c
            if lastchar is not None and c[0] == lastchar:
                break
        return s

    finally:
        termios.tcsetattr(0, termios.TCSANOW, old_attrs)

def pause(prompt):
    input(prompt + (" " if len(prompt) > 0 else "") + "--More--")

def main():
    testnames = [
        "title",
        "icon",
        "minimise",
        "maximise",
        "minquery",
        "setpalette",
        "querypalette",
        "winpos",
        "setwinsize",
        "querywinsize",
        "zorder",
        "mousereport",
    ]

    parser = argparse.ArgumentParser(
        description='Test PuTTY\'s ancillary terminal updates')
    parser.add_argument("test", choices=testnames, nargs="*",
                        help='Sub-test to perform (default: all of them)')
    args = parser.parse_args()

    if len(args.test) == 0:
        dotest = lambda s: True
    else:
        testset = set(args.test)
        dotest = lambda s: s in testset

    if dotest("title"):
        send("\033]2;Title test 1\a")
        pause("Title test 1.")
        send("\033]2;Title test 2\a")
        pause("Title test 2.")

    if dotest("icon"):
        send("\033]1;Icon-title test 1\a")
        pause("Icon-title test 1.")
        send("\033]1;Icon-title test 2\a")
        pause("Icon-title test 2.")

    if dotest("minimise"):
        pause("About to minimise and restore.")
        send("\033[2t")
        sleep(2)
        send("\033[1t")

    if dotest("maximise"):
        pause("About to maximise.")
        send("\033[9;1t")
        pause("About to unmaximise.")
        send("\033[9;0t")

    if dotest("minquery"):
        pause("Query minimised status.")
        s = query("\033[11t")
        if s == b"\033[1t":
            print("Reply: not minimised")
        elif s == b"\033[2t":
            print("Reply: minimised")
        else:
            print("Reply unknown:", repr(s))

    if dotest("setpalette"):
        print("Palette testing:")
        teststr = ""
        for i in range(8):
            teststr += "\033[0;{:d}m{:X}".format(i+30, i)
        for i in range(8):
            teststr += "\033[1;{:d}m{:X}".format(i+30, i+8)
        teststr += "\033[0;39mG\033[1mH\033[0;7mI\033[1mJ"
        teststr += "\033[m"
        teststr += " " * 9 + "K" + "\b" * 10
        for c in "0123456789ABCDEFGHIJKL":
            pause("Base:    " + teststr)
            send("\033]P" + c + "ff0000")
            pause(c + " red:   " + teststr)
            send("\033]P" + c + "00ff00")
            pause(c + " green: " + teststr)
            send("\033]P" + c + "0000ff")
            pause(c + " blue:  " + teststr)
            send("\033]R")

    if dotest("querypalette"):
        send("\033]R")
        nfail = 262
        for i in range(nfail):
            s = query("\033]4;{:d};?\a".format(i), 7).decode("ASCII")
            prefix, suffix = "\033]4;{:d};rgb:".format(i), "\a"
            if s.startswith(prefix) and s.endswith(suffix):
                rgb = [int(word, 16) for word in
                       s[len(prefix):-len(suffix)].split("/")]
                if 0 <= i < 8:
                    j = i
                    expected_rgb = [0xbbbb * ((j>>n) & 1) for n in range(3)]
                elif 0 <= i-8 < 8:
                    j = i-8
                    expected_rgb = [0x5555 + 0xaaaa * ((j>>n) & 1)
                                    for n in range(3)]
                elif 0 <= i-16 < 216:
                    j = i-16
                    expected_rgb = [(0 if v==0 else 0x3737+0x2828*v) for v in
                                    (j // 36, j // 6 % 6, j % 6)]
                elif 0 <= i-232 < 24:
                    j = i-232
                    expected_rgb = [j * 0x0a0a + 0x0808] * 3
                else:
                    expected_rgb = [
                        [0xbbbb, 0xbbbb, 0xbbbb], [0xffff, 0xffff, 0xffff],
                        [0x0000, 0x0000, 0x0000], [0x5555, 0x5555, 0x5555],
                        [0x0000, 0x0000, 0x0000], [0x0000, 0xffff, 0x0000],
                    ][i-256]
                if expected_rgb == rgb:
                    nfail -= 1
                else:
                    print(i, "unexpected: {:04x} {:04x} {:04x}".format(*rgb))
            else:
                print(i, "bad format:", repr(s))
        print("Query palette: {:d} colours wrong".format(nfail))

    if dotest("winpos"):
        print("Query window position: ", end="")
        s = query("\033[13t").decode("ASCII")
        prefix, suffix = "\033[3;", "t"
        if s.startswith(prefix) and s.endswith(suffix):
            x, y = [int(word) for word in
                    s[len(prefix):-len(suffix)].split(";")]
            print("x={:d} y={:d}".format(x, y))
        else:
            print("bad format:", repr(s))
            x, y = 50, 50

        pause("About to move window.")
        send("\033[3;{:d};{:d}t".format(x+50, y+50))
        pause("About to move it back again.")
        send("\033[3;{:d};{:d}t".format(x, y))

    if dotest("setwinsize"):
        pause("About to DECCOLM to 132 cols.")
        send("\033[?3h")
        pause("About to DECCOLM to 80 cols.")
        send("\033[?3l")
        pause("About to DECCOLM to 132 cols again.")
        send("\033[?3h")
        pause("About to reset the terminal.")
        send("\033c")
        pause("About to DECSLPP to 30 rows.")
        send("\033[30t")
        pause("About to DECSLPP to 24 rows.")
        send("\033[24t")
        pause("About to DECSNLS to 30 rows.")
        send("\033[*30|")
        pause("About to DECSNLS to 24 rows.")
        send("\033[*24|")
        pause("About to DECSCPP to 90 cols.")
        send("\033[$90|")
        pause("About to DECSCPP to 80 cols.")
        send("\033[$80|")
        pause("About to xterm to 90x30.")
        send("\033[8;30;90t")
        pause("About to xterm back to 80x24.")
        send("\033[8;24;80t")

    if dotest("querywinsize"):
        print("Query window size: ", end="")
        s = query("\033[14t").decode("ASCII")
        prefix, suffix = "\033[4;", "t"
        if s.startswith(prefix) and s.endswith(suffix):
            h, w = [int(word) for word in
                    s[len(prefix):-len(suffix)].split(";")]
            print("w={:d} h={:d}".format(w, h))
        else:
            print("bad format:", repr(s))

    if dotest("zorder"):
        pause("About to lower to bottom and then raise.")
        send("\033[6t")
        sleep(2)
        send("\033[5t")

    if dotest("mousereport"):
        send("\033[?1000h")
        pause("Mouse reporting on: expect clicks to generate input")
        send("\033[?1000l")
        pause("Mouse reporting off: expect clicks to select")

if __name__ == '__main__':
    main()
