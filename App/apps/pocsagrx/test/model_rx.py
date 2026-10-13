#!/usr/bin/env python3
"""Model of the generic POCSAG receive chain (512 / 1200 / 2400 baud, direct FSK)
and the integer demodulator the radio app will run. Written to be transliterated
to C: integer arithmetic, shifts, no division in the per-sample path.

Chain: POCSAG bits -> discriminator NRZ (Hz) at FS_SIM -> radio audio path (RAW:
5 kHz low-pass) -> 12-bit ADC on PA4 at 38.4 kHz (optional clock error, noise)
-> a bank of three DemodInt (512/1200/2400) fed the same samples -> POCSAG words
-> numeric / alphanumeric payloads.

Demod (per sample):
  slow DC tracker -> low-pass biquad -> 1-bit matched-filter boxcar (running
  sum) -> symmetric peak tracker with hysteresis slice -> DPLL (65536 per bit)
  -> hard bit -> POCSAG sync / batch / BCH / payload.

  model_rx.py            runs the synthetic sweep (three rates, numeric + alpha)
  model_rx.py <file.wav> decodes a real recording at every rate
"""
import math
import os
import random
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pocsag as P

FS_SIM = 96000
FS_ADC = 38400
BIAS = 2048
LSB_PER_HZ = 0.065
DEV = 3000.0

# Per-rate demodulator config. 38.4 kHz = LCM(512,1200,2400), so every rate has
# an integer number of samples per bit. step = 65536 * baud / fs.
RATE_CFG = {
    512:  {"spb": 75, "step": round(65536 * 512 / FS_ADC),  "box": 75, "fc": 1500.0, "dc": 10, "pk": 10},
    1200: {"spb": 32, "step": 2048,                          "box": 32, "fc": 2000.0, "dc": 9,  "pk": 8},
    2400: {"spb": 16, "step": 4096,                          "box": 16, "fc": 3000.0, "dc": 9,  "pk": 8},
}
RATES = (512, 1200, 2400)


# ------------------------------------------------------------------ channel ---

def nrz(bits, baud, dev=DEV, fs=FS_SIM):
    spb = fs / baud
    out = []
    for i, b in enumerate(bits):
        n = int(round((i + 1) * spb)) - int(round(i * spb))
        out += [dev if b else -dev] * n
    return out


def biquad(x, fc, fs, kind="lp"):
    w = math.tan(math.pi * fc / fs)
    q = 1 / math.sqrt(2)
    if kind == "lp":
        a0 = 1 + w / q + w * w
        b0, b1, b2 = w * w, 2 * w * w, w * w
        a1, a2 = 2 * (w * w - 1), 1 - w / q + w * w
    else:  # hp
        a0 = 1 + w / q + w * w
        b0, b1, b2 = 1, -2, 1
        a1, a2 = 2 * (w * w - 1), 1 - w / q + w * w
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0
    y = []
    x1 = x2 = y1 = y2 = 0.0
    for v in x:
        o = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1, y2, y1 = x1, v, y1, o
        y.append(o)
    return y


def audio_path(x, mode, fs=FS_SIM):
    if mode == "raw":
        return biquad(x, 5000, fs, "lp")
    if mode == "std":
        x = biquad(x, 300, fs, "hp")
        a = math.exp(-1.0 / (750e-6 * fs))
        y = []; p = 0.0
        for v in x:
            p = p * a + (1 - a) * v; y.append(p)
        return biquad(y, 3000, fs, "lp")
    if mode == "ac":       # no DAC bias: capacitor coupling only
        return biquad(x, 20, fs, "hp")
    return x


def to_adc(x, fs=FS_SIM, out_fs=FS_ADC, clock_err=0.0, noise=0.0, seed=0):
    rng = random.Random(seed)
    step = (fs / out_fs) * (1.0 + clock_err)
    n = int(len(x) / step)
    y = []
    for k in range(n):
        p = k * step
        i = int(p)
        f = p - i
        v = x[i] * (1 - f) + x[min(i + 1, len(x) - 1)] * f
        y.append(v)
    if noise:
        y = [v + rng.gauss(0, noise) for v in y]
    return [int(round(BIAS + v * LSB_PER_HZ)) for v in y]


# -------------------------------------------------------------------- demod ---

def biquad_q14(fc, fs):
    w = math.tan(math.pi * fc / fs)
    q = 1 / math.sqrt(2)
    a0 = 1 + w / q + w * w
    return tuple(int(round(v * 16384)) for v in
                 (w * w / a0, 2 * w * w / a0, 2 * (w * w - 1) / a0,
                  (1 - w / q + w * w) / a0))


class DemodInt:
    """Integer demodulator transliterated to the C app. One instance per rate:
    the matched-filter boxcar length (samples/bit) and the DPLL step are the only
    rate-dependent constants besides the low-pass corner."""

    DC_SHIFT = 8
    PK_SHIFT = 8
    PLL_SHIFT = 3        # proportional: phase -= err >> 3
    INT_SHIFT = 9        # integral: iint += err >> 9

    def __init__(self, baud):
        c = RATE_CFG[baud]
        self.box = c["box"]
        self.step = c["step"]
        self.dc_shift = c["dc"]
        self.pk_shift = c["pk"]
        self.int_max = max(1, c["step"] * 2 // 100)
        self.B0, self.B1, self.A1, self.A2 = biquad_q14(c["fc"], FS_ADC)
        self.dc = BIAS
        self.x1 = self.x2 = self.y1 = self.y2 = 0
        self.hi = 200
        self.lo = -200
        self.ph = 0
        self.iint = 0
        self.last = 0
        self.buf = [0] * self.box
        self.bi = 0
        self.bsum = 0
        self.on_bit = None

    def sample(self, x):
        self.dc += (x - self.dc) >> self.dc_shift
        y = x - self.dc
        o = (self.B0 * y + self.B1 * self.x1 + self.B0 * self.x2
             - self.A1 * self.y1 - self.A2 * self.y2) >> 14
        self.x2, self.x1, self.y2, self.y1 = self.x1, y, self.y1, o
        self.bsum += o - self.buf[self.bi]
        self.buf[self.bi] = o
        self.bi = (self.bi + 1) % self.box
        y = self.bsum
        self.hi -= (self.hi - y) >> self.pk_shift
        if y > self.hi:
            self.hi = y
        self.lo += (y - self.lo) >> self.pk_shift
        if y < self.lo:
            self.lo = y
        mid = (self.hi + self.lo) >> 1
        h = (self.hi - self.lo) >> 6
        if y > mid + h:
            level = 1
        elif y < mid - h:
            level = 0
        else:
            level = self.last
        if level != self.last:
            err = self.ph
            if err > 32768:
                err -= 65536
            self.iint += err >> self.INT_SHIFT
            self.iint = max(-self.int_max, min(self.int_max, self.iint))
            self.ph -= (err >> self.PLL_SHIFT) + self.iint
            if self.ph < 0:
                self.ph += 65536
            elif self.ph >= 65536:
                self.ph -= 65536
        self.last = level
        p0 = self.ph
        self.ph += self.step
        if p0 < 32768 <= self.ph and self.on_bit:
            self.on_bit(level)
        if self.ph >= 65536:
            self.ph -= 65536


# -------------------------------------------------------------------- pocsag ---

class Receiver:
    """One POCSAG decoder state machine (rate-independent): sync hunt, 16-codeword
    batches, BCH(31,21) single-bit correction, message assembly. Keeps the raw
    message codewords so both numeric and alphanumeric payloads can be decoded."""

    def __init__(self, baud):
        self.baud = baud
        self.state = 0
        self.sr = 0
        self.nb = 0
        self.pol = 1
        self.wc = 0
        self.fp = 0
        self.inmsg = False
        self.addr = 0
        self.func = 0
        self.cws = []
        self.err = False
        self.msgs = []
        self.syncs = 0
        self.words = 0
        self.bch_ok = 0
        self.bch_fix = 0
        self.bch_bad = 0
        self.hunt = 0

    def flush(self):
        self.inmsg = False
        if not self.cws:
            return
        self.msgs.append((self.addr, self.func, self.err, list(self.cws)))
        self.cws = []

    def feed(self, bit):
        self.sr = ((self.sr << 1) | bit) & 0xFFFFFFFF
        if self.state == 0:
            if P.popcount(self.sr ^ P.SYNC_STD) <= 2:
                self.pol = 1; self.state = 1; self.wc = self.fp = self.nb = 0; self.syncs += 1
            elif P.popcount(self.sr ^ P.SYNC_INV) <= 2:
                self.pol = -1; self.state = 1; self.wc = self.fp = self.nb = 0; self.syncs += 1
            if self.inmsg:
                self.hunt += 1
                if self.hunt > 64:
                    self.flush()
            return
        self.nb += 1
        if self.nb != 32:
            return
        self.nb = 0
        raw = self.sr if self.pol == 1 else (~self.sr & 0xFFFFFFFF)
        cor, ok = P.bch_decode(raw)
        self.words += 1
        if ok:
            self.bch_ok += 1
            if cor != raw:
                self.bch_fix += 1
        else:
            self.bch_bad += 1
        self.fp += 1
        self.wc += 1
        if P.popcount(cor ^ P.SYNC_STD) <= 2:
            self.wc = self.fp = 0
        elif P.popcount(cor ^ P.IDLE_WORD) <= 2:
            if self.inmsg:
                self.flush()
        elif (cor >> 31) == 0:
            if self.inmsg:
                self.flush()
            self.func = (cor >> 11) & 3
            self.addr = ((cor >> 13) & 0x3FFFF) * 8 + (self.fp - 1) // 2
            self.cws = []
            self.inmsg = True
            self.err = not ok
        elif (cor >> 31) == 1:
            if self.inmsg:
                self.cws.append(cor)
                if not ok:
                    self.err = True
        if self.wc >= 16:
            self.state = 0


def run_demod(adc, baud):
    d = DemodInt(baud)
    rx = Receiver(baud)
    d.on_bit = rx.feed
    for x in adc:
        d.sample(x)
    rx.flush()
    return rx


def run_demod_multi(adc):
    """Three parallel decoders fed the same samples (what the app does)."""
    demods = {b: DemodInt(b) for b in RATES}
    receivers = {b: Receiver(b) for b in RATES}
    for b in RATES:
        demods[b].on_bit = receivers[b].feed
    for x in adc:
        for b in RATES:
            demods[b].sample(x)
    for b in RATES:
        receivers[b].flush()
    return receivers


# -------------------------------------------------------------------- tests ---

def synthetic(addr, func, cws, baud, mode="raw", noise=1500.0, clock_err=0.0,
              seed=0, ac=False):
    bits = P.transmission(addr, func, cws)
    lead = [0.0] * int(0.05 * FS_SIM)
    wave = lead + nrz(bits, baud) + lead
    wave = audio_path(wave, mode)
    if ac:
        wave = audio_path(wave, "ac")
    return to_adc(wave, clock_err=clock_err, noise=noise, seed=seed)


def _hit(recv, addr, text, alpha):
    for m in recv.msgs:
        if m[0] != addr or m[2]:
            continue
        got = P.decode_alpha(m[3]).rstrip() if alpha else P.decode_bcd(m[3])
        if got.rstrip() == text:
            return True
    return False


def sweep():
    cases = [
        ("clean", dict(noise=0.0)),
        ("n1500", dict(noise=1500.0)),
        ("n3000", dict(noise=3000.0)),
        ("n4500", dict(noise=4500.0)),
        ("clk+1%", dict(noise=0.0, clock_err=+0.01)),
        ("clk-1%", dict(noise=0.0, clock_err=-0.01)),
    ]
    for baud in RATES:
        print("\n=== %d baud ===" % baud)
        print("%-8s | %-6s | %-11s | %-8s | %s" %
              ("case", "sync", "words(+fix)", "num", "alpha"))
        for name, kw in cases:
            syncs = ok = bad = fix = 0
            num_ok = alpha_ok = 0
            for seed in range(3):
                s = synthetic(1234000, 3, P.numeric_cws("412   087 01234"),
                              baud, seed=seed, **kw)
                rx = run_demod(s, baud)
                num_ok += 1 if _hit(rx, 1234000, "412   087 01234", False) else 0
                syncs += rx.syncs; ok += rx.bch_ok; bad += rx.bch_bad; fix += rx.bch_fix
                s = synthetic(100000, 0, P.alpha_cws("TEST 1234"), baud,
                              seed=seed, **kw)
                rx = run_demod(s, baud)
                alpha_ok += 1 if _hit(rx, 100000, "TEST 1234", True) else 0
                syncs += rx.syncs; ok += rx.bch_ok; bad += rx.bch_bad; fix += rx.bch_fix
            print("%-8s | %-6d | %4d(%-4d)b%-3d | %-8s | %s" %
                  (name, syncs, ok, bad, fix,
                   "%d/3" % num_ok, "%d/3" % alpha_ok))


def from_wav(path):
    """Decode a discriminator recording with all three integer demodulators."""
    import array
    w = wave.open(path, "rb")
    fs = w.getframerate(); ch = w.getnchannels(); n = w.getnframes(); sw = w.getsampwidth()
    raw = w.readframes(n); w.close()
    if sw == 2:
        a = array.array("h"); a.frombytes(raw)
    elif sw == 1:
        a = array.array("B"); a.frombytes(raw)
        a = array.array("h", [(v - 128) << 8 for v in a])
    else:
        sys.exit("unsupported sample width %d" % sw)
    if ch > 1:
        a = array.array("h", [a[i] for i in range(0, len(a), ch)])
    peak = max(1, max(abs(v) for v in a))
    g = 600.0 / peak
    step = fs / float(FS_ADC)
    out = []
    i = 0.0
    while int(i) + 1 < len(a):
        k = int(i); f = i - k
        out.append(int(round(BIAS + (a[k] * (1 - f) + a[k + 1] * f) * g)))
        i += step
    recv = run_demod_multi(out)
    print("wav: fs=%d ch=%d n=%d" % (fs, ch, n))
    for baud in RATES:
        rx = recv[baud]
        print("  %4d baud: sync=%d words=%d ok=%d fix=%d bad=%d msgs=%d" %
              (baud, rx.syncs, rx.words, rx.bch_ok, rx.bch_fix, rx.bch_bad, len(rx.msgs)))
        for m in rx.msgs[:10]:
            print("     addr=%d func=%d err=%s num=%r alpha=%r" %
                  (m[0], m[1], m[2], P.decode_bcd(m[3]), P.decode_alpha(m[3])))


if __name__ == "__main__":
    if len(sys.argv) > 1:
        from_wav(sys.argv[1])
    else:
        sweep()
