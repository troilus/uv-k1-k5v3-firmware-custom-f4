/* Debug aid (not part of CI): includes the decoder source to reach the static
 * bit pipe, replays the per-sample loop, and dumps every sliced raw bit so a
 * failing vector can be compared bit-for-bit with the transmitted stream.
 *
 *   debug_ais samples.u16 [maxbits]
 */
#include <stdio.h>
#include <stdlib.h>
#include "../ais_decode.c"

/* same per-sample math as ais_push(), plus a bit hook */
static FILE *bitf;
static long bitn;

static void push_dbg(ais_t *d, uint16_t sample)
{
    int32_t xq = (int32_t)sample << 8;
    if (d->meanQ8 < 0) d->meanQ8 = xq;
    int32_t e = xq - d->meanQ8;
    d->meanQ8 += (e + (1 << (MEAN_SHR - 1))) >> MEAN_SHR;
    int32_t v = e >> 4;

    uint8_t s = (uint8_t)(v > 0);
    if (s != d->prev_lvl) {
        int32_t err = d->ph;
        if (err > SLOT / 2) err -= SLOT;
        d->ph -= err >> PLL_K;
        if (d->ph < 0) d->ph += SLOT;
        d->prev_lvl = s;
    }

    d->acc += v;
    d->ph  += 256;
    if (d->ph >= SLOT) {
        d->ph -= SLOT;
        uint8_t bit = (uint8_t)(d->acc > 0);
        d->acc = 0;
        uint8_t raw = (uint8_t)(bit == d->prev_bit);
        d->prev_bit = bit;
        if (bitf && bitn < 4000) fprintf(bitf, "%ld:%c\n", ++bitn, raw ? '1' : '0');
        ais_bit(d, raw);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    bitf = fopen("out/bits.txt", "w");

    ais_t d;
    ais_init(&d);
    unsigned char b[2];
    long n = 0;
    uint8_t last = d.state;
    while (fread(b, 1, 2, f) == 2) {
        n++;
        push_dbg(&d, (uint16_t)(b[0] | (b[1] << 8)));
        if (d.state != last) {
            printf("sample %6ld state %u->%u nbits=%u ones=%u ph=%ld bits=%ld\n",
                   n, last, d.state, d.nbits, d.ones, (long)d.ph, bitn);
            if (d.state == AIS_DONE) {
                ais_info_t in;
                int ok = ais_parse(&d, &in);
                printf("   DONE parse=%d type=%u mmsi=%lu\n",
                       ok, in.type, (unsigned long)in.mmsi);
                ais_rearm(&d);
            }
            last = d.state;
        }
    }
    printf("samples=%ld sliced_bits=%ld final state=%u\n", n, bitn, d.state);
    fclose(bitf);
    fclose(f);
    return 0;
}
