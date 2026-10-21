/* LBJ log (resident firmware feature) - see lbj_log.h.
 *
 * A 32 KiB append-only ring in the external flash zone the flash map already
 * reserves for an RX/TX log (0x018000..0x01FFFF). Each 32-byte record carries
 * one train; a record is written into an erased slot, its CRC covers the first
 * 30 bytes and its commit byte is programmed afterwards, exactly as the
 * upstream RF log does it. Programming never rewrites a slot: the ring moves
 * forward and a sector is erased only when its turn comes round again, so a
 * busy night costs one page program per train and one sector erase per 128.
 *
 * The zone is read back by the host over the raw-flash command (0x0738), the
 * same one tools/font_write.py uses, so tools/lbj_log.py turns it into a CSV
 * with no viewer protocol involved.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/py25q16.h"
#include "lbj_log.h"
#include "settings.h"

/* ---- flash layout -------------------------------------------------------- */

#define LBJ_LOG_BASE      0x018000u   /* "RX/TX log" in the flash map, unused  */
#define LBJ_LOG_SECTOR    0x1000u     /* 4 KiB                                 */
#define LBJ_LOG_SECTORS   8u          /* 32 KiB                                */
#define LBJ_LOG_END       (LBJ_LOG_BASE + (LBJ_LOG_SECTOR * LBJ_LOG_SECTORS))

#define LBJ_LOG_REC_SIZE  32u
#define LBJ_LOG_COMMIT    0xA5u

/* On-flash record: little endian and packed, so the tool can parse it field by
 * field. Bytes 28..29 are reserved (0xFF) for a later revision. */
typedef struct __attribute__((packed)) {
    uint8_t  flags;      /* LBJ_LOG_F_*                                        */
    char     pref[2];    /* train class prefix                                 */
    uint8_t  trn[3];     /* 6 BCD nibbles, 0xF = missing                       */
    uint16_t spd;        /* km/h, LBJ_LOG_SPD_NONE if unknown                  */
    uint8_t  km[3];      /* km post, 0.1 km, 24-bit LE, 0xFFFFFF if unknown    */
    uint16_t mdl;        /* model code, LBJ_LOG_MDL_NONE if unknown            */
    uint8_t  loc[4];     /* 8 BCD nibbles, 0xF = missing                       */
    char     rt[4];      /* route initials                                     */
    int16_t  dst;        /* distance, tenths of km; < 0 = unknown              */
    uint8_t  dir;        /* LBJ_LOG_DIR_*                                      */
    uint16_t seq;        /* ordinal, monotonic while the ring is not cleared   */
    uint16_t uptime;     /* receive time, seconds since power on               */
    uint8_t  rsv1[2];    /* reserved (0xFF)                                    */
    uint8_t  crc;        /* CRC8 over the first 30 bytes                       */
    uint8_t  commit;     /* LBJ_LOG_COMMIT, programmed last                    */
} lbj_log_flash_t;

_Static_assert(sizeof(lbj_log_flash_t) == LBJ_LOG_REC_SIZE, "LBJ log record size");
_Static_assert((LBJ_LOG_SECTOR / LBJ_LOG_REC_SIZE) * LBJ_LOG_SECTORS == 1024u,
               "the ring holds 128 records per sector, 1024 in all");

/* The layout is a contract with tools/lbj_log.py: lock every field offset so a
 * struct tweak cannot silently shift what the export reads. */
_Static_assert(offsetof(lbj_log_flash_t, pref)   ==  1, "LBJ log layout");
_Static_assert(offsetof(lbj_log_flash_t, trn)    ==  3, "LBJ log layout");
_Static_assert(offsetof(lbj_log_flash_t, spd)    ==  6, "LBJ log layout");
_Static_assert(offsetof(lbj_log_flash_t, km)     ==  8, "LBJ log layout");
_Static_assert(offsetof(lbj_log_flash_t, mdl)    == 11, "LBJ log layout");
_Static_assert(offsetof(lbj_log_flash_t, loc)    == 13, "LBJ log layout");
_Static_assert(offsetof(lbj_log_flash_t, rt)     == 17, "LBJ log layout");
_Static_assert(offsetof(lbj_log_flash_t, dst)    == 21, "LBJ log layout");
_Static_assert(offsetof(lbj_log_flash_t, dir)    == 23, "LBJ log layout");
_Static_assert(offsetof(lbj_log_flash_t, seq)    == 24, "LBJ log layout");
_Static_assert(offsetof(lbj_log_flash_t, uptime) == 26, "LBJ log layout");
_Static_assert(offsetof(lbj_log_flash_t, crc)    == 30, "LBJ log layout");
_Static_assert(offsetof(lbj_log_flash_t, commit) == 31, "LBJ log layout");

/* ---- state --------------------------------------------------------------- */

static uint32_t head;          /* next slot to write                          */
static uint16_t nextSeq;       /* ordinal of the next record                  */
static bool     ready;         /* the ring has been scanned                   */
static bool     clearing;      /* a clear is in progress                      */
static uint8_t  clearSector;
static bool     suspended;     /* an overlay app owns the flash cache         */
/* What the last scan saw. The viewer starts at newestAddr instead of rescanning
 * and uses storedCount to know where the ring ends; ringMoved says a write has
 * been filed since, so the pair is stale and the next open has to scan again. */
static uint32_t newestAddr;
static uint16_t storedCount;
static bool     ringMoved;

/* ---- helpers ------------------------------------------------------------- */

/* CRC8, init 0x5A, polynomial 0x31 - byte for byte the upstream RF log's. */
static uint8_t crc8(const uint8_t *p, uint8_t n){
    uint8_t crc = 0x5Au;
    while (n--){
        crc ^= *p++;
        for (uint8_t i = 0; i < 8u; i++)
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x31u)
                                : (uint8_t)(crc << 1);
    }
    return crc;
}

/* n digits -> n/2 bytes, high nibble first, 0xF for a missing digit. */
static void pack_bcd(uint8_t *out, const char *s, uint8_t n){
    for (uint8_t i = 0; i < n; i += 2u){
        const uint8_t hi = (s[i]     >= '0' && s[i]     <= '9') ? (uint8_t)(s[i]     - '0') : 0x0Fu;
        const uint8_t lo = (s[i + 1u] >= '0' && s[i + 1u] <= '9') ? (uint8_t)(s[i + 1u] - '0') : 0x0Fu;
        *out++ = (uint8_t)((hi << 4) | lo);
    }
}

/* The other way round, for the viewer: n/2 bytes -> n characters, a nibble that
 * is not a digit (the 0xF the writer pads with) reads as a dash. */
static void unpack_bcd(char *out, const uint8_t *p, uint8_t n){
    for (uint8_t i = 0; i < n; i++){
        const uint8_t b = p[i / 2u];
        const uint8_t d = (uint8_t)((i & 1u) ? (b & 0x0Fu) : (b >> 4));
        out[i] = (d <= 9u) ? (char)('0' + d) : '-';
    }
}

/* A character a report actually carried: the writer leaves 0xFF (and the odd
 * 0x00 from a shorter string) in the rest of a text field. */
static bool ch_ok(char c){
    return c >= 0x20 && c <= 0x7E;
}

static bool record_ok(const lbj_log_flash_t *e){
    return e->commit == LBJ_LOG_COMMIT &&
           e->crc == crc8((const uint8_t *)e, LBJ_LOG_REC_SIZE - 2u);
}

/* Read the ring once: find the newest valid record, count the valid records,
 * and resume behind the newest. The ring is written in order, so everything
 * from the write pointer round to the newest is stored data and the rest is
 * either blank or a sector an earlier firmware left behind - the commit + CRC
 * test tells them apart. Reading the whole ring is the only way to know where a
 * previous session stopped; it is done in batches, on the first tick after the
 * log is switched on (LBJ_LOG_Tick) and when the viewer needs an exact list,
 * never while a train is being filed. */
static void scan_ring(void){
    head        = LBJ_LOG_BASE;
    nextSeq     = 0u;
    newestAddr  = 0u;
    storedCount = 0u;
    ringMoved   = false;          /* what the viewer now reads is what is there */

    bool     found = false;
    uint16_t newestSeq = 0;

    /* Four records (128 bytes) per SPI transaction: 256 of them read the whole
     * 32 KiB, instead of one short transaction per record. */
    lbj_log_flash_t buf[4];

    for (uint32_t a = LBJ_LOG_BASE; a < LBJ_LOG_END; a += sizeof buf){
        PY25Q16_ReadBuffer(a, buf, sizeof buf);
        for (uint8_t i = 0; i < (uint8_t)(sizeof buf / sizeof buf[0]); i++){
            const lbj_log_flash_t *e = &buf[i];
            if (!record_ok(e))
                continue;
            storedCount++;
            if (!found || (int16_t)(e->seq - newestSeq) > 0){
                found = true;
                newestSeq  = e->seq;
                newestAddr = a + (uint32_t)i * LBJ_LOG_REC_SIZE;
            }
        }
    }

    if (found){
        head = newestAddr + LBJ_LOG_REC_SIZE;
        if (head >= LBJ_LOG_END)
            head = LBJ_LOG_BASE;
        nextSeq = (uint16_t)(newestSeq + 1u);
    }
}

static void ensure_ready(void){
    if (ready)
        return;
    ready = true;
    scan_ring();
}

static bool slot_blank(uint32_t address){
    lbj_log_flash_t e;
    PY25Q16_ReadBuffer(address, &e, LBJ_LOG_REC_SIZE);
    const uint8_t *p = (const uint8_t *)&e;
    for (uint8_t i = 0; i < LBJ_LOG_REC_SIZE; i++)
        if (p[i] != 0xFFu)
            return false;
    return true;
}

/* Move to a slot that is still erased, erasing its sector if the ring has come
 * all the way round to a used one. Programming then only ever sets bits to 0,
 * so the driver's write path stays on its fast page-program branch. */
static void prepare_slot(void){
    if (head >= LBJ_LOG_END)
        head = LBJ_LOG_BASE;

    if (slot_blank(head))
        return;

    if ((head % LBJ_LOG_SECTOR) != 0u){
        head += LBJ_LOG_SECTOR - (head % LBJ_LOG_SECTOR);
        if (head >= LBJ_LOG_END)
            head = LBJ_LOG_BASE;
    }

    if (!slot_blank(head))
        PY25Q16_SectorErase(head);

    /* A sector the ring came back round to takes 128 stored records with it, so
     * a view opened from the last scan would walk over a hole: make the next
     * open scan again. */
    ringMoved = true;
}

/* ---- the route field ------------------------------------------------------
 *
 * The English build hands over up to four pinyin initials, which fit the 4-byte
 * rt field as they are. The Chinese build hands over the four characters
 * themselves - four GB2312 pairs, 8 bytes - while the record has 6 bytes to
 * spare (rt[4] plus the two reserved bytes). Level-1 GB2312 holds 3755
 * characters, so one character is a 12-bit index in it, and four of those are
 * 48 bits: exactly the two fields. flags bit 7 says which form a record holds,
 * so a record an English build wrote still reads as initials.
 *
 * A route character the Chinese font does not carry falls back to its pinyin
 * initial (an ASCII letter, or '?'), and that has a range of its own, so the
 * field never has to throw anything away. 0xFFF is "no character there".
 */
#define RT_NONE   0xFFFu
#define RT_ASCII  0xF00u              /* + (c - 0x20) for c 0x20..0x7F */

/* The four 12-bit codes of a route name, written into rt[4] and the reserved
 * bytes, low bits first. */
static void rt_pack(const char *rt, lbj_log_flash_t *e){
    uint8_t b[6] = { 0, 0, 0, 0, 0, 0 };
    const char *p = rt;

    for (uint8_t i = 0; i < 4u; i++){
        uint16_t c = RT_NONE;
        if (*p != '\0' && (uint8_t)p[0] >= 0xA1u && (uint8_t)p[0] <= 0xF7u &&
            (uint8_t)p[1] >= 0xA1u && (uint8_t)p[1] <= 0xFEu){
            const uint8_t hi = (uint8_t)p[0], lo = (uint8_t)p[1];
            p += 2;
            if (hi >= 0xB0u && hi <= 0xD7u){
                const uint16_t idx = (uint16_t)((uint16_t)(hi - 0xB0u) * 94u
                                                + (uint16_t)(lo - 0xA1u));
                if (idx < 3755u)
                    c = idx;
            }
        } else if (*p != '\0'){
            const uint8_t v = (uint8_t)*p++;
            if (v >= 0x20u && v < 0x80u)
                c = (uint16_t)(RT_ASCII + (v - 0x20u));
        }
        for (uint8_t k = 0; k < 12u; k++){
            if (c & (uint16_t)(1u << k)){
                const uint8_t bit = (uint8_t)(12u * i + k);
                b[bit >> 3] |= (uint8_t)(1u << (bit & 7u));
            }
        }
    }

    e->rt[0] = (char)b[0]; e->rt[1] = (char)b[1];
    e->rt[2] = (char)b[2]; e->rt[3] = (char)b[3];
    e->rsv1[0] = b[4];     e->rsv1[1] = b[5];
}

/* The inverse, into a C string. */
static void rt_unpack(const char *rt, const uint8_t *rsv, char *out){
    const uint8_t *src = (const uint8_t *)rt;
    uint8_t b[6] = { (uint8_t)src[0], (uint8_t)src[1], (uint8_t)src[2],
                     (uint8_t)src[3], rsv[0], rsv[1] };
    uint8_t n = 0;

    for (uint8_t i = 0; i < 4u; i++){
        uint16_t c = 0;
        for (uint8_t k = 0; k < 12u; k++){
            const uint8_t bit = (uint8_t)(12u * i + k);
            if (b[bit >> 3] & (uint8_t)(1u << (bit & 7u)))
                c |= (uint16_t)(1u << k);
        }
        if (c == RT_NONE)
            continue;
        if (c >= RT_ASCII && c < (uint16_t)(RT_ASCII + 0x60u))
            out[n++] = (char)(c - RT_ASCII + 0x20u);
        else if (c < 3755u){
            out[n++] = (char)(0xB0u + c / 94u);
            out[n++] = (char)(0xA1u + c % 94u);
        }
    }
    out[n] = '\0';
}

/* ---- api ----------------------------------------------------------------- */

bool LBJ_LOG_Enabled(void){
    return gEeprom.LBJ_LOG_ENABLE != 0u;
}

bool LBJ_LOG_Write(const lbj_log_rec_t *rec){
    if (suspended || clearing || !LBJ_LOG_Enabled())
        return false;

    ensure_ready();
    prepare_slot();

    lbj_log_flash_t e;
    memset(&e, 0xFF, sizeof e);

    e.flags    = rec->flags;
    e.pref[0]  = rec->pref[0];
    e.pref[1]  = rec->pref[1];
    pack_bcd(e.trn, rec->trn, 6u);
    pack_bcd(e.loc, rec->loc, 8u);
    if (rec->flags & LBJ_LOG_F_RT){
        bool plain = true;
        for (uint8_t i = 0; i < 4u && rec->rt[i]; i++)
            if ((uint8_t)rec->rt[i] >= 0xA1u){ plain = false; break; }
        if (plain){
            memcpy(e.rt, rec->rt, sizeof e.rt);
        } else {
            rt_pack(rec->rt, &e);
            e.flags |= LBJ_LOG_F_RT_PACKED;
        }
    }
    e.spd = (rec->flags & LBJ_LOG_F_SPD) ? rec->spd : (uint16_t)LBJ_LOG_SPD_NONE;
    e.mdl = (rec->flags & LBJ_LOG_F_MDL) ? rec->mdl : (uint16_t)LBJ_LOG_MDL_NONE;
    e.dst = (rec->flags & LBJ_LOG_F_DST) ? rec->dst : (int16_t)-1;

    const uint32_t km = (rec->flags & LBJ_LOG_F_KM) ? rec->km10 : (uint32_t)LBJ_LOG_KM_NONE;
    e.km[0] = (uint8_t)(km);
    e.km[1] = (uint8_t)(km >> 8);
    e.km[2] = (uint8_t)(km >> 16);

    e.seq    = nextSeq;
    e.uptime = rec->sec;
    e.dir    = rec->dir;
    e.crc    = crc8((const uint8_t *)&e, LBJ_LOG_REC_SIZE - 2u);

    PY25Q16_WriteBuffer(head, &e, LBJ_LOG_REC_SIZE, false);

    const uint8_t commit = LBJ_LOG_COMMIT;   /* separate: committed only when whole */
    PY25Q16_WriteBuffer(head + LBJ_LOG_REC_SIZE - 1u, &commit, 1u, false);

    head += LBJ_LOG_REC_SIZE;
    if (head >= LBJ_LOG_END)
        head = LBJ_LOG_BASE;
    nextSeq++;

    /* The viewer keeps no running index of its own: it reads what the flash
     * holds when it opens, so all a write has to say is "look again". */
    ringMoved = true;

    return true;
}

void LBJ_LOG_Clear(void){
    clearing    = true;
    clearSector = 0u;
}

void LBJ_LOG_Tick(void){
    /* The one-off scan of the ring runs here, on the first tick after the log
     * is switched on: at boot (or while the operator is in the menu) reading a
     * few milliseconds of flash costs nothing, while doing it when the first
     * train is filed would stall the decoder right as it is listening. */
    if (!ready && !clearing && LBJ_LOG_Enabled())
        ensure_ready();

    if (!clearing || suspended)
        return;

    PY25Q16_SectorErase(LBJ_LOG_BASE + ((uint32_t)clearSector * LBJ_LOG_SECTOR));
    clearSector++;
    if (clearSector >= LBJ_LOG_SECTORS){
        clearing = false;
        head     = LBJ_LOG_BASE;
        nextSeq  = 0u;
        ready    = true;
    }
}

void LBJ_LOG_Suspend(void){
    suspended = true;
}

void LBJ_LOG_Resume(void){
    suspended = false;
}

/* ---- the viewer ---------------------------------------------------------- */

/* One slot back / forward in the ring, wrapping at either end. */
static uint32_t step_prev(uint32_t a){
    return (a <= LBJ_LOG_BASE) ? (uint32_t)(LBJ_LOG_END - LBJ_LOG_REC_SIZE)
                               : (uint32_t)(a - LBJ_LOG_REC_SIZE);
}
static uint32_t step_next(uint32_t a){
    const uint32_t n = a + LBJ_LOG_REC_SIZE;
    return (n >= LBJ_LOG_END) ? (uint32_t)LBJ_LOG_BASE : n;
}

/* One slot, read and checked the way the writer wrote it. */
static bool slot_ok(uint32_t address){
    lbj_log_flash_t e;
    PY25Q16_ReadBuffer(address, &e, LBJ_LOG_REC_SIZE);
    return record_ok(&e);
}

bool LBJ_LOG_ViewOpen(lbj_log_view_t *v){
    /* A clear in progress is erasing sector by sector, so anything listed now
     * could be gone in a moment. Better to say there is nothing yet. */
    if (clearing)
        return false;

    if (!ready || ringMoved){
        ready = true;
        scan_ring();
    }
    if (!storedCount)
        return false;

    v->pos   = newestAddr;
    v->idx   = 0u;
    v->count = storedCount;
    return true;
}

void LBJ_LOG_ViewGet(const lbj_log_view_t *v, lbj_log_rec_t *out){
    lbj_log_flash_t e;

    memset(out, 0, sizeof *out);
    PY25Q16_ReadBuffer(v->pos, &e, LBJ_LOG_REC_SIZE);
    if (!record_ok(&e))
        return;                       /* erased under us: hand back "nothing" */

    out->flags = e.flags & (uint8_t)(~LBJ_LOG_F_RT_PACKED);
    out->dir   = e.dir;
    out->sec   = e.uptime;
    out->spd   = e.spd;
    out->km10  = ((uint32_t)e.km[0] | ((uint32_t)e.km[1] << 8) | ((uint32_t)e.km[2] << 16));
    out->mdl   = e.mdl;
    out->dst   = e.dst;
    out->seq   = e.seq;

    /* Text fields keep the padding the writer put there (0xFF, or a 0 behind a
     * shorter string): hand the display the same "not there" the live path
     * uses, an empty string, and a NONE number for a field the report did not
     * carry. The digits keep one dash per lost nibble. */
    out->pref[0] = ch_ok(e.pref[0]) ? e.pref[0] : '\0';
    out->pref[1] = ch_ok(e.pref[1]) ? e.pref[1] : '\0';
    unpack_bcd(out->trn, e.trn, 6u);
    unpack_bcd(out->loc, e.loc, 8u);
    if (e.flags & LBJ_LOG_F_RT_PACKED){
        rt_unpack(e.rt, e.rsv1, out->rt);
    } else {
        for (uint8_t i = 0; i < 4u; i++)
            out->rt[i] = ch_ok(e.rt[i]) ? e.rt[i] : '\0';
    }

    if (!(e.flags & LBJ_LOG_F_SPD)) out->spd  = (uint16_t)LBJ_LOG_SPD_NONE;
    if (!(e.flags & LBJ_LOG_F_KM))  out->km10 = (uint32_t)LBJ_LOG_KM_NONE;
    if (!(e.flags & LBJ_LOG_F_MDL)) out->mdl  = (uint16_t)LBJ_LOG_MDL_NONE;
    if (!(e.flags & LBJ_LOG_F_DST)) out->dst  = -1;
}

/* Step to the neighbouring record, skipping the holes an erase leaves behind.
 * Bounded by the count, so a ring of nothing but holes still returns. */
static bool walk(lbj_log_view_t *v, bool older){
    if (older ? ((uint32_t)v->idx + 1u >= v->count) : (v->idx == 0u))
        return false;                 /* that was the end of the ring */

    uint32_t a = v->pos;
    for (uint16_t n = 0; n < v->count; n++){
        a = older ? step_prev(a) : step_next(a);
        if (!slot_ok(a))
            continue;
        v->pos = a;
        if (older) v->idx++;
        else       v->idx--;
        return true;
    }
    return false;
}

bool LBJ_LOG_ViewOlder(lbj_log_view_t *v){ return walk(v, true);  }
bool LBJ_LOG_ViewNewer(lbj_log_view_t *v){ return walk(v, false); }
