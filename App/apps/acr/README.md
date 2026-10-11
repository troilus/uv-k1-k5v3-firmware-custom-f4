# ACARS RX2: on-radio ACARS receiver (development build)

Receives **ACARS** (ARINC 618: **131.525 MHz AM**, MSK **2400 baud**, 1200/2400 Hz
tones) on the UV-K1 / UV-K5 v3 and shows the decoded message.

- **Receive / demod / field extraction** follow upstream's ACARS RX overlay
  (Armel F4HWN): a complex 1800 Hz mixer and a half-sine matched filter feed a
  second-order carrier PLL with a wide acquisition mode and a **data-aided bit
  clock**. That loop follows the MCU's RC oscillator (up to ~1% off) and the bit
  sampling instant, which is what makes the on-air decode reliable.
- **Error correction** is acarsdec's (Thierry Leconte): `fixprerr` repairs up to
  three odd-parity bit errors, `fixdberr` a double bit in one byte; a repair is
  accepted only when the complete 16-bit BCS becomes zero (then re-verified).
- **UI and keys** are this app's own.

The APRS RX / LBJ RX / EPIRB 406 apps provide the PA4 sampling and the
overlay-app conventions.

## 使用步骤

1. 把 VFO 手动调到 **131.525 MHz**、**AM**（ACARS 常用频率还有 129.525 /
   131.725 / 131.825 / 136.975 MHz；本 app 不会自己改频率，也不会切换调制）。
2. 进入 **F4HWN APPS → ACARS RX2**。
3. 扬声器默认关闭，按 `1` 开（原始音频连续输出）。

## 屏幕说明

顶部 1 行状态栏 + 主区 7 行（7×8=56 px，小字体 3×5、每字符 4 px，一行 32 字符）。

```
[ACARS RX2] 131.5250          ███   ← 状态栏：标题 · 接收频率 · 电池
F-GTAE AF1234 H1 1*                 ← 第0行（粗体）：机号 [航班号] Label Block ID + 标记
REQUEST ALTN RWY 26L                ← 第1~5行：报文正文（小字体，每行32字符）
...                                    （按 ↑/↓ 选报文，6/9 翻正文页）
1SPK: ON 4BL: OFF 3/8 -85           ← 底部：按键状态 · 当前/共几条 · 实时RSSI(dBm)
```

- 机号 = 航空器注册号（7 字符地址字段；6 位注册号的补位 `.` 已去掉）；`航班号`
  仅在下行报文出现（Block ID 为数字），取自地址后的 6 字符航班号字段。
- `Label` 与 `Block ID` 是 ACARS 的报文类型/分块标识。
- 行尾标记：无 = BCS 正确；`*` = 坏 BCS 已纠错修复；`!` = 修不好（仍显示）。
- **最多保留 8 条**历史报文（新的在前）；↑/↓ 选报文，6/9 翻当前正文页。
- 正文按上游解析：下行报文前面的 4 位序号 + 6 位航班号已去掉（不混进正文）；
  控制字符（CR/LF 等）显示为空格，末尾空格已裁掉。

**收到一条报文时的提示**：唤醒背光（`4` 设成常亮时解码后保持不灭；自动
熄灭模式下按收音机 `BLTime` 熄灭），并让**绿色 LED 闪两下**——亮 100 / 灭 100 /
亮 100 / 灭 100 ms（与收音机自带 A/B RX 结束闪烁的"亮 100 ms"同宽），由采样
循环里的 100 ms 节拍驱动，不阻塞解调。

## 按键操作

| 键 | 作用 |
|---|---|
| UP / DOWN | **选历史报文**：更新的 / 更旧的（最新在前，最多 8 条）；UV-K1 用左右键（`nav_dir`），每次只走一步，按住不连发 |
| `6` / `9` | **翻当前正文页**：6 上一页、9 下一页（每次 32 字符一行） |
| `1` | 扬声器开 / 关（默认关，退出保存） |
| `3` | 清空历史，回到 `WAIT` |
| `4` | 背光常亮 / 自动熄灭。ON 时每个屏刷新槽都重新武装 BLTime，所以解码唤醒后一直亮着不灭；OFF 时按收音机 `BLTime` 自动熄灭（等效收音机 F+8 的常亮功能；仅本次运行有效） |
| EXIT | 退出（回 Apps 菜单） |

## Receive path

Same hardware path as EPIRB 406 / APRS RX / LBJ RX: the RX audio reaches **PA4**
(the voice DAC pin), held at mid-scale by the MCU DAC, and is sampled on **ADC
channel 4 at 19.2 kHz** (8 samples per 2400-baud bit), timed from SysTick,
continuously. The receiver is switched to **RAW** (`reg 0x2B` / `reg 0x73`, as
EPIRB 406: 300 Hz high-pass, de-emphasis and low-pass off) and to **AM**; the AF
output is fixed to `AF_AM` (7), the output that decodes. The loader restores the
registers on exit.

## Demodulator (from upstream ACARS RX)

Per 19.2 kHz sample: an audio DC tracker removes the carrier level; the audio is
mixed with a complex **1800 Hz** local oscillator (the middle of the two tones);
once per bit (3/4 turn of a 16-bit accumulator = 8 samples) the last 16 baseband
samples are correlated with the `round(127*sin(pi*n/16))` half-sine matched
filter; the decision is a sign test on the alternating Re/Im component (MSK is
OQPSK). A second-order PLL (`pllUpdate`: proportional table + integrator, wide
acquisition before the text) tracks the carrier, and a **data-aided bit clock**
nudges the bit sampling instant from the quadrature rail of each decision. The
`SYN SYN` header is found on a 16-bit shift register in either polarity.

## Frames

`SYN SYN SOH mode addr(7) ack(1) label(2) block(1) STX … ETX/ETB BCS(2)`, bytes
LSB first with **odd parity in bit 7**, **CRC-16/KERMIT** (0x8408 reflected) over
`…+BCS` must be `0`. After SOH the offsets are `[0]` mode, `[1..7]` aircraft
address, `[8]` ack, `[9..10]` label, `[11]` block id, `[12]` STX; on a downlink
(block id is a digit) `[13..16]` are the 4-character sequence number and
`[17..22]` the 6-character flight id, text follows at `[23]`; on an uplink text
starts at `[13]`.

## Notes

- 4 KiB overlay: texts are read-only assets (`gen_assets.py`); the DSP tables are
  `const` in flash; the eight history records (text capped at 200 chars each), the
  frame buffer (240 B), the demodulator state and the scratch text live on the app
  stack.
- `APP_NAME = "ACARS RX2"`, blob `ACARSRX2.app`, built by `./compile-app.sh acr`
  (Docker) or discovered by `./compile-app.sh All`.
- The frequency is shown on the status bar only; the app does not verify the VFO
  (the API exposes no modulation getter), so tuning to 131.525 MHz AM is manual.
