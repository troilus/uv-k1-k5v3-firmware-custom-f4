/* QR Code encoder for the LBJ decode popup.
 *
 * The URL a 1234001/1234002 (or a merged 65-character) report hands to the
 * phone is:
 *
 *   https://uri.amap.com/marker?position=<lon>,<lat>&coordinate=wgs84
 *
 * That is 70 bytes with four decimal places, so the symbol is QR version 4-L
 * (33 x 33 modules, 78 bytes at level L). The coordinate=wgs84 parameter lets
 * Amap convert the WGS84 fix to GCJ-02 on its side, so the firmware does not
 * need the conversion math.
 *
 * Pure C, no API call: the host test builds it as it is.
 */
#ifndef LBJ_QR_H
#define LBJ_QR_H

#include <stdbool.h>
#include <stdint.h>

#define LBJ_QR_N    33u    /* modules per side                        */
#define LBJ_QR_MAX  78u    /* bytes that fit at level L               */

/* Encode the map URL for a position in 1e-4 degree units (the unit lbj_fix()
 * returns, north and east only). Row y of the symbol is rows[y]: bit x set =
 * dark module, x = 0 on the left. The quiet zone is not included.
 *
 * Returns false when the position is out of range or the URL does not fit
 * (neither happens for a position lbj_fix() accepted). */
bool LBJ_QR_Build(uint32_t lat4, uint32_t lon4, uint64_t rows[LBJ_QR_N]);

#endif /* LBJ_QR_H */
