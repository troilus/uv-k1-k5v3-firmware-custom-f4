/* LBJ position store (resident firmware feature) - see lbj_pos.c.
 *
 * The positions the decode window measures a train from, edited from the LBJ
 * settings menu (LBJPRI / LBJLAT / LBJLON) rather than from an overlay app.
 *
 * The record is byte for byte the one the - now deprecated, but still buildable
 * - LBJ Edit overlay app keeps in its own config area: a magic, a version,
 * which slot is the primary, and five slots of thirteen decimal digits -
 * latitude DD MM hh then longitude DDD MM hh, hundredths of a minute, north and
 * east only - exactly the fields APRS Edit edits. The popup therefore needs no
 * conversion it does not already have for the APRS TX profile, and the app's
 * store stays readable as a fallback.
 *
 * The store has its own 4 KiB sector of the external flash (0x012000, the gap
 * the flash map leaves between the boot logo and the RX/TX log). Records are
 * appended, never rewritten: a slot is committed only once its bytes are in, so
 * a power loss mid-write leaves the previous record in charge, and the sector is
 * erased only when it runs out of slots.
 *
 * Guarded by ENABLE_FEAT_F4HWN_LBJ so the stock firmwares are unaffected. */
#ifndef APP_LBJ_POS_H
#define APP_LBJ_POS_H

#include <stdbool.h>
#include <stdint.h>

/* The record: magic, version, primary slot, then five slots of 13 digits. */
#define LBJ_POS_MAGIC    0x314A424Cu /* "LBJ1" */
#define LBJ_POS_VERSION  1u
#define LBJ_POS_SLOTS    5u
#define LBJ_POS_DIGITS   13u

#define LBJ_POS_VER      4u          /* version byte                             */
#define LBJ_POS_PRIMARY  5u          /* which slot the popup must use            */
#define LBJ_POS_NONE     0xFFu       /* that byte: no slot chosen                */
#define LBJ_POS_FIRST    8u          /* the first slot's 13 digits               */
#define LBJ_POS_LEN      (LBJ_POS_FIRST + LBJ_POS_SLOTS * LBJ_POS_DIGITS)

/* The 13 digits of one slot, in the order the popup reads them. Filled with
 * 0xFF - the dashes the menu shows - when the store holds nothing for it.
 * False when there is no store at all. */
bool LBJ_POS_Get(uint8_t slot, uint8_t *digits);

/* The slot the popup measures from, LBJ_POS_NONE when none is chosen. */
uint8_t LBJ_POS_Primary(void);

/* Choose which slot that is (LBJ_POS_NONE for "none"). */
void LBJ_POS_SetPrimary(uint8_t slot);

/* Replace one half of one slot: the latitude (6 digits, DD MM hh) or the
 * longitude (7 digits, DDD MM hh). A slot that was never filled in starts from
 * erased digits, so the two halves can be typed in either order and the other
 * one only shows dashes until it is entered too. False - and nothing written -
 * when the digits are not a usable coordinate half. */
bool LBJ_POS_SetLat(uint8_t slot, const uint8_t *digits);
bool LBJ_POS_SetLon(uint8_t slot, const uint8_t *digits);

/* True when 6 (latitude, DD MM hh) or 7 (longitude, DDD MM hh) digits are a
 * filled-in coordinate half in range: minutes below 60, and 90/180 degrees only
 * with nothing after them. LBJ_POS_SetLat/Lon apply the same rules, so the menu
 * can check what has been typed before it commits anything. */
bool LBJ_POS_LatOk(const uint8_t *digits);
bool LBJ_POS_LonOk(const uint8_t *digits);

#endif /* APP_LBJ_POS_H */
