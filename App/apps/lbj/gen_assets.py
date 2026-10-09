#!/usr/bin/env python3
# LBJ RX read-only assets: the screen texts. Read in ONE asset_read into a
# word-aligned stack block by draw(), so every label costs a 2-byte sp-relative
# add instead of a literal-pool word (keeps the 4 KiB overlay free for code).
#
#   ./gen_assets.py lbj_assets.bin lbj_assets.h
import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

TITLE = "LBJ RX"
WAIT = "WAIT"

# Only the labels the two pages actually read (SUM + PDU).
UI = [
    ("T_TITLE",  TITLE),
    ("T_WAIT",   WAIT),
    ("T_SUM",    "SUM"),
    ("T_PDU",    "PDU"),
    ("T_SPD",    "Sp "),
    ("T_KM",     "Km "),
    ("T_LBJ",    "LBJ"),
    ("T_NOLBJ",  "--"),
    ("T_DN",     "DN"),
    ("T_UP",     "UP"),
    ("T_UNK",    "??"),
]

a = Assets("LBJ")
ui_size = 0
for name, s in UI:
    pad = -(len(s) + 1) % 4
    a.text(name, s + "\0" * pad)
    ui_size += len(s) + 1 + pad

a.const("T_TITLE_CHARS", len(TITLE))
a.const("UI_SIZE", ui_size)

if __name__ == "__main__":
    a.main()
