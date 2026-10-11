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
3. 扬声器默认关闭，按 `1` 开（原始音频连续输出）；按 `*` 在小字体（32 字符/行）
   与大字体（18 字符/行）之间切换正文。

## 屏幕说明 / Screen layout

顶部 1 行状态栏 + 粗体标题 1 行 + 正文最多 5 行 + 底部 1 行（每行 8 px）。

```
[ACARS RX2] 131.5250          ███   ← 状态栏：标题 · 接收频率 · 电池
F-GTAE AF1234 H1 1                  ← 标题（粗体）：机号 [航班号] Label Block ID
REQUEST ALTN RWY 26L                ← 正文第 1~5 行（每行 32 或 18 字符）
...                                    （↑/↓ 选报文，6/9 翻页，* 换字号）
1SPK: ON 4BL: OFF 3/8 -85        *   ← 底部：按键状态 · 当前/共几条 · RSSI；最右=状态标记
```

### 字段含义 / Field reference

| 位置 Field | 例 Example | 含义 / Meaning |
|---|---|---|
| 机号 Registration | `F-GTAE` | 航空器注册号：ACARS 地址字段的 7 个字符；6 位注册号传输时用前导 `.` 补齐，显示时去掉。 / Aircraft registration: the 7-character address field; a leading `.` padding a six-character reg is hidden. |
| 航班号 Flight id | `AF1234` | 航班号；**仅下行**出现（Block ID 为数字），取地址后的 6 字符字段。 / Flight id; present on **downlinks only** (block id is a digit), the 6 characters after the address. |
| Label | `H1` | 报文类型标签（2 字符）：`Q0`=链路测试、`H1`=通用/终端报文、`SA`=媒体通告、`MA`=MIAM 等。 / Two-character message-type label: `Q0` = link test, `H1` = general/terminal message, `SA` = media advisory, `MA` = MIAM, … |
| Block ID | `1` | 分块/方向标识（1 字符）：数字 `0`~`9`=**下行**（飞机→地面）；字母=**上行**（地面→飞机）。 / Block/direction id: a digit `0`–`9` = **downlink** (air→ground), a letter = **uplink** (ground→air). |
| 正文 Message | `REQUEST ALTN RWY 26L` | 报文正文；下行前面的 4 位序号 + 6 位航班号已去掉，控制字符转空格，行尾空格裁掉。 / Payload; the 4-char sequence + 6-char flight id prefix of a downlink is stripped, control characters become spaces, trailing blanks trimmed. |
| 右下角标记 Status | `*` / `!` / 空 empty | 空=BCS 正确；`*`=坏 BCS 已纠错修复；`!`=修不好（仍显示）。 / empty = valid BCS, `*` = repaired, `!` = unrepaired (still shown). |

- `H1 1` 连读：**类型 H1、下行第 1 块**。 / `H1 1` reads as *type H1, downlink block 1*.
- 粗体标题一行最多 18 字符（7 px/字符）；7 位机号带航班号时尾部会被裁掉，但**航班号始终保留**。
- **最多保留 8 条**历史报文（新的在前）；↑/↓ 选报文，6/9 翻当前正文页。
- 标记显示在**屏幕右下角**（不在标题行）。

**收到一条报文时的提示**：唤醒背光（`4` 设成常亮时解码后保持不灭；自动
熄灭模式下按收音机 `BLTime` 熄灭），并让**绿色 LED 闪两下**——亮 100 / 灭 100 /
亮 100 / 灭 100 ms（与收音机自带 A/B RX 结束闪烁的"亮 100 ms"同宽），由采样
循环里的 100 ms 节拍驱动，不阻塞解调。

## 按键操作

| 键 | 作用 |
|---|---|
| UP / DOWN | **选历史报文**：更新的 / 更旧的（最新在前，最多 8 条）；UV-K1 用左右键（`nav_dir`），每次只走一步，按住不连发 |
| `6` / `9` | **翻当前正文页**：6 上一页、9 下一页（每次一页：小字体 32 字符、大字体 18 字符） |
| `*` | **切换正文字号**：小字体 32 字符/行 ↔ 大字体 18 字符/行 |
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

## Compared to upstream ACARS RX

Upstream (Armel F4HWN's `App/apps/acarsrx`) and this app share the same receive
path — complex 1800 Hz mixer, half-sine matched filter, second-order carrier PLL
with wide acquisition, data-aided bit clock, then the same framing and field
extraction. Everything below is what this app does differently.

**Operations**
1. The speaker is a plain continuous on/off (`1`): the PA4 audio path stays open
   while enabled; upstream opens the speaker only after a complete ACARS header.
2. Clear history is on `3` (upstream uses `2`).
3. Backlight always-on toggle on `4`: re-arms the radio's `BLTime` on every
   screen slot, so a decode wake-up stays lit; upstream has no such toggle.
4. Text is paged with `6`/`9` (one page = 32 or 18 characters); upstream scrolls
   the message with UP/DOWN.
5. Direction keys go through `nav_dir`: UP/DOWN (UV-K5) or LEFT/RIGHT (UV-K1)
   browse the history one step per press; upstream reads raw UP/DOWN.
6. No AF-cycle key: AF is fixed to `AF_AM` (7), the output that decodes; upstream
   lets `2` cycle AM/FM/RAW/USB.
7. `*` toggles the body font between the 32-character tiny font and the
   18-character normal font.

**Decoding**
8. Stronger error correction (acarsdec `fixprerr`/`fixdberr`): up to three
   odd-parity bit errors or a same-byte double bit, each accepted only after the
   whole 16-bit BCS is re-verified; upstream repairs at most two parity errors.
9. Unrepaired frames are still shown, marked `!`; upstream drops any frame whose
   BCS does not become zero.

**UI**
10. Bold header line: `aircraft address | downlink flight id | label | block id`;
    upstream shows only the address and flight, with a separate DEC/RAW indicator.
11. Per-message status mark in the bottom-right corner: nothing = clean BCS,
    `*` = repaired, `!` = unrepaired.
12. Plain-payload body in up to five rows of 32 characters; upstream reformats
    the body with decoded labels (DOWN/UP, SUB, MFI, application type) and needs
    an `F` decoded/raw toggle.
13. Live footer `1SPK:ON 4BL:OFF n/m -RSSI`; upstream shows RSSI plus RX/FIX
    counters and, while idle, ADC/SYN/CRC/PAR diagnostics (this app shows `WAIT`).

**History**
14. Eight messages kept in RAM, newest first; upstream keeps three.

**LED + screen wake**
15. On a decode the green LED double-blinks (on/off/on/off, 100 ms steps, the
    same "on 100 ms" width as the radio's own RX-end blink), driven from the
    sample loop; upstream only wakes the backlight.

**Keys**
16. `1` speaker on/off · `3` clear history · `4` backlight always-on/auto ·
    `6` previous page · `9` next page · `UP/DOWN` browse history · `*` font size ·
    `EXIT` quit.

## Notes

- 4 KiB overlay: texts are read-only assets (`gen_assets.py`); the DSP tables are
  `const` in flash; the eight history records (text capped at 200 chars each), the
  frame buffer (240 B), the demodulator state and the scratch text live on the app
  stack.
- `APP_NAME = "ACARS RX2"`, blob `ACARSRX2.app`, built by `./compile-app.sh acr`
  (Docker) or discovered by `./compile-app.sh All`.
- The frequency is shown on the status bar only; the app does not verify the VFO
  (the API exposes no modulation getter), so tuning to 131.525 MHz AM is manual.
