/* Shared PA4 audio sampler - see pa4_sampler.h.
 *
 * This is the sampler that used to live in lbj_dual.c, unchanged in behaviour:
 * a 19.2 kHz TIM3 update interrupt reads ADC channel 4 while the MCU DAC holds
 * PA4 at mid-scale.  The RAW BK4819 profile is selected so the discriminator
 * audio is not shaped by the voice de-emphasis/filters.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "py32f071_ll_bus.h"
#include "py32f071_ll_tim.h"
#include "py32f0xx.h"

#include "driver/bk4819.h"

#include "pa4_sampler.h"

#define FS             19200u
#define ADC_CH_PA4     4u
#define SMP8_POS       24u
#define SMP4_POS       12u
#define BIAS_CODE      2048u

#define ADC_SR         (*(volatile uint32_t *)0x40012400u)
#define ADC_CR2        (*(volatile uint32_t *)(0x40012400u + 0x08u))
#define ADC_SMPR3      (*(volatile uint32_t *)(0x40012400u + 0x14u))
#define ADC_SQR3       (*(volatile uint32_t *)(0x40012400u + 0x38u))
#define ADC_DR         (*(volatile uint32_t *)(0x40012400u + 0x50u))
#define GPIOA_MODER    (*(volatile uint32_t *)0x50000000u)
#define DAC_CR         (*(volatile uint32_t *)0x40007400u)
#define DAC_SWTRIGR    (*(volatile uint32_t *)(0x40007400u + 0x04u))
#define DAC_DHR12R1    (*(volatile uint32_t *)(0x40007400u + 0x08u))
#define RCC_APBENR1    (*(volatile uint32_t *)0x4002103Cu)
#define RCC_DACEN      (1u << 29)

#define DAC_CR_BIAS    ((1u << 0) | (1u << 1) | (1u << 2) | (7u << 3))
#define PA4_ADC_SR_EOC     (1u << 1)
#define PA4_ADC_CR2_START  ((1u << 22) | (1u << 20))

#define REG_2B         0x2Bu
#define REG_73         0x73u
#define REG_RAW2B_OR   0x0700u     /* RX HPF/LPF + de-emphasis + AFC off */
#define REG_RAW2B_AND  0x0007u
#define REG_RAW73_OR   0x0010u

static pa4_sample_fn g_fn;
static bool          g_active;
static bool          g_raw;

static uint32_t g_savedModer, g_savedDac, g_savedRcc, g_savedDhr;
static uint32_t g_savedSqr3, g_savedSmpr3;
static uint16_t g_saved2B, g_saved73;

static void adcSelPA4(uint32_t smpr3, uint32_t sqr3)
{
    ADC_SMPR3 = (smpr3 & ~(7u << SMP4_POS)) | (((smpr3 >> SMP8_POS) & 7u) << SMP4_POS);
    ADC_SQR3  = (sqr3 & ~0x1Fu) | ADC_CH_PA4;
}

static uint16_t adcRead(void)
{
    ADC_CR2 |= PA4_ADC_CR2_START;
    /* Bounded wait: if the ADC never signals EOC, drop this sample instead of
     * spinning (a long spin inside the 19.2 kHz ISR would starve the main loop). */
    for (uint16_t n = 0; !(ADC_SR & PA4_ADC_SR_EOC) && n < 300u; n++) { }
    return (uint16_t)(ADC_DR & 0x0FFFu);
}

static void biasOn(uint32_t moder, uint32_t rcc)
{
    GPIOA_MODER = moder | (3u << 8);
    RCC_APBENR1 = rcc | RCC_DACEN;
    DAC_CR      = DAC_CR_BIAS;
    DAC_DHR12R1 = BIAS_CODE;
    DAC_SWTRIGR = 1u;
}

static void biasOff(uint32_t dac, uint32_t dhr, uint32_t rcc, uint32_t moder)
{
    DAC_CR      = dac;
    DAC_DHR12R1 = dhr;
    RCC_APBENR1 = rcc;
    GPIOA_MODER = moder;
}

void TIM3_IRQHandler(void)
{
    if (LL_TIM_IsActiveFlag_UPDATE(TIM3)) {
        LL_TIM_ClearFlag_UPDATE(TIM3);
        if (g_fn)
            g_fn(adcRead());
    }
}

bool PA4_StartEx(pa4_sample_fn fn, uint32_t fs, bool raw)
{
    if (g_active || !fn || fs < 1000u || fs > 48000u)
        return false;

    g_savedModer = GPIOA_MODER;
    g_savedDac   = DAC_CR;
    g_savedRcc   = RCC_APBENR1;
    g_savedDhr   = DAC_DHR12R1;
    g_savedSqr3  = ADC_SQR3;
    g_savedSmpr3 = ADC_SMPR3;

    biasOn(g_savedModer, g_savedRcc);

    if (raw) {
        g_saved2B = BK4819_ReadRegister((BK4819_REGISTER_t)REG_2B);
        g_saved73 = BK4819_ReadRegister((BK4819_REGISTER_t)REG_73);
        BK4819_WriteRegister((BK4819_REGISTER_t)REG_2B,
                             (uint16_t)((g_saved2B | REG_RAW2B_OR) & ~REG_RAW2B_AND));
        BK4819_WriteRegister((BK4819_REGISTER_t)REG_73,
                             (uint16_t)(g_saved73 | REG_RAW73_OR));
    }
    BK4819_SetAF(BK4819_AF_FM);

    adcSelPA4(g_savedSmpr3, g_savedSqr3);

    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM3);
    LL_TIM_SetPrescaler(TIM3, 0);
    LL_TIM_SetAutoReload(TIM3, (SystemCoreClock / fs) - 1u);
    LL_TIM_SetCounter(TIM3, 0);
    LL_TIM_ClearFlag_UPDATE(TIM3);
    LL_TIM_EnableIT_UPDATE(TIM3);
    NVIC_SetPriority(TIM3_IRQn, 2);
    NVIC_EnableIRQ(TIM3_IRQn);
    LL_TIM_EnableCounter(TIM3);

    g_fn     = fn;
    g_raw    = raw;
    g_active = true;
    return true;
}

bool PA4_Start(pa4_sample_fn fn)
{
    return PA4_StartEx(fn, FS, true);
}

void PA4_Stop(void)
{
    if (!g_active)
        return;

    LL_TIM_DisableCounter(TIM3);
    LL_TIM_DisableIT_UPDATE(TIM3);
    NVIC_DisableIRQ(TIM3_IRQn);

    ADC_SQR3  = g_savedSqr3;
    ADC_SMPR3 = g_savedSmpr3;
    if (g_raw) {
        BK4819_WriteRegister((BK4819_REGISTER_t)REG_2B, g_saved2B);
        BK4819_WriteRegister((BK4819_REGISTER_t)REG_73, g_saved73);
    }
    biasOff(g_savedDac, g_savedDhr, g_savedRcc, g_savedModer);

    g_fn     = NULL;
    g_raw    = false;
    g_active = false;
}

bool PA4_Active(void)
{
    return g_active;
}

/* The battery conversion runs on channel 8 between two audio samples.  While
 * the sampler runs, a conversion is in flight most of the time and EOC is the
 * same bit as EOS on this part, so BOARD_ADC_GetBatteryInfo()'s "wait for EOS"
 * returns at once and reads a result that is not there yet.  One such reading
 * in the four-sample average drops the voltage under the critical threshold and
 * shows LOW BATTERY on a full pack, so the engine is parked completely: the
 * counter is stopped (no new START), the in-flight conversion is drained and
 * its flag dropped, and only then is channel 8 handed back. */
void PA4_AdcBegin(void)
{
    if (!g_active)
        return;

    LL_TIM_DisableCounter(TIM3);
    LL_TIM_DisableIT_UPDATE(TIM3);
    NVIC_DisableIRQ(TIM3_IRQn);
    NVIC_ClearPendingIRQ(TIM3_IRQn);

    for (uint16_t n = 0; n < 400u && !(ADC_SR & PA4_ADC_SR_EOC); n++) { }
    (void)ADC_DR;
    ADC_SR = (uint32_t)~PA4_ADC_SR_EOC;

    ADC_SQR3 = g_savedSqr3;
}

void PA4_AdcEnd(void)
{
    if (!g_active)
        return;

    ADC_SQR3 = (g_savedSqr3 & ~0x1Fu) | ADC_CH_PA4;
    ADC_SR   = (uint32_t)~PA4_ADC_SR_EOC;   /* drop the battery result flag */
    LL_TIM_ClearFlag_UPDATE(TIM3);
    LL_TIM_EnableIT_UPDATE(TIM3);
    NVIC_EnableIRQ(TIM3_IRQn);
    LL_TIM_EnableCounter(TIM3);
}
