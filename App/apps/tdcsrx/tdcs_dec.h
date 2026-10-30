/* TDCS RX overlay app - the FFSK decoder core.
 *
 * Same model as the resident TDCS mode (App/app/tdcs_dual.c): 1200 baud,
 * 1200 Hz mark / 1800 Hz space, 9.6 kHz samples through the APRS RX front end
 * (band-pass, two sliding correlators, per-tone AGC, three slicers with their
 * own DPLL), then the 31/32-bit sync, BCH(26,16) codewords and the CRC-16.
 *
 * The app runs the demodulator from its sampling loop instead of a TIM3
 * interrupt: the overlay cannot own a vector, so every sample is pushed
 * through tdcs_dec_sample() by hand.
 */
#ifndef TDCS_DEC_H
#define TDCS_DEC_H

#include <stdint.h>

#define TDCS_FRAME_BYTES  45u
#define TDCS_FRAME_PAD    46u
#define TDCS_CW_BITS      26u
#define TDCS_CW_COUNT     23u
#define TDCS_SLICERS       2u

typedef struct {
    uint32_t cw;
    uint8_t  cw_bits, cw_index, total_cw, nbytes, done;
    uint8_t  frame[TDCS_FRAME_PAD];
} tdcs_col_t;

typedef struct {
    uint32_t   sr;
    uint8_t    state, inv;
    tdcs_col_t col;
} tdcs_fr_t;

typedef struct {
    int32_t dc;
    int32_t x1, x2, y1, y2;
    int32_t im, qm, is, qs;
    int32_t pm, ps;
    uint8_t r, ks;
    int16_t ring[8][4];
} tdcs_dem_t;

typedef struct {
    int32_t dprev, phase;
    uint8_t wa, wb;
} tdcs_sl_t;

/* One decoded A-class frame. */
typedef struct {
    uint8_t  cls[4];
    uint32_t train_no;
    uint32_t km_abs;
    uint16_t loco_no, weight, length10, driver, speed;
    uint8_t  loco_type, loco_ext, crew;
    uint8_t  km_valid, km_neg, km_inc;
    uint8_t  cars, station, station_ext, section, route, driver_ext;
    uint8_t  sig, sig_type, test, crc_ok;
} tdcs_msg_t;

typedef struct {
    tdcs_dem_t dem;
    tdcs_sl_t  sl[TDCS_SLICERS];
    tdcs_fr_t  fr[TDCS_SLICERS];
} tdcs_dec_t;

typedef void (*tdcs_on_msg_t)(const tdcs_msg_t *m);

void tdcs_dec_init(tdcs_dec_t *d);
void tdcs_dec_sample(tdcs_dec_t *d, int32_t adc, tdcs_on_msg_t on_msg);

#endif /* TDCS_DEC_H */
