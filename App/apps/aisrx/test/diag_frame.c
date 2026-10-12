/* Dump every CRC-valid frame host_ais would skip: raw length, AIS message type,
 * and why parse rejected it. Diagnoses whether total=N on a real recording are
 * genuine AIS frames of unsupported types or junk.
 *
 *   gcc -O2 -o out/diag_frame diag_frame.c ../ais_decode.c
 *   ./out/diag_frame out/realaudio.u16
 */
#include <stdio.h>
#include "../ais_decode.h"

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s samples.u16\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }

    ais_t d;
    ais_init(&d);
    unsigned char b[2];
    int shown = 0;
    while (fread(b, 1, 2, f) == 2) {
        if (!ais_push(&d, (uint16_t)(b[0] | (b[1] << 8)))) continue;
        ais_info_t in;
        int ok = ais_parse(&d, &in);
        unsigned type = d.nbits >= 6 ? ((d.frame[0] >> 2) & 0x3Fu) : 0xFFu;
        printf("frame nbits=%u type=%u parse=%s crcOk=%u",
               d.nbits, type, ok ? "OK" : "REJECT", d.crcOk);
        if (ok) {
            if (in.type == 1u || in.type == 3u)
                printf(" mmsi=%lu sog=%u cog=%u lat=%ld lon=%ld",
                       (unsigned long)in.mmsi, in.sog, in.cog,
                       (long)in.latS, (long)in.lonS);
            else
                printf(" mmsi=%lu name=[%s] dest=[%s]",
                       (unsigned long)in.mmsi, in.name, in.dest);
        }
        printf("\n");
        shown++;
        ais_rearm(&d);
    }
    fclose(f);
    printf("total_crc=%u  frames_seen=%d\n", d.total, shown);
    return 0;
}
