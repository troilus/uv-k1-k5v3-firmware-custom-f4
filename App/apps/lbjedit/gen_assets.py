#!/usr/bin/env python3
"""LBJ Edit texts: the title, the hints and the menu of the five position slots.

There is no config-proxy descriptor here: this app's cfg_load/cfg_save are its
own store (the bytes the resident LBJ popup reads), not another app's asset."""
import os, sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from app_assets import Assets

UI = [
    ("T_TITLE", "LBJ EDIT"),
    ("T_NONE", "--"),
    ("T_INVALID", "Invalid value"),
    ("T_EMPTY", "Slot not set"),
    ("T_HINT_INDEX", "MENU=EDIT EXIT=SAVE"),
    ("T_HINT_EDIT", "0-9 SET MENU=OK EXIT=UNDO"),
]

# A latitude and a longitude per slot, then the picker the popup reads.
MENU = ["LAT 1", "LON 1", "LAT 2", "LON 2", "LAT 3",
        "LON 3", "LAT 4", "LON 4", "LAT 5", "LON 5", "PRIMARY"]
MENU_STRIDE = 8          # "PRIMARY" and its NUL

a = Assets("LBJEDIT")
ui_size = 0
for name, value in UI:
    pad = -(len(value) + 1) % 4
    a.text(name, value + "\0" * pad)
    ui_size += len(value) + 1 + pad
a.table("T_MENU", MENU, stride=MENU_STRIDE)
ui_size += len(MENU) * MENU_STRIDE
a.const("UI_SIZE", ui_size)
a.const("T_TITLE_CHARS", len("LBJ EDIT"))
a.const("T_MENU_COUNT", len(MENU))
a.const("T_PRIMARY_INDEX", MENU.index("PRIMARY"))

if __name__ == "__main__":
    a.main()
