# AIS RX: on-radio AIS receiver (prototype)

Receives **AIS** (Automatic Identification System: **161.975 / 162.025 MHz**,
GMSK **9600 baud**, NRZI + HDLC + CRC-16) on the UV-K1 / UV-K5 v3 and shows the
key fields of the decoded messages.

- **Receive path** follows EPIRB 406 / APRS RX / LBJ RX: the RX audio reaches
  **PA4** (the voice DAC pin), held at mid-scale by the MCU DAC, and is sampled
  on **ADC channel 4 at 38.4 kHz** (4 samples per 9600-baud symbol), timed from
  SysTick, continuously. The receiver is switched to **RAW** (`reg 0x2B` /
  `reg 0x73`: RX HPF/LPF, de-emphasis and AFC off) and to **FM** (the FM
  discriminator is the demod output). The loader restores the registers on exit.
- **Demodulator / deframer / parser** (`ais_decode.c`) are this app's own, ported
  bit-for-bit and verified on the host against **pyais** (MIT) over both synthetic
  and real AISHub traffic. The app does **not** copy any GPL (gr-ais) code — that
  was used as an algorithm reference only.

## 使用步骤

1. 把 VFO **手动**调到 **162.000 MHz**（或 161.975 / 162.025，或任一存有 AIS
   频点的记忆信道）、**FM** 模式。**本 app 不会自己改频率、不会切调制、不会动 PLL**，
   它只解当前 VFO 停在的频率。
2. 进入 **F4HWN APPS → AIS RX**。
3. 无需扬声器（默认静音）。收到 CRC 正确的报文时，屏幕显示其关键字段，**绿色 LED
   闪两下**，底部计数 +1。

## 屏幕说明 / Screen layout

状态栏 1 行 + 粗体标题 1 行 + 正文最多 4 行 + 底部 1 行调试读数。

```
[ AIS RX ]  161.9750           ███   ← 状态栏：标题 · 接收频率 · 电池
MMSI 244690971                        ← 位置报告 (类型 1/3) 或 静态信息 (类型 5)
SOG 10.5 KN COG 123                   ← 对地航速(节) / 对地航向(度)
LAT 51.5010                           ← 纬度（十进制度，4 位小数）
LON -0.1419                           ← 经度（十进制度，4 位小数）
FM A1200 B47 F2 C1 H42                ← 底部调试行（见下）
```

类型 5（静态与航次）显示为：

```
MMSI 215678900
NAME TEST SHIP
DEST ST PETERSBURG
```

### 底部调试行 / Bring-up debug line

上电调试用，每 50 ms 刷新，把整条解调链路从低到高打通：

| 段 | 含义 |
|---|---|
| `FM` | 当前 AF 输出模式（**按 `2` 循环** FM / AM / RAW / USB / BYP / M13 / M14，见下） |
| `A` | PA4 音频峰峰值（原始 ADC）。`≈0` 说明没音频进来 |
| `B` | 本窗口切出的 bit 数。`A` 大而 `B=0` 说明 DPLL 没锁 |
| `F` | 看到的 HDLC 开 flag 数。`B` 大而 `F=0` 说明波形不是 AIS（频率/波特不对） |
| `C` | CRC 正确帧数。`F>0 C=0` 说明帧结构已现、CRC 在挂 |
| `H` | **带宽探针**：相邻样本差 / 幅度 ×100。越大说明 PA4 信号高频成分越多（越宽）。判读见下 |
| `!` | 末尾 `!` 表示刚显示的帧 CRC 没过（放宽显示，字段可能不准） |

**`H`（带宽）判读**：AIS 9600 baud 需要 ~8–10 kHz 基带。在同一个频道上，若 `H` 小
（语音级 ~3 kHz 限制）且切 NARROW/WIDE 时 `H` 明显变化，说明 **RX 通道滤波在限带**；
本版已**强制 `REG_43` = WIDE(25 kHz)**（不依赖菜单带宽），若仍解不出，则看 `H` 是否
在强信号下仍偏小 → 硬件通路限带。

**排查顺序**：`A≈0` → 查调制模式/静噪/频率（`2` 切 AF 模式直到 `A` 动）；
`A` 大 `B=0` → DPLL；`B` 大 `F=0` → 不是 161.975/162.025 的 AIS；`F>0 C=0` → CRC/信噪；
`C>0` → 成功，进入准确性阶段。

**已确认的结论（上机 bring-up 实测）**：

- 正确的接收组合是 **FM 模式、161.975（或 162.025）、静噪 0、AF 模式 `FM`**（`A` 在 FM 下
  最强，压倒 AM/RAW/USB）。
- 主机诊断（`test/diag_chain.py`、`diag_real_audio.py`）证明：
  - **解调算法本身没问题** —— 在真实 AIS 录音上解出了 CRC 有效的 type-8 帧
    （`diag_frame.c` 显示 `crcOk=1`），空中失败不是解码器 bug。
  - 解调链需要 **≥ ~6.5 kHz** 的基带带宽（再窄 ISI 必挂）；采样率是软件精确 pacing 的，
    波特误差非瓶颈。
- **对照实验（关键）**：同一台机器上，**lbj**（1200 baud FSK，走 FM/AF_FM/PA4）和 **acr**
  （2400 baud AM-MSK）**都能成功解码** → RF 输入、PA4、FM 鉴频器、AM 通路**全部验证可用**。
  aisrx 与它们唯一的实质差别是 **9600 baud = 8× 带宽**。
- **结论：BK4819 的 RX 通道滤波对 9600 baud AIS 太窄**（低速 app 落在通带内，AIS 超出 →
  带内 ISI → 抓得到 flag、CRC 永远挂）。本版因此**在 app 内强制 `REG_43` = WIDE(25 kHz)**，
  不依赖菜单带宽；并用 `H`（带宽探针）观察 PA4 信号实际有多宽。

### 诊断工具 / Diagnostics (host only, not in the app)

`test/` 下的独立诊断脚本，不参与 `run_tests.sh`（主套件保持 8/8），也不进 CI 产物：

| 工具 | 用途 |
|---|---|
| `diag_chain.py` | 扫**带宽悬崖**（AF 低通截止）和**波特/采样率误差**容限，定位解调链在哪个维度断裂 |
| `diag_real_audio.py` + `diag_frame.c` | 把**真实 AIS 录音**喂给解调链，dump 每个 CRC 有效帧（长度/类型/hex/parse 结果），验证算法对真实波形有效 |
| `diag_sdr_iq.py` | 把**真实 SDR IQ**（WAV）解调成基带 `.u16` 供 `host_ais`/`diag_frame` 跑 |
| `ref_decoder.py` | **独立参考解码器**（Python，不同定时恢复：突发+相位搜索+积分），可在同一真实 IQ 上与我 C 解码器对照、并输出合法 MMSI |
| `mk96.py` / `run_spssweep.sh` | 把 ship162 已知 IQ `ais_96k.bin`（Cu8/96k 双信道）解调成 `.u16`，按 4/8/16 sps 跑 C 解码器 |
| `chk_crc.py` | 独立复算 CRC 残差（与 `ais_decode.c` 的 crc16 一致，MSB-first），核对帧真伪 |
| `real_payloads.txt` | 真实 AISHub payload（`real` 主测试用） |

### 字段含义 / Field reference

| 字段 Field | 例 Example | 含义 / Meaning |
|---|---|---|
| MMSI | `244690971` | 船舶身份码（9 位）。 / Nine-digit ship identification. |
| SOG | `10.5 KN` | 对地航速，0.1 节为单位；`N/A`=1023（无效）。 / Speed over ground, 0.1 kn units; `N/A` = 1023 (not available). |
| COG | `123` | 对地航向，整数度（原始 0.1°）；`N/A`=3600。 / Course over ground, whole degrees (raw 0.1°); `N/A` = 3600. |
| LAT / LON | `51.5010` | 纬/经度，十进制度 4 位小数（原始 1/10000 角分）；`N/A`=91°/181°。 / Latitude / longitude in decimal degrees (raw 1/10000 arc-min); `N/A` = 91°/181°. |
| NAME / DEST | `TEST SHIP` | 船名 / 目的港（6-bit 文本，去尾部 `@` 与空格）。 / Ship name / destination (6-bit text, trailing `@`/spaces stripped). |

只解 **类型 1/3**（A 类船位置报告）和 **类型 5**（静态与航次）。其它类型的帧只要 CRC
正确也会让底部计数 +1，但不显示字段。

## 按键操作

| 键 | 作用 |
|---|---|
| `2` | **循环 AF 输出模式**：FM → AM → RAW → USB → BYP → M13 → M14 → FM。切换送到 PA4 的解调信号源；调试行开头显示当前模式。上电排查时按它直到 `A`（音频摆幅）有读数 |
| `3` | 清屏，回到 `WAIT`（清显示、计数和 `!` 标记） |
| `4` | 背光常亮 / 自动熄灭 |
| EXIT | 退出（回 Apps 菜单） |

## Receive path

Same hardware path as EPIRB 406 / APRS RX / LBJ RX, but at **38.4 kHz**
(4 samples per 9600-baud symbol, `AIS_FS`/`AIS_SPS` in `ais_decode.h`). RAW RX
(`reg 0x2B |= 0x0700 & ~0x0007`, `reg 0x73 |= 0x0010`): RX HPF/LPF, de-emphasis
and AFC off, so the discriminator gives a clean baseband; the FM offset is
removed by the decoder's DC block. AF is fixed to `AF_FM` (1).

## Demodulator (`ais_decode.c`)

Per 38.4 kHz sample: a rounded **leaky DC block** (128-sample mean, `MEAN_SHR=7`,
settles inside the RTS training) removes the discriminator offset; a
**zero-crossing DPLL** (slot = 1024 in Q8, +256/sample, each crossing pulls the
phase by `err>>PLL_K`) tracks the 9600-baud symbol clock; the symbol is
**integrated and sliced** on slot end; **NRZI** decoding (`bit = cur==prev`) is
polarity-invariant; a bit-oriented **HDLC** deframer hunts the `01111110` flag,
drops stuffed bits and, on the closing flag, pops the 7 flag bits and runs
**CRC-16** (poly 0x8408 reflected, init 0xFFFF, residue 0xF0B8). Field offsets
and 6-bit text follow pyais.

## Frames

`… training (RTS) + flag + [payload + FCS, bit-stuffed] + flag …`, NRZI on the
air. Payload bits are stored MSB-first in transmission order, so pyais bit
offsets apply directly. Type 1/3 = 168 bits, type 5 = 424 bits (+16 FCS).

## Host tests

`test/run_tests.sh` synthesizes over-the-air-like 38.4 kHz discriminator audio
from bit-level AIS frames (GMSK-smoothed NRZI, with noise / DC / baud-error /
multi-frame / corrupted-FCS variants) plus a **real AISHub payload** case, takes
the golden field values from **pyais**, and diffs the decoder's output. Run under
a host gcc (the decoder is freestanding C, identical code on target and host):

```
cd App/apps/aisrx/test
CC=gcc PY=python3 ./run_tests.sh      # 8 cases: clean noise dc clock multi badcrc type5 real
```

## Notes

- 4 KiB overlay: labels are read-only assets (`gen_assets.py`); the decoder
  state (`ais_t`, incl. the 64-byte frame buffer), the parsed message and the
  scratch text live on the app stack.
- `APP_NAME = "AIS RX"`, blob `AISRX.app`, built by `./compile-app.sh aisrx`
  (Docker) or discovered by `./compile-app.sh All`.
- The app cannot verify the VFO frequency or modulation (the API exposes no
  getter), so tuning to 162 MHz FM is manual. If the IF filter is narrow the
  default 25 kHz channel filter is used; wide-open AIS decoding may want a wider
  filter (future work).
- Reference material (`pyais`, `ship162`, `gr-ais`, `ais-tools`, an AISHub NMEA
  sample and real AIS audio) is used only to cross-check the decoder and is not
  redistributed with the app (see `.gitignore`).
