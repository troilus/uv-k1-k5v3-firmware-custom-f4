/* LBJ dual-watch (resident firmware feature).
 *
 * When one of the two dual-watch VFOs sits on the railway LBJ frequency
 * (821.2375 MHz) and that channel squelch opens (exactly like a normal FM
 * carrier), a timer-driven sampler decodes the POCSAG/LBJ report and pops up a
 * decode window that stays until EXIT (or PTT). Sampling is entirely in a
 * 19.2 kHz timer interrupt, so the main loop never blocks: idle dual-watch and
 * the whole UI stay smooth. When the squelch closes the sampler stops.
 *
 * The window can also close itself: the LBJPOP menu item (ON / 3 / 5 / 10 /
 * 60 s) picks how long it stays up after the last report or key press. ON is
 * the old behaviour. A window opened by hand from the LBJ menu item never times
 * out.
 *
 * The popup's row 0 shows the distance from the receiver to the decoded train
 * at the right. The train's fix comes from the report's detail block; the
 * receiver's position is the beacon position of the APRS TX overlay app, edited
 * with APRS Edit - so that app must be installed for a distance to appear.
 *
 * Guarded by ENABLE_FEAT_F4HWN_LBJ so the stock firmwares are unaffected.
 */
#ifndef APP_LBJ_DUAL_H
#define APP_LBJ_DUAL_H

#include <stdbool.h>
#include <stdint.h>

/* True when the RX frequency matches the LBJ channel (10 Hz units). */
bool LBJ_IsLbjFreq(uint32_t freq);

/* True when the currently selected RX VFO is the LBJ channel. */
bool LBJ_RxVfoIsLbj(void);

/* Called from the 10 ms tick: starts/stops the sampler from the FM squelch
 * state, refreshes the popup and enforces the safety cap. */
void LBJ_Tick(void);

/* True while the sampler is running (the dual-watch toggle is held off). */
bool LBJ_Active(void);

/* Stop the sampler now (e.g. before keying up). */
void LBJ_StopSampler(void);

/* Popup state. */
bool LBJ_PopupActive(void);
void LBJ_ClosePopup(void);
void LBJ_DrawPopup(void);

/* Open the decode window by hand (the LBJMSG menu item, and the side key /
 * M Long action the operator can point at it), showing the newest report.
 * Returns false when nothing has been decoded yet - the window is not opened at
 * all then, and the caller refuses the key the way a failed shortcut launch
 * does. The stored list is a separate menu item of its own, LBJHST. */
bool LBJ_OpenPopup(void);

/* Open the decode window on the list of reports stored in flash (the LBJHST
 * menu item). Returns false when the log holds nothing, or while it is being
 * cleared. */
bool LBJ_OpenList(void);

/* Key hook: while the popup is up every key event is swallowed, so nothing
 * reaches the radio. Only a fresh press acts - UP/DOWN step through the
 * history once and EXIT closes the window - so holding a key does not
 * auto-repeat and releasing it does not step again. PTT closes the popup but
 * is left to the normal transmit path. Returns true when the key was consumed. */
bool LBJ_HandleKey(uint8_t key, bool pressed, bool held);

/* ADC sharing with the battery reader (channel 8): suspend the sampler around
 * a battery conversion so neither corrupts the other. */
void LBJ_AdcBegin(void);
void LBJ_AdcEnd(void);

#endif /* APP_LBJ_DUAL_H */
