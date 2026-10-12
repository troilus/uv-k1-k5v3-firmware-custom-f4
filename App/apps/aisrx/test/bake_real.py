#!/usr/bin/env python3
"""Bake a handful of real AIS payloads from the AISHub NMEA sample into a small
hex file the test harness can replay without the 4 MB source.

Writes test/real_payloads.txt: one line per message, "<type> <hex-bits>".

    python bake_real.py <nmea-file> [n_per_type]
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from extract_nmea import parse_file_simple  # noqa: E402

WANT = {1: 3, 3: 3, 5: 3}


def tohex(bits):
    h = ""
    for i in range(0, len(bits), 4):
        v = 0
        for k in range(4):
            v = (v << 1) | (bits[i + k] if i + k < len(bits) else 0)
        h += "%x" % v
    return h


def main():
    nmea = sys.argv[1]
    out = os.path.join(HERE, "real_payloads.txt")
    got = {1: 0, 3: 0, 5: 0}
    lines = []
    for t, bits in parse_file_simple(nmea):
        if t not in WANT or got[t] >= WANT[t]:
            continue
        if len(bits) != (168 if t in (1, 3) else 424):
            continue
        got[t] += 1
        lines.append("%d %s" % (t, tohex(bits)))
    with open(out, "w", newline="\n") as f:
        f.write("# real AIS payloads (AISHub sample), type + hex bit stream\n")
        for l in lines:
            f.write(l + "\n")
    print("baked", len(lines), "payloads ->", out, got)


if __name__ == "__main__":
    main()
