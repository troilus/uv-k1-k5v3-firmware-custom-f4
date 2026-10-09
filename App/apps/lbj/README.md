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
3. Symmetric peak trackers (attack/decay 1/256) → mid threshold, plus a small
   manual trim (keys `4`/`6`).
4. **DPLL**: 65536 per bit, 8192 per sample; each level transition pulls the
   phase toward the bit boundary (proportional + small integral); the bit is
   sampled at the 0.5 crossing.
5. POCSAG: sync `0x7CD215D8` / its inverse (popcount ≤ 2, polarity auto),
   16 codewords per batch, idle word, address/message words, **BCH(31,21)**
   single-bit correction (feedback 873, 31-entry syndrome table), then the LBJ
   BCD layout: 5 bit-reversed nibbles per word, alphabet `0123456789*U -)(`.

LBJ addresses: `1233999`, `1234000` (short / merged), `1234001`, `1234002`
(standalone / merged); `func 1 = 下行`, `3 = 上行`. Short reports pack
train `[0:6]`, speed `[6:9]`, position km `[10:15]`; a detailed report carries a
prefix, the loco code and the GBK route in the last 50 characters.

Chinese (route / loco name / category) is GBK and the radio's built-in font is
ASCII, so those fields are shown as `--` / raw BCD only (the optional 16×16
Chinese font at flash `0xA0000` is a separate blob and not assumed present).

## Debug pages (key `3`)

| Page | Content |
|---|---|
| **SUM** | Newest record: train, direction, speed, km, loco code, address / func / length / BCH-error / LBJ, raw BCD, counters |
| **PDU** | Every decoded message, **any address**: `A<addr> F<func> LBJ/-- E/+`, then up to 32 raw BCD chars (UP/DOWN scrolls) |
| **RAW** | Last 16 codewords in hex, `S`ync / `I`dle / `A`ddress / `M`essage and BCH `ok`/`XX` (UP/DOWN scrolls) |
| **SIG** | sync / word / fix / bad / msg / up / dn counters, peak-to-peak, baseline, DPLL phase, polarity, RSSI, threshold trim |

Keys: `3` page · UP/DOWN scroll or newer/older record (`nav_dir`: UV-K1
LEFT/RIGHT) · `1` speaker · `2` clear history + counters · `4`/`6` slicer
threshold trim · `5` reset DPLL · EXIT quit.

## Tests (`test/`)

```
cd test
python pocsag.py          # frame builder/decoder self-test
python model_rx.py        # synthetic sweep (table below)
python model_rx.py <file.wav>   # decode a discriminator recording
```

`pocsag.py` builds/decodes POCSAG + LBJ payloads; `model_rx.py` is the receive
chain (channel + `DemodInt`, the exact integer demodulator the C runs).

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

- History is 8 records (`.bss`/stack), raw codeword ring 16; no external-flash
  journal.
- 4 KiB overlay: texts are read-only assets (`gen_assets.py`), as in APRS RX.
- Built with `./compile-app.sh lbj` (Docker), `APP_NAME = "LBJ RX"`, blob
  `LBJRX.app`.
