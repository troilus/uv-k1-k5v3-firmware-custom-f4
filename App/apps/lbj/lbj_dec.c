/* LBJ / POCSAG decoder core - see lbj_dec.h. Included by lbj_app.c. */

#include "lbj_dec.h"

#define POC_SYNC      0x7CD215D8u
#define POC_IDLE      2055848343u
#define SYNC_ERR      2u        /* wrong bits tolerated in a sync word (upstream);
                                * the complement test is e >= 32 - SYNC_ERR */

/* 32-bit popcount without a hardware instruction. */
static uint8_t pc32(uint32_t x)
{
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;
    return (uint8_t)((x * 0x01010101u) >> 24);
}

/* BCH(31,21) syndrome: process the 31 bits MSB first through a 10-bit LFSR. */
static uint16_t bch_syn(uint32_t d31)
{
    uint16_t r = 0;
    for (uint8_t i = 31u; i-- > 0u;) {
        uint8_t fb = (uint8_t)((r >> 9) & 1u);
        r = (uint16_t)(((r << 1) | ((d31 >> i) & 1u)) & 0x3FFu);
        if (fb)
            r ^= 873u;
    }
    return r;
}

/* BCH(31,21) decode. Returns 0 when the word was already clean, 1 when a
 * single bit was corrected, 2 when it is uncorrectable. The verdict is what
 * the display's trailing '*' / '!' mark is made of (same as the resident LBJ
 * dual-watch popup); the 31 single-bit syndromes are recomputed on demand, so
 * there is no 62-byte table.
 *
 * The syndrome only sees the 31-bit BCH word, never the trailing even-parity
 * bit, so the parity is checked too (as upstream fix() does): a null syndrome
 * with odd parity means the parity bit itself is the one in error, and a bit
 * correction that still leaves the parity odd means a second bit was wrong -
 * both are caught, where a syndrome-only decoder would keep a bad word. */
static uint8_t bch_dec(uint32_t cw, uint32_t *out)
{
    uint32_t d31 = (cw >> 1) & 0x7FFFFFFFu;
    uint16_t s = bch_syn(d31);
    if (!s) {
        if (pc32(cw) & 1u) {           /* the parity bit is the error */
            *out = cw ^ 1u;
            return 1u;
        }
        *out = cw;
        return 0u;
    }
    for (uint8_t p = 0; p < 31u; p++) {
        if (bch_syn(1u << p) == s) {
            uint32_t c = ((d31 ^ (1u << p)) << 1) | (cw & 1u);
            if ((pc32(c) & 1u) == 0u) {   /* even parity: the one bit was it */
                *out = c;
                return 1u;
            }
            break;                        /* odd: a second bit is wrong too */
        }
    }
    *out = cw;
    return 2u;
}

/* One message finished: extract the BCD string and hand it to the app. */
static void lbj_flush(lbj_rx_t *r)
{
    char bcd[LBJ_CWMAX * 5u];
    uint16_t k = 0;
    r->inmsg = 0;
    if (!r->n)
        return;
    for (uint8_t i = 0; i < r->n; i++) {
        if (!r->cws[i]) {                    /* a lost word: its 5 chars are '?' */
            for (uint8_t n = 0; n < 5u; n++)
                bcd[k++] = '?';
            continue;
        }
        uint32_t d20 = (r->cws[i] >> 11) & 0xFFFFFu;
        for (uint8_t n = 0; n < 5u; n++) {
            uint8_t v = (uint8_t)((d20 >> (16u - n * 4u)) & 0xFu);
            uint8_t rev = (uint8_t)(((v & 1u) << 3) | ((v & 2u) << 1) |
                                    ((v & 4u) >> 1) | ((v & 8u) >> 3));
            bcd[k++] = "0123456789*U -)("[rev];
        }
    }
    /* debug counters (see lbj_dec.h): r->msgs++, r->dn++ / r->up++ */
    lbj_emit_msg(r, bcd, k);
    r->n = 0;
}

void lbj_rx_bit(lbj_rx_t *r, uint8_t bit)
{
    r->sr = (r->sr << 1) | (bit & 1u);

    if (r->state == 0u) {
        /* The discriminator polarity is not known: the sync word may arrive
         * inverted. One popcount of xor with the sync word answers both. */
        uint8_t e = pc32(r->sr ^ POC_SYNC);
        if (e <= SYNC_ERR) {
            r->pol = 1u; r->state = 1u; r->wc = r->fp = r->nb = 0u;
            r->hunt = 0u;
            /* r->syncs++; */
        } else if (e >= 32u - SYNC_ERR) {
            r->pol = 0u; r->state = 1u; r->wc = r->fp = r->nb = 0u;
            r->hunt = 0u;
            /* r->syncs++; */
        } else if (r->inmsg) {
            if (++r->hunt > 64u)
                lbj_flush(r);        /* message not closed by a sync/idle/batch end */
        }
        return;
    }

    if (++r->nb < 32u)
        return;
    r->nb = 0u;

    uint32_t raw = r->pol ? r->sr : ~r->sr;
    uint32_t cor;
    uint8_t st = bch_dec(raw, &cor);       /* 0 clean, 1 repaired, 2 uncorrectable */

    /* debug counters (see lbj_dec.h):
     * r->words++; if (st<2u) { r->ok++; if (st) r->fix++; } else r->bad++; */
    r->fp++;
    r->wc++;

    if (pc32(cor ^ POC_SYNC) <= SYNC_ERR) {
        r->wc = r->fp = 0u;
    } else if (pc32(cor ^ POC_IDLE) <= SYNC_ERR) {
        if (r->inmsg)
            lbj_flush(r);
    } else if (!(cor >> 31)) {
        if (r->inmsg)
            lbj_flush(r);
        r->func = (uint8_t)((cor >> 11) & 3u);
        r->addr = ((cor >> 13) & 0x3FFFFu) * 8u + (uint32_t)((r->fp - 1u) >> 1);
        r->n = 0u;
        r->inmsg = 1u;
        r->err = (st == 2u) ? 1u : 0u;      /* new report: the verdict starts here */
        r->cor = (st == 1u) ? 1u : 0u;
    } else if (r->inmsg) {
        /* A word that cannot be corrected is kept as a 0 marker (no real
         * message word has its id bit clear): its 20 bits are lost but the
         * words that follow stay in place, and the display fills the gap with
         * '?'. The '!' mark is still set. */
        if (r->n < LBJ_CWMAX)
            r->cws[r->n++] = (st == 2u) ? 0u : cor;
        if (st == 2u)
            r->err = 1u;                    /* '!' wins over '*' */
        else if (st == 1u)
            r->cor = 1u;
    }

    if (r->wc >= 16u)
        r->state = 0u;              /* end of batch: hunt the next sync */
}
