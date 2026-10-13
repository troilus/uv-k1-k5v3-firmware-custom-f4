/* LBJ dual-watch (resident firmware feature) - see lbj_dual.h.
 *
 * A channel whose modulation is set to LBJ (menu, like FM/AM/RAW) is its own
 * receive "mode": the squelch behaves like FM (a normal carrier opens/closes
 * it, holds dual-watch, opens the audio path) while the LBJ/POCSAG decode runs
 * in the background - the BK4819 uses the RAW receive profile and the RX audio
 * is sampled on PA4 (ADC channel 4) at 19.2 kHz, exactly as the LBJ RX overlay
 * app does. Set the channel modulation to LBJ and tune it to 821.2375 MHz.
 *
 * The sampling runs in a 19.2 kHz timer interrupt (TIM3), so the main loop is
 * never blocked: idle dual-watch and the UI stay smooth. The sampler is enabled
 * only while the selected RX VFO is in LBJ mode and its squelch is open (a
 * report is on air); when the squelch closes it stops. The sampler never
 * touches the audio path, so the channel can still be monitored by ear (MONI).
 * Decoded reports pop up a small window that stays until EXIT or PTT.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "py32f071_ll_bus.h"
#include "py32f071_ll_tim.h"
#include "py32f0xx.h"

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

/* ---- sampler timing ---- */
#define LBJ_MAX_TICKS    6000u     /* watchdog: 60 s of continuous sampling    */
#define LBJ_LOST_DEBOUNCE  20u     /* squelch closed for 200 ms -> stop         */

/* ---- receiver ---- */
#define FS             19200u
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

static dem_t g_dm;

/* ---- state ---- */
static struct {
    lbj_rx_t rx;
    char     sess[16];            /* last 1233999/1234000 short block (15 ch) */
    bool     sessValid;
    bool     popup;
    bool     dirty;
    bool     dismissed;           /* popup dismissed until the carrier drops  */
    char     l0[20], l1[20], l2[20], l3[20], l4[20];

    volatile bool pending;        /* decoded report handed to the main loop   */
    uint32_t pendAddr;
    uint8_t  pendFunc;
    uint16_t pendLen;
    char     pendBcd[LBJ_CWMAX * 5u];

    bool     on;                 /* sampler running                           */
    uint16_t ticks, lostTicks;
    uint32_t savedModer, savedDac, savedRcc, savedDhr, savedSqr3, savedSmpr3;
    uint16_t saved2B, saved73;
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
static char *putdash(char *o, uint8_t n){ while (n--) *o++ = '-'; return o; }

static char *puttrain(char *o, const char *sb, uint8_t func, bool have){
    if (!have){ *o++ = '-'; *o++ = '-'; }
    else for (uint8_t i = 0; i < 6u; i++) if (sb[i] != ' ') *o++ = sb[i];
    *o++ = ' ';
    return put(o, (func == 1u) ? "DN" : ((func == 3u) ? "UP" : "??"));
}

/* One unified 5-row layout for every LBJ address:
 *   row0 train + direction, row1 SPD, row2 KM, row3 model code + loco number,
 *   row4 the decoded address code. Missing fields are dashes. */
static void format(uint32_t addr, uint8_t func, const char *b, uint16_t len){
    char *o;
    const bool merged     = (len >= 65u);
    const bool det_addr   = (addr >= 1234001u);
    const bool has_detail = merged || (det_addr && len >= 30u);

    const char *sb;
    bool have_short;
    if (merged)                       { sb = b;      have_short = true; }
    else if (!det_addr && len >= 15u) { sb = b;      have_short = true; }
    else                              { sb = g.sess; have_short = g.sessValid; }

    const char *d = merged ? (b + (uint16_t)(len - 50u)) : b;
    const uint8_t dlen = merged ? 50u : (uint8_t)(len < 50u ? len : 50u);

    /* row0: train + direction */
    o = g.l0; o = puttrain(o, sb, func, have_short); *o = '\0';

    /* row1: speed */
    o = g.l1; o = put(o, "SPD ");
    if (have_short) o = putu(o, digv(sb + 6u, 3u)); else o = putdash(o, 7u);
    *o = '\0';

    /* row2: position km */
    o = g.l2; o = put(o, "KM ");
    if (have_short) o = putkm(o, sb + 10u); else o = putdash(o, 7u);
    *o = '\0';

    /* row3: model code (3) + loco number (5) = the 8-digit registration */
    o = g.l3;
    if (has_detail){
        for (uint8_t i = 4u; i < 12u && i < dlen; i++) *o++ = d[i];
        if (o == g.l3) o = putdash(o, 7u);
    } else {
        o = putdash(o, 7u);
    }
    *o = '\0';

    /* row4: the decoded address code (1233999..1234002) */
    o = g.l4; o = putu(o, addr); *o = '\0';
}

/* ---- decoder callback (timer ISR context: only queue the raw report; all
 * formatting/popup work is done by the main loop so the ISR stays light.) ---- */
void lbj_emit_msg(const lbj_rx_t *r, const char *bcd, uint16_t len){
    if ((uint32_t)(r->addr - 1233980u) > 40u)
        return;                                   /* keep the LBJ family only */
    uint16_t n = (len > sizeof g.pendBcd) ? (uint16_t)sizeof g.pendBcd : len;
    g.pendAddr = r->addr;
    g.pendFunc = r->func;
    g.pendLen  = n;
    for (uint16_t i = 0; i < n; i++) g.pendBcd[i] = bcd[i];
    g.pending = true;
}

/* Replace any non-printable byte so the font routine never sees one. */
static void sanitize(char *s){
    for (; *s; s++){ uint8_t c = (uint8_t)*s; if (c < 0x20u || c > 0x7Eu) *s = '.'; }
}

/* Format the queued report and (unless dismissed) show the popup. Main loop. */
static void processPending(void){
    if (!g.pending)
        return;
    char bcd[sizeof g.pendBcd];
    uint32_t addr;
    uint8_t  func;
    uint16_t len;
    NVIC_DisableIRQ(TIM3_IRQn);
    addr = g.pendAddr; func = g.pendFunc; len = g.pendLen;
    for (uint16_t i = 0; i < len; i++) bcd[i] = g.pendBcd[i];
    g.pending = false;
    NVIC_EnableIRQ(TIM3_IRQn);

    if ((addr == 1233999u || addr == 1234000u) && len >= 15u){
        for (uint8_t i = 0; i < 15u; i++) g.sess[i] = bcd[i];
        g.sessValid = true;
    }
    format(addr, func, bcd, len);
    sanitize(g.l0); sanitize(g.l1); sanitize(g.l2); sanitize(g.l3); sanitize(g.l4);
    if (!g.dismissed){
        g.popup = true;
        g.dirty = true;
    }
}

/* ---- popup ---- */
bool LBJ_PopupActive(void){ return g.popup; }
void LBJ_ClosePopup(void){
    if (g.popup){ g.popup = false; gUpdateDisplay = true; }
    g.dismissed = true;              /* stay dismissed until the carrier drops */
}

bool LBJ_HandleKey(uint8_t key){
    if (key == KEY_PTT){                 /* PTT closes the popup and stops the sampler, still transmits */
        LBJ_ClosePopup();
        LBJ_StopSampler();
        return false;
    }
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

    GUI_DisplaySmallest("LBJ", 2, 0, true, true);
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

/* ---- frequency test ("LBJ mode" is selected by the channel modulation, but
 * the frequency helper is kept for reference / future auto-detection) ---- */
bool LBJ_IsLbjFreq(uint32_t f){
    return (f > LBJ_FREQ_HZ10) ? ((f - LBJ_FREQ_HZ10) <= LBJ_FREQ_TOL)
                              : ((LBJ_FREQ_HZ10 - f) <= LBJ_FREQ_TOL);
}
/* "LBJ mode": the selected RX VFO's modulation is LBJ. */
bool LBJ_RxVfoIsLbj(void){
    return gRxVfo && gRxVfo->Modulation == MODULATION_LBJ;
}
bool LBJ_Active(void){ return g.on; }

/* ---- PA4 ADC / DAC bias ---- */
static void adcSelPA4(uint32_t smpr3, uint32_t sqr3){
    ADC_SMPR3 = (smpr3 & ~(7u << SMP4_POS)) | (((smpr3 >> SMP8_POS) & 7u) << SMP4_POS);
    ADC_SQR3 = (sqr3 & ~0x1Fu) | ADC_CH_PA4;
}
static uint16_t adcRead(void){
    ADC_CR2 |= LBJ_ADC_CR2_START;
    /* Bounded wait: if the ADC never signals EOC, drop this sample instead of
     * spinning (a long spin inside the 19.2 kHz ISR would starve the main loop). */
    for (uint16_t n = 0; !(ADC_SR & LBJ_ADC_SR_EOC) && n < 300u; n++){}
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

/* ---- 19.2 kHz sampler (TIM3 update interrupt) ---- */
void TIM3_IRQHandler(void){
    if (LL_TIM_IsActiveFlag_UPDATE(TIM3)){
        LL_TIM_ClearFlag_UPDATE(TIM3);
        dem_sample(&g_dm, adcRead());
    }
}

static void samplerStart(void){
    g.savedModer = GPIOA_MODER; g.savedDac = DAC_CR;
    g.savedRcc = RCC_APBENR1;   g.savedDhr = DAC_DHR12R1;
    g.savedSqr3 = ADC_SQR3;     g.savedSmpr3 = ADC_SMPR3;
    g.saved2B = BK4819_ReadRegister((BK4819_REGISTER_t)REG_2B);
    g.saved73 = BK4819_ReadRegister((BK4819_REGISTER_t)REG_73);

    biasOn(g.savedModer, g.savedRcc);
    BK4819_WriteRegister((BK4819_REGISTER_t)REG_2B, (uint16_t)((g.saved2B | REG_RAW2B_OR) & ~REG_RAW2B_AND));
    BK4819_WriteRegister((BK4819_REGISTER_t)REG_73, (uint16_t)(g.saved73 | REG_RAW73_OR));
    BK4819_SetAF(BK4819_AF_FM);

    memset(&g.rx, 0, sizeof g.rx);
    memset(&g_dm, 0, sizeof g_dm);
    g_dm.dc = BIAS_CODE; g_dm.hi = 200; g_dm.lo = -200;

    adcSelPA4(g.savedSmpr3, g.savedSqr3);

    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM3);
    LL_TIM_SetPrescaler(TIM3, 0);
    LL_TIM_SetAutoReload(TIM3, (SystemCoreClock / FS) - 1u);
    LL_TIM_SetCounter(TIM3, 0);
    LL_TIM_ClearFlag_UPDATE(TIM3);
    LL_TIM_EnableIT_UPDATE(TIM3);
    NVIC_SetPriority(TIM3_IRQn, 2);
    NVIC_EnableIRQ(TIM3_IRQn);
    LL_TIM_EnableCounter(TIM3);

    g.ticks = 0; g.lostTicks = 0;
    g.on = true;
}

static void samplerStop(void){
    if (!g.on)
        return;
    LL_TIM_DisableCounter(TIM3);
    LL_TIM_DisableIT_UPDATE(TIM3);
    NVIC_DisableIRQ(TIM3_IRQn);
    g.on = false;

    ADC_SQR3   = g.savedSqr3;
    ADC_SMPR3  = g.savedSmpr3;
    BK4819_WriteRegister((BK4819_REGISTER_t)REG_2B, g.saved2B);
    BK4819_WriteRegister((BK4819_REGISTER_t)REG_73, g.saved73);
    biasOff(g.savedDac, g.savedDhr, g.savedRcc, g.savedModer);
}

void LBJ_StopSampler(void){ samplerStop(); }

/* ADC sharing with the battery reader (channel 8). */
void LBJ_AdcBegin(void){
    if (g.on){ NVIC_DisableIRQ(TIM3_IRQn); ADC_SQR3 = g.savedSqr3; }
}
void LBJ_AdcEnd(void){
    if (g.on){ ADC_SQR3 = (g.savedSqr3 & ~0x1Fu) | ADC_CH_PA4; NVIC_EnableIRQ(TIM3_IRQn); }
}

/* ---- 10 ms tick ---- */
void LBJ_Tick(void){
    /* Format a decoded report queued by the ISR (main-loop context). */
    processPending();

    /* Never sample while keyed up or in power save: the radio is reconfigured. */
    if (gCurrentFunction == FUNCTION_TRANSMIT || gPttIsPressed){
        LBJ_ClosePopup();
        samplerStop();
        return;
    }
    if (gCurrentFunction == FUNCTION_POWER_SAVE){
        samplerStop();
        return;
    }

    /* Another channel got a signal: dismiss the LBJ decode window. */
    if (!LBJ_RxVfoIsLbj() && g_SquelchLost)
        LBJ_ClosePopup();

    if (g.on){
        if (++g.ticks >= LBJ_MAX_TICKS){          /* watchdog only */
            samplerStop();
        } else if (!g_SquelchLost){               /* carrier gone -> stop */
            if (++g.lostTicks >= LBJ_LOST_DEBOUNCE)
                samplerStop();
        } else {
            g.lostTicks = 0;
        }
    } else {
        if (LBJ_RxVfoIsLbj() && g_SquelchLost)    /* squelch open = report on air */
            samplerStart();
    }

    /* Re-arm the popup once the LBJ carrier is gone. */
    if (g.dismissed && !g_SquelchLost)
        g.dismissed = false;

    if (g.dirty){                                 /* refresh the popup from the main loop */
        g.dirty = false;
        BACKLIGHT_TurnOn();
        gUpdateDisplay = true;
    }
}
