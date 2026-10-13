/* LBJ dual-watch (resident firmware feature) - see lbj_dual.h.
 *
 * Sampling/demodulation is a transcription of the LBJ RX overlay app
 * (App/apps/lbj/lbj_app.c): PA4 (ADC channel 4) biased by the MCU DAC at
 * mid-scale, sampled at 19.2 kHz from SysTick, through the integer demodulator
 * validated in App/apps/lbj/test/model_rx.py, then the POCSAG/LBJ decoder in
 * lbj_dec.c.
 *
 * Unlike the overlay app this runs as a bounded, blocking dwell from inside the
 * dual-watch toggle, so channel A (the other VFO) is only interrupted while the
 * LBJ channel is being polled. The decoded report is shown in a small popup that
 * persists until EXIT.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "lbj_dec.h"
#include "lbj_dual.h"

#include "audio.h"
#include "driver/backlight.h"
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "driver/keyboard.h"
#include "driver/st7565.h"
#include "functions.h"
#include "helper/battery.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/helper.h"
#include "ui/status.h"

/* ---- LBJ channel: 821.2375 MHz (frequency unit = 10 Hz; _1GHz_in_KHz) ---- */
#define LBJ_FREQ_HZ10   82123750u
#define LBJ_FREQ_TOL      500u     /* +- 5 kHz match window                     */

/* ---- dwell timing ---- */
#define LBJ_DWELL_MS      500u     /* max look per dual-watch LBJ slot          */
#define LBJ_HOLD_MS       500u     /* extra while a report is still coming in   */
#define LBJ_HOUSE_SAMPLES 960u     /* 50 ms between key/screen service          */

/* ---- receiver ---- */
#define FS             19200u
#define CYC_PER_SAMPLE (48000000u / FS)     /* 2500 cycles at 48 MHz */
#define ADC_CH_PA4     4u
#define SMP8_POS       24u
#define SMP4_POS       12u
#define BIAS_CODE      2048u

#define DC_SHIFT   7
#define PK_SHIFT   8
#define PLL_STEP   4096
#define PLL_SHIFT  3
#define INT_SHIFT  9
#define INT_MAX    82
#define BOX        16u
#define M_B0       725
#define M_B1       1451
#define M_A1     (-21674)
#define M_A2       8192

/* ---- PY32F071 registers (same direct access the overlay app uses) ---- */
static inline volatile uint32_t *hw(uint32_t a){
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
#define RCC_DACEN   (1u << 29)
#define DAC_CR_BIAS ((1u << 0) | (1u << 1) | (1u << 2) | (7u << 3))
#define LBJ_ADC_SR_EOC    (1u << 1)
#define LBJ_ADC_CR2_START ((1u << 22) | (1u << 20))

#define REG_2B  0x2Bu
#define REG_73  0x73u
#define REG_RAW2B_OR  0x0700u     /* RX HPF/LPF + de-emphasis + AFC off      */
#define REG_RAW2B_AND 0x0007u
#define REG_RAW73_OR  0x0010u

/* ---- decoder state ---- */
static struct {
    lbj_rx_t rx;
    char     sess[16];            /* last 1233999/1234000 short block (15 ch) */
    bool     sessValid;
    bool     popup;
    bool     dirty;
    char     l0[20], l1[20], l2[20], l3[20], l4[20];
    uint32_t tPrev, tCyc;
} g;

/* ---- formatting ---- */
static char *put(char *o, const char *s){ while (*s) *o++ = *s++; return o; }
static unsigned sub(uint32_t *v, uint32_t d){ unsigned q = 0; while (*v >= d){ *v -= d; q++; } return q; }
static const uint32_t P10U[] = { 1000000u, 100000u, 10000u, 1000u, 100u, 10u, 1u };
static char *putu(char *o, uint32_t v){
    bool lead = false;
    for (uint8_t i = 0; i < 7u; i++){
        unsigned c = sub(&v, P10U[i]);
        if (c || lead || i == 6u){ *o++ = (char)('0' + c); lead = true; }
    }
    return o;
}
static uint32_t digv(const char *p, uint8_t n){
    uint32_t v = 0;
    for (uint8_t i = 0; i < n; i++){ char c = p[i]; if (c >= '0' && c <= '9') v = v * 10u + (uint32_t)(c - '0'); }
    return v;
}
static char *putkm(char *o, const char *p){       /* 5 BCD chars -> DDDD.D */
    for (uint8_t i = 0; i < 4u; i++){ char c = p[i]; *o++ = (c >= '0' && c <= '9') ? c : '0'; }
    *o++ = '.';
    char c = p[4]; *o++ = (c >= '0' && c <= '9') ? c : '0';
    return o;
}
static const char HEX[] = "0123456789ABCDEF";
static char *puthex2(char *o, uint8_t v){ *o++ = HEX[(v >> 4) & 0xFu]; *o++ = HEX[v & 0xFu]; return o; }

/* "<train> <dir>" from a 6-char short block. */
static char *puttrain(char *o, const char *sb, uint8_t func, bool have){
    if (!have){ *o++ = '-'; *o++ = '-'; }
    else for (uint8_t i = 0; i < 6u; i++) if (sb[i] != ' ') *o++ = sb[i];
    *o++ = ' ';
    return put(o, (func == 1u) ? "DN" : ((func == 3u) ? "UP" : "??"));
}

/* Decode the LBJ family; only 1233999..1234002 are laid out. */
static bool is_lbj(uint32_t a){ return (uint32_t)(a - 1233999u) <= 3u; }
static bool hasdet(uint32_t a, uint8_t len){
    if (!is_lbj(a)) return false;
    if (a >= 1234001u) return len >= 30u;   /* standalone or merged */
    return len >= 65u;                      /* merged only */
}

/* Build the popup lines from a decoded report (no Chinese: the model is the
 * raw code and the route field is printed as its raw bytes in hex). */
static void format(uint32_t addr, uint8_t func, const char *b, uint16_t len){
    char *o;
    if (hasdet(addr, (uint8_t)len)){
        const bool merged = (len >= 65u);
        const uint8_t start = merged ? (uint8_t)(len - 50u) : 0u;
        const char *d = b + start;
        const uint8_t dlen = (uint8_t)(len - start);
        const char *sb = merged ? b : g.sess;
        const bool have = merged || g.sessValid;

        o = g.l0; o = puttrain(o, sb, func, have); *o = '\0';
        /* model code [4:7] + loco number [7:12], raw ASCII, no Chinese name */
        o = g.l1; for (uint8_t i = 4u; i < 12u && i < dlen; i++) *o++ = d[i]; *o = '\0';
        /* route field [14:30] shown as raw bytes */
        o = g.l2; if (dlen >= 30u){ for (uint8_t i = 14u; i < 30u; i++) o = puthex2(o, (uint8_t)d[i]); } *o = '\0';
        /* speed + km from the short prefix / last short report */
        o = g.l3; o = put(o, "SPD");
        if (have){ o = putu(o, digv(sb + 6u, 3u)); o = put(o, " KM"); o = putkm(o, sb + 10u); }
        else o = put(o, " --");
        *o = '\0';
        o = g.l4; o = putu(o, addr); *o = '\0';
    } else {
        o = g.l0; o = puttrain(o, b, func, len >= 6u); *o = '\0';
        o = g.l1; o = put(o, "SPD "); o = putu(o, digv(b + 6u, 3u)); o = put(o, " km/h"); *o = '\0';
        o = g.l2; o = put(o, "KM "); if (len >= 15u) o = putkm(o, b + 10u); else o = put(o, "--"); *o = '\0';
        o = g.l3; o = putu(o, addr); *o = '\0';
        g.l4[0] = '\0';
    }
}

/* ---- decoder callback ---- */
void lbj_emit_msg(const lbj_rx_t *r, const char *bcd, uint16_t len){
    if ((uint32_t)(r->addr - 1233980u) > 40u)
        return;                                   /* keep the LBJ family only */
    if ((r->addr == 1233999u || r->addr == 1234000u) && len >= 15u){
        for (uint8_t i = 0; i < 15u; i++) g.sess[i] = bcd[i];
        g.sessValid = true;
    }
    format(r->addr, r->func, bcd, len);
    g.popup = true;
    g.dirty = true;
    BACKLIGHT_TurnOn();
}

/* ---- popup ---- */
bool LBJ_PopupActive(void){ return g.popup; }
void LBJ_ClosePopup(void){ g.popup = false; gUpdateDisplay = true; }

bool LBJ_HandleKey(uint8_t key){
    if (!g.popup)
        return false;
    if (key == KEY_EXIT){
        LBJ_ClosePopup();
        return true;
    }
    return false;
}

void LBJ_DrawPopup(void){
    char f[16];
    UI_DisplayClear();
    UI_StatusClear();

    GUI_DisplaySmallest("LBJ RX", 2, 0, true, true);
    /* frequency on the status bar (10 Hz units -> MHz) */
    {
        uint32_t fr = LBJ_FREQ_HZ10;
        char *o = f;
        o = putu(o, sub(&fr, 100000u)); *o++ = '.';
        *o++ = (char)('0' + sub(&fr, 10000u));
        *o++ = (char)('0' + sub(&fr, 1000u));
        *o++ = (char)('0' + sub(&fr, 100u));
        *o++ = (char)('0' + sub(&fr, 10u));
        *o = '\0';
    }
    GUI_DisplaySmallest(f, 46, 0, true, true);

    UI_PrintStringSmallBold(g.l0, 0, 0, 0);
    UI_PrintStringSmallBold(g.l1, 0, 0, 1);
    UI_PrintStringSmallBold(g.l2, 0, 0, 2);
    UI_PrintStringSmallBold(g.l3, 0, 0, 3);
    UI_PrintStringSmallBold(g.l4, 0, 0, 4);
    GUI_DisplaySmallest("EXIT", 100, 48, false, true);

    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}

/* ---- frequency test ---- */
bool LBJ_IsLbjFreq(uint32_t f){
    return (f > LBJ_FREQ_HZ10) ? ((f - LBJ_FREQ_HZ10) <= LBJ_FREQ_TOL)
                              : ((LBJ_FREQ_HZ10 - f) <= LBJ_FREQ_TOL);
}

bool LBJ_RxVfoIsLbj(void){
    return gRxVfo && gRxVfo->pRX && LBJ_IsLbjFreq(gRxVfo->pRX->Frequency);
}

/* ---- demodulator (test/model_rx.py, class DemodInt) ---- */
typedef struct {
    int32_t dc;
    int32_t x1, x2, y1, y2;
    int32_t hi, lo;
    int32_t ph, iint;
    int32_t bsum;
    int16_t box[BOX];
    uint8_t last, bi;
} dem_t;

static void dem_sample(dem_t *m, int32_t x){
    m->dc += (x - m->dc) >> DC_SHIFT;
    int32_t y = x - m->dc;
    int32_t o = (M_B0*y + M_B1*m->x1 + M_B0*m->x2 - M_A1*m->y1 - M_A2*m->y2) >> 14;
    m->x2 = m->x1; m->x1 = y; m->y2 = m->y1; m->y1 = o;
    m->bsum += o - m->box[m->bi];
    m->box[m->bi] = (int16_t)o;
    m->bi = (uint8_t)((m->bi + 1u) & (BOX - 1u));
    y = m->bsum;
    m->hi -= (m->hi - y) >> PK_SHIFT;
    if (y > m->hi) m->hi = y;
    m->lo += (y - m->lo) >> PK_SHIFT;
    if (y < m->lo) m->lo = y;
    const int32_t mid = (m->hi + m->lo) >> 1;
    const int32_t h = (m->hi - m->lo) >> 6;
    uint8_t level = m->last;
    if (y > mid + h) level = 1u; else if (y < mid - h) level = 0u;
    if (level != m->last){
        int32_t err = m->ph;
        if (err > 32768) err -= 65536;
        m->iint += err >> INT_SHIFT;
        if (m->iint > INT_MAX) m->iint = INT_MAX; else if (m->iint < -INT_MAX) m->iint = -INT_MAX;
        m->ph -= (err >> PLL_SHIFT) + m->iint;
        if (m->ph < 0) m->ph += 65536; else if (m->ph >= 65536) m->ph -= 65536;
    }
    m->last = level;
    int32_t p0 = m->ph;
    m->ph += PLL_STEP;
    if (p0 < 32768 && m->ph >= 32768) lbj_rx_bit(&g.rx, level);
    if (m->ph >= 65536) m->ph -= 65536;
}

/* ---- PA4 ADC / DAC bias / SysTick timing ---- */
static void adcSelPA4(uint32_t smpr3, uint32_t sqr3){
    ADC_SMPR3 = (smpr3 & ~(7u << SMP4_POS)) | (((smpr3 >> SMP8_POS) & 7u) << SMP4_POS);
    ADC_SQR3 = (sqr3 & ~0x1Fu) | ADC_CH_PA4;
}
static uint16_t adcRead(void){
    ADC_CR2 |= LBJ_ADC_CR2_START;
    for (uint16_t n = 0; !(ADC_SR & LBJ_ADC_SR_EOC) && n < 2000u; n++){}
    return (uint16_t)(ADC_DR & 0x0FFFu);
}
static void biasOn(uint32_t moder, uint32_t rcc){
    GPIOA_MODER = moder | (3u << 8);
    RCC_APBENR1 = rcc | RCC_DACEN;
    DAC_CR = DAC_CR_BIAS;
    DAC_DHR12R1 = BIAS_CODE;
    DAC_SWTRIGR = 1u;
}
static void biasOff(uint32_t dac, uint32_t dhr, uint32_t rcc, uint32_t moder){
    DAC_CR = dac; DAC_DHR12R1 = dhr; RCC_APBENR1 = rcc; GPIOA_MODER = moder;
}
static void clkStart(void){ g.tPrev = SYST_VAL; g.tCyc = 0; }
static uint32_t clkCyc(void){
    uint32_t v = SYST_VAL;
    g.tCyc += (v <= g.tPrev) ? (g.tPrev - v) : (g.tPrev + (SYST_LOAD + 1u - v));
    g.tPrev = v;
    return g.tCyc;
}

/* ---- one bounded LBJ receive dwell (blocking) ---- */
void LBJ_Dwell(void){
    const uint32_t savedModer = GPIOA_MODER, savedDac = DAC_CR;
    const uint32_t savedRcc = RCC_APBENR1, savedDhr = DAC_DHR12R1;
    const uint32_t savedSqr3 = ADC_SQR3, savedSmpr3 = ADC_SMPR3;
    const uint16_t saved2B = BK4819_ReadRegister((BK4819_REGISTER_t)REG_2B);
    const uint16_t saved73 = BK4819_ReadRegister((BK4819_REGISTER_t)REG_73);

    /* Mute the LBJ dwell (channel A keeps its own audio path). */
    AUDIO_AudioPathOff();

    biasOn(savedModer, savedRcc);
    BK4819_WriteRegister((BK4819_REGISTER_t)REG_2B, (uint16_t)((saved2B | REG_RAW2B_OR) & ~REG_RAW2B_AND));
    BK4819_WriteRegister((BK4819_REGISTER_t)REG_73, (uint16_t)(saved73 | REG_RAW73_OR));
    BK4819_SetAF(BK4819_AF_FM);

    memset(&g.rx, 0, sizeof g.rx);

    dem_t dm;
    memset(&dm, 0, sizeof dm);
    dm.dc = BIAS_CODE; dm.hi = 200; dm.lo = -200;

    adcSelPA4(savedSmpr3, savedSqr3);
    clkStart();
    uint32_t next = 0;
    uint16_t cnt = 0;
    const uint32_t maxS  = (FS / 1000u) * LBJ_DWELL_MS;
    const uint32_t holdS = maxS + (FS / 1000u) * LBJ_HOLD_MS;

    for (uint32_t n = 0; (n < maxS) || (g.rx.inmsg && n < holdS); n++){
        while ((int32_t)(clkCyc() - next) < 0){}
        next += CYC_PER_SAMPLE;
        dem_sample(&dm, adcRead());

        if (++cnt >= LBJ_HOUSE_SAMPLES){
            cnt = 0;
            if (g.dirty){ g.dirty = false; LBJ_DrawPopup(); }
            KEY_Code_t k = KEYBOARD_GetKey();
            if (k != KEY_INVALID)
                LBJ_HandleKey((uint8_t)k);
            if (GPIO_IsPttPressed())
                break;                  /* let the main loop key up on channel A */
            BACKLIGHT_Update();
        }
    }

    /* Restore the analog front end for channel A. */
    ADC_SQR3   = savedSqr3;
    ADC_SMPR3  = savedSmpr3;
    BK4819_WriteRegister((BK4819_REGISTER_t)REG_2B, saved2B);
    BK4819_WriteRegister((BK4819_REGISTER_t)REG_73, saved73);
    biasOff(savedDac, savedDhr, savedRcc, savedModer);

    if (gEnableSpeaker)
        AUDIO_AudioPathOn();
}
