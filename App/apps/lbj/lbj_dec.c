/* LBJ / POCSAG decoder core - see lbj_dec.h. Included by lbj_app.c. */

#include "lbj_dec.h"

#define POC_SYNC      0x7CD215D8u
#define POC_SYNC_INV  2200824359u
#define POC_IDLE      2055848343u

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

/* Correct a single-bit error; returns false when the word is uncorrectable. */
static bool bch_dec(lbj_rx_t *r, uint32_t cw, uint32_t *out)
{
    uint32_t d31 = (cw >> 1) & 0x7FFFFFFFu;
    uint16_t s = bch_syn(d31);
    if (!s) {
        *out = cw;
        return true;
    }
    for (uint8_t p = 0; p < 31u; p++) {
        if (s == r->syn_lut[p]) {
            d31 ^= (1u << p);
            *out = (d31 << 1) | (cw & 1u);
            return true;
        }
    }
    *out = cw;
    return false;
}

void lbj_rx_init(lbj_rx_t *r)
{
    /* Zero everything the loader does not already zero is not needed for .bss;
     * build the 31 single-bit syndromes once. */
    for (uint8_t p = 0; p < 31u; p++)
        r->syn_lut[p] = bch_syn(1u << p);
    r->lut_ready = 1u;
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
        uint32_t d20 = (r->cws[i] >> 11) & 0xFFFFFu;
        for (uint8_t n = 0; n < 5u; n++) {
            uint8_t v = (uint8_t)((d20 >> (16u - n * 4u)) & 0xFu);
            uint8_t rev = (uint8_t)(((v & 1u) << 3) | ((v & 2u) << 1) |
                                    ((v & 4u) >> 1) | ((v & 8u) >> 3));
            bcd[k++] = "0123456789*U -)("[rev];
        }
    }
    r->msgs++;
    if (r->func == 1u)
        r->dn++;
    else if (r->func == 3u)
        r->up++;
    lbj_emit_msg(r, bcd, k);
    r->n = 0;
}

void lbj_rx_bit(lbj_rx_t *r, uint8_t bit)
{
    r->sr = (r->sr << 1) | (bit & 1u);

    if (r->state == 0u) {
        if (pc32(r->sr ^ POC_SYNC) <= 2u) {
            r->pol = 1u; r->state = 1u; r->wc = r->fp = r->nb = 0u;
            r->hunt = 0u;
            r->syncs++;
        } else if (pc32(r->sr ^ POC_SYNC_INV) <= 2u) {
            r->pol = 0u; r->state = 1u; r->wc = r->fp = r->nb = 0u;
            r->hunt = 0u;
            r->syncs++;
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
    bool ok = bch_dec(r, raw, &cor);
    uint8_t cls;

    r->words++;
    if (ok) {
        r->ok++;
        if (cor != raw)
            r->fix++;
    } else {
        r->bad++;
    }
    r->fp++;
    r->wc++;

    if (pc32(cor ^ POC_SYNC) <= 2u) {
        r->wc = r->fp = 0u;
        cls = LBJ_CLS_SYNC;
    } else if (pc32(cor ^ POC_IDLE) <= 2u) {
        if (r->inmsg)
            lbj_flush(r);
        cls = LBJ_CLS_IDLE;
    } else if (!(cor >> 31)) {
        if (r->inmsg)
            lbj_flush(r);
        r->func = (uint8_t)((cor >> 11) & 3u);
        r->addr = ((cor >> 13) & 0x3FFFFu) * 8u + (uint32_t)((r->fp - 1u) >> 1);
        r->n = 0u;
        r->inmsg = 1u;
        r->err = ok ? 0u : 1u;
        cls = LBJ_CLS_ADDR;
    } else {
        cls = LBJ_CLS_MSG;
        if (r->inmsg) {
            if (r->n < LBJ_CWMAX)
                r->cws[r->n++] = cor;
            if (!ok)
                r->err = 1u;
        }
    }
    lbj_emit_word(cor, cls, ok ? 1u : 0u);

    if (r->wc >= 16u)
        r->state = 0u;              /* end of batch: hunt the next sync */
}
