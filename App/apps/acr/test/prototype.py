#!/usr/bin/env python3
# ACARS RX demodulator prototype - decides WHICH algorithm goes into the C app.
#
# Synthesises real ACARS MSK (2400 baud, 1200/2400 Hz tones, continuous phase,
# odd parity, LSB first, CRC-16/KERMIT) at 19.2 kHz, runs candidate demods over
# it, and measures bit errors + full-frame decode rate over SNR and timing
# offsets.  The winner is transcribed into acr_app.c.
#
#   python prototype.py
import math
import random

FS = 19200
SPS = 8                      # samples per symbol (2400 baud)
SYN, SOH, STX, ETX, ETB = 0x16, 0x01, 0x02, 0x83, 0x97


# --------------------------------------------------------------------------
# frame construction (transmitted form: 7 data bits + odd parity in bit 7)
# --------------------------------------------------------------------------
def odd_par(b):
    b &= 0x7F
    return b if bin(b).count("1") % 2 else b | 0x80


def crc16(data):
    crc = 0
    for c in data:
        crc ^= c
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc


def build_frame(text, flight="A6-EDY ", label="SA", mode="2", block="A"):
    """on-air burst: SYN SYN SOH + txt + crc; the CRC covers txt + crc only."""
    body = bytearray([SYN, SYN, SOH])
    body.append(odd_par(ord(mode)))
    body.append(odd_par(STX))
    for ch in flight[:7].ljust(7):
        body.append(odd_par(ord(ch)))
    for ch in label[:2].ljust(2):
        body.append(odd_par(ord(ch)))
    body.append(odd_par(ord(block)))
    body.append(odd_par(STX))
    for ch in text:
        body.append(odd_par(ord(ch)))
    body.append(odd_par(ETX))
    c = crc16(body[3:])
    body.append(c & 0xFF)
    body.append(c >> 8)
    return bytes(body)


def frame_bits(body, pre=32, tail=16):
    """preamble (alternating, starts with 0) + bytes LSB first + tail."""
    bits = [i & 1 for i in range(pre)]
    for b in body:
        bits += [(b >> i) & 1 for i in range(8)]
    bits += [0] * tail
    return bits


def synth(bits, amp=400.0, sigma=20.0, dc=2048.0, phase0=0.0, seed=1):
    rnd = random.Random(seed)
    out = []
    ph = phase0
    for b in bits:
        f = 2400.0 if b else 1200.0
        for _ in range(SPS):
            out.append(int(round(dc + amp * math.cos(ph) + rnd.gauss(0.0, sigma))))
            ph += 2.0 * math.pi * f / FS
    return out


# --------------------------------------------------------------------------
# receiver side: byte assembly + frame state machine + CRC (shared by all)
# --------------------------------------------------------------------------
class Framing:
    """LSB-first assembly, SYN/SYN/SOH sync with polarity recovery, ETX end."""

    def __init__(self):
        self.st = 0            # 0 WSYN 1 SYN2 2 SOH 3 TXT 4 CRC1 5 CRC2
        self.acc = 0
        self.n = 1
        self.inv = 0
        self.c0 = 0
        self.txt = bytearray()
        self.frames = []
        self.crc_err = 0

    def bit(self, b):
        self.acc >>= 1
        if b:
            self.acc |= 0x80
        self.n -= 1
        if self.n > 0:
            return
        r = self.acc
        if self.st == 0:                       # sync search on the raw byte
            if r == SYN:
                self.inv = 0
                self.st, self.n = 1, 8
            elif r == (0xFF ^ SYN):
                self.inv = 1
                self.st, self.n = 1, 8
            else:
                self.n = 1
            return
        if self.inv:                           # polarity applies to everything
            r ^= 0xFF
        if self.st == 1:
            if r == SYN:
                self.st, self.n = 2, 8
            else:
                self.st, self.n = 0, 1
        elif self.st == 2:
            if r == SOH:
                self.txt = bytearray()
                self.st, self.n = 3, 8
            else:
                self.st, self.n = 0, 1
        elif self.st == 3:
            self.txt.append(r)
            if r == ETX or r == ETB:
                self.st, self.n = 4, 8
            elif len(self.txt) > 240:
                self.st, self.n = 0, 1
            else:
                self.n = 8
        elif self.st == 4:
            self.c0 = r
            self.st, self.n = 5, 8
        else:
            if crc16(list(self.txt) + [self.c0, r]) == 0:
                self.frames.append(bytes(self.txt))
            else:
                self.crc_err += 1
            self.st, self.n = 0, 1


def parse(txt):
    """mode / flight / label / text out of a validated frame."""
    if len(txt) < 14:
        return None
    t = bytes(x & 0x7F for x in txt)
    return dict(mode=chr(t[0]), flight=t[2:9].decode("ascii", "replace"),
                label=t[9:11].decode("ascii", "replace"),
                text=t[13:-1].decode("ascii", "replace"))


# --------------------------------------------------------------------------
# common DPLL (the LBJ loop: pull ph=0 onto the level change, sample at 32768)
# --------------------------------------------------------------------------
PLL_STEP, PLL_SHIFT, INT_SHIFT, INT_MAX, PK_SHIFT = 8192, 3, 9, 160, 8


class Dpll:
    def __init__(self, emit, scale=None):
        self.ph = 0
        self.last = 0
        self.iint = 0
        self.scale = scale
        self.hi = 1.0
        self.lo = -1.0
        self.emit = emit

    def level(self, m):
        """hysteresis slice of the metric -> 0/1, steering the DPLL on change."""
        if self.scale is not None:
            m = m * self.scale
        m = float(m)
        self.hi -= (self.hi - m) / 256.0
        if m > self.hi:
            self.hi = m
        self.lo += (m - self.lo) / 256.0
        if m < self.lo:
            self.lo = m
        mid = (self.hi + self.lo) * 0.5
        h = max(1.0, (self.hi - self.lo) / 64.0)
        lv = self.last
        if m > mid + h:
            lv = 1
        elif m < mid - h:
            lv = 0
        if lv != self.last:
            err = self.ph if self.ph <= 32768 else self.ph - 65536
            self.iint += err >> INT_SHIFT
            if self.iint > INT_MAX:
                self.iint = INT_MAX
            elif self.iint < -INT_MAX:
                self.iint = -INT_MAX
            self.ph -= (err >> PLL_SHIFT) + self.iint
            if self.ph < 0:
                self.ph += 65536
            elif self.ph >= 65536:
                self.ph -= 65536
        self.last = lv
        p0 = self.ph
        self.ph += PLL_STEP
        if p0 < 32768 and self.ph >= 32768:
            self.emit(lv)
        if self.ph >= 65536:
            self.ph -= 65536


# --------------------------------------------------------------------------
# candidate demods: push(x) once per sample
# --------------------------------------------------------------------------
class DemA:
    """A: real delay-multiply  p = y[n]*y[n-8], 8-sample boxcar."""

    name = "A delay-mult"

    def __init__(self, emit):
        self.dc = 2048
        self.dl = [0] * SPS
        self.box = [0] * SPS
        self.i = 0
        self.s = 0
        self.d = Dpll(emit)

    def push(self, x):
        self.dc += (x - self.dc) >> 7
        y = x - self.dc
        y8 = self.dl[self.i]
        self.dl[self.i] = y
        p = y * y8
        self.s += p - self.box[self.i]
        self.box[self.i] = p
        self.i = (self.i + 1) & (SPS - 1)
        self.d.level(self.s)


class DemC:
    """C: sliding two-tone correlator, metric = |R24|^2 - |R12|^2."""

    name = "C 2-tone"
    T24 = [(math.cos(2 * math.pi * 2400 * i / FS), -math.sin(2 * math.pi * 2400 * i / FS))
           for i in range(SPS)]
    T12 = [(math.cos(2 * math.pi * 1200 * i / FS), -math.sin(2 * math.pi * 1200 * i / FS))
           for i in range(SPS)]

    def __init__(self, emit):
        self.dc = 2048
        self.dl = [0] * SPS
        self.i = 0
        self.d = Dpll(emit, scale=1e-6)

    def push(self, x):
        self.dc += (x - self.dc) >> 7
        y = x - self.dc
        self.dl[self.i] = y
        self.i = (self.i + 1) & (SPS - 1)
        w = self.dl                       # w[oldest] .. w[newest]
        r24i = r24q = r12i = r12q = 0.0
        for k in range(SPS):
            v = w[k]
            r24i += v * self.T24[k][0]
            r24q += v * self.T24[k][1]
            r12i += v * self.T12[k][0]
            r12q += v * self.T12[k][1]
        m = (r24i * r24i + r24q * r24q) - (r12i * r12i + r12q * r12q)
        self.d.level(m)


class DemB:
    """B: complex LO at 1800 Hz + 8-tap lowpass (image reject), metric =
       Im(z[n] * conj(z[n-8])) - a full +/-|z|^2 swing, no window artefacts."""

    name = "B LO1800"

    def __init__(self, emit, taps=8):
        self.dc = 2048
        self.idx = 0                 # LO phase: (3n) mod 32  -> 1800 Hz
        self.n = 0
        self.li = [0.0] * taps
        self.lq = [0.0] * taps
        self.taps = taps
        self.zi = [0.0] * SPS
        self.zq = [0.0] * SPS
        self.i = 0
        self.s = 0
        self.box = [0.0] * SPS
        self.b = 0
        self.d = Dpll(emit, scale=1e-3)

    def push(self, x):
        self.dc += (x - self.dc) >> 7
        y = x - self.dc
        th = math.pi * self.idx / 16.0        # 2*pi*(3n)/32
        self.idx = (self.idx + 3) & 31
        ci, si = math.cos(th), -math.sin(th)
        self.li.append(y * ci)
        self.li.pop(0)
        self.lq.append(y * si)
        self.lq.pop(0)
        zi = sum(self.li) / self.taps
        zq = sum(self.lq) / self.taps
        zi8, zq8 = self.zi[self.i], self.zq[self.i]
        self.zi[self.i], self.zq[self.i] = zi, zq
        self.i = (self.i + 1) & (SPS - 1)
        p = zi * zq8 - zq * zi8        # Im(z * conj(z8))
        self.s += p - self.box[self.b]
        self.box[self.b] = p
        self.b = (self.b + 1) & (SPS - 1)
        self.d.level(self.s)


# --------------------------------------------------------------------------
# test harness
# --------------------------------------------------------------------------
def _pack(bits):
    b = bytearray((len(bits) + 7) // 8)
    for i, v in enumerate(bits):
        if v:
            b[i >> 3] |= 1 << (i & 7)
    return int.from_bytes(bytes(b), "little")


def run(dem_cls, samples, bits, **kw):
    got = []
    fr = Framing()
    dem = dem_cls(lambda lv: (got.append(lv), fr.bit(lv)), **kw)
    for x in samples:
        dem.push(x)
    # align the emitted bit stream against the transmitted one (allow a
    # delay of up to 64 symbols in either direction)
    n = len(bits)
    if not got:
        return 1.0, fr
    B, G = _pack(bits), _pack([0] * 64 + got + [0] * 64)
    mask = (1 << n) - 1
    best = None
    for off in range(0, len(got) + 128 - n + 1):
        e = (((G >> off) ^ B) & mask).bit_count()
        e = min(e, n - e)          # polarity is free (FSM recovers it)
        if best is None or e < best[1]:
            best = (off, e)
    return best[1] / n, fr


def main():
    text = "REQUEST ALTN ARPT EDDF"
    body = build_frame(text)
    bits = frame_bits(body)
    print(f"frame: {len(body)} bytes, {len(bits)} bits, body={body[:6].hex()} ...")
    fr = Framing()
    for b in bits:
        fr.bit(b)
    assert fr.frames and parse(fr.frames[0])["text"] == text, "reference FSM broken"
    print(f"reference (noiseless FSM): {parse(fr.frames[0])}")

    plan = [
        (400, 10), (400, 20), (400, 40), (400, 80),
        (150, 20), (150, 40), (80, 20), (80, 40),
    ]
    for cls in (DemA, DemC, DemB):
        print(f"\n=== {cls.name} ===")
        print("  amp/sig |  BER (4 phase offsets)            | frames ok / crc err")
        for amp, sig in plan:
            bers, ok, cerr = [], 0, 0
            for seed in range(4):
                ph = seed * math.pi / 4
                s = synth(bits, amp=amp, sigma=sig, phase0=ph, seed=seed)
                ber, fr = run(cls, s, bits)
                bers.append(ber)
                ok += len(fr.frames)
                cerr += fr.crc_err
            flag = "  <== ok" if max(bers) == 0 and ok == 4 else ""
            print(f"  {amp:4d}/{sig:3d} | " +
                  " ".join(f"{b * 100:6.2f}%" for b in bers) +
                  f" |  {ok}/4  (crc fail {cerr}){flag}")


if __name__ == "__main__":
    main()
