# ACARS RX: on-radio ACARS receiver (development build)

Goal: an overlay app that receives **ACARS** (ARINC 618: **131.525 MHz AM**, MSK
**2400 baud**, 1200/2400 Hz tones) on the UV-K1 / UV-K5 v3 and shows the decoded
message.

Reference: [acarsdec](https://github.com/szpajder/acarsdec) (`msk.c` / `acars.c`,
Thierry Leconte) — its DSP is the demodulator below, transcribed to fixed point.
The APRS RX / LBJ RX / EPIRB 406 apps of this firmware provide the PA4 sampling
and the overlay-app conventions.

Status: **v0.5, decoding on air with error repair.** With the VFO on 131.525 MHz
**AM**, the app shows messages; the demodulator was already validated on real
ACARS audio (acarsdec's `test.wav`, 4 channels, 12500 Hz: **7/7 messages, no CRC
errors**). v0.1 never displayed anything (the screen stayed on `WAIT`); the
on-air bring-up found why: the AF output mode must be **AM (7)**, and v0.1's
strict `crc == 0` test dropped every frame, because the radio's frames often
fail the CRC as received. v0.5 repairs them (acarsdec's parity/syndrome fix)
and keeps the last four messages.

## On-air bring-up notes

- **AF output mode must be `BK4819_AF_AM` (7, REG_47 = 0x6740).** Measured on
  the radio: with AM (7) the demod locks and the `T` trace line reads
  `16 01 <mode> 02 ..`; with FM (1) the burst is still audible but the trace
  line stays random. (Counter-intuitive: this firmware's own *listening* AM path
  uses `AF_FM` + `REG_31` AM enable; decoding instead wants `AF_AM`.) v0.4
  defaults to AM; **key 2** cycles AM / FM / RAW / USB as a fallback.
- **Error repair (no `!` on most frames).** A frame with a bad CRC is repaired
  with acarsdec's parity/syndrome algorithm (`fixprerr` / `fixdberr`, up to 3
  parity errors, single-bit and same-byte double-bit errors). A repaired frame
  gets `*` after the header line; a frame that cannot be repaired keeps `!`. The
  3872-byte `syndrom[]` table does **not** fit the 4 KiB overlay, so the
  syndrome of a bit at a given distance is computed on the fly from the CRC
  (same linear result, ~24 candidates per bad frame, only at frame end).
- **History.** The last four messages stay in RAM (newest first): UP/DOWN pick
  the newer/older one, 6/9 page its text. Records live on the app stack, and the
  stored text is capped at 200 chars to stay within the stack budget (a rare
  longer message loses its tail).
- **DC tracker rounding.** The carrier tracker rounds to nearest instead of
  flooring its arithmetic shift, the same bias EPIRB 406 hit at PA4 levels.
- **Debug view.** Automatic while no frame has been seen, or forced with **key
  5** (press again for the message view, so the debug lines never cover a
  decode):
  - `T ..` - the 8 raw bytes after the last detected `SYN`. A real frame reads
    `16 01 <mode> 02 ..`; noise reads something random.
  - `AF<n> n/m` - AF output mode (key 2) and the selected message. `SYN<n>
    S2<n> HDR<n>` - header starts / 2nd `SYN` seen / full `SYN SYN SOH` headers.
    `BY<n> CE<n> FX<n>` - bytes after a header / CRC failures / repaired frames.
    `LV<n>` - matched-filter magnitude.

## 中文说明

### 使用步骤

1. 把 VFO 手动调到 **131.525 MHz**、**AM**（ACARS 常用频率还有 129.525 /
   131.725 / 131.825 / 136.975 MHz；本 app 不会自己改频率，也不会切换调制）。
2. 进入 **F4HWN APPS → ACARS RX**。
3. 扬声器默认关闭，按 `1` 开（想听原始音频时用）。

### 屏幕说明

顶部 1 行状态栏 + 主区 7 行（7×8=56 px，小字体 3×5、每字符 4 px，一行 32 字符）。

```
[ACARS RX] 131.5250           ███   ← 状态栏：标题 · 接收频率 · 电池
F-GTAE  H1 A*                       ← 第0行（粗体）：机号(7)+Label(2)+Block ID+标记
D65CAF7728#DFB00000/V206,05,124,... ← 第1~5行：报文正文（小字体，每行32字符）
...                                    （按 ↑/↓ 选报文，6/9 翻正文页）
1SPK: ON 4BL: OFF 2/4 -85           ← 底部：按键状态 · 当前/共几条 · 实时RSSI(dBm)
```

- 机号 = 航空器注册号（7 字符，如 `F-GTAE`）或航班号；`Label` 与 `Block ID`
  是 ACARS 的报文类型/分块标识。
- 行尾标记：无 = CRC 正确；`*` = 坏 CRC 已纠错修复；`!` = 修不好（仍显示）。
- **最多保留 4 条**历史报文（新的在前）；↑/↓ 选报文，6/9 翻当前正文页。
- 空 `text` 的短帧（只有报头）只增加计数，不覆盖正在显示的内容。
- 正文里的控制字符（CR/LF 等）显示为空格，末尾空格已裁掉。

**收到一条报文时的提示**：唤醒背光（`4` 设成常亮时解码后保持不灭；自动
熄灭模式下按收音机 `BLTime` 熄灭），并让**绿色 LED 闪两下**——亮 100 / 灭 100 /
亮 100 / 灭 100 ms（与收音机自带 A/B RX 结束闪烁的"亮 100 ms"同宽），由采样
循环里的 100 ms 节拍驱动，不阻塞解调。

### 按键操作

| 键 | 作用 |
|---|---|
| UP / DOWN | **选历史报文**：更新的 / 更旧的（最新在前，最多 4 条）；UV-K1 用左右键（`nav_dir`），每次只走一步，按住不连发 |
| `6` / `9` | **翻当前正文页**：6 上一页、9 下一页（每次 32 字符一行） |
| `1` | 扬声器开 / 关（默认关，退出保存） |
| `5` | **报文 ↔ 调试屏切换**（默认显示报文；没报文时自动显示调试屏。这样调试行不会盖住报文） |
| `2` | **备用**：循环 AF 输出模式 AM(默认) → FM → RAW → USB，屏上 `AF<n>` 显示当前值 |
| `3` | 清空历史与计数，回到 `WAIT` 调试屏 |
| `4` | 背光常亮 / 自动熄灭。ON 时每个屏刷新槽都重新武装 BLTime，所以解码唤醒后一直亮着不灭；OFF 时按收音机 `BLTime` 自动熄灭（等效收音机 F+8 的常亮功能；仅本次运行有效） |
| EXIT | 退出（回 Apps 菜单） |

## Using the app

1. Set the VFO **manually** to **131.525 MHz AM** (also 129.525 / 131.725 /
   131.825 / 136.975 MHz). The app never retunes and cannot read the modulation
   from the API, so pick AM yourself.
2. Launch **ACARS RX** (APPS menu). The speaker is off by default (key `1`).
3. UP/DOWN picks the newer/older of the last four messages; `6`/`9` page the
   text; `4` keeps the backlight on; `5` switches message/debug; EXIT quits.
4. Header marks: none = CRC ok, `*` = repaired, `!` = uncorrectable (still
   shown). If it stays on `WAIT`, read the `T ..` trace line after a burst (see
   *On-air bring-up notes*).

## Receive path

Same hardware path as EPIRB 406 / APRS RX / LBJ RX: the RX audio reaches **PA4**
(the voice DAC pin), held at mid-scale by the MCU DAC, and is sampled on **ADC
channel 4 at 19.2 kHz** (8 samples per 2400-baud bit), timed from SysTick,
continuously. The receiver is switched to **RAW** (`reg 0x2B` / `reg 0x73`, as
EPIRB 406: 300 Hz high-pass, de-emphasis and low-pass off) and to **AM**, since
ACARS is an AM signal whose envelope carries the MSK tones. The loader restores
the registers on exit.

## Demodulator (`test/fx.py`, class `FxDemod`; the C is a transcription)

This is acarsdec's coherent MSK DSP in integer arithmetic — **not** a
delay-multiply / peak-tracker demodulator, which was tried first and fails on
real signals (its metric carries a DC bias, so runs of equal bits drag the
slicer threshold).

Per 19.2 kHz sample:

1. Audio DC tracker (`>>8`) removes the carrier level.
2. **Complex LO at 1800 Hz** — the middle of the two tones. The audio is real,
   so the baseband is `x * exp(-j p)` with `p` in 1/4096 turn units
   (1800/19200 = 3/32 turn per sample).
3. **Matched filter**: a bit clock running off the VCO phase (3/4 turn = exactly
   8 samples) triggers, once per bit, a correlation of the last **17 baseband
   samples** (two bit periods) with `cos(2*pi*(i-8)/32)`.
4. **Decision = sign test** on the alternating Re/Im component (MSK is OQPSK),
   so no level threshold is tracked: long runs of equal bits are harmless, and
   the demodulator is amplitude-independent (works with any audio level).
5. **Bang-bang carrier PLL** (±1/4096 turn per sample, from the quadrature
   component) locks the LO phase on the preamble.

`1800/19200 = 3/32 turn` and the `cos(2*pi*(i-8)/32)` window are both samples of
one **64-entry Q12 cosine table** (`sin(x) = cos(x-90deg)`), so 128 B of flash
carry the whole DSP.

Frames: `SYN SYN SOH txt… ETX/ETB crc_lo crc_hi`, bytes LSB first with **odd
parity in bit 7**, **CRC-16/KERMIT** (0x8408, reflected, init 0) over `txt+CRC`
must be `0`. The burst polarity is recovered from `SYN` vs `~SYN` and folded
into the demodulator's quadrature counter, which inverts the whole bit stream
(CRC bytes included). Frames shorter than 13 bytes, or with a bad CRC, are
dropped (no error correction: acarsdec's parity/syndrome repair was not ported).
Layout: `[0]` mode, `[1]` STX, `[2..8]` registration, `[9..10]` label, `[11]`
block id, `[12]` STX/ETX, `[13..]` text, last `ETX`/`ETB`.

## Tests (`test/`)

```
cd test
# reference recording (4 channels x 12500 Hz, one ACARS frequency each):
curl -L -o acars_test.wav \
    https://cdn.jsdelivr.net/gh/szpajder/acarsdec@master/test.wav
python fx.py                # the integer demodulator the C runs -> 7 messages
python fx.py <rec.wav>      # any PCM16 WAV (12500 Hz preferred; resampled)
python ref_acarsdec.py      # the float reference, for comparison
```

`fx.py` decodes **the same 7 messages as the reference** (`F-GTAE/H1`,
`PH-BXR/5V`, `LN-DYY/Q0`, `G-DBCKW/_`, `G-DBCK/Q0`, `LN-DYY/Q0`) and is stable
under fractional-sample timing offsets (`ACARS_SHIFT`); loop settings are
`PLLMODE=bb PLLMAX=1`, exactly what `acr_app.c` uses.
`prototype.py` is the design-study harness that ruled out the delay-multiply and
single-symbol two-tone correlator candidates.

## Notes

- 4 KiB overlay: texts are read-only assets (`gen_assets.py`); the message
  buffer (240 B), the demodulator state and the scratch text live on the stack.
  Local GCC 10 `-Os` build: **2576 B** (CI's `-Oz` is smaller).
- `APP_NAME = "ACARS RX"`, blob `ACARSRX.app`, built by `./compile-app.sh acr`
  (Docker) or discovered by `./compile-app.sh All`.
- The frequency is shown on the status bar only; the app does not verify the VFO
  (the API exposes no modulation getter), so tuning to 131.525 MHz AM is manual.
