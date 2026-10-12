#!/usr/bin/env python3
"""Analyze real AIS audio recordings with the same bit pipeline as the C decoder.

Demod: integrate-and-dump at 9600 baud over SPS samples, try every phase,
NRZI (raw = bit==prev), HDLC flag hunt + stuff removal + CRC-16 (0xF0B8
residue).  Also reports basic signal stats so the real recording can be
compared against gen_ais.py's synthetic waveform.

    python analyze_real.py <audio-or-u16> [fs]
"""
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "pyais-main"))

BAUD = 9600


def load(path, fs_arg=None):
    if path.endswith(".u16"):
        raw = open(path, "rb").read()
        s = struct.unpack("<%dH" % (len(raw) // 2), raw)
        return [float(x) - 2048.0 for x in s], fs_arg or 38400
    import soundfile as sf
    data, fs = sf.read(path, dtype="float64", always_2d=True)
    return [float(x) for x in data[:, 0]], fs


def stats(x, fs):
    n = len(x)
    mean = sum(x) / n
    rms = math.sqrt(sum((v - mean) ** 2 for v in x) / n)
    peak = max(abs(v) for v in x)
    print("  samples=%d dur=%.3fs fs=%d mean=%.1f rms=%.1f peak=%.1f" %
          (n, n / fs, fs, mean, rms, peak))
    # burst segmentation: envelope above rms/3
    thr = rms / 3.0
    bursts, in_b, t0 = [], False, 0
    env_n = 16
    for i in range(0, n, env_n):
        w = x[i:i + env_n]
        e = math.sqrt(sum(v * v for v in w) / len(w))
        if e > thr and not in_b:
            in_b, t0 = True, i
        elif e <= thr and in_b:
            in_b = False
            if i - t0 > 100:
                bursts.append((t0, i))
    print("  bursts=%d" % len(bursts))
    for (a, b) in bursts[:12]:
        print("    t=%.3f..%.3f  len=%d samples (%.1f ms, %.0f symbols)" %
              (a / fs, b / fs, b - a, (b - a) / fs * 1000,
               (b - a) / fs * BAUD))


def slice_bits(x, fs, sps, phase):
    n = len(x) // sps * sps
    bits = []
    for i in range(phase, n - sps + 1, sps):
        m = sum(x[i:i + sps]) / sps
        bits.append(1 if m > 0 else 0)
    return bits


def crc16(bits):
    crc = 0xFFFF
    for b in bits:
        mix = (crc ^ b) & 1
        crc >>= 1
        if mix:
            crc ^= 0x8408
    return crc


FLAG = [0, 1, 1, 1, 1, 1, 1, 0]


def find_frames(bits):
    """same state machine idea as ais_bit(): hunt flag, collect, close on flag."""
    frames, ones, nbits, in_data, buf = [], 0, 0, False, []
    for b in bits:
        if not in_data:
            ones = ((ones << 1) | b) & 0x7F
            if [ones >> k & 1 for k in range(6, -1, -1)] == FLAG:
                in_data, nbits, buf, ones = True, 0, [], 0
            continue
        if b:
            ones += 1
            buf.append(b)
            nbits += 1
            if ones > 6:
                in_data, ones = False, 0
        else:
            if ones == 5:      # stuffed bit
                ones = 0
                continue
            buf.append(b)
            nbits += 1
            if ones == 6:      # closing flag: 6 ones + this 0
                body = buf[:nbits - 7]
                if len(body) >= 24 and len(body) % 8 == 0 and \
                        crc16(body) == 0xF0B8:
                    frames.append(body)
                buf, nbits, ones = [], 0, 0
            else:
                ones = 0
    return frames


def pack(bits):
    out = bytearray(len(bits) // 8)
    for k, b in enumerate(bits):
        if b:
            out[k >> 3] |= 0x80 >> (k & 7)
    return bytes(out)


def armor(bits):
    pad = (-len(bits)) % 6
    bits = list(bits) + [0] * pad
    out = []
    for i in range(0, len(bits), 6):
        v = 0
        for k in range(6):
            v = (v << 1) | bits[i + k]
        out.append(chr(v + 48) if v < 40 else chr(v + 56))
    return "".join(out)


def report(path, fs_arg=None):
    print(path)
    x, fs = load(path, fs_arg)
    stats(x, fs)
    sps = fs // BAUD
    print("  sps=%d" % sps)
    for ph in range(sps):
        bits = slice_bits(x, fs, sps, ph)
        frames = find_frames(bits)
        if frames:
            print("  phase %d: %d CRC-valid frames" % (ph, len(frames)))
            for f in frames[:8]:
                by = pack(f)
                t = (by[0] >> 2) & 0x3F
                print("    len=%d bytes type=%d head=%s" %
                      (len(by), t, by[:12].hex()))
            return frames
    print("  no CRC-valid frames at any phase")
    return []


if __name__ == "__main__":
    report(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else None)
