#!/usr/bin/env python3
# ACARS RX read-only assets: the screen texts, read in ONE asset_read into a
# word-aligned stack block by draw(), so every label costs a 2-byte sp-relative
# add instead of a literal-pool word (keeps the 4 KiB overlay free for code).
# ACARS carries ASCII only, so no GB2312 texts and no language toggle.
#
#   ./gen_assets.py acr_assets.bin acr_assets.h
import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

TITLE = "ACARS RX"

UI = [
    ("T_TITLE", TITLE),
    ("T_WAIT",  "WAIT"),
    # footer: "1SPK:ON 4BL:OFF x/y -rr"
    ("T_SPK",   "1SPK:"),
    ("T_BL",    "4BL:"),
    ("T_ON",    "ON"),
    ("T_OFF",   "OFF"),
]

a = Assets("ACR")
ui_size = 0
for name, s in UI:
    nb = len(s.encode("ascii"))
    pad = (-(nb + 1)) % 4
    a.text(name, s + "\0" * pad)
    ui_size += nb + 1 + pad

a.const("T_TITLE_CHARS", len(TITLE))
a.const("UI_SIZE", ui_size)

if __name__ == "__main__":
    a.main()
