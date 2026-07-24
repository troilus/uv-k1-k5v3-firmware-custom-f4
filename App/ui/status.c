/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
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

#include <string.h>

#include "app/chFrScanner.h"
#ifdef ENABLE_FMRADIO
    #include "app/fm.h"
#endif
#include "app/scanner.h"
#include "bitmaps.h"
#include "driver/keyboard.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "functions.h"
#include "helper/battery.h"
#include "misc.h"
#include "settings.h"
#include "ui/battery.h"
#include "ui/helper.h"
#include "ui/ui.h"
#include "ui/status.h"

#ifdef ENABLE_FEAT_F4HWN_RX_TX_TIMER
#ifndef ENABLE_FEAT_F4HWN_DEBUG
static void convertTime(uint8_t *line, uint8_t type) 
{
    uint16_t t = (type == 0) ? (gTxTimerCountdown_500ms / 2) : (3600 - gRxTimerCountdown_500ms / 2);

    uint8_t m = t / 60;
    uint8_t s = t - (m * 60); // Replace modulo with subtraction for efficiency

    gStatusLine[0] = gStatusLine[7] = gStatusLine[14] = 0x00; // Quick fix on display (on scanning I, II, etc.)

    char str[6];
    sprintf(str, "%02u:%02u", m, s);
    UI_PrintStringSmallBufferNormal(str, line);

    gUpdateStatus = true;
}
#endif
#endif

void UI_DisplayStatus()
{
    char str[8] = "";
    uint8_t scanlist_end = 0;
    char scanlist_str[8] = "";

    gUpdateStatus = false;
    UI_StatusClear();

    uint8_t     *line = gStatusLine;
    unsigned int x    = 0;

/*#ifdef ENABLE_NOAA
    // NOAA indicator
    if (!(gScanStateDir != SCAN_OFF || SCANNER_IsScanning()) && gIsNoaaMode) { // NOASS SCAN indicator
        memcpy(line + x, BITMAP_NOAA, sizeof(BITMAP_NOAA));
    }
    // Power Save indicator
    else if (gCurrentFunction == FUNCTION_POWER_SAVE) {
        memcpy(line + x, gFontPowerSave, sizeof(gFontPowerSave));
    }
    x += 8;
#else
    // Power Save indicator
    if (gCurrentFunction == FUNCTION_POWER_SAVE) {
        memcpy(line + x, gFontPowerSave, sizeof(gFontPowerSave));
    }
    x += 8;
#endif*/

    unsigned int x1 = x;

#ifdef ENABLE_DTMF_CALLING
    if (gSetting_KILLED) {
        memset(line + x, 0xFF, 10);
        x1 = x + 10;
    }
    else
#endif
    { // SCAN indicator
        if (gScanStateDir != SCAN_OFF || SCANNER_IsScanning()) {
            if (IS_MR_CHANNEL(gNextMrChannel) && !SCANNER_IsScanning()) { // channel mode

                if(gEeprom.SCAN_LIST_DEFAULT == MR_CHANNELS_LIST + 1)
                {
                    sprintf(scanlist_str, gEeprom.SCAN_LIST_ENABLED ? "%s+" : "%s", "ALL");
                    scanlist_end = gEeprom.SCAN_LIST_ENABLED ? 18 : 14;
                }
                else
                {
                    const char *name = gListName[gEeprom.SCAN_LIST_DEFAULT - 1];

                    if (!IsEmptyName(name, sizeof(gListName[0]))) {
                        sprintf(scanlist_str, "%.3s%s", name, gEeprom.SCAN_LIST_ENABLED ? "+" : "");
                        scanlist_end = gEeprom.SCAN_LIST_ENABLED ? 18 : 14;
                    } 
                    else {
                        sprintf(scanlist_str, "%02d%s", gEeprom.SCAN_LIST_DEFAULT, gEeprom.SCAN_LIST_ENABLED ? "+" : "");
                        scanlist_end = gEeprom.SCAN_LIST_ENABLED ? 14 : 10;
                    }
                }
            }
            else {  // frequency mode
                memcpy(line + x + 1, gFontS, sizeof(gFontS));
            }
            x1 = x + 10;
        }
    }
    x += 10;

    #ifdef ENABLE_FEAT_F4HWN_DEBUG
        // Only for debug
        // Only for debug
        // Only for debug

        sprintf(str, "%d", gDebug);
        UI_PrintStringSmallBufferNormal(str, line + x + 1);
        x += 16;
    #else
        #ifdef ENABLE_VOICE
        // VOICE indicator
        if (gEeprom.VOICE_PROMPT != VOICE_PROMPT_OFF){
            memcpy(line + x, BITMAP_VoicePrompt, sizeof(BITMAP_VoicePrompt));
            x1 = x + sizeof(BITMAP_VoicePrompt);
        }
        x += sizeof(BITMAP_VoicePrompt);
        #endif

        if(!SCANNER_IsScanning()) {
        #ifdef ENABLE_FEAT_F4HWN_RX_TX_TIMER
            bool isTransmit = gCurrentFunction == FUNCTION_TRANSMIT;
            if (gSetting_set_tmr && (isTransmit || FUNCTION_IsRx())) {
                convertTime(line, !isTransmit);
            }
            else
        #endif
            {
                /*if(!gAirCopyBootMode) {
                    const void *src = NULL;    // Pointer to the font/bitmap to copy
                    size_t sSize = 0;          // Size of the font/bitmap
                    uint8_t sOff = 2;          // Offset relative to the reference position

                    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
                        if (gEeprom.MENU_LOCK) {
                            src = gFontRO;
                            sSize = sizeof(gFontRO);
                        } else 
                    #endif
                    {
                        uint8_t xb = (gEeprom.CROSS_BAND_RX_TX != CROSS_BAND_OFF);

                        if (gEeprom.DUAL_WATCH != DUAL_WATCH_OFF) {
                            if (gDualWatchActive) { // DWR - dual watch + respond
                                src = gFontDWR;
                                sOff = xb ? 2 : 0;
                                sSize = sizeof(gFontDWR);
                            } else {
                                src = gFontHold;
                                sOff = 3;
                                sSize = sizeof(gFontHold);
                            }
                        } else {
                            src   = xb ? gFontXB         : gFontMO;          // XB - crossband
                            sSize = xb ? sizeof(gFontXB) : sizeof(gFontMO);  // MO - main only
                        }
                    }

                    // Perform the memcpy if a source was selected
                    if (src) {
                        memcpy(line + x + sOff, src, sSize);
                    }
                }*/
            }
        }
        x += sizeof(gFontDWR) + 3;
    #endif

#ifdef ENABLE_VOX
    // VOX indicator
    if (gEeprom.VOX_SWITCH) {
        memcpy(line + x, gFontVox, sizeof(gFontVox));
        x1 = x + sizeof(gFontVox) + 1;
    }
    x += sizeof(gFontVox) + 3;
#endif

/*#ifdef ENABLE_FEAT_F4HWN
    // PTT indicator
    if(!gAirCopyBootMode) {
        if (gSetting_set_ptt_session) {
            memcpy(line + x, gFontPttOnePush, sizeof(gFontPttOnePush));
            x1 = x + sizeof(gFontPttOnePush) + 1;
        }
        else
        {
            memcpy(line + x, gFontPttClassic, sizeof(gFontPttClassic));
            x1 = x + sizeof(gFontPttClassic) + 1;       
        }
    }
    x += sizeof(gFontPttClassic) + 3;
#endif*/

#ifdef ENABLE_FEAT_F4HWN
    // FM indicator
    if (gFmRadioMode) {
        UI_PrintStringSmallBufferNormal("FM", line + x);
        x1 = x + 13;
        x += 16;
    }
#endif

    x = MAX(x1, 69u); 

    const void *src = NULL;   // Pointer to the font/bitmap to copy
    size_t size = 0;          // Size of the font/bitmap


    // Determine the source and size based on conditions
    if (gEeprom.KEY_LOCK) {
        src = gFontKeyLock;
        size = sizeof(gFontKeyLock);
    }
    else if (gWasFKeyPressed) {
        src = gFontF;
        size = sizeof(gFontF);
    }
    #ifdef ENABLE_FEAT_F4HWN
        else if (gMute) {
            src = gFontMute;
            size = sizeof(gFontMute);
        }
    #endif
    else if (gBackLight) {
        src = gFontLight;
        size = sizeof(gFontLight);
    }
    #ifdef ENABLE_FEAT_F4HWN_CHARGING_C
    else if (gChargingWithTypeC) {
        src = BITMAP_USB_C;
        size = sizeof(BITMAP_USB_C);
    }
    #endif

    // Perform the memcpy if a source was selected
    if (src) {
        memcpy(line + x + 1, src, size);
    }

    // Draw scanlist name to the left of the F icon
    if (gScanStateDir != SCAN_OFF || SCANNER_IsScanning()) {
        if (scanlist_str[0] != '\0') {
            uint8_t sl_width = strlen(scanlist_str) * 4;
            uint8_t sl_x = x + 1 - sl_width - 4;
            if (sl_x > 2) {
                GUI_DisplaySmallest(scanlist_str, sl_x, 1, true, true);
                uint8_t byte_start = (sl_x - 2) / 8;
                uint8_t byte_end = (sl_x + sl_width + 2) / 8;
                if (byte_end > 15) byte_end = 15;
                gStatusLine[byte_start] ^= 0x3E;
                for (uint8_t i = byte_start + 1; i < byte_end; i++)
                    gStatusLine[i] ^= 0x7F;
                gStatusLine[byte_end] ^= 0x3E;
            }
        }
    }

    // Battery voltage/percentage display
    unsigned int x2 = LCD_WIDTH;

    switch (gSetting_battery_text) {
        case 1:
        case 3:
        {
            const uint16_t v = (gBatteryVoltageAverage <= 999) ? gBatteryVoltageAverage : 999;
            sprintf(str, "%u.%02uV", v / 100, v % 100);
            UI_PrintStringSmallBufferNormal(str, line + 0);
            break;
        }
        default:
            break;
    }

    switch (gSetting_battery_text) {
        case 2:
        case 3:
            sprintf(str, "%02u%%", BATTERY_VoltsToPercent(gBatteryVoltageAverage));
            x2 -= (7 * strlen(str));
            UI_PrintStringSmallBufferNormal(str, line + x2);
            break;
        default:
            break;
    }

    // **************

    ST7565_BlitStatusLine();
}
