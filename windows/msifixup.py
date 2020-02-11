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

def make_changes(msi, args):
    run(["msidump", "-t", msi], args.verbose)
    build_cmd = ["msibuild", msi]

    def change_table(filename):
        with open(filename) as fh:
            lines = [line.rstrip("\r\n").split("\t")
                     for line in iter(fh.readline, "")]

        for line in lines[3:]:
            yield line

        with open(filename, "w") as fh:
            for line in lines:
                fh.write("\t".join(line) + "\r\n")

        build_cmd.extend(["-i", filename])

    if args.platform is not None:
        for line in change_table("_SummaryInformation.idt"):
            if line[0] == "7":
                line[1] = ";".join([args.platform] + line[1].split(";", 1)[1:])

    if args.dialog_bmp_width is not None:
        for line in change_table("Control.idt"):
            if line[9] == "WixUI_Bmp_Dialog":
                line[5] = args.dialog_bmp_width

    run(build_cmd, args.verbose)

def main():
    parser = argparse.ArgumentParser(
        description='Change the platform field of an MSI installer package.')
    parser.add_argument("msi", help="MSI installer file.")
    parser.add_argument("--platform", help="Change the platform field.")
    parser.add_argument("--dialog-bmp-width", help="Change the width field"
                        " in all uses of WixUI_Bmp_Dialog.")
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
        make_changes(msi, args)
    finally:
        if args.keep:
            sys.stdout.write(
                "Retained temporary directory {}\n".format(tempdir))
        else:
            shutil.rmtree(tempdir)

if __name__ == '__main__':
    main()
