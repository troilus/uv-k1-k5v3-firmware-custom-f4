# LBJ RX: on-radio LBJ / POCSAG receiver (development build)

Goal: an overlay app that receives the Chinese railway LBJ signal (POCSAG,
1200 baud, **direct FSK**, around **821.24 MHz**) on the UV-K1 / UV-K5 v3 and
shows the decoded LBJ messages plus the whole receive chain for debugging.

Reference: [Sdr-Is-Fun/RTL_SDR_LBJ_RECEIVER](https://github.com/Sdr-Is-Fun/RTL_SDR_LBJ_RECEIVER)
(the Python chain this app is modelled on) and the APRS RX app of this firmware
(PA4 sampling, DPLL, overlay-app conventions).

Status: **v0.19, decoding confirmed on-air.** The demodulator and the
POCSAG/LBJ decoder are validated in `test/model_rx.py` against synthetic frames
(clean / noise / ±1 % clock / AC-coupled / high-passed); see the tables below.
The resident firmware also keeps a train log in external flash (menu items
`LBJLOG` / `LBJCLR`), exported either with `tools/lbj_log.py` or with the
browser tool in `tools/lbj_log/` (no firmware change).

**使用说明（面向使用者）**：固件版 / LBJ RX App 版 / LBJ Edit 经纬度 / 导出，见
[`USAGE.md`](USAGE.md)。开发细节在下面。

What v0.2 changed (all mirrored in `test/model_rx.py`):

- **Slower baseline tracker** (`DC_SHIFT` 7→9, 512 samples ≈ 26.7 ms): the
  tracker no longer follows the AC-coupled droop, which is where most of the
  gain is (AC-coupled case 1/3 → 3/3, noise 4500 0/3 → 2/3).
- **Parity-checked BCH**: the trailing even-parity bit is checked too, catching
  a parity-bit-only error and rejecting a "correction" that is really two bits.
- **Soft erasure**: an uncorrectable word keeps its place with `?` characters
  instead of being dropped, so the rest of the report survives.
- **Sync popcount ≤ 2** (was ≤ 3), matching the model and upstream.
- **Proportional-only DPLL** (the integral term cost code and RAM for no
  measurable change).
- **Absolute sample clock** (`ticks_ms * 48000 + SysTick position`) and the
  **skip compensation** over the samples the key/screen slot took.

v0.3 adds **machine-readable size reporting**: each flavour's exact overlay
size (used and free) is emitted as a CI check-run annotation, which is where
the "room left" figures below come from. A formatting slimming was tried here
too (a `%10` / `/10` `putu` instead of the powers-of-ten table) and CI measured
it **larger** on this Cortex-M0+ `-Oz` build, so it was reverted - on this
target the table is the smaller form, and the compiler is the arbiter.

v0.4 carries the v0.2/v0.3 decode set into the **resident firmware feature**
(`App/app/lbj_dec.c`, `App/app/lbj_dual.c` - the LBJ dual-watch mode: set a
channel's modulation to LBJ) and adds the item the overlay had no room for:

- **The resident decoder caught up with the overlay** (until v0.4 it ran the
  *old* chain: baseline `DC_SHIFT` 7, syndrome-only BCH with no parity check, no
  soft erasure, sync popcount ≤ 3, and the DPLL integral term). It now has the
  slower baseline tracker, the parity-checked BCH, the soft erasure, sync ≤ 2
  and the proportional-only DPLL - the same code, transcribed.
- **#12: three hysteresis slicers + page merge.** One front-end (baseline
  tracker, biquad, boxcar matched filter, symmetric peak tracker), three slicers
  with band widths `(hi-lo)>>{3,5,7}` and their own DPLLs, and a page merge that
  takes each character from whichever slicer read it (a `?` from one is filled
  from another, and the longer text extends the shorter). A single slicer
  collapses as soon as the radio's audio path high-passes the NRZ; the overlay
  cannot hold it (16 B free in the tightest flavour), the resident feature can.
- **BCH moved out of the 19.2 kHz sampler ISR into the main loop.** The ISR
  (`lbj_rx_bit`) only aligns the bit stream and queues the raw 32-bit words
  (8-deep ring per slicer); the 10 ms tick drains them through BCH and the word
  state machine (`lbj_process`). A correction is up to ~160 µs against a 52 µs
  sample period, so decoding three slicers inside the ISR would have dropped
  samples (the single old slicer was already marginal on error words).

The overlay app itself is **unchanged** in v0.4 - there is no room in 4 KiB.
The shared model gained `class Slicer`, `merge_pages()` and `hpsweep()`.

v0.5 adds the **distance from the receiver to the decoded train** to the resident
popup (again resident-only: the overlay app and its 4 KiB budget are untouched):

- **Row 0, right-aligned**: `123.4km` (one decimal), `>999km` past 999.9 km,
  `--km` when it cannot be worked out. Only a report that carries the detail
  block has a fix of its own, so only those pages show anything - a page without
  one draws nothing there at all. The train number is cut to 11 characters so
  the readout always fits the 128 px row.
- **The receiver's position is not entered in the firmware.** It is the beacon
  position of the **APRS TX** overlay app, which the **APRS Edit** app maintains
  through the configuration proxy (v0.11 adds LBJ Edit ahead of it, see below).
  One copy means the readout always agrees with what is actually being beaconed,
  and no menu item and no private flash block were added. APRS TX must therefore
  be installed with a valid north/east position; otherwise every detail page
  shows `--km`. Its slot is found by itself (`APP_ValidateSlot` + the profile's
  `APC6` magic), cached and re-validated, so an edit made in APRS Edit shows up
  without a relaunch and a reinstall into another slot is picked up too.
- **The train's fix comes from the detail block** (`[30:39)` longitude,
  `[39:47)` latitude - the same two fields the overlay reads for `LBJ_COORDS`,
  except that here the trailing digits are minutes and not tenths of a minute),
  and the distance is an equirectangular approximation: a 0.0001 degree of
  latitude is 0.011132 km anywhere, longitude is scaled by the cosine of the mean
  latitude (a 19-entry Q15 table, 5 degree steps, linear in between). Integer
  only - no 64-bit product, no floating point, so no libgcc.

v0.6 is a **display fix** (both the resident popup and the overlay app; the
decoder is untouched, so it changes no decode rate):

- **The speed and the km post accept the blanks a transmitter leaves in the
  unused leading positions of their fixed-width fields.** The short prefix
  carries the speed as 3 characters and the km post as 5 (`DDDD.D`), and a
  transmitter that does not zero-pad sends a blank in the positions it
  "shortened away" - so a speed below 100 and a km post below 1000 came out as
  a dash even though they had decoded perfectly. The gate is now "digits plus
  blanks" instead of "digits only", and a blank reads as a 0 in place
  (` 87` -> `87`, ` 3447` -> `0344.7`); the upstream Android receiver does the
  same (`replace(' ', '0')` before its digit test). A `?` (a word the BCH could
  not repair) and an all-blank field still show a dash, so this can never
  invent a value: the "never a fake 0" rule is kept.

v0.7 keeps **one history entry per train** in the resident popup instead of one
per report (resident-only again; the overlay app and its 4 KiB budget are
untouched):

- **The short block and the detail block both refresh the entry they belong to.**
  A train sends its train number/speed/km post (1233999/1234000) and its
  model+number/route/fix (1234001/1234002) as separate pages; the popup now
  shows one row per train, updated by whichever page arrived last, and the list
  stays newest first. Nothing has to be deleted - the detail completes the entry
  the short block opened.
- **How a page finds its entry**: by registration (the detail block's 8-digit
  model+number) first, then by train number (the short block's 6 digits), and -
  for the first detail of a train, which carries no field the other block has -
  by taking the newest entry that saw a short block, still has no detail, and
  arrived within 5 s, which is the order the two are transmitted in. Every later
  report of that train matches on a key, so the pairing is a guess only once.
- **Each half owns its rows**: a detail never overwrites the speed of the entry
  it completes with the last short block heard from somebody else, and the BCH
  verdict on row 0 is the one of the page that filled the entry last.
- The two blocks share no field, so two trains heard back to back can still be
  cross-paired on their first detail; every report after that is matched on the
  registration or the train number.

v0.8 fixes that pairing rule (resident-only). v0.7 waited for an entry whose
**train number had decoded**, but the train number is often the very field that
is lost, so a detail right behind such a short block started a second entry
instead of completing the first - the new one's train/speed/km rows were blank
and only the BCH verdict showed. An entry now records that a short block was
seen at all, and a detail pairs with that, decoded train number or not. A detail
that still finds nothing to complete (no short block within 5 s, or a train that
only ever sends details) fills those three rows with the placeholders used
elsewhere (`--`, `SPD - km/h`, `KM -`) rather than leaving them blank.

v0.9 aligns the page classification with the overlay's own `hasdet()` and drops
what is not a report (resident-only):

- **What a page is** is now decided by the address *and* the length, exactly as
  the overlay does: a detail block needs at least 30 characters on
  1234001/1234002 and only the 65-character merged report has one on the short
  addresses; anything else of at least 15 characters is a short block. The
  resident version used to key on the address alone, so a short page on any other
  address in the window (for instance `1234008`, the tail-of-train signal) was
  rendered from the last short block heard instead of its own characters - and,
  since v0.7, filed an entry with every row empty.
- **A page that is neither** - the 5-character status page the short addresses
  send, and anything else below 15 characters - is dropped outright: no history
  entry and no popup. The upstream receiver returns on that same page (and on
  `1234008`) too; the overlay app still files its `--`/`-`/`-` row for it.
- The point of both halves of that: no history entry can be left with blank rows.
  An unpaired detail shows the placeholders above, and a short page shows its own
  characters even when they decoded to padding codes (`U`, `*`, a blank).

v0.10 makes that pairing two-way and tightens its window (resident-only). A train
sends its short block and its detail block almost together, and which one the
radio decodes first is up to the signal, so neither order may be privileged: a
detail completes the entry a short block opened, and a short block now completes
the entry a detail opened (its placeholder rows are replaced by the real ones).
The window in both directions drops from 5 s to 2 s - the two are sent back to
back, so a loose window only widened the chance of crossing two trains that were
heard interleaved. Without it, a detail that arrived first left its train split
over two entries for good: the detail kept matching by registration and the short
block by train number, so the two never met again.

v0.11 gives the receiver's position a dedicated source, the new **LBJ Edit**
overlay app (see `apps/lbjedit/`):

- **Five stored positions, one marked primary.** The popup measures from the
  primary one whenever LBJ Edit is installed and has it set; the APRS TX profile
  of v0.5 stays as the fallback, so a radio without LBJ Edit behaves exactly as
  before and an unset primary hands the place back to the beacon position. The
  five live in the app's own config area (`cfg_load`/`cfg_save`), which is why
  that area grew from 16 to 96 bytes: an app whose settings need more than 16
  must declare API level 3, and an older resident refuses such an app instead of
  silently truncating its settings.
- **The store** is `apps/lbjedit/lbjedit_pos.h` - `"LBJ1"`, a version, the primary
  index and five slots of the same 13 digit `DD MM hh` / `DDD MM hh` fields APRS
  Edit uses (north/east only, 0.01' steps). One sweep over the 48 slots records
  where that store and the APRS TX profile live - a reinstall bumps
  `APP_SlotRevision()` and they are looked for again - and the position bytes are
  re-read every time, so an edit in either app shows up without a relaunch.

v0.12 hides a short block that decoded no digit at all (resident-only). The
direction on row 0 comes from the frame header, not from the payload, so a base
alert whose train number, speed and km post were all lost still arrived - and it
used to be filed and popped up with nothing on it but ` DN`. It is now held back:
a detail sent right behind it still completes it into one full record (same 2 s
window), while on its own it never reaches the history or the window. Nothing is
lost - the block stays the partner of that detail - and nothing is invented.

v0.13 adds the **LBJPOP** menu item (resident-only, right after `LBJ`), the power
question the popup has always raised: while the window is up the firmware holds
battery save off, so the radio sits at 100 % RX - and left open by accident it
sits there for good. `ON` is the old behaviour. `3s`, `5s`, `10s` and `60s` close
the window by itself that many seconds after the last thing that changed it: a
new report, UP/DOWN paging or any key press. Closing is exactly what EXIT does -
the rest of that same carrier is not interrupted by the window popping back up
(the reports still go into the history) and the next carrier opens a fresh
window - and 10 s after the last report the radio is back in battery save. A
window opened by hand from the LBJ menu item is *pinned*: it never times out and
stays up until EXIT or PTT, as it always did, which is also the way to read at
leisure without touching the setting. Its byte is one the settings block had kept
unused, so an upgrade reads 0xFF there and lands on `ON`: nothing changes for a
radio whose owner never opens the item.

## 中文说明

**v0.4**:解码改进落到了**常驻固件的 LBJ 双守模式**上（`App/app/lbj_dec.c`、
`App/app/lbj_dual.c`，把某信道调制设为 LBJ 即可），overlay app 本身没变（4 KiB
装不下）：常驻解码器补上了 v0.2/v0.3 的全套（慢基线 `DC_SHIFT` 7→9、带奇偶校验的
BCH、软擦除 `?`、同步 ≤2、去积分 DPLL），并实现 overlay 放不下的 **#12：三切点
迟滞切片 + 页级合并**（一个前端 `(hi-lo)>>{3,5,7}` 三个切片各带独立 DPLL，同一页
三个切片各自解出的字符互补合并）。同时把 **BCH 从 19.2 kHz 采样中断搬到主循环**
（中断只对齐码位、把 32-bit 原始码字入队，10 ms 节拍里跑 `lbj_process()` 做 BCH 与
字状态机）——单次纠错最长 ~160 µs，远大于 52 µs 采样周期，三个切片在中断里跑必然丢采样。

**v0.5**：常驻弹窗加上**到列车的距离**（仍然只动常驻固件，overlay app 和它那 4 KiB
预算没碰）：

- **第 1 行右上角**：`123.4km`（1 位小数），超过 999.9 km 显示 `>999km`，算不出来显示
  `--km`。只有带详情块的报文才自带坐标，所以只有这类页显示；没有详情块的页这一格
  **什么都不画**。车次号截断到 11 字符，保证读数挤得下 128 px 那一行。
- **本机位置不用固件输入**：直接用 **APRS TX** 的信标坐标（由 **APRS Edit** 经
  configuration proxy 写入）。只存一份，读数永远和实际信标一致，也就没加菜单项、
  没占私有 flash 块。代价是必须装了 APRS TX 且坐标是有效的北纬/东经，否则详情页一律
  `--km`。它所在槽位由 `APP_ValidateSlot` + profile 的 `APC6` magic 自动找到并缓存、
  每次复核，所以在 APRS Edit 里改完立即生效，重装到别的槽位也能自己认回来。
- **列车坐标取自详情块**（`[30:39)` 经度、`[39:47)` 纬度——和 overlay 的 `LBJ_COORDS`
  是同两个字段，只是这里的尾数是"分"而不是十分之一分），距离用等距投影近似：纬度
  0.0001° 恒为 0.011132 km，经度按平均纬度的余弦缩放（19 项 Q15 表、5° 一档、档间线性
  插值）。全程整数运算——没有 64 位乘积、没有浮点，所以不会拉进 libgcc。

**v0.6** 是个**显示修正**（常驻弹窗和 overlay app 同步改；解码链没动，所以不改变解码成功率）：

- **速度与公里标接受发送端在定宽字段高位留下的空格。** 短前缀里速度是 3 字符、公里标是
  5 字符（`DDDD.D`）；发送端不做零填充时，被"省掉"的高位就是一个空格——于是 **<100 的
  速度和 <1000 的公里标即使完整解出来也会显示 `-`**。判据从"全是数字"放宽成"数字或空格"，
  空格按位置当 0（` 87`→`87`，` 3447`→`0344.7`）；上游安卓接收机就是这么做的（先
  `replace(' ', '0')` 再判数字）。**`?`（BCH 修不回来的字）和整字段全空仍然显示 `-`**，
  所以不会凭空造出数值，"绝不给假 0"的原则保留。

**v0.7**：常驻弹窗的历史改成**一辆车一条记录**（仍然只动常驻固件，overlay app 与其 4 KiB 预算没碰）：

- **短前缀与详情块各自刷新同一条记录**：一辆车把"车次/速度/公里标"(1233999/1234000)与
  "车型车号/线路/坐标"(1234001/1234002)分两页发，弹窗现在每辆车只留一行，由后到的页原地
  更新，列表仍是最新在前。不需要"删除已用过的 000"——详情页只是把短报文开的那条补全。
- **匹配顺序**：先按**车号**（详情块的 8 位"车型+车号"），再按**车次号**（短前缀的 6 位
  数字），最后——只针对一趟车的第一条详情（两个块没有任何共同字段）——取**最近一条还在等
  详情的记录**，且必须 5 秒内到达（这就是两者在协议里的发送顺序）。此后该车的每条报文都能
  命中键，所以"猜"只发生一次。
- **两半各管各的行**：详情页不会用"别人最近一次的短报文"去覆盖它补全的那条记录的速度；
  行 0 的 BCH 标记取**最后填充该记录的页**。配不上（5 秒内没收到短报文）的详情会自建一条，
  车次行留空（**v0.8 起改为占位行**），而不是借用别的车的车次/速度。
- 两个块没有共同字段，所以**两台车背靠背发短报文时，第一对仍可能配错**；之后每条都按
  车号/车次命中，不会再错。

**v0.8** 修上面那条配对规则（仅常驻固件）：v0.7 要求"那条还在等详情的记录**已经解出车次号**"，
可车次号恰恰是最容易丢的字段——于是紧跟在"车次没解出来"的短报文后面的详情会**另起一条**，
新那条的前三行是空的、只剩 BCH 标记。现在记录改记"**收到过短报文**"这个事实，详情按它对号
入座，车次解没解出来都一样。仍然配不上的详情（5 秒内没有短报文，或该车只发详情）不再留空行，
而是填上和别处一致的占位：`--` / `SPD - km/h` / `KM -`。

**v0.9** 把"一页算什么"的分类**对齐 overlay 自己的 `hasdet()`**，并丢掉不是报文的页（仅常驻固件）：

- **分类改看地址 + 长度**，与 overlay 完全一致：详情块在 1234001/1234002 上至少 30 字符、
  在短地址上只有 65 字符合并报文才有；其余 ≥15 字符的一律算短报文。常驻版以前**只看地址**，
  于是窗口内**其他地址**（如 `1234008` 尾信号）的短页会去"借上一条短报文"显示，而不是用它
  自己的字符；并且从 v0.7 起会留下**一行全空的记录**（就是你看到的那条）。
- **两半都不占的页**——短地址发的那种 **5 字符状态页**，以及任何 <15 字符的页——**整页丢弃**：
  不进历史、不弹窗。上游接收机对同一页（以及 `1234008`）也是直接 return；overlay app 仍会
  为它留一行 `--`/`-`/`-` 占位。
- 结果：**历史里再也不会出现整行空白的记录**。配不上的详情照旧显示占位，短页即使解出来的是
  "填充码"（`U`/`*`/空格）也显示它自己的字符。

**v0.10** 把配对做成**双向**并收紧窗口（仅常驻固件）：一辆车的短报文与详情几乎同时发出，
先解出哪一个取决于信号，所以两个方向都不能偏心 —— 详情照旧补全短报文开的记录，
**短报文现在也会补全详情开的记录**（那几条占位行会被真实数据覆盖）；窗口同时从 5 s 收到
**2 s**。不修的话，详情先到会让那趟车**永久裂成两条**：详情那条按车号自我刷新、短报文那条
按车次号刷新，两者再也不会相遇。窗口放太宽则只会增加"两台车交错时配错"的机会 —— 两条本来
就是背靠背发出来的。

**v0.11**：本机位置有了专用来源 —— 新的 **LBJ Edit** 应用（见 `apps/lbjedit/`）：

- **存 5 个位置，指定其中一个为主地址**。装了 LBJ Edit 且设了主地址时，弹窗就用它算距离；
  v0.5 的 APRS TX profile **仍保留为回退**——没装 LBJ Edit 的机器行为完全不变，主地址没设时
  也会把手交回给信标位置。五个位置存在应用**自己的配置区**（`cfg_load`/`cfg_save`），所以那个区
  从 **16 加宽到 96 字节**：设置超过 16 字节的应用要声明 **API level 3**，旧固件会**拒绝**这种
  应用而不是悄悄截断它的设置。
- **存储格式**见 `apps/lbjedit/lbjedit_pos.h`：`"LBJ1"` + 版本 + 主地址序号 + 5 个槽，每槽就是
  APRS Edit 那套 13 位 `DD MM hh` / `DDD MM hh`（只支持北纬/东经，0.01′）。一次扫槽记下
  "LBJ Edit 存储"和"APRS TX profile"各在哪（重装会 bump `APP_SlotRevision()`，届时重新找）；
  位置字节每次重读，所以在哪个应用里改都立即生效、不用重启。

**v0.12** 把"一个数字都没解出来"的短报文**先按住**（仅常驻固件）：行 0 的方向来自**帧头**而不是
payload，所以车次/速度/公里标全丢的基础预警照样会到达 —— 以前它照样进历史、弹窗，可整屏只有
` DN`。现在它不落盘：紧随其后的详情（同一个 2 s 窗口内）仍能把它补成完整的一条，单独出现时
既不进历史也不弹窗。信息没丢（它仍是那条详情的配对目标），也没有凭空编造。

> 判据是"这一页 15 个字符里**没有任何一位是数字**"。若速度位还留着数字（例如车次空但速度有
> `8`），那它不算空页，照旧显示 —— 有意的：收到过数字，只是字段不可用。

**v0.13** 加上 **LBJPOP** 菜单项（仅常驻固件，紧跟 `LBJ` 之后），回答弹窗一直以来的耗电问题：
窗口开着时固件会把省电**按住不放**，电台就一直 100% 收 —— 忘了关就一直这么耗下去。选 `ON`
就是原来的行为；选 `3s`/`5s`/`10s`/`60s` 则窗口在**最后一次变化之后**这么多秒自己关掉：变化
= 新报文、UP/DOWN 翻页、任意按键。关闭走的正是 EXIT 那条路 —— 同一次载波里后续报文**不会再把
窗口弹回来**（报文照旧进历史），下一次载波重新正常弹出；最后一次报文之后 10 s 电台即回到省电。
从 **LBJ 菜单手动打开**的窗口是**钉住的**：永不超时，一直留到 EXIT/PTT（和以前一样）—— 想慢慢
看又不想动设置时就用它。存的字节用的是设置区里**一直空着的那一个**，所以升级后该字节是 `0xFF`、
按 `ON` 处理：不动这个菜单项的人**行为完全不变**。

**v0.14** 加上 **LBJLOG / LBJCLR** 两个菜单项（仅常驻固件，紧跟 `LBJPOP` 之后），
把解出的每一列车**记一条日志**到外部 flash。区域用的是 flashmap 里本来就空着、标着
"RX/TX log" 的 32 KiB（`0x018000..0x01FFFF`，8 个 4 KiB 扇区 × 128 条 × 32 B；这里
不再改那张表，只是复用这块目前无人使用的空间），存法照抄上游 RF log：环形追加、
数据先写、commit 字节最后单独写、CRC8（初值 `0x5A`、多项式 `0x31`）——**断电中断的
那条下次扫描时被丢掉**，不会当成半条读出来。每条记录就是那 8 项加序号和时间：

    [0]      标志（b0..b6 = 车次/速度/公里标/车型/车号/线路/距离 有效，b7 保留）
    [1..2]   车次前缀（2 ASCII）        [3..5]  车次数字（6 位 BCD，0xF=缺）
    [6..7]   速度 u16（km/h）           [8..10] 公里标 u24（0.1 km）
    [11..12] 车型码 u16                 [13..16] 车号（8 位 BCD）
    [17..20] 线路（4 ASCII 拼音）       [21..22] 距离 i16（0.1 km）
    [23]     方向（0=未知,1=DN,2=UP）   [24..25] 序号 u16
    [26..27] 接收时的开机秒数           [28..29] 保留
    [30]     CRC8（前 30 字节）         [31]    commit = 0xA5（最后写）

一列车的短报文和详情块在历史里本来就是**一条**，日志也是**一条**：车次先在 RAM 里
攒着，安静 10 s（或来了下一列车、或下一列车的车号/车次不同）才落盘。这样每列车只写
一页，而不用为每条报文擦一个扇区；代价是**断电会丢掉最后那条还没落盘的**（计数只到
"安静"为止）。

`LBJLOG` 默认 **OFF**：不打开就完全不写 flash —— 这个设置字节以前一直空着，升级后
读到的都是 `0xFF`、按 OFF 处理，所以不动这个菜单项的人**行为完全不变**（解码、弹窗、
历史照旧）。`LBJCLR` 是清空，带 **`No`/`Yes`** 确认（`No` 在前，误按 EXIT 不会清）；
擦除是一个扇区一扇区地在后台做，不卡界面。（首次写入时才扫描整块区域找断点，开机
不额外读 flash。）

运行 overlay app 期间日志会**挂起**：app 的代码正是跑在驱动那块 4 KiB 扇区缓存里，
这时写日志会把正在执行的 app 覆盖掉 —— 所以 loader 起停 app 时顺带 `LBJ_LOG_Suspend`
/ `Resume`（复用了原来 `RXTX_LOG` 留下的那三个位置）。

导出用新脚本 `tools/lbj_log.py`：走固件自带的物理读命令（`0x0738`，和
`tools/font_write.py` 同一套协议），读回 32 KiB → 校验 CRC → 按序号排序 → CSV，列就是
上面那 8 项：

    python tools/lbj_log.py --port COM3 -o lbj.csv     # 存 CSV（Excel 可直接开）
    python tools/lbj_log.py --port COM3                # 直接打印成表格
    python tools/lbj_log.py --port COM3 --clear        # 远程清空
    python tools/lbj_log.py --port COM3 --raw dump.bin # 只存原始 32 KiB

> 这里**故意**没有照抄上游 K5Viewer 的 `0x05/0x06` 帧：UV Studio 的一行固定 25 B
> （车次/方向/线路只能塞进 `channelName[10]`），这 8 项根本放不下。日志因此走自己的
> 区域、自己的工具，UV Studio 那边不做改动。

**v0.15** 加上**网页版导出工具** `tools/lbj_log/` —— 纯前端静态页，**固件零改动**
（overlay 一个字节没动，固件版本号也没动）。用桌面版 Chrome / Edge **双击
`tools/lbj_log/index.html`** 就能读日志：走的是同一块 32 KiB 区域、同一套 `0x0738`
物理读命令，页面外观 / 布局 / 串口连接方式**照 UV Studio 来做**（顶栏、侧栏、串口状态
胶囊、全局断开按钮、主题切换、中英文切换、进度条、Console 都是内置的上游样式与代码）。

- **只读**：连接 → 读取 → 表格 → 导出，**不带远程清空**（清空仍在固件菜单 `LBJCLR`
  或 `python tools/lbj_log.py --clear`），误按关不掉车上的数据。
- 表格 10 列 = 序号 + 开机秒 + 那 8 项（车次 / 上行下行 / 速度 / 公里标 / 车型 / 车号 /
  线路 / 距离）；顶部输入框按**车次 / 车号**筛选（纯前端，不影响导出内容）。
- 导出两个文件：`lbj-log.csv`（UTF-8 BOM + CRLF，界面是中文时表头也是中文，和
  `lbj_log.py --chinese` **逐字节一致**）和 `lbj-log.bin`（原始 32 KiB，可离线分析，
  也能再喂给 `python tools/lbj_log.py --raw`）。空槽数和 CRC 错的条数在结果上方单独计数，
  坏记录不会当成半条读出来。
- 实现上把 UV Studio 的 `flash.js` 协议层（组帧 / 混淆 / CRC16 / `0x0514` 握手 /
  `0x0738`+`0x0739` / 重试与地址校验）搬过来，只新增"256 × 128 B 走完整个区域"这一段；
  `css/{core,studio,tools}.css`、`js/studio-{preferences,i18n,serial}.js`、
  `locales/{en,zh}.js` 都保持**逐字节原样**（locale 只在结尾追加本页的 `lbj*` 键），
  本页自己的样式只有 `css/lbj-log.css` 十几行（窄屏表格 + 等宽数字）。
- 记录解析与 CSV 有 Node 自测：`node --test tools/lbj_log/js/lbj-log.test.js`（15 项），
  并与 `tools/lbj_log.py` 在同一份镜像上做了逐字节比对。

同 32 B 记录格式、同一块区域，`tools/lbj_log.py`（Python + pyserial）和本页可以互换；
没有 K 头写频线、或者只是想把 dump 转成 CSV 时用 Python 版，想看着表点两下就用本页。

**v0.16** 修掉一个**能让电台死机**的固件缺陷：**只要 `LBJLOG` 刚记过车，紧接着读日志就会把
电台卡死** —— LCD、按键、串口全停，固件没有看门狗，只能拔电池。网页版和
`tools/lbj_log.py` 表现完全一样，因为都走同一条 `0x0738` 命令。

- **根因**：`0x0738` 原来调 `PY25Q16_ReadBufferPhysical()`，它内部走 SPI2 的 **DMA 读**
  （`SPI_ReadBuf()` 等 DMA1 CH4/CH5 的完成中断）。而电台自己写日志 / 写 app 槽位是另一条
  路径，会把 SPI2 留在 DMA 模式；此后这条 DMA 读等不到完成中断，就永远卡在
  `while (!TC_Flag);` 里。1 字节的探测读是**纯轮询**的，所以能过，卡死的是紧随其后的
  第一条 128 字节 DMA 读 —— 这也解释了为什么 UV Studio 读"已安装 app 列表"一直正常：
  那条命令走的是 `mb_flash.c` 的全轮询实现，从不碰 DMA。
- **修法**：`0x0738` 改走全轮询读 —— `mb_flash.c` 新增导出 `MB_ExternalFlashRead()`，
  复用的就是 UV Studio 读"已安装 app / 槽位头"那条 `mb_ext_read()`（每字节自带超时）。
  同时给 `py25q16.c` 的 DMA 路径补上界限：`SPI_WriteByte()` 的 TXE/RXNE、
  `SPI_ReadBuf()`/`SPI_WriteBuf()` 的 DMA 完成等待、`WaitWIP()` 的 100 万次上限
  （≈10 s → 10 万次），并在轮询访问前按 `mb_spi_polled_mode()` 的做法把 SPI2 复位成
  轮询模式。现在总线再出问题，最坏也只是**这一次读报错**，不会再把电台锁死。
- 顺手修掉网页版错误信息没做插值的问题（原来显示字面的 `错误：{0}`，现在是
  `错误：Read failed at 0x18000`）。
- 只改常驻固件，**overlay blob 一个字节没动**，`APP_VER` 0.4 不变。

**v0.17** 两处常驻固件改动（overlay blob 依然一个字节没动，`APP_VER` 0.4 不变）：

- **菜单项 `LBJ` 改名 `LBJMSG`**：只改显示名，`MENU_LBJ` 的 id 和功能不变（仍是打开解码
  窗口）；旁边的 `LBJPOP` / `LBJLOG` / `LBJCLR` 不动。6 个字符和 `LBJLOG` / `LBJCLR`
  同宽，菜单列不会溢出。
- **LBJ 通道的 LED 改成"解出新报文才闪"**：以前只要静噪一开绿灯就亮、静噪一关就按
  `SetEOT` 的规则闪几下 —— LBJ 的 OOK 载波是整段报文在发，静噪几乎一直开着，所以灯
  不是长亮就是一直闪。现在**调制方式为 LBJ 的通道**上，这两种"有信号就动灯"的行为都
  关掉，改为**每解出一条新报文闪 2 下**：亮 100 ms / 灭 100 ms × 2（共 400 ms），和
  LBJ 覆盖层 App 自己的绿灯双闪一致；A 通道亮绿灯，B 通道亮绿+红。同一次列车的短块和
  详块相隔只有几百毫秒，会合并成**一次**闪灯，不会连成一片。
  - 触发点是 `page_commit()` 里"这条报文已归档"（和写日志、弹窗、亮背光是同一拍），
    所以**只有真的解出新信息才闪**，光有载波不闪。
  - 这个闪灯**不受 `SetEOT` 开关影响**（`SetEOT` 只管非 LBJ 通道的收尾闪灯）。
  - 发信红灯、非 LBJ 通道原来的信号灯、新信息亮背光，全都不变。

**v0.18** 修正 v0.17 的灯行为（overlay 仍然一个字节没动，`APP_VER` 0.4 不变）：

- **载波灯保留**：LBJ 通道上，只有载波、还没解出报文时 LED 是**常亮**
  （A 通道绿灯，B 通道绿+红）—— v0.17 把这条也关掉了，等于把"有没有信号"这个
  最基本的信息也丢了，这是错的。
- **只有新报文才闪**：解出一条新报文 → **闪 2 下**（灭 100 / 亮 100 / 灭 100 /
  亮 100，共 400 ms）。先灭再亮是有意的：有载波时灯本来就常亮，这样"两下"在
  载波还在、载波已经断掉两种情况下看起来都一样。闪完自动回到载波状态 —— 载波还在
  就继续常亮，载波没了就灭。
- **载波断开不再闪**：`SetEOT` 的收尾闪灯在 LBJ 通道继续跳过（LBJ 的载波每段报文
  都在断开，按原逻辑会一直闪，这正是当初"一直闪"的来源）。
- 实现上 LED 只在 `lbj_dual.c` 一处、每 10 ms 同步一次（`ledService()`）；
  `app.c` 的静噪沿不再直接写灯，避免两边打架把闪灯的灭相掐断。

**v0.19** 把解码窗口挂到按键上（overlay 仍然一个字节没动，`APP_VER` 0.4 不变）：

- **`F1Shrt` / `F1Long` / `F2Shrt` / `F2Long` / `M Long` 五个菜单项多出一个 `LBJMSG` 选项**
  （`App/ui/menu.c` 的 `gSubMenu_SIDEFUNCTIONS`，和 `TONE CALL` 同一张表，五个菜单项共用，
  所以一次就都有）。选中后按那个键 = 菜单项 `LBJMSG`：打开解码窗口显示最新一条报文。
  没有解出过任何报文时**不打开、双响**；发射中和 FM 收音机模式下同样双响拒绝。
  - 窗口是**手工打开**的（`LBJ_OpenPopup()` 的 `g.pinned`），所以**不受 `LBJPOP` 自动关闭
    影响**，要按 EXIT 或 PTT 才关 —— 和菜单项打开的行为完全一致。
  - 在菜单/设置里按也能用：`GUI_SelectNextDisplay(DISPLAY_MAIN)` 会像 EXIT 一样清掉
    子菜单状态再回到主界面（`ui.c`），窗口随后由 `LBJ_Tick()` 的 `g.dirty` 刷出并点亮背光。
  - 键盘锁着也能用（侧键和长按 M 本来就走锁键豁免，和 `MONITOR` 一样）；弹窗打开期间按键
    被窗口独占，所以再按一次不会重复触发，也不会误改设置。
  - 动作枚举 `ACTION_OPT_LBJMSG` **追加在 `ACTION_OPT_LEN` 之前**（`settings.h`），
    已存进 EEPROM 的旧值一个都不动；读到的值越界时仍按现有规则回退成该键的默认值
    （`settings.c`）。`M Long` 的字节只有 7 位，够用。
  - 常驻固件新增约 30 B（CI 工件 `firmware-artifact` **83,347 B**、`-artifact9` **83,343 B**，
    v0.18 是 83,318 / 83,316 B；RAM 不变）。**6 个 overlay 工件不受影响**：这次没有触碰
    `App/apps/**`，overlay 二进制的源头一个字节没动，CI 的 Build Apps 全绿，各变体在
    4096 B 槽位里的占用是 4092 / 3912 / 3940 / 3820 / 4092 / 4032 B。

### 使用步骤

1. 把 VFO 调到 LBJ 频率、**FM**（如 **821.2375 MHz**；BK4829 覆盖 760–1160 MHz）。
   带 `LBJ_PIN_FREQ` 的工件（见下方"构建选项与体积"）不用手动调：进入时自动钉频率、
   钉 25 kHz 带宽并让芯片重新校准，退出后 loader 还原你原来的频率，EEPROM 不动。
   也就是说那个 VFO 原来是 438.500 也没关系。
2. 进入 **F4HWN APPS → LBJ RX**。
3. 现在只有 SUM 一页（PDU 页因 4 KiB 容量停用，键 `3` 已无效）；扬声器默认关闭，按 `1` 开。

### 屏幕说明

顶部 1 行状态栏 + 主区 7 行。

状态栏保持固件 `UI_DisplayStatus()` 的原样（电池、各种图标），app 不再自绘
标题与接收频率——顶栏就是收音机自己画的样子。

**SUM 页（解析摘要）**

```
███████████████████████████████     ← 顶栏：固件 UI_DisplayStatus() 原样（电池等）
K412 DN                             ← 第1行：车次前缀 + 车次 + 方向（DN 下行 / UP 上行 / ?? 未知，粗体），
                                      末尾 BCH 标记：干净无字符、修复过加 `*`、修不好加 `!`（`!` 优先）
SPD 87 km/h                         ← 第2行：速度（没解出来显示 `-`，不会显示假的 0；发送端在
                                      高位留的空格当 0，见 v0.6）
KM 0344.7                           ← 第3行：公里标（同上；`DDDD.D`，发送端在千位以上留的空格当 0）
-------                             ← 第4行：短报文没有详情块 → 车型行画 7 个横杠（与固件一致）
LINE -                              ← 第5行：同理无线路 → `LINE -`（与固件一致；中文版没有拼音行，留空）
1234000 -95dB                       ← 第6行：解出的地址值 + 接收瞬间的信号强度
                                      （小字、不加粗；不是实时值）
4BL:OFF 1SPK:ON 1/3                 ← 底部状态栏：4=背光常亮(ON/OFF)、1=扬声器(ON/OFF)、
                                      x/y；进入即显示 0/0（不显示 RSSI、不每 0.5s 刷新）
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
███████████████████████████████     ← 顶栏：固件 UI_DisplayStatus() 原样
K412 DN*                            ← 第1行：同上，多一个 BCH 标记（合并报文取自身短前缀；独立
                                      详情取最近一条 1234000 的短前缀）
SPD 87 km/h                         ← 第2行：速度（无效时显示 `-`）
KM 0344.7                           ← 第3行：公里标（无效时显示 `-`）
HXD1C 23900050                      ← 第4行：车型英文缩写 + 8 位机车登记号
                                      （3 位车型码 + 5 位车号）
LINE JHX                            ← 第5行：线路 `LINE ` + 每个汉字的拼音首字母（京沪线 → JHX；
                                      非一级汉字显示 `?`；中文版改原样汉字、英文模式留空）
1234002 -87dB                       ← 第6行：地址 + 强度（小字、不加粗）
4BL:OFF 1SPK:ON 1/3                 ← 底部状态栏（同前）
```

> 经纬度默认**关闭**（编译选项 `LBJ_COORDS` 打开）。打开后坐标与地址、强度**合并成
> 同一行**（tiny 字，分钟只留 1 位小数、0.1'≈185 m，32 个字符正好铺满 128 px）：
> `1234002 -120dB 104-16.1E30-35.1N`；校验不通过或详情块不足 47 字符时退回普通
> 地址行（地址与强度照常显示，只是没有坐标）。
>
> 中文版另有构建选项 `LBJ_ROUTE_CN`：线路行改回原样的 GB2312 汉字（不带"线路"
> 标签）、键 `5` 恢复、底栏恢复 `5EN/ZH` 提示；英文模式下线路行留空（拼音首字母行
> 只编进英文版构建，车型行两个版本都只显示英文缩写）。中文版余量更大（332 /
> 408 B），可自行搭配 `LBJ_COORDS`。
>
> 编译选项 `LBJ_PIN_FREQ` 在进入时把接收钉到 821.2375 MHz。写的是固件自己"运行中
> 换频"用的那一套寄存器（`spectrum.c` 的 `SetF`、`chFrScanner.c` 的 `ScanFastTune`、
> `epirb406` 的 `tune` 都一样）：频率对 REG_38/39、REG_43 = 25 kHz 带宽（RAW 只旁路
> 音频域滤波，REG_43 才是信道滤波器——VFO 若是话音设置会带进来，所以像 AISRX 一样
> 强制它），以及 **REG_30 清零再写回**：那一步让合成器按新频率重新校准、重新锁定
> （`ENABLE_VCO_CALIB` 就在这个寄存器里）。app 外面这一步是白来的——省电每次唤醒
> 都会 `BK4819_RX_TurnOn()`（`functions.c` 的 `FUNCTION_Select`）；而 overlay 占着
> 前台循环时没有那次唤醒，必须自己补。退出时 loader 会用**没被 app 碰过**的
> `gRxVfo->pRX->Frequency` 重新调谐，所以还原还是白送的，app 里没有一行恢复代码。
> CI 里只有 `overlay-apps` 和 `overlay-apps-cn` 两个工件打开它——`9` 版与经纬度版
> 不带，余量留给各自的功能。

**车次前缀（K/T/D/C 等）**：取自详情块 `0-3` 的 2 个 ASCII 字符（每个字符 = 2 个
BCD 半字节，与线路名同样的打包方式），去掉空格/不可打印字符后粘性记住（同
LBJ_Console 的 `lbjClass`），拼在车次号前面显示——所以只有短报文（无详情块）
时，车次号前仍带上最近一条详情报文的前缀。这是电台真实发来的数据，非推导。

详情块内偏移：`0-3` 为 2 个 ASCII 车次前缀字符，`4-6` 为 3 位车型代码，
`7-11` 为 5 位机车号（`4-11` 合起来即 8 位登记号），`12-13` 端号
（31=A、32=B、30=未知，暂未显示），`14-29` 线路 GB2312，`30-38` 经度
`DDD MM MMMM`，`39-46` 纬度 `DD MM MMMM`（均为纯数字，没有小数点）。
经纬度的**读法照搬上游安卓接收机（`LbjDecoder.kt`）**：同样的 DMS 打法、同样的
合法性校验（9/8 个数字必须全是 0-9、分钟 <60、经度首位 ≤1），小字体里没有度/分
字形、用 `-` 分隔度与分；只是分钟只留 1 位小数（0.1'≈185 m），好和地址、强度挤在
同一行：`1234002 -120dB 104-16.1E30-35.1N`。校验不通过、或详情块不足 47 字符时
**退回普通地址行**——不会把坏数据印成一个“看着挺像”的十进制数（坐标为编译选项
`LBJ_COORDS`，默认关闭）。车型行**只显示英文缩写**
（如 东风4C→DF4C、韶山7E→SS7E、东方红21→DFH21）——中文名表已从资源里删掉。
线路行默认显示 `LINE ` + 每个汉字的拼音首字母：GB2312 一级字库本身按拼音排序，
所以首字母就是 23 项边界码表的一次区间查找（拼音没有 I/U/V 三个声母，所以是
23 个字母）；二级字库/符号显示 `?`。中文版（`LBJ_ROUTE_CN`）恢复原样的汉字
线路名和键 `5` 切换（英文模式与短报文下该行留空）。

详情卡与它的 `1233999/1234000` 基础预警卡**各留一条**，历史里不做自动合并或
删除（独立详情块里没有完整车次，无法安全判定它属于哪趟车）。
`1233999/1234000` 仍为传统基础预警（车次/速度/公里标）。
**以上是 overlay app 的行为（v0.7 未改）；常驻固件版自 v0.7 起改为"一辆车一条",见上方 v0.7。**

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
| （无选项） | 英文版：线路行 `LINE `+拼音首字母、车型只显示英文缩写、没有键 `5`、默认不显示经纬度 | 默认 |
| `LBJ_ROUTE_CN` | 中文版：线路行改回 GB2312 汉字（英文模式该行留空）、键 `5` 恢复、底栏恢复 `5EN/ZH` 提示 | 关 |
| `LBJ_COORDS` | 把经纬度并进地址行（英文版现在也装得下、只剩几十字节；CI 只出中文版的经纬度工件） | 关 |
| `LBJ_HIDE9` | 车次整串首字符为 `9` 的记录整条不显示、不进历史 | 关 |
| `LBJ_PIN_FREQ` | 进入时把接收钉到 821.2375 MHz（含 25 kHz 带宽与 VCO 重校准），退出由 loader 还原成原频率 | 关 |

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
| `overlay-apps-cn-coord` | 中文版 + 经纬度（并进地址行），隐藏9 |
| `overlay-apps-cn9-coord` | 中文版 + 经纬度（并进地址行），不隐藏9 |

余量（CI GCC 13 `-Oz` 实测，每次构建都会以 check-run 注解给出，可随时从 API 复核：
`::notice … N B used, M B free of 4096`）：

| 档 | 已用 | 剩余 |
|---|---|---|
| 英文版，不隐藏9 | 3900 B | 196 B |
| 英文版，隐藏9 + pin | 4020 B | 76 B |
| 中文版，不隐藏9 | 3808 B | 288 B |
| 中文版，隐藏9 + pin | 3928 B | 168 B |
| 中文版 + 经纬度，不隐藏9 | 4020 B | 76 B |
| 中文版 + 经纬度，隐藏9 + pin | **4080 B** | **16 B** |

最紧的一档重新变成「中文版 + 经纬度 + 隐藏9 + pin」，只剩 16 B。

v0.5 的距离显示全在常驻固件里（`App/app/lbj_dual.c`），overlay 一个字节都没动，
所以上表的余量不变；常驻固件的增量体现在 CI 出的固件工件体积上。

v0.6 的显示修正**会动 overlay**（`putspd()` 与公里标那处的判据），所以上表的余量会有
几十字节的变化，以 CI 每次构建给出的 `N B used, M B free of 4096` 为准。

教训（v0.3）：Cortex-M0+ 没有除法指令，遇到常数的 `/`、`%` 会引入 libgcc 的除法
例程。把 `putu` 从查表改成 `%10` / `/10` 反而让 overlay **涨了 264 B**
（3808 → 4072），六个档里五个溢出。本目标下查表才是小形态——overlay 里基本应禁用
除法。

## Using the app

1. Set the VFO to the LBJ frequency, **FM** (e.g. 821.2375 MHz). The BK4829
   covers 18–580 MHz and 760–1160 MHz, so 821 MHz is inside its range. In a
   build with `LBJ_PIN_FREQ` (see below) this step is automatic: the app pins
   the frequency, forces the 25 kHz IF filter and re-calibrates the synthesizer
   on entry, and the loader restores the previous frequency on exit without
   touching the EEPROM - the VFO can well have been on 438.500.
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

1. Slow baseline tracker (1-pole, 512 samples ≈ 26.7 ms) — essential: the LBJ
   baseband is AC-coupled and the baseline drifts over many bits. Its time
   constant is slower than a run of 13 equal bits (11 ms), so the baseline never
   chases the signal down; it is the same corner upstream uses (`>>8` at 9.6 kHz,
   `>>9` here at 19.2 kHz).
2. Low-pass biquad 1500 Hz (Q14 fixed point).
3. **One-bit matched filter**: a 16-sample running sum (16 samples = one bit;
   its null at 1200 Hz rejects the data-rate harmonics). The sum, not the mean,
   is sliced — the peak tracker scales it, so no division is needed.
4. Symmetric peak trackers (attack/decay 1/256) with a **hysteresis** slice
   (`±(hi-lo)/64`, as the reference `_d8`): noise near the threshold neither
   flips the bit nor kicks the DPLL. The resident firmware feature now runs
   **three** such bands (`>>3`, `>>5`, `>>7`) over the same front-end and merges
   their pages (#12, below) - `DemodInt` takes the band width as a parameter and
   defaults to the single `>>6` band the overlay app runs.
5. **DPLL**: 65536 per bit, 4096 per sample; each level transition pulls the
   phase toward the bit boundary (proportional only, as upstream); the bit is
   sampled at the 0.5 crossing.
6. POCSAG: sync `0x7CD215D8` / its inverse (popcount ≤ 2, polarity auto),
   16 codewords per batch, idle word, address/message words, **BCH(31,21)**
   correction (feedback 873) with the trailing **even-parity bit checked too**
   (as upstream `fix()`: a null syndrome with odd parity is a parity-bit error,
   and a correction that still leaves the parity odd is a second bit,
   uncorrectable). A word the BCH cannot correct is not dropped: its 20 bits are
   skipped and its 5 characters shown as `?`, so the words after it stay in
   place and the rest of the report survives (**soft erasure**, upstream). Then
   the LBJ BCD layout: 5 bit-reversed nibbles per word, alphabet `0123456789*U -)(`.
7. The keys and the screen are served between frames; the samples the slot took
   are not read, so the bit clocks run on over them and the missing bits are
   taken as zeros, keeping a codeword's alignment (upstream `Demod.skip`). The
   sampling clock is the absolute `ticks_ms * 48000 + position in the SysTick
   period`, so a long LCD blit cannot lose a whole period.

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
latitude `39:47` (`DDDMM.MMMM` / `DDMM.MMMM`, rendered as `104-16.1E` / `30-35.1N`:
a tenth of a minute instead of the upstream receiver's four decimals - the address
row shares the line, so the pair has to fit 32 chars).

The model row always shows the English abbreviation (DF4C/SS7E/DFH21); the GB2312
name table is gone from the assets. The route row is a compile option: by default
it reads `LINE ` + the pinyin initial of each GB2312 route character (level-1 of
the font is sorted by pinyin, so a 23-entry boundary table in the assets is all
it takes), while `-DLBJ_ROUTE_CN` keeps the original Chinese route and the key-5
language toggle for radios with the built-in 8x8 font; in that build the row stays
blank in English mode (the pinyin row is compiled into the English builds only)
and blank for short reports, which carry no route at all.

## Pages

SUM only for now. The PDU page (one line per message with
`A<addr> F<func> LBJ/-- +/!` + raw BCD) and the shared counter row
`M<msgs> S<sync> W<words> F<fixed> B<bad>` were disabled to fit the 4 KiB
overlay; they remain in the source under `#if 0` for later restoration. The
PDU page listed every address; with the LBJ window filter in `lbj_emit_msg`
only LBJ-family messages reach it nowadays.

| Page | Content |
|---|---|
| **SUM** | Newest/selected record, laid out row for row like the firmware's `LBJ_DrawPopup`: the status bar stays exactly as `UI_DisplayStatus()` draws it (battery and icons; the app draws no title and no frequency), then row 1 train + direction ending in the BCH verdict (nothing when the report arrived clean, `*` when a codeword was repaired, `!` when it could not be repaired at all; `!` wins), row 2 `SPD xx km/h`, row 3 `KM xxxxx.x`, row 4 model abbreviation + 8-digit registration number, row 5 `LINE xxx` route, row 6 the decoded address value plus the strength captured at reception (`1234002 -87dB`, small font, not bold). A short report (1233999/1234000) has no detail block, so rows 4/5 get the firmware's placeholders - `-------` and `LINE -` (the Chinese build, which has no pinyin row, leaves row 5 empty); the train/speed/km digits show a dash instead of a fake 0 unless a full 15-char short prefix is there, which is the firmware's own gate (a blank the transmitter left in an unused leading position reads as a 0 since v0.6). A merged 65-char report uses its own short prefix, a standalone detail (30..64 chars) the last short report kept in `g.sess`; the resident popup keeps one entry per train since v0.7 (a detail completes the entry its short block opened) while this page still files one entry per report. With `LBJ_COORDS` (off by default) the coordinates join row 6 in the tiny font as `1234002 -120dB 104-16.1E30-35.1N` - the upstream Android receiver's DMS layout and validity check, minutes to a tenth of a minute so the pair fits the 128 px row; a rejected pair or a detail block shorter than 47 chars falls back to the plain address row. The train number is prefixed with the train-class letter (K/T/D/C...) decoded from the detail block's `[0:4]` - the two ASCII chars the radio actually transmits, sticky like LBJ_Console's `lbjClass` so a short report still carries the last prefix seen. The bottom bar shows the key hints (`4` backlight, `1` speaker) then `x/y` (selected/count, 1 = newest), drawn from launch as `0/0`; it is not polled for RSSI any more |

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
python model_rx.py        # synthetic sweep (tables below)
python model_rx.py hp     # the same sweep with a high-passed audio path (#12)
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

v0.2 (DC tracker 7→9, parity-checked BCH, sync ≤ 2, proportional DPLL), against
the previous matched-filter build:

| Case | matched-filter build | v0.2 |
|---|---|---|
| clean | 3/3 | 3/3 |
| noise 1500 | 3/3 | 3/3 |
| noise 3000 | 3/3 | 3/3 |
| noise 4500 | 0/3 | 2/3 |
| ±1 % clock | 3/3 | 3/3 |
| STD audio path (de-emphasis) | 0/3 (why RAW is used) | 0/3 |
| AC-coupled (no DAC bias) | 1/3 | 3/3 |

The one-bit matched filter is the main win: it roughly doubles the noise the
decoder tolerates (about +2–3 dB). The v0.2 gain is the slower baseline tracker
(26.7 ms): it stops following the AC-coupled droop, which is what lifted the
AC-coupled case from 1/3 to 3/3 and noise 4500 from 0/3 to 2/3. The other v0.2
changes (parity-checked BCH, soft erasure, ≤ 2 sync, proportional DPLL, the
absolute clock and the skip compensation) mostly buy correctness and robustness
on the radio rather than a different number here.

Note the model's `raw` path is a 5 kHz low-pass with no high-pass: give
`audio_path(..., hp=…)` a corner to see how a high-passed RAW path droops, which
is what a single-threshold slicer is weak against. That corner is what #12
answers: the same transmission read through three hysteresis bands and merged
page by page (`python model_rx.py hp`, 8 seeds, noise 1500, address 1234000
func 3):

| hp corner | single slicer (k=6) | #12, three merged |
|---|---|---|
| 0 Hz (no high-pass) | 8/8 | 8/8 |
| 30 Hz | 4/8 | **8/8** |
| 50 Hz | 1/8 | **6/8** |
| 100 Hz | 0/8 | 0/8 |
| 300 Hz | 1/8 | **7/8** |
| 600 Hz | 0/8 | **3/8** |
| 1000 Hz | 0/8 | 0/8 |
| 1500 Hz | 0/8 | 0/8 |

The three bands are `h = (hi-lo) >> {3,5,7}`: `k=3` flips late but never on
noise, `k=7` flips on the drooping edge, `k=5` in between. The set came out of
that sweep (k-sets were tried one by one; `{3,5,7}` beat `{5,6,7}`, `{4,5,6}`,
`{6,7,8}`, `{4,6,8}`, `{5,7,9}`, `{2,4,6}`). On the flat cases the merge is never
worse than the single chain:

| Case | single slicer | #12 merged |
|---|---|---|
| noise 4500 | 2/3 | **3/3** |
| clean / 1500 / 3000 / ±1 % clock / AC-coupled | 3/3 | 3/3 |
| STD audio path | 0/3 | 0/3 |

Two honest caveats the firmware accepts as they are: at a 100 Hz corner *no*
slicer finds the sync at all (the single chain is 0/8 there too - a resonance of
that synthetic corner with the bit pattern, not something the merge can fix),
and past ~1000 Hz the high-pass has left nothing to read. Upstream reaches the
same design (`s2/s3/s4` over a shared front-end, one page merge).

## Notes

- History is 8 records (on the stack); no raw-codeword ring and no external-flash
  journal in the **app** (the RAW page was removed to fit the 4 KiB overlay). The
  **resident firmware** keeps the flash journal instead - see v0.14 above and
  `App/app/lbj_log.c`.
- 4 KiB overlay: texts are read-only assets (`gen_assets.py`), as in APRS RX.
- Built with `./compile-app.sh lbj` (Docker), `APP_NAME = "LBJ RX"`, blob
  `LBJRX.app`.

### Compile options and sizes

| Option | Effect | Default |
|---|---|---|
| (none) | English build: `LINE ` + pinyin-initial route row, model row shows the English abbreviation only, no key `5`, no coordinates | default |
| `LBJ_ROUTE_CN` | Chinese build: the original GB2312 route row (blank in English mode - the pinyin row is compiled into the English builds only), key `5` and the `5EN/ZH` footer hint come back; the model row is English-only in every build | off |
| `LBJ_COORDS` | The coordinates merged into the address row (`1234002 -120dB 104-16.1E30-35.1N`, tiny font); the English build fits them too now, with only tens of bytes left, so CI ships the coordinate flavour for the Chinese builds only | off |
| `LBJ_HIDE9` | Records whose train string starts with `9` are dropped entirely (never shown, never stored) | off |
| `LBJ_PIN_FREQ` | The receiver is pinned to 821.2375 MHz on entry, with the 25 kHz IF filter forced and the synthesizer re-calibrated - the register set the firmware's own run-time retunes use (`spectrum.c` `SetF`, `chFrScanner.c` `ScanFastTune`, `epirb406` `tune`): REG_38/39, REG_43 = `0x3628` and REG_30 to 0 and back (that last one is what makes the PLL relock; outside an app the power-save wake-up `BK4819_RX_TurnOn()` does it for free). On exit the loader re-tunes from the untouched `gRxVfo->pRX->Frequency`, so the app carries no restore code at all. FM comes from the app's own `set_af(APP_AF_FM)` | off |

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
| `overlay-apps-cn-coord` | Chinese + coordinates in the address row, hide-9 |
| `overlay-apps-cn9-coord` | Chinese + coordinates in the address row, show all |

Room left, **measured by CI** (GCC 13 `-Oz`). Every figure is a check-run
annotation from the build itself (`::notice … N B used, M B free of 4096`), so
it can be re-checked from the API at any time:

| Flavour | Used | Free |
|---|---|---|
| English, show-all | 3900 B | 196 B |
| English, hide-9 + pin | 4020 B | 76 B |
| Chinese, show-all | 3808 B | 288 B |
| Chinese, hide-9 + pin | 3928 B | 168 B |
| Chinese + coordinates, show-all | 4020 B | 76 B |
| Chinese + coordinates, hide-9 + pin | **4080 B** | **16 B** |

The razor-edge build is now Chinese + coordinates with hide-9 and the pinned
frequency, at 16 B. `[C1]`..`[C6]` in the source tag everything cut to buy room,
so it can be put back one item at a time.

**A caution learned the hard way (v0.3):** the Cortex-M0+ has no divide
instruction, so a `/` or `%` by a constant pulls in libgcc's division helper. A
`putu` rewritten to walk the digits with `%10` / `/10` instead of the
powers-of-ten table grew the overlay by **264 B** (3808 -> 4072) and overflowed
five of the six flavours. The table is the small form on this target; division
is effectively banned from the overlay.
