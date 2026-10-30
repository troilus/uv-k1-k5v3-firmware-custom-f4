/* TDCS receive mode (resident firmware feature).
 *
 * A channel whose modulation is set to TDCS is a receive-only mode for the
 * 450 MHz train-number check system (TB/T 3325-2013, A-class encoder frame).
 * The receiver uses the same PA4/TIM3 sampler as LBJ; this module demodulates
 * the FFSK audio (1200 baud, 1200/1800 Hz), checks the A-class frame, parses
 * the 32-byte information field and opens a scrollable decode window.
 *
 * Guarded by ENABLE_FEAT_F4HWN_TDCS so stock builds are unaffected.
 */
#ifndef APP_TDCS_DUAL_H
#define APP_TDCS_DUAL_H

#include <stdbool.h>
#include <stdint.h>

/* True when the selected RX VFO is in TDCS mode. */
bool TDCS_RxVfoIsTdcs(void);

/* Called from the 10 ms tick: runs the framer, starts/stops the sampler from
 * the FM squelch state and refreshes the decode window. */
void TDCS_Tick(void);

/* True while the sampler is running (dual-watch is held off then). */
bool TDCS_Active(void);

/* Stop the sampler now (PTT, mode change, overlay app launch). */
void TDCS_StopSampler(void);

/* Decode window state. */
bool TDCS_PopupActive(void);
void TDCS_ClosePopup(void);
void TDCS_DrawPopup(void);

/* Open the decode window by hand (the TCDMSG menu item and the side key / M
 * Long shortcut): shows the newest report and stays up until EXIT or PTT -
 * TCDPOP does not apply to it.  Returns false when there is nothing decoded
 * yet, so the caller can double beep and stay where it was. */
bool TDCS_OpenPopup(void);

/* Key hook: while the window is up every key event is swallowed.  Only a fresh
 * press acts - UP/DOWN step through the history, 6/9 page the field list,
 * 2 clears the history and EXIT closes the window.  PTT closes the window but
 * is left to the normal transmit path.  Returns true when the key was used. */
bool TDCS_HandleKey(uint8_t key, bool pressed, bool held);

#endif /* APP_TDCS_DUAL_H */
