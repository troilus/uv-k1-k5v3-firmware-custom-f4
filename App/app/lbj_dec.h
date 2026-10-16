/* LBJ / POCSAG 1200 baud decoder core (resident copy for the LBJ dual-watch
 * feature). Transcibed from the LBJ RX overlay app (App/apps/lbj/lbj_dec.c),
 * itself from Sdr-Is-Fun's RTL_SDR_LBJ_RECEIVER.
 *
 * POCSAG codeword: [1 id][20 data][10 BCH][1 even parity], MSB first.
 *   id=0 address word: data20 = ((addr>>3)<<2) | func
 *   id=1 message word: data20 = 5 BCD nibbles, each bit-reversed.
 * BCH(31,21), generator feedback 873; single-bit correction, the trailing
 * even-parity bit checked too. err is set when a word could not be corrected at
 * all, cor when one was repaired, so the UI can mark a report '*' (repaired) or
 * '!' (corrupt) after the direction. A word that cannot be corrected is not
 * dropped: its 20 bits are lost in place and show as '?', so the rest of the
 * report survives (soft erasure, upstream).
 *
 * [deferred BCH] The sampler runs in a 19.2 kHz timer interrupt whose period is
 * 52 us, and a BCH search is far longer than that (up to 31 syndromes, ~160 us);
 * correcting a word inside the ISR would make the sampler miss samples. So
 * lbj_rx_bit() (ISR context) only aligns the bit stream and queues the raw
 * 32-bit words, and lbj_process() (main-loop context) runs the BCH and the word
 * state machine. #12 runs three of these side by side, one per slicer.
 */
#ifndef LBJ_DEC_H
#define LBJ_DEC_H

#include <stdint.h>
#include <stdbool.h>

#define LBJ_CWMAX   14u     /* message codewords kept per report (70 BCD chars) */
#define LBJ_CWQ      8u     /* raw words the ISR may queue ahead of the loop  */

typedef struct {
    uint32_t sr;
    uint32_t cws[LBJ_CWMAX];
    uint32_t addr;
    uint16_t hunt;
    uint8_t  n, nb, state, pol, wc, fp, inmsg, err, cor, func;
    uint32_t q[LBJ_CWQ];    /* raw words queued by the ISR, decoded by the loop */
    uint8_t  qh, qt;        /* queue head, tail (mod LBJ_CWQ)                  */
    uint8_t  synced;        /* the ISR found a sync word since the last process */
    uint8_t  flushReq;      /* the ISR timed a message out; the loop flushes it */
} lbj_rx_t;

void lbj_rx_bit(lbj_rx_t *r, uint8_t bit);   /* ISR context: align + queue     */
void lbj_process(lbj_rx_t *r);               /* main loop: BCH + state machine */

/* Provided by lbj_dual.c: one call per completed report. */
void lbj_emit_msg(const lbj_rx_t *r, const char *bcd, uint16_t len);

#endif /* LBJ_DEC_H */
