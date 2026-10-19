/* LBJ position store (resident firmware feature) - see lbj_pos.h.
 *
 * One 4 KiB sector of the external flash, in the gap the flash map leaves
 * between the boot logo (0x011000) and the RX/TX log (0x018000). It holds the
 * five positions the decode window measures a train from, in the very record
 * the LBJ Edit overlay app used to keep in its own config area, so the popup
 * reads the same digits it always did - only the editor moved into the resident
 * settings menu.
 *
 * The discipline is the LBJ log's: an append-only list of records, and a record
 * counts only once its commit byte is programmed behind it. Programming can
 * only turn 1s into 0s, so a record cut short by a power loss is exactly the
 * record that never was, and the scan below stops at the first append slot that
 * is not committed - which is also where the next record goes. The sector is
 * erased only when the list is full (53 saves), and the record in force is
 * written straight back after that erase, so the worst a power loss can cost is
 * the previous record during that one window.
 *
 * The store is the resident one; an installed LBJ Edit app's store and the APRS
 * TX profile stay as fallbacks in lbj_dual.c, which is what reads this. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/py25q16.h"
#include "lbj_pos.h"

/* ---- the flash layout ---------------------------------------------------- */

#define LBJ_POS_BASE    0x012000u   /* "the gap" in the flash map               */
#define LBJ_POS_SECTOR  0x1000u     /* 4 KiB                                    */
#define LBJ_POS_CMT     LBJ_POS_LEN /* the commit byte, behind the record       */
#define LBJ_POS_STEP    76u         /* 73 + commit + 2 pad, 4-byte aligned      */
#define LBJ_POS_COMMIT  0x5Au       /* programmed last; 0xFF = nothing here     */
#define LBJ_POS_RECS    (LBJ_POS_SECTOR / LBJ_POS_STEP)

_Static_assert(LBJ_POS_STEP >= LBJ_POS_LEN + 1u, "the commit byte fits the step");
_Static_assert(LBJ_POS_STEP % 4u == 0u, "every record starts 4-byte aligned");
_Static_assert(LBJ_POS_RECS > 0u, "the sector must hold at least one record");

/* The newest record we read (or wrote), and whether the flash was looked at
 * yet. Only ever one record: the radio has 16 KiB of RAM for everything. */
static uint8_t pos_rec[LBJ_POS_LEN];
static bool    pos_have;
static bool    pos_read;

static void pos_blank(void)
{
    memset(pos_rec, 0xFF, sizeof pos_rec);
}

/* A fresh store: the magic, the version and five erased slots. Only written
 * once something is edited, so a radio whose owner never sets a position keeps
 * the popup on the APRS TX fallback. */
static void pos_init_record(void)
{
    const uint32_t magic = LBJ_POS_MAGIC;

    pos_blank();
    memcpy(pos_rec, &magic, sizeof magic);
    pos_rec[LBJ_POS_VER]     = LBJ_POS_VERSION;
    pos_rec[LBJ_POS_PRIMARY] = LBJ_POS_NONE;
}

/* Read the sector: everything up to the first slot that is not committed, the
 * last record that parses being the one in force. The scan ends at that first
 * gap rather than walking all 53 slots, and a record from another version is
 * skipped instead of stopping the scan (the newest of ours still wins). */
static void pos_load(void)
{
    uint8_t  buf[LBJ_POS_STEP];
    uint32_t off;

    pos_read = true;
    pos_have = false;
    pos_blank();

    for (off = 0; off + LBJ_POS_STEP <= LBJ_POS_SECTOR; off += LBJ_POS_STEP)
    {
        uint32_t magic;

        PY25Q16_ReadBuffer(LBJ_POS_BASE + off, buf, LBJ_POS_STEP);
        if (buf[LBJ_POS_CMT] != LBJ_POS_COMMIT)
            break;

        memcpy(&magic, buf, sizeof magic);
        if (magic != LBJ_POS_MAGIC || buf[LBJ_POS_VER] != LBJ_POS_VERSION)
            continue;

        memcpy(pos_rec, buf, LBJ_POS_LEN);
        pos_have = true;
    }

    if (pos_have && pos_rec[LBJ_POS_PRIMARY] >= LBJ_POS_SLOTS)
        pos_rec[LBJ_POS_PRIMARY] = LBJ_POS_NONE;   /* 0xFF or a stray value */
}

static void pos_ensure(void)
{
    if (!pos_read)
        pos_load();
}

/* Append what pos_rec holds. The erase, when the list is full, loses the
 * previous record but never the one being written. */
static void pos_store(void)
{
    uint8_t  buf[LBJ_POS_STEP];
    uint32_t off;

    for (off = 0; off + LBJ_POS_STEP <= LBJ_POS_SECTOR; off += LBJ_POS_STEP)
    {
        uint8_t cmt;

        PY25Q16_ReadBuffer(LBJ_POS_BASE + off + LBJ_POS_CMT, &cmt, 1u);
        if (cmt != LBJ_POS_COMMIT)
            break;
    }

    if (off + LBJ_POS_STEP > LBJ_POS_SECTOR)
    {
        PY25Q16_SectorErase(LBJ_POS_BASE);
        off = 0;
    }

    memset(buf, 0xFF, sizeof buf);
    memcpy(buf, pos_rec, LBJ_POS_LEN);
    PY25Q16_WriteBuffer(LBJ_POS_BASE + off, buf, LBJ_POS_LEN, false);

    buf[0] = LBJ_POS_COMMIT;
    PY25Q16_WriteBuffer(LBJ_POS_BASE + off + LBJ_POS_CMT, buf, 1u, false);

    pos_read = true;
    pos_have = true;
}

/* ---- the digits ---------------------------------------------------------- */

static bool half_filled(const uint8_t *d, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
        if (d[i] > 9u)
            return false;

    return true;
}

/* DD MM hh, the range the APRS Edit editor applies plus what the popup can
 * read back: 90 degrees only with nothing after them. */
static bool lat_range(const uint8_t *d)
{
    const uint8_t deg = (uint8_t)(d[0] * 10u + d[1]);
    const uint8_t min = (uint8_t)(d[2] * 10u + d[3]);

    if (deg > 90u || min > 59u)
        return false;
    if (deg == 90u && (min | d[4] | d[5]))
        return false;

    return true;
}

/* DDD MM hh, the same rules at 180 degrees. */
static bool lon_range(const uint8_t *d)
{
    const uint8_t deg = (uint8_t)(d[0] * 100u + d[1] * 10u + d[2]);
    const uint8_t min = (uint8_t)(d[3] * 10u + d[4]);

    if (deg > 180u || min > 59u)
        return false;
    if (deg == 180u && (min | d[5] | d[6]))
        return false;

    return true;
}

bool LBJ_POS_LatOk(const uint8_t *digits)
{
    return half_filled(digits, 6u) && lat_range(digits);
}

bool LBJ_POS_LonOk(const uint8_t *digits)
{
    return half_filled(digits, 7u) && lon_range(digits);
}

bool LBJ_POS_Get(uint8_t slot, uint8_t *digits)
{
    pos_ensure();

    if (!pos_have || slot >= LBJ_POS_SLOTS)
    {
        memset(digits, 0xFF, LBJ_POS_DIGITS);
        return false;
    }

    memcpy(digits, pos_rec + LBJ_POS_FIRST + (uint16_t)slot * LBJ_POS_DIGITS,
           LBJ_POS_DIGITS);
    return true;
}

uint8_t LBJ_POS_Primary(void)
{
    pos_ensure();

    return pos_have ? pos_rec[LBJ_POS_PRIMARY] : LBJ_POS_NONE;
}

void LBJ_POS_SetPrimary(uint8_t slot)
{
    pos_ensure();

    if (slot >= LBJ_POS_SLOTS)
        slot = LBJ_POS_NONE;

    if (!pos_have)
    {
        if (slot == LBJ_POS_NONE)
            return;                    /* nothing to say, so write nothing */

        pos_init_record();
    }
    else if (pos_rec[LBJ_POS_PRIMARY] == slot)
        return;                        /* already the one the popup uses */

    pos_rec[LBJ_POS_PRIMARY] = slot;
    pos_store();
}

/* Write one half of one slot: first = 0 for the latitude, 6 for the longitude.
 * The rest of the slot - the other half, the primary and the other slots - is
 * carried over as it stands, so an edit never disturbs what it does not own. */
static bool pos_set_half(uint8_t slot, uint8_t first, const uint8_t *digits,
                         uint8_t len)
{
    uint8_t *dst;

    pos_ensure();

    if (slot >= LBJ_POS_SLOTS)
        return false;

    if (!pos_have)
        pos_init_record();

    dst = pos_rec + LBJ_POS_FIRST + (uint16_t)slot * LBJ_POS_DIGITS + first;
    memcpy(dst, digits, len);
    pos_store();

    return true;
}

bool LBJ_POS_SetLat(uint8_t slot, const uint8_t *digits)
{
    if (!half_filled(digits, 6u) || !lat_range(digits))
        return false;

    return pos_set_half(slot, 0u, digits, 6u);
}

bool LBJ_POS_SetLon(uint8_t slot, const uint8_t *digits)
{
    if (!half_filled(digits, 7u) || !lon_range(digits))
        return false;

    return pos_set_half(slot, 6u, digits, 7u);
}
