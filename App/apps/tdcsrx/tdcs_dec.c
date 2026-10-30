/* TDCS RX overlay app - the FFSK decoder core. See tdcs_dec.h. */
#include <stddef.h>

#include "tdcs_dec.h"

#define TDCS_SYNC        0x0DD4259Fu   /* 31/32-bit sync, MSB first          */
#define TDCS_SYNC31_INV  0x722BDA60u
#define TDCS_SYNC32_INV  0xF22BDA60u
#define TDCS_BCH_GEN     0x05B9u

/* APRS RX front end, retuned to 1200/1800 Hz at 9.6 kHz. */
#define BP_B0    5130
#define BP_A1  (-12870)
#define BP_A2    6125
#define AGC_ATT     5
#define AGC_DEC    10
#define PK_MIN     16
#define PLL_STEP 8192
#define PLL_CTR 36864
#define PLL_SHIFT   2

static const int8_t COS1200[8] = { 127, 90, 0, -90, -127, -90, 0, 90 };
static const int8_t COS1800[16] = {
    127, 49, -90, -117, 0, 117, 90, -49,
    -127, -49, 90, 117, 0, -117, -90, 49
};

/* ---- bits ------------------------------------------------------------- */

static uint8_t pc32(uint32_t x)
{
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;
    return (uint8_t)((x * 0x01010101u) >> 24);
}

static int32_t mag(int32_t i, int32_t q)
{
    if (i < 0) i = -i;
    if (q < 0) q = -q;
    if (i < q) { int32_t t = i; i = q; q = t; }
    return i + (q >> 2) + (q >> 3);
}

static int32_t tone_mag(int32_t i, int32_t q, int32_t *pk)
{
    const int32_t mt = mag(i, q);
    int32_t p = *pk;
    p += (mt > p) ? ((mt - p) >> AGC_ATT) : -(p >> AGC_DEC);
    if (p < PK_MIN)
        p = PK_MIN;
    *pk = p;
    return mt;
}

/* ---- BCH(26,16), generator 0x5B9 -------------------------------------- */

static uint16_t bch_syn(uint32_t cw)
{
    uint32_t r = cw & 0x3FFFFFFu;
    for (int i = 25; i >= 10; i--)
        if ((r >> i) & 1u)
            r ^= (uint32_t)TDCS_BCH_GEN << (i - 10);
    return (uint16_t)(r & 0x3FFu);
}

static uint16_t bch_decode(uint32_t cw)
{
    const uint16_t s = bch_syn(cw);
    if (s == 0u)
        return (uint16_t)((cw >> 10) & 0xFFFFu);

    uint16_t syn1[TDCS_CW_BITS];
    for (uint8_t p = 0; p < TDCS_CW_BITS; p++)
        syn1[p] = bch_syn(1u << p);

    for (uint8_t p = 0; p < TDCS_CW_BITS; p++)
        if (syn1[p] == s)
            return (uint16_t)(((cw ^ (1u << p)) >> 10) & 0xFFFFu);

    for (uint8_t p = 0; p < TDCS_CW_BITS; p++)
        for (uint8_t q = (uint8_t)(p + 1u); q < TDCS_CW_BITS; q++)
            if ((uint16_t)(syn1[p] ^ syn1[q]) == s)
                return (uint16_t)(((cw ^ (1u << p) ^ (1u << q)) >> 10) & 0xFFFFu);

    /* Uncorrectable: keep the raw data, the CRC has the last word. */
    return (uint16_t)((cw >> 10) & 0xFFFFu);
}

/* ---- frame ------------------------------------------------------------ */

static uint16_t crc16(const uint8_t *p, uint16_t n)
{
    uint16_t crc = 0u;
    while (n--) {
        crc ^= (uint16_t)(*p++) << 8;
        for (uint8_t i = 0; i < 8u; i++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
    }
    return crc;
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t le24(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

/* Parse one 45-byte frame.  Returns 0 when it is not an A-class TDCS frame,
 * 1 when it is, and the caller gets the message. */
static uint8_t tdcs_parse(const uint8_t *f, uint8_t len, tdcs_msg_t *m)
{
    if (len < TDCS_FRAME_BYTES)
        return 0u;
    if (f[0] != 0x0Cu || f[1] != 43u || f[10] != 32u)
        return 0u;

    const uint8_t test = (f[7] == 0xFFu && f[8] == 0xFFu && f[9] == 0xFFu);
    if (!test && (f[7] != 0x1Fu || f[8] != 0x8Cu || f[9] != 0x30u))
        return 0u;
    if ((uint16_t)f[1] + 2u != (uint16_t)f[10] + 13u)
        return 0u;

    const uint16_t rx = (uint16_t)(((uint16_t)f[43] << 8) | f[44]);
    if (crc16(f, TDCS_FRAME_BYTES - 2u) != rx)
        return 0u;

    const uint8_t *p = f + 11u;
    m->sig       = (uint8_t)(p[0] & 0x0Fu);
    m->sig_type  = (uint8_t)(p[1] & 0x07u);
    m->crew      = (uint8_t)(p[2] & 0x03u);
    for (uint8_t i = 0; i < 4u; i++)
        m->cls[i] = p[3u + i];
    m->train_no  = le24(p + 7u);

    /* Train numbers that start with 9 are not shown; the leading zeros of a
     * seven-digit number do not count (9xxxxxx, 09xxxxx, 009xxxx ...).  The
     * leading digit is found by subtracting powers of ten, so no divide
     * helper has to be linked. */
    {
        static const uint32_t PW[7] = {
            1000000u, 100000u, 10000u, 1000u, 100u, 10u, 1u
        };
        uint32_t t = m->train_no;
        uint8_t k = 0, d = 0;
        while (k < 6u && t < PW[k]) k++;
        while (t >= PW[k]) { t -= PW[k]; d++; }
        if (d == 9u)
            return 0u;
    }

    m->loco_type = p[10];
    m->loco_ext  = p[11];
    m->loco_no   = le16(p + 12u);

    m->km_valid  = (p[14] != 0xFFu || p[15] != 0xFFu || p[16] != 0xFFu) ? 1u : 0u;
    m->km_neg    = (p[16] & 0x80u) ? 1u : 0u;
    m->km_inc    = (p[16] & 0x40u) ? 1u : 0u;
    m->km_abs    = (uint32_t)(p[14] | ((uint16_t)p[15] << 8) |
                              ((uint16_t)(p[16] & 0x3Fu) << 16));

    m->speed     = (uint16_t)(le24(p + 17u) & 0x3FFu);
    m->weight    = le16(p + 20u);
    m->length10  = le16(p + 22u);
    m->cars      = p[24];
    m->station   = p[25];
    m->station_ext = p[26];
    m->section   = p[27];
    m->route     = p[28];
    m->driver    = le16(p + 29u);
    m->driver_ext = p[31];
    m->test      = test;
    m->crc_ok    = 1u;
    return 1u;
}

/* ---- 31/32-bit sync, then 26-bit BCH codewords ------------------------ */

static void framer_bit(tdcs_fr_t *f, uint8_t bit, tdcs_on_msg_t on_msg)
{
    f->sr = (f->sr << 1) | (bit & 1u);

    if (f->state == 0u) {
        const uint32_t w31 = f->sr & 0x7FFFFFFFu;
        const uint8_t e31n = pc32(w31 ^ TDCS_SYNC);
        const uint8_t e31i = pc32(w31 ^ TDCS_SYNC31_INV);
        const uint8_t e32n = pc32(f->sr ^ TDCS_SYNC);
        const uint8_t e32i = pc32(f->sr ^ TDCS_SYNC32_INV);

        uint8_t best = e31n, inv = 0u;
        if (e31i < best) { best = e31i; inv = 1u; }
        if (e32n < best) { best = e32n; inv = 0u; }
        if (e32i < best) { best = e32i; inv = 1u; }

        if (best <= 2u) {
            f->inv = inv;
            f->state = 1u;
            tdcs_col_t *c = &f->col;
            c->cw = 0u;
            c->cw_bits = c->cw_index = c->total_cw = c->nbytes = 0u;
        }
        return;
    }

    tdcs_col_t *c = &f->col;
    const uint8_t b = f->inv ? (uint8_t)!bit : bit;
    c->cw = (c->cw << 1) | b;
    if (++c->cw_bits < TDCS_CW_BITS)
        return;
    c->cw_bits = 0u;

    const uint16_t data = bch_decode(c->cw);
    c->cw = 0u;

    if ((uint16_t)c->nbytes + 2u <= TDCS_FRAME_PAD) {
        c->frame[c->nbytes++] = (uint8_t)(data >> 8);
        c->frame[c->nbytes++] = (uint8_t)(data & 0xFFu);
    }
    c->cw_index++;

    if (c->cw_index == 1u) {
        if (c->frame[0] != 0x0Cu || c->frame[1] != 43u) {
            f->state = 0u;
            return;
        }
        c->total_cw = (uint8_t)(((uint16_t)c->frame[1] + 3u) / 2u);
        if (c->total_cw < 2u || c->total_cw > TDCS_CW_COUNT) {
            f->state = 0u;
            return;
        }
    }

    if (c->cw_index >= c->total_cw) {
        f->state = 0u;
        tdcs_msg_t m;
        if (tdcs_parse(c->frame, c->nbytes, &m) && on_msg)
            on_msg(&m);
    }
}

/* ---- public ----------------------------------------------------------- */

void tdcs_dec_init(tdcs_dec_t *d)
{
    uint8_t *p = (uint8_t *)d;
    for (unsigned i = 0; i < sizeof *d; i++)
        p[i] = 0u;

    d->dem.dc = 2048 << 4;
    d->dem.pm = d->dem.ps = PK_MIN;
    d->sl[0].wa = 2; d->sl[0].wb = 3;   /* favours space */
#if TDCS_SLICERS > 1
    d->sl[1].wa = 3; d->sl[1].wb = 2;   /* favours mark  */
#endif
#if TDCS_SLICERS > 2
    d->sl[2].wa = 1; d->sl[2].wb = 1;
#endif
}

void tdcs_dec_sample(tdcs_dec_t *d, int32_t adc, tdcs_on_msg_t on_msg)
{
    tdcs_dem_t *m = &d->dem;

    m->dc += ((adc << 4) - m->dc) >> 6;
    const int32_t x = adc - (m->dc >> 4);
    const int32_t y = (BP_B0 * (x - m->x2) - BP_A1 * m->y1 - BP_A2 * m->y2) >> 14;
    m->x2 = m->x1; m->x1 = x;
    m->y2 = m->y1; m->y1 = y;

    int16_t *o = m->ring[m->r];
    int32_t p;
    p = (y * COS1200[m->r]) >> 8;                 m->im += p - o[0]; o[0] = (int16_t)p;
    p = (y * COS1200[(m->r + 6u) & 7u]) >> 8;     m->qm += p - o[1]; o[1] = (int16_t)p;
    p = (y * COS1800[m->ks]) >> 8;                m->is += p - o[2]; o[2] = (int16_t)p;
    p = (y * COS1800[(m->ks + 4u) & 15u]) >> 8;   m->qs += p - o[3]; o[3] = (int16_t)p;
    m->r = (uint8_t)((m->r + 1u) & 7u);
    m->ks = (uint8_t)((m->ks + 1u) & 15u);

    const int32_t mm = tone_mag(m->im, m->qm, &m->pm);
    const int32_t ms = tone_mag(m->is, m->qs, &m->ps);
    const int32_t a = mm * m->ps;
    const int32_t b = ms * m->pm;

    for (uint8_t i = 0; i < TDCS_SLICERS; i++) {
        tdcs_sl_t *s = &d->sl[i];
        const int32_t dd = a * s->wa - b * s->wb;
        s->phase += PLL_STEP;
        if ((dd > 0) != (s->dprev > 0))
            s->phase += (PLL_CTR - s->phase) >> PLL_SHIFT;
        s->dprev = dd;
        if (s->phase >= 65536) {
            s->phase -= 65536;
            framer_bit(&d->fr[i], dd > 0, on_msg);
        }
    }
}
