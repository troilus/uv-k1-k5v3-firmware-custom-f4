/* LBJ / POCSAG 1200 baud decoder core (resident copy for the LBJ dual-watch
 * feature). Transcibed from the LBJ RX overlay app (App/apps/lbj/lbj_dec.c),
 * itself from Sdr-Is-Fun's RTL_SDR_LBJ_RECEIVER.
 *
 * POCSAG codeword: [1 id][20 data][10 BCH][1 even parity], MSB first.
 *   id=0 address word: data20 = ((addr>>3)<<2) | func
 *   id=1 message word: data20 = 5 BCD nibbles, each bit-reversed.
 * BCH(31,21), generator feedback 873; single-bit correction. err is set when a
 * word could not be corrected at all, cor when one was repaired, so the UI can
 * mark a report '*' (repaired) or '!' (corrupt) after the direction.
 */
#ifndef LBJ_DEC_H
#define LBJ_DEC_H

#include <stdint.h>
#include <stdbool.h>

#define LBJ_CWMAX   14u     /* message codewords kept per report (70 BCD chars) */

typedef struct {
    uint32_t sr;
    uint32_t cws[LBJ_CWMAX];
    uint32_t addr;
    uint16_t hunt;
    uint8_t  n, nb, state, pol, wc, fp, inmsg, err, cor, func;
} lbj_rx_t;

void lbj_rx_bit(lbj_rx_t *r, uint8_t bit);

/* Provided by lbj_dual.c: one call per completed message. */
void lbj_emit_msg(const lbj_rx_t *r, const char *bcd, uint16_t len);

#endif /* LBJ_DEC_H */
