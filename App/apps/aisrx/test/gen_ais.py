#!/usr/bin/env python3
"""Synthesize AIS demodulator test vectors for the host harness.

One message -> payload bits (pyais field offsets) -> CRC-16 (~crc LSB-first
append, verify_crc.py variant A) -> bit stuffing -> HDLC flags -> NRZI ->
GMSK-ish smoothing -> 38.4 kHz little-endian uint16 discriminator audio,
mid-scale 2048 like the BK4819 ADC.  The expected decoder output is taken
from pyais decoding the same payload (AIVDM armor), so any offset or unit
error in the C parser shows up as a diff.

    gen_ais.py <outdir>      writes <case>.u16 + <case>.exp per case
"""
import math
import os
import random
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "pyais-main"))
from pyais import decode  # noqa: E402
from extract_nmea import parse_file_simple  # noqa: E402

FS = 38400           # sample rate, matches AIS_FS
BAUD = 9600
SPS = 4              # samples per symbol
FLAG = [0, 1, 1, 1, 1, 1, 1, 0]
TRAIN = [0, 1] * 12  # 24-bit RTS preamble, real AIS bursts have it


# ---------- bit-level construction ----------

def crc_bits(bits, init=0xFFFF):
    crc = init
    for b in bits:
        mix = (crc ^ b) & 1
        crc >>= 1
        if mix:
            crc ^= 0x8408
    return crc


def stuff(bits):
    out, ones = [], 0
    for b in bits:
        out.append(b)
        if b:
            ones += 1
            if ones == 5:
                out.append(0)
                ones = 0
        else:
            ones = 0
    return out


def put(bits, off, w, val):
    for k in range(w):
        bits[off + k] = (val >> (w - 1 - k)) & 1


def sixbit(text):
    out = []
    for c in text:
        v = ord(c)
        out.append(v - 64 if v >= 64 else v)
    return out


def frame_bits(payload):
    """payload -> on-air NRZ bit list: training + flag + stuffed(payload+fcs) + flag"""
    f = (~crc_bits(payload)) & 0xFFFF
    fcs = [(f >> k) & 1 for k in range(16)]   # ~crc LSB-first (variant A)
    return TRAIN + FLAG + stuff(payload + fcs) + FLAG


# ---------- golden output via pyais ----------

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


def sentence(payload):
    body = "AIVDM,1,1,,A,%s,0" % armor(payload)
    cs = 0
    for c in body:
        cs ^= ord(c)
    return "!%s*%02X" % (body, cs)


def golden_line(payload):
    """expected harness line for one payload (raw units, like host_ais.c prints)"""
    msg = decode(sentence(payload))
    t = msg.msg_type
    if t in (1, 3):
        lat = int(round(msg.lat * 600000))
        lon = int(round(msg.lon * 600000))
        sog = int(round(msg.speed * 10))
        cog = int(round(msg.course * 10))
        return "type=%d mmsi=%d sog=%d cog=%d lat=%d lon=%d" % (
            t, msg.mmsi, sog, cog, lat, lon)
    if t == 5:
        return "type=%d mmsi=%d name=[%s] dest=[%s]" % (
            t, msg.mmsi, msg.shipname.strip(), msg.destination.strip())
    raise SystemExit("unsupported golden type %r" % t)


# ---------- message payloads ----------

def msg_type13(mmsi, nav, speed, cog, lat, lon, typ=1, repeat=0):
    bits = [0] * 168
    put(bits, 0, 6, typ)
    put(bits, 6, 2, repeat)
    put(bits, 8, 30, mmsi)
    put(bits, 38, 4, nav)
    put(bits, 42, 8, 0xE0)                  # turn rate: -32 (special)
    put(bits, 50, 10, speed)                # 0.1 kn
    put(bits, 60, 1, 1)                     # accuracy: high
    put(bits, 61, 28, lon % (1 << 28))      # 1/10000 arc-min, signed
    put(bits, 89, 27, lat % (1 << 27))
    put(bits, 116, 12, cog)                 # 0.1 deg
    put(bits, 128, 9, (cog // 10) % 512)    # heading ~ course
    put(bits, 137, 6, 33)                   # timestamp
    put(bits, 143, 2, 0)
    put(bits, 148, 1, 0)                    # raim off
    return bits


def msg_type5(mmsi, name, dest, imo=9012345, callsign="AB1CD"):
    bits = [0] * 424
    put(bits, 0, 6, 5)
    put(bits, 6, 2, 0)
    put(bits, 8, 30, mmsi)
    put(bits, 40, 30, imo)
    for i, v in enumerate(sixbit(callsign.ljust(7, "@"))):
        put(bits, 70 + 6 * i, 6, v)
    for i, v in enumerate(sixbit(name.ljust(20, "@"))):
        put(bits, 112 + 6 * i, 6, v)
    put(bits, 232, 8, 70)                   # cargo ship
    put(bits, 240, 9, 120)                  # to bow
    put(bits, 249, 9, 30)                   # to stern
    put(bits, 258, 6, 5)                    # to port
    put(bits, 264, 6, 7)                    # to starboard
    put(bits, 294, 8, 55)                   # draught 5.5 m
    for i, v in enumerate(sixbit(dest.ljust(20, "@"))):
        put(bits, 302 + 6 * i, 6, v)
    put(bits, 422, 1, 0)                    # dte ready
    return bits


# ---------- waveforms ----------

def gmsk_smooth(levels):
    """NRZ +-1 symbols -> smoothed discriminator waveform (GMSK h=0.5-ish)."""
    sig = 0.265 * SPS                       # gaussian sigma, symbols -> samples
    half = max(2, int(3 * sig))
    kern = [math.exp(-0.5 * (t / sig) ** 2) for t in range(-half, half + 1)]
    s = sum(kern)
    kern = [k / s for k in kern]
    hi = []                                 # upsample x4, smooth, decimate back
    for lv in levels:
        hi.extend([float(lv)] * (SPS * 4))
    out = []
    step = SPS * 4 // SPS
    for i in range(0, len(hi) - step + 1, step):
        acc = 0.0
        for j, k in enumerate(kern):
            p = i + j - half
            if 0 <= p < len(hi):
                acc += k * hi[p]
        out.append(acc)
    return out


def nrzi(bits, level=1):
    out = []
    for b in bits:
        if b == 0:
            level = -level
        out.append(level)
    return out


def render(bursts, gap_syms=96, noise=0.0, dc=0, baud_err=0.0, seed=1):
    """list of NRZ bit lists -> uint16 samples at FS (gap = idle carrier)."""
    rng = random.Random(seed)
    wave = []
    for i, bits in enumerate(bursts):
        if i:
            wave.extend([0.0] * (gap_syms * SPS))
        wave.extend(gmsk_smooth(nrzi(bits)))
    n = int(len(wave) / (1.0 + baud_err)) + 1
    out = []
    for k in range(n):
        t = k * (1.0 + baud_err)
        i = int(t)
        if i >= len(wave):
            break
        fr = t - i
        v = wave[i] * (1.0 - fr) + (wave[i + 1] if i + 1 < len(wave) else 0.0) * fr
        x = 2048 + dc + 1100.0 * v + rng.gauss(0.0, noise)
        out.append(max(0, min(4095, int(round(x)))))
    return out


# ---------- cases ----------

T1 = msg_type13(244690971, 8, 105, 1234, int(round(51.5010 * 600000)),
                int(round(-0.1419 * 600000)))
T3 = msg_type13(273445000, 0, 0, 0, int(round(59.9430 * 600000)),
                int(round(30.3150 * 600000)), typ=3)
T5 = msg_type5(215678900, "TEST SHIP", "ST PETERSBURG")

# corrupt variant: same length as T1, flips payload bits after CRC computed
T1BAD = list(T1)


def corrupt(bits):
    b = frame_bits(bits)
    # flip 3 bits inside the frame body (well past the opening flag)
    for p in (40, 90, 140):
        b[p] ^= 1
    return b


def write_case(name, samples, expected):
    with open(os.path.join(OUT, name + ".u16"), "wb") as f:
        f.write(struct.pack("<%dH" % len(samples), *samples))
    with open(os.path.join(OUT, name + ".exp"), "w", newline="\n") as f:
        for line in expected:
            f.write(line + "\n")
        f.write("total=%d\n" % (len(expected)))


def load_real(path):
    """real_payloads.txt -> list of payload bit lists (real AISHub traffic)."""
    out = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            typ, hx = line.split()
            bits = []
            for c in hx:
                v = int(c, 16)
                for k in range(3, -1, -1):
                    bits.append((v >> k) & 1)
            out.append((int(typ), bits))
    return out


def main():
    global OUT
    OUT = sys.argv[1] if len(sys.argv) > 1 else "out"
    os.makedirs(OUT, exist_ok=True)

    write_case("clean", render([frame_bits(T1)], noise=0),
               [golden_line(T1)])
    write_case("noise", render([frame_bits(T1)], noise=60, seed=2),
               [golden_line(T1)])
    write_case("dc", render([frame_bits(T1)], noise=20, dc=450, seed=3),
               [golden_line(T1)])
    write_case("clock", render([frame_bits(T1)], noise=20, baud_err=0.002, seed=4),
               [golden_line(T1)])
    write_case("multi", render([frame_bits(T1), frame_bits(T5), frame_bits(T3)],
                               noise=40, seed=5),
               [golden_line(T1), golden_line(T5), golden_line(T3)])
    write_case("badcrc", render([corrupt(T1BAD), frame_bits(T3)], noise=20, seed=6),
               [golden_line(T3)])
    write_case("type5", render([frame_bits(T5)], noise=30, seed=7),
               [golden_line(T5)])

    # real AISHub payloads through the same RF chain (only if baked file exists)
    rp = os.path.join(HERE, "real_payloads.txt")
    if os.path.exists(rp):
        real = load_real(rp)
        write_case("real",
                   render([frame_bits(bits) for _, bits in real], noise=35, seed=8),
                   [golden_line(bits) for _, bits in real])

    print("gen_ais: wrote cases to", OUT)


if __name__ == "__main__":
    main()
