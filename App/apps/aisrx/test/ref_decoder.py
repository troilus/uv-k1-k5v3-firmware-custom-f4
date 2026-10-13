#!/usr/bin/env python3
"""Independent AIS reference decoder (Python, Windows) -- deliberately does NOT
share the C decoder's timing recovery. It uses burst detection + brute-force
symbol-phase search + integrate-and-dump, then HDLC de-stuff / flag split / CRC
(ship162 convention, residue 0xF0B8) and pyais for the final message.

Purpose: run on the REAL IQ to get ground truth, and compare with the C decoder.

    python ref_decoder.py "../AIS IQ.wav" 48000
"""
import os
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "pyais-main"))
from pyais import decode  # noqa: E402

FLAG = [0, 1, 1, 1, 1, 1, 1, 0]


def fm_disc(iq):
    return np.angle(iq[1:] * np.conj(iq[:-1]))


def to_8sps(x, fs):
    """linear resample is fine for a probe; 8 samples per 9600-baud symbol."""
    fs8 = 9600.0 * 8
    n = int(len(x) * fs8 / fs)
    t = np.arange(n) * (fs / fs8)
    return np.interp(t, np.arange(len(x)), x)


def crc16_residue_msb(bits):
    by = bytearray()
    n = (len(bits) // 8) * 8
    for i in range(0, n, 8):
        v = 0
        for k in range(8):
            v = (v << 1) | (bits[i + k] & 1)
        by.append(v)
    crc = 0xFFFF
    for b in by:
        c = b
        for _ in range(8):
            mix = (crc ^ (c >> 7)) & 1
            crc >>= 1
            if mix:
                crc ^= 0x8408
            c = (c << 1) & 0xFF
    return crc == 0xF0B8, by


def destuff(bits):
    out = []
    ones = 0
    for b in bits:
        if b:
            ones += 1
            out.append(1)
        else:
            if ones != 5:
                out.append(0)
            ones = 0
    return out


def nrzi(raw):
    out = []
    prev = 0
    for b in raw:
        out.append(1 if b == prev else 0)
        prev = b
    return out


def split_frames(bits):
    """destuffed bits -> candidate frames between FLAG occurrences."""
    idx = []
    i = 0
    n = len(bits)
    while i <= n - 8:
        if bits[i:i + 8] == FLAG:
            idx.append(i)
            i += 8
        else:
            i += 1
    frames = []
    for a, b in zip(idx, idx[1:]):
        seg = bits[a + 8:b]
        if len(seg) >= 24:
            frames.append(seg)
    return frames


def armor(payload_bits):
    b = list(payload_bits)
    pad = (-len(b)) % 6
    b += [0] * pad
    s = ""
    for i in range(0, len(b), 6):
        v = 0
        for k in range(6):
            v = (v << 1) | b[i + k]
        s += chr(v + 48) if v < 40 else chr(v + 56)
    return s


def which_type(payload_bits):
    v = 0
    for k in range(6):
        v = (v << 1) | payload_bits[k]
    return v


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "../AIS IQ.wav"
    fs = int(sys.argv[2]) if len(sys.argv) > 2 else 48000
    fmt = sys.argv[3] if len(sys.argv) > 3 else "auto"
    rot = float(sys.argv[4]) if len(sys.argv) > 4 else 0.0

    if fmt == "auto":
        fmt = "cu8" if path.endswith(".bin") else (
            "u16" if path.endswith(".u16") else "wav")

    if fmt == "wav":
        x, sr = sf.read(path, dtype="float64", always_2d=True)
        iq = x[:, 0] + 1j * x[:, 1]
        fs = sr
    elif fmt == "cu8":  # raw interleaved uint8, offset-binary around 127.5
        raw = np.fromfile(path, dtype=np.uint8).astype(np.float64)
        raw = (raw - 127.5) / 127.5
        iq = raw[0::2] + 1j * raw[1::2]
    else:  # u16: already-demodulated baseband, mid 2048
        bb = np.fromfile(path, dtype=np.uint16).astype(np.float64) - 2048.0
        iq = None

    if iq is not None:
        if rot:
            n = np.arange(len(iq))
            iq = iq * np.exp(-1j * 2 * np.pi * rot / fs * n)
        bb = fm_disc(iq)

    print("fs=%d n=%d dur=%.1fs fmt=%s rot=%.0f Hz"
          % (fs, len(bb), len(bb) / fs, fmt, rot))

    pw = (np.abs(iq) ** 2) if iq is not None else (bb ** 2)
    w = int(0.002 * fs)
    sm = np.convolve(pw, np.ones(w) / w, "same")
    thr = np.percentile(sm, 99.0)
    on = sm > thr
    d = np.diff(on.astype(int))
    starts = np.flatnonzero(d == 1) + 1
    ends = np.flatnonzero(d == -1) + 1
    if len(ends) < len(starts):
        ends = np.append(ends, len(on))
    bursts = [(a, b) for a, b in zip(starts, ends) if b - a > int(0.01 * fs)]
    print("bursts: %d" % len(bursts))

    got = []
    for a, b in bursts:
        seg = bb[max(0, a - w):min(len(bb), b + w)]
        s8 = to_8sps(seg, fs)
        for phase in range(8):
            syms = []
            k = phase
            while k + 8 <= len(s8):
                s = s8[k:k + 8].sum()
                syms.append(1 if s > 0 else 0)
                k += 8
            raw = nrzi(syms)
            for fr in split_frames(destuff(raw)):
                ok, by = crc16_residue_msb(fr)
                if ok:
                    pl = []
                    for byte in by[:-2]:
                        for kk in range(7, -1, -1):
                            pl.append((byte >> kk) & 1)
                    t = which_type(pl)
                    nmea = "!AIVDM,1,1,,A,%s,0" % armor(pl)
                    try:
                        m = decode(nmea.encode())
                        got.append((t, m.msg_type, getattr(m, "mmsi", None), by.hex()))
                    except Exception as e:
                        got.append((t, "ERR", str(e), by.hex()))
                    break
            else:
                continue
            break

    print("valid frames: %d" % len(got))
    for t, mt, mmsi, hx in got[:40]:
        print("  type=%s (decoded %s) mmsi=%s" % (t, mt, mmsi))


if __name__ == "__main__":
    main()
