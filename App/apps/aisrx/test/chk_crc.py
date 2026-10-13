def crc16_c(data, init=0xFFFF):
    """Exactly the C crc16 in ais_decode.c (MSB-of-byte first, reflected)."""
    crc = init
    for by in data:
        c = by
        for _ in range(8):
            mix = (crc ^ (c >> 7)) & 1
            crc >>= 1
            if mix:
                crc ^= 0x8408
            c = (c << 1) & 0xFF
    return crc


frames = [
    "2001A2901D000008B4A5CA05E574E40DA650012A190172",
    "2000008DCF07000874802904251911B2FF4900108B311A",
    "202CB2274D040000B452CC0485F80A00FF394003E093F7",
    "28EC0AD62780EBEC3B6B916AB006044FA010C8D14110044110044110046200000000964300BE064A102AE1411004411004411004012694",
]
print("SDR frames (my decoder said crcOk=1 -> residue must be 0xF0B8):")
for h in frames:
    b = bytes.fromhex(h)
    r = crc16_c(b)
    print("  len=%2d  residue=0x%04X  %s" % (len(b), r, "OK" if r == 0xF0B8 else "*** MISMATCH ***"))

# Build a message + FCS the standard AIS way (gen_ais.py does ~crc_bits LSB-first),
# and confirm the decoder's residue convention on a KNOWN-GOOD frame.
def crc_bits_lsb(bits, init=0xFFFF):
    crc = init
    for b in bits:
        mix = (crc ^ b) & 1
        crc >>= 1
        if mix:
            crc ^= 0x8408
    return crc

# real type-1 payload from nmea-sample (message-only, 168 bits)
payload_hex = "13HOI:0P0000VOHLCnHQKwvL05Ip"
bits = []
for ch in payload_hex:
    v = ord(ch) - 48
    if v > 40:
        v -= 8
    for k in range(5, -1, -1):
        bits.append((v >> k) & 1)
# gen_ais frame: message + fcs, fcs = ~crc_bits(payload) as 16 bits LSB-first
f = (~crc_bits_lsb(bits)) & 0xFFFF
fcs = [(f >> k) & 1 for k in range(16)]
allbits = bits + fcs
# pack MSB-first into bytes
by = bytearray()
for i in range(0, len(allbits), 8):
    x = 0
    for k in range(8):
        x = (x << 1) | allbits[i + k]
    by.append(x)
print("\nknown-good frame built by gen_ais convention (should be 0xF0B8):")
print("  len=%d residue=0x%04X" % (len(by), crc16_c(by)))
