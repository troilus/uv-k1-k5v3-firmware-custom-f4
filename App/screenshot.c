/* Copyright 2024 Armel F4HWN
 * https://github.com/armel
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include "debugging.h"
#include "driver/st7565.h"
#include "screenshot.h"
#include "misc.h"
#include "driver/vcp.h"
#include "driver/keyboard.h"

// SRAM optimization: minimize static allocations
// - previousHash: 256 bytes (16-bit fingerprint per 8-byte chunk)
// - No frame buffers at all: each chunk is computed on demand
static uint16_t previousHash[128] = {0};
static uint8_t forcedBlock = 0;
static uint8_t keepAlive = 3;

#define SCREENSHOT_CHUNK_SIZE       8u
#define SCREENSHOT_CHUNKS_PER_LINE  16u
#define SCREENSHOT_HALF_LINE_COLS   (LCD_WIDTH / 2u)

// FNV-1a over one 8-byte chunk, folded to 16 bits (as upstream K5Viewer).
static uint16_t SCREENSHOT_Hash(const uint8_t *data)
{
    uint32_t h = 2166136261u;
    for (uint8_t i = 0; i < SCREENSHOT_CHUNK_SIZE; i++)
        h = (h ^ data[i]) * 16777619u;
    return (uint16_t)(h ^ (h >> 16));
}

void SCREENSHOT_ParseInput(void)
{
    if (SCREENSHOT_IsLocked())
        return;

    if (UART_IsCableConnected()) {
        keepAlive = 15;
        gUSB_ScreenshotEnabled = false;
    }
    else if (VCP_ScreenshotPing()) {
        keepAlive = 15;
        gUSB_ScreenshotEnabled = true;
    }
}

static void SCREENSHOT_Send(const uint8_t *buf, uint16_t len)
{
    if (gUSB_ScreenshotEnabled) {
        cdc_acm_data_send_with_dtr(buf, len);
    } else {
        UART_Send(buf, len);
    }
}

// Compute one 8-byte output chunk directly from the display buffers.
// Frame layout: per line (status + 7 frame lines), 8 bit layers of
// 16 bytes each. Each bit layer is split into two 64-column chunks.
static void SCREENSHOT_Chunk(uint8_t chunkIdx, uint8_t *dest)
{
    const uint8_t chunkInLine = chunkIdx % SCREENSHOT_CHUNKS_PER_LINE;
    const uint8_t line = chunkIdx / SCREENSHOT_CHUNKS_PER_LINE;
    const uint8_t bit = chunkInLine / 2u;
    const uint8_t columnBase = (chunkInLine % 2u) * SCREENSHOT_HALF_LINE_COLS;
    const uint8_t *src = (line == 0u ? gStatusLine : gFrameBuffer[line - 1u])
                         + columnBase;

    for (uint8_t j = 0; j < SCREENSHOT_CHUNK_SIZE; j++) {
        uint8_t acc = 0;
        for (uint8_t k = 0; k < SCREENSHOT_CHUNK_SIZE; k++) {
            if (src[j * SCREENSHOT_CHUNK_SIZE + k] & (1u << bit))
                acc |= (uint8_t)(1u << k);
        }
        dest[j] = gSetting_set_inv ? (uint8_t)~acc : acc;
    }
}

void SCREENSHOT_Update(bool force)
{
    static bool wasConnected = false;
    uint16_t deltaLen = 0;
    uint8_t changedBitmap[16] = {0};  // 1 bit per chunk
    uint8_t chunk[9];                 // [0] = index, [1..8] = payload

    if (SCREENSHOT_IsLocked())
        return;

    if (keepAlive > 0) {
        if (--keepAlive == 0) {
            // Connection just lost → reset state for next reconnection
            wasConnected = false;
            return;
        }
    } else {
        return;
    }

    // Connection is alive — detect reconnection and force full frame
    if (!wasConnected) {
        force = true;
        wasConnected = true;
    }

    // ==== FIRST PASS: Count changed chunks ====
    for (uint8_t chunkIdx = 0; chunkIdx < 128u; chunkIdx++) {
        SCREENSHOT_Chunk(chunkIdx, &chunk[1]);

        bool changed = SCREENSHOT_Hash(&chunk[1]) != previousHash[chunkIdx];
        bool isForced = (chunkIdx == forcedBlock);

        if (changed || isForced || force) {
            changedBitmap[chunkIdx >> 3] |= (uint8_t)(1u << (chunkIdx & 7u));
            deltaLen += 9u;
        }
    }

    forcedBlock = (forcedBlock + 1u) % 128u;

    if (deltaLen == 0)
        return;

    // Skip transmission if a key is currently pressed
    // UART_Send is blocking - would freeze the main loop and lose keypresses
    if (gKeyReading0 != KEY_INVALID)
        return;

    // ==== Send version marker (for backward compatibility detection) ====
    // New format: sends 0xFF before header
    // Old format: doesn't exist, so viewers can differentiate
    uint8_t versionMarker = 0xFF;
    SCREENSHOT_Send(&versionMarker, 1);

    // ==== Send header ====
    uint8_t header[5] = {
        0xAA, 0x55, 0x02,
        (uint8_t)(deltaLen >> 8),
        (uint8_t)(deltaLen & 0xFF)
    };

    SCREENSHOT_Send(header, 5);

    // ==== SECOND PASS: Send only changed chunks ====
    for (uint8_t chunkIdx = 0; chunkIdx < 128u; chunkIdx++) {
        if (!(changedBitmap[chunkIdx >> 3] & (1u << (chunkIdx & 7u))))
            continue;

        chunk[0] = chunkIdx;
        SCREENSHOT_Chunk(chunkIdx, &chunk[1]);

        SCREENSHOT_Send(chunk, 9);

        // Update the fingerprint only once the chunk is actually sent, so
        // chunks skipped by an early return stay marked as changed.
        previousHash[chunkIdx] = SCREENSHOT_Hash(&chunk[1]);
    }

    uint8_t end = 0x0A;
    SCREENSHOT_Send(&end, 1);
}