/* QR Code encoder for the LBJ decode popup.
 *
 * The URL a 1234001/1234002 (or a merged 65-character) report hands to the
 * phone is:
 *
 *   https://apis.map.qq.com/uri/v1/marker?marker=coord:<lat>,<lon>&referer=lbjradio
 *
 * That is 84 bytes with four decimal places, so the symbol is QR version 5-L
 * (37 x 37 modules, 106 bytes at level L). Version 3-L, which the QR Map
 * overlay app uses, only holds 53 bytes.
 *
 * Pure C, no API call: the host test builds it as it is.
 */
#ifndef LBJ_QR_H
#define LBJ_QR_H

#include <stdbool.h>
#include <stdint.h>

#define LBJ_QR_N    37u    /* modules per side                        */
#define LBJ_QR_MAX 106u    /* bytes that fit at level L               */

/* Encode the map URL for a position in 1e-4 degree units (the unit lbj_fix()
 * returns, north and east only). Row y of the symbol is rows[y]: bit x set =
 * dark module, x = 0 on the left. The quiet zone is not included.
 *
 * Returns false when the position is out of range or the URL does not fit
 * (neither happens for a position lbj_fix() accepted). */
bool LBJ_QR_Build(uint32_t lat4, uint32_t lon4, uint64_t rows[LBJ_QR_N]);

#endif /* LBJ_QR_H */
