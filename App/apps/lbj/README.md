# LBJ RX: on-radio LBJ / POCSAG receiver (development build)

Goal: an overlay app that receives the Chinese railway LBJ signal (POCSAG,
1200 baud, **direct FSK**, around **821.24 MHz**) on the UV-K1 / UV-K5 v3 and
shows every decoded message plus the whole receive chain for debugging.

Reference: [Sdr-Is-Fun/RTL_SDR_LBJ_RECEIVER](https://github.com/Sdr-Is-Fun/RTL_SDR_LBJ_RECEIVER)
(the Python chain this app is modelled on) and the APRS RX app of this firmware
(PA4 sampling, DPLL, overlay-app conventions).

Status: **v0.1, first on-air build not yet tested.** The demodulator and the
POCSAG/LBJ decoder are validated in `test/model_rx.py` against synthetic frames
(clean / noise / ±1 % clock / AC-coupled); see the table below.

## 中文说明

### 使用步骤

1. 把 VFO 调到 LBJ 频率、**FM**（如 **821.2375 MHz**；BK4829 覆盖 760–1160 MHz）。
2. 进入 **F4HWN APPS → LBJ RX**。
3. 按 `3` 在 SUM / PDU 两页间切换；扬声器默认关闭，按 `1` 开。

### 屏幕说明

顶部 1 行状态栏 + 主区 7 行。

状态栏：`LBJ RX`（标题）· `[SUM]/[PDU]`（当前页）· 接收频率（如 `821.2375`）· 电池。

**SUM 页（解析摘要）**

```
[LBJ RX] 821.2375             ███
412 DN                              ← 车次 + 方向（DN 下行 / UP 上行 / ?? 未知，粗体）
Sp 087 Km 01234                     ← 速度 / 公里标（与车次行同字体，粗体）
                          1/3       ← 右下角 x/y = 当前第 x 条 / 共 y 条（1 最新）
```

> PDU 页（全部报文 + 地址/功能/LBJ/BCH + 原始 BCD）与 `M/S/W/F/B` 计数行、
> `pp/d/R` 调试行因 4 KiB 容量暂时停用（源码中以 `#if 0` 保留，便于日后恢复）。

**1234002 新版 LB 预警**：SUM 页解析报文尾部 50 个 nibble——

```
[LBJ RX] 821.2375             ███
412 DN                              ← 车次 + 方向（粗体）
105 12345678                        ← 车型代码(4位BCD) + 8位机车登记号
线路 京沪线                          ← GB2312 线路名
经度 E11623.4567                    ← DDMM.MMMM'E
纬度 N3954.3210                     ← DDMM.MMMM'N
Sp 087 Km 01234                     ← 速度 / 公里标（合并报文的前 15 字符）
                          1/3       ← 右下角 x/y
```

0-3 为 4 位十进制 BCD 车型代码，4-11 为 8 位机车登记号，14-29 线路
GB2312，30-38 经度，39-46 纬度，47-49 保留（12-13 端号暂未显示）。线路用设备
内置 8×8 中文字库；按 `5` 切到英文时线路隐藏、字段用英文缩写。
`1233999/1234000` 仍为传统基础预警（车次/速度/公里标）。

### 按键操作

| 键 | 作用 |
|---|---|
| UP / DOWN | SUM：选更新/更旧的记录（UV-K1 用左右，`nav_dir`）。每次按键只走一步，按住不放不会连发 |
| `1` | 扬声器开/关（默认关，退出保存） |
| `2` | 清空报文历史与全部计数 |
| `4` | 背光常亮 / 正常（仅本次运行有效，退出后恢复系统 BLTime） |
| `5` | 中文 / 英文显示切换（无字库设备用英文缩写，退出保存） |
| EXIT | 退出（回 Apps 菜单） |

### 计数行 `M S W F B`（暂时停用）

`M<msgs> S<sync> W<words> F<fix> B<bad>`。此调试行与 PDU 页一起在源码中以
`#if 0` 停用（4 KiB 容量），下表仅作恢复后的说明：

| 字符 | 含义 | 正常 |
|---|---|---|
| **M** | 解出的**报文条数** | 随接收增长 |
| **S** | 检测到的 **POCSAG 同步字**次数 | 有信号就增长 |
| **W** | 处理过的 **32-bit 码字数**（每批 16） | ≈ `16×S` |
| **F** | 经 **BCH 单比特纠错**的码字数 | 少量、接近 0 |
| **B** | BCH **无法纠正**的码字数 | 接近 0 |

每批 = 1 同步字 + 16 码字，故 `W ≈ 16×S`。例：`M3 S6 W96 F2 B0` = 3 条报文、
6 个批次、96 个码字、2 个单比特纠错、0 个不可纠。

判读：`S=0` → 没信号或 RAW 未生效；`F`/`B` 偏大 → 信噪比不足（距离/天线）；
`S` 正常、`W≈16S`、`B≈0` → 链路健康，PDU 页应出现 `LBJ` 报文。按 `2` 清零。

### 其它屏幕缩写

| 记号 | 含义 |
|---|---|
| `pp <v>` | 解调基带峰峰值（信号幅度） |
| `d<val>` | 慢速基线跟踪值（有符号） |
| `R<val>` | RX VFO 的 RSSI（dBm） |
| `A<addr>` | POCSAG 地址（RIC） |
| `F<func>` | 功能字（本例 `1`=下行 `3`=上行） |
| `LBJ` / `--` | 是否属于 LBJ 地址集（1233999/1234000/1234001/1234002） |
| `+` / `!` | 该条报文 BCH 正常 / 不可纠正 |
| `DN` / `UP` / `??` | 方向：下行 / 上行 / 未知 |

## Using the app

1. Set the VFO to the LBJ frequency, **FM** (e.g. 821.2375 MHz). The BK4829
   covers 18–580 MHz and 760–1160 MHz, so 821 MHz is inside its range.
2. Launch **LBJ RX**.
3. Key `3` cycles the debug pages; the speaker is off by default (key `1`).

## Receive path

Same hardware path as EPIRB 406 / APRS RX: the RX audio reaches **PA4** (the
voice DAC pin), held at mid-scale by the MCU DAC, and is sampled on **ADC
channel 4 at 9.6 kHz** (8 samples per 1200-baud bit), timed from SysTick,
continuously. The receiver is switched to **RAW** (`reg 0x2B` / `reg 0x73`, as
EPIRB 406): the 300 Hz high-pass, de-emphasis and 3 kHz low-pass would destroy
the NRZ baseband. The loader restores the registers on exit.

## Demodulator (`test/model_rx.py`, class `DemodInt`; the C is a transcription)

Per 9.6 kHz sample:

1. Slow baseline tracker (1-pole, 128 samples ≈ 16 bits) — essential: the LBJ
   baseband is AC-coupled and the baseline drifts over many bits.
2. Low-pass biquad 1500 Hz (Q14 fixed point).
3. Symmetric peak trackers (attack/decay 1/256) → mid threshold.
4. **DPLL**: 65536 per bit, 8192 per sample; each level transition pulls the
   phase toward the bit boundary (proportional + small integral); the bit is
   sampled at the 0.5 crossing.
5. POCSAG: sync `0x7CD215D8` / its inverse (popcount ≤ 2, polarity auto),
   16 codewords per batch, idle word, address/message words, **BCH(31,21)**
   single-bit correction (feedback 873, 31-entry syndrome table), then the LBJ
   BCD layout: 5 bit-reversed nibbles per word, alphabet `0123456789*U -)(`.

LBJ addresses: `1233999`, `1234000` (short / merged), `1234001`, `1234002`
(standalone / merged); `func 1 = 下行`, `3 = 上行`. Short reports pack
train `[0:6]`, speed `[6:9]`, position km `[10:15]`; a 1234002 report carries
the new-LB block in its last 50 nibbles: model code `0:4` (4-digit BCD),
registration number `4:12`, GB2312 route `14:30`, longitude `30:39`, latitude
`39:47`.

The route is GB2312 and is drawn with the radio's built-in 8x8 Chinese font;
key `5` falls back to ASCII labels for radios without it.

## Pages

SUM only for now. The PDU page (every decoded message with
`A<addr> F<func> LBJ/-- +/!` + raw BCD) and the shared counter row
`M<msgs> S<sync> W<words> F<fixed> B<bad>` were disabled to fit the 4 KiB
overlay; they remain in the source under `#if 0` for later restoration.

| Page | Content |
|---|---|
| **SUM** | Newest/selected record: train + direction (bold), speed + km (bold); for a 1234002 report also the model code + registration number, the GB2312 route, longitude and latitude. `x/y` (selected / total, 1 = newest) sits at the right of the last row |

Keys: UP/DOWN pick the newer/older record (`nav_dir`: UV-K1 LEFT/RIGHT; one step
per press — holding does not auto-repeat) · `1` speaker · `2` clear history +
counters · `4` backlight always-on / timeout (session-only) · `5` Chinese/English
display (saved) · EXIT quit.

## Tests (`test/`)

```
cd test
python pocsag.py          # frame builder/decoder self-test
python model_rx.py        # synthetic sweep (table below)
python model_rx.py <file.wav>   # decode a discriminator recording
```

`pocsag.py` builds/decodes POCSAG + LBJ payloads; `model_rx.py` is the receive
chain (channel + `DemodInt`, the exact integer demodulator the C runs).

### Cross-check with a POCSAG encoder

`test/pocsag-golang-windows-amd64/` holds a Windows POCSAG encoder/decoder
(`pocsag`, `pocsag-burst`, `pocsag-decode`; the `.exe`s are not versioned, only
kept on disk). Generate a real LBJ short report and decode it with this app's
model:

```
pocsag-windows-amd64.exe -a 1234000 -f 3 -type numeric -b 1200 \
    -m "412   087 01234" -o lbj_short.wav
python model_rx.py lbj_short.wav
```

Expected: `sync=1 ... fix=0 bad=0 msgs=1`, `addr=1234000 func=3
bcd[15]='412   087 01234'`, train=412, speed=87, km=0123.4. The other LBJ
addresses (1233999/1234001/1234002) and a 65-char (13-codeword) report decode
the same way; `pocsag-burst` with a JSON message list makes a multi-address
burst. On air, `pocsag-decode` / `multimon-ng -a POCSAG1200` are the ground
truth.

The reference `rtl_sdr_lbj_receiver.py` and a sample recording are kept here.
That recording is a heavily filtered, AC-coupled off-radio capture: PDW needs
maximum volume and yields wrong data, and the model finds no reliable batch in
it. It is only used to confirm the front end can see 1200-baud structure; the
on-radio, DAC-biased RAW path is the real target.

### Model results (`model_rx.py`, 3 seeds, address 1234000 func 3)

| Case | Sync | Decoded |
|---|---|---|
| clean | 3 | 3/3 |
| noise 1500 | 3 | 3/3 |
| noise 3000 | 3 | 0/3 (BCH busy) |
| ±1 % clock | 3 | 3/3 |
| STD audio path (de-emphasis) | 0 | 0/3 (why RAW is used) |
| AC-coupled (no DAC bias) | 3 | 2/3 |

## Notes

- History is 8 records (on the stack); no raw-codeword ring and no external-flash
  journal (the RAW page was removed to fit the 4 KiB overlay).
- 4 KiB overlay: texts are read-only assets (`gen_assets.py`), as in APRS RX.
- Built with `./compile-app.sh lbj` (Docker), `APP_NAME = "LBJ RX"`, blob
  `LBJRX.app`.
