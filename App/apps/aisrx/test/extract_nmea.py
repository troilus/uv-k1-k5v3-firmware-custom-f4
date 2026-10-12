#!/usr/bin/env python3
"""Extract real AIS payloads from an AIVDM NMEA file -> payload bit lists.

NMEA 0183 armoring packs 6 bits/char (A=0..Z=25 then a-w).  Dearmor gives the
exact on-air bit stream (before CRC/bit-stuffing), which we feed through the
same GMSK chain as the synthetic vectors.  Returns payload bits INCLUDING the
16-bit FCS? No: AIVDM payload excludes FCS, so we re-append our own CRC when
building the frame (same as gen_ais.frame_bits).

    python extract_nmea.py <nmea-file> [max_msgs]
"""
import sys


def dearmor(payload):
    bits = []
    for c in payload:
        v = ord(c) - 48
        if v > 40:
            v -= 8
        for k in range(5, -1, -1):
            bits.append((v >> k) & 1)
    return bits


def parse_file_simple(path):
    """Reassemble multi-part AIVDM; yield (msg_type, payload_bits)."""
    pending = []
    with open(path, "r", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line.startswith("!AIVDM"):
                continue
            body = line[1:].split("*")[0]
            fld = body.split(",")
            fcnt, fidx = int(fld[1]), int(fld[2])
            pl, fill = fld[5], int(fld[6])
            pending.append((fidx, pl, fill))
            if fidx == fcnt:
                pending.sort()
                bits = []
                for _, p, _ in pending:
                    bits.extend(dearmor(p))
                if fill:
                    bits = bits[:-fill]
                pending = []
                if len(bits) >= 6:
                    t = 0
                    for k in range(6):
                        t = (t << 1) | bits[k]
                    yield (t, bits)


if __name__ == "__main__":
    path = sys.argv[1]
    lim = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    seen = {}
    for t, bits in parse_file_simple(path):
        if t not in (1, 3, 5):
            continue
        seen.setdefault(t, 0)
        if seen[t] >= (lim or 3):
            continue
        seen[t] += 1
        print("type=%d len=%d" % (t, len(bits)))
