#!/usr/bin/env python3
"""Minimal PCM16 WAV reader shared by the ACARS tests (no numpy/soundfile)."""
import struct


def wav_info(path):
    """(sample rate, channel count)"""
    d = open(path, 'rb').read(4096)
    if d[0:4] != b'RIFF' or d[8:12] != b'WAVE':
        raise SystemExit('%s: not a RIFF/WAVE file' % path)
    pos = 12
    while pos + 24 <= len(d):
        cid = d[pos:pos + 4]
        sz = struct.unpack('<I', d[pos + 4:pos + 8])[0]
        if cid == b'fmt ':
            ch = struct.unpack('<H', d[pos + 10:pos + 12])[0]
            rate = struct.unpack('<I', d[pos + 12:pos + 16])[0]
            bits = struct.unpack('<H', d[pos + 22:pos + 24])[0]
            if bits != 16:
                raise SystemExit('%s: only 16-bit PCM is supported' % path)
            return rate, ch
        pos += 8 + sz + (sz & 1)
    raise SystemExit('%s: no fmt chunk' % path)


def load(path, want):
    """channel `want` as a list of int16 samples"""
    d = open(path, 'rb').read()
    ch = 0
    pos = 12
    while pos + 8 <= len(d):
        cid = d[pos:pos + 4]
        sz = struct.unpack('<I', d[pos + 4:pos + 8])[0]
        if cid == b'fmt ':
            ch = struct.unpack('<H', d[pos + 10:pos + 12])[0]
        elif cid == b'data':
            if want >= ch:
                return []
            n = min(sz, len(d) - pos - 8) // (2 * ch)
            step = 2 * ch
            off = pos + 8 + 2 * want
            return [struct.unpack('<h', d[off + i * step:off + i * step + 2])[0]
                    for i in range(n)]
        pos += 8 + sz + (sz & 1)
    raise SystemExit('%s: no data chunk' % path)
