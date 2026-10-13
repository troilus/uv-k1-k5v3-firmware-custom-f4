# POCSAG RX: on-radio generic POCSAG receiver

Goal: an overlay app that receives **POCSAG** (direct FSK paging, 512 / 1200 /
2400 baud) on the UV-K1 / UV-K5 v3 and shows the decoded messages. Unlike the
LBJ RX app it is not tied to one rate, one address range or one payload layout:

* **auto rate detection** — three demodulators (512/1200/2400) run in parallel
  on the same sample stream; whichever rate yields a valid sync + BCH + address
  codeword reports the message, tagged with its rate;
* **all addresses** — no receive window, every RIC is shown;
* **numeric (4-bit BCD) and alphanumeric (7-bit ASCII)** decoded, switchable;
* a **RAW page** with the message codewords in hex and the shared counters.

## 中文说明

### 使用步骤

1. 把 VFO 调到寻呼/传呼频率、**FM**（BK4829 覆盖 18–580 MHz 与 760–1160 MHz）。
2. 进入 **F4HWN APPS → POCSAG RX**。
3. `3` 在 **MSG / RAW** 页间切换；`5` 在 **NUM / ALP** 之间切换。

### 屏幕说明

顶部 1 行状态栏 + 主区 7 行。

状态栏：`POCSAG RX`（标题）· 接收频率 · 电池。

**MSG 页（解码内容）**

```
[POCSAG RX] 821.2375          ███
A1234000 F1                         ← 第1行：地址(RIC) + 功能位 0-3
1200 NUM +                          ← 第2行：速率 + NUM/ALP + BCH 正常(+)/不可纠(!)
412   087 01234                     ← 第3行起：解出的正文（numeric 或 alphanumeric，
                                      每行 32 字符，最多 4 行）
3=MSG 5=NUM 1=OFF 1/2 -95           ← 底部状态栏：3=当前页、5=num/alpha、
                                      1=扬声器、x/y、RSSI(dBm)
```

**RAW 页（原始数据）**

```
[POCSAG RX] 821.2375          ███
A1234000 F1 1200                    ← 第1行：地址 + 功能 + 速率
3BB0BA5F 3FF0BA5F ...               ← 第2行起：报文码字十六进制，
                                      每行 4 个 32-bit 码字（共 16 个）
M3 S6 W96 F2 B0                     ← 计数：报文/同步字/码字/单比特纠错/不可纠
3=RAW 5=ALP 1=OFF 1/2 -95
```

### 按键操作

| 键 | 作用 |
|---|---|
| UP / DOWN | 选择更新/更旧的记录（一次一步，按住不连发；UV-K1 用左右） |
| `1` | 扬声器开/关（默认关，退出保存） |
| `2` | 清空报文历史与计数 |
| `3` | MSG（解码内容）/ RAW（码字十六进制 + 计数）页切换 |
| `4` | 背光常亮 / 自动熄灭（仅本次运行） |
| `5` | numeric / alphanumeric 显示切换（退出保存） |
| EXIT | 退出（回 Apps 菜单） |

## Receive path

Same hardware path as EPIRB 406 / LBJ RX: the RX audio reaches **PA4** (the
voice DAC pin, held at mid-scale by the MCU DAC), sampled on **ADC channel 4 at
38.4 kHz**. The receiver is switched to **RAW** (`reg 0x2B` / `reg 0x73`): the
300 Hz high-pass, de-emphasis and 3 kHz low-pass would destroy the NRZ baseband.
The loader restores the registers on exit.

**Why 38.4 kHz?** It is `LCM(512, 1200, 2400)`, so each standard POCSAG rate has
an integer number of samples per bit: 512 → 75, 1200 → 32, 2400 → 16. At the old
19.2 kHz, 512 baud would need 37.5 samples/bit.

## Demodulator (test/model_rx.py, class `DemodInt`; the C is a transcription)

Per ADC sample, for each of the three rate branches:

1. Slow baseline tracker (1-pole; the LBJ baseband is AC-coupled).
2. Low-pass biquad, one corner per rate (512 → 1500 Hz, 1200 → 2000 Hz,
   2400 → 3000 Hz).
3. **One-bit matched filter**: a running sum of `samples/bit` samples.
4. Symmetric peak trackers with a **hysteresis** slice (`±(hi-lo)/64`).
5. **DPLL**: 65536 per bit; a level transition pulls the phase toward the bit
   boundary; the bit is sampled at the 0.5 crossing.

The DC and peak-tracker time constants are per rate (they must scale with
samples per bit): 512 → shift 10/10, 1200 → 9/8, 2400 → 9/8. These, and the LPF
corners, were chosen with `test/model_rx.py` (see the sweep below).

## Decoder (pocsag_dec.c)

Rate-independent: sync `0x7CD215D8` / its inverse (popcount ≤ 3, polarity auto),
16 codewords per batch, idle word, address/message words, **BCH(31,21)**
single-bit correction (feedback 873), message assembly that continues across
batch boundaries (a sync inside a message does not flush it). Each completed
message keeps up to 16 raw codewords. All addresses are reported.

Payload decode (same translation unit, app side):

* **numeric**: 5 BCD nibbles per codeword (each bit-reversed), standard POCSAG
  alphabet `0123456789*U -)(`;
* **alphanumeric**: the 20-bit fields of the message codewords concatenate into a
  7-bit ASCII stream (chars packed MSB-first left-to-right, each char LSB sent
  first), reverse each 7-bit group; non-printable bytes show as `.`.

## Tests (`test/`)

```
cd test
python pocsag.py        # frame builder/decoder self-test (numeric + alpha)
python model_rx.py      # synthetic sweep over the three rates
python model_rx.py <file.wav>   # decode a recording with all three demodulators
```

`pocsag.py` builds/decodes POCSAG numeric and alphanumeric frames (multi-batch
aware); `model_rx.py` is the receive chain (channel + three `DemodInt`, the exact
integer demodulator the C runs).

### Model results (`model_rx.py`, 3 seeds, numeric + alpha)

| Case | 512 num/alpha | 1200 num/alpha | 2400 num/alpha |
|---|---|---|---|
| clean | 3/3 3/3 | 3/3 3/3 | 3/3 3/3 |
| noise 1500 | 3/3 3/3 | 3/3 3/3 | 3/3 3/3 |
| noise 3000 | 3/3 3/3 | 3/3 3/3 | 3/3 3/3 |
| noise 4500 | 3/3 3/3 | 3/3 2/3 | 2/3 0/3 |
| ±1 % clock | 3/3 3/3 | 3/3 3/3 | 3/3 3/3 |

Feeding one rate's signal to the three-way parallel bank, only the matching rate
reports the message (no cross-talk, no false positives on the others).

## Notes

* History is 8 records (on the stack); the three demodulators live on the stack
  too, so `.bss` stays small for the 4 KiB overlay.
* Text labels are read-only assets (`gen_assets.py`).
* Built with `./compile-app.sh pocsagrx`, `APP_NAME = "POCSAG RX"`, blob
  `POCSAGRX.app`.
