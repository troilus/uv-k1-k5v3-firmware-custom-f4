import numpy as np
import sys

fs = 96000
raw = np.fromfile("../ship162-master/crates/rs162/data/ais_96k.bin",
                  dtype=np.uint8).astype(np.float64)
raw = (raw - 127.5) / 127.5
iq = raw[0::2] + 1j * raw[1::2]
rot, name = 25000, "b"          # channel B (+25k), which the reference decoded
n = np.arange(len(iq))
r = iq * np.exp(-1j * 2 * np.pi * rot / fs * n)
d = np.angle(r[1:] * np.conj(r[:-1]))
d -= d.mean()

for fs2 in (38400, 76800, 153600):
    m = int(len(d) * fs2 / fs)
    t = np.arange(m) * (fs / fs2)
    bb = np.interp(t, np.arange(len(d)), d)
    pk = np.percentile(np.abs(bb), 99.5) or 1.0
    y = np.clip(2048.0 + bb * (1800.0 / pk), 0, 4095).astype(np.uint16)
    y.tofile("out/ais96k_%s_%d.u16" % (name, fs2))
    print("wrote out/ais96k_%s_%d.u16  (%d sps)" % (name, fs2, fs2 // 9600))
