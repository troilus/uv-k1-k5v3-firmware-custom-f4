#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build App/ui/cn_font.c from the FontTool dot-matrix files.

The firmware normally draws Chinese out of the external SPI flash (tools/font_write.py
puts the 16x16/8x8 blobs there). The LBJRadio Chinese build (ENABLE_CN_FONT)
instead carries the characters it actually uses inside the firmware image, so
the radio shows Chinese with no font flashed at all.

Source of the bitmaps: the two files UV-K1_FontTool produces,
UV-K1_FontTool_v1.0/字体部分/16x16.FON and 8x8.FON. Their layout is the one the
firmware already reads:

    16x16 : 8178 glyphs x 32 B, glyph = GB2312 index (hi-0xA1)*94 + (lo-0xA1),
            column-major: bytes 0..15 are the top page (8 rows), 16..31 the
            bottom page (8 rows), one bit per row, LSB first.
    8x8   : 8178 glyphs x 8 B, column-major, one page.

Which characters go in:

    8x8    every CJK character in a string literal of the firmware sources
           below, plus every character listed in App/cn_chars.txt (the route
           names a report carries, one block of free text).
    16x16  the CJK characters of the menu sources only - they are the ones the
           big font draws - and they are also kept in the 8x8 table, because
           the same names appear in a small-font list (the menu wrap-around,
           the battery-type values).

The generator fails when a character has no glyph in the .FON files.

    python tools/mkcnfont.py            # regenerate App/ui/cn_font.c
    python tools/mkcnfont.py --list     # print the character set and sizes
    python tools/mkcnfont.py --check    # fail when cn_font.c is out of date

The generated file is committed, so the CI needs neither Python nor Pillow.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

FON16 = os.path.join(ROOT, "UV-K1_FontTool_v1.0", "字体部分", "16x16.FON")
FON8 = os.path.join(ROOT, "UV-K1_FontTool_v1.0", "字体部分", "8x8.FON")
EXTRA = os.path.join(ROOT, "App", "cn_chars.txt")
OUT = os.path.join(ROOT, "App", "ui", "cn_font.c")
OUT_H = os.path.join(ROOT, "App", "ui", "cn_font.h")

GLYPH16 = 32
GLYPH8 = 8
GLYPH_COUNT = 94 * 87          # GB2312 0xA1A1 .. 0xF7FE

# Source files whose string literals are scanned. Only CJK is taken, so the
# English literals next to them cost nothing.
SOURCES = [
    "App/ui/menu.c",
    "App/ui/helper.c",
    "App/ui/main.c",
    "App/ui/welcome.c",
    "App/app/menu.c",
    "App/app/lbj_dual.c",
    "App/app/lbj_log.c",
    "App/frequencies.c",
]

# Among those, the ones drawn with the 16x16 font: menu names and category
# names go through UI_PrintString(), everything else through the 8x8 helpers.
BIG_SOURCES = [
    "App/ui/menu.c",
    "App/app/menu.c",
]


def gb_index(ch):
    b = ch.encode("gb2312")
    if len(b) != 2:
        return None
    hi, lo = b[0], b[1]
    if not (0xA1 <= hi <= 0xF7 and 0xA1 <= lo <= 0xFE):
        return None
    return (hi - 0xA1) * 94 + (lo - 0xA1)


def load_font(path, size):
    with open(path, "rb") as f:
        data = f.read()
    need = GLYPH_COUNT * size
    if len(data) < need:
        sys.exit("%s: %d bytes, need at least %d" % (path, len(data), need))
    return data


def is_cjk(ch):
    o = ord(ch)
    return (0x4E00 <= o <= 0x9FFF or 0x3400 <= o <= 0x4DBF
            or 0x3000 <= o <= 0x303F or 0xFF00 <= o <= 0xFFEF)


STR_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')


def scan_sources(files):
    chars = set()
    for rel in files:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
        for line in text.splitlines():
            line = line.split("//", 1)[0]
            for m in STR_RE.finditer(line):
                for ch in m.group(1):
                    if is_cjk(ch) and gb_index(ch) is not None:
                        chars.add(ch)
    return chars


def scan_extra():
    chars = set()
    if not os.path.exists(EXTRA):
        return chars
    with open(EXTRA, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0]
            for ch in line:
                if is_cjk(ch) and gb_index(ch) is not None:
                    chars.add(ch)
    return chars


def build():
    f16 = load_font(FON16, GLYPH16)
    f8 = load_font(FON8, GLYPH8)

    big = scan_sources(BIG_SOURCES)
    extra = scan_extra()
    small = scan_sources(SOURCES) | extra | big

    def glyphs(chars, size, font):
        out = []
        for ch in chars:
            idx = gb_index(ch)
            out.append((idx, ch, font[idx * size:(idx + 1) * size]))
        out.sort(key=lambda e: e[0])
        return out

    return glyphs(big, GLYPH16, f16), glyphs(small, GLYPH8, f8), extra


def indent(data, per_line=16, width=2):
    out = []
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        out.append("    " + ", ".join("0x%0*x" % (width, b) for b in chunk) + ",")
    return "\n".join(out)


HEADER = """\
/* Generated by tools/mkcnfont.py - do not edit by hand.
 *
 * The Chinese characters the LBJRadio Chinese build (ENABLE_CN_FONT) draws,
 * cut out of the FontTool dot-matrix files (16x16.FON / 8x8.FON) that the
 * external-flash font is built from. Same glyph layout as the flash font, so
 * the drawing code treats both the same way:
 *
 *   16x16 : 32 B, column-major, bytes 0..15 = top page, 16..31 = bottom page
 *    8x8  :  8 B, column-major, one page
 *
 * Sorted by GB2312 code, binary searched. A code that is not in the set has no
 * glyph here; the drawing code then leaves a blank cell (the Chinese build
 * never reads the external flash font, so it works without one flashed).
 */
#ifndef APP_UI_CN_FONT_H
#define APP_UI_CN_FONT_H

#include <stdbool.h>
#include <stdint.h>

#define CN_FONT_GLYPH16  32u
#define CN_FONT_GLYPH8    8u

/* 16x16 characters (the menu) and 8x8 characters (the small font). */
#define CN_FONT_COUNT16  %(n16)du
#define CN_FONT_COUNT8   %(n8)du

/* Null when the character is not in the built-in set. */
const uint8_t *CN_FONT_Glyph16(uint16_t code);
const uint8_t *CN_FONT_Glyph8(uint16_t code);

#endif /* APP_UI_CN_FONT_H */
"""


def emit(big, small, out_c=None, out_h=None):
    out_c = out_c or OUT
    out_h = out_h or OUT_H

    with open(out_h, "w", encoding="utf-8", newline="\n") as f:
        f.write(HEADER % {"n16": len(big), "n8": len(small)})

    with open(out_c, "w", encoding="utf-8", newline="\n") as f:
        f.write("/* Generated by tools/mkcnfont.py - do not edit by hand. */\n")
        f.write('#include "cn_font.h"\n\n')

        for codes_name, glyph_name, entries, count, size in (
                ("cnCodes16", "cnGlyph16", big, "CN_FONT_COUNT16", GLYPH16),
                ("cnCodes8", "cnGlyph8", small, "CN_FONT_COUNT8", GLYPH8)):
            f.write("static const uint16_t %s[%s] = {\n" % (codes_name, count))
            line = []
            for e in entries:
                line.append("0x%04X," % e[0])
                if len(line) == 12:
                    f.write("    " + " ".join(line) + "\n")
                    line = []
            if line:
                f.write("    " + " ".join(line) + "\n")
            f.write("};\n\n")
            f.write("static const uint8_t %s[%s * %d] =\n{\n"
                    % (glyph_name, count, size))
            f.write("\n".join(indent(e[2]) for e in entries) + "\n};\n\n")

        f.write("""\
/* Index of a GB2312 code in a sorted table, or n when it is not there. */
static uint16_t cn_find(const uint16_t *codes, uint16_t n, uint16_t code){
    uint16_t lo = 0u, hi = n;
    while (lo < hi){
        const uint16_t mid = (uint16_t)(lo + (uint16_t)((hi - lo) >> 1));
        const uint16_t c = codes[mid];
        if (c == code) return mid;
        if (c < code) lo = (uint16_t)(mid + 1u); else hi = mid;
    }
    return n;
}

const uint8_t *CN_FONT_Glyph16(uint16_t code){
    const uint16_t i = cn_find(cnCodes16, CN_FONT_COUNT16, code);
    return (i < CN_FONT_COUNT16) ? &cnGlyph16[(uint32_t)i * CN_FONT_GLYPH16] : 0;
}

const uint8_t *CN_FONT_Glyph8(uint16_t code){
    const uint16_t i = cn_find(cnCodes8, CN_FONT_COUNT8, code);
    return (i < CN_FONT_COUNT8) ? &cnGlyph8[(uint32_t)i * CN_FONT_GLYPH8] : 0;
}
""")


def main():
    ap = argparse.ArgumentParser(description="Build the built-in Chinese font subset")
    ap.add_argument("--list", action="store_true", help="print the character set")
    ap.add_argument("--check", action="store_true",
                    help="fail unless App/ui/cn_font.c is up to date")
    args = ap.parse_args()

    big, small, extra = build()
    n16, n8 = len(big), len(small)
    size16 = n16 * GLYPH16
    size8 = n8 * GLYPH8

    if args.list:
        print("16x16: %d chars %d B, 8x8: %d chars %d B, total %d B"
              % (n16, size16, n8, size8, size16 + size8))
        print("routes/extra: %d chars" % len(extra))
        print("big: " + "".join(e[1] for e in big))
        return 0

    if args.check:
        if not os.path.exists(OUT) or not os.path.exists(OUT_H):
            sys.exit("App/ui/cn_font.c is missing, run tools/mkcnfont.py")
        import tempfile
        tmp = tempfile.mkdtemp()
        a_c = os.path.join(tmp, "cn_font.c")
        a_h = os.path.join(tmp, "cn_font.h")
        emit(big, small, a_c, a_h)
        # A Windows checkout may have rewritten the generated files to CRLF, so
        # compare with the line endings normalised.
        def norm(path):
            with open(path, "rb") as f:
                return f.read().replace(b"\r\n", b"\n")
        same = (norm(a_c) == norm(OUT) and norm(a_h) == norm(OUT_H))
        if not same:
            sys.exit("App/ui/cn_font.c is out of date, run tools/mkcnfont.py")
        print("cn_font.c is up to date (%d + %d chars)" % (n16, n8))
        return 0

    emit(big, small)
    print("wrote App/ui/cn_font.c: 16x16 %d chars %d B + 8x8 %d chars %d B = %d B"
          % (n16, size16, n8, size8, size16 + size8))
    return 0


if __name__ == "__main__":
    sys.exit(main())
