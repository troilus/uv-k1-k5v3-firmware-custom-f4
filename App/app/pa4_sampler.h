/* Shared PA4 (ADC channel 4) audio sampler for the resident railway decoders.
 *
 * The LBJ dual-watch and TDCS modes both need the receiver's discriminator
 * audio, which reaches PA4 (the voice DAC pin, held at mid-scale by the MCU
 * DAC) and is sampled on ADC channel 4.  Only one of the two modes can be
 * selected at a time, so they share one TIM3 19.2 kHz interrupt instead of
 * defining two TIM3_IRQHandler() symbols.
 *
 * The caller registers a per-sample callback.  PA4_Start() saves the ADC, DAC,
 * GPIO and BK4819 registers it changes, selects the RAW receive profile (RX
 * HPF/LPF, de-emphasis and AFC off), starts the timer and returns true.  If a
 * sampler is already running it refuses: the mode that owns it must stop first.
 * PA4_Stop() restores everything it saved.
 */
#ifndef APP_PA4_SAMPLER_H
#define APP_PA4_SAMPLER_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*pa4_sample_fn)(int32_t x);

/* Start sampling PA4 at 19.2 kHz with the RAW audio profile (LBJ). */
bool PA4_Start(pa4_sample_fn fn);

/* Start sampling PA4 at fs with either the RAW profile (raw=true: no HPF/LPF,
 * de-emphasis or AFC) or the normal FM audio path (raw=false: de-emphasis and
 * the normal audio filters stay on, as APRS RX uses).  Returns false when a
 * sampler is already active. */
bool PA4_StartEx(pa4_sample_fn fn, uint32_t fs, bool raw);

/* Stop the sampler and restore the ADC/DAC/GPIO/BK4819 state it saved. */
void PA4_Stop(void);

bool PA4_Active(void);

/* ADC sharing with the battery reader (channel 8): suspend the sampler around
 * a battery conversion so neither corrupts the other. */
void PA4_AdcBegin(void);
void PA4_AdcEnd(void);

#endif /* APP_PA4_SAMPLER_H */
