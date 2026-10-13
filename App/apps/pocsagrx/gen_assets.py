#!/usr/bin/env python3
# POCSAG RX read-only assets: the screen texts, read in ONE asset_read into a
# word-aligned stack block by draw(), so every label costs a 2-byte sp-relative
# add instead of a literal-pool word (keeps the 4 KiB overlay free for code).
# All-ASCII: no Chinese font, no locomotive table.
#
#   ./gen_assets.py pocsagrx_assets.bin pocsagrx_assets.h
import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

TITLE = "POCSAG RX"

UI = [
    ("T_TITLE", TITLE),
    ("T_WAIT",  "WAITING..."),
    ("T_MSG",   "MSG"),
    ("T_RAW",   "RAW"),
    ("T_NUM",   "NUM"),
    ("T_ALP",   "ALP"),
    ("T_L3",    "3="),
    ("T_L5",    "5="),
    ("T_SPK",   "1="),
    ("T_ON",    "ON"),
    ("T_OFF",   "OFF"),
]

a = Assets("POCSAGRX")
ui_size = 0
for name, s in UI:
    pad = (-(len(s) + 1)) % 4
    a.text(name, s + "\0" * pad)
    ui_size += len(s) + 1 + pad

a.const("T_TITLE_CHARS", len(TITLE))
a.const("UI_SIZE", ui_size)

if __name__ == "__main__":
    a.main()
