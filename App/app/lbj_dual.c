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
 *
 * The popup's row0 prefixes the train number with the 2 ASCII train-class chars
 * (K/T/D/C...) decoded from the detail block's [0:4] - each packed as 2 BCD
 * nibbles like a GB2312 route char. They are sticky in g.pref (as
 * LBJ_Console's lbjClass), so a short report with no detail block still shows
 * the last prefix seen. LBJ_Console reaches the same value via its lbjClass;
 * no train-type/regex table is used here.
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
#define LBJ_HISTORY   8u          /* decoded reports kept for paging back     */

typedef struct {
    uint32_t addr;                /* decoded address code (1233999..1234002) */
    uint16_t sec;                 /* receive time, seconds (age base)        */
    int16_t  dbm;                 /* signal strength at receive time         */
    char     l0[14], l1[18], l2[14], l3[20];
} lbj_rec_t;

static struct {
    lbj_rx_t rx;
    char     sess[16];            /* last 1233999/1234000 short block (15 ch) */
    bool     sessValid;
    bool     popup;
    bool     dirty;
    bool     dismissed;           /* popup dismissed until the carrier drops  */
    char     pref[3];             /* sticky 2-char train prefix from the last detail */
    char     l0[14], l1[18], l2[14], l3[20];

    lbj_rec_t rec[LBJ_HISTORY];   /* history, newest first                    */
    uint8_t   count, cur;
    uint16_t  sec, subSec;        /* free running clock for the message age  */

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

/* bcd char -> raw 4-bit value (inverse of the decoder's BCD alphabet). */
static uint8_t nib(char c){
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    switch (c){
        case '*': return 10u; case 'U': return 11u; case ' ': return 12u;
        case '-': return 13u; case ')': return 14u; case '(': return 15u;
        default:  return 0u;
    }
}

/* The detail block's [0:4] holds 2 ASCII train-prefix chars (e.g. "K "), each
 * as 2 BCD nibbles - exactly how the route field is packed. Decode them into
 * the sticky g.pref, printable bytes only, the usual blank pad dropped, so "K "
 * becomes "K". Sticky like LBJ_Console's lbjClass: a short report has no detail
 * block, yet still shows the last prefix seen. */
static void decode_pref(const char *d){
    for (uint8_t i = 0; i < 2u; i++){
        uint8_t v = (uint8_t)((nib(d[i * 2u]) << 4) | nib(d[i * 2u + 1u]));
        g.pref[i] = (v > 0x20u && v < 0x7Fu) ? (char)v : 0;  /* drop NUL/space */
    }
    g.pref[2] = '\0';
}

static char *puttrain(char *o, const char *sb, uint8_t func, bool have){
    if (g.pref[0]) *o++ = g.pref[0];        /* sticky train prefix, K/T/D/C... */
    if (g.pref[1]) *o++ = g.pref[1];
    if (!have){ *o++ = '-'; *o++ = '-'; }
    else for (uint8_t i = 0; i < 6u; i++) if (sb[i] != ' ') *o++ = sb[i];
    *o++ = ' ';
    return put(o, (func == 1u) ? "DN" : ((func == 3u) ? "UP" : "??"));
}

/* True when all n BCD characters are real digits. Used so a field that was
 * not decoded shows a dash instead of a fake 0. */
static bool bcdnum(const char *p, uint8_t n){
    for (uint8_t i = 0; i < n; i++) if (p[i] < '0' || p[i] > '9') return false;
    return true;
}

/* Locomotive model code -> ASCII name. Hard coded so there is no pointer
 * table in .rodata (the generated table was the one thing the detail path
 * touched that could take the radio down). */
static const char *type_name(uint16_t c){
    switch (c){
        case    1: return "JF";
        case    3: return "QJ";
        case    5: return "JS";
        case    6: return "KD7";
        case   55: return "LJK";
        case   81: return "DF21";
        case  101: return "DF";
        case  102: return "DF2";
        case  103: return "DF3";
        case  104: return "DF4";
        case  105: return "DF4K";
        case  106: return "DF4C";
        case  107: return "DF5";
        case  108: return "DF5W";
        case  109: return "DF6";
        case  110: return "DF7";
        case  111: return "DF8";
        case  112: return "DF9";
        case  113: return "DF10";
        case  114: return "DFH1";
        case  115: return "DFH2";
        case  116: return "DFH3";
        case  117: return "DFH5";
        case  118: return "BJ";
        case  119: return "BJW";
        case  120: return "ND2";
        case  121: return "ND3";
        case  122: return "ND4";
        case  123: return "ND5";
        case  124: return "NY5";
        case  125: return "NY6";
        case  126: return "NY7";
        case  127: return "QY";
        case  128: return "DFH21";
        case  129: return "DF7B";
        case  130: return "DF5S";
        case  131: return "DF7C";
        case  132: return "DF7S";
        case  133: return "GK1";
        case  134: return "GK1F";
        case  135: return "DF4E";
        case  136: return "DF7D";
        case  137: return "GK1A";
        case  138: return "DF11";
        case  139: return "TA";
        case  140: return "DF10F";
        case  141: return "DF4D";
        case  142: return "DF8B";
        case  143: return "DF12";
        case  144: return "DF7E";
        case  145: return "NYJ1";
        case  146: return "NZJ1";
        case  147: return "NZJ2";
        case  148: return "DF4DJ";
        case  149: return "XSG";
        case  150: return "SZ";
        case  151: return "NJ2";
        case  152: return "DF7G";
        case  153: return "NDJ3";
        case  156: return "DF11Z";
        case  157: return "FXN3D";
        case  158: return "DF11G";
        case  160: return "HXN3";
        case  161: return "HXN5";
        case  162: return "HXN3B";
        case  163: return "HXN5B";
        case  167: return "FXN3B";
        case  169: return "FXN3C";
        case  170: return "FXN5C";
        case  171: return "FXN3-J";
        case  201: return "8G";
        case  202: return "8K";
        case  203: return "6G";
        case  204: return "6K";
        case  205: return "SS1";
        case  206: return "SS3";
        case  207: return "SS4";
        case  208: return "SS5";
        case  209: return "SS6";
        case  210: return "SS3B";
        case  211: return "SS7";
        case  212: return "SS8";
        case  213: return "SS7B";
        case  214: return "SS7C";
        case  215: return "SS6B";
        case  216: return "SS9";
        case  217: return "SS7D";
        case  218: return "DJXM";
        case  219: return "DJ1";
        case  220: return "DJ2";
        case  221: return "DJF";
        case  222: return "LJD";
        case  223: return "XFH";
        case  224: return "SS7E";
        case  225: return "SS4G";
        case  226: return "SS3C";
        case  228: return "TS";
        case  229: return "DJ4HX";
        case  230: return "KTT";
        case  231: return "HXD1";
        case  232: return "HXD2";
        case  233: return "HXD3";
        case  234: return "HXD1B";
        case  235: return "HXD2B";
        case  236: return "HXD3B";
        case  237: return "HXD1C";
        case  238: return "HXD2C";
        case  239: return "HXD3C";
        case  240: return "HXD1D";
        case  241: return "HXD2D";
        case  242: return "HXD3D";
        case  243: return "FXD1B";
        case  244: return "FXD2B";
        case  245: return "FXD1";
        case  246: return "FXD3";
        case  247: return "FXD1-J";
        case  248: return "FXD3-J";
        case  249: return "KZ25TA";
        case  251: return "KZ25TB";
        case  252: return "HXD1D-J";
        case  254: return "FXD1H";
        case  300: return "XYSZ";
        case  301: return "CRH1";
        case  302: return "CRH2";
        case  303: return "CRH3";
        case  305: return "CRH5";
        case  306: return "CRH380A";
        case  307: return "CRH380B";
        case  308: return "CRH380C";
        case  309: return "CRH380D";
        case  310: return "CRH6A";
        case  311: return "CR400AF";
        case  312: return "CR400BF";
        case  313: return "CR300AF";
        case  314: return "CR300BF";
        case  315: return "CRH2E";
        case  316: return "CRH6F";
        case  330: return "CJ1";
        case  331: return "CJ2";
        case  332: return "CJ3";
        case  333: return "CJ4";
        case  334: return "CJ5";
        case  335: return "CJ6";
        case  400: return "GCD-1000J";
        case  403: return "GX-160";
        case 3073: return "CRH380BL";
        case 3075: return "CRH380BG";
        case 3111: return "CR400AF-Z";
        case 3112: return "CR400AF-S";
        case 3125: return "CR400BF-G";
        default:   return 0;
    }
}

/* "<age>" - 12s below a minute, 3m20s up to 99 min, 99m+ after that. */
static char *putage(char *o, uint16_t sec){
    if (sec < 60u){ o = putu(o, sec); *o++ = 's'; return o; }
    uint32_t v = sec;
    const unsigned m = sub(&v, 60u);                    /* v = leftover seconds */
    if (m > 99u){ return put(o, "99m+"); }
    o = putu(o, m); *o++ = 'm';
    if (v < 10u) *o++ = '0';
    o = putu(o, v); *o++ = 's';
    return o;
}

/* One unified layout for every LBJ address; missing fields are dashes. The
 * address code and the signal strength are added by the popup itself. */
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

    if (has_detail) decode_pref(d);   /* sticky train prefix, [0:4] */

    /* row0: train + direction */
    o = g.l0; o = puttrain(o, sb, func, have_short); *o = '\0';

    /* row1: speed. A speed that was not decoded is a dash, never a 0. */
    o = g.l1; o = put(o, "SPD ");
    if (have_short && bcdnum(sb + 6u, 3u)) o = putu(o, digv(sb + 6u, 3u));
    else                                   *o++ = '-';
    o = put(o, " km/h");
    *o = '\0';

    /* row2: position km, same rule as the speed */
    o = g.l2; o = put(o, "KM ");
    if (have_short && bcdnum(sb + 10u, 5u)) o = putkm(o, sb + 10u);
    else                                    *o++ = '-';
    *o = '\0';

    /* row3: model name + the 8-digit code/registration */
    o = g.l3;
    if (has_detail){
        const char *nm = type_name((uint16_t)digv(d + 4u, 3u));
        if (nm) o = put(o, nm); else o = put(o, "--");
        *o++ = ' ';
        for (uint8_t i = 4u; i < 12u && i < dlen; i++) *o++ = d[i];
    } else {
        o = putdash(o, 7u);
    }
    *o = '\0';
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

static void copyline(char *dst, uint8_t cap, const char *src){
    uint8_t i = 0;
    while (src[i] && i + 1u < cap){ dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* Signal strength in dBm, with the same band correction the S-meter uses. */
static int16_t lbj_dbm(void){
    return (int16_t)(BK4819_GetRSSI_dBm() + dBmCorrTable[gRxVfo->Band]);
}

/* Store the freshly formatted report (newest first) together with the time
 * and the signal strength it was received at, then make it the current one. */
static void history_push(uint32_t addr){
    if (g.count < LBJ_HISTORY)
        g.count++;
    for (uint8_t k = (uint8_t)(g.count - 1u); k > 0u; k--)
        g.rec[k] = g.rec[k - 1u];

    g.rec[0].addr = addr;
    g.rec[0].sec  = g.sec;
    g.rec[0].dbm  = lbj_dbm();
    copyline(g.rec[0].l0, (uint8_t)sizeof g.rec[0].l0, g.l0);
    copyline(g.rec[0].l1, (uint8_t)sizeof g.rec[0].l1, g.l1);
    copyline(g.rec[0].l2, (uint8_t)sizeof g.rec[0].l2, g.l2);
    copyline(g.rec[0].l3, (uint8_t)sizeof g.rec[0].l3, g.l3);
    g.cur = 0;                       /* a new report always shows the newest */
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
    sanitize(g.l0); sanitize(g.l1); sanitize(g.l2); sanitize(g.l3);

#ifdef ENABLE_LBJ_HIDE9
    /* Optional build: a train number starting with 9 is never displayed.
     * g.l0 is "<prefix><digits> <dir>" exactly as puttrain() wrote it, so its
     * first character is the first character of the shown train number. The
     * session tracking above has already run: reception and decoding are
     * untouched, only the report is dropped. */
    if (g.l0[0] == '9')
        return;
#endif

    history_push(addr);
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

/* The decode window owns the keypad while it is up: nothing else may reach
 * the radio. The two paging keys step through the message history, EXIT
 * closes the window and PTT closes it and transmits. */
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
    if (key == KEY_UP || key == KEY_DOWN){
        if (g.count){
            int t = (int)g.cur + ((key == KEY_UP) ? -1 : 1);
            if (t < 0) t = 0;
            if (t > (int)g.count - 1) t = (int)g.count - 1;
            if (t != (int)g.cur){
                g.cur = (uint8_t)t;
                g.dirty = true;            /* redraw with the other report */
            }
        }
        return true;
    }
    return true;                          /* swallow every other key */
}

/* The decode report owns the whole screen while it is up: the main area is
 * cleared and redrawn. The status bar is left exactly as UI_DisplayStatus()
 * drew it - no caption, no frequency. */
#define POP_FOOTER (FRAME_LINES - 1)    /* the very bottom row of the screen  */
#define POP_PAGEX   0u                  /* page number, hard left             */
#define POP_TIMEX  52u                  /* age, middle                        */
#define POP_EXITX 100u                  /* EXIT, hard right (100..127)        */

void LBJ_DrawPopup(void){
    const lbj_rec_t *r = &g.rec[g.cur];
    char s[22];

    UI_DisplayClear();

    UI_PrintStringSmallBold(r->l0, 0, 0, 0);
    UI_PrintStringSmallBold(r->l1, 0, 0, 1);
    UI_PrintStringSmallBold(r->l2, 0, 0, 2);
    UI_PrintStringSmallBold(r->l3, 0, 0, 3);

    /* the decoded address code and the strength it was received at */
    {
        char *o = s;
        int16_t v = r->dbm;
        o = putu(o, r->addr);
        *o++ = ' ';
        if (v < 0){ *o++ = '-'; v = (int16_t)(-v); }
        o = putu(o, (uint32_t)v);
        o = put(o, "dB");
        *o = '\0';
        UI_PrintStringSmallNormal(s, 0, 0, 4);
    }

    /* footer on the bottom row: page and age in normal size, EXIT bold and
     * hard right */
    {
        char *o = s;
        o = putu(o, (uint32_t)g.cur + 1u);
        *o++ = '/';
        o = putu(o, g.count);
        *o = '\0';
        UI_PrintStringSmallNormal(s, POP_PAGEX, 0, POP_FOOTER);
    }
    {
        char *o = putage(s, (uint16_t)(g.sec - r->sec));
        *o = '\0';
        UI_PrintStringSmallNormal(s, POP_TIMEX, 0, POP_FOOTER);
    }
    UI_PrintStringSmallBold("EXIT", POP_EXITX, 0, POP_FOOTER);

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
    /* Free running clock used for the message age (10 ms resolution). */
    if (++g.subSec >= 100u){ g.subSec = 0; g.sec++; }

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

    /* The window stays up until EXIT: a signal on the other channel no longer
       dismisses it, and the audio of the received channel keeps playing. */

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
