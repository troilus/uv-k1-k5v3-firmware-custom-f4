#!/usr/bin/env python3
"""Verify the AIS CRC/FCS bit-order convention before the C decoder is written.

Tries all four combinations (complement/not, LSB-first/MSB-first append) and
reports which yields the standard residue 0xF0B8. Also checks the ship162
byte-packing equivalence.
"""
import sys


def crc_bits(bits, init=0xFFFF):
    crc = init
    for b in bits:
        mix = (crc ^ b) & 1
        crc >>= 1
        if mix:
            crc ^= 0x8408
    return crc


def crc_bytes(data, init=0xFFFF):
    """ship162 crc16(): processes each byte LSB-first."""
    crc = init
    for byte in data:
        b = byte
        for _ in range(8):
            mix = (crc ^ b) & 1
            crc >>= 1
            if mix:
                crc ^= 0x8408
            b >>= 1
    return crc


def pack_lsb(bits):
    out = []
    for i in range(0, len(bits), 8):
        v = 0
        for k in range(8):
            if i + k < len(bits) and bits[i + k]:
                v |= 1 << k
        out.append(v)
    return out


def pack_msb(bits):
    out = []
    for i in range(0, len(bits), 8):
        v = 0
        for k in range(8):
            if i + k < len(bits) and bits[i + k]:
                v |= 0x80 >> k
        out.append(v)
    return out


def main():
    payload = [((i * i * 7 + i) % 3) % 2 for i in range(168)]
    c = crc_bits(payload)
    f = (~c) & 0xFFFF
    print(f"crc(payload)      = {c:#06x}")
    print(f"~crc              = {f:#06x}")

    variants = {
        "A: ~crc LSB-first append ": payload + [(f >> k) & 1 for k in range(16)],
        "B: ~crc MSB-first append ": payload + [(f >> (15 - k)) & 1 for k in range(16)],
        "C:  crc LSB-first append ": payload + [(c >> k) & 1 for k in range(16)],
        "D:  crc MSB-first append ": payload + [(c >> (15 - k)) & 1 for k in range(16)],
    }
    ok = False
    for name, bits in variants.items():
        r = crc_bits(bits)
        mark = " <-- residue" if r == 0xF0B8 else ""
        print(f"residue {name} = {r:#06x}{mark}")
        if r == 0xF0B8:
            ok = True

    # ship162 packing equivalence: arrival bits packed LSB-first, crc16 byte-wise
    print("ship162-pack+crc16(A) =", hex(crc_bytes(pack_lsb(variants["A: ~crc LSB-first append "]))))
    print("MSB-pack+MSB-first crc(A) =",
          hex(crc_bytes_msb(pack_msb(variants["A: ~crc LSB-first append "]))))

    # an "MSB-first" byte CRC = arrival-order if the bytes were packed MSB-first
    if not ok:
        print("FAIL: no variant yields 0xF0B8", file=sys.stderr)
        return 1
    return 0


def crc_bytes_msb(data, init=0xFFFF):
    crc = init
    for byte in data:
        for k in range(8):
            b = (byte >> (7 - k)) & 1
            mix = (crc ^ b) & 1
            crc >>= 1
            if mix:
                crc ^= 0x8408
    return crc


if __name__ == "__main__":
    sys.exit(main())
