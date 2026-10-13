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

/* Generic POCSAG 512/1200/2400 decoder core (rate-independent). The bit clock
 * comes from the demodulator; this file only sees one hard bit per bit period.
 * Keeps the raw message codewords so the app can render numeric and
 * alphanumeric payloads and a raw hex page.
 *
 * POCSAG codeword: [1 id][20 data][10 BCH][1 even parity], MSB first.
 *   id=0 address word: data20 = ((addr>>3)<<2) | func
 *   id=1 message word: numeric 4-bit BCD / alphanumeric 7-bit ASCII stream.
 * BCH(31,21), generator feedback 873; single-bit correction.
 */
#ifndef POCSAG_DEC_H
#define POCSAG_DEC_H

#include <stdint.h>
#include <stdbool.h>

#define POC_CWMAX   16u     /* message codewords kept per report (320 data bits) */

typedef struct {
    uint32_t sr;
    uint32_t cws[POC_CWMAX];
    uint32_t addr;          /* decoded RIC (address), any value                    */
    uint16_t hunt;
    uint16_t syncs, words, ok, fix, bad, msgs;   /* debug counters               */
    uint8_t  n, nb, state, pol, wc, fp, inmsg, err, func, rate;
} poc_rx_t;

/* rate is a caller tag (0=512, 1=1200, 2=2400); stored for poc_emit_msg. */
void poc_rx_start(poc_rx_t *r, uint8_t rate);

/* One demodulated bit. */
void poc_rx_bit(poc_rx_t *r, uint8_t bit);

/* Provided by the app (same translation unit): one call per completed message. */
void poc_emit_msg(const poc_rx_t *r);

#endif /* POCSAG_DEC_H */
