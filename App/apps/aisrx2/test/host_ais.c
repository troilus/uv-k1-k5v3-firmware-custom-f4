/* Host harness: run the freestanding AIS decoder over a file of little-endian
 * uint16 discriminator samples at 38.4 kHz and print every decoded message,
 * one line per message, in the same format gen_ais.py writes into .exp:
 *
 *   type=1 mmsi=244690971 sog=105 cog=1234 lat=30900540 lon=-85140
 *   type=5 mmsi=215678900 name=[TEST SHIP] dest=[ST PETERSBURG]
 *   total=2
 *
 *   host_ais samples.u16
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
    int frames = 0;
    while (fread(b, 1, 2, f) == 2) {
        if (!ais_push(&d, (uint16_t)(b[0] | (b[1] << 8)))) continue;
        ais_info_t in;
        if (!ais_parse(&d, &in)) { ais_rearm(&d); continue; }
        frames++;
        if (in.type == 1u || in.type == 3u) {
            printf("type=%u mmsi=%lu sog=%u cog=%u lat=%ld lon=%ld\n",
                   in.type, (unsigned long)in.mmsi, in.sog, in.cog,
                   (long)in.latS, (long)in.lonS);
        } else {
            printf("type=%u mmsi=%lu name=[%s] dest=[%s]\n",
                   in.type, (unsigned long)in.mmsi, in.name, in.dest);
        }
        ais_rearm(&d);
    }
    fclose(f);
    printf("total=%u\n", d.total);
    return frames ? 0 : 1;
}
