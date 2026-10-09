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

/* LBJ / POCSAG 1200 baud decoder core. Transcibed from the model
 * (test/model_rx.py + test/pocsag.py, itself from Sdr-Is-Fun's
 * RTL_SDR_LBJ_RECEIVER) and from APRS RX's HDLC/DPLL structure.
 *
 * POCSAG codeword: [1 id][20 data][10 BCH][1 even parity], MSB first.
 *   id=0 address word: data20 = ((addr>>3)<<2) | func
 *   id=1 message word: data20 = 5 BCD nibbles, each bit-reversed.
 * BCH(31,21), generator feedback 873; single-bit correction.
 */
#ifndef LBJ_DEC_H
#define LBJ_DEC_H

#include <stdint.h>
#include <stdbool.h>

#define LBJ_CWMAX   20u     /* message codewords kept per report (100 BCD chars) */

/* classification of a processed codeword (Raw CW debug page) */
enum { LBJ_CLS_SYNC = 0u, LBJ_CLS_IDLE = 1u, LBJ_CLS_ADDR = 2u, LBJ_CLS_MSG = 3u };

typedef struct {
    uint32_t sr;
    uint32_t cws[LBJ_CWMAX];
    uint32_t addr;
    uint16_t syn_lut[31];
    uint16_t hunt;
    uint8_t  n, nb, state, pol, wc, fp, inmsg, err, func;
    uint8_t  lut_ready;
    /* counters shown on the debug pages */
    uint16_t syncs, words, ok, fix, bad, msgs, up, dn;
} lbj_rx_t;

void lbj_rx_init(lbj_rx_t *r);
void lbj_rx_bit(lbj_rx_t *r, uint8_t bit);

/* Provided by the app (same translation unit): one call per decoded word /
 * completed message, for the raw ring and the history. */
void lbj_emit_word(uint32_t cw, uint8_t cls, uint8_t ok);
void lbj_emit_msg(const lbj_rx_t *r, const char *bcd, uint16_t len);

#endif /* LBJ_DEC_H */
