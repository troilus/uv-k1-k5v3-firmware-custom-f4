/* TDCS RX - a 450 MHz TDCS (TB/T 3325-2013 A-class) decoder as an overlay app.
 *
 * Modelled on the LBJ RX app: the receiver is pinned to 457.5500 MHz, the RX
 * audio on PA4 is sampled at 9.6 kHz from a SysTick-paced loop and pushed
 * through the same demodulator the resident TDCS mode uses, and a decoded
 * frame opens a four-page window.
 *
 * The Chinese labels go through the resident small-font printer, so they come
 * from the radio's 8x8 font (the external flash one, or the built-in one of
 * the LBJRadio CN build).  A radio with no font at all draws blank cells.
 *
 * Keys: UP/DOWN older/newer report, 6/9 page, 1 speaker, 4 backlight always
 * on, EXIT quit.  The green LED double-blinks on every report that passes the
 * CRC; train numbers that start with 9 are dropped.
 */
#include <stdint.h>
#include <stddef.h>

#include "app_api.h"
#include "tdcs_dec.h"
#include "tdcs_dec.c"          /* the FFSK/BCH/CRC core (same translation unit) */

/* ---- hardware (PY32F071, SysTick at 48 MHz) --------------------------- */
static inline volatile uint32_t *hw(uint32_t a)
{
    volatile uint32_t *p = (volatile uint32_t *)a;
    __asm__("" : "+l"(p));
    return p;
}

#define SYST_LOAD   (*(volatile uint32_t *)0xE000E014u)
#define SYST_VAL    (*(volatile uint32_t *)0xE000E018u)
#define ADC_SR      (hw(0x40012400u)[0x00/4])
#define ADC_CR2     (hw(0x40012400u)[0x08/4])
#define ADC_SMPR3   (hw(0x40012400u)[0x14/4])
#define ADC_SQR3    (hw(0x40012400u)[0x38/4])
#define ADC_DR      (hw(0x40012400u)[0x50/4])
#define GPIOA_MODER (*(volatile uint32_t *)0x50000000u)
#define DAC_CR      (hw(0x40007400u)[0x00/4])
#define DAC_SWTRIGR (hw(0x40007400u)[0x04/4])
#define DAC_DHR12R1 (hw(0x40007400u)[0x08/4])
#define RCC_APBENR1 (*(volatile uint32_t *)0x4002103Cu)

#define DAC_CR_BIAS    ((1u<<0)|(1u<<1)|(1u<<2)|(7u<<3))
#define RCC_DACEN      (1u << 29)
#define BIAS_CODE      2048u
#define ADC_CH_PA4     4u
#define SMP8_POS       24u
#define SMP4_POS       12u
#define ADC_SR_EOC     (1u << 1)
#define ADC_CR2_START  ((1u << 22) | (1u << 20))

/* BK4819 registers the frequency pin writes (see the LBJ app's LBJ_PIN_FREQ). */
#define REG_30   0x30u
#define REG_38   0x38u
#define REG_39   0x39u
#define REG_43   0x43u
#define REG43_WIDE 0x3628u

#define TDCS_FREQ_HZ10 45755000u   /* 457.5500 MHz, 10 Hz units */
#define CYC_PER_SAMPLE    5000u    /* 48 MHz / 9600 */
#define TICK_EVERY         960u    /* 100 ms */
#define HISTORY              4u
#define ROWS                 6u
#define PAGES                3u

/* ---- state ------------------------------------------------------------ */
typedef struct {
    tdcs_msg_t m;
    uint16_t   sec;
} rec_t;

static struct {
    const app_api_t *A;
    rec_t   rec[HISTORY];
    uint8_t count, cur, page, prevKey;
    uint8_t ledPhase, ledHold;
    uint16_t tick;
    bool    running, spk, blAlways;
    uint32_t savedSqr3, savedSmpr3;
} g;

void *memset(void *d, int c, size_t n)
{
    uint8_t *p = d;
    while (n) p[--n] = (uint8_t)c;
    return d;
}
void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *p = d; const uint8_t *q = s;
    while (n) { --n; p[n] = q[n]; }
    return d;
}

/* ---- PA4 bias / ADC --------------------------------------------------- */
static void biasOn(uint32_t moder, uint32_t rcc)
{
    GPIOA_MODER = moder | (3u << 8);
    RCC_APBENR1 = rcc | RCC_DACEN;
    DAC_CR = DAC_CR_BIAS;
    DAC_DHR12R1 = BIAS_CODE;
    DAC_SWTRIGR = 1u;
}
static void biasOff(uint32_t dac, uint32_t dhr, uint32_t rcc, uint32_t moder)
{
    DAC_CR = dac; DAC_DHR12R1 = dhr; RCC_APBENR1 = rcc; GPIOA_MODER = moder;
}
static void adcRestore(void)
{
    ADC_SQR3 = g.savedSqr3;
    ADC_SMPR3 = g.savedSmpr3;
}
static uint16_t adcRead(void)
{
    ADC_CR2 |= ADC_CR2_START;
    for (uint16_t n = 0; !(ADC_SR & ADC_SR_EOC) && n < 2000u; n++) { }
    return (uint16_t)(ADC_DR & 0x0FFFu);
}

/* Absolute cycle clock: ticks_ms * 48000 + position in the period. */
static uint32_t clkCyc(void)
{
    uint32_t t, v;
    do { t = g.A->ticks_ms(); v = SYST_VAL; } while (t != g.A->ticks_ms());
    return t * 48000u + (SYST_LOAD - v);
}

/* ---- text ------------------------------------------------------------- */
static char *put(char *o, const char *s)
{
    while (*s) *o++ = *s++;
    return o;
}
/* Decimal without a division: subtracting the powers of ten keeps the 32-bit
 * divide helper (266 B) out of the 4 KiB blob. */
static const uint32_t P10[10] = {
    1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
    10000u, 1000u, 100u, 10u, 1u
};
static char *putu(char *o, uint32_t v)
{
    uint8_t started = 0;
    for (uint8_t i = 0; i < 10u; i++) {
        uint8_t d = 0;
        while (v >= P10[i]) { v -= P10[i]; d++; }
        if (d || started || i == 9u) { *o++ = (char)('0' + d); started = 1u; }
    }
    return o;
}
static char *puthex2(char *o, uint8_t v)
{
    static const char H[] = "0123456789ABCDEF";
    *o++ = H[v >> 4];
    *o++ = H[v & 0x0Fu];
    return o;
}

/* metres -> "K123+456": the thousands by subtraction, no divide helper. */
static char *putkm(char *o, uint32_t m)
{
    uint32_t km = 0;
    while (m >= 1000u) { m -= 1000u; km++; }
    o = putu(o, km);
    *o++ = '+';
    uint8_t h = 0, t = 0;
    while (m >= 100u) { m -= 100u; h++; }
    while (m >= 10u)  { m -= 10u;  t++; }
    *o++ = (char)('0' + h);
    *o++ = (char)('0' + t);
    *o++ = (char)('0' + m);
    return o;
}

/* 0.1 m units -> "52.3" */
static char *putlen(char *o, uint32_t l)
{
    uint32_t m = 0;
    while (l >= 10u) { l -= 10u; m++; }
    o = putu(o, m);
    *o++ = '.';
    return putu(o, l);
}
static uint8_t slen(const char *s)
{
    uint8_t n = 0;
    while (s[n]) n++;
    return n;
}

/* One row: the label (Chinese, from the radio font), ": ", then the value.
 * The stock printer advances a GB2312 character by two byte slots, so the
 * label ends at strlen * 7 px - that is where the value goes. */
static void row_put(uint8_t row, const char *label, const char *value)
{
    const uint8_t w = (uint8_t)(slen(label) * 7u);
    g.A->print_normal(label, 0, 0, row);
    g.A->print_normal(": ", w, 0, row);
    g.A->print_normal(value, (uint8_t)(w + 14u), 0, row);
}

/* ---- fields ----------------------------------------------------------- */
/* 18 fields: the two "extension" bytes (loco type extension, station
 * extension) are 0 on almost every report and the 4 KiB overlay has no room
 * for them. */
static const char *const LBL[18] = {
    "信号机", "信号种类", "本补客货", "车次种类", "车次",
    "机车类型", "机车号", "公里标", "速度",
    "总重", "计长", "辆数", "车站号",
    "区段号", "实际交路", "司机号", "司机扩充", "状态"
};

static const char *const SIG[7] = {
    "始发车", "编组站", "出站", "进站", "通过", "预告", "容许"
};
static const char *sig_name(uint8_t v)
{
    return (v < 7u) ? SIG[v] : "未知";
}
static const char *sig_type_name(uint8_t v)
{
    return (v >= 2u && v < 7u) ? SIG[v] : "未知";
}
static const char *const CREW[4] = {
    "本务 货", "本务 客", "补机 货", "补机 客"
};

static void draw_field(uint8_t row, const rec_t *r, uint8_t idx)
{
    const tdcs_msg_t *m = &r->m;
    char v[16];
    char *o = v;
    const char *cn = 0;

    switch (idx) {
        case 0: cn = sig_name(m->sig); break;
        case 1: cn = sig_type_name(m->sig_type); break;
        case 2: cn = CREW[m->crew & 3u]; break;
        case 3:
            for (uint8_t i = 0; i < 4u; i++) {
                const uint8_t c = (uint8_t)m->cls[i];
                if (c == ' ') continue;
                *o++ = (c >= 0x20u && c < 0x7Fu) ? (char)c : '.';
            }
            break;
        case 4: {
            /* Seven digits, no leading-zero trim.  Subtracting the powers of
             * ten (P10[3..9]) keeps the divide helpers out of the blob. */
            uint32_t t = m->train_no;
            for (uint8_t i = 3u; i < 10u; i++) {
                uint8_t d = 0;
                while (t >= P10[i]) { t -= P10[i]; d++; }
                *o++ = (char)('0' + d);
            }
            break;
        }
        case 5:
            o = putu(o, m->loco_type);
            *o++ = ' '; *o++ = '(';
            o = puthex2(o, m->loco_type);
            *o++ = ')';
            break;
        case 6:  o = putu(o, m->loco_no); break;
        case 7:
            if (!m->km_valid) { o = put(o, "----"); break; }
            if (m->km_neg) *o++ = '-';
            *o++ = 'K';
            o = putkm(o, m->km_abs);
            *o++ = m->km_inc ? '^' : 'v';
            break;
        case 8:  o = putu(o, m->speed); o = put(o, "km/h"); break;
        case 9:  o = putu(o, m->weight); *o++ = 't'; break;
        case 10: o = putlen(o, m->length10); break;
        case 11: o = putu(o, m->cars); break;
        case 12: o = putu(o, m->station); break;
        case 13: o = putu(o, m->section); break;
        case 14: o = putu(o, m->route); break;
        case 15: o = putu(o, m->driver); break;
        case 16: o = putu(o, m->driver_ext); break;
        case 17: {
            const int16_t db = g.A->rssi_dbm();
            if (db < 0) { *o++ = '-'; o = putu(o, (uint32_t)(-db)); }
            else o = putu(o, (uint32_t)db);
            o = put(o, "dB");
            break;
        }
        default:
            return;
    }
    *o = 0;

    row_put(row, LBL[idx], cn ? cn : v);
}

static const uint8_t PAGE_MAP[PAGES][ROWS] = {
    {  3,  4,  5,  6,  7,  8 },
    {  9, 10, 11, 12, 13, 14 },
    { 15, 16,  0,  1,  2, 17 }
};

static void draw(void)
{
    const app_api_t *A = g.A;
    char s[16];
    char *o;

    memset(A->fb, 0, 7u * 128u);

    if (g.count) {
        const rec_t *r = &g.rec[g.cur];

        for (uint8_t i = 0; i < ROWS; i++) {
            const uint8_t idx = PAGE_MAP[g.page][i];
            if (idx < 18u) draw_field(i, r, idx);
        }

        /* page number, top right of the first field row */
        o = s; *o++ = 'P'; o = putu(o, (uint32_t)g.page + 1u);
        *o++ = '/'; o = putu(o, PAGES); *o = 0;
        A->print_normal(s, (uint8_t)(128u - (uint8_t)(o - s) * 7u), 0, 0);

        /* footer: history index, EXIT */
        o = s; o = putu(o, (uint32_t)g.cur + 1u);
        *o++ = '/'; o = putu(o, g.count); *o = 0;
        A->print_normal(s, 0, 0, 6);
    }

    A->print_normal("EXIT", 100u, 0, 6);
    A->blit_full();
}

/* ---- reception -------------------------------------------------------- */
static void on_msg(const tdcs_msg_t *m)
{
    if (g.count < HISTORY)
        g.count++;
    for (uint8_t i = (uint8_t)(g.count - 1u); i > 0u; i--)
        g.rec[i] = g.rec[i - 1u];
    g.rec[0].m = *m;
    g.cur = 0u;
    g.page = 0u;

    if (!g.ledPhase) {           /* a report's repeats merge into one blink */
        g.ledPhase = 16u;
        g.tick = 0u;
    }
    g.A->backlight_on();
}

/* The LBJ window's announcement: four quick double flashes (100 on / 100 off /
 * 100 on / 500 off), in 100 ms ticks.  The long gap is every fourth phase. */

/* ---- keys ------------------------------------------------------------- */
static void handleKeys(void)
{
    const app_api_t *A = g.A;
    const uint8_t key = A->get_key();
    if (key == APP_KEY_INVALID || key == g.prevKey) { g.prevKey = key; return; }
    g.prevKey = key;

    const int d = A->nav_dir(key);
    if (d) {
        int t = (int)g.cur + d;
        if (t < 0) t = 0;
        if (t >= (int)g.count) t = (int)g.count - 1;
        if (t < 0) t = 0;
        if ((uint8_t)t != g.cur) { g.cur = (uint8_t)t; g.page = 0u; draw(); }
        return;
    }

    switch (key) {
        case APP_KEY_EXIT: g.running = false; break;
        case APP_KEY_1:    g.spk = !g.spk; A->audio_path(g.spk); break;
        case APP_KEY_4:    g.blAlways = !g.blAlways; A->backlight_on(); break;
        case APP_KEY_6:
            if (g.page > 0u) { g.page--; draw(); }
            break;
        case APP_KEY_9:
            if ((uint8_t)(g.page + 1u) < PAGES) { g.page++; draw(); }
            break;
        default: break;
    }
}

/* ---- sampling loop ---------------------------------------------------- */
__attribute__((noinline))
static void listen(void)
{
    tdcs_dec_t dec;                 /* on the stack: .bss counts toward 4 KiB */
    tdcs_dec_init(&dec);

    uint32_t next = clkCyc();

    while (g.running) {
        while ((int32_t)(clkCyc() - next) < 0) { }
        next += CYC_PER_SAMPLE;
        tdcs_dec_sample(&dec, adcRead(), on_msg);

        if (++g.tick >= TICK_EVERY) {
            g.tick = 0;
            if (g.ledPhase) {
                if (g.ledHold) {
                    g.ledHold--;
                } else {
                    g.A->led((g.ledPhase & 1u) == 0u);
                    g.ledHold = ((g.ledPhase & 3u) == 1u) ? 4u : 0u;
                    g.ledPhase--;
                }
            }
            handleKeys();
            if (!g.running) break;
            if (g.blAlways) g.A->backlight_on();
            g.A->backlight_update();
        }
    }

    adcRestore();
}

/* ---- entry ------------------------------------------------------------ */
__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    g.A = api;
    g.prevKey = APP_KEY_INVALID;
    g.savedSqr3 = ADC_SQR3;
    g.savedSmpr3 = ADC_SMPR3;

    const uint32_t savedModer = GPIOA_MODER, savedDac = DAC_CR;
    const uint32_t savedRcc = RCC_APBENR1, savedDhr = DAC_DHR12R1;
    biasOn(savedModer, savedRcc);
    api->backlight_on();

    g.spk = false;
    g.blAlways = false;
    g.page = 0;
    g.count = g.cur = 0;

    /* Pin the receiver to 457.5500 MHz. The loader restores the user's VFO on
     * exit through RADIO_SetupRegisters, so nothing is saved here. */
    api->bk_write(REG_38, (uint16_t)(TDCS_FREQ_HZ10 & 0xFFFFu));
    api->bk_write(REG_39, (uint16_t)(TDCS_FREQ_HZ10 >> 16));
    api->bk_write(REG_43, REG43_WIDE);
    const uint16_t r30 = api->bk_read(REG_30);
    api->bk_write(REG_30, 0);
    api->bk_write(REG_30, r30);

    api->audio_path(g.spk);
    api->set_af(APP_AF_FM);
    api->delay_ms(50);

    draw();
    g.running = true;
    listen();

    biasOff(savedDac, savedDhr, savedRcc, savedModer);
    api->led(false);
    api->set_af(APP_AF_MUTE);
    api->audio_path(false);
}
