#!/usr/bin/env python3
"""T-IQ: run the exact AIS demod chain on the SDR-recorded IQ.

AIS IQ.wav is 48 kHz complex IQ centred on the channel. FM-discriminate it to
the baseband the decoder eats, resample to 38.4 kHz (4 samples/symbol), write
out/sdriq*.u16, then host_ais decodes it. Also writes low-pass variants to find
the bandwidth at which the chain breaks -- the number to compare with the
BK4819 RX filter.

    python diag_sdr_iq.py
"""
import os
import struct
import sys

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
FS_DEC = 38400


def fm_disc(iq):
    d = np.angle(iq[1:] * np.conj(iq[:-1]))
    return d - d.mean()


def resample(x, fs_in, fs_out):
    n_out = int(round(len(x) * fs_out / float(fs_in)))
    t = np.arange(n_out) * (fs_in / float(fs_out))
    return np.interp(t, np.arange(len(x)), x)


def lowpass(x, fs, fc, ntaps=255):
    fc_n = fc / float(fs)
    half = (ntaps - 1) // 2
    h = np.empty(ntaps)
    for i in range(ntaps):
        m = i - half
        h[i] = (2.0 * fc_n if m == 0
                else np.sin(2 * np.pi * fc_n * m) / (np.pi * m))
    h *= 0.54 - 0.46 * np.cos(2 * np.pi * np.arange(ntaps) / (ntaps - 1))
    h /= h.sum()
    return np.convolve(x, h, "same")


def to_u16(x):
    x = x - x.mean()
    pk = np.percentile(np.abs(x), 99.5) or (np.abs(x).max() or 1.0)
    y = 2048.0 + x * (1800.0 / pk)
    return np.clip(y, 0, 4095).astype(np.uint16)


def write_u16(name, samp):
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, name + ".u16"), "wb") as f:
        f.write(struct.pack("<%dH" % len(samp), *samp))


def main():
    src = os.path.join(HERE, "..", "AIS IQ.wav")
    x, sr = sf.read(src, dtype="float64", always_2d=True)
    iq = x[:, 0] + 1j * x[:, 1]
    print("IQ: fs=%d n=%d dur=%.1fs" % (sr, len(iq), len(iq) / sr))

    base = fm_disc(iq)                       # 48 kHz discriminator baseband
    bb = resample(base, sr, FS_DEC)          # 38.4 kHz, 4 sps
    write_u16("sdriq", to_u16(bb))
    print("wrote sdriq.u16 (%d samples @ %d Hz)" % (len(bb), FS_DEC))

    for fc in (16000, 12000, 10000, 8000, 7000, 6500, 6000, 5000, 4000, 3000):
        f = lowpass(bb, FS_DEC, fc)
        write_u16("sdriq_bw%d" % fc, to_u16(f))
    print("wrote sdriq_bw*.u16 (bandwidth sweep at the 38.4 kHz rate)")


if __name__ == "__main__":
    sys.exit(main())
