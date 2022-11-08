#!/usr/bin/env python3

# Tool to read various files from the Unicode character database and
# generate headers containing derived arrays and lookup tables needed
# by PuTTY.
#
# The aim is to have this be a single tool which you can easily re-run
# against a new version of Unicode, simply by pointing it at an
# appropriate UCD.zip or a directory containing the same files
# unpacked.

import argparse
import collections
import io
import os
import string
import sys
import zipfile

UCDRecord = collections.namedtuple('UCDRecord', [
    'c',
    'General_Category',
    'Bidi_Class',
    'Decomposition_Mapping',
])

def to_ranges(iterable):
    """Collect together adjacent ranges in a list of (key, value) pairs.

    The input iterable should deliver a sequence of (key, value) pairs
    in which the keys are integers in sorted order. The output is a
    sequence of tuples with structure ((start, end), value), each
    indicating that all the keys [start, start+1, ..., end] go with
    that value.
    """
    start = end = val = None

    for k, v in iterable:
        if (k-1, v) == (end, val):
            end = k
        else:
            if start is not None:
                yield (start, end), val
            start, end, val = k, k, v

    if start is not None:
        yield (start, end), val

def map_to_ranges(m):
    """Convert an integer-keyed map into a list of (range, value) pairs."""
    yield from to_ranges(sorted(m.items()))

def set_to_ranges(s):
    """Convert a set into a list of ranges."""
    for r, _ in to_ranges((x, None) for x in sorted(s)):
        yield r

def lines(iterable, keep_comments=False):
    """Deliver the lines of a Unicode data file.

    The input iterable should yield raw lines of the file: for
    example, it can be the file handle itself. The output values have
    their newlines removed, comments and trailing spaces deleted, and
    blank lines discarded.
    """
    for line in iter(iterable):
        line = line.rstrip("\r\n")
        if not keep_comments:
            line = line.split("#", 1)[0]
        line = line.rstrip(" \t")
        if line == "":
            continue
        yield line

class Main:
    def run(self):
        "Parse arguments and generate all the output files."

        parser = argparse.ArgumentParser(
            description='Build UCD-derived source files.')
        parser.add_argument("ucd", help="UCD to work from, either UCD.zip or "
                            "a directory full of unpacked files.")
        args = parser.parse_args()

        if os.path.isdir(args.ucd):
            ucd_dir = args.ucd
            self.open_ucd_file = lambda filename: (
                open(os.path.join(ucd_dir, filename)))
        else:
            ucd_zip = zipfile.ZipFile(args.ucd)
            self.open_ucd_file = lambda filename: (
                io.TextIOWrapper(ucd_zip.open(filename)))

        self.find_unicode_version()

        with open("version.h", "w") as fh:
            self.write_version_header(fh)
        with open("bidi_type.h", "w") as fh:
            self.write_bidi_type_table(fh)
        with open("bidi_mirror.h", "w") as fh:
            self.write_bidi_mirroring_table(fh)
        with open("bidi_brackets.h", "w") as fh:
            self.write_bidi_brackets_table(fh)
        with open("nonspacing_chars.h", "w") as fh:
            self.write_nonspacing_chars_list(fh)
        with open("wide_chars.h", "w") as fh:
            self.write_wide_chars_list(fh)
        with open("ambiguous_wide_chars.h", "w") as fh:
            self.write_ambiguous_wide_chars_list(fh)

    def find_unicode_version(self):
        """Find out the version of Unicode.

        This is read from the top of NamesList.txt, which has the
        closest thing to a machine-readable statement of the version
        number that I found in the whole collection of files.
        """
        with self.open_ucd_file("NamesList.txt") as fh:
            for line in lines(fh):
                if line.startswith("@@@\t"):
                    self.unicode_version_full = line[4:]
                    self.unicode_version_short = " ".join(
                        w for w in self.unicode_version_full.split(" ")
                        if any(c in string.digits for c in w))
                    return

    @property
    def UnicodeData(self):
        """Records from UnicodeData.txt.

        Each yielded item is a UCDRecord tuple.
        """
        with self.open_ucd_file("UnicodeData.txt") as fh:
            for line in lines(fh):
                # Split up the line into its raw fields.
                (
                    codepoint, name, category, cclass, bidiclass, decomp,
                    num6, num7, num8, bidimirrored, obsolete_unicode1_name,
                    obsolete_comment, uppercase, lowercase, titlecase,
                ) = line.split(";")

                # By default, we expect that this record describes
                # just one code point.
                codepoints = [int(codepoint, 16)]

                # Spot the special markers where consecutive lines say
                # <Foo, First> and <Foo, Last>, indicating that the
                # entire range of code points in between are treated
                # the same. If so, we replace 'codepoints' with a
                # range object.
                if "<" in name:
                    assert name.startswith("<") and name.endswith(">"), (
                        "Confusing < in character name: {!r}".format(line))
                    name_pieces = [piece.strip(" \t") for piece in
                                   name.lstrip("<").rstrip(">").split(",")]
                    if "First" in name_pieces:
                        assert isinstance(codepoints, list)
                        prev_line_was_first = True
                        prev_codepoint = codepoints[0]
                        continue
                    elif "Last" in name_pieces:
                        assert prev_line_was_first
                        codepoints = range(prev_codepoint, codepoints[0]+1)
                        del prev_codepoint
                prev_line_was_first = False

                # Decode some of the raw fields into more cooked
                # forms.

                # For the moment, we only care about decomposition
                # mappings that consist of a single hex number (i.e.
                # are singletons and not compatibility mappings)
                try:
                    dm = [int(decomp, 16)]
                except ValueError:
                    dm = []

                # And yield a UCDRecord for each code point in our
                # range.
                for codepoint in codepoints:
                    yield UCDRecord(
                        c=codepoint,
                        General_Category=category,
                        Bidi_Class=bidiclass,
                        Decomposition_Mapping=dm,
                    )

    @property
    def BidiMirroring(self):
        """Parsed character pairs from BidiMirroring.txt.

        Each yielded tuple is a pair of Unicode code points.
        """
        with self.open_ucd_file("BidiMirroring.txt") as fh:
            for line in lines(fh):
                cs1, cs2 = line.split(";")
                c1 = int(cs1, 16)
                c2 = int(cs2, 16)
                yield c1, c2

    @property
    def BidiBrackets(self):
        """Bracket pairs from BidiBrackets.txt.

        Each yielded tuple is a pair of Unicode code points, followed
        by either 'o', 'c' or 'n' to indicate whether the first one is
        an open or closing parenthesis or neither.
        """
        with self.open_ucd_file("BidiBrackets.txt") as fh:
            for line in lines(fh):
                cs1, cs2, kind = line.split(";")
                c1 = int(cs1, 16)
                c2 = int(cs2, 16)
                kind = kind.strip(" \t")
                yield c1, c2, kind

    @property
    def EastAsianWidth(self):
        """East Asian width types from EastAsianWidth.txt.

        Each yielded tuple is (code point, width type).
        """
        with self.open_ucd_file("EastAsianWidth.txt") as fh:
            for line in lines(fh):
                fields = line.split(";")
                if ".." in fields[0]:
                    start, end = [int(s, 16) for s in fields[0].split("..")]
                    cs = range(start, end+1)
                else:
                    cs = [int(fields[0], 16)]
                for c in cs:
                    yield c, fields[1]

    def write_file_header_comment(self, fh, description):
        print("/*", file=fh)
        print(" * Autogenerated by read_ucd.py from",
              self.unicode_version_full, file=fh)
        print(" *", file=fh)
        for line in description.strip("\n").split("\n"):
            print(" *" + (" " if line != "" else "") + line, file=fh)
        print(" */", file=fh)
        print(file=fh)

    def write_version_header(self, fh):
        self.write_file_header_comment(fh, """

String literals giving the currently supported version of Unicode.
Useful for error messages and 'about' boxes.

""")
        assert all(0x20 <= ord(c) < 0x7F and c != '"'
                   for c in self.unicode_version_full)

        print("#define UNICODE_VERSION_FULL \"{}\"".format(
            self.unicode_version_full), file=fh)
        print("#define UNICODE_VERSION_SHORT \"{}\"".format(
            self.unicode_version_short), file=fh)

    def write_bidi_type_table(self, fh):
        self.write_file_header_comment(fh, """

Bidirectional type of every Unicode character, excluding those with
type ON.

Used by terminal/bidi.c, whose associated lookup function returns ON
by default for anything not in this list.

""")
        types = {}

        for rec in self.UnicodeData:
            if rec.Bidi_Class != "ON":
                types[rec.c] = rec.Bidi_Class

        for (start, end), t in map_to_ranges(types):
            print(f"{{0x{start:04x}, 0x{end:04x}, {t}}},", file=fh)

    def write_bidi_mirroring_table(self, fh):
        self.write_file_header_comment(fh, """

Map each Unicode character to its mirrored form when printing right to
left.

Used by terminal/bidi.c.

""")
        bidi_mirror = {}
        for c1, c2 in self.BidiMirroring:
            assert bidi_mirror.get(c1, c2) == c2, f"Clash at {c1:%04X}"
            bidi_mirror[c1] = c2
            assert bidi_mirror.get(c2, c1) == c1, f"Clash at {c2:%04X}"
            bidi_mirror[c2] = c1

        for c1, c2 in sorted(bidi_mirror.items()):
            print("{{0x{:04x}, 0x{:04x}}},".format(c1, c2), file=fh)

    def write_bidi_brackets_table(self, fh):
        self.write_file_header_comment(fh, """

Identify Unicode characters that count as brackets for the purposes of
bidirectional text layout. For each one, indicate whether it's an open
or closed bracket, and identify up to two characters that can act as
its counterpart.

Used by terminal/bidi.c.

""")
        bracket_map = {}
        for c1, c2, kind in self.BidiBrackets:
            bracket_map[c1] = kind, c2

        equivalents = {}
        for rec in self.UnicodeData:
            if len(rec.Decomposition_Mapping) == 1:
                c = rec.c
                c2 = rec.Decomposition_Mapping[0]
                equivalents[c] = c2
                equivalents[c2] = c

        for src, (kind, dst) in sorted(bracket_map.items()):
            dsteq = equivalents.get(dst, 0)
            # UCD claims there's an 'n' kind possible, but as of UCD
            # 14, no instances of it exist
            enumval = {'o': 'BT_OPEN', 'c': 'BT_CLOSE'}[kind]
            print("{{0x{:04x}, {{0x{:04x}, 0x{:04x}, {}}}}},".format(
                src, dst, dsteq, enumval), file=fh)

    def write_nonspacing_chars_list(self, fh):
        self.write_file_header_comment(fh, """

Identify Unicode characters that occupy no character cells of a
terminal.

Used by utils/wcwidth.c.

""")
        cs = set()

        for rec in self.UnicodeData:
            nonspacing = rec.General_Category in {"Me", "Mn", "Cf"}
            if rec.c == 0xAD:
                # In typography this is a SOFT HYPHEN and counts as
                # discardable. But it's also an ISO 8859-1 printing
                # character, and all of those occupy one character
                # cell in a terminal.
                nonspacing = False
            if 0x1160 <= rec.c <= 0x11FF:
                # Medial (vowel) and final (consonant) jamo for
                # decomposed Hangul characters. These are regarded as
                # non-spacing on the grounds that they compose with
                # the preceding initial consonant.
                nonspacing = True
            if nonspacing:
                cs.add(rec.c)

        for start, end in set_to_ranges(cs):
            print(f"{{0x{start:04x}, 0x{end:04x}}},", file=fh)

    def write_width_table(self, fh, accept):
        cs = set()

        for c, wid in self.EastAsianWidth:
            if wid in accept:
                cs.add(c)

        for start, end in set_to_ranges(cs):
            print(f"{{0x{start:04x}, 0x{end:04x}}},", file=fh)

    def write_wide_chars_list(self, fh):
        self.write_file_header_comment(fh, """

Identify Unicode characters that occupy two adjacent character cells
in a terminal.

Used by utils/wcwidth.c.

""")
        self.write_width_table(fh, {'W', 'F'})

    def write_ambiguous_wide_chars_list(self, fh):
        self.write_file_header_comment(fh, """

Identify Unicode characters that are width-ambiguous: some regimes
regard them as occupying two adjacent character cells in a terminal,
and others do not.

Used by utils/wcwidth.c.

""")
        self.write_width_table(fh, {'A'})

if __name__ == '__main__':
    Main().run()
