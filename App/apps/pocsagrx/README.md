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

## Using the app

1. Set the VFO to the paging frequency, **FM** (the BK4829 covers 18–580 MHz and
   760–1160 MHz).
2. Enter **F4HWN APPS → POCSAG RX**.
3. Key `3` toggles MSG / RAW; key `5` toggles NUM / ALP.

The speaker is off by default (key `1`).

## Screen

One status bar row + seven body rows.

Status bar: `POCSAG RX` (title) · receive frequency · battery.

**MSG page (decoded content)**

```
[POCSAG RX] 821.2375          ███
A1234000 F1                         ← row 1: address (RIC) + function 0-3
1200 NUM +                          ← row 2: rate + NUM/ALP + BCH ok(+)/bad(!)
412   087 01234                     ← rows 3+: the decoded text (numeric or
                                      alphanumeric), 32 chars per row, up to 4
3=MSG 5=NUM 1=OFF 1/2 -95           ← bottom bar: 3=page, 5=num/alpha,
                                      1=speaker, x/y, RSSI (dBm)
```

**RAW page (raw data)**

```
[POCSAG RX] 821.2375          ███
A1234000 F1 1200                    ← row 1: address + function + rate
3BB0BA5F 3FF0BA5F ...               ← rows 2+: message codewords in hex,
                                      4 x 32-bit codewords per row (16 total)
M3 S6 W96 F2 B0                     ← counters: msgs / syncs / words /
                                      single-bit fixes / uncorrectable
3=RAW 5=ALP 1=OFF 1/2 -95
```

## Keys

| Key | Action |
|---|---|
| UP / DOWN | pick the newer / older record (one step per press, no auto-repeat; UV-K1 uses LEFT/RIGHT) |
| `1` | speaker on/off (off by default, saved on exit) |
| `2` | clear the message history and the counters |
| `3` | MSG (decoded content) / RAW (codeword hex + counters) page |
| `4` | backlight always-on / auto-off (session only) |
| `5` | numeric / alphanumeric display (saved on exit) |
| EXIT | quit (back to the Apps menu) |

## Receive path

Same hardware path as EPIRB 406 / LBJ RX: the RX audio reaches **PA4** (the
voice DAC pin, held at mid-scale by the MCU DAC), sampled on **ADC channel 4 at
38.4 kHz**. The receiver is switched to **RAW** (`reg 0x2B` / `reg 0x73`): the
300 Hz high-pass, de-emphasis and 3 kHz low-pass would destroy the NRZ baseband.
The loader restores the registers on exit.

**Why 38.4 kHz?** It is `LCM(512, 1200, 2400)`, so each standard POCSAG rate has
an integer number of samples per bit: 512 → 75, 1200 → 32, 2400 → 16. At the old
19.2 kHz, 512 baud would need 37.5 samples/bit.

## Demodulator (`test/model_rx.py`, class `DemodInt`; the C is a transcription)

Per ADC sample, for each of the three rate branches:

1. Slow baseline tracker (1-pole; the LBJ baseband is AC-coupled).
2. Low-pass biquad, one corner per rate (512 → 1500 Hz, 1200 → 2000 Hz,
   2400 → 3000 Hz), Q14 fixed point.
3. **One-bit matched filter**: a running sum of `samples/bit` samples; its null
   at the bit rate rejects the data harmonics.
4. Symmetric peak trackers with a **hysteresis** slice (`±(hi-lo)/64`).
5. **DPLL**: 65536 per bit; a level transition pulls the phase toward the bit
   boundary; the bit is sampled at the 0.5 crossing.

The DC and peak-tracker time constants are per rate (they must scale with
samples per bit): 512 → shift 10/10, 1200 → 9/8, 2400 → 9/8. These, and the LPF
corners, were chosen with `test/model_rx.py` (see the sweep below).

## Decoder (`pocsag_dec.c`)

Rate-independent: sync `0x7CD215D8` / its inverse (popcount ≤ 3, polarity auto),
16 codewords per batch, idle word, address/message words, **BCH(31,21)**
single-bit correction (feedback 873), and message assembly that continues across
batch boundaries (a sync inside a message does not flush it). Each completed
message keeps up to 16 raw codewords. Every address is reported.

Payload decode (app side, same translation unit):

* **numeric**: 5 BCD nibbles per codeword (each bit-reversed), standard POCSAG
  alphabet `0123456789*U -)(`;
* **alphanumeric**: the 20-bit fields of the message codewords concatenate into a
  7-bit ASCII stream (characters packed MSB-first left-to-right, each character's
  LSB sent first); each 7-bit group is reversed. Non-printable bytes show as `.`.

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
reports the message (no cross-talk, no false positives on the other two).

## Notes

* History is 8 records (on the stack); the three demodulators live on the stack
  too, so `.bss` stays small for the 4 KiB overlay.
* Text labels are read-only assets (`gen_assets.py`).
* Built with `./compile-app.sh pocsagrx`, `APP_NAME = "POCSAG RX"`, blob
  `POCSAGRX.app`.
