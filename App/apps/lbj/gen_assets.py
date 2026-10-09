#!/usr/bin/env python3
# LBJ RX read-only assets: the screen texts, read in ONE asset_read into a
# word-aligned stack block by draw(), so every label costs a 2-byte sp-relative
# add instead of a literal-pool word (keeps the 4 KiB overlay free for code).
# UI strings are stored as GB2312 so the firmware renders any Chinese byte pairs
# through its built-in 8x8 font.
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
    ("T_SPD",    "SPEED "),   # 1234000 row1 prefix
    ("T_KMH",    " km/h"),    # 1234000 row1 suffix
    ("T_SPD2",   "SPD "),     # 1234002 row3 prefix (speed + km on one line)
    ("T_KM",     "KM "),
    ("T_LBJ",    "LBJ"),
    ("T_NOLBJ",  "--"),
    ("T_DN",     "DN"),
    ("T_UP",     "UP"),
    ("T_UNK",    "??"),
    ("T_ROUTE",  "线路 "),
    ("T_LON",    "LON "),
    ("T_LAT",    "LAT "),
    # footer flags (bottom status bar): "5EN 4BL:ON 1SPK:ON x/y -rr"
    ("T_L5",     "5"),
    ("T_EN",     "EN"),
    ("T_ZH",     "ZH"),
    ("T_BL",     "4BL:"),
    ("T_SPK",    "1SPK:"),
    ("T_ON",     "ON"),
    ("T_OFF",    "OFF"),
]

# Locomotive model code (4-digit BCD, decoded from nibbles 0-3) -> name. Only
# the Chinese name is stored (GB2312); the numeric code is the ASCII fallback in
# the app. Sorted below for the runtime binary search.
TYPES = [
    (1, "解放"), (3, "前进"), (5, "建设"), (6, "KD7"), (55, "蓝箭控车"),
    (81, "东风21"),
    (101, "东风"), (102, "东风2"), (103, "东风3"), (104, "东风4"),
    (105, "东风4客"), (106, "东风4C"), (107, "东风5"), (108, "东风5宽"),
    (109, "东风6"), (110, "东风7"), (111, "东风8"), (112, "东风9"),
    (113, "东风10"), (114, "东方红1"), (115, "东方红2"), (116, "东方红3"),
    (117, "东方红5"), (118, "北京"), (119, "北京宽"), (120, "ND2"),
    (121, "ND3"), (122, "ND4"), (123, "ND5"), (124, "NY5"), (125, "NY6"),
    (126, "NY7"), (127, "轻油"), (128, "东方红21"), (129, "东风7B"),
    (130, "东风5S"), (131, "东风7C"), (132, "东风7S"), (133, "工矿1"),
    (134, "工矿1F"), (135, "东风4E"), (136, "东风7D"), (137, "工矿1A"),
    (138, "东风11"), (139, "天安"), (140, "东风10F"), (141, "东风4D"),
    (142, "东风8B"), (143, "东风12"), (144, "东风7E"), (145, "NYJ1"),
    (146, "NZJ1"), (147, "NZJ2"), (148, "东风4DJ"), (149, "新曙光"),
    (150, "神州"), (151, "NJ2"), (152, "东风7G"), (153, "NDJ3"),
    (156, "东风11Z"), (157, "FXN3D"), (158, "东风11G"), (160, "HXN3"),
    (161, "HXN5"), (162, "HXN3B"), (163, "HXN5B"), (167, "FXN3B"),
    (169, "FXN3C"), (170, "FXN5C"), (171, "FXN3-J"),
    (201, "8G"), (202, "8K"), (203, "6G"), (204, "6K"), (205, "韶山1"),
    (206, "韶山3"), (207, "韶山4"), (208, "韶山5"), (209, "韶山6"),
    (210, "韶山3B"), (211, "韶山7"), (212, "韶山8"), (213, "韶山7B"),
    (214, "韶山7C"), (215, "韶山6B"), (216, "韶山9"), (217, "韶山7D"),
    (218, "DJ熊猫"), (219, "DJ1"), (220, "DJ2"), (221, "DJF"),
    (222, "蓝箭动车"), (223, "先锋号"), (224, "韶山7E"), (225, "韶山4G"),
    (226, "韶山3C"), (228, "天梭"), (229, "DJ4和谐"), (230, "KTT"),
    (231, "HXD1"), (232, "HXD2"), (233, "HXD3"), (234, "HXD1B"),
    (235, "HXD2B"), (236, "HXD3B"), (237, "HXD1C"), (238, "HXD2C"),
    (239, "HXD3C"), (240, "HXD1D"), (241, "HXD2D"), (242, "HXD3D"),
    (243, "FXD1B"), (244, "FXD2B"), (245, "FXD1"), (246, "FXD3"),
    (247, "FXD1-J"), (248, "FXD3-J"), (249, "KZ25TA"), (251, "KZ25TB"),
    (252, "HXD1D-J"), (254, "FXD1H"),
    (300, "雪域神州"), (301, "CRH1"), (302, "CRH2"), (303, "CRH3"),
    (305, "CRH5"), (306, "CRH380A"), (307, "CRH380B"), (3073, "CRH380BL"),
    (3075, "CRH380BG"), (308, "CRH380C"), (309, "CRH380D"), (310, "CRH6A"),
    (311, "CR400AF"), (3111, "CR400AF-Z"), (3112, "CR400AF-S"),
    (312, "CR400BF"), (3125, "CR400BF-G"), (313, "CR300AF"), (314, "CR300BF"),
    (315, "CRH2E"), (316, "CRH6F"),
    (330, "CJ1"), (331, "CJ2"), (332, "CJ3"), (333, "CJ4"), (334, "CJ5"),
    (335, "CJ6"),
    (400, "GCD-1000J"), (403, "GX-160"),
]

a = Assets("LBJ")
ui_size = 0
for name, s in UI:
    nb = len(s.encode("gb2312"))
    pad = (-(nb + 1)) % 4
    a.text(name, s + "\0" * pad, enc="gb2312")
    ui_size += nb + 1 + pad

a.const("T_TITLE_CHARS", len(TITLE))
a.const("UI_SIZE", ui_size)


def abbr(name):
    """Chinese model name -> ASCII abbreviation for radios without a font:
    东风4C -> DF4C, 韶山7E -> SS7E, 东方红21 -> DFH21, 北京宽 -> BJW,
    DJ4和谐 -> DJ4HX; names that are already ASCII pass through unchanged."""
    rules = [
        ("雪域神州", "XYSZ"), ("东方红", "DFH"), ("东风", "DF"),
        ("韶山", "SS"), ("蓝箭控车", "LJK"), ("蓝箭动车", "LJD"),
        ("解放", "JF"), ("前进", "QJ"), ("建设", "JS"), ("北京", "BJ"),
        ("工矿", "GK"), ("天安", "TA"), ("天梭", "TS"), ("神州", "SZ"),
        ("新曙光", "XSG"), ("先锋号", "XFH"), ("轻油", "QY"),
        ("熊猫", "XM"), ("和谐", "HX"), ("客", "K"), ("宽", "W"),
    ]
    out = name
    for cn, en in rules:
        out = out.replace(cn, en)
    return out


TYPES.sort(key=lambda t: t[0])
names = [name for _, name in TYPES]
ens = [abbr(name) for name in names]
width = max(max(len(n.encode("gb2312")) for n in names),
            max(len(e) for e in ens)) + 1
a.u16("TY_CODE", [code for code, _ in TYPES])
a.table("TY_NAME", names, stride=width, enc="gb2312")
a.table("TY_EN", ens, stride=width)
a.const("TY_COUNT", len(TYPES))

if __name__ == "__main__":
    a.main()
