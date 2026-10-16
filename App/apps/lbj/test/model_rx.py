#!/usr/bin/env python3
"""Model of the LBJ (POCSAG 1200 baud, direct FSK) receive chain, and the integer
demodulator the radio app will run. Written to be transliterated to C: integer
arithmetic, shifts, no division.

Chain: POCSAG bits -> discriminator NRZ (Hz) at FS_SIM -> radio audio path
(RAW: 5 kHz low-pass; STD: 300 Hz high-pass, 750 us de-emphasis, 3 kHz low-pass;
AC: 20 Hz coupling, i.e. no DAC bias) -> 12-bit ADC on PA4 at 19.2 kHz (optional
clock error, noise) -> Demod -> POCSAG words -> LBJ fields.

Demod (per 19.2 kHz sample):
  slow DC tracker -> low-pass biquad -> 1-bit matched-filter boxcar (running
  sum) -> symmetric peak tracker with hysteresis slice -> DPLL (65536 per bit,
  4096 per sample; a transition pulls phase to the bit boundary) -> hard bit ->
  POCSAG sync / batch / BCH / BCD.

  model_rx.py            runs the synthetic sweep
  model_rx.py <file.wav> decodes a real recording
"""
import math
import os
import random
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pocsag as P

FS_SIM = 96000
FS_ADC = 19200
BAUD = 1200
BIAS = 2048
LSB_PER_HZ = 0.065
DEV = 3000.0


# ------------------------------------------------------------------ channel ---

def nrz(bits, dev=DEV, fs=FS_SIM):
    spb = fs / BAUD
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


def deemp(x, tau, fs):
    a = math.exp(-1.0 / (tau * fs))
    y = []
    p = 0.0
    for v in x:
        p = p * a + (1 - a) * v
        y.append(p)
    return y


def audio_path(x, mode, fs=FS_SIM, hp=0.0):
    if mode == "raw":
        # RAW is not flat: the audio path still high-passes, and its corner is
        # not known on the BK4829 (upstream sweeps 10..1500 Hz). A high-passed
        # NRZ level droops back to zero during a run of equal bits (up to 13,
        # 11 ms), which is what breaks a plain threshold slicer on the radio.
        # hp=0 keeps the old, flat model; hp>0 is the honest one.
        y = biquad(x, 5000, fs, "lp")
        return biquad(y, hp, fs, "hp") if hp > 0.0 else y
    if mode == "std":
        x = biquad(x, 300, fs, "hp")
        x = deemp(x, 750e-6, fs)
        x = biquad(x, 3000, fs, "lp")
        return x
    if mode == "ac":       # no DAC bias: capacitor coupling only
        return biquad(x, 20, fs, "hp")
    return x


def to_adc(x, fs=FS_SIM, out_fs=FS_ADC, clock_err=0.0, noise=0.0, seed=0, ac=False):
    rng = random.Random(seed)
    t = [i for i in range(len(x))]
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

class Demod:
    def __init__(self, fs=FS_ADC, baud=BAUD, dc_shift=7, pk_shift=8,
                 pll_k=0.1, pll_i=0.005, fc=1500.0, lpf=True):
        self.fs = fs
        self.step = 65536 * baud / fs
        self.dc_shift = dc_shift
        self.pk_shift = pk_shift
        self.pll_k = pll_k
        self.pll_i = pll_i
        self.fc = fc
        self.lpf = lpf
        w = math.tan(math.pi * fc / fs)
        q = 1 / math.sqrt(2)
        a0 = 1 + w / q + w * w
        self.b0 = w * w / a0
        self.b1 = 2 * w * w / a0
        self.b2 = self.b0
        self.a1 = 2 * (w * w - 1) / a0
        self.a2 = (1 - w / q + w * w) / a0
        self.px1 = self.px2 = self.py1 = self.py2 = 0.0
        self.dc = BIAS
        self.hi = BIAS + 100.0
        self.lo = BIAS - 100.0
        self.ph = 0.0
        self.int = 0.0
        self.lh = 0
        self.lc = 0
        self.last = 0
        self.on_bit = None

    def filt(self, v):
        o = self.b0 * v + self.b1 * self.px1 + self.b2 * self.px2 - self.a1 * self.py1 - self.a2 * self.py2
        self.px2, self.px1, self.py2, self.py1 = self.px1, v, self.py1, o
        return o

    def sample(self, x):
        self.dc += (x - self.dc) / (1 << self.dc_shift)
        y = x - self.dc
        if self.lpf:
            y = self.filt(y)
        self.hi -= (self.hi - y) / (1 << self.pk_shift)
        if y > self.hi:
            self.hi = y
        self.lo += (y - self.lo) / (1 << self.pk_shift)
        if y < self.lo:
            self.lo = y
        thr = (self.hi + self.lo) * 0.5
        hb = 1 if y > thr else 0
        if hb != self.lh:
            err = self.ph
            if err > 0.5:
                err -= 1.0
            self.int += self.pll_i * err
            lim = self.step / 65536 * 0.02
            self.int = max(-lim, min(lim, self.int))
            self.ph -= self.pll_k * err + self.int
        self.lh = hb
        self.ph += self.step / 65536.0
        if self.ph > 0.5:
            if self.lc == 0 and self.on_bit:
                self.on_bit(hb)
            self.lc = 1
        else:
            self.lc = 0
        if self.ph >= 1.0:
            self.ph -= 1.0


# -------------------------------------------------------------------- pocsag ---

class Receiver:
    def __init__(self, dm=None):
        self.dm = dm                 # the DemodInt that owns us: its sample count
        self.at = 0                  # sample the current address word arrived at
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
        self.msgs.append((self.addr, self.func, self.err,
                          P.decode_bcd(self.cws), list(self.cws), self.at))
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
        cor, st = P.bch_decode(raw)
        ok = st < 2
        self.words += 1
        if ok:
            self.bch_ok += 1
            if st == 1:
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
            self.at = self.dm.n if self.dm else 0
            self.cws = []
            self.inmsg = True
            self.err = (st == 2)
        elif (cor >> 31) == 1:
            if self.inmsg:
                # soft erasure: an uncorrectable word is a 0 marker, its 5
                # characters become '?' and what follows stays in place
                self.cws.append(0 if st == 2 else cor)
                if st == 2:
                    self.err = True
        if self.wc >= 16:
            self.state = 0


# --- fixed point, exact mirror of the C (int32/shifts, Q14 biquad) ----------
# biquad LP @ 1500 Hz / 9600 Hz, Q14 (computed once, printed for the C header)
def biquad_q14(fc=1500.0, fs=FS_ADC):
    w = math.tan(math.pi * fc / fs)
    q = 1 / math.sqrt(2)
    a0 = 1 + w / q + w * w
    return tuple(int(round(v * 16384)) for v in
                 (w * w / a0, 2 * w * w / a0, 2 * (w * w - 1) / a0,
                  (1 - w / q + w * w) / a0))


class Slicer:
    """One hysteresis slicer over the shared front-end: its own band width
    (h = (hi-lo) >> k), its own level and its own DPLL. The level at the 0.5
    crossing is the bit, and a level transition pulls the phase toward the bit
    boundary (proportional only, as upstream).

    #12: the single chain ran k = 6 and collapsed as soon as the radio audio
    path high-passes the NRZ. Three bands over the same samples - one tighter,
    one looser - read the marginal bits differently, and the page merge keeps
    whichever read each character."""

    PLL_SHIFT = 3
    STEP = 4096

    def __init__(self, k):
        self.k = k
        self.last = 0
        self.ph = 0
        self.on_bit = None

    def step(self, y, mid, pp):
        h = pp >> self.k
        level = self.last
        if y > mid + h:
            level = 1
        elif y < mid - h:
            level = 0
        if level != self.last:
            err = self.ph
            if err > 32768:
                err -= 65536
            self.ph -= err >> self.PLL_SHIFT
            if self.ph < 0:
                self.ph += 65536
            elif self.ph >= 65536:
                self.ph -= 65536
        self.last = level
        p0 = self.ph
        self.ph += self.STEP
        if p0 < 32768 <= self.ph and self.on_bit:
            self.on_bit(level)
        if self.ph >= 65536:
            self.ph -= 65536


class DemodInt:
    """Integer demodulator transliterated to the C app. 16 samples/bit at
    19.2 kHz: phase 65536/bit, step 4096; a 16-sample boxcar (one-bit matched
    filter, running sum) feeds the symmetric peak tracker shared by every
    hysteresis slicer. The front-end runs once, whatever the slicer count."""

    B0, B1, A1, A2 = biquad_q14()
    DC_SHIFT = 9
    PK_SHIFT = 8
    STEP = 4096
    PLL_SHIFT = 3        # proportional DPLL only, as upstream: phase -= err >> 3
    BOX = 16             # 1-bit matched filter (samples per bit)
    HS = (6,)            # one slicer, as before #12
    # #12: the shipped set. k = 3 reads the level through a wide band (it flips
    # late but never on noise), k = 7 through a narrow one (it flips on the
    # drooping edge), k = 5 in between; the page merge keeps whichever read each
    # character. The sweep that chose them is in hpsweep().

    def __init__(self, ks=HS):
        self.dc = BIAS
        self.x1 = self.x2 = self.y1 = self.y2 = 0
        self.hi = 200
        self.lo = -200
        self.buf = [0] * self.BOX
        self.bi = 0
        self.bsum = 0
        self.n = 0                          # samples seen (the page merge key)
        self.sl = [Slicer(k) for k in ks]
        self.rx = [Receiver(self) for _ in ks]
        for s, r in zip(self.sl, self.rx):
            s.on_bit = r.feed

    def sample(self, x):
        self.n += 1
        self.dc += (x - self.dc) >> self.DC_SHIFT
        y = x - self.dc
        o = (self.B0 * y + self.B1 * self.x1 + self.B0 * self.x2
             - self.A1 * self.y1 - self.A2 * self.y2) >> 14
        self.x2, self.x1, self.y2, self.y1 = self.x1, y, self.y1, o
        self.bsum += o - self.buf[self.bi]
        self.buf[self.bi] = o
        self.bi = (self.bi + 1) % self.BOX
        y = self.bsum
        self.hi -= (self.hi - y) >> self.PK_SHIFT
        if y > self.hi:
            self.hi = y
        self.lo += (y - self.lo) >> self.PK_SHIFT
        if y < self.lo:
            self.lo = y
        mid = (self.hi + self.lo) >> 1
        pp = self.hi - self.lo
        for s in self.sl:
            s.step(y, mid, pp)

    def flush(self):
        for r in self.rx:
            r.flush()


def run_demod(adc, integer=True, ks=None, **kw):
    """One slicer (k = 6 by default): the pre-#12 chain, kept so the sweeps can
    compare a single chain with the merged three."""
    if not integer:
        d = Demod(**kw)
        rx = Receiver()
        d.on_bit = rx.feed
        for x in adc:
            d.sample(x)
        rx.flush()
        return rx
    d = DemodInt(ks=ks or DemodInt.HS)
    for x in adc:
        d.sample(x)
    d.flush()
    return d.rx[0]


DUP_SAMPLES = 256       # half a codeword (32 bits * 16 samples) at 19.2 kHz


def merge_pages(receivers, dup=DUP_SAMPLES):
    """#12: the same page read by several slicers, merged. A character one
    slicer lost ('?') is taken from another that read it, and the longer text
    extends the shorter. What tells the same page from another is not the text
    but where it sits: its address word arrived at the same sample, within half
    a codeword, for every slicer that read it."""
    pages = []                              # [addr, func, chars, at]
    for r in receivers:
        for (addr, func, err, bcd, cws, at) in r.msgs:
            t = list(bcd)
            for p in pages:
                if p[0] != addr or p[1] != func or abs(at - p[3]) >= dup:
                    continue
                o = p[2]
                for i in range(min(len(o), len(t))):
                    if o[i] == '?':
                        o[i] = t[i]
                o.extend(t[len(o):])
                break
            else:
                pages.append([addr, func, t, at])
    return pages


def run_demod_merged(adc, ks=(3, 5, 7)):
    """The three-slicer chain of #12, with the page merge."""
    d = DemodInt(ks=ks)
    for x in adc:
        d.sample(x)
    d.flush()
    return merge_pages(d.rx)


# -------------------------------------------------------------------- LBJ -----

LBJ_ADDRS = {1233999, 1234000, 1234001, 1234002}


def parse_lbj(addr, func, bcd, err):
    d = "下行" if func == 1 else "上行" if func == 3 else "?"
    out = {"addr": addr, "func": func, "dir": d, "err": err, "bcd": bcd}
    if addr in (1233999, 1234000) and len(bcd) >= 15:
        out["train"] = bcd[0:6].strip()
        rs = bcd[6:9].replace(" ", "0").replace("U", "0").replace("*", "0")
        out["speed"] = int(rs) if rs.isdigit() and int(rs) <= 400 else None
        pr = bcd[10:15]
        out["km"] = ("%s.%s" % (pr[0:4], pr[4])) if pr.isdigit() else None
    return out


# -------------------------------------------------------------------- tests ---

def synthetic(addr, func, bcd, mode="raw", noise=1500.0, clock_err=0.0, seed=0, ac=False, hp=0.0):
    bits = P.transmission(addr, func, bcd, preamble_bits=576)
    lead = [0.0] * int(0.05 * FS_SIM)
    wave = lead + nrz(bits) + lead
    wave = audio_path(wave, mode, hp=hp)
    if ac:
        wave = audio_path(wave, "ac")
    adc = to_adc(wave, clock_err=clock_err, noise=noise, seed=seed)
    return adc


def sweep():
    cases = [
        ("clean", dict(noise=0.0)),
        ("n1500", dict(noise=1500.0)),
        ("n3000", dict(noise=3000.0)),
        ("n4500", dict(noise=4500.0)),
        ("clk+1%", dict(noise=0.0, clock_err=+0.01)),
        ("clk-1%", dict(noise=0.0, clock_err=-0.01)),
        ("std", dict(noise=1500.0, mode="std")),
        ("ac", dict(noise=1500.0, ac=True)),
    ]
    print("%-8s | %-6s | %-9s | %-8s | %s" %
          ("case", "sync", "words(+fix)", "single", "merged"))
    for name, kw in cases:
        ok = mo = 0
        syncs = words = bad = 0
        for seed in range(3):
            s = synthetic(1234000, 3, P.lbj_short("412", 78, "01234"), seed=seed, **kw)
            rx = run_demod(s)
            hit = [m for m in rx.msgs if m[0] == 1234000 and not m[2]]
            ok += 1 if hit else 0
            syncs += rx.syncs; words += rx.bch_ok; bad += rx.bch_bad
            mo += 1 if any(p[0] == 1234000 for p in run_demod_merged(s)) else 0
        print("%-8s | %-6d | %4d(%-4d) | %d/3      | %d/3" % (name, syncs, words, bad, ok, mo))


def hpsweep():
    """The corner the model exists for: the radio audio path high-passes the
    NRZ. A single slicer dives as soon as the corner passes ~30 Hz; #12 is the
    answer - three bands over the same samples, merged page by page."""
    print("%5s | %-8s | %s" % ("hp", "single", "merged"))
    for hp in (0, 30, 50, 100, 300, 600, 1000, 1500):
        s1 = sm = 0
        for seed in range(8):
            adc = synthetic(1234000, 3, P.lbj_short("412", 78, "01234"),
                            noise=1500.0, seed=seed, hp=hp)
            rx = run_demod(adc)
            s1 += 1 if any(m[0] == 1234000 and not m[2] for m in rx.msgs) else 0
            sm += 1 if any(p[0] == 1234000 for p in run_demod_merged(adc)) else 0
        print("%5d | %d/8      | %d/8" % (hp, s1, sm))


def from_wav(path):
    """Decode a discriminator recording with the same integer demodulator the C
    app runs. 16-bit or 8-bit PCM, any sample rate; the signal is normalised to
    the model's ADC scale, so recordings at very different levels all work."""
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
    rx = run_demod(out)
    print("wav: fs=%d ch=%d n=%d sync=%d words=%d bch_ok=%d fix=%d bad=%d msgs=%d" %
          (fs, ch, n, rx.syncs, rx.words, rx.bch_ok, rx.bch_fix, rx.bch_bad, len(rx.msgs)))
    for m in rx.msgs[:30]:
        print("  [1] addr=%d func=%d err=%s bcd[%d]=%r" % (m[0], m[1], m[2], len(m[3]), m[3]))
    print("  --- merged (#12) ---")
    for p in run_demod_merged(out)[:30]:
        bcd = "".join(p[2])
        print("  [m] addr=%d func=%d bcd[%d]=%r lbj=%s" %
              (p[0], p[1], len(bcd), bcd, parse_lbj(p[0], p[1], bcd, False)))


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "hp":
        hpsweep()
    elif len(sys.argv) > 1:
        from_wav(sys.argv[1])
    else:
        sweep()
