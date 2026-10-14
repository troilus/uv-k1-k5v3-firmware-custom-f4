#!/usr/bin/env python3
# Man100 ("是男人就下100层") read-only assets: texts, decimal places, the
# sprite sheet (column-major, LSB = top row, read by blit() at draw time) and
# the 128x64 title screen.
#
#   ./gen_assets.py man100_assets.bin man100_assets.h [preview.pgm]
import os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from app_assets import Assets
from app_art import Canvas

SPRITES = [
    ("SPR_MAN",   [0x00,0x88,0xE4,0x1F,0x1F,0xE4,0x88,0x00]),  # the falling man
    ("SPR_BOOM",  [0x00,0x52,0x34,0x08,0x34,0x4A,0x08,0x00]),
    ("SPR_TOOTH", [0x03,0x07,0x0F,0x07,0x03,0x01,0x01]),       # ceiling spike (7 px)
    ("SPR_SPIKE", [0x0C,0x0E,0x0F,0x0E,0x0C,0x08,0x08]),       # spiked-floor tooth period
]

def pill(cv, x, y, w):
    """A safe platform as drawn by the game: hollow 3-row pill, rounded ends."""
    for k in range(w):
        v = 0x02 if k in (0, w - 1) else 0x07 if k in (1, w - 2) else 0x05
        for bit in range(3):
            if v >> bit & 1:
                cv.set(x + k, y + bit)

def title_screen(sprites):
    """A big MAN logo over a shaft: ceiling teeth, the man mid-fall and two
    floors to drop onto (bottom page stays blank for the blinking prompt)."""
    cv = Canvas()
    cv.word("MAN", 2, kx=1.5, ky=1.1)
    tooth = sprites["SPR_TOOTH"]
    for x in range(0, 128, 7):                     # spiked ceiling
        cv.sprite(x, 30, [tooth[k % 7] for k in range(7)])
    cv.sprite(60, 36, sprites["SPR_MAN"])          # the man falling
    pill(cv, 8, 46, 40)                            # a floor to land on
    pill(cv, 78, 52, 40)
    return cv

preview_path = sys.argv.pop(3) if len(sys.argv) == 4 else None
a = Assets("MAN100")
a.text("T_GAME_OVER", "GAME OVER")
a.text("T_WIN", "YOU WIN!")
a.text("T_PAUSE", "PAUSE")
a.text("T_PRESS", "PRESS MENU")
a.text("T_FLOOR", "F")
# 10^4 first: a value of w digits is capped below PLACE[4 - w] (no division).
a.u16("PLACE", [10000, 1000, 100, 10, 1])
# New-game values of the app's struct globals from `mode` on, in their order
# (the app checks the length with a static assert): mode (ST_PLAY = 0),
# paused, riseAcc, lastX, nextX, spikeAllowed, rows, then the tick countdowns
# boom, banner and wait.
a.u8("NEW_GAME", [0, 0, 0, 44, 44, 0, 0, 0, 0, 40])
for name, cols in SPRITES:
    a.u8(name, cols)
title = title_screen(dict(SPRITES))
a.raw("ART_TITLE", title.pages())
a.main()
if preview_path:
    title.preview(preview_path)
