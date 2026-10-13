/* LBJ dual-watch (resident firmware feature).
 *
 * When one of the two dual-watch VFOs sits on the railway LBJ frequency
 * (821.2375 MHz), the dual-watch toggle hands that VFO's dwell to this module:
 * the RX audio is sampled on PA4 (ADC channel 4) at 19.2 kHz, demodulated and
 * decoded as POCSAG/LBJ (see lbj_dec.c). A decoded report pops up a small
 * decode window that stays until EXIT. Channel A (the other VFO) keeps its
 * normal voice reception and PTT; the LBJ dwell is muted.
 *
 * Everything is guarded by ENABLE_FEAT_F4HWN_LBJ so the stock firmwares are
 * unaffected.
 */
#ifndef APP_LBJ_DUAL_H
#define APP_LBJ_DUAL_H

#include <stdbool.h>
#include <stdint.h>

/* True when the RX frequency matches the LBJ channel (10 Hz units). */
bool LBJ_IsLbjFreq(uint32_t freq);

/* True when the currently selected RX VFO is the LBJ channel. */
bool LBJ_RxVfoIsLbj(void);

/* Run one bounded LBJ receive dwell (blocking). Called from the dual-watch
 * toggle when the LBJ VFO is selected. Servicing keys/backlight internally. */
void LBJ_Dwell(void);

/* Popup state. */
bool LBJ_PopupActive(void);
void LBJ_ClosePopup(void);
void LBJ_DrawPopup(void);

/* Key hook: returns true when the key was consumed by the popup (EXIT). */
bool LBJ_HandleKey(uint8_t key);

#endif /* APP_LBJ_DUAL_H */
