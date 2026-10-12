#!/usr/bin/env python3
# AIS RX read-only assets: the screen labels, read in ONE asset_read into a
# word-aligned stack block by draw(), so every label costs a 2-byte sp-relative
# add instead of a literal-pool word (keeps the 4 KiB overlay free for code).
# AIS ship names/destinations are ASCII six-bit text, so no GB2312 assets.
#
#   ./gen_assets.py aisrx_assets.bin aisrx_assets.h
import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

TITLE = "AIS RX"

UI = [
    ("T_TITLE", TITLE),
    ("T_WAIT",  "WAIT"),
    ("T_MMSI",  "MMSI "),
    ("T_SOG",   "SOG "),
    ("T_COG",   "COG "),
    ("T_LAT",   "LAT "),
    ("T_LON",   "LON "),
    ("T_NAME",  "NAME "),
    ("T_DEST",  "DEST "),
    ("T_KN",    "KN"),
    ("T_NA",    "N/A"),
]

a = Assets("AISRX")
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
