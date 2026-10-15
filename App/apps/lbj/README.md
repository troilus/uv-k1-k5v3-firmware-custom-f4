# LBJ RX: on-radio LBJ / POCSAG receiver (development build)

Goal: an overlay app that receives the Chinese railway LBJ signal (POCSAG,
1200 baud, **direct FSK**, around **821.24 MHz**) on the UV-K1 / UV-K5 v3 and
shows the decoded LBJ messages plus the whole receive chain for debugging.

Reference: [Sdr-Is-Fun/RTL_SDR_LBJ_RECEIVER](https://github.com/Sdr-Is-Fun/RTL_SDR_LBJ_RECEIVER)
(the Python chain this app is modelled on) and the APRS RX app of this firmware
(PA4 sampling, DPLL, overlay-app conventions).

Status: **v0.1, decoding confirmed on-air.** The demodulator and the
POCSAG/LBJ decoder are validated in `test/model_rx.py` against synthetic frames
(clean / noise / ±1 % clock / AC-coupled); see the table below.

## 中文说明

### 使用步骤

1. 把 VFO 调到 LBJ 频率、**FM**（如 **821.2375 MHz**；BK4829 覆盖 760–1160 MHz）。
   带 `LBJ_PIN_FREQ` 的工件（见下方"构建选项与体积"）不用手动调：进入时自动钉到
   821.2375 MHz，退出后自动还原成你原来的频率、FM 与带宽不动 EEPROM。
2. 进入 **F4HWN APPS → LBJ RX**。
3. 现在只有 SUM 一页（PDU 页因 4 KiB 容量停用，键 `3` 已无效）；扬声器默认关闭，按 `1` 开。

### 屏幕说明

顶部 1 行状态栏 + 主区 7 行。

状态栏保持固件 `UI_DisplayStatus()` 的原样（电池、各种图标），app 不再自绘
标题与接收频率——顶栏就是收音机自己画的样子。

**SUM 页（解析摘要）**

```
```
███████████████████████████████  ← 顶栏：固件 UI_DisplayStatus() 原样（电池等）
K412 DN                             ← 第1行：车次前缀 + 车次 + 方向（DN 下行 / UP 上行 /
                                      ?? 未知，粗体），末尾跟 BCH 标记：干净无字符、
                                      修复过加 `*`、修不好加 `!`（`!` 优先）
SPEED 87 km/h                       ← 第2行：速度
KM 0344.7                           ← 第3行：公里标
1234000 -95dB                       ← 第4行：解出的地址值 + 接收瞬间的信号强度
                                      （小字、不加粗；不是实时值）
4BL:OFF 1SPK:ON 1/3                 ← 底部状态栏：4=背光常亮(ON/OFF)、1=扬声器(ON/OFF)、
                                      x/y；进入即显示 0/0（不再显示 RSSI、不再每 0.5s 刷新）
```

> PDU 页（报文 + 地址/功能/LBJ/BCH + 原始 BCD）与 `M/S/W/F/B` 计数行、
> `pp/d/R` 调试行因 4 KiB 容量暂时停用（源码中以 `#if 0` 保留，便于日后恢复）；
> `lbj_rx_t` 的计数字段、`rec->flags`、计数自增也一并注释掉了。

**收到一条 LBJ 报文时的提示**：唤醒背光（`4` 设成常亮时解码后保持不灭；
自动熄灭模式下按收音机 `BLTime` 熄灭），并让**绿色 LED 闪两下**——亮 100 /
灭 100 / 亮 100 / 灭 100 ms（与收音机自带 A/B RX 结束闪烁的"亮 100 ms"同宽），
由采样循环里的 100 ms 节拍驱动，不阻塞解调。

**1234001 / 1234002 新版 LB 预警**：SUM 页解析详情块——独立的 30~64 字符
详情，或 65 字符合并报文（前 15 字符短前缀 + 后 50 字符详情）；1233999/1234000
只可能有合并形式。

```
██████████████████████████████  ← 顶栏：固件 UI_DisplayStatus() 原样
```
███████████████████████████████  ← 顶栏：固件 UI_DisplayStatus() 原样
K412 DN*                            ← 第1行：车次前缀 + 车次 + 方向 + BCH 标记（合并报文取
                                      自身短前缀；独立详情取最近一条 1234000）
HXD1C 23900050                      ← 第2行：车型**英文缩写** + 8位机车登记号
                                      （3位车型码 + 5位车号）
LINE JHX                            ← 第3行：线路，默认英文 `LINE ` + 每个汉字的拼音首字母
                                      （例：京沪线 → JHX；非一级汉字显示 `?`）
SPD 87 KM 00344.7                   ← 第4行：速度 + 公里标
1234002 -87dB                       ← 第5行：解出的地址值 + 接收瞬间的信号强度
                                      （小字、不加粗）
4BL:OFF 1SPK:ON 1/3                 ← 底部状态栏（同前）
```

> 经纬度行默认**关闭**（4 KiB 容量装不下；编译选项 `LBJ_COORDS` 打开）。打开后
> 经纬度占第 5 行、地址行退到第 6 行。
>
> 中文版另有构建选项 `LBJ_ROUTE_CN`：线路行改回原样的 GB2312 汉字（不带"线路"
> 标签）、车型行恢复中文/英文切换、键 `5` 恢复、底栏恢复 `5EN/ZH` 提示。中文版
> 余量更大（约 228 B），可自行搭配 `LBJ_COORDS`。
>
> 编译选项 `LBJ_PIN_FREQ` 在进入时把接收钉到 821.2375 MHz（写 BK4819 的
> REG_38/39，就是 `BK4819_SetFrequency` 干的事）。退出时 loader 会用**没被 app
> 碰过**的 `gRxVfo->pRX->Frequency` 重新调谐，所以还原是白送的，app 里没有一行
> 恢复代码。FM 由 app 自己的 `set_af(APP_AF_FM)` 负责、带宽被 RAW 旁路，都不占
> 字节。CI 里只有 `overlay-apps` 和 `overlay-apps-cn` 两个工件打开它——`9` 版与
> 经纬度版不带，余量留给各自的功能。

**车次前缀（K/T/D/C 等）**：取自详情块 `0-3` 的 2 个 ASCII 字符（每个字符 = 2 个
BCD 半字节，与线路名同样的打包方式），去掉空格/不可打印字符后粘性记住（同
LBJ_Console 的 `lbjClass`），拼在车次号前面显示——所以只有短报文（无详情块）
时，车次号前仍带上最近一条详情报文的前缀。这是电台真实发来的数据，非推导。

详情块内偏移：`0-3` 为 2 个 ASCII 车次前缀字符，`4-6` 为 3 位车型代码，
`7-11` 为 5 位机车号（`4-11` 合起来即 8 位登记号），`12-13` 端号
（31=A、32=B、30=未知，暂未显示），`14-29` 线路 GB2312，`30-38` 经度
`DDD MM MMMM`，`39-46` 纬度 `DD MM MMMM`（均为纯数字，没有小数点）。
经纬度**照搬上游安卓接收机（`LbjDecoder.kt`）的做法**：直接把 DMS 原样打成
`104-16.1280E 30-35.0780N`（小字体里没有度/分字形，用 `-` 分隔度与分），并套用
它的合法性校验：9/8 个数字必须全是 0-9、分钟 <60、经度首位 ≤1。校验不通过、
或独立详情不足 47 字符时**整行不显示** —— 不会把坏数据印成一个“看着挺像”的
十进制数（整行为编译选项 `LBJ_COORDS`，默认关闭）。车型行**只显示英文缩写**
（如 东风4C→DF4C、韶山7E→SS7E、东方红21→DFH21）——中文名表已从资源里删掉。
线路行默认显示 `LINE ` + 每个汉字的拼音首字母：GB2312 一级字库本身按拼音排序，
所以首字母就是 23 项边界码表的一次区间查找（拼音没有 I/U/V 三个声母，所以是
23 个字母）；二级字库/符号显示 `?`。中文版（`LBJ_ROUTE_CN`）恢复原样的汉字
线路名和键 `5` 切换。

详情卡与它的 `1233999/1234000` 基础预警卡**各留一条**，历史里不做自动合并或
删除（独立详情块里没有完整车次，无法安全判定它属于哪趟车）。
`1233999/1234000` 仍为传统基础预警（车次/速度/公里标）。

### 按键操作

| 键 | 作用 |
|---|---|
| UP / DOWN | SUM：选更新/更旧的记录（UV-K1 用左右，`nav_dir`）。每次按键只走一步，按住不放不会连发 |
| `1` | 扬声器开/关（默认关；设置不再保存，每次进入都是默认值） |
| `2` | 清空报文历史（含最近一条短报文会话） |
| `4` | 背光常亮 / 自动熄灭。ON 时每个屏刷新槽都重新武装 BLTime，所以解码唤醒后一直亮着不灭；OFF 时按收音机的 `BLTime` 自动熄灭（等效收音机 F+8 的常亮功能；仅本次运行有效，退出后恢复系统 BLTime） |
| `5` | 中文 / 英文显示切换（仅中文版 `LBJ_ROUTE_CN` 有此键；默认英文，不再保存） |
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
| `LBJ` / `--` | 是否属于 SUM 页认识的地址（1233999/1234000/1234001/1234002） |
| `+` / `!` | 该条报文 BCH 正常 / 不可纠正（PDU 页停用中） |
| `*` / `!` | **SUM 页车次行末尾的 BCH 标记**：干净无字符、修复过加 `*`、修不好加 `!`（`!` 优先） |
| `DN` / `UP` / `??` | 方向：下行 / 上行 / 未知 |

### 构建选项与体积

| 选项 | 作用 | 默认 |
|---|---|---|
| （无选项） | 英文版：线路行 `LINE `+拼音首字母、车型只显示英文缩写、没有键 `5`、没有经纬度行 | 默认 |
| `LBJ_ROUTE_CN` | 中文版：线路行改回 GB2312 汉字、车型恢复中/英切换、键 `5` 恢复、底栏恢复 `5EN/ZH` 提示 | 关 |
| `LBJ_COORDS` | 打开经纬度行（英文版装不下会溢出 4 KiB，中文版可以） | 关 |
| `LBJ_HIDE9` | 车次整串首字符为 `9` 的记录整条不显示、不进历史 | 关 |
| `LBJ_PIN_FREQ` | 进入时把接收钉到 821.2375 MHz，退出由 loader 还原成原频率 | 关 |

环境变量经 `build.sh`（`LBJ_HIDE9=1 LBJ_ROUTE_CN=1 ./build.sh`）打开，`compile-app.sh`
会透传给 Docker。

CI（`.github/workflows/build-apps.yml`）一次出 **6 个工件**，下载后取里面的
`LBJRX.app` 即可：

| 工件 | 内容 |
|---|---|
| `overlay-apps` | 英文版，隐藏9，**进入自动钉 821.2375 MHz** |
| `overlay-apps9` | 英文版，不隐藏9 |
| `overlay-apps-cn` | 中文版，隐藏9，**进入自动钉 821.2375 MHz** |
| `overlay-apps-cn9` | 中文版，不隐藏9 |
| `overlay-apps-cn-coord` | 中文版 + 经纬度行，隐藏9 |
| `overlay-apps-cn9-coord` | 中文版 + 经纬度行，不隐藏9 |

余量（本地 GCC 10 `-Os` 实测，带 pin 的两个已计入 +24 B；CI 的 GCC 13 `-Oz`
一般再小 40 B 左右）：英文版 104 / 180 B、中文版 204 / 280 B、中文版+经纬度
**24 / 76 B**——最后这一档最紧，是唯一可能因编译器版本擦边的。

## Using the app

1. Set the VFO to the LBJ frequency, **FM** (e.g. 821.2375 MHz). The BK4829
   covers 18–580 MHz and 760–1160 MHz, so 821 MHz is inside its range. In a
   build with `LBJ_PIN_FREQ` (see below) this step is automatic: the receiver is
   pinned to 821.2375 MHz on entry and the loader restores the previous
   frequency on exit, without touching the EEPROM.
2. Launch **LBJ RX**.
3. The speaker is off by default (key `1`) and nothing is saved any more: every
   launch starts from the defaults (speaker off, English display).

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

Only messages whose address falls in the LBJ family window `1233980..1234020`
(matching the upstream receiver) reach the history; other POCSAG traffic is
dropped on arrival. Inside the window the SUM page has layouts for `1233999`,
`1234000` (short, or merged), `1234001`, `1234002` (standalone detail, or
merged); other addresses (e.g. `1234008`, the tail-of-train signal) are shown
with the short layout. `func 1 = 下行`, `3 = 上行`. Short reports pack
train `[0:6]`, speed `[6:9]`, position km `[10:15]`. A detail block sits at
`[0:len)` of a standalone 30..64-char report, or at the last 50 chars of a
65-char merged report: 2 ASCII prefix chars `0:4`, 3-digit model code `4:7`,
loco number `7:12`, end flag `12:14`, GB2312 route `14:30`, longitude `30:39`,
latitude `39:47` (`DDDMM.MMMM` / `DDMM.MMMM`, shown as decimal degrees).

The model row always shows the English abbreviation (DF4C/SS7E/DFH21); the GB2312
name table is gone from the assets. The route row is a compile option: by default
it reads `LINE ` + the pinyin initial of each GB2312 route character (level-1 of
the font is sorted by pinyin, so a 23-entry boundary table in the assets is all
it takes), while `-DLBJ_ROUTE_CN` keeps the original Chinese route and the key-5
language toggle for radios with the built-in 8x8 font.

## Pages

SUM only for now. The PDU page (one line per message with
`A<addr> F<func> LBJ/-- +/!` + raw BCD) and the shared counter row
`M<msgs> S<sync> W<words> F<fixed> B<bad>` were disabled to fit the 4 KiB
overlay; they remain in the source under `#if 0` for later restoration. The
PDU page listed every address; with the LBJ window filter in `lbj_emit_msg`
only LBJ-family messages reach it nowadays.

| Page | Content |
|---|---|
| **SUM** | Newest/selected record. The status bar is left exactly as the firmware's `UI_DisplayStatus()` draws it (battery and icons; the app draws no title and no frequency). The train line ends with the BCH verdict: nothing when the report arrived clean, `*` when a codeword was repaired, `!` when it could not be repaired at all (`!` wins). Short report (1233999/1234000): train + direction, `SPEED xx km/h`, `KM xxxx.x`, then the decoded address value plus the strength captured at reception (`1234000 -87dB`, small font, not bold). New-gen alert (1234001/1234002, standalone or merged; 1233999/1234000 merged): train + direction, model abbreviation + 8-digit registration number, the `LINE xxx` route row, `SPD xx KM xxxx.x`, then the address value + strength; the DMS coordinates row (`104-16.1280E 30-35.0780N`, drawn as the upstream Android receiver draws them, with its validity check, tiny font, merged blocks only, hidden when the pair is rejected or the block is short) is a compile option (`LBJ_COORDS`, off by default). A merged report uses its own short prefix, a standalone one the last short report. The train number is prefixed with the train-class letter (K/T/D/C...) decoded from the detail block's `[0:4]` — the two ASCII chars the radio actually transmits, sticky like LBJ_Console's `lbjClass` so a short report still carries the last prefix seen. The bottom bar shows the key hints (`4` backlight, `1` speaker) then `x/y` (selected/count, 1 = newest), drawn from launch as `0/0`; it is not polled for RSSI any more |

Keys: UP/DOWN pick the newer/older record (`nav_dir`: UV-K1 LEFT/RIGHT; one step
per press — holding does not auto-repeat) · `1` speaker · `2` clear history +
counters · `4` backlight always-on / auto-off (the radio's F+8 always-on;
session-only) · `5` Chinese/English display (`-DLBJ_ROUTE_CN` build only) ·
EXIT quit. Nothing is written to the app config any more (no `cfg_load` /
`cfg_save`), so every launch starts from the defaults: speaker off, English.

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

### Compile options and sizes

| Option | Effect | Default |
|---|---|---|
| (none) | English build: `LINE ` + pinyin-initial route row, model row shows the English abbreviation only, no key `5`, no coordinates row | default |
| `LBJ_ROUTE_CN` | Chinese build: the original GB2312 route row, the model-row CN/EN toggle, key `5` and the `5EN/ZH` footer hint all come back | off |
| `LBJ_COORDS` | The DMS coordinates row (it overflows the 4 KiB overlay in the English build; it fits in the Chinese one) | off |
| `LBJ_HIDE9` | Records whose train string starts with `9` are dropped entirely (never shown, never stored) | off |
| `LBJ_PIN_FREQ` | The receiver is pinned to 821.2375 MHz on entry (BK4819 REG_38/39, what `BK4819_SetFrequency` does); on exit the loader re-tunes from the untouched `gRxVfo->pRX->Frequency`, so the app carries no restore code at all. FM comes from the app's own `set_af(APP_AF_FM)` and the audio filter is bypassed in RAW, so those cost no extra bytes | off |

`LBJ_PIN_FREQ` is enabled for `overlay-apps` and `overlay-apps-cn` in CI; the
`9` and coordinate variants stay as they are and keep their overlay budget.

They are environment variables for `build.sh` (`LBJ_HIDE9=1 LBJ_ROUTE_CN=1
./build.sh`), which `compile-app.sh` passes through to Docker. CI
(`.github/workflows/build-apps.yml`) ships **six** overlay artifacts per run -
download the one you want and take the `LBJRX.app` inside:

| Artifact | Build |
|---|---|
| `overlay-apps` | English, hide-9, **pins 821.2375 MHz on entry** |
| `overlay-apps9` | English, show all |
| `overlay-apps-cn` | Chinese, hide-9, **pins 821.2375 MHz on entry** |
| `overlay-apps-cn9` | Chinese, show all |
| `overlay-apps-cn-coord` | Chinese + coordinates row, hide-9 |
| `overlay-apps-cn9-coord` | Chinese + coordinates row, show all |

Room left (local GCC 10 `-Os`, the two pinned builds including their +24 B;
CI's GCC 13 `-Oz` is usually about 40 B smaller): English 104 / 180 B, Chinese
204 / 280 B, Chinese + coordinates **24 /
76 B**. The last flavour is the tight one - the only one that could brush the
limit on a different compiler. Everything cut to buy that room back is tagged
`[C1]`..`[C6]` in the source, so it can be put back one item at a time.
