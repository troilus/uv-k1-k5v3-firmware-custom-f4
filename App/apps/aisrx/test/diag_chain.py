#!/usr/bin/env python3
"""Diagnostic: find where the demod chain breaks. Informational only -- NOT part
of run_tests.sh (that stays 8/8).

Two sweeps over the same T1 frame the main suite uses:

  1. BANDWIDTH  -- low-pass the rendered 38.4 kHz discriminator audio to a
     cutoff fc (windowed-sinc FIR, linear phase). Mimics a voice-band FM
     discriminator filter that is too narrow for 9600-baud GMSK (ISI).
     If the chain dies at a cutoff the BK4819 plausibly has (~3-5 kHz),
     bandwidth/ISI is the on-air killer.

  2. BAUD ERR   -- stretch/compress the waveform (baud_err) to mimic a sample
     rate or symbol-rate mismatch the free-running DPLL must track. Finds the
     rate-error tolerance. If it dies at ~1%, and the real sample rate is off
     by more than that, timing is the killer.

    CC=gcc PY=python3 ./diag_chain.py
"""
import math
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_ais as G  # noqa: E402

OUT = os.path.join(HERE, "out")
BIN = os.path.join(OUT, "host_ais")
FS = G.FS


# ---------- linear-phase windowed-sinc low-pass ----------

def lowpass(x, fc, ntaps=255):
    """Windowed-sinc FIR low-pass at fc Hz, unit DC gain, 'same' alignment."""
    fc_n = fc / float(FS)                      # cycles per sample
    half = (ntaps - 1) // 2
    h = []
    for n in range(ntaps):
        m = n - half
        if m == 0:
            v = 2.0 * fc_n
        else:
            v = math.sin(2.0 * math.pi * fc_n * m) / (math.pi * m)
        w = 0.54 - 0.46 * math.cos(2.0 * math.pi * n / (ntaps - 1))  # Hamming
        h.append(v * w)
    s = sum(h)
    h = [v / s for v in h]                     # exact DC gain 1 (keeps 2048)

    y = [0.0] * len(x)
    for i in range(len(x)):
        acc = 0.0
        for j in range(ntaps):
            p = i - j + half
            if 0 <= p < len(x):
                acc += h[j] * x[p]
        y[i] = acc
    return y


def write_case(name, samples, expected):
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, name + ".u16"), "wb") as f:
        f.write(struct.pack("<%dH" % len(samples), *samples))
    with open(os.path.join(OUT, name + ".exp"), "w", newline="\n") as f:
        for line in expected:
            f.write(line + "\n")
        f.write("total=%d\n" % (len(expected)))


def run(name):
    u16 = os.path.join(OUT, name + ".u16")
    exp = os.path.join(OUT, name + ".exp")
    got = os.path.join(OUT, name + ".got")
    with open(got, "w", newline="\n") as g:
        subprocess.run([BIN, u16], stdout=g, stderr=subprocess.DEVNULL)
    with open(exp) as a, open(got) as b:
        want = a.read().splitlines()
        have = b.read().splitlines()
    return want == have, len([l for l in have if l.startswith("type=")])


def render_float(name):
    """render() but keep the exact samples the clean/noise cases use."""
    return list(G.render([G.frame_bits(G.T1)], **name))


def main():
    # compile the harness once
    subprocess.run(
        ["gcc", "-O2", "-Wall", "-Wextra", "-o", BIN,
         os.path.join(HERE, "host_ais.c"), os.path.join(HERE, "..", "ais_decode.c")],
        check=True)

    gold = [G.golden_line(G.T1)]
    print("=" * 52)
    print("0) CONTROL (no filter, the noise level used below)")
    print("=" * 52)
    for nsd in (0, 20, 40, 60):
        s = render_float(dict(noise=nsd, seed=2))
        n = "ctl_%d" % nsd
        write_case(n, s, gold)
        ok, dec = run(n)
        print("  noise=%-3d  dec=%d  %s" % (nsd, dec, "PASS" if ok else "FAIL"))

    print("=" * 52)
    print("1) BANDWIDTH sweep  (voice-band AF low-pass -> ISI)")
    print("=" * 52)
    base = render_float(dict(noise=20, seed=2))     # light noise, control passes
    print("%-12s %-6s %s" % ("cutoff", "dec", "result"))
    for fc in (16000, 12000, 10000, 9000, 8000, 7500, 7000, 6500, 6000,
               5500, 5000, 4000, 3000, 2000):
        filt = lowpass(base, fc)
        clamped = [max(0, min(4095, int(round(v)))) for v in filt]
        n = "bw_%d" % fc
        write_case(n, clamped, gold)
        ok, dec = run(n)
        print("%-8s Hz %-6d %s" % (fc, dec, "PASS" if ok else "FAIL"))

    print("=" * 52)
    print("2) BAUD/SAMPLE-RATE ERROR sweep  (DPLL must track rate mismatch)")
    print("=" * 52)
    print("%-12s %-6s %s" % ("err", "dec", "result"))
    for err in (0.0, 0.003, 0.006, 0.010, 0.015, 0.020, 0.030, 0.050):
        samp = render_float(dict(noise=30, baud_err=err, seed=4))
        n = "err_%.3f" % err
        write_case(n, samp, gold)
        ok, dec = run(n)
        print("%-10.1f%% %-6d %s" % (err * 100, dec, "PASS" if ok else "FAIL"))

    print("=" * 52)
    print("baseline (clean, no lpf/err): ", end="")
    samp = render_float(dict(noise=0, seed=1))
    write_case("diag_base", samp, gold)
    ok, dec = run("diag_base")
    print("%d frames, %s" % (dec, "PASS" if ok else "FAIL"))


if __name__ == "__main__":
    main()
