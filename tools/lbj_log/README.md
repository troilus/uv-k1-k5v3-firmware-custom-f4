# LBJ 车次日志 · 网页版导出工具

`tools/lbj_log.py` 的浏览器版本：用 **Web Serial** 直接连对讲机，把外部 flash
`0x018000..0x01FFFF`（1024 条 × 32 字节）里的 LBJ 车次日志读回来，在页面上列成表格，
导出 CSV 或原始 32 KiB 镜像。

页面外观、布局、串口连接方式**照搬 UV Studio**（`armel.github.io/uvstudio`）：同一套
顶栏 / 侧栏 / 状态胶囊 / 主题切换 / 语言选择 / 表格 / 进度条 / Console，同一套
`0x0514` 握手 + `0x0738/0x0739` 读取协议、同样的重试与地址校验。**固件不需要任何改动**
（`0x0738` 由 `ENABLE_FEAT_F4HWN_EXT_FLASH_RW` 提供，Fusion / LBJRadio 预设已开启）。

## 打开

用桌面版 **Chrome / Edge / Opera** 打开 `tools/lbj_log/index.html`
（双击即可，`file://` 也能用 Web Serial；`http://localhost` 亦可）。

- 先把 UV Studio / K5Viewer 关掉 —— 串口同一时刻只能被一个页面占用。
- 用 K 头写频线连接电台，电台开机在正常模式。
- 点 **读取车次日志**，在浏览器弹窗里选中电台的串口。

读完后页面上会出现两个下载链接：

| 下载 | 内容 |
| --- | --- |
| `lbj-log.csv` | 与 `lbj_log.py` 逐字节一致的 CSV（UTF-8 BOM + CRLF）；界面语言为中文时表头用中文 |
| `lbj-log.bin` | 原始 32 KiB，可直接喂给 `python tools/lbj_log.py --raw` 或离线分析 |

表格上方的输入框可按 **车次** 或 **车号** 筛选（纯前端过滤，不影响导出内容）。
**本页只读**，不提供清空日志的功能（要清空请用固件菜单里的 `LBJCLR`，或
`python tools/lbj_log.py --clear`）。

## 记录格式

与 `tools/lbj_log.py`、`App/app/lbj_log.c` 完全一致（32 字节，小端）：

```
[0]      flags   b0..b6 = 车次/速度/公里标/车型/车号/线路/距离 有效（b7 保留）
[1..2]   车次前缀（ASCII）
[3..5]   车次数字 6 位 BCD（半字节 0xF = 缺）
[6..7]   速度 u16（0xFFFF = 未知）
[8..10]  公里标 u24（0.1 km，0xFFFFFF = 未知）
[11..12] 车型码 u16（0xFFFF = 未知）
[13..16] 车号 8 位 BCD（半字节 0xF = 缺）
[17..20] 线路 4 个 ASCII（拼音首字母）
[21..22] 距离 i16（0.1 km，<0 = 未知）
[23]     方向（0 = 未知，1 = 下行 DN，2 = 上行 UP）
[24..25] sequence u16
[26..27] uptime u16
[28..29] 保留（0xFF）
[30]     CRC8（前 30 字节，初值 0x5A，多项式 0x31）
[31]     commit = 0xA5（最后写入，才算一条完整记录）
```

## 文件

```
index.html            UV Studio 外壳 + LBJ Log 工具视图
css/{core,studio,tools}.css   原样内置自 UV Studio（见下"来源"）
css/lbj-log.css       本页自己的少量样式（宽屏/窄屏表格、等宽数字）
js/studio-preferences.js      原样内置
js/studio-i18n.js             原样内置（data-i18n 机制）
js/studio-serial.js           原样内置（串口所有权仲裁）
js/serial.js          改编自 UV Studio 的 flash.js：连接 + 0x0514 + 0x0738/0x0739
js/lbj-log.js         纯模块：32 字节记录解析 + CRC8 + CSV（可选 Node CLI）
js/lbj-log.test.js    Node 自测（node --test）
js/app.js             页面接线：状态胶囊 / 主题 / 读取流程 / 表格 / 导出
locales/{zh,en}.js    内置自 UV Studio，并在结尾追加本页的 lbj* 键
```

没有内置 `viewer.css` 和 `flash.js`（K5Viewer 屏幕、固件刷写等与本页无关）。

## 来源（vendored）

内置资源取自 **UV Studio v2.0.0**，仓库 `armel/armel.github.io`，目录 `uvstudio/`：

| 本地文件 | 上游 | 版本参数 |
| --- | --- | --- |
| `css/core.css` | `css/core.css` | `?v=5` |
| `css/studio.css` | `css/studio.css` | `?v=26` |
| `css/tools.css` | `css/tools.css` | `?v=57` |
| `js/studio-preferences.js` | `js/studio-preferences.js` | `?v=2` |
| `js/studio-i18n.js` | `js/studio-i18n.js` | `?v=5` |
| `js/studio-serial.js` | `js/studio-serial.js` | `?v=2` |
| `locales/en.js` | `locales/en.js` | `?v=63` |
| `locales/zh.js` | `locales/zh.js` | `?v=63` |

上表这些都是**逐字节原样**的，只有两个 locale 在文件**结尾追加**了本页的 `lbj*` /
`studio_nav_lbj_log` 键（原来的键一个没改）。要跟着上游升级：重新下载同名文件，再把结尾的
LBJ 键块拼回去即可。`css/lbj-log.css` 是本页新加的，不来自上游。

不是原样内置、而是**改编**的三个上游文件：

| 本仓库 | 上游 | 版本参数 |
| --- | --- | --- |
| `js/serial.js` | `js/flash.js`（连接 / 0x0514 / 0x0738/0x0739 / 重试） | `?v=84` |
| `js/app.js` | `js/studio.js`（状态胶囊）+ `js/flash.js`（读取流程） | `?v=19` / `?v=84` |
| `js/lbj-log.js` + `js/lbj-log.test.js` | `js/rf-log.js` + `tests/rf-log.test.js`（UMD 模块 + node:test 写法） | `?v=4` |

## 自测

```sh
node --test tools/lbj_log/js/lbj-log.test.js
node --check tools/lbj_log/js/lbj-log.js
```

覆盖：CRC8 固件向量、几何常量、全字段解析、方向三态、缺字段哨兵、未知车型码、
未知距离、CRC 失败、commit 未写、整块扫描与空槽/坏记录计数、CSV（BOM/表头/CRLF/排序/引号）。

也可以用 CLI 把一份已 dump 的镜像直接转成 CSV，用来和 Python 版逐字节对比：

```sh
node tools/lbj_log/js/lbj-log.js zone.bin -o js.csv
python tools/lbj_log.py --raw zone.bin -o py.csv
cmp js.csv py.csv            # 应完全一致（中文表头用 --chinese）
```

## 真机验证

需要在电台 + K 头写频线上手动点一次（本页无法在无硬件环境里自动化验证）。建议：

1. 固件菜单里打开 `LBJLOG`，等解到几列车；
2. 打开本页 → 读取车次日志 → 表格里应出现这些车次；
3. 导出 CSV，与 `python tools/lbj_log.py --port COMx -o py.csv` 的结果对比应完全一致。

---

*LBJ Log is a small companion page for this firmware repo. Its look, page layout
and serial connection deliberately follow UV Studio (armel/armel.github.io) so it
behaves like a familiar UV Studio tab; the LBJ zone walk and the record parser are
new. Vendored UV Studio assets are kept verbatim and stay under their original
licence.*
