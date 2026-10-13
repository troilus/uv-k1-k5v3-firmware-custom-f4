/* Generic POCSAG decoder core - see pocsag_dec.h. Included by pocsagrx_app.c. */

#include "pocsag_dec.h"

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
static bool bch_dec(uint32_t cw, uint32_t *out)
{
    uint32_t d31 = (cw >> 1) & 0x7FFFFFFFu;
    uint16_t s = bch_syn(d31);
    if (!s) {
        *out = cw;
        return true;
    }
    for (uint8_t p = 0; p < 31u; p++) {
        if (bch_syn(1u << p) == s) {
            d31 ^= (1u << p);
            *out = (d31 << 1) | (cw & 1u);
            return true;
        }
    }
    *out = cw;
    return false;
}

void poc_rx_start(poc_rx_t *r, uint8_t rate)
{
    uint16_t keep = rate;
    for (uint16_t i = 0; i < sizeof *r; i++)
        ((uint8_t *)r)[i] = 0u;
    r->rate = (uint8_t)keep;
}

/* One message finished: hand the address/func/codewords to the app. */
static void poc_flush(poc_rx_t *r)
{
    r->inmsg = 0;
    if (!r->n)
        return;
    r->msgs++;
    poc_emit_msg(r);
    r->n = 0;
}

void poc_rx_bit(poc_rx_t *r, uint8_t bit)
{
    r->sr = (r->sr << 1) | (bit & 1u);

    if (r->state == 0u) {
        if (pc32(r->sr ^ POC_SYNC) <= 3u) {
            r->pol = 1u; r->state = 1u; r->wc = r->fp = r->nb = 0u;
            r->hunt = 0u; r->syncs++;
        } else if (pc32(r->sr ^ POC_SYNC_INV) <= 3u) {
            r->pol = 0u; r->state = 1u; r->wc = r->fp = r->nb = 0u;
            r->hunt = 0u; r->syncs++;
        } else if (r->inmsg) {
            if (++r->hunt > 64u)
                poc_flush(r);        /* message not closed by a sync/idle/batch end */
        }
        return;
    }

    if (++r->nb < 32u)
        return;
    r->nb = 0u;

    uint32_t raw = r->pol ? r->sr : ~r->sr;
    uint32_t cor;
    bool ok = bch_dec(raw, &cor);

    r->words++;
    if (ok) { r->ok++; if (cor != raw) r->fix++; } else r->bad++;

    r->fp++;
    r->wc++;

    if (pc32(cor ^ POC_SYNC) <= 3u) {
        r->wc = r->fp = 0u;
    } else if (pc32(cor ^ POC_IDLE) <= 3u) {
        if (r->inmsg)
            poc_flush(r);
    } else if (!(cor >> 31)) {
        if (r->inmsg)
            poc_flush(r);
        r->func = (uint8_t)((cor >> 11) & 3u);
        r->addr = ((cor >> 13) & 0x3FFFFu) * 8u + (uint32_t)((r->fp - 1u) >> 1);
        r->n = 0u;
        r->inmsg = 1u;
        r->err = ok ? 0u : 1u;
    } else if (r->inmsg) {
        if (r->n < POC_CWMAX)
            r->cws[r->n++] = cor;
        if (!ok)
            r->err = 1u;
    }

    if (r->wc >= 16u)
        r->state = 0u;              /* end of batch: hunt the next sync */
}
