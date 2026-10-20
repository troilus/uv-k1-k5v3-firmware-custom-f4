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

/* Streaming AIS demodulator + HDLC deframer + field parser.
 *
 * Input: FM-discriminator audio (BK4819 RAW RX on PA4) at AIS_FS = 38400 Hz,
 *        4 samples per 9600-baud symbol.
 * Output: ais_info_t for message types 1/3 (position) and 5 (static).
 *
 * Chain (per sample): DC block -> zero-crossing DPLL (slot = 1024 Q8,
 * +256/sample, crossing pulls phase by err>>PLL_K) -> integrate and slice
 * per symbol -> NRZI (bit = cur==prev, polarity invariant) -> bit-oriented
 * HDLC with stuffing removal -> CRC-16 residue check when the closing flag
 * arrives (poly 0x8408 reflected, init 0xFFFF, good residue 0xF0B8).
 *
 * Bits are stored MSB-first into frame[] in transmission order, so the
 * pyais field offsets apply directly (verified against pyais:
 * test/verify_fields.py). CRC runs on the final buffer after the flag bits
 * are popped, so stuffed/flag bits never take part (verified against the
 * arrival-order convention in test/verify_crc.py).
 */

#include "ais_decode.h"

#define SLOT      ((int32_t)AIS_SPS * 256)  /* symbol slot, Q8 samples      */
#ifndef MEAN_SHR
#define MEAN_SHR  7    /* DC tracker: 2^7 = 128 samples ~ 3.3 ms @38.4 kHz,
                        * settles inside the RTS training so the DPLL locks
                        * on the first frame (512 was too slow: subharmonic
                        * half-rate slicing on frame 1).                       */
#endif
#ifndef PLL_K
#define PLL_K     3    /* zero-crossing correction gain, 1/8                */
#endif
/* Front-end pre-filter + adaptive slicer, mirroring the proven apps (lbj uses a
 * slow DC tracker + low-pass biquad + peak tracker; aprsrx a band-pass + AGC).
 * Pre-filter: RBJ low-pass biquad, Q=0.7071 (Butterworth), fc @ 38.4 kHz, Q14
 * (b2 = b0). Default fc ~8 kHz: passes the AIS main lobe, rejects out-of-band
 * noise. Overridable with -D for sweeps. */
#ifndef LPF_B0
#define LPF_B0   8893
#define LPF_B1  17786
#define LPF_B2   8893
#define LPF_A1  14160
#define LPF_A2   5027
#endif
#define MIN_FRAME 24u  /* smallest sane frame, bits (3 bytes)               */
#define FLAG7E    6u   /* consecutive 1s that end a frame (01111110)        */

/* CRC-16, poly 0x8408 reflected, init 0xFFFF. Residue over message+FCS is
 * 0xF0B8 for a good frame (AIS/HDLC convention, ship162 crc16). */
static uint16_t crc16(const uint8_t *p, uint16_t n)
{
    uint16_t crc = 0xFFFFu;
    for (uint16_t i = 0; i < n; i++) {
        uint8_t c = p[i];
        for (uint8_t k = 0; k < 8u; k++) {
            uint16_t mix = (uint16_t)((crc ^ (uint16_t)(c >> 7)) & 1u);
            crc = (uint16_t)(crc >> 1);
            if (mix) crc = (uint16_t)(crc ^ 0x8408u);
            c = (uint8_t)(c << 1);
        }
    }
    return crc;
}

void ais_init(ais_t *d)
{
    d->meanQ8  = -1;          /* seed on first sample                       */
    d->acc     = 0;
    d->ph      = 0;
    d->bx1 = d->bx2 = d->by1 = d->by2 = 0;   /* biquad state               */
    d->nbits   = 0;
    d->state   = AIS_SEARCH;
    d->prev_lvl = 0;
    d->prev_bit = 0;
    d->ones    = 0;
    d->total   = 0;
    d->nSlices = 0;
    d->nFlags  = 0;
    d->crcOk   = 1;
    d->showBad = 0;           /* app sets 1 for on-air bring-up             */
}

void ais_rearm(ais_t *d)
{
    d->state = AIS_SEARCH;    /* frame[], meanQ8, prev_lvl, prev_bit kept   */
    d->nbits = 0;
    d->ones  = 0;
    d->acc   = 0;
}

/* One NRZI-decoded bit. Bit-oriented HDLC:
 *  - collect bits while in frame,
 *  - drop the stuffed 0 after five 1s,
 *  - a 0 after six 1s closes the frame: the 7 flag bits (leading 0 + six
 *    1s) were pushed optimistically, pop them, then CRC-check. */
static void ais_bit(ais_t *d, uint8_t b)
{
    if (d->state == AIS_SEARCH) {
        /* Hunt 01111110 with the same ones counter: entering a frame on a
         * closing flag is fine, CRC rejects the junk. */
        if (b) {
            if (d->ones < 255u) d->ones++;
            return;
        }
        if (d->ones == FLAG7E) {
            d->state = AIS_DATA;
            d->nbits = 0;
            if (d->nFlags < 65535u) d->nFlags++;
        }
        d->ones = 0;
        return;
    }

    /* AIS_DATA */
    if (b) {
        if (d->nbits < AIS_FRAME_MAX * 8u) {
            uint8_t *p = &d->frame[d->nbits >> 3];
            if ((d->nbits & 7u) == 0u) *p = 0;   /* fresh byte */
            *p = (uint8_t)(*p | (uint8_t)(0x80u >> (d->nbits & 7u)));
            d->nbits++;
        }
        d->ones++;
        if (d->ones > FLAG7E) {                  /* 7 ones: abort          */
            d->state = AIS_SEARCH;
            d->nbits = 0;
            d->ones  = 0;
        }
        return;
    }

    /* b == 0 */
    if (d->ones == FLAG7E) {                     /* 01111110: close frame  */
        d->ones = 0;
        if (d->nbits >= 7u) d->nbits = (uint16_t)(d->nbits - 7u);
        else                d->nbits = 0;
        if (d->nbits >= MIN_FRAME &&
            (d->nbits & 7u) == 0u &&
            crc16(d->frame, (uint16_t)(d->nbits >> 3)) == 0xF0B8u) {
            d->crcOk = 1;
            d->state = AIS_DONE;
            if (d->total < 255u) d->total++;
        } else if (d->showBad && d->nbits >= MIN_FRAME && (d->nbits & 7u) == 0u) {
            /* On-air bring-up: a byte-aligned frame of plausible length whose
             * CRC failed is still surfaced (crcOk = 0) so the UI can show the
             * extracted fields marked '!' instead of staying dark. Host tests
             * leave showBad = 0, so their behaviour is unchanged. */
            d->crcOk = 0;
            d->state = AIS_DONE;
        } else {
            /* Bad frame: the flag that closed it also opens the next
             * frame, so stay in AIS_DATA with an empty buffer. */
            d->nbits = 0;
        }
        return;
    }
    if (d->ones == 5u) {                         /* stuffed bit: drop     */
        d->ones = 0;
        return;
    }
    /* plain data 0 */
    if (d->nbits < AIS_FRAME_MAX * 8u) {
        uint8_t *p = &d->frame[d->nbits >> 3];
        if ((d->nbits & 7u) == 0u) *p = 0;
        /* bit already 0 */
        d->nbits++;
    }
    d->ones = 0;
}

bool ais_push(ais_t *d, uint16_t sample)
{
    if (d->state == AIS_DONE) return true;

    /* DC block: rounded leaky mean of the Q8 input. */
    int32_t xq = (int32_t)sample << 8;
    if (d->meanQ8 < 0) d->meanQ8 = xq;
    int32_t e = xq - d->meanQ8;
    d->meanQ8 += (e + (1 << (MEAN_SHR - 1))) >> MEAN_SHR;
    int32_t v = e >> 4;

    /* Pre-filter (low-pass biquad, Q14): strip out-of-band noise. */
#ifdef AIS_NO_LPF
    int32_t y = v;
#else
    int32_t y = (LPF_B0 * v + LPF_B1 * d->bx1 + LPF_B2 * d->bx2
                 - LPF_A1 * d->by1 - LPF_A2 * d->by2) >> 14;
    d->bx2 = d->bx1; d->bx1 = v;
    d->by2 = d->by1; d->by1 = y;
#endif

    /* Slice on the filtered signal (timing above stays on the raw v). */
    int32_t yc = y;

    /* Zero-crossing DPLL: pull the slot phase toward each sign change.
     * Use the UNfiltered v for timing (the pre-filter's group delay would
     * otherwise shift the crossings and break the loop); the filtered yc only
     * feeds the integrator below. */
    uint8_t s = (uint8_t)(v > 0);
    if (s != d->prev_lvl) {
        int32_t err = d->ph;
        if (err > SLOT / 2) err -= SLOT;
        d->ph -= err >> PLL_K;
        if (d->ph < 0) d->ph += SLOT;
        d->prev_lvl = s;
    }

    /* Integrate over the symbol, slice on slot end, NRZI decode. */
    d->acc += yc;
    d->ph  += 256;
    if (d->ph >= SLOT) {
        d->ph -= SLOT;
        uint8_t bit = (uint8_t)(d->acc > 0);
        d->acc = 0;
        uint8_t raw = (uint8_t)(bit == d->prev_bit);
        d->prev_bit = bit;
        if (d->nSlices < 65535u) d->nSlices++;   /* bring-up: bits sliced   */
        ais_bit(d, raw);
    }
    return d->state == AIS_DONE;
}

/* ---- field extraction ---- */

/* Read w bits at bit offset off, MSB-first (transmission order). */
static uint32_t getbits(const uint8_t *f, uint16_t off, uint8_t w)
{
    uint32_t v = 0;
    for (uint8_t i = 0; i < w; i++) {
        uint16_t p = (uint16_t)(off + i);
        v = (v << 1) | (uint32_t)((f[p >> 3] >> (7u - (p & 7u))) & 1u);
    }
    return v;
}

static int32_t getsbits(const uint8_t *f, uint16_t off, uint8_t w)
{
    uint32_t v = getbits(f, off, w);
    if (v & (1u << (w - 1u))) v |= ~((1u << w) - 1u);
    return (int32_t)v;
}

/* AIS 6-bit text: v<32 -> v+64 else v; then rstrip '@' and strip spaces
 * (pyais _SIXBIT_CHAR + get_str). out holds nchars+1 bytes. */
static void getstr(const uint8_t *f, uint16_t off, uint8_t nchars, char *out)
{
    for (uint8_t i = 0; i < nchars; i++) {
        uint32_t v = getbits(f, (uint16_t)(off + 6u * i), 6u);
        out[i] = (char)(v < 32u ? v + 64u : v);
    }
    out[nchars] = '\0';
    uint8_t n = nchars;
    while (n > 0u && (out[n - 1u] == '@' || out[n - 1u] == ' ')) out[--n] = '\0';
    uint8_t k = 0;
    while (out[k] == ' ') k++;
    if (k) {
        uint8_t j = 0;
        while (out[j + k]) { out[j] = out[j + k]; j++; }
        out[j] = '\0';
    }
}

bool ais_parse(const ais_t *d, ais_info_t *out)
{
    if (d->nbits < 16u) return false;
    uint8_t type = (uint8_t)getbits(d->frame, 0, 6);
    out->type   = type;
    out->repeat = (uint8_t)getbits(d->frame, 6, 2);
    out->mmsi   = getbits(d->frame, 8, 30);
    out->hasPos = 0;
    out->name[0] = '\0';
    out->dest[0] = '\0';

    if (type == 1u || type == 3u) {
        if (d->nbits < 168u + 16u) return false;  /* content + FCS */
        out->nav = (uint8_t)getbits(d->frame, 38, 4);
        out->sog = (uint16_t)getbits(d->frame, 50, 10);
        out->lonS = getsbits(d->frame, 61, 28);
        out->latS = getsbits(d->frame, 89, 27);
        out->cog  = (uint16_t)getbits(d->frame, 116, 12);
        out->hasPos = 1;
        return true;
    }
    if (type == 5u) {
        if (d->nbits < 424u + 16u) return false;
        getstr(d->frame, 112, 20, out->name);
        getstr(d->frame, 302, 20, out->dest);
        return true;
    }
    return false;
}
