/* LBJ log (resident firmware feature) - see lbj_log.c.
 *
 * One flash record per train, kept in the 32 KiB zone the flash map already
 * reserves for an RX/TX log (0x018000..0x01FFFF, 8 x 4 KiB sectors of 128 x
 * 32 B records). The record discipline is the upstream RF log's: an append-only
 * ring, a CRC over the record, and a commit byte programmed last, so a record
 * cut short by a power loss (or one an earlier firmware left half-written) is
 * simply ignored on the next scan.
 *
 * The record holds the fields the operator asked for - train number, direction,
 * speed, km post, model, registration, route and the distance to the receiver -
 * plus an ordinal and the receive time. It is the export side, not the decoder:
 * the storage never looks at the payload, it just stores what lbj_dual.c hands
 * over when a train has settled.
 *
 * The zone is read back over the host raw-flash command (0x0738), the same
 * route tools/font_write.py uses, so tools/lbj_log.py can turn it into a CSV
 * without any viewer protocol. See App/apps/lbj/README.md.
 *
 * Guarded by ENABLE_FEAT_F4HWN_LBJ so the stock firmwares are unaffected.
 */
#ifndef APP_LBJ_LOG_H
#define APP_LBJ_LOG_H

#include <stdbool.h>
#include <stdint.h>

/* Sentinels for a field the report did not carry. A 0 would be a real speed,
 * km post or model code, so each gets its own "unknown". */
#define LBJ_LOG_SPD_NONE  0xFFFFu
#define LBJ_LOG_KM_NONE   0xFFFFFFu
#define LBJ_LOG_MDL_NONE  0xFFFFu

/* Presence bits for lbj_log_rec_t.flags: one per field the report actually
 * carried. The direction is not one of these - it has a byte of its own (see
 * LBJ_LOG_DIR_*), because "the frame header did not say" is a third state that
 * a single presence bit could not hold. */
#define LBJ_LOG_F_TRN   0x01u
#define LBJ_LOG_F_SPD   0x02u
#define LBJ_LOG_F_KM    0x04u
#define LBJ_LOG_F_MDL   0x08u
#define LBJ_LOG_F_LOC   0x10u
#define LBJ_LOG_F_RT    0x20u
#define LBJ_LOG_F_DST   0x40u

/* On flash only: the route field is four 12-bit character codes packed into
 * rt[4] and the two reserved bytes, not four ASCII initials. Written by the
 * Chinese build, understood by both; never handed back in
 * lbj_log_rec_t.flags. lbj_log.c has the encoding. */
#define LBJ_LOG_F_RT_PACKED 0x80u

/* lbj_log_rec_t.dir, exactly what the popup shows: func 1 is down, func 3 is
 * up, anything else is the "??" the row prints. */
#define LBJ_LOG_DIR_NONE  0u
#define LBJ_LOG_DIR_DN    1u
#define LBJ_LOG_DIR_UP    2u

/* The route name. The English build stores the four pinyin initials ("JGX");
 * the Chinese build stores the four characters themselves as GB2312 pairs.
 * Both fit, terminator included, in LBJ_LOG_RT_LEN. */
#define LBJ_LOG_RT_LEN 9u

/* One train, in the terms the decoder speaks. Strings are '\0' padded (and are
 * empty when the report did not carry that half); the numbers use the NONE
 * sentinels above. */
typedef struct {
    uint8_t  flags;
    uint8_t  dir;        /* LBJ_LOG_DIR_*                                 */
    uint16_t sec;        /* uptime seconds when the report was received   */
    char     pref[2];    /* train class prefix (K/T/D/C...), 0 = none      */
    char     trn[6];     /* train digits, digits only, '\0' padded         */
    uint16_t spd;        /* km/h                                           */
    uint32_t km10;       /* km post in 0.1 km units (5 BCD digits)         */
    uint16_t mdl;        /* model code 0..999 -> type_name()               */
    char     loc[8];     /* 8-digit registration                           */
    char     rt[LBJ_LOG_RT_LEN]; /* route name, see above                  */
    int16_t  dst;        /* distance, tenths of km; < 0 = unknown kind     */
    uint16_t seq;        /* the ordinal the record carries in the flash, i.e.
                          * the export's "seq" column. Only the viewer fills it
                          * - the writer takes the ordinal from the ring. */
} lbj_log_rec_t;

/* The LBJLOG menu setting. */
bool LBJ_LOG_Enabled(void);

/* Append one record. Returns false when it could not be stored (the log is
 * disabled or an overlay app holds the flash cache); the caller then keeps it
 * for the next try. */
bool LBJ_LOG_Write(const lbj_log_rec_t *rec);

/* Start erasing the whole zone. LBJ_LOG_Tick() does one sector per call so the
 * clear never blocks the main loop for long. */
void LBJ_LOG_Clear(void);

/* Main-loop housekeeping (called from LBJ_Tick at 10 ms): one sector of a
 * running clear. Cheap and empty when nothing is going on. */
void LBJ_LOG_Tick(void);

/* An overlay app runs its code from the driver's 4 KiB sector cache, so no log
 * write (which reads a whole sector into that cache) may happen while one is
 * up. The loader calls these around the app's lifetime. */
void LBJ_LOG_Suspend(void);
void LBJ_LOG_Resume(void);

/* ---- the viewer (the list the radio shows) -------------------------------
 *
 * Read-only: the very records the export tool reads, walked newest first
 * straight out of the flash. Nothing is copied into RAM - the radio has 16 KiB
 * for everything - so a view is a position and a count, and every slot it hands
 * over is checked with the same commit + CRC test the writer uses, which means
 * a sector an earlier firmware left behind is skipped instead of shown.
 */
typedef struct {
    uint32_t pos;        /* flash address of the record this view is on      */
    uint16_t idx;        /* its rank from the newest, 0 based                */
    uint16_t count;      /* records the ring held when the view was opened   */
} lbj_log_view_t;

/* Point a view at the newest stored record - the ring is scanned once, not on
 * every open. False when nothing is stored, or while a clear is still running
 * (anything listed now could be wiped from under it). */
bool LBJ_LOG_ViewOpen(lbj_log_view_t *v);

/* Read the record a view is on. Unreadable slots come back empty. */
void LBJ_LOG_ViewGet(const lbj_log_view_t *v, lbj_log_rec_t *out);

/* Step one record older / newer. False at either end of the ring. */
bool LBJ_LOG_ViewOlder(lbj_log_view_t *v);
bool LBJ_LOG_ViewNewer(lbj_log_view_t *v);

#endif /* APP_LBJ_LOG_H */
