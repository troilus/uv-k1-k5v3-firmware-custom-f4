#!/usr/bin/env python3
"""Fixed-point (integer) implementation of the acarsdec coherent MSK demod,
exactly as written in C for the overlay app (acr_app.c, dem_sample/acr_byte).

  FS = 19200, tones 1200/2400, 2400 baud -> 8 samples/bit
  complex LO at 1800 Hz   (1800/19200 = 3/32 turn/sample)
  matched filter cos(2*pi*(i-8)/32), FLEN=17 taps, evaluated once per bit
  ONE 64-entry Q12 cos table serves as LO and as matched filter.
  bit clock is derived from the VCO phase (no separate DPLL), bang-bang carrier
  PLL nudges the VCO frequency (phase acquisition).

  python fx.py [recording.wav] [channel]

Any PCM16 WAV works; by default it reads acarsdec's reference recording:

  curl -L -o test/acars_test.wav \
      https://cdn.jsdelivr.net/gh/szpajder/acarsdec@master/test.wav

which holds 4 channels x 12500 Hz, one ACARS frequency each, and must decode
7 messages (F-GTAE/H1, PH-BXR/5V, LN-DYY/Q0, G-DBCKW/_, G-DBCK/Q0, LN-DYY/Q0).
"""
import math
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from prototype import crc16, parse, SYN
from wav import wav_info, load

FS = 19200
FLEN = FS // 1200 + 1                     # 17
T = [int(round(4096 * math.cos(2 * math.pi * i / 64))) for i in range(64)]
STEP = round(1800.0 / FS * 4096)          # 384
CLKTHR = round(0.75 * 4096)               # 3072  -> fires every 8 samples
PLLK = int(os.environ.get('PLLK', '3'))
PLLMAX = int(os.environ.get('PLLMAX', '1'))   # 1 = what the C does
PLLMODE = os.environ.get('PLLMODE', 'bb')     # 'bb' = what the C does
# Real ACARS audio. Default: acarsdec's test.wav, the reference recording this
# demodulator was validated against (see the module docstring for the URL).
WAV = os.environ.get('ACARS_WAV', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'acars_test.wav'))


class FxDemod:
    def __init__(self, emit):
        self.acc = 0
        self.clk = 0
        self.corr = 0
        self.ring = [0] * (2 * FLEN)
        self.idx = 0
        self.msks = 0
        self.dc = 0
        self.level = 0
        self.emit = emit
        self.nbit = 0

    # ---- one sample in -------------------------------------------------
    def push(self, x):
        step = STEP + self.corr
        self.acc = (self.acc + step) & 4095

        # DC removal (slow, ~1 kHz corner) -- the ADC bias / carrier level
        self.dc += (x - self.dc) >> 8
        y = x - self.dc

        i = (self.acc >> 6) & 63
        r = (y * T[i]) >> 12                  #  cos(p)
        q = (y * T[(i + 16) & 63]) >> 12      # -sin(p)

        self.ring[2 * self.idx] = r
        self.ring[2 * self.idx + 1] = q
        self.idx += 1
        if self.idx == FLEN:
            self.idx = 0

        self.clk += step
        if self.clk >= CLKTHR:
            self.clk -= CLKTHR
            self._bit()

    def _bit(self):
        vr = vi = 0
        k = self.idx                            # oldest sample
        for j in range(FLEN):
            hv = T[(2 * (j - FLEN // 2)) & 63]
            vr += hv * self.ring[2 * k]
            vi += hv * self.ring[2 * k + 1]
            k += 1
            if k == FLEN:
                k = 0
        self.level = (abs(vr) + abs(vi)) >> 10

        s = self.msks & 3
        if s == 0:
            self.emit(1 if vr > 0 else 0)
            dphi = vi if vr >= 0 else -vi
        elif s == 1:
            self.emit(1 if vi > 0 else 0)
            dphi = -vr if vi >= 0 else vr
        elif s == 2:
            self.emit(1 if -vr > 0 else 0)
            dphi = vi if vr >= 0 else -vi
        else:
            self.emit(1 if -vi > 0 else 0)
            dphi = -vr if vi >= 0 else vr
        self.msks += 1

        # normalise by |v| so the loop gain is signal independent
        m = ((abs(vr) + abs(vi)) >> 12) + 1
        if PLLMODE == 'bb':
            d = PLLMAX if dphi > 0 else (-PLLMAX if dphi < 0 else 0)
        else:
            d = (dphi >> 12) * PLLK // m
        if d > PLLMAX:
            d = PLLMAX
        elif d < -PLLMAX:
            d = -PLLMAX
        self.corr = d


class Chan:
    """FxDemod + framing; also honours the MskS^2 polarity flip"""

    def __init__(self):
        self.frames = []
        self.crc_err = 0
        self.txt = bytearray()
        self.st = 0
        self.acc = 0
        self.n = 1
        self.inv = 0
        self.c0 = 0
        self.dem = FxDemod(self.bit)
        self.nbytes = 0

    def bit(self, b):
        self.acc >>= 1
        if b:
            self.acc |= 0x80
        self.n -= 1
        if self.n > 0:
            return
        r = self.acc
        if self.st == 0:
            if r == SYN:
                self.inv = 0
                self.st, self.n = 1, 8
            elif r == (0xFF ^ SYN):
                self.dem.msks ^= 2
                self.st, self.n = 1, 8
            else:
                self.n = 1
            return
        if self.inv:
            r ^= 0xFF
        if self.st == 1:
            if r == SYN:
                self.st, self.n = 2, 8
            else:
                self.st, self.n = 0, 1
        elif self.st == 2:
            if r == 0x01:
                self.txt = bytearray()
                self.st, self.n = 3, 8
            else:
                self.st, self.n = 0, 1
        elif self.st == 3:
            self.txt.append(r)
            if r in (0x83, 0x97):
                self.st, self.n = 4, 8
            elif len(self.txt) > 240:
                self.st, self.n = 0, 1
            else:
                self.n = 8
        elif self.st == 4:
            self.c0 = r
            self.st, self.n = 5, 8
        else:
            self.nbytes += 1
            if crc16(list(self.txt) + [self.c0, r]) == 0:
                self.frames.append(bytes(self.txt))
            else:
                self.crc_err += 1
            self.st, self.n = 0, 1


def resample(v, src, dst=FS, shift=0.0):
    """linear interpolation; the demodulator needs 19200 Hz (8 samples/bit).
    `shift` adds a fractional input-sample delay, to model an arbitrary ADC
    start phase."""
    step = src / dst
    n = int((len(v) - shift) / step)
    out = [0] * n
    for i in range(n):
        p = i * step + shift
        j = int(p)
        f = p - j
        a = v[j] if j < len(v) else 0
        b = v[j + 1] if j + 1 < len(v) else a
        out[i] = int(round(a + (b - a) * f))
    return out


def decode(samples, scale=1):
    """16-bit WAV (+-32768) -> the ADC's +-2048 range, then dem+frame"""
    ch = Chan()
    for x in samples:
        ch.dem.push(x // scale if scale > 1 else x)
    return ch


def run_real(path, only=None):
    rate, nch = wav_info(path)
    shift = float(os.environ.get('ACARS_SHIFT', '0'))
    print('%s: %d Hz, %d channel(s), shift %.3f' % (path, rate, nch, shift))
    total = 0
    for c in range(nch):
        if only is not None and c != only:
            continue
        ch = decode(resample(load(path, c), rate, shift=shift), 16)
        total += len(ch.frames)
        print('  ch%d  frames=%d crc_err=%d' % (c, len(ch.frames), ch.crc_err))
        for f in ch.frames:
            p = parse(f)
            print('       ', p if p else f[:100])
    print('  total %d message(s)' % total)


if __name__ == '__main__':
    arg = sys.argv[1] if len(sys.argv) > 1 else WAV
    chan = int(sys.argv[2]) if len(sys.argv) > 2 else None
    if not os.path.exists(arg):
        print('missing %s\nfetch the reference recording with:\n'
              '  curl -L -o "%s" '
              'https://cdn.jsdelivr.net/gh/szpajder/acarsdec@master/test.wav'
              % (arg, arg))
        sys.exit(1)
    run_real(arg, chan)
