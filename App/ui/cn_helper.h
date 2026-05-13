#ifndef CN_HELPER_H
#define CN_HELPER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef ENABLE_CHINESE

size_t UI_SmallStringPixelWidth(const char *pString);
void UI_PrintStringSmallAtPixel(const char *pString, uint8_t x_start, uint8_t x_end, uint8_t y_pixel_start, uint8_t y_pixel_end, uint8_t latin_down_when_mixed);
void UI_PrintStringSmallChannelNameBand(const char *pString, uint8_t x_start, uint8_t x_end, uint8_t y_pixel_top);
void UI_PrintStringSmallAtPixelInverse(const char *pString, uint8_t x_start, uint8_t x_end, uint8_t y_pixel_start, uint8_t y_pixel_end);
void UI_PrintStringSmallAtPixelCnInverse(const char *pString, uint8_t x_start, uint8_t x_end, uint8_t y_pixel_start, uint8_t y_pixel_end);

#endif /* ENABLE_CHINESE */

#endif /* CN_HELPER_H */
