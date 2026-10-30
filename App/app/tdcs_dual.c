/* TDCS receive mode (resident firmware feature) - see tdcs_dual.h.
 *
 * 450 MHz train-number check system, TB/T 3325-2013 A-class frame, on-air
 * format per TDCS_FFSK_params.md:
 *
 *  160 bit  preamble (TB/T 3052 uses 51 bit; the sync hunt does not count it)
 *   32 bit  frame sync, 0DD4259F, MSB first, popcount <= 2 (inverted >= 30)
 *           TB/T 3052 specifies 31 bit; both 31/32-bit alignments are tried
 *   26 bit  BCH(26,16) codeword, 16 data bits (MSB first) + 10 parity bits
 *           generator 0x5B9, d_min 3
 *  ...     23 codewords for the 45-byte TDCS frame (last data byte padded 0x00)
 *
 * Decoded frame (45 bytes):
 *    0     0C   frame start
 *    1     43   frame length excluding CRC; total = f[1] + 2 = 45
 *    2     --   station address (variable)
 *    3..6  --   locomotive address (variable)
 *    7     1F   signature (FF in a test frame)
 *    8     8C   signature (FF in a test frame)
 *    9     30   information function code (FF in a test frame)
 *   10     32   information length; total = f[10] + 13 = 45
 *   11..42 payload (32 bytes, offsets below)
 *   43..44 CRC-16/XMODEM, poly 1021, init 0, big-endian, over f[0..42]
 *
 * Payload:
 *   0 signal aspect (low nibble)      1 signal type (low 3 bits)
 *   2 crew/passenger bits             3..6 train class (4 ASCII)
 *   7..9 train number (24 bit LE)    10 loco type
 *  11 loco type extension            12..13 loco number (16 bit LE)
 *  14..16 kilometre post (24 bit LE) 17..19 speed (24 bit LE, low 10 bits)
 *  20..21 total weight               22..23 length (16 bit LE, 0.1)
 *  24 cars                           25 station
 *  26 station extension              27 section
 *  28 actual route                   29..30 driver number (16 bit LE)
 *  31 driver extension
 *
 * FFSK: 1200 baud, 1200 Hz mark / 1800 Hz space, direct FSK (no NRZI/HDLC).
 * The PA4 sampler runs at 9.6 kHz (8 samples per bit) with the normal FM audio
 * path (STD), as APRS RX: band-pass, 1200/1800 sliding correlators, per-tone
 * AGC and three slicers (2:3, 1:1, 3:2), each with its own DPLL.  The ISR only
 * produces bits; sync, BCH, CRC and field parsing run in the 10 ms tick.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "external/printf/printf.h"

#include "tdcs_dual.h"
#include "pa4_sampler.h"

#include "driver/backlight.h"
#include "driver/bk4819.h"
#include "driver/keyboard.h"
#include "driver/st7565.h"
#include "functions.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/helper.h"
#include "ui/ui.h"

#ifdef ENABLE_FEAT_F4HWN_LBJ
#include "lbj_dual.h"   /* LBJ and TDCS share the screen: one window at a time */
#endif
#ifdef ENABLE_FMRADIO
#include "app/fm.h"     /* gFmRadioMode: the window is not drawn over the FM radio */
#endif

/* ---- receiver ---------------------------------------------------------- */
#define TDCS_MAX_TICKS       6000u   /* 60 s watchdog                        */
#define TDCS_LOST_DEBOUNCE     20u   /* squelch closed for 200 ms -> stop    */
#define TDCS_BITQ              32u
#define TDCS_HISTORY            8u
#define TDCS_MERGE_SECS        30u
#define TDCS_FIELDS            20u
/* gFrameBuffer is FRAME_LINES (7) rows, 0..6, and LCD page 0 is the status
 * line: a write to row 7 runs 128 bytes past the array and lands on whatever
 * the linker put behind it.  Fields take rows 0..5, the footer row 6. */
#define TDCS_ROWS               6u
#define TDCS_FOOTER             6u
#define TDCS_PAGES              4u
_Static_assert(TDCS_FOOTER < FRAME_LINES, "TDCS popup must fit gFrameBuffer");

/* Popup text positions, the LBJ window's arrangement: the page number on the
 * top right, and the footer split into history index / age / EXIT. */
#define TDCS_PAGE_X           100u   /* "P1/4", 4 x 7 px, hard right        */
#define TDCS_AGE_X             52u   /* "12s" / "3m05s", middle             */
#define TDCS_TEST_X            24u   /* "测试" beside the index, test frames */
#define TDCS_EXIT_X           100u   /* EXIT, hard right (100..127)         */

#define TDCS_FRAME_BYTES       45u   /* TDCS frame without the BCH pad byte  */
#define TDCS_FRAME_PAD         46u   /* 23 codewords x 2 bytes               */
#define TDCS_CW_BITS           26u
#define TDCS_CW_COUNT          23u
#define TDCS_BCH_GEN       0x5B9u
#define TDCS_SYNC       0x0DD4259Fu  /* 31/32-bit sync, MSB first            */
#define TDCS_SYNC31_INV 0x722BDA60u  /* 31-bit complement                    */
#define TDCS_SYNC32_INV 0xF22BDA60u  /* 32-bit complement                    */

/* APRS RX demodulator, retuned to 1200/1800 Hz at 9.6 kHz. */
#define TDCS_SLICERS            3u
#define TDCS_AGC_ATT            5
#define TDCS_AGC_DEC           10
#define TDCS_PK_MIN            16
#define TDCS_PLL_STEP        8192    /* 65536 / 8 samples per bit            */
#define TDCS_PLL_CTR        36864    /* transitions pulled to mid-bit        */
#define TDCS_PLL_SHIFT          2

/* Band-pass 1470 Hz, Q 0.9 at 9.6 kHz, Q14 (b2 = -b0, b1 = 0). */
#define BP_B0                5130
#define BP_A1              (-12870)
#define BP_A2                6125

/* APRS RX-style cosine tables: 127*cos(2*pi*f*n/9600).
 * 1200 Hz: 8 entries = 1 cycle; sine is +6 entries.
 * 1800 Hz: 16 entries = 3 cycles; sine is +4 entries (3/4 cycle). */
static const int8_t TDCS_COS1200[8] = {
    127, 90, 0, -90, -127, -90, 0, 90
};
static const int8_t TDCS_COS1800[16] = {
    127, 49, -90, -117, 0, 117, 90, -49,
    -127, -49, 90, 117, 0, -117, -90, 49
};

/* ---- frame sync / BCH ------------------------------------------------- */

typedef struct {
    int32_t  dc;
    int32_t  x1, x2, y1, y2;
    int32_t  im, qm, is, qs;
    int32_t  pm, ps;
    uint8_t  r, ks;
    int16_t  ring[8][4];
} tdcs_dem_t;

typedef struct {
    int32_t  dprev;
    int32_t  phase;
    uint8_t  wa, wb;
} tdcs_slice_t;

typedef struct {
    uint32_t cw;               /* current 26-bit BCH codeword             */
    uint8_t  done;
    uint8_t  cw_bits;
    uint8_t  cw_index;
    uint8_t  total_cw;
    uint8_t  nbytes;
    uint8_t  frame[TDCS_FRAME_PAD];
} tdcs_collector_t;

typedef struct {
    uint32_t sr;               /* 32-bit sync shift register              */
    uint8_t  state;            /* 0 hunt, 1 collect codewords             */
    uint8_t  inv;              /* sync matched inverted                   */
    tdcs_collector_t col;      /* the 31/32-bit sync alignment            */
} tdcs_framer_t;

typedef struct {
    uint8_t  sig, sig_type, crew;
    uint8_t  loco_type, loco_ext;
    uint8_t  cars, station, station_ext, section, route, driver_ext;
    uint8_t  test, crc_ok, km_valid, km_neg, km_inc;
    uint16_t loco_no, speed, weight, length10, driver;
    uint32_t km_abs, train_no;
    char     cls[5];
} tdcs_msg_t;

typedef struct {
    tdcs_msg_t m;
    uint16_t   sec;
    int16_t    rssi;
} tdcs_rec_t;

static struct {
    tdcs_dem_t    dem;
    tdcs_slice_t  sl[TDCS_SLICERS];
    tdcs_framer_t fr[TDCS_SLICERS];
    volatile uint8_t bits[TDCS_SLICERS][TDCS_BITQ];
    volatile uint8_t qh[TDCS_SLICERS], qt[TDCS_SLICERS];
    tdcs_rec_t    rec[TDCS_HISTORY];
    uint8_t       count, cur, page;
    bool          popup, dismissed, dirty, on;
    bool          pinned;             /* opened by hand: TCDPOP does not apply */
    uint8_t       ledPhase, ledTick, ledVfo;
    uint16_t      ticks, lostTicks;
    uint16_t      sec, subSec;
    uint16_t      popSec;
} g;

/* ---- tiny helpers ------------------------------------------------------ */
static uint8_t pc32(uint32_t x)
{
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;
    return (uint8_t)((x * 0x01010101u) >> 24);
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t le24(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

static uint16_t crc16(const uint8_t *p, uint16_t n, uint16_t init)
{
    uint16_t crc = init;
    while (n--) {
        crc ^= (uint16_t)(*p++) << 8;
        for (uint8_t i = 0; i < 8u; i++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
    }
    return crc;
}

/* |I| + |Q| approximation, as APRS RX. */
static int32_t mag(int32_t i, int32_t q)
{
    if (i < 0) i = -i;
    if (q < 0) q = -q;
    if (i < q) { int32_t t = i; i = q; q = t; }
    return i + (q >> 2) + (q >> 3);
}

static char *put(char *o, const char *s)
{
    while (*s)
        *o++ = *s++;
    return o;
}

static char *putu(char *o, uint32_t v)
{
    char t[10];
    uint8_t n = 0;
    do {
        t[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v);
    while (n)
        *o++ = t[--n];
    return o;
}

static char *puthex2(char *o, uint8_t v)
{
    static const char H[] = "0123456789ABCDEF";
    *o++ = H[v >> 4];
    *o++ = H[v & 0x0Fu];
    return o;
}

/* Zero-padded unsigned, width 2..10 digits (train numbers, kilometre posts). */
static char *putpad(char *o, uint32_t v, uint8_t width)
{
    char t[10];
    uint8_t n = 0;
    do {
        t[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v && n < 10u);
    while (n < width)
        t[n++] = '0';
    while (n)
        *o++ = t[--n];
    return o;
}

/* How long ago the report was received, the way the LBJ window prints it:
 * 12s, 3m05s, 99m+ past the hour. */
static char *putage(char *o, uint16_t sec)
{
    if (sec < 60u) {
        o = putu(o, sec);
        *o++ = 's';
        return o;
    }
    uint32_t v = sec;
    const unsigned m = (unsigned)(v / 60u);
    v %= 60u;
    if (m > 99u)
        return put(o, "99m+");
    o = putu(o, m);
    *o++ = 'm';
    if (v < 10u)
        *o++ = '0';
    o = putu(o, v);
    *o++ = 's';
    return o;
}

/* ---- bit queues (ISR producer, main-loop consumer) --------------------- */
static void push_bit(uint8_t si, uint8_t b)
{
    const uint8_t nh = (uint8_t)((g.qh[si] + 1u) & (TDCS_BITQ - 1u));
    if (nh == g.qt[si])
        g.qt[si] = (uint8_t)((g.qt[si] + 1u) & (TDCS_BITQ - 1u));
    g.bits[si][g.qh[si]] = b;
    g.qh[si] = nh;
}

static bool pop_bit(uint8_t si, uint8_t *b)
{
    if (g.qt[si] == g.qh[si])
        return false;
    *b = g.bits[si][g.qt[si]];
    g.qt[si] = (uint8_t)((g.qt[si] + 1u) & (TDCS_BITQ - 1u));
    return true;
}

/* One tone magnitude with its slow peak tracker (APRS RX). */
static int32_t tone_mag(int32_t i, int32_t q, int32_t *pk)
{
    const int32_t mt = mag(i, q);
    int32_t p = *pk;
    p += (mt > p) ? ((mt - p) >> TDCS_AGC_ATT) : -(p >> TDCS_AGC_DEC);
    if (p < TDCS_PK_MIN)
        p = TDCS_PK_MIN;
    *pk = p;
    return mt;
}

/* ---- FFSK demodulator: APRS RX front-end retuned to 1200/1800 Hz -------- */
static void tdcs_sample(int32_t adc)
{
    tdcs_dem_t *m = &g.dem;
    m->dc += ((adc << 4) - m->dc) >> 6;
    const int32_t x = adc - (m->dc >> 4);
    const int32_t y = (BP_B0 * (x - m->x2) - BP_A1 * m->y1 - BP_A2 * m->y2) >> 14;
    m->x2 = m->x1; m->x1 = x;
    m->y2 = m->y1; m->y1 = y;

    int16_t *o = m->ring[m->r];
    int32_t p;
    p = (y * TDCS_COS1200[m->r]) >> 8;              m->im += p - o[0]; o[0] = (int16_t)p;
    p = (y * TDCS_COS1200[(m->r + 6u) & 7u]) >> 8;  m->qm += p - o[1]; o[1] = (int16_t)p;
    p = (y * TDCS_COS1800[m->ks]) >> 8;             m->is += p - o[2]; o[2] = (int16_t)p;
    p = (y * TDCS_COS1800[(m->ks + 4u) & 15u]) >> 8; m->qs += p - o[3]; o[3] = (int16_t)p;
    m->r = (uint8_t)((m->r + 1u) & 7u);
    m->ks = (uint8_t)((m->ks + 1u) & 15u);

    const int32_t mm = tone_mag(m->im, m->qm, &m->pm);
    const int32_t ms = tone_mag(m->is, m->qs, &m->ps);
    const int32_t a = mm * m->ps;
    const int32_t b = ms * m->pm;

    for (uint8_t i = 0; i < TDCS_SLICERS; i++) {
        tdcs_slice_t *s = &g.sl[i];
        const int32_t d = a * s->wa - b * s->wb;
        s->phase += TDCS_PLL_STEP;
        if ((d > 0) != (s->dprev > 0))
            s->phase += (TDCS_PLL_CTR - s->phase) >> TDCS_PLL_SHIFT;
        s->dprev = d;
        if (s->phase >= 65536) {
            s->phase -= 65536;
            push_bit(i, d > 0);
        }
    }
}

/* ---- BCH(26,16), generator 0x5B9 --------------------------------------- */
static uint16_t bch_syn(uint32_t cw)
{
    uint32_t r = cw & 0x3FFFFFFu;
    for (int i = 25; i >= 10; i--) {
        if ((r >> i) & 1u)
            r ^= TDCS_BCH_GEN << (i - 10);
    }
    return (uint16_t)(r & 0x3FFu);
}

/* Decode one 26-bit codeword: 16 data bits (MSB first) + 10 parity bits.
 * Correct one or two bit errors; an uncorrectable word keeps its raw data and
 * is left to the CRC. */
static uint16_t bch_decode(uint32_t cw, uint8_t *err)
{
    const uint16_t s = bch_syn(cw);
    if (s == 0u) {
        *err = 0u;
        return (uint16_t)((cw >> 10) & 0xFFFFu);
    }

    uint16_t syn1[TDCS_CW_BITS];
    for (uint8_t p = 0; p < TDCS_CW_BITS; p++)
        syn1[p] = bch_syn(1u << p);

    for (uint8_t p = 0; p < TDCS_CW_BITS; p++) {
        if (syn1[p] == s) {
            cw ^= 1u << p;
            *err = 1u;
            return (uint16_t)((cw >> 10) & 0xFFFFu);
        }
    }
    for (uint8_t p = 0; p < TDCS_CW_BITS; p++) {
        for (uint8_t q = (uint8_t)(p + 1u); q < TDCS_CW_BITS; q++) {
            if ((uint16_t)(syn1[p] ^ syn1[q]) == s) {
                cw ^= (1u << p) | (1u << q);
                *err = 2u;
                return (uint16_t)((cw >> 10) & 0xFFFFu);
            }
        }
    }

    *err = 3u;
    return (uint16_t)((cw >> 10) & 0xFFFFu);
}

/* ---- decoded TDCS frame ------------------------------------------------ */
static void tdcs_store(const tdcs_msg_t *m);

static bool tdcs_frame(const uint8_t *f, uint8_t len)
{
    if (len < TDCS_FRAME_BYTES)
        return false;
    if (f[0] != 0x0Cu || f[1] != 43u || f[10] != 32u)
        return false;
    /* f[2] is the station address and f[3..6] the locomotive address; both
     * vary per locomotive, so they are not checked here. */

    const bool test = (f[7] == 0xFFu && f[8] == 0xFFu && f[9] == 0xFFu);
    if (!test) {
        if (f[7] != 0x1Fu || f[8] != 0x8Cu || f[9] != 0x30u)
            return false;
    }
    if ((uint16_t)f[1] + 2u != (uint16_t)f[10] + 13u)
        return false;

    const uint16_t rx = (uint16_t)(((uint16_t)f[43] << 8) | f[44]);
    const bool ok = (crc16(f, TDCS_FRAME_BYTES - 2u, 0x0000u) == rx);
    if (!ok) {
#ifndef TDCS_ACCEPT_BAD_CRC
        return false;
#endif
    }

    tdcs_msg_t m;
    memset(&m, 0, sizeof m);
    const uint8_t *p = f + 11u;

    m.sig       = (uint8_t)(p[0] & 0x0Fu);
    m.sig_type  = (uint8_t)(p[1] & 0x07u);
    m.crew      = p[2];
    for (uint8_t i = 0; i < 4u; i++)
        m.cls[i] = (char)p[3u + i];
    m.cls[4]    = '\0';
    m.train_no  = le24(p + 7u);
    m.loco_type = p[10];
    m.loco_ext  = p[11];
    m.loco_no   = le16(p + 12u);

    const uint32_t km = le24(p + 14u);
    if (km != 0xFFFFFFu) {
        m.km_valid = 1u;
        m.km_abs   = km & 0x3FFFFFu;
        m.km_neg   = (km & 0x800000u) ? 1u : 0u;
        m.km_inc   = (km & 0x400000u) ? 1u : 0u;
    }

    m.speed      = (uint16_t)(le24(p + 17u) & 0x3FFu);
    m.weight     = le16(p + 20u);
    m.length10   = le16(p + 22u);
    m.cars       = p[24];
    m.station    = p[25];
    m.station_ext= p[26];
    m.section    = p[27];
    m.route      = p[28];
    m.driver     = le16(p + 29u);
    m.driver_ext = p[31];
    m.test       = test ? 1u : 0u;
    m.crc_ok     = ok ? 1u : 0u;

    tdcs_store(&m);
    return true;
}

/* ---- 31/32-bit sync, then 26-bit BCH codewords ------------------------ */
static void tdcs_framer_bit(tdcs_framer_t *f, uint8_t bit)
{
    f->sr = (f->sr << 1) | (bit & 1u);

    if (f->state == 0u) {
        /* TB/T 3052 says 31 bit, the TDCS_FFSK_params notes say 32 bit; try
         * both.  The 32-bit window of a 31-bit sync has the last preamble bit
         * (1) on top, so compare both widths and their complements. */
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
            f->inv   = inv;
            f->state = 1u;
            memset(&f->col, 0, sizeof f->col);
        }
        return;
    }

    tdcs_collector_t *c = &f->col;
    if (c->done) {
        f->state = 0u;
        return;
    }

    const uint8_t b = f->inv ? (uint8_t)!bit : bit;
    c->cw = (c->cw << 1) | b;
    if (++c->cw_bits < TDCS_CW_BITS)
        return;
    c->cw_bits = 0u;

    uint8_t err;
    const uint16_t data = bch_decode(c->cw, &err);
    (void)err;
    c->cw = 0u;

    if ((uint16_t)c->nbytes + 2u <= TDCS_FRAME_PAD) {
        c->frame[c->nbytes++] = (uint8_t)(data >> 8);
        c->frame[c->nbytes++] = (uint8_t)(data & 0xFFu);
    }
    c->cw_index++;

    if (c->cw_index == 1u) {
        /* The first codeword carries f[0] and f[1]. */
        if (c->frame[0] != 0x0Cu || c->frame[1] != 43u) {
            c->done = 1u;
            f->state = 0u;
            return;
        }
        c->total_cw = (uint8_t)(((uint16_t)c->frame[1] + 3u) / 2u);
        if (c->total_cw < 2u || c->total_cw > TDCS_CW_COUNT) {
            c->done = 1u;
            f->state = 0u;
            return;
        }
    }

    if (c->cw_index >= c->total_cw) {
        c->done = 1u;
        f->state = 0u;
        tdcs_frame(c->frame, c->nbytes);
    }
}

/* ---- LED --------------------------------------------------------------- */
/* The same announcement the LBJ window makes: four quick double flashes
 * (100 ms on / 100 off / 100 on / 500 off) when a report decodes, and the
 * carrier state otherwise.  The LED is taken over while the RX VFO is TDCS,
 * the way LBJ does it, so the radio's own blink cannot fight it. */
#define TDCS_LED_PHASES  16u

static const uint8_t TDCS_LED_TICKS[TDCS_LED_PHASES] = {
    10u, 10u, 10u, 50u,
    10u, 10u, 10u, 50u,
    10u, 10u, 10u, 50u,
    10u, 10u, 10u, 50u
};

static void ledBlinkStart(void)
{
    if (g.ledPhase)                  /* a report's repeats merge into one blink */
        return;
    g.ledPhase = TDCS_LED_PHASES;
    g.ledTick  = 0u;
    g.ledVfo   = gEeprom.RX_VFO;     /* latched: the A/B LED of the channel it came on */
}

/* One 10 ms step, from TDCS_Tick(). */
static void ledService(void)
{
    /* Not a TDCS channel, or keyed up (the red TX light is not ours): drop a
     * blink still running and leave the LED to the radio. */
    if (!TDCS_RxVfoIsTdcs() || gCurrentFunction == FUNCTION_TRANSMIT || gPttIsPressed) {
        if (g.ledPhase) {
            g.ledPhase = 0u;
            g.ledTick  = 0u;
            BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
        }
        return;
    }

    if (!g.ledPhase) {
        /* Nothing to announce: the LED just reports the carrier. */
        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, g_SquelchLost);
        BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, g_SquelchLost && gEeprom.RX_VFO != 0u);
        return;
    }

    if (g.ledTick) {
        g.ledTick--;
        return;
    }

    /* Phases 16..1: the first is the LED on, the table says how long each one
     * lasts, and the idle branch above puts the LED back once they have run. */
    const bool on = (g.ledPhase & 1u) == 0u;
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, on);
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, on && g.ledVfo != 0u);
    g.ledTick = TDCS_LED_TICKS[TDCS_LED_PHASES - g.ledPhase];
    g.ledPhase--;
}

/* ---- history ----------------------------------------------------------- */
static void tdcs_store(const tdcs_msg_t *m)
{
    int found = -1;
    for (uint8_t i = 0; i < g.count; i++) {
        if (g.rec[i].m.train_no == m->train_no &&
            g.rec[i].m.loco_no  == m->loco_no &&
            (uint16_t)(g.sec - g.rec[i].sec) <= TDCS_MERGE_SECS) {
            found = (int)i;
            break;
        }
    }

    if (found >= 0) {
        g.rec[found].m    = *m;
        g.rec[found].sec  = g.sec;
        g.rec[found].rssi = (int16_t)(BK4819_GetRSSI_dBm() + dBmCorrTable[gRxVfo->Band]);
        g.cur  = (uint8_t)found;
        g.page = 0u;               /* a new report always jumps to itself */
    } else {
        if (g.count < TDCS_HISTORY)
            g.count++;
        for (uint8_t i = (uint8_t)(g.count - 1u); i > 0u; i--)
            g.rec[i] = g.rec[i - 1u];
        g.rec[0].m    = *m;
        g.rec[0].sec  = g.sec;
        g.rec[0].rssi = (int16_t)(BK4819_GetRSSI_dBm() + dBmCorrTable[gRxVfo->Band]);
        g.cur  = 0u;               /* newest first, and show it */
        g.page = 0u;
    }

    g.dirty = true;
    ledBlinkStart();               /* a report decoded: flash the LED */

    /* LBJ and TDCS share the screen: whichever window is already up keeps it,
     * so the two cannot take turns covering each other.  The frame is still
     * stored, so it is there when the other window closes. */
    if (!g.dismissed) {
#ifdef ENABLE_FEAT_F4HWN_LBJ
        if (LBJ_PopupActive())
            return;
#endif
        if (!g.popup)
            g.pinned = false;      /* opened by a report: TCDPOP applies */
        g.popup = true;
    }
    g.popSec = g.sec;
    BACKLIGHT_TurnOn();
}

/* ---- sampler start/stop ------------------------------------------------ */
/* A 790-bit burst can leave the last BCH codeword one bit short: the DPLL
 * phase wrap that would emit it falls just past the last sample.  The missing
 * bit is the last parity bit, so feeding a zero completes the codeword and the
 * BCH decoder corrects the parity; the CRC is still the final check. */
static void tdcs_flush(void)
{
    for (uint8_t i = 0; i < TDCS_SLICERS; i++) {
        if (g.fr[i].state) {
            tdcs_framer_bit(&g.fr[i], 0u);
            tdcs_framer_bit(&g.fr[i], 0u);
        }
    }
}

static void tdcs_start(void)
{
    memset(g.fr, 0, sizeof g.fr);
    memset(&g.dem, 0, sizeof g.dem);
    memset(g.sl, 0, sizeof g.sl);
    g.dem.dc = 2048 << 4;
    g.dem.pm = g.dem.ps = TDCS_PK_MIN;
    g.sl[0].wa = 2; g.sl[0].wb = 3;   /* favours space */
    g.sl[1].wa = 1; g.sl[1].wb = 1;
    g.sl[2].wa = 3; g.sl[2].wb = 2;   /* favours mark  */
    for (uint8_t i = 0; i < TDCS_SLICERS; i++)
        g.qh[i] = g.qt[i] = 0u;

    if (PA4_StartEx(tdcs_sample, 9600u, false)) {
        g.ticks = 0u;
        g.lostTicks = 0u;
        g.on = true;
    }
}

void TDCS_StopSampler(void)
{
    if (g.on) {
        g.on = false;
        PA4_Stop();
    }
}

bool TDCS_Active(void)
{
    return g.on;
}

bool TDCS_RxVfoIsTdcs(void)
{
    return gRxVfo && gRxVfo->Modulation == MODULATION_TDCS;
}

/* Either VFO is TDCS: dual watch switching the RX VFO must not close the
 * window the operator is reading. */
static bool tdcs_any_vfo(void)
{
    return gEeprom.VfoInfo[0].Modulation == MODULATION_TDCS ||
           gEeprom.VfoInfo[1].Modulation == MODULATION_TDCS;
}

/* ---- decode window ----------------------------------------------------- */
bool TDCS_PopupActive(void)
{
    return g.popup;
}

void TDCS_ClosePopup(void)
{
    if (g.popup) {
        g.popup = false;
        g.dismissed = true;
        /* The window owns the whole screen: repaint the frame and the status
         * line together, or the battery/time row keeps whatever the window
         * left there until the next 2 s status tick. */
        gUpdateDisplay = true;
        gUpdateStatus  = true;
    }
}

/* Manually open the decode window (the TCDMSG menu item and the side key / M
 * Long action the operator can point at it): shows the newest report and stays
 * up until EXIT or PTT - TCDPOP does not apply to it (see g.pinned).  With
 * nothing decoded yet there is no window to show, so the open is refused and
 * the caller double beeps.  FM mode is refused for the same reason as LBJ: the
 * window is drawn over the main screen, so it could swallow the keypad without
 * ever being visible. */
bool TDCS_OpenPopup(void)
{
    if (gFmRadioMode)
        return false;
    if (!g.count)
        return false;

    g.cur       = 0u;
    g.page      = 0u;
    g.popup     = true;
    g.pinned    = true;       /* opened by hand: it never times out */
    g.dismissed = false;      /* an explicit open overrides an earlier EXIT */
    g.dirty     = true;
    return true;
}

/* The decode window: four pages of six fields, the labels in the same 8x8
 * device font the menus use and the values in the 6 px ASCII font.  Values are
 * formatted by hand - the firmware printf has no long-integer support, and a
 * %lu in this page was the first suspect when the full window crashed. */
static const char *sig_name(uint8_t v)
{
    switch (v) {
        case 0: return "始发车";
        case 1: return "编组站";
        case 2: return "出站";
        case 3: return "进站";
        case 4: return "通过";
        case 5: return "预告";
        case 6: return "容许";
        default: return NULL;
    }
}

static const char *sig_type_name(uint8_t v)
{
    switch (v) {
        case 2: return "出站";
        case 3: return "进站";
        case 4: return "通过";
        case 5: return "预告";
        case 6: return "容许";
        default: return NULL;
    }
}

/* Short labels: an 8x8 glyph takes 9 px, so five characters plus ": " still
 * leave more than half of the 128 px row for the value. */
static const char *const TDCS_LABEL[TDCS_FIELDS] = {
    "信号机", "信号种类", "本补客货", "车次种类", "车次",
    "机车类型", "类型扩充", "机车号", "公里标", "速度",
    "总重", "计长", "辆数", "车站号", "车站扩充",
    "区段号", "实际交路", "司机号", "司机扩充", "状态"
};

/* Display order: the field index shown in each row of each page.  The order in
 * the frame is not the order an operator reads: train and locomotive first,
 * then the consist, then the crew and the signal.  0xFF leaves the row blank. */
static const uint8_t TDCS_PAGE_MAP[TDCS_PAGES][TDCS_ROWS] = {
    {  3,  4,  5,  6,  7,  8 },   /* 车次种类 车次 机车类型 类型扩充 机车号 公里标 */
    {  9, 10, 11, 12, 13, 14 },   /* 速度 总重 计长 辆数 车站号 车站扩充 */
    { 15, 16, 17, 18,  0,  1 },   /* 区段号 实际交路 司机号 司机扩充 信号机 信号种类 */
    {  2, 19, 0xFF, 0xFF, 0xFF, 0xFF }   /* 本补客货 状态 */
};

static void field_row(uint8_t row, const char *label, const char *value)
{
    char s[48];
    char *o = s;
    while (*label)
        *o++ = *label++;
    *o++ = ':';
    *o++ = ' ';
    while (*value)
        *o++ = *value++;
    *o = '\0';
    UI_PrintStringSmallCN(s, 0, row);
}

static void draw_field(uint8_t row, const tdcs_rec_t *r, uint8_t idx)
{
    const tdcs_msg_t *m = &r->m;
    char v[24];
    char *o;

    switch (idx) {
        case 0: {
            const char *n = sig_name(m->sig);
            if (!n) {
                o = put(v, "未知 ");
                o = putu(o, m->sig);
                *o = '\0';
                n = v;
            }
            field_row(row, TDCS_LABEL[idx], n);
            break;
        }
        case 1: {
            const char *n = sig_type_name(m->sig_type);
            if (!n) {
                o = put(v, "未知 ");
                o = putu(o, m->sig_type);
                *o = '\0';
                n = v;
            }
            field_row(row, TDCS_LABEL[idx], n);
            break;
        }
        case 2:
            o = put(v, (m->crew & 2u) ? "补机 " : "本务 ");
            o = put(o, (m->crew & 1u) ? "客车" : "货车");
            *o = '\0';
            field_row(row, TDCS_LABEL[idx], v);
            break;
        case 3: {
            bool any = false;
            o = v;
            for (uint8_t i = 0; i < 4u; i++) {
                const uint8_t c = (uint8_t)m->cls[i];
                if (c == ' ')
                    continue;
                *o++ = (c >= 0x20u && c < 0x7Fu) ? (char)c : '.';
                any = true;
            }
            *o = '\0';
            field_row(row, TDCS_LABEL[idx], any ? v : "--");
            break;
        }
        case 4:
            if (m->train_no > 9999999u) {
                field_row(row, TDCS_LABEL[idx], "-------");
            } else {
                o = putpad(v, m->train_no, 7u);
                *o = '\0';
                field_row(row, TDCS_LABEL[idx], v);
            }
            break;
        case 5: {
            /* The type byte uses the LBJ model code space: 240 is HXD1D.  Show
             * the name with the raw code in hex, and keep the old "240 (F0)"
             * for a code the table does not know.  A seven-character name drops
             * the hex - "HXD1D-J(F0)" would not fit the 128 px row. */
            const char *n = NULL;
#ifdef ENABLE_FEAT_F4HWN_LBJ
            n = LBJ_TypeName(m->loco_type);
#endif
            o = v;
            if (n) {
                o = put(o, n);
                if (o - v <= 6) {
                    *o++ = '(';
                    o = puthex2(o, (uint8_t)m->loco_type);
                    *o++ = ')';
                }
            } else {
                o = putu(o, m->loco_type);
                o = put(o, " (");
                o = puthex2(o, (uint8_t)m->loco_type);
                *o++ = ')';
            }
            *o = '\0';
            field_row(row, TDCS_LABEL[idx], v);
            break;
        }
        case 6:
            o = putu(v, m->loco_ext);
            *o = '\0';
            field_row(row, TDCS_LABEL[idx], v);
            break;
        case 7:
            o = putu(v, m->loco_no);
            *o = '\0';
            field_row(row, TDCS_LABEL[idx], v);
            break;
        case 8:
            if (!m->km_valid) {
                field_row(row, TDCS_LABEL[idx], "----");
            } else {
                o = v;
                if (m->km_neg)
                    *o++ = '-';
                *o++ = 'K';
                o = putu(o, m->km_abs / 1000u);
                *o++ = '+';
                o = putpad(o, m->km_abs % 1000u, 3u);
                *o++ = m->km_inc ? '^' : 'v';
                *o = '\0';
                field_row(row, TDCS_LABEL[idx], v);
            }
            break;
        case 9:
            o = putu(v, m->speed);
            o = put(o, "km/h");
            *o = '\0';
            field_row(row, TDCS_LABEL[idx], v);
            break;
        case 10:
            o = putu(v, m->weight);
            *o++ = 't';
            *o = '\0';
            field_row(row, TDCS_LABEL[idx], v);
            break;
        case 11:
            o = putu(v, m->length10 / 10u);
            *o++ = '.';
            o = putu(o, m->length10 % 10u);
            *o++ = 'm';
            *o = '\0';
            field_row(row, TDCS_LABEL[idx], v);
            break;
        case 12:
            o = putu(v, m->cars);
            *o = '\0';
            field_row(row, TDCS_LABEL[idx], v);
            break;
        case 13:
            o = putu(v, m->station);
            *o = '\0';
            field_row(row, TDCS_LABEL[idx], v);
            break;
        case 14:
            o = putu(v, m->station_ext);
            *o = '\0';
            field_row(row, TDCS_LABEL[idx], v);
            break;
        case 15:
            o = putu(v, m->section);
            *o = '\0';
            field_row(row, TDCS_LABEL[idx], v);
            break;
        case 16:
            o = putu(v, m->route);
            *o = '\0';
            field_row(row, TDCS_LABEL[idx], v);
            break;
        case 17:
            o = putu(v, m->driver);
            *o = '\0';
            field_row(row, TDCS_LABEL[idx], v);
            break;
        case 18:
            o = putu(v, m->driver_ext);
            *o = '\0';
            field_row(row, TDCS_LABEL[idx], v);
            break;
        case 19:
            o = put(v, m->crc_ok ? "OK " : "ERR ");
            if (m->test)
                o = put(o, "测试 ");
            if (r->rssi < 0) {
                *o++ = '-';
                o = putu(o, (uint32_t)(-(int32_t)r->rssi));
            } else {
                o = putu(o, (uint32_t)r->rssi);
            }
            o = put(o, "dB");
            *o = '\0';
            field_row(row, TDCS_LABEL[idx], v);
            break;
        default:
            break;
    }
}

void TDCS_DrawPopup(void)
{
    if (!g.count)
        return;

    const tdcs_rec_t *r = &g.rec[g.cur];
    char s[32];
    char *o;

    UI_DisplayClear();

    for (uint8_t i = 0; i < TDCS_ROWS; i++) {
        const uint8_t idx = TDCS_PAGE_MAP[g.page][i];
        if (idx >= TDCS_FIELDS)
            continue;
        draw_field(i, r, idx);
    }

    /* page number, top right of the first field row */
    o = s;
    *o++ = 'P';
    o = putu(o, (uint32_t)g.page + 1u);
    *o++ = '/';
    o = putu(o, TDCS_PAGES);
    *o = '\0';
    UI_PrintStringSmallNormal(s, TDCS_PAGE_X, 0, 0);

    /* footer: history index hard left, the report's age in the middle, EXIT
     * hard right - the LBJ window's arrangement */
    o = s;
    o = putu(o, (uint32_t)g.cur + 1u);
    *o++ = '/';
    o = putu(o, g.count);
    *o = '\0';
    UI_PrintStringSmallNormal(s, 0, 0, TDCS_FOOTER);

    if (r->m.test)
        UI_PrintStringSmallCN("测试", TDCS_TEST_X, TDCS_FOOTER);

    o = putage(s, (uint16_t)(g.sec - r->sec));
    *o = '\0';
    UI_PrintStringSmallNormal(s, TDCS_AGE_X, 0, TDCS_FOOTER);

    UI_PrintStringSmallBold("EXIT", TDCS_EXIT_X, 0, TDCS_FOOTER);

    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}

/* ---- keys -------------------------------------------------------------- */
bool TDCS_HandleKey(uint8_t key, bool pressed, bool held)
{
    if (key == KEY_PTT) {
        TDCS_ClosePopup();
        TDCS_StopSampler();
        return false;
    }
    if (!g.popup)
        return false;
    if (gScreenToDisplay != DISPLAY_MAIN)
        return false;

    /* The low-battery page is drawn under this window and its EXIT handler runs
     * after this hook.  Close the window and hand the key over, so one press
     * confirms the warning instead of being swallowed here. */
    if (gLowBattery && !gLowBatteryConfirmed) {
        TDCS_ClosePopup();
        return false;
    }

    if (!pressed || held)
        return true;

    g.popSec = g.sec;

    if (key == KEY_EXIT) {
        TDCS_ClosePopup();
        return true;
    }
    if (key == KEY_UP || key == KEY_DOWN) {
        if (g.count) {
            int t = (int)g.cur + ((key == KEY_UP) ? -1 : 1);
            if (t < 0) t = 0;
            if (t > (int)g.count - 1) t = (int)g.count - 1;
            if ((uint8_t)t != g.cur) {
                g.cur = (uint8_t)t;
                g.page = 0u;
                g.dirty = true;
            }
        }
        return true;
    }
    if (key == KEY_6) {
        if (g.page > 0u) {
            g.page--;
            g.dirty = true;
        }
        return true;
    }
    if (key == KEY_9) {
        if ((uint8_t)(g.page + 1u) < TDCS_PAGES) {
            g.page++;
            g.dirty = true;
        }
        return true;
    }
    if (key == KEY_2) {
        g.count = 0u;
        g.cur = 0u;
        g.page = 0u;
        TDCS_ClosePopup();
        return true;
    }
    return true;
}

/* ---- 10 ms tick -------------------------------------------------------- */
void TDCS_Tick(void)
{
    if (++g.subSec >= 100u) {
        g.subSec = 0u;
        g.sec++;
    }

    uint8_t b;
    for (uint8_t i = 0; i < TDCS_SLICERS; i++) {
        for (uint8_t k = 0; k < 64u && pop_bit(i, &b); k++)
            tdcs_framer_bit(&g.fr[i], b);
    }

    ledService();

    if (g.dismissed && !g_SquelchLost)
        g.dismissed = false;

    if (gCurrentFunction == FUNCTION_TRANSMIT || gPttIsPressed) {
        TDCS_ClosePopup();
        TDCS_StopSampler();
        return;
    }

    if (g.popup) {
        gBatterySaveCountdown_10ms = battery_save_count_10ms;
        if (gCurrentFunction == FUNCTION_POWER_SAVE)
            FUNCTION_Select(FUNCTION_FOREGROUND);
    }

    if (gCurrentFunction == FUNCTION_POWER_SAVE) {
        TDCS_StopSampler();
        return;
    }

    if (!tdcs_any_vfo()) {
        TDCS_ClosePopup();
        TDCS_StopSampler();
        if (!g_SquelchLost) g.dismissed = false;
        return;
    }

    /* Dual watch has the other VFO up: the window stays (the operator is
     * reading a report, not watching a channel), only the sampler stops. */
    if (!TDCS_RxVfoIsTdcs()) {
        TDCS_StopSampler();
        if (g.dirty) {
            g.dirty = false;
            g.popSec = g.sec;
            BACKLIGHT_TurnOn();
            gUpdateDisplay = true;
        }
        return;
    }

    if (g.on) {
        if (++g.ticks >= TDCS_MAX_TICKS) {
            TDCS_StopSampler();
            tdcs_flush();
        } else if (!g_SquelchLost) {
            if (++g.lostTicks >= TDCS_LOST_DEBOUNCE) {
                TDCS_StopSampler();
                tdcs_flush();
            }
        } else {
            g.lostTicks = 0u;
        }
    } else {
        if (g_SquelchLost)
            tdcs_start();
    }

    if (g.popup && !g.dismissed && !g.pinned) {
        static const uint8_t secs[] = { 0u, 3u, 5u, 10u, 60u };
        const uint8_t t = gEeprom.TCD_POPUP_TIME;
        if (t < 5u && secs[t] && (uint16_t)(g.sec - g.popSec) >= secs[t])
            TDCS_ClosePopup();
    }

    if (g.dirty) {
        g.dirty = false;
        g.popSec = g.sec;
        BACKLIGHT_TurnOn();
        gUpdateDisplay = true;
    }
}
