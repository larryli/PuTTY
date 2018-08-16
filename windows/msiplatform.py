#!/usr/bin/env python

import argparse
import os
import tempfile
import shutil
import subprocess
import pipes

def run(command, verbose):
    if verbose:
        sys.stdout.write("$ {}\n".format(" ".join(
            pipes.quote(word) for word in command)))
    out = subprocess.check_output(command)
    if verbose:
        sys.stdout.write("".join(
            "> {}\n".format(line) for line in out.splitlines()))

def set_platform(msi, platform, verbose):
    run(["msidump", "-t", msi], verbose)

    summary_stream = "_SummaryInformation.idt"

    with open(summary_stream) as fh:
        lines = [line.rstrip("\r\n").split("\t")
                 for line in iter(fh.readline, "")]

    for line in lines[3:]:
        if line[0] == "7":
            line[1] = ";".join([platform] + line[1].split(";", 1)[1:])

    with open(summary_stream, "w") as fh:
        for line in lines:
            fh.write("\t".join(line) + "\r\n")

    run(["msibuild", msi, "-i", summary_stream], verbose)

def main():
    parser = argparse.ArgumentParser(
        description='Change the platform field of an MSI installer package.')
    parser.add_argument("msi", help="MSI installer file.")
    parser.add_argument("platform", help="New value for the platform field.")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Log what this script is doing.")
    parser.add_argument("-k", "--keep", action="store_true",
                        help="Don't delete the temporary working directory.")
    args = parser.parse_args()

    msi = os.path.abspath(args.msi)
    msidir = os.path.dirname(msi)
    try:
        tempdir = tempfile.mkdtemp(dir=msidir)
        os.chdir(tempdir)
        set_platform(msi, args.platform, args.verbose)
    finally:
        if args.keep:
            sys.stdout.write(
                "Retained temporary directory {}\n".format(tempdir))
        else:
            shutil.rmtree(tempdir)

if __name__ == '__main__':
    main()
