#include <string.h>
#include "driver/st7565.h"
#include "font.h"
#include "ui/cn_helper.h"
#include "ui/helper.h"
#include "misc.h"
#include "settings.h"

#ifdef ENABLE_CHINESE

static bool IsChineseChar(const char *pStr)
{
    uint8_t c = (uint8_t)pStr[0];
    return (c >= 0xE4 && c <= 0xEF);
}

static uint16_t Utf8ToUnicode(const char *pStr)
{
    uint8_t c1 = (uint8_t)pStr[0];
    uint8_t c2 = (uint8_t)pStr[1];
    uint8_t c3 = (uint8_t)pStr[2];
    return ((c1 & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
}

static void DrawChineseChar(uint16_t unicode, uint8_t x, uint8_t y_pixel, uint8_t y_pixel_end)
{
    int16_t spi_index = SETTINGS_CNCharToIndex(unicode);
    if (spi_index < 0)
        return;

    const uint8_t char_height = 12;
    const uint8_t char_width = 12;
    const uint16_t y_range = (uint16_t)y_pixel_end - (uint16_t)y_pixel + 1u;
    uint8_t y_offset = 0;
    if (y_range >= char_height)
        y_offset = (uint8_t)((y_range - char_height) / 2u);
    uint8_t y = y_pixel + y_offset;
    if (y < 8)
        y = 8;

    uint16_t spi_bitmap[12];
    SETTINGS_ReadCNFontBitmap((uint16_t)spi_index, spi_bitmap);

    for (uint8_t row = 0; row < char_height; row++) {
        uint16_t row_data = spi_bitmap[row];
        uint8_t current_y = y + row;
        uint8_t line = (current_y - 8) / 8;
        uint8_t bit_offset = (current_y - 8) % 8;
        for (uint8_t col = 0; col < char_width; col++) {
            if (x + col >= LCD_WIDTH)
                break;
            if (row_data & (0x8000 >> col))
                gFrameBuffer[line][x + col] |= (1 << bit_offset);
        }
    }
}

size_t UI_SmallStringPixelWidth(const char *pString)
{
    const uint8_t eng_char_width = 6;
    const uint8_t chn_char_width = 12;
    size_t        total_width    = 0;
    size_t        i              = 0;
    while (pString[i]) {
        if (IsChineseChar(&pString[i])) {
            total_width += chn_char_width + 1;
            i += 3;
        } else {
            total_width += eng_char_width + 1;
            i++;
        }
    }
    if (total_width > 0)
        total_width--;
    return total_width;
}

void UI_PrintStringSmallAtPixel(const char *pString, uint8_t x_start, uint8_t x_end, uint8_t y_pixel_start, uint8_t y_pixel_end, uint8_t latin_down_when_mixed)
{
    const uint8_t eng_char_width = 6;
    const uint8_t eng_char_height = 7;
    const uint8_t chn_char_width = 12;
    size_t total_width = 0;
    size_t i = 0;
    bool has_chinese = false;
    while (pString[i]) {
        if (IsChineseChar(&pString[i])) {
            has_chinese = true;
            total_width += chn_char_width + 1;
            i += 3;
        } else {
            total_width += eng_char_width + 1;
            i++;
        }
    }
    if (total_width > 0)
        total_width--;
    uint8_t x = x_start;
    if (x_end > x_start && total_width < (x_end - x_start))
        x += (x_end - x_start - total_width) / 2;
    i = 0;
    while (pString[i]) {
        if (IsChineseChar(&pString[i])) {
            uint16_t unicode = Utf8ToUnicode(&pString[i]);
            DrawChineseChar(unicode, x, y_pixel_start, y_pixel_end);
            x += chn_char_width + 1;
            i += 3;
        } else {
            const uint16_t y_range = (uint16_t)y_pixel_end - (uint16_t)y_pixel_start + 1u;
            unsigned y_offset = 0;
            if (y_range >= eng_char_height)
                y_offset = (unsigned)((y_range - eng_char_height) / 2u);
            if (y_range >= eng_char_height) {
                const unsigned max_off = (unsigned)(y_range - eng_char_height);
                if (y_offset > max_off)
                    y_offset = max_off;
            }
            uint8_t y_pixel = y_pixel_start + (uint8_t)y_offset;
            /* Mixed CJK + Latin: move Latin down to align baseline with Han */
            if (has_chinese) {
                const unsigned add = (unsigned)(latin_down_when_mixed + 1u);
                if (y_pixel <= (uint8_t)(255u - add))
                    y_pixel = (uint8_t)(y_pixel + add);
            }
            if (y_pixel < 8)
                y_pixel = 8;
            uint8_t line = (y_pixel - 8) / 8;
            uint8_t bit_offset = (y_pixel - 8) % 8;
            if (pString[i] >= '!' && pString[i] < 127) {
                const unsigned int index = pString[i] - ' ' - 1;
                if (index < ARRAY_SIZE(gFontSmall)) {
                    const uint8_t *font_data = gFontSmall[index];
                    for (uint8_t col = 0; col < eng_char_width; col++) {
                        if (x + col >= LCD_WIDTH)
                            break;
                        uint8_t pixel_col = font_data[col];
                        gFrameBuffer[line][x + col] |= (pixel_col << bit_offset);
                        if (bit_offset + eng_char_height > 8 && line + 1 < FRAME_LINES)
                            gFrameBuffer[line + 1][x + col] |= (pixel_col >> (8 - bit_offset));
                    }
                }
            }
            x += eng_char_width + 1;
            i++;
        }
    }
}

void UI_PrintStringSmallChannelNameBand(const char *pString, uint8_t x_start, uint8_t x_end, uint8_t y_pixel_top)
{
    const uint8_t band_bottom_inclusive = (uint8_t)((unsigned)y_pixel_top + 11u);
    UI_PrintStringSmallAtPixel(pString, x_start, x_end, y_pixel_top, band_bottom_inclusive, 0u);
}

void UI_PrintStringSmallAtPixelInverse(const char *pString, uint8_t x_start, uint8_t x_end, uint8_t y_pixel_start, uint8_t y_pixel_end)
{
    const uint8_t eng_char_width = 6;
    const uint8_t eng_char_height = 7;
    const uint8_t chn_char_width = 12;
    size_t total_width = 0;
    size_t i = 0;
    bool has_chinese = false;
    while (pString[i]) {
        if (IsChineseChar(&pString[i])) {
            has_chinese = true;
            total_width += chn_char_width + 1;
            i += 3;
        } else {
            total_width += eng_char_width + 1;
            i++;
        }
    }
    if (total_width > 0)
        total_width--;
    uint8_t x = x_start;
    if (x_end > x_start && total_width < (x_end - x_start))
        x += (x_end - x_start - total_width) / 2;
    i = 0;
    while (pString[i]) {
        if (IsChineseChar(&pString[i])) {
            uint16_t unicode = Utf8ToUnicode(&pString[i]);
            int16_t spi_idx = SETTINGS_CNCharToIndex(unicode);
            if (spi_idx >= 0) {
                uint16_t spi_bitmap[12];
                SETTINGS_ReadCNFontBitmap((uint16_t)spi_idx, spi_bitmap);
                const uint16_t y_range_inv = (uint16_t)y_pixel_end - (uint16_t)y_pixel_start + 1u;
                const uint8_t chn_top = (y_range_inv >= 12u)
                    ? (uint8_t)(y_pixel_start + (uint8_t)((y_range_inv - 12u) / 2u))
                    : y_pixel_start;
                for (uint8_t row = 0; row < 12; row++) {
                    uint16_t row_data = spi_bitmap[row];
                    uint8_t y = chn_top + row;
                    if (y < 8)
                        y = 8;
                    uint8_t line = (y - 8) / 8;
                    uint8_t bit_offset = (y - 8) % 8;
                    for (uint8_t col = 0; col < 12; col++) {
                        if (x + col >= LCD_WIDTH)
                            break;
                        if (row_data & (0x8000 >> col))
                            gFrameBuffer[line][x + col] &= ~(1 << bit_offset);
                    }
                }
            }
            x += chn_char_width + 1;
            i += 3;
        } else {
            const uint16_t y_range = (uint16_t)y_pixel_end - (uint16_t)y_pixel_start + 1u;
            unsigned y_offset = 0;
            if (y_range >= eng_char_height)
                y_offset = (unsigned)((y_range - eng_char_height) / 2u);
            if (y_range >= eng_char_height) {
                const unsigned max_off = (unsigned)(y_range - eng_char_height);
                if (y_offset > max_off)
                    y_offset = max_off;
            }
            uint8_t y_pixel = y_pixel_start + (uint8_t)y_offset;
            if (has_chinese) {
                if (y_pixel >= y_pixel_start + 4u)
                    y_pixel -= 4u;
                else if (y_pixel > y_pixel_start)
                    y_pixel = y_pixel_start;
            }
            if (y_pixel < 8)
                y_pixel = 8;
            uint8_t line = (y_pixel - 8) / 8;
            uint8_t bit_offset = (y_pixel - 8) % 8;
            if (pString[i] >= '!' && pString[i] < 127) {
                const unsigned int char_index = pString[i] - ' ' - 1;
                if (char_index < ARRAY_SIZE(gFontSmall)) {
                    const uint8_t *font_data = gFontSmall[char_index];
                    for (uint8_t col = 0; col < eng_char_width; col++) {
                        if (x + col >= LCD_WIDTH)
                            break;
                        uint8_t pixel_col = font_data[col];
                        gFrameBuffer[line][x + col] &= ~(pixel_col << bit_offset);
                        if (bit_offset + eng_char_height > 8 && line + 1 < FRAME_LINES)
                            gFrameBuffer[line + 1][x + col] &= ~(pixel_col >> (8 - bit_offset));
                    }
                }
            }
            x += eng_char_width + 1;
            i++;
        }
    }
}

void UI_PrintStringSmallAtPixelCnInverse(const char *pString, uint8_t x_start, uint8_t x_end, uint8_t y_pixel_start, uint8_t y_pixel_end)
{
    const uint8_t chn_char_width = 12;
    size_t total_width = 0;
    size_t i = 0;
    while (pString[i]) {
        if (IsChineseChar(&pString[i])) {
            total_width += chn_char_width + 1;
            i += 3;
        } else {
            total_width += 7;
            i++;
        }
    }
    if (total_width > 0)
        total_width--;
    uint8_t x = x_start;
    if (x_end > x_start && total_width < (x_end - x_start))
        x += (uint8_t)((x_end - x_start - total_width) / 2);
    i = 0;
    while (pString[i]) {
        if (IsChineseChar(&pString[i])) {
            uint16_t unicode = Utf8ToUnicode(&pString[i]);
            int16_t spi_index = SETTINGS_CNCharToIndex(unicode);
            if (spi_index >= 0) {
                uint16_t spi_bitmap[12];
                SETTINGS_ReadCNFontBitmap((uint16_t)spi_index, spi_bitmap);
                const uint16_t y_range = (uint16_t)y_pixel_end - (uint16_t)y_pixel_start + 1u;
                const uint8_t cn_top = (y_range >= 12u)
                    ? (uint8_t)(y_pixel_start + (uint8_t)((y_range - 12u) / 2u))
                    : y_pixel_start;
                for (uint8_t row = 0; row < 12; row++) {
                    uint16_t row_data = spi_bitmap[row];
                    uint8_t y = cn_top + row;
                    if (y < 8)
                        y = 8;
                    uint8_t line = (y - 8) / 8;
                    uint8_t bit_offset = (y - 8) % 8;
                    for (uint8_t col = 0; col < 12; col++) {
                        if (x + col >= LCD_WIDTH)
                            break;
                        if (row_data & (0x8000 >> col))
                            gFrameBuffer[line][x + col] &= ~(1 << bit_offset);
                    }
                }
            }
            x += chn_char_width + 1;
            i += 3;
        } else {
            const uint8_t char_width = 6;
            const uint8_t char_height = 7;
            const uint16_t y_range = (uint16_t)y_pixel_end - (uint16_t)y_pixel_start + 1u;
            unsigned y_offset = 0;
            if (y_range >= char_height)
                y_offset = (unsigned)((y_range - char_height) / 2u);
            if (y_offset > (unsigned)(y_range - char_height))
                y_offset = (unsigned)(y_range - char_height);
            uint8_t y_pixel = y_pixel_start + (uint8_t)y_offset;
            if (y_pixel < 8)
                y_pixel = 8;
            uint8_t line = (y_pixel - 8) / 8;
            uint8_t bit_offset = (y_pixel - 8) % 8;
            if (pString[i] >= '!' && pString[i] < 127) {
                const unsigned int char_idx = pString[i] - ' ' - 1;
                if (char_idx < ARRAY_SIZE(gFontSmall)) {
                    const uint8_t *font_data = gFontSmall[char_idx];
                    for (uint8_t col = 0; col < char_width; col++) {
                        if (x + col >= LCD_WIDTH)
                            break;
                        uint8_t pixel_col = font_data[col];
                        gFrameBuffer[line][x + col] &= ~(pixel_col << bit_offset);
                        if (bit_offset + char_height > 8 && line + 1 < FRAME_LINES)
                            gFrameBuffer[line + 1][x + col] &= ~(pixel_col >> (8 - bit_offset));
                    }
                }
            }
            x += 7;
            i++;
        }
    }
}

#endif /* ENABLE_CHINESE */
