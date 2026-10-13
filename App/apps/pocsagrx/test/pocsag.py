#!/usr/bin/env python3
# Generic POCSAG frame builder/decoder used by model_rx.py.
#
# POCSAG codeword: [1 id][20 data][10 BCH][1 even parity], MSB first.
#   id=0 address word: data20 = ((addr>>3)<<2) | func
#   id=1 message word, numeric:     20 data bits = 5 BCD nibbles (4-bit, LSB first)
#   id=1 message word, alphanumeric:20 data bits = a slice of a 7-bit ASCII stream
#     (chars packed MSB-first left-to-right, each char LSB-first on the wire).
# BCH(31,21), generator feedback 873 (the low 10 bits of 0x769); single-bit
# correction, matching the app.
#
# Rates (512/1200/2400) only affect the sample-level channel in model_rx.py; the
# codeword bit stream is identical, so this module is rate-independent.

SYNC_STD = 0x7CD215D8
SYNC_INV = 2200824359
IDLE_WORD = 2055848343  # 0x7A89C197

# Standard POCSAG numeric alphabet: 4-bit value -> character.
NUMERIC = "0123456789*U -)("


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


def numeric_cws(text):
    """5 BCD chars per message codeword (pad with spaces)."""
    s = text + " " * ((-len(text)) % 5)
    out = []
    for k in range(0, len(s), 5):
        d20 = 0
        for n in range(5):
            ch = s[k + n]
            rev = NUMERIC.index(ch)
            d20 |= _nibble_rev(rev) << (16 - n * 4)
        out.append(_word(1, d20))
    return out


def alpha_bits(text):
    """7-bit chars, LSB-first on the wire; pad with spaces to a 20-bit boundary."""
    while (len(text) * 7) % 20:
        text += " "
    bits = []
    for ch in text:
        v = ord(ch) & 0x7F
        for b in range(7):
            bits.append((v >> b) & 1)
    return bits


def alpha_cws(text):
    """Alphanumeric message codewords: 7-bit ASCII packed into 20-bit fields."""
    bits = alpha_bits(text)
    out = []
    for i in range(0, len(bits), 20):
        d20 = 0
        for j in range(20):
            if bits[i + j]:
                d20 |= 1 << (19 - j)
        out.append(_word(1, d20))
    return out


def address_field(addr):
    """Field value the receiver will see, and the frame slot it must sit in."""
    return addr >> 3, addr & 7


def bits_of_word(w):
    return [(w >> i) & 1 for i in range(31, -1, -1)]


def batch(addr, func, cws):
    """One POCSAG batch: 16 codewords with the address in its frame slot."""
    field, frame = address_field(addr)
    out = [IDLE_WORD] * 16
    out[2 * frame] = address_cw(addr, func)
    for i, w in enumerate(cws):
        if 2 * frame + 1 + i < 16:
            out[2 * frame + 1 + i] = w
    return out


def transmission(addr, func, cws, preamble_bits=576):
    """A full transmission: preamble + one or more batches. A message may run
    past the end of a batch; it continues (after that batch's sync) in the next."""
    field, frame = address_field(addr)
    pos = 2 * frame
    items = [(pos, address_cw(addr, func))]
    pos += 1
    for w in cws:
        items.append((pos, w))
        pos += 1
    nb = (pos + 15) // 16
    bits = [1 if i % 2 == 0 else 0 for i in range(preamble_bits)]
    for b in range(nb):
        out = [IDLE_WORD] * 16
        for p, w in items:
            if b * 16 <= p < (b + 1) * 16:
                out[p - b * 16] = w
        bits += bits_of_word(SYNC_STD)
        for w in out:
            bits += bits_of_word(w)
    return bits


# ---- payload decode (host side; mirrors the app) ----

def decode_bcd(cws):
    out = []
    for cw in cws:
        d20 = (cw >> 11) & 0xFFFFF
        for n in range(5):
            v = (d20 >> (16 - n * 4)) & 0xF
            out.append(NUMERIC[_nibble_rev(v)])
    return "".join(out)


def decode_alpha(cws):
    bits = []
    for cw in cws:
        d20 = (cw >> 11) & 0xFFFFF
        for i in range(19, -1, -1):
            bits.append((d20 >> i) & 1)
    out = []
    for i in range(0, len(bits) - 6, 7):
        v = 0
        for b in range(7):
            v |= bits[i + b] << b
        out.append(chr(v))
    return "".join(out)


if __name__ == "__main__":
    # self-test: address codewords, numeric and alphanumeric round-trips.
    for addr, func in ((1234000, 3), (1233999, 1), (100000, 0), (2000000, 2)):
        cw, ok = bch_decode(address_cw(addr, func))
        assert ok and cw == address_cw(addr, func), hex(cw)
    m = numeric_cws("412   087 01234")
    assert decode_bcd(m) == "412   087 01234", repr(decode_bcd(m))
    for text in ("TEST 1234", "Hello, world!", "A", "x" * 20):
        m = alpha_cws(text)
        got = decode_alpha(m).rstrip()
        assert got == text, (repr(text), repr(got))
    bits = transmission(100000, 0, numeric_cws("12"))
    assert len(bits) == 576 + 32 + 16 * 32
    print("pocsag self-test OK")
