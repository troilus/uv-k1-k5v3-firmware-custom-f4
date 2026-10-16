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
 * Decoded reports pop up a small window that stays until EXIT or PTT. The
 * history it pages through holds one entry per train, not one per report: a
 * train's short block and its detail block arrive as separate pages and each
 * refreshes the entry the other opened, so a later report of the same train
 * updates the row instead of filing a second one (see history_merge()).
 *
 * Row0, at the right, also shows how far the train is from the receiver: the
 * train's own GPS fix comes from the detail block, the receiver's position from
 * the APRS TX profile that the APRS Edit overlay app maintains (see the block
 * above format()). Only a report with a detail block has a fix, so only those
 * pages show a distance.
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

#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS
#include "apps/app_overlay.h"
#include "apps/aprs_profile.h"
#endif
#include "audio.h"
#include "driver/backlight.h"
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "driver/keyboard.h"
#include "driver/py25q16.h"
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

#define DC_SHIFT   9          /* ~512 samples = 26.7 ms at 19.2 kHz: slower than
                               * a run of 13 equal bits, so the baseline never
                               * chases the droop. (#12) */
#define PK_SHIFT   8
#define PLL_STEP   4096
#define PLL_SHIFT  3          /* proportional DPLL only, as upstream: phase -=
                               * err >> 3. The integral term only cost code and
                               * RAM, the model is identical without it. (#12) */
#define BOX        16u

/* #12: three hysteresis slicers over ONE front-end (DC tracker, low-pass
 * biquad, boxcar matched filter, symmetric peak tracker), merged page by page.
 * k = 3 reads the level through a wide band (it flips late but never on noise),
 * k = 7 through a narrow one (it flips on the drooping edge of a high-passed
 * NRZ), k = 5 in between. The single k = 6 chain this replaces collapsed as
 * soon as the radio audio path cut high; the three read the marginal bits
 * differently and the merge keeps whichever read each character. Chosen with
 * the model in App/apps/lbj/test/model_rx.py (hpsweep). */
#define LBJ_SLICERS 3u
static const uint8_t LBJ_SL_K[LBJ_SLICERS] = { 3u, 5u, 7u };
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

/* ---- demodulator (test/model_rx.py, class DemodInt / class Slicer) ----
 * The front-end is shared; each hysteresis slicer keeps its own level and its
 * own DPLL, so the three of them hang off one pass over the samples. */
typedef struct {
    uint8_t  last;                 /* level held between the two thresholds */
    int32_t  ph;                   /* DPLL phase, 65536 per bit            */
} slice_t;

typedef struct {
    int32_t dc;
    int32_t x1, x2, y1, y2;
    int32_t hi, lo;
    int32_t bsum;
    int16_t box[BOX];
    uint8_t bi;
    slice_t sl[LBJ_SLICERS];
} dem_t;

static dem_t g_dm;

/* ---- state ---- */
#define LBJ_HISTORY   8u          /* decoded reports kept for paging back     */

typedef struct {
    uint32_t addr;                /* decoded address code (1233999..1234002) */
    uint16_t sec;                 /* receive time, seconds (age base)        */
    int16_t  dbm;                 /* signal strength at receive time         */
    int16_t  dst;                 /* distance to the receiver, tenths of km   */
    uint8_t  err, cor;            /* BCH verdict of the page that last filled it */
    uint8_t  sh;                  /* a short block has been seen for this entry */
    char     trn[7];              /* train digits   (short block), "" if unknown */
    char     loc[9];              /* 8-digit registration (detail), "" if unknown */
    char     rt[5];               /* route name initials, e.g. "JGX"         */
    char     l0[14], l1[18], l2[14], l3[20];
} lbj_rec_t;

/* g.dst / lbj_rec_t.dst values that are not a distance */
#define LBJ_DST_NODET  (-1)       /* report has no detail block: draw nothing */
#define LBJ_DST_UNK    (-2)       /* detail block, but no usable fix: "--km"  */
#define LBJ_DST_MAX  9999         /* 999.9 km; anything further reads ">999km" */

static struct {
    lbj_rx_t rx[LBJ_SLICERS];     /* one decoder per hysteresis slicer (#12)   */
    char     sess[16];            /* last 1233999/1234000 short block (15 ch) */
    bool     sessValid;
    bool     popup;
    bool     dirty;
    bool     dismissed;           /* popup dismissed until the carrier drops  */
    char     pref[3];             /* sticky 2-char train prefix from the last detail */
    char     rt[5];               /* route initials of the report being formatted */
    int16_t  dst;                 /* distance of the report being formatted   */
    char     trn[7];              /* train digits of the report being formatted */
    char     loc[9];              /* registration of the report being formatted */
    char     l0[14], l1[18], l2[14], l3[20];

    lbj_rec_t rec[LBJ_HISTORY];   /* history, newest first                    */
    uint8_t   count, cur;
    uint16_t  sec, subSec;        /* free running clock for the message age  */

    /* #12 page merge: the slicers read the same transmission and each gives the
     * same page, within a tick or two. It is held here until the last slicer
     * has had its say, then committed to the history. */
    uint16_t tick10;              /* 10 ms tick counter (the merge window)     */
    uint16_t pgTick;              /* the tick the page was last handed over   */
    uint32_t pgAddr;
    uint8_t  pgFunc, pgUsed, pgErr, pgCor, pgLen;
    char     pgBcd[LBJ_CWMAX * 5u];

    bool     on;                 /* sampler running                           */
    uint16_t ticks, lostTicks;
    uint32_t savedModer, savedDac, savedRcc, savedDhr, savedSqr3, savedSmpr3;
    uint16_t saved2B, saved73;
} g;

/* Ticks a page stays open for the other slicers before it is committed. The
 * main loop runs every 10 ms; a slicer that lost the word ending a page only
 * flushes it after the decoder's 64-bit hunt timeout (2 words, ~53 ms), so the
 * window has to outlast that. A slow batch is 427 ms, so two pages can never
 * overlap and can never be merged by mistake. */
#define MERGE_TICKS 8u

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

/* Route name = up to 4 GB2312 characters in the detail block's [14:30). GB2312
 * level-1 (0xB0A1..0xD7F9) is sorted by pinyin, so the first letter of every
 * character is a range lookup in this 23-entry table; no per-character table
 * needed. Pinyin has no initial for I/U/V, hence 23 letters and not 26. */
static const uint16_t INIT_BOUND[23] = {
    0, 36, 220, 453, 637, 659, 784, 939, 1120, 1415, 1515, 1763, 1914,
    1995, 2003, 2125, 2282, 2341, 2627, 2783, 2903, 3126, 3432 };
static char cn_initial(uint8_t hi, uint8_t lo){
    if (hi < 0xB0u || hi > 0xD7u)
        return '?';                                   /* level-2 or symbol */
    const uint16_t idx = (uint16_t)((uint16_t)(hi - 0xB0u) * 94u + (lo - 0xA1u));
    uint8_t k = 0;
    for (uint8_t i = 1u; i < 23u; i++){
        if (idx < INIT_BOUND[i]) break;
        k = i;
    }
    return "ABCDEFGHJKLMNOPQRSTWXYZ"[k];
}

/* True when the n characters are digits plus the blanks a transmitter leaves
 * in the unused leading positions of a fixed-width number (the speed's
 * hundreds, the km post's thousands). A blank means 0 there, so it must not
 * hide the value - but '?' is a lost word and must still read as a dash, never
 * a fake 0, and so must an all-blank field (no number was sent at all). */
static bool bcdpad(const char *p, uint8_t n){
    bool any = false;
    for (uint8_t i = 0; i < n; i++){
        const char c = p[i];
        if (c == ' ') continue;
        if (c < '0' || c > '9') return false;
        any = true;
    }
    return any;
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

/* ---- the receiver's own position -------------------------------------------
 *
 * The popup shows how far the decoded train is, which needs to know where the
 * receiver is. That position is not entered here: it is the beacon position the
 * APRS TX overlay app carries (apps/aprs_profile.h), which the standalone APRS
 * Edit app writes through the configuration proxy (app_config_proxy.h). One copy
 * means the readout always agrees with what is being transmitted, and neither a
 * menu nor a private flash block is needed. The price is that APRS TX has to be
 * installed with a valid position; without it a detail page shows "--km".
 *
 * The profile sits in the asset area of whichever slot APRS TX was installed
 * into (APP_SLOT_BASE(s)), so the slot is looked up once and then re-validated:
 * a reinstall into another slot is found again by itself (the Apps code bumps
 * APP_SlotRevision()), and the position bytes are re-read every time, so an edit
 * made in APRS Edit shows up without a relaunch. */
#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS

static uint8_t pos_slot = 0xFFu;   /* slot holding the APRS TX profile, 0xFF = none */
static uint8_t pos_rev  = 0xFFu;   /* APP_SlotRevision() when the miss was recorded */

/* Read the profile's hemisphere byte and 13 position digits out of one slot.
 * False when that slot does not hold the APRS TX profile. */
static bool pos_slot_read(uint8_t slot, uint8_t *dig, uint8_t *hemi){
    app_header_t h;
    uint32_t     magic;

    if (APP_ValidateSlot(slot, &h) != APP_OK)
        return false;
    if (h.asset_size < (uint16_t)(APRS_PROFILE_POS + APRS_POS_DIGITS))
        return false;

    const uint32_t base = APP_SLOT_BASE(slot) + APP_ASSET_OFFSET;
    PY25Q16_ReadBuffer(base, &magic, sizeof magic);
    if (magic != APRS_PROFILE_MAGIC)
        return false;
    PY25Q16_ReadBuffer(base + APRS_PROFILE_HEMI, hemi, 1u);
    PY25Q16_ReadBuffer(base + APRS_PROFILE_POS, dig, APRS_POS_DIGITS);
    return true;
}

/* The receiver's position in 1e-4 degree units, or false when there is none.
 * North/east only: the profile carries a hemisphere and this readout is for the
 * mainland railway, so a southern or western station is simply out of range. */
static bool pos_get(uint32_t *plat, uint32_t *plon){
    uint8_t dig[APRS_POS_DIGITS], hemi;

    if (pos_slot >= APP_SLOT_COUNT){
        /* Nothing found yet: one sweep over the slots, and none again until an
         * app is installed or erased (its revision changes). */
        const uint8_t rev = APP_SlotRevision();
        if (pos_rev == rev)
            return false;
        pos_rev = rev;
        for (uint8_t s = 0; s < APP_SLOT_COUNT; s++){
            if (pos_slot_read(s, dig, &hemi)){
                pos_slot = s;
                break;
            }
        }
        if (pos_slot >= APP_SLOT_COUNT)
            return false;
    } else if (!pos_slot_read(pos_slot, dig, &hemi)){
        pos_slot = 0xFFu;              /* erased or replaced: look again next time */
        return false;
    }

    if (hemi & 3u)                     /* not north/east */
        return false;
    for (uint8_t i = 0; i < APRS_POS_DIGITS; i++)
        if (dig[i] > 9u)               /* the field was never filled in */
            return false;

    /* DD MM.hh over DDD MM.hh: degrees, minutes and hundredths of a minute of
     * decimal digits. 1e-4 degrees = degrees * 10000 + (MMhh * 10000) / 6000. */
    const uint32_t lat = (uint32_t)(dig[0] * 10u + dig[1]) * 10000u +
                         ((uint32_t)((dig[2] * 10u + dig[3]) * 100u + dig[4] * 10u + dig[5])
                          * 10000u) / 6000u;
    const uint32_t lon = (uint32_t)(dig[6] * 100u + dig[7] * 10u + dig[8]) * 10000u +
                         ((uint32_t)((dig[9] * 10u + dig[10]) * 100u + dig[11] * 10u + dig[12])
                          * 10000u) / 6000u;
    if (lat > 900000u || lon > 1800000u)
        return false;

    *plat = lat;
    *plon = lon;
    return true;
}

/* The reporting station's own fix, out of the detail block: [30:39) is the
 * longitude as DDD MM MMMM and [39:47) the latitude as DD MM MMMM, plain
 * decimal digits - the same two fields the overlay's LBJ_COORDS reads (the
 * decimals here are minutes, not degrees). Returns 1e-4 degree units. */
static bool lbj_fix(const char *d, uint8_t dlen, uint32_t *plat, uint32_t *plon){
    if (dlen < 47u)
        return false;
    for (uint8_t i = 0; i < 17u; i++)
        if (d[i + 30u] < '0' || d[i + 30u] > '9')
            return false;
    if (d[30u] > '1' || d[33u] > '5' || d[41u] > '5')
        return false;                            /* < 200 deg, minutes < 60 */

    const uint32_t lon = (uint32_t)digv(d + 30u, 3u) * 10000u +
                         ((uint32_t)digv(d + 33u, 2u) * 10000u +
                          (uint32_t)digv(d + 35u, 4u)) / 60u;
    const uint32_t lat = (uint32_t)digv(d + 39u, 2u) * 10000u +
                         ((uint32_t)digv(d + 41u, 2u) * 10000u +
                          (uint32_t)digv(d + 43u, 4u)) / 60u;
    if (lat > 900000u || lon > 1800000u)
        return false;

    *plat = lat;
    *plon = lon;
    return true;
}

/* cos(latitude) * 32768 for 0..90 degrees in 5 degree steps (linear between). */
static const uint16_t COS5[19] = {
    32767u, 32642u, 32269u, 31650u, 30791u, 29697u, 28377u, 26841u, 25101u,
    23170u, 21062u, 18794u, 16384u, 13848u, 11207u,  8481u,  5690u,  2856u, 0u
};

/* Distance between two 1e-4 degree positions in tenths of a kilometre, or
 * LBJ_DST_UNK when they are too far apart to matter. Equirectangular: a 1e-4
 * degree of latitude is 0.011132 km everywhere, and a degree of longitude
 * shrinks with the cosine of the latitude. Both axes are rejected beyond 20
 * degrees, which keeps every product inside 32 bits - no 64-bit or floating
 * point maths, and so none of libgcc. */
static int16_t lbj_dist(uint32_t lat, uint32_t lon, uint32_t rlat, uint32_t rlon){
    const uint32_t dy = (lat > rlat) ? (lat - rlat) : (rlat - lat);
    const uint32_t dx = (lon > rlon) ? (lon - rlon) : (rlon - lon);
    if (dy > 200000u || dx > 200000u)
        return LBJ_DST_UNK;

    /* cosine of the mean latitude, interpolated on the 5 degree grid */
    const uint32_t mean = (lat + rlat) >> 1;         /* <= 900000: 90.0000 deg */
    const uint32_t dq   = (mean + 5000u) / 10000u;   /* whole degree, rounded  */
    const uint32_t k    = dq / 5u;
    const uint32_t f    = dq % 5u;
    uint32_t       c    = COS5[k];
    if (f)
        c = (uint32_t)((int32_t)c +
                       ((int32_t)COS5[k + 1u] - (int32_t)c) * (int32_t)f / 5);

    /* tenths of a kilometre: 1e-4 degree = 1.1132 tenths, so (v * 114) >> 10.
     * The cosine takes off 15 bits; (dx >> 2) first keeps the product inside
     * 32 bits and costs at most 3e-4 degree of longitude precision. */
    const uint32_t kx = ((((dx >> 2) * c) >> 13) * 114u) >> 10;
    const uint32_t ky = ((dy * 114u) >> 10);

    /* integer square root, bit by bit (the squares above stay below 2^31) */
    uint32_t v = kx * kx + ky * ky;
    uint32_t r = 0;
    uint32_t b = 1u << 30;
    while (b > v)
        b >>= 2;
    while (b){
        if (v >= r + b){ v -= r + b; r = (r >> 1) + b; }
        else            r >>= 1;
        b >>= 2;
    }
    return (int16_t)r;
}

#endif /* ENABLE_FEAT_F4HWN_OVERLAY_APPS */

/* One unified layout for every LBJ address; missing fields are dashes. The
 * address code and the signal strength are added by the popup itself. */
static void format(uint32_t addr, uint8_t func, const char *b, uint16_t len){
    char *o;
    const bool merged     = (len >= 65u);
    const bool det_addr   = (addr >= 1234001u);
    const bool has_detail = merged || (det_addr && len >= 30u);

    const char *sb;
    bool have_short, own_short;
    if (merged)                       { sb = b;      have_short = own_short = true;  }
    else if (!det_addr && len >= 15u) { sb = b;      have_short = own_short = true;  }
    else                              { sb = g.sess; have_short = g.sessValid; own_short = false; }

    const char *d = merged ? (b + (uint16_t)(len - 50u)) : b;
    const uint8_t dlen = merged ? 50u : (uint8_t)(len < 50u ? len : 50u);

    if (has_detail) decode_pref(d);   /* sticky train prefix, [0:4] */

    /* distance to the receiver position: only a report carrying the detail
     * block says where the train itself is */
#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS
    g.dst = LBJ_DST_NODET;
    if (has_detail){
        uint32_t flat, flon, rlat, rlon;
        g.dst = LBJ_DST_UNK;
        if (lbj_fix(d, dlen, &flat, &flon) && pos_get(&rlat, &rlon))
            g.dst = lbj_dist(flat, flon, rlat, rlon);
    }
#else
    g.dst = LBJ_DST_NODET;            /* no overlay app: nothing holds a position */
#endif

    /* the history merge keys: the 6 train digits the short block carries, and
     * the detail block's 8-digit model+number. Each stays empty (and so matches
     * no entry) when that field did not decode cleanly, and the train digits
     * are only this report's when it owns the short block, not when they were
     * borrowed from the last one heard. */
    g.trn[0] = '\0';
    if (own_short){
        uint8_t k = 0;
        for (uint8_t i = 0; i < 6u; i++){
            const char c = sb[i];
            if (c == ' ') continue;
            if (c < '0' || c > '9' || k >= 6u){ k = 0; break; }
            g.trn[k++] = c;
        }
        g.trn[k] = '\0';
    }
    g.loc[0] = '\0';
    if (has_detail && dlen >= 12u){
        uint8_t i = 0;
        for (; i < 8u; i++)
            if (d[4u + i] < '0' || d[4u + i] > '9') break;
        if (i == 8u){
            for (i = 0; i < 8u; i++) g.loc[i] = d[4u + i];
            g.loc[8] = '\0';
        }
    }

    /* row0: train + direction. The BCH verdict of the page is not part of the
     * line: a later page refreshes this entry, so the popup adds the verdict of
     * whichever page filled it last when it draws the row. */
    o = g.l0; o = puttrain(o, sb, func, have_short);
    *o = '\0';

    /* route name initials (the popup draws them as "LINE xxx") */
    o = g.rt;
    if (has_detail){
        for (uint8_t m = 0; m < 4u; m++){
            const uint8_t k = (uint8_t)(14u + 4u * m);
            if ((uint16_t)k + 3u >= (uint16_t)dlen) break;
            const uint8_t hi = (uint8_t)((nib(d[k]) << 4) | nib(d[k + 1u]));
            const uint8_t lo = (uint8_t)((nib(d[k + 2u]) << 4) | nib(d[k + 3u]));
            if (!hi || !lo) break;
            *o++ = cn_initial(hi, lo);
        }
    }
    *o = '\0';

    /* row1: speed. A lost or unsent speed is a dash, never a 0, while the
     * blanks a transmitter leaves in the unused hundreds are zeros (bcdpad). */
    o = g.l1; o = put(o, "SPD ");
    if (have_short && bcdpad(sb + 6u, 3u)) o = putu(o, digv(sb + 6u, 3u));
    else                                   *o++ = '-';
    o = put(o, " km/h");
    *o = '\0';

    /* row2: position km, same rule as the speed */
    o = g.l2; o = put(o, "KM ");
    if (have_short && bcdpad(sb + 10u, 5u)) o = putkm(o, sb + 10u);
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

/* ---- decoder callbacks (main-loop context, from lbj_process; the ISR only
 * queues raw words) ---------------------------------------------------------- */
static void sanitize(char *s);
static void history_merge(uint32_t addr, uint8_t func, uint16_t len, uint8_t err, uint8_t cor);

/* Commit the held page to the history: the merge window closed. */
static void page_commit(void)
{
    if (!g.pgUsed)
        return;
    g.pgUsed = false;

    /* An address alone, or a copy with more characters lost than read, is a
     * slicer that slipped a bit: its shifted codewords are valid ones as often
     * as not. Do not show it (upstream drops it the same way). */
    uint8_t lost = 0;
    for (uint8_t i = 0; i < g.pgLen; i++)
        if (g.pgBcd[i] == '?')
            lost++;
    if (2u * lost > g.pgLen)
        return;

    if ((g.pgAddr == 1233999u || g.pgAddr == 1234000u) && g.pgLen >= 15u){
        for (uint8_t i = 0; i < 15u; i++) g.sess[i] = g.pgBcd[i];
        g.sessValid = true;
    }
    format(g.pgAddr, g.pgFunc, g.pgBcd, g.pgLen);
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

    history_merge(g.pgAddr, g.pgFunc, g.pgLen, g.pgErr, g.pgCor);
    if (!g.dismissed){
        g.popup = true;
        g.dirty = true;
    }
}

/* A page finished on one slicer. The other slicers read the same transmission
 * and hand the same page over within a few ticks: hold it, let them have their
 * say, and merge - a character one slicer lost ('?') is taken from another that
 * read it, and the longer text extends the shorter (#12, as upstream). */
void lbj_emit_msg(const lbj_rx_t *r, const char *bcd, uint16_t len){
    if ((uint32_t)(r->addr - 1233980u) > 40u)
        return;                                   /* keep the LBJ family only */
    uint8_t n = (len > (uint16_t)sizeof g.pgBcd) ? (uint8_t)sizeof g.pgBcd
                                                 : (uint8_t)len;

    if (g.pgUsed && g.pgAddr == r->addr && g.pgFunc == r->func &&
        (uint16_t)(g.tick10 - g.pgTick) < MERGE_TICKS){
        for (uint8_t i = 0; i < n && i < g.pgLen; i++)
            if (g.pgBcd[i] == '?')
                g.pgBcd[i] = bcd[i];
        for (uint8_t i = g.pgLen; i < n; i++)
            g.pgBcd[i] = bcd[i];
        if (n > g.pgLen)
            g.pgLen = n;
        if (r->err) g.pgErr = 1u;
        if (r->cor) g.pgCor = 1u;
        g.pgTick = g.tick10;
        return;
    }

    page_commit();                                /* a different page: close it */
    g.pgUsed = true;
    g.pgAddr = r->addr;
    g.pgFunc = r->func;
    g.pgErr  = r->err;
    g.pgCor  = r->cor;
    g.pgLen  = n;
    for (uint8_t i = 0; i < n; i++) g.pgBcd[i] = bcd[i];
    g.pgTick = g.tick10;
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

/* ---- history -------------------------------------------------------------
 *
 * One entry per train, not one per report. A train sends its short block
 * (1233999/1234000: train number, speed, km post) and its detail block
 * (1234001/1234002: model+number, route, fix) as separate pages, so a page
 * refreshes the entry it belongs to instead of filing a second one, and a later
 * report of the same train carries the newer values.
 *
 * The two blocks do not share a field: the short one has the 6 train digits but
 * no registration, the detail one the 8-digit registration but only a
 * 2-character train prefix. So the entry is found by registration when the page
 * carries one, by train number when it does, and - for the first detail of a
 * train, which finds nothing to match - by taking the newest entry that is
 * still waiting for its detail and arrived right before it, which is the order
 * the two are transmitted in. */
#define LBJ_MERGE_SECS 5u         /* short block -> detail block arrival gap   */

static bool key_eq(const char *a, const char *b){
    if (!a[0] || !b[0])
        return false;             /* an unknown key matches nothing */
    while (*a && *a == *b){ a++; b++; }
    return *a == *b;
}

/* Refresh the entry this page belongs to, or start a new one. The short block
 * owns row0/row1/row2 and the train key, the detail block owns row3, the route,
 * the distance and the registration key, so an update only takes over the rows
 * its own half supplies - a detail never overwrites the speed of the entry it
 * completes with the last short block heard from somebody else. */
static void history_merge(uint32_t addr, uint8_t func, uint16_t len, uint8_t err, uint8_t cor){
    const bool merged   = (len >= 65u);
    const bool own_sh   = merged || (addr < 1234001u && len >= 15u);
    const bool det_at   = merged || (addr >= 1234001u && len >= 30u);

    uint8_t hit = LBJ_HISTORY;             /* no entry yet */
    if (det_at && g.loc[0]){
        for (uint8_t k = 0; k < g.count; k++)
            if (key_eq(g.rec[k].loc, g.loc)){ hit = k; break; }
    }
    if (hit == LBJ_HISTORY && own_sh && g.trn[0]){
        for (uint8_t k = 0; k < g.count; k++)
            if (key_eq(g.rec[k].trn, g.trn)){ hit = k; break; }
    }
    if (hit == LBJ_HISTORY && det_at && !merged){
        /* the first detail of a train: the two blocks share no field, so this
         * detail cannot be matched by anything it carries - take the newest
         * entry that is still waiting for its detail and arrived right before
         * it, which is the order the two are transmitted in. Waiting means a
         * short block was seen for it and no detail yet, not that the block
         * decoded: the train number is often lost and the pairing must not
         * depend on it. A merged page carries its own short block, so it never
         * borrows. */
        for (uint8_t k = 0; k < g.count; k++)
            if (g.rec[k].sh && !g.rec[k].loc[0] &&
                (int16_t)(g.sec - g.rec[k].sec) <= (int16_t)LBJ_MERGE_SECS){ hit = k; break; }
    }

    if (hit == LBJ_HISTORY){               /* a train not seen yet */
        if (g.count < LBJ_HISTORY)
            g.count++;
        for (uint8_t k = (uint8_t)(g.count - 1u); k > 0u; k--)
            g.rec[k] = g.rec[k - 1u];
        g.rec[0].trn[0] = g.rec[0].loc[0] = g.rec[0].rt[0] = '\0';
        g.rec[0].l0[0]  = g.rec[0].l1[0]  = g.rec[0].l2[0]  = g.rec[0].l3[0] = '\0';
        g.rec[0].dst    = LBJ_DST_NODET;
        g.rec[0].sh     = 0u;
        if (!own_sh && det_at){
            /* a detail that found no short block to complete: the train number,
             * speed and km post are simply not known, so say so instead of
             * leaving the three rows blank */
            char *o = g.rec[0].l0;
            o = puttrain(o, g.sess, func, false);
            *o = '\0';
            copyline(g.rec[0].l1, (uint8_t)sizeof g.rec[0].l1, "SPD - km/h");
            copyline(g.rec[0].l2, (uint8_t)sizeof g.rec[0].l2, "KM -");
        }
    } else if (hit != 0u){
        /* the refreshed entry goes back on top, so the list stays newest first */
        const lbj_rec_t t = g.rec[hit];
        for (uint8_t k = hit; k > 0u; k--)
            g.rec[k] = g.rec[k - 1u];
        g.rec[0] = t;
    }

    g.rec[0].addr = addr;
    g.rec[0].sec  = g.sec;
    g.rec[0].dbm  = lbj_dbm();
    g.rec[0].err  = err;
    g.rec[0].cor  = cor;
    if (own_sh){                     /* the short half, only when it is this report's */
        g.rec[0].sh = 1u;
        copyline(g.rec[0].trn, (uint8_t)sizeof g.rec[0].trn, g.trn);
        copyline(g.rec[0].l0, (uint8_t)sizeof g.rec[0].l0, g.l0);
        copyline(g.rec[0].l1, (uint8_t)sizeof g.rec[0].l1, g.l1);
        copyline(g.rec[0].l2, (uint8_t)sizeof g.rec[0].l2, g.l2);
    }
    if (det_at){
        copyline(g.rec[0].loc, (uint8_t)sizeof g.rec[0].loc, g.loc);
        copyline(g.rec[0].l3, (uint8_t)sizeof g.rec[0].l3, g.l3);
        copyline(g.rec[0].rt, (uint8_t)sizeof g.rec[0].rt, g.rt);
        g.rec[0].dst = g.dst;
    }
    g.cur = 0;                       /* a fresh report always shows the newest */
}

/* ---- popup ---- */
bool LBJ_PopupActive(void){ return g.popup; }
void LBJ_ClosePopup(void){
    if (g.popup){ g.popup = false; gUpdateDisplay = true; }
    g.dismissed = true;              /* stay dismissed until the carrier drops */
}

/* Manually open the decode window (the LBJ menu item): unlike a received report
 * nothing re-arms it, so it simply stays until EXIT or PTT, and the newest
 * report is shown. Refuses when the history is still empty. */
bool LBJ_OpenPopup(void){
    if (!g.count)
        return false;
    g.cur       = 0;
    g.popup     = true;
    g.dismissed = false;             /* an explicit open overrides an earlier EXIT */
    g.dirty     = true;
    return true;
}

/* The decode window owns the keypad while it is up: nothing else may reach
 * the radio. Only the fresh press of a key does anything - UP/DOWN step
 * through the history once and EXIT closes the window - while a hold, a
 * release and every other key are swallowed silently. PTT closes the window
 * and transmits. */
bool LBJ_HandleKey(uint8_t key, bool pressed, bool held){
    if (key == KEY_PTT){                 /* PTT closes the popup and stops the sampler, still transmits */
        LBJ_ClosePopup();
        LBJ_StopSampler();
        return false;
    }
    if (!g.popup)
        return false;
    if (!pressed || held)                 /* held or released: eat it, but do not step again */
        return true;
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

    /* row0: the train number, the BCH verdict of whichever page filled this
     * entry last (nothing when it arrived clean, '*' when a codeword was
     * repaired, '!' when it could not be repaired at all), and at the right the
     * distance from the receiver position set in APRS Edit. The number is cut
     * to 10 characters when a verdict follows - 11 otherwise - so the verdict
     * and the readout (up to 7) always fit the 128 px row. */
    {
        char l0[13];
        copyline(l0, (uint8_t)((r->err || r->cor) ? 11u : 12u), r->l0);
        uint8_t n = 0;
        while (l0[n]) n++;
        if (r->err)      l0[n++] = '!';
        else if (r->cor) l0[n++] = '*';
        l0[n] = '\0';
        UI_PrintStringSmallBold(l0, 0, 0, 0);
    }
    if (r->dst != LBJ_DST_NODET){
        char *o = s;
        if (r->dst == LBJ_DST_UNK)          o = put(o, "--km");
        else if (r->dst > LBJ_DST_MAX)      o = put(o, ">999km");
        else {
            o = putu(o, (uint32_t)r->dst / 10u);
            *o++ = '.';
            *o++ = (char)('0' + (unsigned)(r->dst % 10));
            o = put(o, "km");
        }
        *o = '\0';
        UI_PrintStringSmallBold(s, (uint8_t)(128u - (uint8_t)(o - s) * 7u), 0, 0);
    }
    UI_PrintStringSmallBold(r->l1, 0, 0, 1);
    UI_PrintStringSmallBold(r->l2, 0, 0, 2);
    UI_PrintStringSmallBold(r->l3, 0, 0, 3);

    /* route line: the ASCII initials of the line name, or a dash */
    {
        char *o = s;
        o = put(o, "LINE ");
        if (r->rt[0]) o = put(o, r->rt); else *o++ = '-';
        *o = '\0';
        UI_PrintStringSmallBold(s, 0, 0, 4);
    }

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
        UI_PrintStringSmallNormal(s, 0, 0, 5);
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

/* One hysteresis slicer over the shared front-end: its own band width
 * h = (hi-lo) >> k, its own level, its own DPLL. The level at the 0.5 crossing
 * is the bit, and a level transition pulls the phase toward the bit boundary
 * (proportional only, as upstream). */
static void dem_slice(dem_t *m, uint8_t i, int32_t y, int32_t mid, int32_t pp){
    slice_t *s = &m->sl[i];
    const int32_t h = pp >> LBJ_SL_K[i];
    uint8_t level = s->last;
    if (y > mid + h) level = 1u; else if (y < mid - h) level = 0u;
    if (level != s->last){
        int32_t err = s->ph;
        if (err > 32768) err -= 65536;
        s->ph -= err >> PLL_SHIFT;
        if (s->ph < 0) s->ph += 65536; else if (s->ph >= 65536) s->ph -= 65536;
    }
    s->last = level;
    int32_t p0 = s->ph;
    s->ph += PLL_STEP;
    if (p0 < 32768 && s->ph >= 32768) lbj_rx_bit(&g.rx[i], level);
    if (s->ph >= 65536) s->ph -= 65536;
}

/* One ADC sample through the shared front-end, then every slicer. */
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
    const int32_t pp  = m->hi - m->lo;
    for (uint8_t i = 0; i < LBJ_SLICERS; i++)
        dem_slice(m, i, y, mid, pp);
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
    g.tick10++;

    /* Run the BCH and the word state machine over what the 19.2 kHz ISR
     * queued - the correction is far longer than the 52 us sample period and
     * would make the sampler miss samples - then close the merge window of a
     * page no slicer has handed over for a while. */
    for (uint8_t i = 0; i < LBJ_SLICERS; i++)
        lbj_process(&g.rx[i]);
    if (g.pgUsed && (uint16_t)(g.tick10 - g.pgTick) >= MERGE_TICKS)
        page_commit();

    /* Never sample while keyed up or in power save: the radio is reconfigured. */
    if (gCurrentFunction == FUNCTION_TRANSMIT || gPttIsPressed){
        LBJ_ClosePopup();
        samplerStop();
        return;
    }

    /* While the decode window is up, hold the battery save off. Otherwise the
     * radio falls asleep 10 s after the last key, and then LBJ_Tick() returns
     * before it can consume g.dirty, so the window freezes for good: the keys
     * it swallows never reach the wake-up in the normal key path either. The
     * window may also open while already asleep (the report is committed above),
     * so wake up in that case too. */
    if (g.popup){
        gBatterySaveCountdown_10ms = battery_save_count_10ms;
        if (gCurrentFunction == FUNCTION_POWER_SAVE)
            FUNCTION_Select(FUNCTION_FOREGROUND);
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
