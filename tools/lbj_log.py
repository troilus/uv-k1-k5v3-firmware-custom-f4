#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把对讲机里的 LBJ 车次日志（外部 flash 0x018000..0x01FFFF）导出成 CSV。

记录是什么
----------
固件（`ENABLE_FEAT_F4HWN_LBJ`）在解出一列车后，往外部 SPI flash 里追加一条
32 字节的记录。区域用的是 flashmap 里本来就留给 "RX/TX log" 的 32 KiB
（8 个 4 KiB 扇区 × 128 条），目前这块区域在软件包里没有别的用途。

每条记录只存"这列车"的信息（一次报文一条，短块 + 详情块合并成一条）：

    [0]      flags   b0..b6 = 车次/速度/公里标/车型/车号/线路/距离 有效（b7 保留）
    [1..2]   车次前缀（ASCII，如 "K"、"T"，可为空）
    [3..5]   车次数字 6 位 BCD（半字节 0xF = 缺）
    [6..7]   速度 u16（km/h，0xFFFF = 未知）
    [8..10]  公里标 u24（0.1 km，0xFFFFFF = 未知）
    [11..12] 车型码 u16（0xFFFF = 未知，见下表）
    [13..16] 车号 8 位 BCD（半字节 0xF = 缺）
    [17..20] 线路 4 个 ASCII（拼音首字母，如 "JGX"）
    [21..22] 距离 i16（0.1 km，<0 = 未知/无详情块）
    [23]     方向（0 = 未知，1 = 下行 DN，2 = 上行 UP，与机上的 ??/DN/UP 一致）
    [24..25] sequence u16（同一次开机内递增）
    [26..27] uptime u16（收到该车次时的开机秒数）
    [28..29] 保留（0xFF）
    [30]     CRC8（前 30 字节，初值 0x5A，多项式 0x31）
    [31]     commit = 0xA5（最后写，最后写入才算一条完整的记录）

固件为了省擦写，是"攒着"写的：一列车的两个块合进一条，安静 10 秒（或来了
下一列车）才落盘。所以刚收到、还没安静下来的一列车可能在 RAM 里；断电会丢
最后一条。这也是为什么导出时不需要"最后一条"的特殊处理。

用法
----
    python tools/lbj_log.py --port COM3                 # 打印成表格
    python tools/lbj_log.py --port COM3 -o lbj.csv      # 存成 CSV
    python tools/lbj_log.py --port COM3 --clear         # 清空日志
    python tools/lbj_log.py --list                      # 列出串口
    python tools/lbj_log.py --raw dump.bin              # 只把 32 KiB 原样存盘

前提：固件带外部 flash 读写命令（`ENABLE_FEAT_F4HWN_EXT_FLASH_RW`，Fusion
预设默认开），串口没被 UV Studio / K5Viewer 占用，用 K 头写频线。
"""

import argparse
import csv
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from font_write import Device, SECTOR, CHUNK  # 同一套 0x0738/0x073A 协议

# ---------------------------------------------------------------------------
# 区域与记录
# ---------------------------------------------------------------------------

LOG_BASE = 0x018000
LOG_SECTORS = 8
LOG_SIZE = LOG_SECTORS * SECTOR   # 32 KiB
REC = 32
RECS = LOG_SIZE // REC            # 1024
COMMIT = 0xA5

F_TRN, F_SPD, F_KM, F_MDL, F_LOC, F_RT, F_DST = (
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40)

DIR_NONE, DIR_DN, DIR_UP = 0, 1, 2

SPD_NONE = 0xFFFF
KM_NONE = 0xFFFFFF
MDL_NONE = 0xFFFF

# App/app/lbj_dual.c 的 type_name()，一模一样。
MODEL = {
    1: "JF", 3: "QJ", 5: "JS", 6: "KD7", 55: "LJK", 81: "DF21",
    101: "DF", 102: "DF2", 103: "DF3", 104: "DF4", 105: "DF4K", 106: "DF4C",
    107: "DF5", 108: "DF5W", 109: "DF6", 110: "DF7", 111: "DF8", 112: "DF9",
    113: "DF10", 114: "DFH1", 115: "DFH2", 116: "DFH3", 117: "DFH5", 118: "BJ",
    119: "BJW", 120: "ND2", 121: "ND3", 122: "ND4", 123: "ND5", 124: "NY5",
    125: "NY6", 126: "NY7", 127: "QY", 128: "DFH21", 129: "DF7B", 130: "DF5S",
    131: "DF7C", 132: "DF7S", 133: "GK1", 134: "GK1F", 135: "DF4E", 136: "DF7D",
    137: "GK1A", 138: "DF11", 139: "TA", 140: "DF10F", 141: "DF4D", 142: "DF8B",
    143: "DF12", 144: "DF7E", 145: "NYJ1", 146: "NZJ1", 147: "NZJ2", 148: "DF4DJ",
    149: "XSG", 150: "SZ", 151: "NJ2", 152: "DF7G", 153: "NDJ3", 156: "DF11Z",
    157: "FXN3D", 158: "DF11G", 160: "HXN3", 161: "HXN5", 162: "HXN3B",
    163: "HXN5B", 167: "FXN3B", 169: "FXN3C", 170: "FXN5C", 171: "FXN3-J",
    201: "8G", 202: "8K", 203: "6G", 204: "6K", 205: "SS1", 206: "SS3",
    207: "SS4", 208: "SS5", 209: "SS6", 210: "SS3B", 211: "SS7", 212: "SS8",
    213: "SS7B", 214: "SS7C", 215: "SS6B", 216: "SS9", 217: "SS7D", 218: "DJXM",
    219: "DJ1", 220: "DJ2", 221: "DJF", 222: "LJD", 223: "XFH", 224: "SS7E",
    225: "SS4G", 226: "SS3C", 228: "TS", 229: "DJ4HX", 230: "KTT", 231: "HXD1",
    232: "HXD2", 233: "HXD3", 234: "HXD1B", 235: "HXD2B", 236: "HXD3B",
    237: "HXD1C", 238: "HXD2C", 239: "HXD3C", 240: "HXD1D", 241: "HXD2D",
    242: "HXD3D", 243: "FXD1B", 244: "FXD2B", 245: "FXD1", 246: "FXD3",
    247: "FXD1-J", 248: "FXD3-J", 249: "KZ25TA", 251: "KZ25TB", 252: "HXD1D-J",
    254: "FXD1H", 300: "XYSZ", 301: "CRH1", 302: "CRH2", 303: "CRH3",
    305: "CRH5", 306: "CRH380A", 307: "CRH380B", 308: "CRH380C", 309: "CRH380D",
    310: "CRH6A", 311: "CR400AF", 312: "CR400BF", 313: "CR300AF", 314: "CR300BF",
    315: "CRH2E", 316: "CRH6F", 330: "CJ1", 331: "CJ2", 332: "CJ3", 333: "CJ4",
    334: "CJ5", 335: "CJ6", 400: "GCD-1000J", 403: "GX-160",
    3073: "CRH380BL", 3075: "CRH380BG", 3111: "CR400AF-Z", 3112: "CR400AF-S",
    3125: "CR400BF-G",
}


def crc8(data: bytes) -> int:
    """初值 0x5A，多项式 0x31（MSB first）——和 App/app/lbj_log.c 一致。"""
    crc = 0x5A
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x31) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def _bcd(buf: bytes, count: int):
    out = []
    for i in range(count):
        nib = (buf[i // 2] >> 4) if i % 2 == 0 else (buf[i // 2] & 0x0F)
        out.append(None if nib == 0xF else nib)
    return out


def parse_record(rec: bytes):
    """一条 32 字节记录 -> dict；空记录或校验不过返回 None。"""
    if len(rec) != REC:
        return None
    if rec[31] != COMMIT:
        return None
    if rec[30] != crc8(rec[:30]):
        return None

    flags = rec[0]
    pref = bytes(c for c in rec[1:3] if c not in (0, 0xFF)).decode("ascii", "replace")

    trn = "".join(str(d) for d in _bcd(rec[3:6], 6) if d is not None)
    loc = "".join(str(d) for d in _bcd(rec[13:17], 8) if d is not None)
    rt = bytes(c for c in rec[17:21] if c not in (0, 0xFF)).decode("ascii", "replace")

    spd = struct.unpack_from("<H", rec, 6)[0]
    km = rec[8] | (rec[9] << 8) | (rec[10] << 16)
    mdl = struct.unpack_from("<H", rec, 11)[0]
    dst = struct.unpack_from("<h", rec, 21)[0]
    dr = rec[23]
    seq = struct.unpack_from("<H", rec, 24)[0]
    uptime = struct.unpack_from("<H", rec, 26)[0]

    train = (pref + trn) if (trn or pref) else ""
    direction = {DIR_NONE: "", DIR_DN: "DN", DIR_UP: "UP"}.get(dr, f"?{dr}")

    return {
        "seq": seq,
        "uptime_s": uptime,
        "train": train,
        "direction": direction,
        "speed_kmh": "" if (not (flags & F_SPD) or spd == SPD_NONE) else spd,
        "km_post": "" if (not (flags & F_KM) or km == KM_NONE) else f"{km / 10:.1f}",
        "model": "" if (not (flags & F_MDL) or mdl == MDL_NONE)
                 else MODEL.get(mdl, str(mdl)),
        "loco_number": loc if flags & F_LOC else "",
        "route": rt if flags & F_RT else "",
        "distance_km": f"{dst / 10:.1f}" if (flags & F_DST and dst >= 0) else "",
    }


def read_zone(dev) -> bytes:
    """按 128 字节分块把整块 32 KiB 读回来。"""
    blob = bytearray()
    for off in range(0, LOG_SIZE, CHUNK):
        blob += dev.read_chunk(LOG_BASE + off, min(CHUNK, LOG_SIZE - off))
    return bytes(blob)


def clear_zone(dev):
    print(f"清空日志（{LOG_SECTORS} 个 4 KiB 扇区）...", end="", flush=True)
    for i in range(LOG_SECTORS):
        dev.erase_sector(LOG_BASE + i * SECTOR)
    print(" 完成")


# ---------------------------------------------------------------------------
# 输出
# ---------------------------------------------------------------------------

COLUMNS = ["seq", "uptime_s", "train", "direction", "speed_kmh", "km_post",
           "model", "loco_number", "route", "distance_km"]

HEAD_CN = {
    "seq": "序号", "uptime_s": "开机秒", "train": "车次", "direction": "上行下行",
    "speed_kmh": "速度", "km_post": "公里标", "model": "车型",
    "loco_number": "车号", "route": "线路", "distance_km": "距离",
}


def print_table(rows):
    head = ["seq", "sec", "train", "dir", "km/h", "km", "model", "loco", "route", "dist"]
    table = [head] + [[str(r[c]) for c in COLUMNS] for r in rows]
    widths = [max(len(line[i]) for line in table) for i in range(len(head))]
    for n, line in enumerate(table):
        print("  ".join(cell.ljust(widths[i]) for i, cell in enumerate(line)))
        if n == 0:
            print("  ".join("-" * w for w in widths))


def main():
    ap = argparse.ArgumentParser(description="导出对讲机里的 LBJ 车次日志")
    ap.add_argument("--port", help="串口，如 COM3 / /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=38400, help="波特率（默认 38400）")
    ap.add_argument("-o", "--out", help="输出 CSV 文件（默认打印表格）")
    ap.add_argument("--chinese", action="store_true", help="CSV 表头用中文")
    ap.add_argument("--raw", metavar="FILE", help="只把 32 KiB 原样存成文件，不解析")
    ap.add_argument("--clear", action="store_true", help="清空日志后退出")
    ap.add_argument("--list", action="store_true", help="列出可用串口")
    ap.add_argument("--dry-run", action="store_true", help="不连对讲机，只显示将做什么")
    ap.add_argument("--debug", action="store_true", help="打印收发帧")
    args = ap.parse_args()

    if args.list:
        import serial.tools.list_ports
        for p in serial.tools.list_ports.comports():
            print(f"{p.device}  {p.description}")
        return 0

    print(f"日志区域 : 0x{LOG_BASE:06X} .. 0x{LOG_BASE + LOG_SIZE - 1:06X}"
          f"（{LOG_SIZE // 1024} KiB，{RECS} 条 × {REC} B）")
    if args.dry_run:
        if args.clear:
            print("会擦除上面 8 个扇区。")
        elif args.raw:
            print(f"会读回 32 KiB 存到 {args.raw}。")
        else:
            print("会读回并解析成表格/CSV。")
        return 0

    if not args.port:
        ap.error("需要 --port（或用 --list 看有哪些串口）")

    dev = Device(args.port, args.baud, debug=args.debug)
    try:
        version = dev.handshake()
        print(f"已连接   : {version}")

        if args.clear:
            clear_zone(dev)
            return 0

        blob = read_zone(dev)

        if args.raw:
            with open(args.raw, "wb") as f:
                f.write(blob)
            print(f"原始数据 : {args.raw}（{len(blob)} 字节）")
            return 0

        rows = [r for r in (parse_record(blob[i:i + REC]) for i in range(0, len(blob), REC))
                if r is not None]
        rows.sort(key=lambda r: r["seq"])

        print(f"记录条数 : {len(rows)}")
        if not rows:
            print("日志是空的（LBJLOG 还没打开，或还没解到车次）。")
            return 0

        if args.out:
            with open(args.out, "w", newline="", encoding="utf-8-sig") as f:
                w = csv.writer(f)
                w.writerow([HEAD_CN[c] if args.chinese else c for c in COLUMNS])
                for r in rows:
                    w.writerow([r[c] for c in COLUMNS])
            print(f"CSV      : {args.out}")
        else:
            print()
            print_table(rows)
            print("\n提示：加 -o name.csv 可以存成 CSV（Excel 可直接打开）。")
        return 0
    finally:
        dev.close()


if __name__ == "__main__":
    sys.exit(main())
