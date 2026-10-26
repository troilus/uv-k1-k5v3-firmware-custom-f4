#!/usr/bin/env python3
# -*- coding: utf-8 -*-


import argparse
import os
import struct
import sys
import time
import zlib

# ---------------------------------------------------------------------------
# 协议常量：与 App/app/uart.c 一一对应，不要随意改
# ---------------------------------------------------------------------------

FRAME_HEAD = b"\xab\xcd"
FRAME_TAIL = b"\xdc\xba"

# App/app/uart.c 里的 Obfuscation[]
OBF = bytes([
    0x16, 0x6C, 0x14, 0xE6, 0x2E, 0x91, 0x0D, 0x40,
    0x21, 0x35, 0xD5, 0x40, 0x13, 0x03, 0xE9, 0x80,
])

CMD_INFO = 0x0514      # 握手：把本次会话的 timestamp 交给固件
REPLY_INFO = 0x0515

CMD_READ = 0x0738      # 按物理地址读外部 flash
REPLY_READ = 0x0739
CMD_ERASE = 0x073A     # 擦除一个 4 KiB 扇区
REPLY_ERASE = 0x073B
CMD_WRITE = 0x073C     # 写一段（扇区必须先擦过）
REPLY_WRITE = 0x073D
CMD_CRC = 0x073E       # 算一段的 CRC-32
REPLY_CRC = 0x073F

# App/driver/mb_flash.h 的错误码
MB_OK = 0
MB_ERR = {
    1: "没有有效的槽位头（magic 不对）",
    2: "槽位头版本太新",
    3: "镜像没有标记为完成（COMMITTED）",
    4: "地址或长度不合法",
    5: "镜像 CRC32 不匹配",
    6: "外部 flash 读写超时（SPI）",
    7: "槽位/ bank 号越界",
    8: "写被拒绝：timestamp 不匹配",
    9: "恢复用的 RAM 代码校验失败",
    10: "要写的区域是受保护数据（校准区）",
}

SECTOR = 0x1000            # 4 KiB
CHUNK = 128                # 单条写命令最多 128 字节（PY25Q16_RAW_CHUNK_SIZE）

FONT16_SIZE = 0x3FE40      # 261,696 字节
FONT8_SIZE = 0xFF90        #  65,424 字节
FONT16_BASE = 0x18E000
FONT8_BASE = 0x1CE000

# 尾部空区，越界就是固件/脚本的 bug
TAIL_FREE_START = 0x162000
TAIL_FREE_END = 0x1DE000   # 0x1DE000 起是 Sonic 4K 互操作保留区


# ---------------------------------------------------------------------------
# 组帧与解帧
# ---------------------------------------------------------------------------

def crc16(data: bytes) -> int:
    """CRC-16/XMODEM，同 App/driver/crc.c。"""
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc & 0xFFFF


def build_frame(cmd_id: int, data: bytes = b"") -> bytes:
    """AB CD | size(2) | 混淆后的 [ID(2) Size(2) Data...] + CRC(2) | DC BA"""
    payload = struct.pack("<HH", cmd_id, len(data)) + data
    body = payload + struct.pack("<H", crc16(payload))
    body = bytes(b ^ OBF[i % 16] for i, b in enumerate(body))
    return FRAME_HEAD + struct.pack("<H", len(payload)) + body + FRAME_TAIL


class Device:
    """极简串口会话：发命令、收应答，带帧同步。"""

    def __init__(self, port: str, baud: int, debug: bool = False):
        import serial  # 延迟导入，--dry-run 时不需要 pyserial

        self.ser = serial.Serial(port, baudrate=baud, timeout=0.2, write_timeout=5)
        self.rx = bytearray()
        self.timestamp = 0
        self.debug = debug

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass

    def _write(self, frame: bytes):
        if self.debug:
            print("  TX " + frame.hex(" "))
        self.ser.write(frame)
        self.ser.flush()

    def send(self, cmd_id: int, data: bytes = b""):
        self._write(build_frame(cmd_id, data))

    def _pump(self, deadline: float):
        while time.time() < deadline:
            chunk = self.ser.read(256)
            if chunk:
                self.rx.extend(chunk)
                if self.debug:
                    print("  RX " + chunk.hex(" "))

    def read_reply(self, expect_id: int, timeout: float = 3.0, settle: float = 0.06):
        """等到 expect_id 的应答。返回应答的数据段（不含 4 字节头）。

        收到别的应答（例如握手后的版本回包）会被丢掉，不算失败。
        """
        deadline = time.time() + timeout
        while True:
            # 尝试从缓冲区里取一个完整帧
            while True:
                idx = self.rx.find(FRAME_HEAD)
                if idx < 0:
                    if len(self.rx) > 1:
                        del self.rx[:-1]
                    break
                if len(self.rx) < idx + 4:
                    break
                size = self.rx[idx + 2] | (self.rx[idx + 3] << 8)
                total = idx + 4 + size + 2 + 2
                if len(self.rx) < total:
                    break
                if bytes(self.rx[total - 2:total]) != FRAME_TAIL:
                    del self.rx[:idx + 1]
                    continue
                payload = bytes(b ^ OBF[i % 16]
                                for i, b in enumerate(self.rx[idx + 4:idx + 4 + size]))
                # 应答帧没有 CRC；负载后面那 2 字节是固件写死的"混淆尾巴"
                # （uart.c SendReply：Obfuscation[(Size+k)%16] ^ 0xFF），拿它当
                # 一个廉价的完整性检查，不匹配说明帧错位/丢字节。
                pad0 = self.rx[idx + 4 + size] ^ OBF[(size + 0) % 16] ^ 0xFF
                pad1 = self.rx[idx + 4 + size + 1] ^ OBF[(size + 1) % 16] ^ 0xFF
                del self.rx[:total]
                if pad0 != 0 or pad1 != 0:
                    continue
                reply_id = struct.unpack_from("<H", payload, 0)[0]
                return reply_id, payload[4:]

            if time.time() > deadline:
                raise TimeoutError("等应答超时（对讲机没回话？串口被占用？）")
            self._pump(min(deadline, time.time() + settle))

    def handshake(self) -> str:
        """0x0514：把 timestamp 交给固件，之后所有写/擦命令都要带上它。"""
        self.timestamp = int(time.time()) & 0xFFFFFFFF or 0x12345678
        self.send(CMD_INFO, struct.pack("<I", self.timestamp))
        try:
            reply_id, data = self.read_reply(REPLY_INFO)
        except TimeoutError:
            raise SystemExit(
                "握手失败：没有收到 0x0515 应答。\n"
                "请确认：1) 固件是多系统版本（0x0514 已启用）；2) 串口没被 UV Studio/K5Viewer 占用；\n"
                "        3) 用的是 K 头写频线。"
            )
        version = data[:16].split(b"\0")[0].decode("ascii", "replace")
        return version

    # ---- 具体命令 -------------------------------------------------------

    def erase_sector(self, addr: int):
        self.send(CMD_ERASE, struct.pack("<II", addr, self.timestamp))
        reply_id, data = self.read_reply(REPLY_ERASE, timeout=5.0)
        if reply_id != REPLY_ERASE or len(data) < 5:
            raise IOError("擦除应答异常")
        status = data[4]
        if status != MB_OK:
            raise IOError(f"擦除 0x{addr:06X} 失败：{MB_ERR.get(status, '未知错误')}")

    def write_chunk(self, addr: int, blob: bytes):
        self.send(CMD_WRITE, struct.pack("<IHI", addr, len(blob), self.timestamp) + blob)
        reply_id, data = self.read_reply(REPLY_WRITE, timeout=5.0)
        if reply_id != REPLY_WRITE or len(data) < 7:
            raise IOError("写应答异常")
        status = data[6]
        if status != MB_OK:
            raise IOError(f"写 0x{addr:06X} 失败：{MB_ERR.get(status, '未知错误')}")

    def crc32(self, addr: int, length: int) -> int:
        self.send(CMD_CRC, struct.pack("<III", addr, length, self.timestamp))
        reply_id, data = self.read_reply(REPLY_CRC, timeout=5.0)
        if reply_id != REPLY_CRC or len(data) < 13:
            raise IOError("CRC 应答异常")
        status = data[12]
        if status != MB_OK:
            raise IOError(f"CRC 0x{addr:06X} 失败：{MB_ERR.get(status, '未知错误')}")
        return struct.unpack_from("<I", data, 8)[0]

    def read_chunk(self, addr: int, length: int) -> bytes:
        self.send(CMD_READ, struct.pack("<IHI", addr, length, self.timestamp))
        reply_id, data = self.read_reply(REPLY_READ, timeout=5.0)
        if reply_id != REPLY_READ or len(data) < 7:
            raise IOError("读应答异常")
        status = data[6]
        if status != MB_OK:
            raise IOError(f"读 0x{addr:06X} 失败：{MB_ERR.get(status, '未知错误')}")
        return data[7:7 + length]


# ---------------------------------------------------------------------------
# 写一个字库
# ---------------------------------------------------------------------------

def human(n: int) -> str:
    return f"{n:,}".replace(",", " ")


def load_font(path: str):
    """读入 .fon，返回 (类型, 字模数据, 提示)。

    官方 .fon（FontTool 生成 / 仓库样本）末尾常带一个 CRLF（0D 0A），
    但字模本体仍然正好是 261,696 / 65,424 字节——多出来的只是行尾换行。
    只有"去掉末尾换行后尺寸正好对上"时才忽略它，别的尺寸一律拒绝：
    宁可让用户重新导出，也不能写半截字库进 flash。
    """
    if not os.path.isfile(path):
        raise SystemExit(f"找不到文件：{path}")

    with open(path, "rb") as f:
        blob = f.read()

    note = ""
    if len(blob) not in (FONT16_SIZE, FONT8_SIZE):
        trimmed = blob.rstrip(b"\r\n")
        if len(trimmed) in (FONT16_SIZE, FONT8_SIZE) and len(trimmed) < len(blob):
            note = f"（末尾 {len(blob) - len(trimmed)} 个换行字节已忽略）"
            blob = trimmed

    if len(blob) == FONT16_SIZE:
        return "16×16", blob, note
    if len(blob) == FONT8_SIZE:
        return "8×8", blob, note

    raise SystemExit(
        f"{path} 大小是 {human(len(blob))} 字节，既不是 16×16（{human(FONT16_SIZE)}）"
        f"也不是 8×8（{human(FONT8_SIZE)}），拒绝写入。"
    )


def write_font(dev: Device, name: str, path: str, base: int, blob: bytes,
               verify: bool = True):
    print(f"\n== {name} ==")
    print(f"  文件   : {path}（{human(len(blob))} 字节）")
    print(f"  目标   : 0x{base:06X} .. 0x{base + len(blob) - 1:06X}")

    if base < TAIL_FREE_START or base + len(blob) > TAIL_FREE_END:
        raise SystemExit("目标地址不在固件预留的尾部空区里，已中止（可能是脚本常量被改坏了）。")

    # 1) 擦
    sectors = (len(blob) + SECTOR - 1) // SECTOR
    print(f"  擦除   : {sectors} 个 4 KiB 扇区 ...", end="", flush=True)
    for i in range(sectors):
        dev.erase_sector(base + i * SECTOR)
    print(" 完成")

    # 2) 写
    written = 0
    t0 = time.time()
    for off in range(0, len(blob), CHUNK):
        dev.write_chunk(base + off, blob[off:off + CHUNK])
        written += len(blob[off:off + CHUNK])
        pct = written * 100 // len(blob)
        print(f"\r  写入   : {pct:3d}%  ({human(written)}/{human(len(blob))} 字节)", end="", flush=True)
    dt = max(time.time() - t0, 0.001)
    print(f" 完成（{dt:.1f} s，{human(int(len(blob) / dt))} 字节/秒）")

    # 3) CRC + 回读
    if verify:
        print("  校验   : ", end="", flush=True)
        bad = 0
        for off in range(0, len(blob), SECTOR):
            part = blob[off:off + SECTOR]
            onflash = dev.crc32(base + off, len(part))
            mine = zlib.crc32(part) & 0xFFFFFFFF
            if onflash != mine:
                bad += 1
                print(f"\n    0x{base + off:06X} CRC 不符（机内 0x{onflash:08X} / 应为 0x{mine:08X}）")
        if bad:
            raise SystemExit("校验失败：请确认固件已刷成带多系统的版本，然后重跑本脚本。")
        print("CRC 全部一致", end="")

        # 再抽读几段字节，防 CRC 命令本身出问题
        probes = [0, len(blob) // 2, max(0, len(blob) - CHUNK)]
        for off in probes:
            blk = dev.read_chunk(base + off, min(CHUNK, len(blob) - off))
            if blk != blob[off:off + len(blk)]:
                raise SystemExit(f"回读不符：0x{base + off:06X}。")
        print("，回读一致")
    print("  OK")


# ---------------------------------------------------------------------------

def list_ports():
    try:
        from serial.tools import list_ports as lp
    except ImportError:
        raise SystemExit("没有装 pyserial，请先运行：pip install pyserial")
    ports = list(lp.comports())
    if not ports:
        print("没找到串口。")
    for p in ports:
        print(f"{p.device:10s} {p.description}")
    return ports


def main():
    ap = argparse.ArgumentParser(
        description="把 16×16 / 8×8 中文字库写到多系统固件的新地址",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("用法\n----")[-1],
    )
    ap.add_argument("files", nargs="*", help="16x16.fon 和/或 8x8.fon")
    ap.add_argument("--port", help="串口，例如 COM3 或 /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=38400, help="波特率（默认 38400）")
    ap.add_argument("--list", action="store_true", help="列出可用串口")
    ap.add_argument("--dry-run", action="store_true", help="只检查文件，不连对讲机")
    ap.add_argument("--no-verify", action="store_true", help="跳过写后校验")
    ap.add_argument("--yes", action="store_true", help="不问就写（自动化用）")
    ap.add_argument("--debug", action="store_true", help="打印收发原始字节")
    args = ap.parse_args()

    if args.list:
        list_ports()
        return 0

    if not args.files:
        ap.print_help()
        return 1

    # 按文件大小判断是哪个字库，避免手滑把大字体写到小字体地址
    jobs = []
    for path in args.files:
        name, blob, note = load_font(path)
        base = FONT16_BASE if name == "16×16" else FONT8_BASE
        jobs.append((name, path, base, blob, note))

    print("将要写入：")
    for name, path, base, blob, note in jobs:
        print(f"  {name:6s} {path} -> 0x{base:06X}（{human(len(blob))} 字节）{note}")

    if args.dry_run:
        print("\n--dry-run：只检查，没有连接对讲机。")
        return 0

    if not args.port:
        raise SystemExit("需要 --port（用 --list 看可用串口）")

    if not args.yes:
        print("\n注意：写的时候不要断电、不要拔线。写错只会影响字库，不影响固件本身。")
        if input("现在开始？[y/N] ").strip().lower() not in ("y", "yes"):
            print("已取消。")
            return 1

    dev = Device(args.port, args.baud, args.debug)
    try:
        version = dev.handshake()
        print(f"\n已连接：固件 {version or '(未知版本)'}（timestamp 0x{dev.timestamp:08X}）")
        for name, path, base, blob, note in jobs:
            write_font(dev, name, path, base, blob, verify=not args.no_verify)
    except (IOError, TimeoutError) as e:
        print(f"\n失败：{e}")
        return 2
    finally:
        dev.close()

    print("\n全部完成。关机再开机，中文就应该正常了。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
