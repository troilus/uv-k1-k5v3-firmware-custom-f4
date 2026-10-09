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

# name, text (each padded to a multiple of 4 by Assets.text; UI_SIZE below sums
# the padded lengths so draw() reads the whole block at once)
UI = [
    ("T_TITLE",  TITLE),
    ("T_WAIT",   WAIT),
    ("T_DBM",    "dBm"),
    ("T_KMH",    "km/h"),
    ("T_SEP",    " "),
    # page capsules
    ("T_SUM",    "SUM"),
    ("T_PDU",    "PDU"),
    ("T_RAW",    "RAW"),
    ("T_SIG",    "SIG"),
    # LBJ fields
    ("T_TRAIN",  "Tr "),
    ("T_DIR",    "Di "),
    ("T_SPD",    "Sp "),
    ("T_KM",     "Km "),
    ("T_LOCO",   "Lo "),
    ("T_ROUTE",  "Rt "),
    ("T_LBJ",    "LBJ"),
    ("T_NOLBJ",  "--"),
    # PDU list
    ("T_ADDR",   "A"),
    ("T_FUNC",   "F"),
    ("T_ERR",    "E"),
    ("T_LEN",    "L"),
    # stats
    ("T_SYNC",   "sync "),
    ("T_WORD",   "word "),
    ("T_FIX",    "fix "),
    ("T_BAD",    "bad "),
    ("T_MSGS",   "msg "),
    ("T_UP",     "up "),
    ("T_DN",     "dn "),
    ("T_PP",     "pp "),
    ("T_DC",     "dc "),
    ("T_PH",     "ph "),
    ("T_POL",    "pol "),
    ("T_RSSI",   "RSSI "),
]

a = Assets("LBJ")
ui_size = 0
for name, s in UI:
    pad = -(len(s) + 1) % 4
    a.text(name, s + "\0" * pad)
    ui_size += len(s) + 1 + pad

a.const("T_TITLE_CHARS", len(TITLE))
a.const("T_WAIT_CHARS", len(WAIT))
a.const("UI_SIZE", ui_size)

if __name__ == "__main__":
    a.main()
