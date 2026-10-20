#!/usr/bin/env python3
"""T1: run the exact demod chain on a REAL recorded AIS waveform.

Loads AISSound.ogg (rtl-sdr "busy AIS", 48 kHz baseband), resamples to the
38.4 kHz the decoder expects (4 samples/symbol @ 9600 baud), normalizes around
mid-scale like the BK4819 ADC (2048 +- ~1100, matching gen_ais amplitudes),
writes out/realaudio.u16.  host_ais then runs on it.

  * decodes >=1 frame  -> the algorithm handles a real AIS waveform; the
                          on-air failure is RF/antenna/SNR (not the decoder).
  * decodes 0          -> inconclusive (this recording is known to lack a clean
                          9600-baud lag-5 structure), but worth a look.

Windows python has numpy+soundfile; run this there, then host_ais under WSL.
    python diag_real_audio.py
"""
import os
import struct
import sys

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
FS_DEC = 38400
TARGET_PK = 1100.0          # +-swing around 2048, matches gen_ais render()


def load_resample(path, fs_out=38400):
    audio, fs = sf.read(path, dtype="float32", always_2d=True)
    mono = audio.mean(axis=1)
    # linear-interp resample fs -> fs_out (AIS baseband is low; fine for a probe)
    n_out = int(round(len(mono) * fs_out / float(fs)))
    t_out = np.arange(n_out) * (fs / float(fs_out))
    mono = np.interp(t_out, np.arange(len(mono)), mono)
    return mono


def normalize(mono):
    # remove DC, scale so a high percentile maps to TARGET_PK
    mono = mono - mono.mean()
    p95 = np.percentile(np.abs(mono), 95)
    if p95 <= 0:
        p95 = np.abs(mono).max() or 1.0
    mono = mono * (TARGET_PK / p95)
    return 2048.0 + mono


def main():
    fs_out = int(sys.argv[1]) if len(sys.argv) > 1 else 38400
    src = os.path.join(HERE, "..", "AISSound.ogg")
    if not os.path.exists(src):
        print("missing", src)
        return 1
    mono = load_resample(src, fs_out)
    x = normalize(mono)
    x = np.clip(x, 0, 4095).astype(np.uint16)
    os.makedirs(OUT, exist_ok=True)
    out = os.path.join(OUT, "realaudio_%d.u16" % fs_out)
    with open(out, "wb") as f:
        f.write(struct.pack("<%dH" % len(x), *x))
    print("wrote %s  samples=%d  dur=%.1fs  (run host_ais/diag_frame on it)"
          % (out, len(x), len(x) / fs_out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
