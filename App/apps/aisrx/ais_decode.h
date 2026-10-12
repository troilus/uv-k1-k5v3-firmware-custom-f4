/* Copyright 2026
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * ais: streaming AIS (Automatic Identification System) decoder, fed with
 * FM-discriminator audio samples (the BK4819 RAW RX audio as read on PA4).
 * Freestanding C (no libc), no division in the per-sample path, so the same
 * code runs in the overlay app and in the host test harness.
 *
 * Signal: 161.975 / 162.025 MHz, GMSK, 9600 baud, HDLC framing, NRZI line
 * coding, CRC-16 (poly 0x8408 reflected, init 0xFFFF, residue 0xF0B8). The
 * BK4819 discriminator gives the instantaneous frequency, i.e. a smooth NRZ
 * waveform; a DPLL on its zero crossings tracks the 9600-baud symbol clock,
 * each symbol is integrated and sliced, then NRZI-decoded and deframed.
 *
 * The first decoded message type (1/3 position, 5 static) is parsed into
 * ais_info_t. Reference: pyais (MIT) field offsets/widths, ship162 (MIT)
 * NRZI + HDLC + CRC mechanics.
 */

#ifndef AIS_DECODE_H
#define AIS_DECODE_H

#include <stdint.h>
#include <stdbool.h>

#define AIS_FS        38400u   /* sample rate expected by the decoder (Hz)    */
#define AIS_SPS       4u       /* samples per symbol at 9600 baud             */
#define AIS_FRAME_MAX 64u      /* deframed bytes (payload + 2 FCS), >= 55     */

enum { AIS_SEARCH = 0, AIS_DATA = 1, AIS_DONE = 2 };

typedef struct {
    int32_t  meanQ8;       /* input DC estimate, x256                        */
    int32_t  acc;          /* symbol integrator (one symbol, DC-removed)     */
    int32_t  ph;           /* position in the symbol slot, Q8 (slot=1024)    */
    uint16_t nbits;        /* bits collected in the current frame            */
    uint8_t  state;        /* AIS_SEARCH / AIS_DATA / AIS_DONE               */
    uint8_t  prev_lvl;     /* previous DC-removed sign (sample rate)         */
    uint8_t  prev_bit;     /* previous sliced bit, for NRZI (symbol rate)    */
    uint8_t  ones;         /* consecutive 1s in the raw HDLC stream          */
    uint8_t  total;        /* CRC-valid frames decoded (saturating, UI)      */
    /* bring-up debug (app reads/clears; host tests ignore) */
    uint16_t nSlices;      /* bits sliced since last clear (saturating)      */
    uint16_t nFlags;       /* HDLC opening flags seen since last clear       */
    uint8_t  crcOk;        /* last AIS_DONE frame: 1 = CRC passed, 0 = bad   */
    uint8_t  showBad;      /* 1: surface plausible bad-CRC frames (crcOk=0)  */
    uint8_t  frame[AIS_FRAME_MAX];  /* deframed bits, MSB-first (pyais order) */
} ais_t;

typedef struct {
    uint8_t  type;         /* message type (1, 3 or 5)                       */
    uint8_t  repeat;       /* repeat indicator                               */
    uint32_t mmsi;
    /* type 1/3 position report */
    uint8_t  nav;          /* navigation status                              */
    uint16_t sog;          /* speed over ground, 0.1 kn; AIS_NA_SOG = N/A    */
    uint16_t cog;          /* course over ground, 0.1 deg; AIS_NA_COG = N/A  */
    int32_t  latS, lonS;   /* position, 1/10000 arc-min, N and E positive    */
    uint8_t  hasPos;
    /* type 5 static and voyage */
    char     name[21];     /* ship name, NUL terminated                      */
    char     dest[21];     /* destination, NUL terminated                    */
} ais_info_t;

#define AIS_NA_SOG 1023u
#define AIS_NA_COG 3600u
#define AIS_NA_LAT 54600000L   /*  91 degrees (0x3412140)                    */
#define AIS_NA_LON 108600000L  /* 181 degrees (0x6791AC0)                    */

/* Arm the decoder for the next message (keeps the DC estimate). */
void ais_rearm(ais_t *d);

void ais_init(ais_t *d);

/* Feed one sample (any unsigned ADC scale). Returns true when a complete
 * CRC-valid message has been received (state AIS_DONE); call ais_parse,
 * then ais_rearm. */
bool ais_push(ais_t *d, uint16_t sample);

/* Field extraction from the last CRC-valid frame. Returns true when the
 * frame holds a supported message type (1/3 or 5). */
bool ais_parse(const ais_t *d, ais_info_t *out);

#endif /* AIS_DECODE_H */
