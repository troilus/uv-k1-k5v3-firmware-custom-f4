#!/usr/bin/env python3
"""
Convert BillJilly.ttf to font.c bitmap format for UV-K5 firmware.
Generates App/font_billjilly.c as a drop-in replacement for App/font.c
"""

from PIL import Image, ImageFont, ImageDraw
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FONT_PATH = os.path.join(SCRIPT_DIR, 'BillJilly.ttf')
OUTPUT_PATH = os.path.join(SCRIPT_DIR, '..', '..', 'App', 'font_billjilly.c')

CHAR_NAMES = {
    33: 'exclam', 34: 'quotedbl', 35: 'numbersign',
    36: 'dollar', 37: 'percent', 38: 'ampersand', 39: 'quotesingle',
    40: 'parenleft', 41: 'parenright', 42: 'asterisk', 43: 'plus',
    44: 'comma', 45: 'hyphen', 46: 'period', 47: 'slash',
    48: 'zero', 49: 'one', 50: 'two', 51: 'three', 52: 'four',
    53: 'five', 54: 'six', 55: 'seven', 56: 'eight', 57: 'nine',
    58: 'colon', 59: 'semicolon', 60: 'less', 61: 'equal', 62: 'greater',
    63: 'question', 64: 'at',
    65: 'A', 66: 'B', 67: 'C', 68: 'D', 69: 'E',
    70: 'F', 71: 'G', 72: 'H', 73: 'I', 74: 'J',
    75: 'K', 76: 'L', 77: 'M', 78: 'N', 79: 'O',
    80: 'P', 81: 'Q', 82: 'R', 83: 'S', 84: 'T',
    85: 'U', 86: 'V', 87: 'W', 88: 'X', 89: 'Y', 90: 'Z',
    91: 'bracketleft', 92: 'backslash', 93: 'bracketright',
    94: 'asciicircum', 95: 'underscore', 96: 'grave',
    97: 'a', 98: 'b', 99: 'c', 100: 'd', 101: 'e',
    102: 'f', 103: 'g', 104: 'h', 105: 'i', 106: 'j',
    107: 'k', 108: 'l', 109: 'm', 110: 'n', 111: 'o',
    112: 'p', 113: 'q', 114: 'r', 115: 's', 116: 't',
    117: 'u', 118: 'v', 119: 'w', 120: 'x', 121: 'y', 122: 'z',
    123: 'braceleft', 124: 'bar', 125: 'braceright', 126: 'asciitilde',
}


def render_16(font, char, target_w):
    """Render a 16-pixel-tall character, return (top_bytes, bot_bytes)."""
    img = Image.new('1', (target_w, 16), 0)
    draw = ImageDraw.Draw(img)

    bbox = font.getbbox(char)
    char_width = bbox[2] - bbox[0]
    char_height = bbox[3] - bbox[1]

    origin_x = (target_w - char_width) // 2 - bbox[0]
    top_padding = max(0, (16 - char_height) // 2)
    origin_y = top_padding - bbox[1]

    draw.text((origin_x, origin_y), char, font=font, fill=1)

    pixels = img.load()
    top_bytes = []
    bot_bytes = []
    for col in range(target_w):
        tb = 0
        bb = 0
        for row in range(8):
            if pixels[col, row] > 0:
                tb |= (1 << row)
            if pixels[col, row + 8] > 0:
                bb |= (1 << row)
        top_bytes.append(tb)
        bot_bytes.append(bb)

    return top_bytes, bot_bytes


def render_8(font, char, target_w):
    """Render an 8-pixel-tall character, return list of bytes."""
    img = Image.new('1', (target_w, 8), 0)
    draw = ImageDraw.Draw(img)

    bbox = font.getbbox(char)
    char_width = bbox[2] - bbox[0]
    char_height = bbox[3] - bbox[1]

    origin_x = (target_w - char_width) // 2 - bbox[0]
    top_padding = max(0, (8 - char_height) // 2)
    origin_y = top_padding - bbox[1]

    draw.text((origin_x, origin_y), char, font=font, fill=1)

    pixels = img.load()
    result = []
    for col in range(target_w):
        byte = 0
        for row in range(8):
            if pixels[col, row] > 0:
                byte |= (1 << row)
        result.append(byte)

    return result


def gen_gfontbig(font, lines):
    lines.append('')
    lines.append('const uint8_t gFontBig[95 - 1][16 - 2] =')
    lines.append('{')
    lines.append('\t// {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /*0x00,*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},    // \' \'  ')

    for code in range(33, 127):
        if code == 126:
            # Project maps '~' to right arrow glyph
            try:
                char = '\u2192'
                top_bytes, bot_bytes = render_16(font, char, 7)
            except Exception:
                char = '>'
                top_bytes, bot_bytes = render_16(font, char, 7)
            comment_char = '->'
        else:
            char = chr(code)
            top_bytes, bot_bytes = render_16(font, char, 7)
            comment_char = char

        is_last = (code == 126)

        parts_top = ', '.join(f'0x{b:02X}' for b in top_bytes)
        parts_bot = ', '.join(f'0x{b:02X}' for b in bot_bytes)
        line = f'\t{{{parts_top}, /*0x00,*/ {parts_bot}}}'
        if not is_last:
            line += ','
        if code == 126:
            line += f'    // \'{comment_char}\''
        else:
            name = CHAR_NAMES.get(code, '')
            esc = '\\' if comment_char in '\\\'' else ''
            line += f'    // \'{esc}{comment_char}\''
            if name:
                line += f'  // {name}'
        lines.append(line)
    lines.append('};')


def gen_gfontsmalls(font, lines):
    lines.append('')
    lines.append('const uint8_t gFontSmall[95 - 1][6] =')
    lines.append('{')
    lines.append('\t// {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},    // \' \'')

    for code in range(33, 127):
        if code == 126:
            try:
                char = '\u2192'
                bytes_list = render_8(font, char, 6)
            except Exception:
                char = '>'
                bytes_list = render_8(font, char, 6)
            comment_char = '->'
        else:
            char = chr(code)
            bytes_list = render_8(font, char, 6)
            comment_char = char

        is_last = (code == 126)

        parts = ', '.join(f'0x{b:02X}' for b in bytes_list)
        line = f'\t{{{parts}}}'
        if not is_last:
            line += ','
        if code == 126:
            line += f'    // \'{comment_char}\''
        else:
            name = CHAR_NAMES.get(code, '')
            esc = '\\' if comment_char in '\\\'' else ''
            line += f'    // \'{esc}{comment_char}\''
            if name:
                line += f'  // {name}'
        lines.append(line)
    lines.append('};')


def gen_gfontbigdigits(font, lines):
    lines.append('')
    lines.append('// BillJilly digits for VFO frequency display')
    lines.append('const uint8_t gFontBigDigits[11][26 - 6] =')
    lines.append('{')

    digits = '0123456789-'
    for i, char in enumerate(digits):
        top_bytes, bot_bytes = render_16(font, char, 10)
        is_last = (i == len(digits) - 1)

        parts_top = ', '.join(f'0x{b:02X}' for b in top_bytes)
        parts_bot = ', '.join(f'0x{b:02X}' for b in bot_bytes)
        line = f'\t{{{parts_top},          {parts_bot}}}'
        if not is_last:
            line += ','
        line += f'    // \'{char}\''
        lines.append(line)

    lines.append('};')


def main():
    print(f'Loading font: {FONT_PATH}')
    lines = []
    lines.append('/* Copyright 2023 Dual Tachyon')
    lines.append(' * https://github.com/DualTachyon')
    lines.append(' *')
    lines.append(' * Licensed under the Apache License, Version 2.0 (the "License");')
    lines.append(' * you may not use this file except in compliance with the License.')
    lines.append(' * You may obtain a copy of the License at')
    lines.append(' *')
    lines.append(' *     http://www.apache.org/licenses/LICENSE-2.0')
    lines.append(' *')
    lines.append(' *     Unless required by applicable law or agreed to in writing, software')
    lines.append(' *     distributed under the License is distributed on an "AS IS" BASIS,')
    lines.append(' *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.')
    lines.append(' *     See the License for the specific language governing permissions and')
    lines.append(' *     limitations under the License.')   
    lines.append(' */')
    lines.append('')
    lines.append('// Auto-generated from BillJilly.ttf')
    lines.append('// Drop-in replacement for font.c')
    lines.append('')
    lines.append('#include "font.h"')
    lines.append('')
    lines.append('')
    lines.append('// ---------------- Big Font (7x16) ----------------')

    font_big = ImageFont.truetype(FONT_PATH, 9)
    gen_gfontbig(font_big, lines)

    lines.append('')
    lines.append('')
    lines.append('// ---------------- Small Font (6x8) ----------------')

    font_small = ImageFont.truetype(FONT_PATH, 8)
    gen_gfontsmalls(font_small, lines)

    lines.append('')
    lines.append('')
    lines.append('// ---------------- Big Digits for VFO (10x16) ----------------')

    font_digits = ImageFont.truetype(FONT_PATH, 13)
    gen_gfontbigdigits(font_digits, lines)

    # Keep original gFont3x5 data (unchanged)
    lines.append('')
    lines.append('')
    lines.append('// ---------------- 3x5 Font (unaltered from original) ----------------')
    lines.append('')
    lines.append('//#ifdef ENABLE_SPECTRUM')
    lines.append('    const uint8_t gFont3x5[][3] =')
    lines.append('    {')
    lines.append('        {0x00, 0x00, 0x00}, //  32 - space')
    lines.append('        {0x00, 0x17, 0x00}, //  33 - exclam')
    lines.append('        {0x03, 0x00, 0x03}, //  34 - quotedbl')
    lines.append('        {0x1f, 0x0a, 0x1f}, //  35 - numbersign')
    lines.append('        {0x0a, 0x1f, 0x05}, //  36 - dollar')
    lines.append('        {0x09, 0x04, 0x12}, //  37 - percent')
    lines.append('        {0x0f, 0x17, 0x1c}, //  38 - ampersand')
    lines.append('        {0x00, 0x03, 0x00}, //  39 - quotesingle')
    lines.append('        {0x00, 0x0e, 0x11}, //  40 - parenleft')
    lines.append('        {0x11, 0x0e, 0x00}, //  41 - parenright')
    lines.append('        {0x05, 0x02, 0x05}, //  42 - asterisk')
    lines.append('        {0x04, 0x0e, 0x04}, //  43 - plus')
    lines.append('        {0x10, 0x08, 0x00}, //  44 - comma')
    lines.append('        {0x04, 0x04, 0x04}, //  45 - hyphen')
    lines.append('        {0x00, 0x10, 0x00}, //  46 - period')
    lines.append('        {0x18, 0x04, 0x03}, //  47 - slash')
    lines.append('        {0x1e, 0x11, 0x0f}, //  48 - zero')
    lines.append('        {0x02, 0x1f, 0x00}, //  49 - one')
    lines.append('        {0x19, 0x15, 0x12}, //  50 - two')
    lines.append('        {0x11, 0x15, 0x0a}, //  51 - three')
    lines.append('        {0x07, 0x04, 0x1f}, //  52 - four')
    lines.append('        {0x17, 0x15, 0x09}, //  53 - five')
    lines.append('        {0x1e, 0x15, 0x1d}, //  54 - six')
    lines.append('        {0x19, 0x05, 0x03}, //  55 - seven')
    lines.append('        {0x1f, 0x15, 0x1f}, //  56 - eight')
    lines.append('        {0x17, 0x15, 0x0f}, //  57 - nine')
    lines.append('        {0x00, 0x0a, 0x00}, //  58 - colon')
    lines.append('        {0x10, 0x0a, 0x00}, //  59 - semicolon')
    lines.append('        {0x04, 0x0a, 0x11}, //  60 - less')
    lines.append('        {0x0a, 0x0a, 0x0a}, //  61 - equal')
    lines.append('        {0x11, 0x0a, 0x04}, //  62 - greater')
    lines.append('        {0x01, 0x15, 0x03}, //  63 - question')
    lines.append('        {0x0e, 0x15, 0x16}, //  64 - at')
    lines.append('        {0x1e, 0x05, 0x1e}, //  65 - A')
    lines.append('        {0x1f, 0x15, 0x0a}, //  66 - B')
    lines.append('        {0x0e, 0x11, 0x11}, //  67 - C')
    lines.append('        {0x1f, 0x11, 0x0e}, //  68 - D')
    lines.append('        {0x1f, 0x15, 0x15}, //  69 - E')
    lines.append('        {0x1f, 0x05, 0x05}, //  70 - F')
    lines.append('        {0x0e, 0x15, 0x1d}, //  71 - G')
    lines.append('        {0x1f, 0x04, 0x1f}, //  72 - H')
    lines.append('        {0x11, 0x1f, 0x11}, //  73 - I')
    lines.append('        {0x08, 0x10, 0x0f}, //  74 - J')
    lines.append('        {0x1f, 0x04, 0x1b}, //  75 - K')
    lines.append('        {0x1f, 0x10, 0x10}, //  76 - L')
    lines.append('        {0x1f, 0x06, 0x1f}, //  77 - M')
    lines.append('        {0x1f, 0x0e, 0x1f}, //  78 - N')
    lines.append('        {0x0e, 0x11, 0x0e}, //  79 - O')
    lines.append('        {0x1f, 0x05, 0x02}, //  80 - P')
    lines.append('        {0x0e, 0x19, 0x1e}, //  81 - Q')
    lines.append('        {0x1f, 0x0d, 0x16}, //  82 - R')
    lines.append('        {0x12, 0x15, 0x09}, //  83 - S')
    lines.append('        {0x01, 0x1f, 0x01}, //  84 - T')
    lines.append('        {0x0f, 0x10, 0x1f}, //  85 - U')
    lines.append('        {0x07, 0x18, 0x07}, //  86 - V')
    lines.append('        {0x1f, 0x0c, 0x1f}, //  87 - W')
    lines.append('        {0x1b, 0x04, 0x1b}, //  88 - X')
    lines.append('        {0x03, 0x1c, 0x03}, //  89 - Y')
    lines.append('        {0x19, 0x15, 0x13}, //  90 - Z')
    lines.append('        {0x1f, 0x11, 0x11}, //  91 - bracketleft')
    lines.append('        {0x02, 0x04, 0x08}, //  92 - backslash')
    lines.append('        {0x11, 0x11, 0x1f}, //  93 - bracketright')
    lines.append('        {0x02, 0x01, 0x02}, //  94 - asciicircum')
    lines.append('        {0x10, 0x10, 0x10}, //  95 - underscore')
    lines.append('        {0x01, 0x02, 0x00}, //  96 - grave')
    lines.append('        {0x1a, 0x16, 0x1c}, //  97 - a')
    lines.append('        {0x1f, 0x12, 0x0c}, //  98 - b')
    lines.append('        {0x0c, 0x12, 0x12}, //  99 - c')
    lines.append('        {0x0c, 0x12, 0x1f}, // 100 - d')
    lines.append('        {0x0c, 0x1a, 0x16}, // 101 - e')
    lines.append('        {0x04, 0x1e, 0x05}, // 102 - f')
    lines.append('        {0x0c, 0x2a, 0x1e}, // 103 - g')
    lines.append('        {0x1f, 0x02, 0x1c}, // 104 - h')
    lines.append('        {0x00, 0x1d, 0x00}, // 105 - i')
    lines.append('        {0x10, 0x20, 0x1d}, // 106 - j')
    lines.append('        {0x1f, 0x0c, 0x12}, // 107 - k')
    lines.append('        {0x11, 0x1f, 0x10}, // 108 - l')
    lines.append('        {0x1e, 0x0e, 0x1e}, // 109 - m')
    lines.append('        {0x1e, 0x02, 0x1c}, // 110 - n')
    lines.append('        {0x0c, 0x12, 0x0c}, // 111 - o')
    lines.append('        {0x3e, 0x12, 0x0c}, // 112 - p')
    lines.append('        {0x0c, 0x12, 0x3e}, // 113 - q')
    lines.append('        {0x1c, 0x02, 0x02}, // 114 - r')
    lines.append('        {0x14, 0x1e, 0x0a}, // 115 - s')
    lines.append('        {0x02, 0x1f, 0x12}, // 116 - t')
    lines.append('        {0x0e, 0x10, 0x1e}, // 117 - u')
    lines.append('        {0x0e, 0x18, 0x0e}, // 118 - v')
    lines.append('        {0x1e, 0x1c, 0x1e}, // 119 - w')
    lines.append('        {0x12, 0x0c, 0x12}, // 120 - x')
    lines.append('        {0x06, 0x28, 0x1e}, // 121 - y')
    lines.append('        {0x1a, 0x1e, 0x16}, // 122 - z')
    lines.append('        {0x04, 0x1b, 0x11}, // 123 - braceleft')
    lines.append('        {0x00, 0x1b, 0x00}, // 124 - bar')
    lines.append('        {0x11, 0x1b, 0x04}, // 125 - braceright')
    lines.append('        {0x02, 0x03, 0x01}, // 126 - asciitilde')
    lines.append('')
    lines.append('        {0x12, 0x17, 0x12}, // 127 - plusminus')
    lines.append('    };')

    lines.append('')
    lines.append('')
    lines.append('// ---------------- Small Bold Font (unaltered) ----------------')
    lines.append('#ifdef ENABLE_SMALL_BOLD')
    lines.append('    const uint8_t gFontSmallBold[95 - 1][6] =')
    lines.append('    {')
    lines.append('        {0x00, 0x00, 0x5E, 0x5E, 0x00, 0x00},    // \'!\'')
    lines.append('        {0x00, 0x06, 0x00, 0x06, 0x00, 0x00},    // \'"\'')
    lines.append('        {0x14, 0x3E, 0x14, 0x3E, 0x14, 0x00},    // \'#\'')
    lines.append('        {0x26, 0x49, 0x7F, 0x49, 0x32, 0x00},    // \'$\'')
    lines.append('        {0x63, 0x33, 0x18, 0x0C, 0x66, 0x63},    // \'%\'')
    lines.append('        {0x30, 0x4B, 0x4D, 0x55, 0x22, 0x50},    // \'&\'')
    lines.append("        {0x00, 0x00, 0x07, 0x07, 0x00, 0x00},    // '''")
    lines.append('        {0x00, 0x1C, 0x22, 0x41, 0x00, 0x00},    // \'(\'')
    lines.append('        {0x00, 0x41, 0x22, 0x1C, 0x00, 0x00},    // \')\'')
    lines.append('        {0x00, 0x2A, 0x1C, 0x1C, 0x2A, 0x00},    // \'*\'')
    lines.append('        {0x08, 0x08, 0x3E, 0x08, 0x08, 0x00},    // \'+\'')
    lines.append('        {0x00, 0x40, 0x60, 0x20, 0x00, 0x00},    // \',\'')
    lines.append('        {0x00, 0x08, 0x08, 0x08, 0x08, 0x00},    // \'-\'')
    lines.append('        {0x00, 0x00, 0x60, 0x60, 0x00, 0x00},    // \'.\'')
    lines.append('        {0x40, 0x20, 0x10, 0x08, 0x04, 0x02},    // \'/\'')
    lines.append('        {0x3E, 0x7F, 0x41, 0x41, 0x7F, 0x3E},    // \'0\'')
    lines.append('        {0x00, 0x41, 0x7F, 0x7F, 0x40, 0x00},    // \'1\'')
    lines.append('        {0x62, 0x71, 0x51, 0x49, 0x4F, 0x46},    // \'2\'')
    lines.append('        {0x22, 0x41, 0x49, 0x49, 0x7F, 0x36},    // \'3\'')
    lines.append('        {0x18, 0x14, 0x12, 0x7F, 0x7F, 0x10},    // \'4\'')
    lines.append('        {0x27, 0x47, 0x45, 0x45, 0x7D, 0x39},    // \'5\'')
    lines.append('        {0x3E, 0x7F, 0x49, 0x49, 0x7B, 0x32},    // \'6\'')
    lines.append('        {0x01, 0x01, 0x79, 0x7D, 0x07, 0x03},    // \'7\'')
    lines.append('        {0x36, 0x7F, 0x49, 0x49, 0x7F, 0x36},    // \'8\'')
    lines.append('        {0x06, 0x4F, 0x49, 0x49, 0x7F, 0x3E},    // \'9\'')
    lines.append('        {0x00, 0x00, 0x6C, 0x6C, 0x00, 0x00},    // \':\'')
    lines.append('        {0x00, 0x40, 0x6C, 0x2C, 0x00, 0x00},    // \';\'')
    lines.append('        {0x08, 0x1C, 0x36, 0x63, 0x41, 0x00},    // \'<\'')
    lines.append('        {0x14, 0x14, 0x14, 0x14, 0x14, 0x00},    // \'=\'')
    lines.append('        {0x41, 0x63, 0x36, 0x1C, 0x08, 0x00},    // \'>\'')
    lines.append('        {0x02, 0x01, 0x51, 0x09, 0x06, 0x00},    // \'?\'')
    lines.append('        {0x30, 0x4A, 0x4A, 0x52, 0x3C, 0x00},    // \'@\'')
    lines.append('        {0x7E, 0x7F, 0x11, 0x11, 0x7F, 0x7E},    // \'A\'')
    lines.append('        {0x7F, 0x7F, 0x49, 0x49, 0x7F, 0x36},    // \'B\'')
    lines.append('        {0x3E, 0x7F, 0x41, 0x41, 0x63, 0x22},    // \'C\'')
    lines.append('        {0x7F, 0x7F, 0x41, 0x41, 0x7F, 0x3E},    // \'D\'')
    lines.append('        {0x7F, 0x7F, 0x49, 0x49, 0x41, 0x41},    // \'E\'')
    lines.append('        {0x7F, 0x7F, 0x09, 0x09, 0x09, 0x01},    // \'F\'')
    lines.append('        {0x3E, 0x7F, 0x41, 0x51, 0x73, 0x32},    // \'G\'')
    lines.append('        {0x7F, 0x7F, 0x08, 0x08, 0x7F, 0x7F},    // \'H\'')
    lines.append('        {0x00, 0x41, 0x7F, 0x7F, 0x41, 0x00},    // \'I\'')
    lines.append('        {0x20, 0x60, 0x41, 0x7F, 0x3F, 0x01},    // \'J\'')
    lines.append('        {0x7F, 0x7F, 0x0C, 0x1E, 0x73, 0x61},    // \'K\'')
    lines.append('        {0x7F, 0x7F, 0x40, 0x40, 0x40, 0x40},    // \'L\'')
    lines.append('        {0x7F, 0x7F, 0x06, 0x06, 0x7F, 0x7F},    // \'M\'')
    lines.append('        {0x7F, 0x7F, 0x08, 0x10, 0x7F, 0x7F},    // \'N\'')
    lines.append('        {0x3E, 0x7F, 0x41, 0x41, 0x7F, 0x3E},    // \'O\'')
    lines.append('        {0x7F, 0x7F, 0x11, 0x11, 0x1F, 0x0E},    // \'P\'')
    lines.append('        {0x3E, 0x7F, 0x41, 0x61, 0x7F, 0x5E},    // \'Q\'')
    lines.append('        {0x7F, 0x7F, 0x11, 0x11, 0x7F, 0x6E},    // \'R\'')
    lines.append('        {0x26, 0x6F, 0x49, 0x49, 0x7B, 0x32},    // \'S\'')
    lines.append('        {0x01, 0x01, 0x7F, 0x7F, 0x01, 0x01},    // \'T\'')
    lines.append('        {0x3F, 0x7F, 0x40, 0x40, 0x7F, 0x3F},    // \'U\'')
    lines.append('        {0x1F, 0x3F, 0x60, 0x60, 0x3F, 0x1F},    // \'V\'')
    lines.append('        {0x3F, 0x7F, 0x30, 0x30, 0x7F, 0x3F},    // \'W\'')
    lines.append('        {0x77, 0x77, 0x08, 0x08, 0x77, 0x77},    // \'X\'')
    lines.append('        {0x07, 0x0F, 0x78, 0x78, 0x0F, 0x07},    // \'Y\'')
    lines.append('        {0x61, 0x71, 0x59, 0x4D, 0x47, 0x43},    // \'Z\'')
    lines.append('        {0x00, 0x7F, 0x41, 0x41, 0x00, 0x00},    // \'[\'')
    lines.append("        {0x01, 0x02, 0x04, 0x08, 0x10, 0x60},    // '\\'")
    lines.append('        {0x00, 0x00, 0x41, 0x41, 0x7F, 0x00},    // \']\'')
    lines.append('        {0x04, 0x02, 0x01, 0x02, 0x04, 0x00},    // \'^\'')
    lines.append('        {0x40, 0x40, 0x40, 0x40, 0x40, 0x40},    // \'_\'')
    lines.append('        {0x00, 0x03, 0x07, 0x06, 0x00, 0x00},    // \'`\'')
    lines.append('        {0x7C, 0x7E, 0x12, 0x12, 0x7E, 0x7C},    // \'a\'')
    lines.append('        {0x7E, 0x7E, 0x4A, 0x4A, 0x4A, 0x3C},    // \'b\'')
    lines.append('        {0x3C, 0x7E, 0x42, 0x42, 0x66, 0x24},    // \'c\'')
    lines.append('        {0x7E, 0x7E, 0x42, 0x42, 0x7E, 0x3C},    // \'d\'')
    lines.append('        {0x7E, 0x7E, 0x4A, 0x4A, 0x4A, 0x42},    // \'e\'')
    lines.append('        {0x7E, 0x7E, 0x12, 0x12, 0x12, 0x02},    // \'f\'')
    lines.append('        {0x3C, 0x7E, 0x42, 0x52, 0x76, 0x34},    // \'g\'')
    lines.append('        {0x7E, 0x7E, 0x08, 0x08, 0x7E, 0x7E},    // \'h\'')
    lines.append('        {0x00, 0x42, 0x7E, 0x7E, 0x42, 0x00},    // \'i\'')
    lines.append('        {0x00, 0x60, 0x60, 0x40, 0x7E, 0x3E},    // \'j\'')
    lines.append('        {0x7E, 0x7E, 0x18, 0x3C, 0x66, 0x42},    // \'k\'')
    lines.append('        {0x7E, 0x7E, 0x40, 0x40, 0x40, 0x40},    // \'l\'')
    lines.append('        {0x7E, 0x7E, 0x04, 0x04, 0x7E, 0x7E},    // \'m\'')
    lines.append('        {0x7E, 0x7E, 0x08, 0x10, 0x7E, 0x7E},    // \'n\'')
    lines.append('        {0x3C, 0x7E, 0x42, 0x42, 0x7E, 0x3C},    // \'o\'')
    lines.append('        {0x7E, 0x7E, 0x12, 0x12, 0x1E, 0x0C},    // \'p\'')
    lines.append('        {0x3C, 0x7E, 0x42, 0x62, 0x3E, 0x5C},    // \'q\'')
    lines.append('        {0x7E, 0x7E, 0x12, 0x12, 0x7E, 0x6C},    // \'r\'')
    lines.append('        {0x44, 0x4E, 0x4A, 0x4A, 0x7A, 0x3A},    // \'s\'')
    lines.append('        {0x02, 0x02, 0x7E, 0x7E, 0x02, 0x02},    // \'t\'')
    lines.append('        {0x3E, 0x7E, 0x40, 0x40, 0x7E, 0x3E},    // \'u\'')
    lines.append('        {0x1E, 0x3E, 0x60, 0x60, 0x3E, 0x1E},    // \'v\'')
    lines.append('        {0x3E, 0x7E, 0x20, 0x20, 0x7E, 0x3E},    // \'w\'')
    lines.append('        {0x76, 0x76, 0x08, 0x08, 0x76, 0x76},    // \'x\'')
    lines.append('        {0x06, 0x0E, 0x78, 0x78, 0x0E, 0x06},    // \'y\'')
    lines.append('        {0x62, 0x72, 0x5A, 0x4E, 0x46, 0x00},    // \'z\'')
    lines.append('        {0x08, 0x36, 0x41, 0x00, 0x00, 0x00},    // \'{\'')
    lines.append('        {0x00, 0x00, 0x7F, 0x00, 0x00, 0x00},    // \'|\'')
    lines.append('        {0x00, 0x00, 0x41, 0x36, 0x08, 0x00},    // \'}\'')
    lines.append('        {0x04, 0x02, 0x04, 0x08, 0x04, 0x00}     // \'->\'')
    lines.append('    };')
    lines.append('#endif')

    lines.append('')
    lines.append('')

    content = '\n'.join(lines)

    os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)
    with open(OUTPUT_PATH, 'w', encoding='utf-8') as f:
        f.write(content)

    print(f'Generated: {OUTPUT_PATH}')
    print(f'File size: {len(content)} bytes')
    print('Done!')


if __name__ == '__main__':
    main()
