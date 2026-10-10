# LBJ RX: on-radio LBJ / POCSAG receiver (development build)

Goal: an overlay app that receives the Chinese railway LBJ signal (POCSAG,
1200 baud, **direct FSK**, around **821.24 MHz**) on the UV-K1 / UV-K5 v3 and
shows every decoded message plus the whole receive chain for debugging.

Reference: [Sdr-Is-Fun/RTL_SDR_LBJ_RECEIVER](https://github.com/Sdr-Is-Fun/RTL_SDR_LBJ_RECEIVER)
(the Python chain this app is modelled on) and the APRS RX app of this firmware
(PA4 sampling, DPLL, overlay-app conventions).

Status: **v0.1, decoding confirmed on-air.** The demodulator and the
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
412 DN                              ← 第1行：车次 + 方向（DN 下行 / UP 上行 / ?? 未知，粗体）
SPEED 87 km/h                       ← 第2行：速度
KM 0344.7                           ← 第3行：公里标
1234000                             ← 第4行：解出的地址值
5ZH 4BL:OFF 1SPK:ON 1/3 -95         ← 底部状态栏：5=语言(EN/ZH)、4=背光常亮(ON/OFF)、
                                      1=扬声器(ON/OFF)、x/y、RSSI(dBm)；每 0.5s 刷新，
                                      进入即显示 0/0
```

> PDU 页（全部报文 + 地址/功能/LBJ/BCH + 原始 BCD）与 `M/S/W/F/B` 计数行、
> `pp/d/R` 调试行因 4 KiB 容量暂时停用（源码中以 `#if 0` 保留，便于日后恢复）；
> `lbj_rx_t` 的计数字段、`rec->flags`、计数自增也一并注释掉了。

**1234001 / 1234002 新版 LB 预警**：SUM 页解析详情块——独立的 30~64 字符
详情，或 65 字符合并报文（前 15 字符短前缀 + 后 50 字符详情）；1233999/1234000
只可能有合并形式。

```
[LBJ RX] 821.2375             ███
412 DN                              ← 第1行：车次 + 方向（合并报文取自身短前缀；独立详情取最近一条 1234000）
HXD1C 23900050                      ← 第2行：车型(中文) + 8位机车登记号（3位车型码 + 5位车号）
线路 京沪线                          ← 第3行：GB2312 线路名
SPD 87 KM 00344.7                   ← 第4行：速度 + 公里标
LON 104.2064 LAT 30.4129            ← 第5行：经纬度（小字体、英文标签、十进制度）
1234002                             ← 第6行：解出的地址值
5ZH 4BL:OFF 1SPK:ON 1/3 -95         ← 底部状态栏（同前）
```

详情块内偏移：`0-3` 为 2 个 ASCII 车次前缀字符，`4-6` 为 3 位车型代码，
`7-11` 为 5 位机车号（`4-11` 合起来即 8 位登记号），`12-13` 端号
（31=A、32=B、30=未知，暂未显示），`14-29` 线路 GB2312，`30-38` 经度
`DDDMM.MMMM`，`39-46` 纬度 `DDMM.MMMM`。经纬度按 `度 + 分/60` 换算成十进制度
（`104°12.3856′` → `104.2064`），保留 4 位小数；独立详情不足 47 字符时不画
此行。车型名/线路用设备内置 8×8 中文字库；按 `5` 切到英文时线路隐藏、车型
显示英文缩写（如 东风4C→DF4C、韶山7E→SS7E、东方红21→DFH21）。

收到**任何详情报文**时，会把历史里最近一条 `1233999/1234000` 短报文记录删掉
（一条记录对应一列车）：合并报文若车次比对不一致则保留；独立详情没有完整车次
可比，直接删（车次/速度/公里标来自 `g.sess` 里最近一条短报文）。
`1233999/1234000` 仍为传统基础预警（车次/速度/公里标）。

### 按键操作

| 键 | 作用 |
|---|---|
| UP / DOWN | SUM：选更新/更旧的记录（UV-K1 用左右，`nav_dir`）。每次按键只走一步，按住不放不会连发 |
| `1` | 扬声器开/关（默认关，退出保存） |
| `2` | 清空报文历史（含最近一条短报文会话） |
| `4` | 背光常亮 / 自动熄灭（等效收音机 F+8 的常亮功能；仅本次运行有效，退出后恢复系统 BLTime） |
| `5` | 中文 / 英文显示切换（无字库设备用英文缩写，退出保存） |
| EXIT | 退出（回 Apps 菜单） |

### 计数行 `M S W F B`（暂时停用）

`M<msgs> S<sync> W<words> F<fix> B<bad>`。此调试行与 PDU 页一起在源码中以
`#if 0` 停用（4 KiB 容量），`lbj_rx_t` 里的 8 个计数字段及其自增也已注释掉；
恢复时要把 `lbj_dec.h` 的字段、`lbj_dec.c` 的自增和 `lbj_app.c` 的 `#if 0`
页面一起解注释。下表仅作恢复后的说明：

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
3. The speaker is off by default (key `1`); key `5` toggles Chinese/English.

## Receive path

Same hardware path as EPIRB 406 / APRS RX: the RX audio reaches **PA4** (the
voice DAC pin), held at mid-scale by the MCU DAC, and is sampled on **ADC
channel 4 at 19.2 kHz** (16 samples per 1200-baud bit), timed from SysTick,
continuously. The receiver is switched to **RAW** (`reg 0x2B` / `reg 0x73`, as
EPIRB 406): the 300 Hz high-pass, de-emphasis and 3 kHz low-pass would destroy
the NRZ baseband. The loader restores the registers on exit.

## Demodulator (`test/model_rx.py`, class `DemodInt`; the C is a transcription)

Per 19.2 kHz sample:

1. Slow baseline tracker (1-pole, 128 samples ≈ 8 bits) — essential: the LBJ
   baseband is AC-coupled and the baseline drifts over many bits.
2. Low-pass biquad 1500 Hz (Q14 fixed point).
3. **One-bit matched filter**: a 16-sample running sum (16 samples = one bit;
   its null at 1200 Hz rejects the data-rate harmonics). The sum, not the mean,
   is sliced — the peak tracker scales it, so no division is needed.
4. Symmetric peak trackers (attack/decay 1/256) with a **hysteresis** slice
   (`±(hi-lo)/64`, as the reference `_d8`): noise near the threshold neither
   flips the bit nor kicks the DPLL.
5. **DPLL**: 65536 per bit, 4096 per sample; each level transition pulls the
   phase toward the bit boundary (proportional + small integral); the bit is
   sampled at the 0.5 crossing.
6. POCSAG: sync `0x7CD215D8` / its inverse (popcount ≤ 3, polarity auto),
   16 codewords per batch, idle word, address/message words, **BCH(31,21)**
   single-bit correction (feedback 873, 31-entry syndrome table), then the LBJ
   BCD layout: 5 bit-reversed nibbles per word, alphabet `0123456789*U -)(`.

LBJ addresses: `1233999`, `1234000` (short, or merged), `1234001`, `1234002`
(standalone detail, or merged); `func 1 = 下行`, `3 = 上行`. Short reports pack
train `[0:6]`, speed `[6:9]`, position km `[10:15]`. A detail block sits at
`[0:len)` of a standalone 30..64-char report, or at the last 50 chars of a
65-char merged report: 2 ASCII prefix chars `0:4`, 3-digit model code `4:7`,
loco number `7:12`, end flag `12:14`, GB2312 route `14:30`, longitude `30:39`,
latitude `39:47` (`DDDMM.MMMM` / `DDMM.MMMM`, shown as decimal degrees).

The model name and route are GB2312 and are drawn with the radio's built-in 8x8
Chinese font; key `5` switches to ASCII abbreviations (DF4C/SS7E/DFH21) and
hides the route for radios without it.

## Pages

SUM only for now. The PDU page (every decoded message with
`A<addr> F<func> LBJ/-- +/!` + raw BCD) and the shared counter row
`M<msgs> S<sync> W<words> F<fixed> B<bad>` were disabled to fit the 4 KiB
overlay; they remain in the source under `#if 0` for later restoration.

| Page | Content |
|---|---|
| **SUM** | Newest/selected record. Short report (1233999/1234000): train + direction, `SPEED xx km/h`, `KM xxxx.x`, the decoded address value. New-gen alert (1234001/1234002, standalone or merged; 1233999/1234000 merged): train + direction, model (Chinese; ASCII abbreviation with key `5`) + 8-digit registration number, GB2312 route, `SPD xx KM xxxx.x`, `LON .. LAT ..` in decimal degrees (tiny font, merged blocks only), the decoded address value; a merged report uses its own short prefix, a standalone one the last short report. The bottom bar shows the key hints (`5` language, `4` backlight, `1` speaker) then `x/y` (selected/count, 1 = newest) and the live `-xx` dBm RSSI (every 0.5 s; shown from launch as `0/0`) |

Keys: UP/DOWN pick the newer/older record (`nav_dir`: UV-K1 LEFT/RIGHT; one step
per press — holding does not auto-repeat) · `1` speaker · `2` clear history +
counters · `4` backlight always-on / auto-off (the radio's F+8 always-on;
session-only) · `5` Chinese/English display (saved) · EXIT quit.

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

After the 19.2 kHz / matched-filter / hysteresis change (before → after):

| Case | Before | After |
|---|---|---|
| clean | 3/3 | 3/3 |
| noise 1500 | 3/3 | 3/3 |
| noise 3000 | 0/3 | 3/3 |
| noise 4500 | 0/3 | 0/3 (sync 2, words 23) |
| ±1 % clock | 3/3 | 3/3 |
| STD audio path (de-emphasis) | 0/3 (why RAW is used) | 0/3 |
| AC-coupled (no DAC bias) | 2/3 | 1/3 |

The one-bit matched filter is the main win: it roughly doubles the noise the
decoder tolerates (about +2–3 dB).

## Notes

- History is 8 records (on the stack); no raw-codeword ring and no external-flash
  journal (the RAW page was removed to fit the 4 KiB overlay).
- 4 KiB overlay: texts are read-only assets (`gen_assets.py`), as in APRS RX.
- Built with `./compile-app.sh lbj` (Docker), `APP_NAME = "LBJ RX"`, blob
  `LBJRX.app`.
