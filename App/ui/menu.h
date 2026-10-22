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

#ifndef UI_MENU_H
#define UI_MENU_H

#include <stdbool.h>
#include <stdint.h>

#include "audio.h"     // VOICE_ID_t
#include "settings.h"

typedef struct {
    const char  name[7];    // menu display area only has room for 6 characters
    uint8_t     menu_id;
} t_menu_item;

/* The settings menu browser's categories. The order is the order the browser
 * shows them in: the LBJ category sits second, right behind the channels.
 * MENU_CAT_ALL is not a category of its own but the whole MenuList in its own
 * order, and comes last. MENU_CAT_DTMF is the one category that stays hidden:
 * it needs the hidden menu, the PTT + upper side key gesture at power-on, to be
 * listed at all - the way every item past the first screenful used to. */
enum
{
    MENU_CAT_CHANNELS = 0,
    MENU_CAT_LBJ,
    MENU_CAT_SCAN,
    MENU_CAT_KEYS,
    MENU_CAT_POWER,
    MENU_CAT_DISPLAY,
    MENU_CAT_TIMERS,
    MENU_CAT_AUDIO,
    MENU_CAT_RADIO,
    MENU_CAT_DTMF,
    MENU_CAT_SERVICE,
    MENU_CAT_ALL,
    MENU_CAT_COUNT
};

enum
{
    MENU_SQL = 0,
    MENU_STEP,
    MENU_TXP,
    MENU_R_DCS,
    MENU_R_CTCS,
    MENU_T_DCS,
    MENU_T_CTCS,
    MENU_SFT_D,
    MENU_OFFSET,
    MENU_TOT,
    MENU_W_N,
#ifndef ENABLE_FEAT_F4HWN
    MENU_SCR,
#endif
    MENU_BCL,
#ifdef ENABLE_FEAT_F4HWN
    MENU_TX_LOCK, 
#endif
    MENU_MEM_CH,
    MENU_DEL_CH,
    MENU_MEM_NAME,
    MENU_MDF,
    MENU_SAVE,
    MENU_VOX,
    MENU_ABR,
    MENU_ABR_ON_TX_RX,
    MENU_ABR_MIN,
    MENU_ABR_MAX,
    MENU_TDR,
    MENU_BEEP,
#ifdef ENABLE_VOICE
    MENU_VOICE,
#endif
    MENU_SC_REV,
    MENU_AUTOLK,
    MENU_LIST_CH,
    MENU_STE,
    MENU_RP_STE,
    MENU_MIC,
    MENU_MIC_BAR,
    MENU_COMPAND,
    MENU_1_CALL,
    MENU_S_LIST,
    MENU_S_PRI,
    MENU_S_PRI_CH_1,
    MENU_S_PRI_CH_2,    
#ifdef ENABLE_ALARM
    MENU_AL_MOD,
#endif
#ifdef ENABLE_DTMF_CALLING
    MENU_ANI_ID,
#endif
    MENU_UPCODE,
    MENU_DWCODE,
    MENU_PTT_ID,
    MENU_D_ST,
#ifdef ENABLE_DTMF_CALLING
    MENU_D_RSP,
    MENU_D_HOLD,
#endif
    MENU_D_PRE,
#ifdef ENABLE_DTMF_CALLING  
    MENU_D_DCD,
    MENU_D_LIST,
#endif
    MENU_D_LIVE_DEC,
    MENU_PONMSG,
    MENU_ROGER,
    MENU_VOL,
    MENU_BAT_TXT,
    MENU_AM,
#ifdef ENABLE_AM_FIX
    MENU_AM_FIX,
#endif
#ifndef ENABLE_FEAT_F4HWN
    #ifdef ENABLE_NOAA
        MENU_NOAA_S,
    #endif
#endif
    MENU_RESET,
    MENU_F_LOCK,
#ifndef ENABLE_FEAT_F4HWN
    MENU_200TX,
    MENU_350TX,
    MENU_500TX,
#endif
    MENU_350EN,
#ifndef ENABLE_FEAT_F4HWN
    MENU_SCREN,
#endif
#ifdef ENABLE_F_CAL_MENU
    MENU_F_CALI,  // reference xtal calibration
#endif
#ifdef ENABLE_FEAT_F4HWN_SLEEP
    MENU_SET_OFF,
#endif
#ifdef ENABLE_FEAT_F4HWN
    MENU_SET_PWR,
    MENU_SET_PTT,
    MENU_SET_TOT,
    MENU_SET_EOT,
    MENU_SET_CTR,
    MENU_SET_INV,
    MENU_SET_LCK,
    MENU_SET_MET,
    MENU_SET_GUI,
    MENU_SET_TMR,
    #ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
        MENU_SET_SCN,
    #endif
    #ifdef ENABLE_FEAT_F4HWN_NARROWER
        MENU_SET_NFM,
    #endif
    #ifdef ENABLE_FEAT_F4HWN_VOL
        MENU_SET_VOL,
    #endif
    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
        MENU_SET_KEY,
    #endif
    #ifdef ENABLE_NOAA
        MENU_NOAA_S,
    #endif
    MENU_SET_NAV,
    #ifdef ENABLE_FEAT_F4HWN_AUDIO
        MENU_SET_AUD,
    #endif
#endif
    MENU_BATCAL,  // battery voltage calibration
    MENU_F1SHRT,
    MENU_F1LONG,
    MENU_F2SHRT,
    MENU_F2LONG,
    MENU_MLONG,
    MENU_BATTYP,
#ifdef ENABLE_FEAT_F4HWN_LBJ
    /* Not a setting: selecting it drops out of the menu and opens the decode
     * window on the main screen. Appended last so no existing id moves. */
    MENU_LBJ,
    /* How long the decode window the reports opened stays up on its own before
     * it closes itself: ON (as before), 3, 5, 10 or 60 s. The window the LBJ
     * menu item opens by hand is not affected - it stays up as it always did. */
    MENU_LBJPOP,
    /* LBJLOG: append one flash row per decoded train to the RX/TX log zone the
     * flash map reserves (0x018000), the log the export tools read back. */
    MENU_LBJLOG,
    /* Not a setting either: clears that log zone. It carries its own No/Yes
     * choice, so the second press is the confirmation. */
    MENU_LBJCLR,
#endif
#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
    /* Not a stored setting either: picks which settings bank the running
     * firmware loads (0 = Main, then one per user slot), then reboots. */
    MENU_SET_CFG,
#endif
#ifdef ENABLE_FEAT_F4HWN_LBJ
    /* Also an action, not a setting: the list of the reports stored in the flash
     * log (LBJHST). Appended after everything else so no existing id moves - the
     * order the menu shows is MenuList, not this enumeration. */
    MENU_LBJHST,
    /* The receiver position the popup measures a train from, edited right here
     * instead of in the LBJ Edit overlay app. LBJPRI picks which of the five
     * slots that is ("--" for none), LBJLAT and LBJLON hold its latitude (6
     * digits, DD MM hh) and longitude (7 digits, DDD MM hh). None of the three
     * is in gEeprom: the store is the firmware's own flash sector
     * (app/lbj_pos.c), byte for byte the record the deprecated LBJ Edit app
     * keeps in its own config area - which stays readable as a fallback. */
    MENU_LBJPRI,
    MENU_LBJLAT,
    MENU_LBJLON,
    /* LBJNUM: how row3 prints the 8-digit registration. FULL keeps all eight
     * digits (the old behaviour); SIMPLE prints four - the last four for a
     * locomotive, digits 4..7 for an EMU (model code 3xx). Appended last so no
     * existing id moves. */
    MENU_LBJNUM,
#ifdef ENABLE_CN_FONT
    /* LBJLANG: what the Chinese build prints - CHN, ENG, or ENLC (English, but
     * the route names stay Chinese). The English build has no such item and no
     * Chinese in it at all. Appended last again so no existing id moves. */
    MENU_LBJ_LANG,
#endif
#endif
};

extern const t_menu_item MenuList[];

extern const char        gSubMenu_TXP[8][6];
extern const char        gSubMenu_SFT_D[3][4];
extern const char        gSubMenu_W_N[2][7];
extern const char        gSubMenu_OFF_ON[2][4];
extern const char        gSubMenu_LBJ_POP[5][4];
extern const char        gSubMenu_LBJ_CLR[2][4];
extern const char        gSubMenu_LBJ_PRI[6][4];
extern const char        gSubMenu_LBJ_NUM[2][7];
#ifdef ENABLE_CN_FONT
extern const char        gSubMenu_LBJ_LANG[3][5];
#endif
extern const char        gSubMenu_NA[4];
extern const char        gSubMenu_TOT[11][7];
extern const char* const gSubMenu_RXMode[4];

#ifdef ENABLE_VOICE
    extern const char    gSubMenu_VOICE[3][4];
#endif
extern const char* const gSubMenu_MDF[4];
#ifdef ENABLE_ALARM
    extern const char    gSubMenu_AL_MOD[2][5];
#endif
#ifdef ENABLE_DTMF_CALLING
extern const char        gSubMenu_D_RSP[4][11];
#endif

#ifdef ENABLE_FEAT_F4HWN
    extern const char    gSubMenu_SET_PWR[7][6];
    extern const char    gSubMenu_SET_PTT[2][8];
    extern const char    gSubMenu_SET_TOT[4][7];
    extern const char    gSubMenu_SET_LCK[2][9];
    extern const char    gSubMenu_SET_MET[2][8];
    #ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
        extern const char    gSubMenu_SET_SCN[2][7];
    #endif
    #ifdef ENABLE_FEAT_F4HWN_NARROWER
        extern const char    gSubMenu_SET_NFM[2][9];
    #endif
    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
        extern const char gSubMenu_SET_KEY[][9];
    #endif
    #ifdef ENABLE_FEAT_F4HWN_AUDIO
        extern const char    gSubMenu_SET_AUD_FM[5][6];
        extern const char    gSubMenu_SET_AUD_AM[3][6];
    #endif
#endif

extern const char* const gSubMenu_PTT_ID[5];
#ifdef ENABLE_FEAT_F4HWN
    #ifdef ENABLE_FEAT_F4HWN_LOGO
        extern const char        gSubMenu_PONMSG[6][8];
    #else
        extern const char        gSubMenu_PONMSG[5][8];
    #endif
#else
    extern const char        gSubMenu_PONMSG[4][8];
#endif
extern const char        gSubMenu_ROGER[3][6];
extern const char        gSubMenu_RESET[2][4];
extern const char* const gSubMenu_F_LOCK[F_LOCK_LEN];
extern const char        gSubMenu_RX_TX[4][6];
extern const char        gSubMenu_BAT_TXT[4][12];
extern const char        gSubMenu_BATTYP[5][12];

#ifndef ENABLE_FEAT_F4HWN
    extern const char        gSubMenu_SCRAMBLER[11][7];
#endif

typedef struct {char* name; uint8_t id;} t_sidefunction;
extern const uint8_t         gSubMenu_SIDEFUNCTIONS_size;
extern const t_sidefunction gSubMenu_SIDEFUNCTIONS[];
                         
extern bool              gIsInSubMenu;
                         
extern uint8_t           gMenuCursor;

/* The browser: gMenuCatView says the category page is up rather than an item
 * list, gMenuCat is the category it has highlighted - and, with the page down,
 * the item list being shown. Where each category's list was left is remembered
 * for the session, not saved across a restart. */
extern bool              gMenuCatView;
extern uint8_t           gMenuCat;

extern int32_t           gSubMenuSelection;
                         
extern char              edit_original[17];
extern char              edit[17];
extern int               edit_index;
extern bool              edit_is_uppercase;

void UI_DisplayMenu(void);
int UI_MENU_GetCurrentMenuId();
uint8_t UI_MENU_GetMenuIdx(uint8_t id);

/* the browser: open the category page, open one category's item list, and move
 * the highlight from one category to the next (or previous) one */
void UI_MENU_EnterBrowser(void);
void UI_MENU_EnterCategory(uint8_t cat);
void UI_MENU_CatStep(int8_t dir);

#endif
