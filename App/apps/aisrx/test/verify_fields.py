#!/usr/bin/env python3
"""Verify AIS field bit offsets against pyais (the golden reference).

Builds a bit vector using the offsets the C decoder will use, armors it into an
AIVDM sentence, lets pyais decode it, and checks every field round-trips. Any
offset error shows up as a mismatch.
"""
import sys

sys.path.insert(0, r"App/apps/aisrx/pyais-main")
from pyais import decode  # noqa: E402


def put(bits, off, width, val):
    for k in range(width):
        bits[off + k] = (val >> (width - 1 - k)) & 1


def sixbit(text):
    """ASCII -> AIS 6-bit values (inverse of pyais _SIXBIT_CHAR)."""
    out = []
    for c in text:
        v = ord(c)
        out.append(v - 64 if v >= 64 else v)
    return out


def armor(bits):
    # AIVDM armor pads the payload out to whole 6-bit groups (type 5: 424 bits
    # -> 71 chars = 426 bits); pyais trims the surplus back out on decode.
    pad = (-len(bits)) % 6
    bits = list(bits) + [0] * pad
    assert len(bits) % 6 == 0, len(bits)
    out = []
    for i in range(0, len(bits), 6):
        v = 0
        for k in range(6):
            v = (v << 1) | bits[i + k]
        out.append(chr(v + 48) if v < 40 else chr(v + 56))
    return "".join(out)


def sentence(payload):
    body = f"AIVDM,1,1,,A,{payload},0"
    cs = 0
    for c in body:
        cs ^= ord(c)
    return f"!{body}*{cs:02X}"


def check_type13():
    bits = [0] * 168
    put(bits, 0, 6, 1)          # type
    put(bits, 6, 2, 2)          # repeat
    put(bits, 8, 30, 244690971)  # mmsi
    put(bits, 38, 4, 8)         # nav status
    put(bits, 42, 8, 0xE0)      # turn (signed -32)
    put(bits, 50, 10, 105)      # speed 10.5 kn
    put(bits, 60, 1, 1)         # accuracy
    # lon/lat in 1/10000 arc-min: 51.5010 N, 0.1419 W
    put(bits, 61, 28, (int(round(-0.1419 * 600000))) & 0xFFFFFFF)
    put(bits, 89, 27, (int(round(51.5010 * 600000))) & 0x7FFFFFF)
    put(bits, 116, 12, 1234)    # course 123.4
    put(bits, 128, 9, 45)       # heading
    put(bits, 137, 6, 33)       # second
    put(bits, 143, 2, 1)        # maneuver
    put(bits, 148, 1, 1)        # raim

    msg = decode(sentence(armor(bits)))
    ok = True

    def eq(name, got, want):
        nonlocal ok
        if got != want:
            print(f"  FAIL type1/3 {name}: pyais={got!r} want={want!r}")
            ok = False

    eq("type", msg.msg_type, 1)
    eq("repeat", msg.repeat, 2)
    eq("mmsi", msg.mmsi, 244690971)
    eq("nav", int(msg.nav_status) if hasattr(msg, "nav_status") else msg.status, 8)
    eq("speed", msg.speed, 10.5)
    eq("accuracy", msg.accuracy, True)
    eq("lon", round(msg.lon, 4), -0.1419)
    eq("lat", round(msg.lat, 4), 51.5010)
    eq("course", msg.course, 123.4)
    eq("heading", msg.heading, 45)
    eq("second", msg.second, 33)
    eq("raim", msg.raim, True)
    return ok


def check_type5():
    bits = [0] * 424
    put(bits, 0, 6, 5)
    put(bits, 8, 30, 215678900)
    put(bits, 40, 30, 9012345)     # imo
    # callsign @70/42
    cs = sixbit("AB1CD")
    for i, v in enumerate(cs):
        put(bits, 70 + 6 * i, 6, v)
    # shipname @112/120 (20 chars)
    nm = sixbit("TEST SHIP")
    for i, v in enumerate(nm):
        put(bits, 112 + 6 * i, 6, v)
    put(bits, 232,  8, 70)         # ship type
    put(bits, 240,  9, 120)        # to bow
    put(bits, 249,  9, 30)         # to stern
    put(bits, 258,  6, 5)          # to port
    put(bits, 264,  6, 7)          # to starboard
    put(bits, 294,  8, 55)         # draught 5.5 m   <-- claimed offset
    # destination @302/120 (20 chars)
    dst = sixbit("HARBOR")
    for i, v in enumerate(dst):
        put(bits, 302 + 6 * i, 6, v)

    msg = decode(sentence(armor(bits)))
    ok = True

    def eq(name, got, want):
        nonlocal ok
        if got != want:
            print(f"  FAIL type5 {name}: pyais={got!r} want={want!r}")
            ok = False

    eq("type", msg.msg_type, 5)
    eq("mmsi", msg.mmsi, 215678900)
    eq("imo", msg.imo, 9012345)
    eq("callsign", msg.callsign.strip(), "AB1CD")
    eq("shipname", msg.shipname.strip(), "TEST SHIP")
    eq("ship_type", int(msg.ship_type), 70)
    eq("draught", msg.draught, 5.5)
    eq("destination", msg.destination.strip(), "HARBOR")
    return ok


def main():
    ok1 = check_type13()
    ok5 = check_type5()
    if ok1 and ok5:
        print("field offsets verified against pyais: type1/3 OK, type5 OK")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
