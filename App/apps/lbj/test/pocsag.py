#!/usr/bin/env python3
# POCSAG + LBJ frame builder/decoder used by model_rx.py. Mirrors the exact
# conventions of RTL_SDR_LBJ_RECEIVER (rtl_sdr_lbj_receiver.py) so the C app can
# be checked bit for bit.
#
# POCSAG codeword: [1 id][20 data][10 BCH][1 even parity], MSB first.
#   id=0 address word: data20 = ((addr>>3)<<2) | func
#   id=1 message word: data20 = 5 BCD nibbles, each nibble bit-reversed.
# BCH(31,21), generator feedback 873 (matches _calc_syndrome).

SYNC_STD = 0x7CD215D8
SYNC_INV = 2200824359
IDLE_WORD = 2055848343  # 0x7A89C197
BCDTBL = "0123456789*U -)("


def calc_syndrome(d31):
    r = 0
    for i in range(30, -1, -1):
        fb = (r >> 9) & 1
        r = ((r << 1) | ((d31 >> i) & 1)) & 1023
        if fb:
            r ^= 873
    return r


def popcount(v):
    return bin(v & 0xFFFFFFFF).count("1")


def bch_encode(d31):
    """Append 10 BCH parity bits so calc_syndrome(d31)=0."""
    data21 = d31 >> 10            # id<<20 | data20, in bits 20..0
    parity = calc_syndrome(data21 << 10)
    return (data21 << 10) | parity


_SYNDROME_LUT = {calc_syndrome(1 << p): p for p in range(31)}


def bch_decode(cw):
    d31 = (cw >> 1) & 0x7FFFFFFF
    syn = calc_syndrome(d31)
    if syn == 0:
        return cw, True
    fp = _SYNDROME_LUT.get(syn)
    if fp is not None:
        return (((d31 ^ (1 << fp)) << 1) | (cw & 1)), True
    return cw, False


def _word(idbit, data20):
    d31 = (idbit << 30) | (data20 << 10)
    d31 = bch_encode(d31)
    return ((d31 << 1) | (popcount(d31) & 1)) & 0xFFFFFFFF


def address_cw(addr, func):
    return _word(0, (((addr >> 3) << 2) | (func & 3)) & 0xFFFFF)


def _nibble_rev(v):
    return ((v & 1) << 3) | ((v & 2) << 1) | ((v & 4) >> 1) | ((v & 8) >> 3)


def message_cws(bcd):
    """5 BCD chars per message codeword (pad with spaces -> 'C')."""
    s = bcd + " " * ((-len(bcd)) % 5)
    out = []
    for k in range(0, len(s), 5):
        d20 = 0
        for n in range(5):
            ch = s[k + n]
            rev = BCDTBL.index(ch)
            d20 |= _nibble_rev(rev) << (16 - n * 4)
        out.append(_word(1, d20))
    return out


def address_field(addr):
    """Field value the receiver will see, and the frame slot it must sit in."""
    return addr >> 3, addr & 7


def bits_of_word(w):
    return [(w >> i) & 1 for i in range(31, -1, -1)]


def batch(addr, func, bcd, msgs_bytes=None):
    """One POCSAG batch: 16 codewords with the address in its frame slot."""
    field, frame = address_field(addr)
    cws = [IDLE_WORD] * 16
    cws[2 * frame] = address_cw(addr, func)
    for i, w in enumerate(message_cws(bcd)):
        if 2 * frame + 1 + i < 16:
            cws[2 * frame + 1 + i] = w
    return cws


def transmission(addr, func, bcd, batches=1, preamble_bits=576):
    bits = [1 if i % 2 == 0 else 0 for i in range(preamble_bits)]
    for _ in range(batches):
        bits += bits_of_word(SYNC_STD)
        for w in batch(addr, func, bcd):
            bits += bits_of_word(w)
    return bits


# ---- LBJ payload helpers (reference field layout) ----

def lbj_short(train, speed, position):
    """addr 1233999/1234000, >=15 chars: train[0:6] speed[6:9] . pos[10:15]."""
    return ("%-6s%03d %05s" % (train[:6], speed, position))[:15]


def lbj_prefix_hex(chars):
    """2 ASCII chars -> 4 BCD chars using the receiver's hex alphabet."""
    inv = {c: i for i, c in enumerate(BCDTBL)}
    out = ""
    for c in chars:
        out += BCDTBL[(ord(c) >> 4) & 15] + BCDTBL[ord(c) & 15]
    return out


def lbj_route_hex(text, nbytes=8):
    """GBK route bytes -> 16 BCD hex chars (nbytes bytes)."""
    b = text.encode("gbk")[:nbytes].ljust(nbytes, b"\x00")
    out = ""
    for v in b:
        out += BCDTBL[(v >> 4) & 15] + BCDTBL[v & 15]
    return out


def lbj_detail(prefix="T1", loco_code="201", loco_no="1234", route="京沪线"):
    """addr 1234001/1234002 detailed payload: build the last-50 char block the
    receiver slices: [0:4] prefix hex, [4:7] loco code, [7:12] loco number,
    [14:30] route hex."""
    buf = lbj_prefix_hex(prefix) + (loco_code + loco_no)[:8].ljust(8) + "  "
    buf += lbj_route_hex(route, 8)
    return buf.ljust(65, " ")[:65]


def decode_bcd(cws):
    out = []
    for cw in cws:
        d20 = (cw >> 11) & 0xFFFFF
        for n in range(5):
            v = (d20 >> (16 - n * 4)) & 0xF
            out.append(BCDTBL[_nibble_rev(v)])
    return "".join(out)


if __name__ == "__main__":
    # self-test: every generated codeword decodes with no error
    assert bch_encode(0) == 0
    for addr, func in ((1234000, 3), (1233999, 1), (1234001, 1), (1234002, 3)):
        for cw in batch(addr, func, lbj_short("412", 87, "01234")):
            cor, ok = bch_decode(cw)
            assert ok, hex(cw)
    m = message_cws(lbj_short("412", 87, "01234"))
    print("bcd round-trip:", repr(decode_bcd(m)))
    print("addr cw:", hex(address_cw(1234000, 3)))
    print("self-test OK")
