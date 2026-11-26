#!/usr/bin/env python3
"""Writes the Shift-JIS table of the Android app (android/app/src/main/cpp/cp932_table.inc).

The game's text is Shift-JIS as Windows reads it (code page 932). Android's C
library converts no Japanese encodings, so the app carries the table: for each
two-byte code its Unicode character, taken from Python's cp932 codec, which
follows Microsoft's own table. The file changes only if that codec does.

usage: make_cp932.py [OUT]
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "android", "app", "src", "main", "cpp", "cp932_table.inc")

LEADS = list(range(0x81, 0xA0)) + list(range(0xE0, 0xFD))
TRAILS = range(0x40, 0xFD)


def unicode_of(lead, trail):
    try:
        text = bytes([lead, trail]).decode("cp932")
    except UnicodeDecodeError:
        return 0
    # Every character of the code page is in the Basic Multilingual Plane.
    return ord(text) if len(text) == 1 and ord(text) < 0x10000 else 0


def main():
    lines = [
        "// Written by tools/android/make_cp932.py: do not edit by hand.",
        "// Code page 932 (Shift-JIS as Windows reads it) to Unicode. One row per lead byte",
        "// (0x81..0x9F, then 0xE0..0xFC), one entry per trail byte 0x40..0xFC; 0 = no character.",
    ]
    count = 0
    for lead in LEADS:
        row = [unicode_of(lead, trail) for trail in TRAILS]
        count += sum(1 for value in row if value)
        lines.append("{  // lead 0x%02X" % lead)
        for start in range(0, len(row), 16):
            lines.append("\t" + ",".join("0x%04X" % value for value in row[start:start + 16]) + ",")
        lines.append("},")
    with open(OUT, "w", encoding="ascii", newline="\n") as out:
        out.write("\n".join(lines) + "\n")
    print("%s: %d characters in %d rows" % (OUT, count, len(LEADS)))


if __name__ == "__main__":
    main()
