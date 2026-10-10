#!/usr/bin/env python3
"""Faithful Python transcription of acarsdec's demodMSK() + decodeAcars().

Purpose: establish ground truth on the REAL test.wav -- if this decodes and my
demods don't, the demod algorithm is what has to change.

    python ref_acarsdec.py [channel]
"""
import struct
import math
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from prototype import crc16, parse

WAV = r'C:\Users\wangli\AppData\Local\Temp\acars_test.wav'
INTRATE = 12500

SYN, SOH, STX, ETX, ETB, DLE = 0x16, 0x01, 0x02, 0x83, 0x97, 0x7F
PLLC = 3.8e-3
FLEN = INTRATE // 1200 + 1          # 11
H = [math.cos(2 * math.pi * 600.0 / INTRATE * (i - FLEN // 2)) for i in range(FLEN)]
H = H + H                                 # C: h[2*FLEN], h[i]==h[i+FLEN]


def numbits(x):
    return bin(x).count('1')


class RefChannel:
    """demodMSK + decodeAcars, transcribed line by line"""

    def __init__(self):
        self.phi = 0.0
        self.clk = 0.0
        self.df = 0.0
        self.MskS = 0
        self.Msklvl = 0.0
        self.inb = [0.0, 0.0] * FLEN          # (re, im) pairs
        self.idx = 0
        self.outbits = 0
        self.nbits = 8
        self.state = 'WSYN'
        self.frames = []
        self.txt = bytearray()
        self.crc = [0, 0]
        self.nbytes = 0
        self.len_ = 0
        self.err = 0
        self.dphi = 0.0

    # ---- framing (decodeAcars) -------------------------------------
    def reset(self):
        self.state = 'WSYN'
        self.df = 0.0
        self.nbits = 1

    def putbyte(self, r):
        st = self.state
        if st == 'WSYN':
            if r == SYN:
                self.state, self.nbits = 'SYN2', 8
                return
            if r == (0xFF ^ SYN):
                self.MskS ^= 2
                self.state, self.nbits = 'SYN2', 8
                return
            self.nbits = 1
            return
        if st == 'SYN2':
            if r == SYN:
                self.state, self.nbits = 'SOH1', 8
                return
            if r == (0xFF ^ SYN):
                self.MskS ^= 2
                self.nbits = 8
                return
            self.reset()
            return
        if st == 'SOH1':
            if r == SOH:
                self.state, self.nbits = 'TXT', 8
                self.txt = bytearray()
                self.len_ = 0
                self.err = 0
                self.Msklvl = 0.0
                return
            self.reset()
            return
        if st == 'TXT':
            self.txt.append(r)
            self.len_ += 1
            if (numbits(r) & 1) == 0:
                self.err += 1
                if self.err > 4:
                    self.reset()
                    return
            if r in (ETX, ETB):
                self.state, self.nbits = 'CRC1', 8
                return
            if self.len_ > 240:
                self.reset()
                return
            self.nbits = 8
            return
        if st == 'CRC1':
            self.crc[0] = r
            self.state, self.nbits = 'CRC2', 8
            return
        if st == 'CRC2':
            self.crc[1] = r
            self.state, self.nbits = 'END', 8
            self.nbytes += 1
            if self.len_ >= 13 and crc16(list(self.txt) + self.crc) == 0:
                self.frames.append(bytes(self.txt))
            return
        # END
        self.reset()
        self.nbits = 8

    def putbit(self, v):
        self.outbits >>= 1
        if v > 0:
            self.outbits |= 0x80
        self.nbits -= 1
        if self.nbits <= 0:
            self.putbyte(self.outbits)

    # ---- demodMSK ---------------------------------------------------
    def push(self, x):
        s = 2 * math.pi * 1800.0 / INTRATE + self.df
        p = self.phi + s
        if p >= 2 * math.pi:
            p -= 2 * math.pi
        c, sn = math.cos(p), -math.sin(p)       # exp(-j p)
        i = self.idx
        self.inb[2 * i] = x * c
        self.inb[2 * i + 1] = x * sn
        self.idx = (i + 1) % FLEN

        self.clk += s
        if self.clk >= 3 * math.pi / 2.0:
            self.clk -= 3 * math.pi / 2.0
            # matched filter, exactly as in the C (o starts at FLEN-idx)
            o = FLEN - self.idx
            vr = vi = 0.0
            for j in range(FLEN):
                hv = H[o]
                vr += hv * self.inb[2 * j]
                vi += hv * self.inb[2 * j + 1]
                o += 1
            lvl = math.hypot(vr, vi)
            vr /= lvl + 1e-6
            vi /= lvl + 1e-6
            self.Msklvl = 0.99 * self.Msklvl + 0.01 * lvl / 5.2
            k = self.MskS & 3
            if k == 0:
                vo = vr
                self.putbit(vo)
                dphi = vi if vo >= 0 else -vi
            elif k == 1:
                vo = vi
                self.putbit(vo)
                dphi = -vr if vo >= 0 else vr
            elif k == 2:
                vo = vr
                self.putbit(-vo)
                dphi = vi if vo >= 0 else -vi
            else:
                vo = vi
                self.putbit(-vo)
                dphi = -vr if vo >= 0 else vr
            self.MskS += 1
            self.df = PLLC * dphi
        self.phi = p


def load(path, want):
    from wav import load as _load
    return _load(path, want)


def main():
    only = int(sys.argv[1]) if len(sys.argv) > 1 else None
    for c in range(4):
        if only is not None and c != only:
            continue
        ch = RefChannel()
        for x in load(WAV, c):
            ch.push(x)
        print('ch%d  bytes seen=%d  crc-passed=%d  state=%s' %
              (c, ch.nbytes, len(ch.frames), ch.state))
        for f in ch.frames:
            p = parse(f)
            print('    ', p if p else f[:100])


if __name__ == '__main__':
    main()
