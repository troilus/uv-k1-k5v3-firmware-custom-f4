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
#include <stdlib.h>

#include "../app/dtmf.h"
#include "../app/menu.h"
#ifdef ENABLE_FEAT_F4HWN_LBJ
    #include "../app/lbj_pos.h"
#endif
#include "../bitmaps.h"
#include "../board.h"
#include "../dcs.h"
#include "../driver/backlight.h"
#include "../driver/bk4819.h"
#include "../driver/eeprom.h"
#include "../driver/st7565.h"
#include "../external/printf/printf.h"
#include "../font.h"
#include "../frequencies.h"
#include "../helper/battery.h"
#include "../misc.h"
#include "../settings.h"

#ifdef ENABLE_FEAT_F4HWN
    #include "../version.h"
#endif

#include "helper.h"
#include "inputbox.h"
#include "menu.h"
#include "ui.h"
#include "welcome.h"


/* The rows main00 already shows in Chinese. The Chinese build
 * (ENABLE_CN_FONT) prints them from menu_name() below, which follows LBJ_LANG;
 * every other row stays as main00 has it. The English build has none of it. */
const t_menu_item MenuList[] =
{
//   text,          menu ID
    {"SQL",         MENU_SQL           },
    {"Mode",        MENU_AM            }, // was "AM"
    {"Step",        MENU_STEP          },
    {"Power",       MENU_TXP           }, // was "TXP"
    {"RxDCS",       MENU_R_DCS         }, // was "R_DCS"
    {"RxCTCS",      MENU_R_CTCS        }, // was "R_CTCS"
    {"TxDCS",       MENU_T_DCS         }, // was "T_DCS"
    {"TxCTCS",      MENU_T_CTCS        }, // was "T_CTCS"
    {"TxDir",      MENU_SFT_D         }, // was "SFT_D"
    {"TxOffs",      MENU_OFFSET        }, // was "OFFSET"
    {"W/N",         MENU_W_N           },
    {"ChSave",      MENU_MEM_CH        }, // was "MEM-CH"
    {"ChDele",      MENU_DEL_CH        }, // was "DEL-CH"
    {"ChName",      MENU_MEM_NAME      },
    {"VOX",         MENU_VOX           },
#ifdef ENABLE_FEAT_F4HWN
    {"SysInf",    MENU_VOL           }, // was "VOL"
#else
    {"BatVol",    MENU_VOL           }, // was "VOL"
#endif
#ifdef ENABLE_FEAT_F4HWN_LBJ
    {"LBJMSG",      MENU_LBJ           }, // last visible item: opens the decode window
    {"LBJPOP",      MENU_LBJPOP        }, // how long that window stays up on its own
    {"LBJLOG",      MENU_LBJLOG        }, // append one flash row per decoded train
    {"LBJCLR",      MENU_LBJCLR        }, // clear that log (No/Yes)
    {"LBJHST",      MENU_LBJHST        }, // list the reports stored in that log
    {"LBJPRI",      MENU_LBJPRI        }, // which position slot the popup uses
    {"LBJLAT",      MENU_LBJLAT        }, // its latitude, DD MM hh
    {"LBJLON",      MENU_LBJLON        }, // its longitude, DDD MM hh
    {"LBJNUM",      MENU_LBJNUM        }, // registration: FULL 8 digits or SIMPLE 4
#ifdef ENABLE_CN_FONT
    {"LANG",        MENU_LBJ_LANG      }, // CN build only: CHN / ENG / ENLC
#endif
#endif
#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
    {"SetCfg",      MENU_SET_CFG       }, // pick the settings bank to boot with
#endif
    // The rows from here on used to be the hidden menu, the ones the PTT +
    // upper side key gesture at power-on revealed. The browser filters on the
    // table below instead, so every one of them is reachable - in the category
    // it belongs to. Only the DTMF category still waits for that gesture.
    {"F Lock",      MENU_F_LOCK        },
    {"RxMode",      MENU_TDR           },
#ifdef ENABLE_FEAT_F4HWN
    {"SetPwr",      MENU_SET_PWR       },
    {"SetPTT",      MENU_SET_PTT       },
    {"SetTOT",      MENU_SET_TOT       },
    {"SetEOT",      MENU_SET_EOT       },
    {"SetCtr",      MENU_SET_CTR       },
    {"SetInv",      MENU_SET_INV       },
    {"SetLck",      MENU_SET_LCK       },
    {"SetMet",      MENU_SET_MET       },
    {"SetGUI",      MENU_SET_GUI       },
#ifdef ENABLE_FEAT_F4HWN_AUDIO    
    {"SetRxA",      MENU_SET_AUD       },
#endif
    {"SetTmr",      MENU_SET_TMR       },
#ifdef ENABLE_FEAT_F4HWN_SLEEP
    {"SetOff",       MENU_SET_OFF      },
#endif
#ifdef ENABLE_FEAT_F4HWN_NARROWER
    {"SetNFM",      MENU_SET_NFM       },
#endif
#ifdef ENABLE_FEAT_F4HWN_VOL
    {"SetVol",      MENU_SET_VOL       },
#endif
#ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
    {"SetKey",      MENU_SET_KEY       },
#endif
#ifdef ENABLE_NOAA
    {"SetNWR",      MENU_NOAA_S    },
#endif
#endif

#ifndef ENABLE_FEAT_F4HWN
    {"Scramb",      MENU_SCR           }, // was "SCR"
#endif
    {"BzLock",      MENU_BCL           }, // was "BCL"
    {"Compnd",      MENU_COMPAND       },
    {"ChList",      MENU_LIST_CH       },
    {"ScList",       MENU_S_LIST       },
    {"ScPri",        MENU_S_PRI        },
    {"PriCh1",       MENU_S_PRI_CH_1   },
    {"PriCh2",       MENU_S_PRI_CH_2   },
    {"ScnRev",      MENU_SC_REV        },
#ifndef ENABLE_FEAT_F4HWN
    #ifdef ENABLE_NOAA
        {"NOAA-S",      MENU_NOAA_S    },
    #endif
#endif
    {"F1Shrt",      MENU_F1SHRT        },
    {"F1Long",      MENU_F1LONG        },
    {"F2Shrt",      MENU_F2SHRT        },
    {"F2Long",      MENU_F2LONG        },
    {"M Long",      MENU_MLONG         },

    {"KeyLck",      MENU_AUTOLK        }, // was "AUTOLk"
    {"TxTOut",      MENU_TOT           }, // was "TOT"
    {"BatSav",      MENU_SAVE          }, // was "SAVE"
    {"BatTxt",      MENU_BAT_TXT       },
    {"Mic",         MENU_MIC           },
    {"MicBar",      MENU_MIC_BAR       },
    {"ChDisp",      MENU_MDF           }, // was "MDF"
    {"POnMsg",      MENU_PONMSG        },
    {"BLTime",      MENU_ABR           }, // was "ABR"
    {"BLMin",       MENU_ABR_MIN       },
    {"BLMax",       MENU_ABR_MAX       },
    {"BLTxRx",      MENU_ABR_ON_TX_RX  },
    {"Beep",        MENU_BEEP          },
#ifdef ENABLE_VOICE
    {"Voice",       MENU_VOICE         },
#endif
    {"Roger",       MENU_ROGER         },
    {"STE",         MENU_STE           },
    {"RP STE",      MENU_RP_STE        },
    {"1 Call",      MENU_1_CALL        },
#ifdef ENABLE_ALARM
    {"AlarmT",      MENU_AL_MOD        },
#endif
#ifdef ENABLE_DTMF_CALLING
    {"ANI ID",      MENU_ANI_ID        },
#endif
    {"UPCode",      MENU_UPCODE        },
    {"DWCode",      MENU_DWCODE        },
    {"PTT ID",      MENU_PTT_ID        },
    {"D ST",        MENU_D_ST          },
#ifdef ENABLE_DTMF_CALLING
    {"D Resp",      MENU_D_RSP         },
    {"D Hold",      MENU_D_HOLD        },
#endif
    {"D Prel",      MENU_D_PRE         },
#ifdef ENABLE_DTMF_CALLING
    {"D Decd",      MENU_D_DCD         },
    {"D List",      MENU_D_LIST        },
#endif
    {"D Live",      MENU_D_LIVE_DEC    }, // live DTMF decoder
#ifndef ENABLE_FEAT_F4HWN
    #ifdef ENABLE_AM_FIX
        {"AM Fix",      MENU_AM_FIX        },
    #endif
#endif

#ifdef ENABLE_F_CAL_MENU
    {"FrCali",      MENU_F_CALI        }, // reference xtal calibration
#endif
    {"350 En",      MENU_350EN         }, // was "350EN"
#ifndef ENABLE_FEAT_F4HWN
    {"ScraEn",      MENU_SCREN         }, // was "SCREN"
#endif
#ifndef ENABLE_FEAT_F4HWN
    {"Tx 200",      MENU_200TX         }, // was "200TX"
    {"Tx 350",      MENU_350TX         }, // was "350TX"
    {"Tx 500",      MENU_500TX         }, // was "500TX"
#endif
#ifdef ENABLE_FEAT_F4HWN
    {"TXLock",      MENU_TX_LOCK       }, 
#endif


    {"BatCal",      MENU_BATCAL        }, // battery voltage calibration
    {"BatTyp",      MENU_BATTYP        }, // battery type 1600/2200mAh
    {"SetNav",      MENU_SET_NAV       }, // set navigation (LEFT / RIGHT or UP / DOWN)
    {"Reset",       MENU_RESET         }, // might be better to move this to the hidden menu items ?

    {"",                              0xff               }  // end of list - DO NOT delete or move this this
};

/* Which category each menu id belongs to - the table the browser (UI_DisplayMenu
 * below) and the key handling (App/app/menu.c MENU_Key_*) run on. It is indexed
 * by the MENU_* id instead of carried in MenuList so that a menu row never has
 * to change for a category to move, and so the table is the same on every build:
 * the ids a build leaves out sit behind the same #ifdefs the menu id enum uses.
 * An id that is not listed here lands in the first category. */
static const uint8_t gMenuCatOf[] =
{
    [MENU_SQL]              = MENU_CAT_RADIO,
    [MENU_AM]               = MENU_CAT_CHANNELS,
    [MENU_STEP]             = MENU_CAT_CHANNELS,
    [MENU_TXP]              = MENU_CAT_CHANNELS,
    [MENU_R_DCS]            = MENU_CAT_CHANNELS,
    [MENU_R_CTCS]           = MENU_CAT_CHANNELS,
    [MENU_T_DCS]            = MENU_CAT_CHANNELS,
    [MENU_T_CTCS]           = MENU_CAT_CHANNELS,
    [MENU_SFT_D]            = MENU_CAT_CHANNELS,
    [MENU_OFFSET]           = MENU_CAT_CHANNELS,
    [MENU_TOT]              = MENU_CAT_TIMERS,
    [MENU_W_N]              = MENU_CAT_CHANNELS,
#ifndef ENABLE_FEAT_F4HWN
    [MENU_SCR]              = MENU_CAT_RADIO,
#endif
    [MENU_BCL]              = MENU_CAT_CHANNELS,
#ifdef ENABLE_FEAT_F4HWN
    [MENU_TX_LOCK]          = MENU_CAT_CHANNELS,
#endif
    [MENU_MEM_CH]           = MENU_CAT_CHANNELS,
    [MENU_DEL_CH]           = MENU_CAT_CHANNELS,
    [MENU_MEM_NAME]         = MENU_CAT_CHANNELS,
    [MENU_MDF]              = MENU_CAT_DISPLAY,
    [MENU_SAVE]             = MENU_CAT_POWER,
    [MENU_VOX]              = MENU_CAT_RADIO,
    [MENU_ABR]              = MENU_CAT_DISPLAY,
    [MENU_ABR_ON_TX_RX]     = MENU_CAT_DISPLAY,
    [MENU_ABR_MIN]          = MENU_CAT_DISPLAY,
    [MENU_ABR_MAX]          = MENU_CAT_DISPLAY,
    [MENU_TDR]              = MENU_CAT_RADIO,
    [MENU_BEEP]             = MENU_CAT_AUDIO,
#ifdef ENABLE_VOICE
    [MENU_VOICE]            = MENU_CAT_AUDIO,
#endif
    [MENU_SC_REV]           = MENU_CAT_SCAN,
    [MENU_AUTOLK]           = MENU_CAT_KEYS,
    [MENU_LIST_CH]          = MENU_CAT_SCAN,
    [MENU_STE]              = MENU_CAT_RADIO,
    [MENU_RP_STE]           = MENU_CAT_RADIO,
    [MENU_MIC]              = MENU_CAT_AUDIO,
    [MENU_MIC_BAR]          = MENU_CAT_AUDIO,
    [MENU_COMPAND]          = MENU_CAT_CHANNELS,
    [MENU_1_CALL]           = MENU_CAT_KEYS,
    [MENU_S_LIST]           = MENU_CAT_SCAN,
    [MENU_S_PRI]            = MENU_CAT_SCAN,
    [MENU_S_PRI_CH_1]       = MENU_CAT_SCAN,
    [MENU_S_PRI_CH_2]       = MENU_CAT_SCAN,
#ifdef ENABLE_ALARM
    [MENU_AL_MOD]           = MENU_CAT_RADIO,
#endif
#ifdef ENABLE_DTMF_CALLING
    [MENU_ANI_ID]           = MENU_CAT_DTMF,
#endif
    [MENU_UPCODE]           = MENU_CAT_DTMF,
    [MENU_DWCODE]           = MENU_CAT_DTMF,
    [MENU_PTT_ID]           = MENU_CAT_DTMF,
    [MENU_D_ST]             = MENU_CAT_DTMF,
#ifdef ENABLE_DTMF_CALLING
    [MENU_D_RSP]            = MENU_CAT_DTMF,
    [MENU_D_HOLD]           = MENU_CAT_DTMF,
#endif
    [MENU_D_PRE]            = MENU_CAT_DTMF,
#ifdef ENABLE_DTMF_CALLING
    [MENU_D_DCD]            = MENU_CAT_DTMF,
    [MENU_D_LIST]           = MENU_CAT_DTMF,
#endif
    [MENU_D_LIVE_DEC]       = MENU_CAT_DTMF,
    [MENU_PONMSG]           = MENU_CAT_DISPLAY,
    [MENU_ROGER]            = MENU_CAT_RADIO,
    [MENU_VOL]              = MENU_CAT_DISPLAY,
    [MENU_BAT_TXT]          = MENU_CAT_POWER,
#ifdef ENABLE_AM_FIX
    [MENU_AM_FIX]           = MENU_CAT_RADIO,
#endif
#ifdef ENABLE_NOAA
    [MENU_NOAA_S]           = MENU_CAT_RADIO,
#endif
    [MENU_RESET]            = MENU_CAT_SERVICE,
    [MENU_F_LOCK]           = MENU_CAT_SERVICE,
#ifndef ENABLE_FEAT_F4HWN
    [MENU_200TX]            = MENU_CAT_SERVICE,
    [MENU_350TX]            = MENU_CAT_SERVICE,
    [MENU_500TX]            = MENU_CAT_SERVICE,
#endif
    [MENU_350EN]            = MENU_CAT_SERVICE,
#ifndef ENABLE_FEAT_F4HWN
    [MENU_SCREN]            = MENU_CAT_SERVICE,
#endif
#ifdef ENABLE_F_CAL_MENU
    [MENU_F_CALI]           = MENU_CAT_SERVICE,
#endif
#ifdef ENABLE_FEAT_F4HWN_SLEEP
    [MENU_SET_OFF]          = MENU_CAT_POWER,
#endif
#ifdef ENABLE_FEAT_F4HWN
    [MENU_SET_PWR]          = MENU_CAT_CHANNELS,
    [MENU_SET_PTT]          = MENU_CAT_KEYS,
    [MENU_SET_TOT]          = MENU_CAT_TIMERS,
    [MENU_SET_EOT]          = MENU_CAT_TIMERS,
    [MENU_SET_CTR]          = MENU_CAT_DISPLAY,
    [MENU_SET_INV]          = MENU_CAT_DISPLAY,
    [MENU_SET_LCK]          = MENU_CAT_KEYS,
    [MENU_SET_MET]          = MENU_CAT_DISPLAY,
    [MENU_SET_GUI]          = MENU_CAT_DISPLAY,
    [MENU_SET_TMR]          = MENU_CAT_TIMERS,
    #ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
        [MENU_SET_SCN]      = MENU_CAT_SCAN,
    #endif
    #ifdef ENABLE_FEAT_F4HWN_NARROWER
        [MENU_SET_NFM]      = MENU_CAT_CHANNELS,
    #endif
    #ifdef ENABLE_FEAT_F4HWN_VOL
        [MENU_SET_VOL]      = MENU_CAT_AUDIO,
    #endif
    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
        [MENU_SET_KEY]      = MENU_CAT_KEYS,
    #endif
    [MENU_SET_NAV]          = MENU_CAT_SERVICE,
    #ifdef ENABLE_FEAT_F4HWN_AUDIO
        [MENU_SET_AUD]      = MENU_CAT_AUDIO,
    #endif
#endif
    [MENU_BATCAL]           = MENU_CAT_SERVICE,
    [MENU_F1SHRT]           = MENU_CAT_KEYS,
    [MENU_F1LONG]           = MENU_CAT_KEYS,
    [MENU_F2SHRT]           = MENU_CAT_KEYS,
    [MENU_F2LONG]           = MENU_CAT_KEYS,
    [MENU_MLONG]            = MENU_CAT_KEYS,
    [MENU_BATTYP]           = MENU_CAT_SERVICE,
#ifdef ENABLE_FEAT_F4HWN_LBJ
    [MENU_LBJ]              = MENU_CAT_LBJ,
    [MENU_LBJPOP]           = MENU_CAT_LBJ,
    [MENU_LBJLOG]           = MENU_CAT_LBJ,
    [MENU_LBJCLR]           = MENU_CAT_LBJ,
#endif
#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
    [MENU_SET_CFG]          = MENU_CAT_CHANNELS,
#endif
#ifdef ENABLE_FEAT_F4HWN_LBJ
    [MENU_LBJHST]           = MENU_CAT_LBJ,
    [MENU_LBJPRI]           = MENU_CAT_LBJ,
    [MENU_LBJLAT]           = MENU_CAT_LBJ,
    [MENU_LBJLON]           = MENU_CAT_LBJ,
    [MENU_LBJNUM]           = MENU_CAT_LBJ,
#ifdef ENABLE_CN_FONT
    [MENU_LBJ_LANG]         = MENU_CAT_LBJ,
#endif
#endif
};

/* The names the browser shows the categories by, in the MENU_CAT_* order above.
 * The hidden one is named here all the same, it is simply never picked. */
static const char *const gMenuCatName[MENU_CAT_COUNT] =
{
    "Channels", /* MENU_CAT_CHANNELS */
    "LBJ",      /* MENU_CAT_LBJ */
    "Scan",     /* MENU_CAT_SCAN */
    "Keys",     /* MENU_CAT_KEYS */
    "Power",    /* MENU_CAT_POWER */
    "Display",  /* MENU_CAT_DISPLAY */
    "Timers",   /* MENU_CAT_TIMERS */
    "Audio",    /* MENU_CAT_AUDIO */
    "Radio",    /* MENU_CAT_RADIO */
    "DTMF",     /* MENU_CAT_DTMF */
    "Service",  /* MENU_CAT_SERVICE */
    "All"       /* MENU_CAT_ALL */
};

bool    gMenuCatView = false;              /* the category page is up */
uint8_t gMenuCat = MENU_CAT_CHANNELS;      /* category highlighted / list shown */

static uint8_t gMenuCatCursor[MENU_CAT_COUNT];   /* where each list was left */

/* DTMF is the last category the hidden menu still gates: without the PTT +
 * upper side key gesture at power-on it is not listed, and its items are not in
 * All either, the way every item past the first screenful used to be. */
static bool cat_visible(uint8_t cat)
{
    if (cat == MENU_CAT_DTMF && !gF_LOCK)
        return false;

    return cat < MENU_CAT_COUNT;
}

static uint8_t cat_of(const t_menu_item *item)
{
    const uint8_t id = item->menu_id;

    return (id < ARRAY_SIZE(gMenuCatOf)) ? gMenuCatOf[id] : MENU_CAT_CHANNELS;
}

/* Does a category hold this item: All holds every item the user is allowed to
 * see, any other one only the items that belong to it. */
static bool item_in_cat(const t_menu_item *item, uint8_t cat)
{
    const uint8_t c = cat_of(item);

    if (!cat_visible(c))
        return false;

    return (cat == MENU_CAT_ALL) || (c == cat);
}

/* The idx-th item of a category (idx is 0 based), NULL when there is none. */
static const t_menu_item *cat_item(uint8_t cat, uint8_t idx)
{
    for (uint8_t i = 0; MenuList[i].name[0] != '\0'; i++)
    {
        if (!item_in_cat(&MenuList[i], cat))
            continue;

        if (idx == 0)
            return &MenuList[i];

        idx--;
    }

    return NULL;
}

/* The row the list draws at a position: the empty row at the end of MenuList
 * when the position is past it, so the drawing code can print it as it is. */
static const t_menu_item *view_item(int idx)
{
    const t_menu_item *item = (idx >= 0) ? cat_item(gMenuCat, (uint8_t)idx) : NULL;

    return item ? item : &MenuList[ARRAY_SIZE(MenuList) - 1];
}

/* The row the rest of the firmware takes as "the current menu item". With the
 * category page up there is none, so it is the one the browser would open. */
static uint8_t list_cursor(void)
{
    if (gMenuCatView && gMenuCat < MENU_CAT_COUNT)
        return gMenuCatCursor[gMenuCat];

    return gMenuCursor;
}

/* What a row of the browser prints. The Chinese build shows the words main00
 * already uses while LBJ_LANG is CHN; ENG and ENLC keep the English names the
 * table carries. The English build has no Chinese in it at all, so there the
 * name is simply the table's. */
#ifdef ENABLE_CN_FONT
static const char *menu_name(const t_menu_item *item)
{
    if (gSetting_LBJ_lang != LBJ_LANG_CHN)
        return item->name;

    switch (item->menu_id)
    {
        case MENU_AM:       return "模式";
        case MENU_STEP:     return "步进";
        case MENU_TXP:      return "功率";
        case MENU_W_N:      return "宽窄带";
        case MENU_MEM_CH:   return "存信道";
        case MENU_DEL_CH:   return "删信道";
        case MENU_MEM_NAME: return "信道名";
#ifdef ENABLE_FEAT_F4HWN
        case MENU_VOL:      return "信息";
#else
        case MENU_VOL:      return "电压";
#endif
        case MENU_F_LOCK:   return "F锁";
        case MENU_SET_INV:  return "反色";
        case MENU_LBJ_LANG: return "语言";
        default:            return item->name;
    }
}
#else
#define menu_name(item) ((item)->name)
#endif

static const char *cat_name(uint8_t cat)
{
    if (cat >= MENU_CAT_COUNT)
        return "";

#ifdef ENABLE_CN_FONT
    if (gSetting_LBJ_lang == LBJ_LANG_CHN)
    {
        switch (cat)
        {
            case MENU_CAT_CHANNELS: return "信道";
            case MENU_CAT_LBJ:      return "LBJ";
            case MENU_CAT_SCAN:     return "扫描";
            case MENU_CAT_KEYS:     return "按键";
            case MENU_CAT_POWER:    return "电源";
            case MENU_CAT_DISPLAY:  return "显示";
            case MENU_CAT_TIMERS:   return "定时";
            case MENU_CAT_AUDIO:    return "音频";
            case MENU_CAT_RADIO:    return "电台";
            case MENU_CAT_DTMF:     return "DTMF";
            case MENU_CAT_SERVICE:  return "服务";
            case MENU_CAT_ALL:      return "全部";
            default:                return "";
        }
    }
#endif

    return gMenuCatName[cat];
}

static uint8_t cat_item_count(uint8_t cat)
{
    uint8_t n = 0;

    for (uint8_t i = 0; MenuList[i].name[0] != '\0'; i++)
        if (item_in_cat(&MenuList[i], cat))
            n++;

    return n;
}

/* The category next to this one in the browser, wrapping around. */
static uint8_t cat_neighbour(uint8_t cat, int8_t dir)
{
    do
    {
        if (dir >= 0)
            cat = (uint8_t)((cat + 1) % MENU_CAT_COUNT);
        else
            cat = (cat == 0) ? (MENU_CAT_COUNT - 1) : (uint8_t)(cat - 1);
    } while (!cat_visible(cat));

    return cat;
}

/* Which category of how many is showing, for the footer. */
static uint8_t cat_number(uint8_t cat)
{
    uint8_t n = 0;

    for (uint8_t c = 0; c <= cat && c < MENU_CAT_COUNT; c++)
        if (cat_visible(c))
            n++;

    return n;
}

static uint8_t cat_visible_count(void)
{
    uint8_t n = 0;

    for (uint8_t c = 0; c < MENU_CAT_COUNT; c++)
        if (cat_visible(c))
            n++;

    return n;
}

void UI_MENU_CatStep(int8_t dir)
{
    gMenuCat = cat_neighbour(gMenuCat, dir);
}

/* Show one category's item list, MENU_CAT_ALL being the whole flat list. */
void UI_MENU_EnterCategory(uint8_t cat)
{
    const uint8_t count = cat_item_count(cat);

    if (!gMenuCatView && gMenuCat < MENU_CAT_COUNT)
        gMenuCatCursor[gMenuCat] = gMenuCursor;   /* remember the list we leave */

    gMenuCat       = cat;
    gMenuCatView   = false;
    gMenuListCount = count;
    gMenuCursor    = (gMenuCatCursor[cat] < count) ? gMenuCatCursor[cat] : 0;
}

/* Show the category page, leaving the item list where it was. */
void UI_MENU_EnterBrowser(void)
{
    if (!gMenuCatView && gMenuCat < MENU_CAT_COUNT)
        gMenuCatCursor[gMenuCat] = gMenuCursor;

    if (!cat_visible(gMenuCat))
        gMenuCat = MENU_CAT_CHANNELS;

    gMenuCatView   = true;
    gMenuListCount = cat_item_count(MENU_CAT_ALL);
}

/* Small text against the right edge of a page, for the item count. */
static void UI_MENU_PrintSmallRight(const char *text, const uint8_t line)
{
    const unsigned int pitch = ARRAY_SIZE(gFontSmall[0]) + 1u;
    const unsigned int width = strlen(text) * pitch;
    const unsigned int x     = (width + 2u < LCD_WIDTH) ? (LCD_WIDTH - 2u - width) : 2u;
    const uint8_t      row   = (line < FRAME_LINES) ? line : (FRAME_LINES - 1u);

    UI_PrintStringSmallNormal(text, (uint8_t)x, (uint8_t)x, row);
}

/* The browser page. The neighbours sit on the row above the highlighted name -
 * the category before it on the left behind a "<", the one after it on the
 * right ahead of a ">" - and the highlighted one stays on the row the item list
 * highlights, centred and inverted, so opening a category does not move it.
 * Under its name is how many items it holds, and the bottom right corner says
 * which category of how many. The neighbours are set in the same font the names
 * are, so a Chinese name reads here as it does in the list; only the longer
 * English names are wide enough to run out of screen, and then the arrows go
 * first and the right hand name after them. */
static void UI_MENU_DrawCategoryPage(void)
{
    const char        *name  = cat_name(gMenuCat);
    const char        *prev  = cat_name(cat_neighbour(gMenuCat, -1));
    const char        *next  = cat_name(cat_neighbour(gMenuCat, +1));
    const unsigned int w_cur = strlen(name) * 8u;
    const unsigned int x_cur = (w_cur < LCD_WIDTH) ? ((LCD_WIDTH - w_cur) / 2u) : 0u;
    unsigned int       w_prev = strlen(prev) * 8u;
    unsigned int       w_next = strlen(next) * 8u;
    unsigned int       arrow  = 8u;                 /* the "<" and ">" columns */
    char               String[16];
    unsigned int       i;

    if (w_prev + arrow + w_next + arrow > LCD_WIDTH)
    {
        arrow = 0u;
        if (w_prev + w_next > LCD_WIDTH)
            w_next = 0u;                            /* cannot have both names */
    }

    if (arrow)
        UI_PrintString("<", 0u, 0u, 0u, 8);

    if (w_prev)
        UI_PrintString(prev, (uint8_t)arrow, (uint8_t)arrow, 0u, 8);

    if (w_next)
    {
        const uint8_t x_next = (uint8_t)(LCD_WIDTH - w_next - arrow);
        UI_PrintString(next, x_next, x_next, 0u, 8);
    }

    if (arrow)
        UI_PrintString(">", (uint8_t)(LCD_WIDTH - arrow), (uint8_t)(LCD_WIDTH - arrow), 0u, 8);

    UI_PrintString(name, (uint8_t)x_cur, (uint8_t)x_cur, 2, 8);

    /* invert the highlighted category, as wide as its name is */
    for (i = 0; i < w_cur && (x_cur + i) < LCD_WIDTH; i++)
    {
        gFrameBuffer[2][x_cur + i] ^= 0xFF;
        gFrameBuffer[3][x_cur + i] ^= 0xFF;
    }

    /* how many items it holds, centred under its name */
    sprintf(String, "%u", cat_item_count(gMenuCat));
    {
        const unsigned int w_items = strlen(String) * 8u;
        const unsigned int x_items = (x_cur + (w_cur / 2u) >= (w_items / 2u))
                                   ? (x_cur + (w_cur / 2u) - (w_items / 2u)) : 0u;
        UI_PrintString(String, (uint8_t)x_items, (uint8_t)x_items, 4, 8);
    }

    /* which category of how many, in the bottom right corner */
    sprintf(String, "%u/%u", cat_number(gMenuCat), cat_visible_count());
    UI_MENU_PrintSmallRight(String, FRAME_LINES - 1u);
}

const char gSubMenu_TXP[][6] =
{
    "USER",
    "LOW 1",
    "LOW 2",
    "LOW 3",
    "LOW 4",
    "LOW 5",
    "MID",
    "HIGH"
};

const char gSubMenu_SFT_D[][4] =
{
    "OFF",
    "+",
    "-"
};

const char gSubMenu_W_N[][7] =
{
    "WIDE",
    "NARROW"
};

const char gSubMenu_OFF_ON[][4] =
{
    "OFF",
    "ON"
};

#ifdef ENABLE_FEAT_F4HWN_LBJ
/* The LBJPOP choices, in menu order. 0 is ON: the decode window the reports
 * opened stays up exactly as it always did (until EXIT or PTT); the rest close
 * it that many seconds after the last report or key press. */
const char gSubMenu_LBJ_POP[][4] =
{
    "ON",
    "3s",
    "5s",
    "10s",
    "60s"
};

/* The LBJCLR confirmation, in menu order. It starts on No, so a stray EXIT
 * never wipes the log. */
const char gSubMenu_LBJ_CLR[][4] =
{
    "No",
    "Yes"
};

/* LBJPRI: which of the five positions the decode window measures a train from.
 * Entry 0 is "--", no primary at all, so the popup keeps its old distance
 * source; 1..5 pick the store's slot 0..4 (lbj_pos.c). It starts on whatever the
 * store already holds. */
const char gSubMenu_LBJ_PRI[][4] =
{
    "--",
    "1",
    "2",
    "3",
    "4",
    "5"
};

/* LBJNUM: how row3 prints the 8-digit registration. FULL keeps the whole
 * model+number code, the way it always did; SIMPLE keeps only four digits -
 * the last four for a locomotive, digits 4..7 for an EMU (model code 3xx). */
const char gSubMenu_LBJ_NUM[][7] =
{
    "FULL",
    "SIMPLE"
};

#ifdef ENABLE_CN_FONT
/* LBJLANG: what the Chinese build prints. CHN is the way it always printed;
 * ENG turns everything English, the route names included (they become their
 * pinyin initials); ENLC keeps the route names Chinese and everything else
 * English. */
const char gSubMenu_LBJ_LANG[][5] =
{
    "CHN",
    "ENG",
    "ENLC"
};
#endif
#endif

const char gSubMenu_NA[4] =
{
    "N/A"
};

const char* const gSubMenu_RXMode[] =
{
    "MAIN\nONLY",       // TX and RX on main only
    "DUAL RX\nRESPOND", // Watch both and respond
    "CROSS\nBAND",      // TX on main, RX on secondary
    "MAIN TX\nDUAL RX"  // always TX on main, but RX on both
};

#ifdef ENABLE_VOICE
    const char gSubMenu_VOICE[][4] =
    {
        "OFF",
        "CHI",
        "ENG"
    };
#endif

const char* const gSubMenu_MDF[] =
{
    "FREQ",
    "CHANNEL\nNUMBER",
    "NAME",
    "NAME\n+\nFREQ"
};

#ifdef ENABLE_ALARM
    const char gSubMenu_AL_MOD[][5] =
    {
        "SITE",
        "TONE"
    };
#endif

#ifdef ENABLE_DTMF_CALLING
const char gSubMenu_D_RSP[][11] =
{
    "DO\nNOTHING",
    "RING",
    "REPLY",
    "BOTH"
};
#endif

const char* const gSubMenu_PTT_ID[] =
{
    "OFF",
    "UP CODE",
    "DOWN CODE",
    "UP+DOWN\nCODE",
    "APOLLO\nQUINDAR"
};

const char gSubMenu_PONMSG[][8] =
{
#ifdef ENABLE_FEAT_F4HWN
    "ALL",
    "SOUND",
#else
    "FULL",
#endif
    "MESSAGE",
    "VOLTAGE",
#ifdef ENABLE_FEAT_F4HWN_LOGO
    "LOGO",
#endif
    "NONE"
};

const char gSubMenu_ROGER[][6] =
{
    "OFF",
    "ROGER",
    "DIDI"
};

const char gSubMenu_RESET[][4] =
{
    "VFO",
    "ALL"
};

const char * const gSubMenu_F_LOCK[] =
{
    "DEFAULT+\n137-174\n400-470",
    "FCC HAM\n144-148\n420-450",
#ifdef ENABLE_FEAT_F4HWN_CA
    "CA HAM\n144-148\n430-450",
#endif
    "CE HAM\n144-146\n430-440",
    "GB HAM\n144-148\n430-440",
    "137-174\n400-430",
    "137-174\n400-438",
#ifdef ENABLE_FEAT_F4HWN_PMR
    "PMR 446",
#endif
#ifdef ENABLE_FEAT_F4HWN_GMRS_FRS_MURS
    "GMRS\nFRS\nMURS",
#endif
#ifdef ENABLE_FEAT_F4HWN_CN
    "CN\n409-410\n144-148\n430-440",
#endif
    "DISABLE\nALL",
    "UNLOCK\nALL",
};

const char gSubMenu_RX_TX[][6] =
{
    "OFF",
    "TX",
    "RX",
    "TX/RX"
};

const char gSubMenu_BAT_TXT[][12] =
{
    "NONE",
    "VOLTAGE",
    "PERCENT",
    "BOTH"
};

/* What the battery-type row prints. The Chinese build follows LBJ_LANG the way
 * the menu names do; the English build has no other table. */
static const char *bat_text(int32_t sel)
{
#ifdef ENABLE_CN_FONT
    static const char *const cn[4] = {"无", "电压", "百分比", "BOTH"};

    if (gSetting_LBJ_lang == LBJ_LANG_CHN && sel >= 0 && sel < 4)
        return cn[sel];
#endif

    return gSubMenu_BAT_TXT[sel];
}

const char gSubMenu_BATTYP[][12] =
{
    "1600mAh K5",
    "2200mAh K5",
    "3500mAh K5",
    "1400mAh K1",
    "2500mAh K1"
};

const char gSubMenu_SET_NAV[][17] =
{
    "LEFT\nRIGHT\nUV-K1",
    "UP\nDOWN\nUV-K5(8)",
};

#ifndef ENABLE_FEAT_F4HWN
const char gSubMenu_SCRAMBLER[][7] =
{
    "OFF",
    "2600Hz",
    "2700Hz",
    "2800Hz",
    "2900Hz",
    "3000Hz",
    "3100Hz",
    "3200Hz",
    "3300Hz",
    "3400Hz",
    "3500Hz"
};
#endif

#ifdef ENABLE_FEAT_F4HWN
    const char gSubMenu_SET_PWR[][6] =
    {
        "< 20m",
        "125m",
        "250m",
        "500m",
        "1",
        "2",
        "5"
    };

    const char gSubMenu_SET_PTT[][8] =
    {
        "CLASSIC",
        "ONEPUSH"
    };

    const char gSubMenu_SET_TOT[][7] =  // Use by SET_EOT too
    {
        "OFF",
        "SOUND",
        "VISUAL",
        "ALL"
    };

    const char gSubMenu_SET_LCK[][9] =
    {
        "KEYS",
        "KEYS+PTT"
    };

    const char gSubMenu_SET_MET[][8] =
    {
        "TINY",
        "CLASSIC"
    };

    #ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
        const char gSubMenu_SET_SCN[][7] =
        {
            "NORMAL",
            "FAST"
        };
    #endif

    #ifdef ENABLE_FEAT_F4HWN_AUDIO
        const char gSubMenu_SET_AUD_FM[][6] =
        {
            "FLAT",
            "CLEAN",
            "MID",
            "BOOST",
            "MAX"
        };

        const char gSubMenu_SET_AUD_AM[][6] =
        {
            "SHARP",
            "STOCK",
            "OPEN"
        };
    #endif

    #ifdef ENABLE_FEAT_F4HWN_NARROWER
        const char gSubMenu_SET_NFM[][9] =
        {
            "NARROW",
            "NARROWER"
        };
    #endif

    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
        const char gSubMenu_SET_KEY[][9] =
        {
            "KEY_MENU",
            "KEY_UP",
            "KEY_DOWN",
            "KEY_EXIT",
            "KEY_STAR"
        };
    #endif
#endif

const t_sidefunction gSubMenu_SIDEFUNCTIONS[] =
{
    {"NONE",            ACTION_OPT_NONE},
#ifdef ENABLE_FLASHLIGHT
    {"FLASH\nLIGHT",    ACTION_OPT_FLASHLIGHT},
#endif
    {"POWER",           ACTION_OPT_POWER},
    {"MONITOR",         ACTION_OPT_MONITOR},
    {"SCAN",            ACTION_OPT_SCAN},
#ifdef ENABLE_VOX
    {"VOX",             ACTION_OPT_VOX},
#endif
#ifdef ENABLE_ALARM
    {"ALARM",           ACTION_OPT_ALARM},
#endif
#ifdef ENABLE_FMRADIO
    {"FM RADIO",        ACTION_OPT_FM},
#endif
#ifdef ENABLE_TX1750
    {"1750Hz",          ACTION_OPT_1750},
#endif
#ifdef ENABLE_REGA
    {"REGA\nALARM",     ACTION_OPT_REGA_ALARM},
    {"REGA\nTEST",      ACTION_OPT_REGA_TEST},
#endif
    {"LOCK\nKEYPAD",    ACTION_OPT_KEYLOCK},
    {"VFO A\nVFO B",    ACTION_OPT_A_B},
    {"VFO\nMEM",        ACTION_OPT_VFO_MR},
    {"MODE",            ACTION_OPT_SWITCH_DEMODUL},
#ifdef ENABLE_BLMIN_TMP_OFF
    {"BLMIN\nTMP OFF",  ACTION_OPT_BLMIN_TMP_OFF},      //BackLight Minimum Temporay OFF
#endif
#ifdef ENABLE_FEAT_F4HWN
    {"RX MODE",         ACTION_OPT_RXMODE},
    {"MAIN ONLY",       ACTION_OPT_MAINONLY},
    {"PTT",             ACTION_OPT_PTT},
    {"WIDE\nNARROW",    ACTION_OPT_WN},
    {"MUTE",            ACTION_OPT_MUTE},
    #ifdef ENABLE_FEAT_F4HWN_AUDIO
        {"RxA",            ACTION_OPT_RXA},
    #endif
    #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
        {"POWER\nHIGH",    ACTION_OPT_POWER_HIGH},
        {"REMOVE\nOFFSET",  ACTION_OPT_REMOVE_OFFSET},
    #endif
    #ifdef ENABLE_FEAT_F4HWN_BEAM
        {"BEAM",            ACTION_OPT_BEAM},
    #endif
    #ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS
        {"TONE CALL",       ACTION_OPT_TONECALL},
    #endif
    #ifdef ENABLE_FEAT_F4HWN_LBJ
        {"LBJMSG",          ACTION_OPT_LBJMSG},
    #endif
#endif
};

const uint8_t gSubMenu_SIDEFUNCTIONS_size = ARRAY_SIZE(gSubMenu_SIDEFUNCTIONS);

bool    gIsInSubMenu;
uint8_t gMenuCursor;
int UI_MENU_GetCurrentMenuId() {
    return view_item(list_cursor())->menu_id;
}

uint8_t UI_MENU_GetMenuIdx(uint8_t id)
{
    uint8_t i;

    for(i = 0; i < gMenuListCount; i++)
        if(view_item(i)->menu_id == id)
            return i;

    /* Not in the list that is showing: switch to the category that holds it and
     * answer with its position there, so a caller that only wants the cursor to
     * land on an item (the CSS scan) always finds it. */
    for(i = 0; i < ARRAY_SIZE(MenuList); i++)
        if(MenuList[i].menu_id == id)
        {
            uint8_t n = 0;
            uint8_t k;

            UI_MENU_EnterCategory(cat_of(&MenuList[i]));

            for(k = 0; k < i; k++)
                if(item_in_cat(&MenuList[k], gMenuCat))
                    n++;

            return n;
        }

    return 0;
}

int32_t gSubMenuSelection;

// edit box
char    edit_original[17]; // a copy of the text before editing so that we can easily test for changes/difference
char    edit[17];
int     edit_index;
bool    edit_is_uppercase = false;

static void UI_MENU_DrawTopRightRoundedBadge(const char *text, const uint8_t line, const bool center_in_area, const uint8_t area_x1, const uint8_t area_x2)
{
    const size_t length = strlen(text);
    const size_t char_pitch = ARRAY_SIZE(gFontSmall[0]) + 1u;
    const size_t text_width = length * char_pitch;
    const size_t capsule_span = text_width + 1u; // matches UI_PrintStringSmallNormalInverse x_end computation
    uint8_t text_x;

    if (length == 0 || line == 0 || line >= FRAME_LINES) {
        return;
    }

    if (center_in_area && area_x2 > area_x1 + 2u) {
        const uint8_t min_x = area_x1 + 1u;
        uint8_t max_x;
        const uint8_t area_width = area_x2 - area_x1 + 1u;

        if (capsule_span >= area_width) {
            text_x = min_x;
        } else {
            text_x = (uint8_t)(area_x1 + ((area_width - capsule_span) / 2u));
        }

        if (area_x2 > capsule_span) {
            max_x = (uint8_t)(area_x2 - capsule_span);
        } else {
            max_x = min_x;
        }

        if (max_x < min_x) {
            max_x = min_x;
        }
        if (text_x < min_x) {
            text_x = min_x;
        } else if (text_x > max_x) {
            text_x = max_x;
        }
    } else {
        if (capsule_span >= (LCD_WIDTH - 3u)) {
            text_x = 1u;
        } else {
            const uint8_t global_shift_right = 1u;
            const uint8_t base_text_x = (uint8_t)(LCD_WIDTH - capsule_span - 3u);
            const uint8_t max_text_x  = (uint8_t)(LCD_WIDTH - capsule_span - 1u);
            const uint16_t shifted_x = (uint16_t)base_text_x + global_shift_right;

            if (shifted_x > max_text_x) {
                text_x = max_text_x;
            } else {
                text_x = (uint8_t)shifted_x;
            }
        }
    }

    UI_PrintStringSmallNormalInverse(text, text_x, 0, line);
}

void UI_DisplayMenu(void)
{
    const unsigned int menu_list_width = 6; // max no. of characters on the menu list (left side)
    const unsigned int menu_item_x1    = (8 * menu_list_width) + 2;
    const unsigned int menu_item_x2    = LCD_WIDTH - 1;
    unsigned int       i;
    char               String[64];  // bigger cuz we can now do multi-line in one string (use '\n' char)
    char               top_right_badge[16];

    const int m = UI_MENU_GetCurrentMenuId();

#ifdef ENABLE_DTMF_CALLING
    char               Contact[16];
#endif

    UI_DisplayClear();

    if (gMenuCatView)
    {   /* the browser replaces the item list: pick a category, M opens it */
        UI_MENU_DrawCategoryPage();

        BACKLIGHT_TurnOn();
        ST7565_BlitFullScreen();
        return;
    }

#ifndef ENABLE_CUSTOM_MENU_LAYOUT
    for (i = 0; i < 3; i++)
        if (gMenuCursor > 0 || i > 0)
            if ((gMenuListCount - 1) != gMenuCursor || i != 2)
                UI_PrintString(menu_name(view_item(gMenuCursor + i - 1)), 0, 0, i * 2, 8);

    // invert the current menu list item pixels
    for (i = 0; i < (8 * menu_list_width); i++)
    {
        gFrameBuffer[2][i] ^= 0xFF;
        gFrameBuffer[3][i] ^= 0xFF;
    }

    // draw vertical separating dotted line
    for (i = 0; i < 7; i++)
        gFrameBuffer[i][(8 * menu_list_width) + 1] = 0xAA;

    // draw the little sub-menu triangle marker
    if (gIsInSubMenu)
        memcpy(gFrameBuffer[0] + (8 * menu_list_width) + 1, BITMAP_CurrentIndicator, sizeof(BITMAP_CurrentIndicator));

    // draw the menu index number/count
    sprintf(String, "%2u.%u", 1 + gMenuCursor, gMenuListCount);

    UI_PrintStringSmallNormal(String, 2, 0, 6);

#else
    {   // new menu layout .. experimental & unfinished
        const int menu_index = gMenuCursor;  // current selected menu item
        i = 1;

        if (!gIsInSubMenu) {
            while (i < 2)
            {   // leading menu items - small text
                const int k = menu_index + i - 2;
                if (k < 0)
                    UI_PrintStringSmallNormal(menu_name(view_item(gMenuListCount + k)), 0, 0, i);  // wrap-a-round
                else if (k >= 0 && k < (int)gMenuListCount)
                    UI_PrintStringSmallNormal(menu_name(view_item(k)), 0, 0, i);
                i++;
            }

            // current menu item - keep big n fat
            if (menu_index >= 0 && menu_index < (int)gMenuListCount)
                UI_PrintString(menu_name(view_item(menu_index)), 0, 0, 2, 8);
            i++;

            while (i < 4)
            {   // trailing menu item - small text
                const int k = menu_index + i - 2;
                if (k >= 0 && k < (int)gMenuListCount)
                    UI_PrintStringSmallNormal(menu_name(view_item(k)), 0, 0, 1 + i);
                else if (k >= (int)gMenuListCount)
                    UI_PrintStringSmallNormal(menu_name(view_item(gMenuListCount - k)), 0, 0, 1 + i);  // wrap-a-round
                i++;
            }

            // draw the menu index number/count
#ifndef ENABLE_FEAT_F4HWN
            sprintf(String, "%2u.%u", 1 + gMenuCursor, gMenuListCount);
            UI_PrintStringSmallNormal(String, 2, 0, 6);
#endif
        }
        else if (menu_index >= 0 && menu_index < (int)gMenuListCount)
        {   // current menu item
//          strcat(String, ":");
            UI_PrintString(menu_name(view_item(menu_index)), 0, 0, 0, 8);
//          UI_PrintStringSmallNormal(String, 0, 0, 0);
        }

#ifdef ENABLE_FEAT_F4HWN
        sprintf(String, "%02u/%u", 1 + gMenuCursor, gMenuListCount);
        UI_PrintStringSmallNormal(String, 6, 0, 6);
#endif
    }
#endif

    // **************

    memset(String, 0, sizeof(String));
    memset(top_right_badge, 0, sizeof(top_right_badge));

    bool already_printed = false;

    /* Brightness is set to max in some entries of this menu. Return it to the configured brightness
       level the "next" time we enter here.I.e., when we move from one menu to another.
       It also has to be set back to max when pressing the Exit key. */

    BACKLIGHT_TurnOn();

    //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
        uint8_t gaugeLine = 0;
        uint8_t gaugeMin = 0;
        uint8_t gaugeMax = 0;
    //#endif

    switch (m)
    {
        case MENU_SQL:
            sprintf(String, "%d", gSubMenuSelection);
            break;

        case MENU_MIC:
            {   // display the mic gain in actual dB rather than just an index number
                const uint8_t mic = gMicGain_dB2[gSubMenuSelection];
                sprintf(String, "+%u.%udB", mic / 2, (mic % 2) * 5);

                gaugeLine = 4;
                gaugeMin = 0;
                gaugeMax = 8;
            }
            break;

        case MENU_MIC_BAR:
            #ifdef ENABLE_AUDIO_BAR
                strcpy(String, gSubMenu_OFF_ON[gSubMenuSelection]);
            #else
                strcpy(String, gSubMenu_NA);
            #endif
            break;

        case MENU_STEP: {
            uint16_t step = gStepFrequencyTable[FREQUENCY_GetStepIdxFromSortedIdx(gSubMenuSelection)];
            sprintf(String, "%d.%02ukHz", step / 100, step % 100);
            break;
        }

        case MENU_TXP:
            if(gSubMenuSelection == 0)
            {
                strcpy(String, gSubMenu_TXP[gSubMenuSelection]);
            }
            else
            {
                sprintf(String, "%s\n%sW", gSubMenu_TXP[gSubMenuSelection], gSubMenu_SET_PWR[gSubMenuSelection - 1]);
            }
            break;

        case MENU_R_DCS:
        case MENU_T_DCS:
            if (gSubMenuSelection == 0)
                strcpy(String, gSubMenu_OFF_ON[0]);
            else if (gSubMenuSelection < 105)
                sprintf(String, "D%03oN", DCS_Options[gSubMenuSelection -   1]);
            else
                sprintf(String, "D%03oI", DCS_Options[gSubMenuSelection - 105]);
            break;

        case MENU_R_CTCS:
        case MENU_T_CTCS:
        {
            if (gSubMenuSelection == 0)
                strcpy(String, gSubMenu_OFF_ON[0]);
            else
                sprintf(String, "%u.%uHz", CTCSS_Options[gSubMenuSelection - 1] / 10, CTCSS_Options[gSubMenuSelection - 1] % 10);
            break;
        }

        case MENU_SFT_D:
            strcpy(String, gSubMenu_SFT_D[gSubMenuSelection]);
            break;

        case MENU_OFFSET:
            if (!gIsInSubMenu || gInputBoxIndex == 0)
            {
                sprintf(String, "%3d.%05u", gSubMenuSelection / 100000, abs(gSubMenuSelection) % 100000);
            }
            else
            {
                const char * ascii = INPUTBOX_GetAscii();
                sprintf(String, "%.3s.%.3s  ",ascii, ascii + 3);
            }

            UI_PrintString(String, menu_item_x1, menu_item_x2, 1, 8);
            UI_PrintString("MHz",  menu_item_x1, menu_item_x2, 3, 8);

            already_printed = true;
            break;

        case MENU_W_N:
            strcpy(String, gSubMenu_W_N[gSubMenuSelection]);
            break;

#ifndef ENABLE_FEAT_F4HWN
        case MENU_SCR:
            strcpy(String, gSubMenu_SCRAMBLER[gSubMenuSelection]);
            #if 1
                if (gSubMenuSelection > 0 && gSetting_ScrambleEnable)
                    BK4819_EnableScramble(gSubMenuSelection - 1);
                else
                    BK4819_DisableScramble();
            #endif
            break;
#endif

        case MENU_VOX:
            #ifdef ENABLE_VOX
                sprintf(String, gSubMenuSelection == 0 ? gSubMenu_OFF_ON[0] : "%u", gSubMenuSelection);
            #else
                strcpy(String, gSubMenu_NA);
            #endif
            break;

        case MENU_ABR:
            if(gSubMenuSelection == 0)
            {
                strcpy(String, gSubMenu_OFF_ON[0]);
            }
            else if(gSubMenuSelection < 61)
            {
                sprintf(String, "%02dm:%02ds", (((gSubMenuSelection) * 5) / 60), (((gSubMenuSelection) * 5) % 60));
                //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
                //ST7565_Gauge(4, 1, 60, gSubMenuSelection);
                gaugeLine = 4;
                gaugeMin = 1;
                gaugeMax = 60;
                //#endif
            }
            else
            {
                strcpy(String, "ON");
            }

            // Obsolete ???
            //if(BACKLIGHT_GetBrightness() < 4)
            //    BACKLIGHT_SetBrightness(4);
            break;

        case MENU_ABR_MIN:
        case MENU_ABR_MAX:
            sprintf(String, "%d", gSubMenuSelection);
            if(gIsInSubMenu)
                BACKLIGHT_SetBrightness(gSubMenuSelection);
            // Obsolete ???
            //else if(BACKLIGHT_GetBrightness() < 4)
            //    BACKLIGHT_SetBrightness(4);
            break;

        case MENU_AM:
            strcpy(String, gModulationStr[gSubMenuSelection]);
            break;

        case MENU_AUTOLK:
            if (gSubMenuSelection == 0)
                strcpy(String, gSubMenu_OFF_ON[0]);
            else
            {
                sprintf(String, "%02dm:%02ds", ((gSubMenuSelection * 15) / 60), ((gSubMenuSelection * 15) % 60));
                //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
                //ST7565_Gauge(4, 1, 40, gSubMenuSelection);
                gaugeLine = 4;
                gaugeMin = 1;
                gaugeMax = 40;
                //#endif
            }
            break;

        case MENU_COMPAND:
        case MENU_ABR_ON_TX_RX:
            strcpy(String, gSubMenu_RX_TX[gSubMenuSelection]);
            break;

        #ifndef ENABLE_FEAT_F4HWN
            #ifdef ENABLE_AM_FIX
                case MENU_AM_FIX:
            #endif
        #endif
        case MENU_BCL:
        case MENU_BEEP:
        case MENU_STE:
        case MENU_D_ST:
#ifdef ENABLE_DTMF_CALLING
        case MENU_D_DCD:
#endif
        case MENU_D_LIVE_DEC:
        #ifdef ENABLE_NOAA
            case MENU_NOAA_S:
        #endif
#ifndef ENABLE_FEAT_F4HWN
        case MENU_350TX:
        case MENU_200TX:
        case MENU_500TX:
#endif
        case MENU_350EN:
#ifndef ENABLE_FEAT_F4HWN
        case MENU_SCREN:
#endif
#ifdef ENABLE_FEAT_F4HWN
        case MENU_SET_TMR:
        case MENU_S_PRI:
#endif
            strcpy(String, gSubMenu_OFF_ON[gSubMenuSelection]);
            break;

        case MENU_MEM_CH:
        case MENU_1_CALL:
        case MENU_DEL_CH:
        case MENU_S_PRI_CH_1:
        case MENU_S_PRI_CH_2:
        {
            if(gSubMenuSelection == MR_CHANNELS_MAX)
            {
                UI_PrintString("None", menu_item_x1, menu_item_x2, 2, 8);
                already_printed = true;
                break;
            }
            else
            {
                const bool valid = RADIO_CheckValidChannel(gSubMenuSelection, false, 0);

                UI_GenerateChannelStringEx(String, valid, gSubMenuSelection);
                UI_PrintString(String, menu_item_x1, menu_item_x2, 0, 8);

                if (valid && !gAskForConfirmation)
                {   // show the frequency so that the user knows the channels frequency
                    const uint32_t frequency = SETTINGS_FetchChannelFrequency(gSubMenuSelection);
                    sprintf(String, "%u.%05u", frequency / 100000, frequency % 100000);
                    UI_PrintString(String, menu_item_x1, menu_item_x2, 5, 8);
                }

                SETTINGS_FetchChannelName(String, gSubMenuSelection);
                UI_PrintString(String[0] ? String : "--", menu_item_x1, menu_item_x2, 2, 8);
                already_printed = true;
                break;
            }
        }

        case MENU_MEM_NAME:
        {
            const bool valid = RADIO_CheckValidChannel(gSubMenuSelection, false, 0);

            UI_GenerateChannelStringEx(String, valid, gSubMenuSelection);
            UI_PrintString(String, menu_item_x1, menu_item_x2, 0, 8);

            if (valid)
            {
                const uint32_t frequency = SETTINGS_FetchChannelFrequency(gSubMenuSelection);

                //if (!gIsInSubMenu || edit_index < 0)
                if (!gIsInSubMenu)
                    edit_index = -1;
                if (edit_index < 0)
                {   // show the channel name
                    SETTINGS_FetchChannelName(String, gSubMenuSelection);
                    char *pPrintStr = String[0] ? String : "--";
                    UI_PrintString(pPrintStr, menu_item_x1, menu_item_x2, 2, 8);
                }
                else
                {   // show the channel name being edited
                    //UI_PrintString(edit, menu_item_x1, 0, 2, 8);
                    UI_PrintString(edit, menu_item_x1, menu_item_x2, 2, 8);
                    if (edit_index < 10) {
                        // UI_PrintString("^", menu_item_x1 - 1 + (8 * edit_index),0, 4, 8); // show the cursor
                        uint8_t x = menu_item_x1 - 1;
                        for (uint8_t i = 0; i < 10; i++) 
                        {
                            if (i != edit_index) 
                            {
                                if (edit[i] != 'g' && edit[i] != 'j')
                                {
                                    UI_DrawLineBuffer(gFrameBuffer, x, 29, x + 6, 29, 1);
                                }
                            }
                            else 
                            {
                                UI_DrawLineBuffer(gFrameBuffer, x + 2, 30, x + 4, 30, 1);
                                UI_DrawPixelBuffer(gFrameBuffer, x + 3, 29, 1);
                            }
                            x += 8;
                        }
                        
                        UI_PrintStringSmallNormal(edit_is_uppercase ? "ABC" : "abc", 77, 0, 4);
                    }
                }

                if (!gAskForConfirmation)
                {   // show the frequency so that the user knows the channels frequency
                    sprintf(String, "%u.%05u", frequency / 100000, frequency % 100000);
                    UI_PrintString(String, menu_item_x1, menu_item_x2, 5, 8);
                }
            }

            already_printed = true;
            break;
        }

        case MENU_SAVE:
            sprintf(String, gSubMenuSelection == 0 ? gSubMenu_OFF_ON[0] : "1:%u", gSubMenuSelection);
            break;

        case MENU_TDR:
            strcpy(String, gSubMenu_RXMode[gSubMenuSelection]);
            break;

        case MENU_TOT:
            sprintf(String, "%02dm:%02ds", (((gSubMenuSelection + 1) * 5) / 60), (((gSubMenuSelection + 1) * 5) % 60));
            //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
            //ST7565_Gauge(4, 5, 179, gSubMenuSelection);
            gaugeLine = 4;
            gaugeMin = 5;
            gaugeMax = 179;
            //#endif
            break;

        #ifdef ENABLE_VOICE
            case MENU_VOICE:
                strcpy(String, gSubMenu_VOICE[gSubMenuSelection]);
                break;
        #endif

        case MENU_SC_REV:
            if(gSubMenuSelection == 0)
            {
                strcpy(String, "STOP");
            }
            else if(gSubMenuSelection < 81)
            {
                sprintf(String, "CARRIER\n%02ds:%03dms", ((gSubMenuSelection * 250) / 1000), ((gSubMenuSelection * 250) % 1000));
                //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
                //ST7565_Gauge(5, 1, 80, gSubMenuSelection);
                gaugeLine = 5;
                gaugeMin = 1;
                gaugeMax = 80;
                //#endif
            }
            else
            {
                sprintf(String, "TIMEOUT\n%02dm:%02ds", (((gSubMenuSelection - 80) * 5) / 60), (((gSubMenuSelection - 80) * 5) % 60));
                //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
                //ST7565_Gauge(5, 80, 104, gSubMenuSelection);
                gaugeLine = 5;
                gaugeMin = 80;
                gaugeMax = 104;
                //#endif
            }
            break;

        case MENU_MDF:
            strcpy(String, gSubMenu_MDF[gSubMenuSelection]);
            break;

        case MENU_RP_STE:
            sprintf(String, gSubMenuSelection == 0 ? gSubMenu_OFF_ON[0] : "%u*100ms", gSubMenuSelection);
            break;

        case MENU_LIST_CH:
        case MENU_S_LIST:
            if (gSubMenuSelection == MR_CHANNELS_LIST + 1)
                strcpy(String, "ALL");
            else if (gSubMenuSelection == 0 && m == MENU_LIST_CH)
                strcpy(String, "OFF");
            else {
                const char *name = gListName[gSubMenuSelection - 1];
                
                // If first character is empty/invalid, display "N/A"
                if (IsEmptyName(name, sizeof(gListName[0])))
                    sprintf(String, "%02u", gSubMenuSelection);
                else
                    sprintf(String, "%02u (%.3s)", gSubMenuSelection, name);
            }
            break;
            
        #ifdef ENABLE_ALARM
            case MENU_AL_MOD:
                sprintf(String, gSubMenu_AL_MOD[gSubMenuSelection]);
                break;
        #endif

#ifdef ENABLE_DTMF_CALLING
        case MENU_ANI_ID:
            strcpy(String, gEeprom.ANI_DTMF_ID);
            break;
#endif
        case MENU_UPCODE:
            sprintf(String, "%.8s\n%.8s", gEeprom.DTMF_UP_CODE, gEeprom.DTMF_UP_CODE + 8);
            break;

        case MENU_DWCODE:
            sprintf(String, "%.8s\n%.8s", gEeprom.DTMF_DOWN_CODE, gEeprom.DTMF_DOWN_CODE + 8);
            break;

#ifdef ENABLE_DTMF_CALLING
        case MENU_D_RSP:
            strcpy(String, gSubMenu_D_RSP[gSubMenuSelection]);
            break;

        case MENU_D_HOLD:
            sprintf(String, "%ds", gSubMenuSelection);
            break;
#endif
        case MENU_D_PRE:
            sprintf(String, "%d*10ms", gSubMenuSelection);
            break;

        case MENU_PTT_ID:
            strcpy(String, gSubMenu_PTT_ID[gSubMenuSelection]);
            break;

        case MENU_BAT_TXT:
            strcpy(String, bat_text(gSubMenuSelection));
            break;

#ifdef ENABLE_DTMF_CALLING
        case MENU_D_LIST:
            gIsDtmfContactValid = DTMF_GetContact((int)gSubMenuSelection - 1, Contact);
            if (!gIsDtmfContactValid)
                strcpy(String, "NULL");
            else
                memcpy(String, Contact, 8);
            break;
#endif

        case MENU_PONMSG:
            strcpy(String, gSubMenu_PONMSG[gSubMenuSelection]);
            break;

        case MENU_ROGER:
            strcpy(String, gSubMenu_ROGER[gSubMenuSelection]);
            break;

        case MENU_VOL: {
            // SysInf is paginated. Pages appear in this order, only when their
            // feature flag is enabled:
            //   0          -> identity
            //   next       -> Build date/time         (ENABLE_FEAT_F4HWN)
            //   next       -> Battery                 (ENABLE_FEAT_F4HWN)
            //   next       -> Flash / SRAM usage      (ENABLE_FEAT_F4HWN_MEM)
            //   next, +1   -> CODE / WIKI QR codes    (ENABLE_FEAT_F4HWN_QRCODE)
            // In non-F4HWN builds, page 0 keeps the old battery-voltage display.
            const uint8_t page = (uint8_t)gSubMenuSelection;
            uint8_t       p    = 0;

            if (page == p++) {
                // Page 0: firmware identity.
#ifdef ENABLE_FEAT_F4HWN
                sprintf(String, "%s\n%s", AUTHOR_STRING_2, VERSION_STRING_2);
                UI_PrintStringSmallNormal(Edition, menu_item_x1 - 1, menu_item_x2, 6);
#else
                sprintf(String, "%u.%02uV\n%u%%",
                    gBatteryVoltageAverage / 100, gBatteryVoltageAverage % 100,
                    BATTERY_VoltsToPercent(gBatteryVoltageAverage));
#endif
                break;
            }
#ifdef ENABLE_FEAT_F4HWN
            if (page == p++) {
                strcpy(top_right_badge, "BUILD");
                UI_PrintStringSmallNormal(BuildDate, menu_item_x1 - 1, menu_item_x2, 3);
                UI_PrintStringSmallNormal(BuildTime, menu_item_x1 - 1, menu_item_x2, 4);
                UI_PrintStringSmallNormal(BuildCommit, menu_item_x1 - 1, menu_item_x2, 6);

                already_printed = true;
                break;
            }

            if (page == p++) {
                char val[16];

                strcpy(top_right_badge, "BATTERY");

                sprintf(val, "%u.%02uV %u%%",
                    gBatteryVoltageAverage / 100, gBatteryVoltageAverage % 100,
                    BATTERY_VoltsToPercent(gBatteryVoltageAverage));
                UI_PrintStringSmallNormal(val, menu_item_x1 - 1, menu_item_x2, 3);

                UI_PrintStringSmallNormal(gSubMenu_BATTYP[gEeprom.BATTERY_TYPE], menu_item_x1 - 1, menu_item_x2, 5);

                already_printed = true;
                break;
            }
#endif
#ifdef ENABLE_FEAT_F4HWN_MEM
            if (page == p++) {
                uint16_t flash_pct = 0;
                uint16_t ram_pct   = 0;
                UI_GetMemPercents(&flash_pct, &ram_pct);

                char val[16];

                // MEMORY title capsule centered in right zone, fb line 1.
                strcpy(top_right_badge, "MEMORY");

                // Flash + SRAM values stacked below, normal small font, with a fb-line of breathing space.
                sprintf(val, "FLASH %u.%u%%",
                        (unsigned)(flash_pct / 100), (unsigned)((flash_pct / 10) % 10));
                UI_PrintStringSmallNormal(val, menu_item_x1 - 1, menu_item_x2, 3);

                sprintf(val, "SRAM  %u.%u%%",
                        (unsigned)(ram_pct / 100), (unsigned)((ram_pct / 10) % 10));
                UI_PrintStringSmallNormal(val, menu_item_x1 - 1, menu_item_x2, 5);

                already_printed = true;
                break;
            }
#endif
#ifdef ENABLE_FEAT_F4HWN_QRCODE
            // Right zone: x=49..127 (79 px). QR centered at x=72..104.
            // Capsule label above QR (small-font Inverse style at fb line 1).
            if (page == p || page == p + 1) {
                const bool is_wiki = (page == (p + 1));

                strcpy(top_right_badge, is_wiki ? "WIKI" : "CODE");
                UI_DrawQRCode(is_wiki, 72, 28);
                
                already_printed = true;
                break;
            }

            p += 2; 
#endif
            break;
        }

        case MENU_RESET:
            strcpy(String, gSubMenu_RESET[gSubMenuSelection]);
            break;

        case MENU_F_LOCK:
#ifdef ENABLE_FEAT_F4HWN
            if(!gIsInSubMenu && gUnlockAllTxConfCnt>0 && gUnlockAllTxConfCnt<3)
#else
            if(!gIsInSubMenu && gUnlockAllTxConfCnt>0 && gUnlockAllTxConfCnt<10)
#endif
                strcpy(String, "READ\nMANUAL");
            else
                strcpy(String, gSubMenu_F_LOCK[gSubMenuSelection]);
            break;

        #ifdef ENABLE_F_CAL_MENU
            case MENU_F_CALI:
                {
                    const uint32_t value   = 22656 + gSubMenuSelection;
                    const uint32_t xtal_Hz = (0x4f0000u + value) * 5;

                    writeXtalFreqCal(gSubMenuSelection, false);

                    sprintf(String, "%d\n%u.%06u\nMHz",
                        gSubMenuSelection,
                        xtal_Hz / 1000000, xtal_Hz % 1000000);
                }
                break;
        #endif

        case MENU_BATCAL:
        {
            const uint16_t vol = (uint32_t)gBatteryVoltageAverage * gBatteryCalibration[3] / gSubMenuSelection;
            sprintf(String, "%u.%02uV\n%u", vol / 100, vol % 100, gSubMenuSelection);
            break;
        }

        case MENU_BATTYP:
            strcpy(String, gSubMenu_BATTYP[gSubMenuSelection]);
            break;

        case MENU_SET_NAV:
            strcpy(String, gSubMenu_SET_NAV[gSubMenuSelection]);
            break;

        case MENU_F1SHRT:
        case MENU_F1LONG:
        case MENU_F2SHRT:
        case MENU_F2LONG:
        case MENU_MLONG:
            strcpy(String, gSubMenu_SIDEFUNCTIONS[gSubMenuSelection].name);
            break;

#ifdef ENABLE_FEAT_F4HWN_SLEEP
        case MENU_SET_OFF:
            if(gSubMenuSelection == 0)
            {
                strcpy(String, gSubMenu_OFF_ON[0]);
            }
            else if(gSubMenuSelection < 121)
            {
                sprintf(String, "%dh:%02dm", (gSubMenuSelection / 60), (gSubMenuSelection % 60));
                //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
                //ST7565_Gauge(4, 1, 120, gSubMenuSelection);
                gaugeLine = 4;
                gaugeMin = 1;
                gaugeMax = 120;
                //#endif
            }
            break;
#endif

#ifdef ENABLE_FEAT_F4HWN
        case MENU_SET_PWR:
            sprintf(String, "%s\n%sW", gSubMenu_TXP[gSubMenuSelection + 1], gSubMenu_SET_PWR[gSubMenuSelection]);
            break;
    
        case MENU_SET_PTT:
            strcpy(String, gSubMenu_SET_PTT[gSubMenuSelection]);
            break;

        case MENU_SET_TOT:
        case MENU_SET_EOT:
            strcpy(String, gSubMenu_SET_TOT[gSubMenuSelection]); // Same as SET_TOT
            break;

        case MENU_SET_CTR:
            #ifdef ENABLE_FEAT_F4HWN_CTR
                sprintf(String, "%d", gSubMenuSelection);
                gSetting_set_ctr = gSubMenuSelection;
                ST7565_ContrastAndInv();
            #else
                strcpy(String, gSubMenu_NA);
            #endif
            break;

        case MENU_SET_INV:
            #ifdef ENABLE_FEAT_F4HWN_INV
                strcpy(String, gSubMenu_OFF_ON[gSubMenuSelection]);
                ST7565_ContrastAndInv();
            #else
                strcpy(String, gSubMenu_NA);
            #endif
            break;

        case MENU_TX_LOCK:
            if(TX_freq_check(gEeprom.VfoInfo[gEeprom.TX_VFO].pTX->Frequency) == 0)
            {
                strcpy(String, "Inside\nF Lock\nPlan");
            }
            else
            {
                strcpy(String, gSubMenu_OFF_ON[gSubMenuSelection]);
            }
            break;

        case MENU_SET_LCK:
            strcpy(String, gSubMenu_SET_LCK[gSubMenuSelection]);
            break;

        case MENU_SET_MET:
        case MENU_SET_GUI:
            strcpy(String, gSubMenu_SET_MET[gSubMenuSelection]); // Same as SET_MET
            break;

        #ifdef ENABLE_FEAT_F4HWN_SCAN_FASTER
            case MENU_SET_SCN:
                strcpy(String, gSubMenu_SET_SCN[gSubMenuSelection]);
                break;
        #endif

        #ifdef ENABLE_FEAT_F4HWN_AUDIO
            case MENU_SET_AUD:
                if(gTxVfo->Modulation == MODULATION_AM) {
                    strcpy(String, gSubMenu_SET_AUD_AM[gSubMenuSelection]);
                    strcpy(top_right_badge, "AM");
                }
                else if (gTxVfo->Modulation == MODULATION_USB) {
                    strcpy(String, "USB");
                    strcpy(top_right_badge, "USB");
                }
                else {
                    strcpy(String, gSubMenu_SET_AUD_FM[gSubMenuSelection]);
                    strcpy(top_right_badge, "FM");
                }
                break;
        #endif

        #ifdef ENABLE_FEAT_F4HWN_NARROWER
            case MENU_SET_NFM:
                strcpy(String, gSubMenu_SET_NFM[gSubMenuSelection]);
                break;
        #endif

        #ifdef ENABLE_FEAT_F4HWN_VOL
            case MENU_SET_VOL:
                if(gSubMenuSelection == 0)
                {
                    strcpy(String, gSubMenu_OFF_ON[0]);
                }
                else if(gSubMenuSelection < 64)
                {
                    sprintf(String, "%02u", gSubMenuSelection);
                    //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
                    //ST7565_Gauge(4, 1, 63, gSubMenuSelection);
                    gaugeLine = 4;
                    gaugeMin = 1;
                    gaugeMax = 63;
                    //#endif
                }
                // gEeprom.VOLUME_GAIN = gSubMenuSelection;
                BK4819_SetRxAudioGain();
                break;
        #endif

        #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
            case MENU_SET_KEY:
                strcpy(String, gSubMenu_SET_KEY[gSubMenuSelection]);
                break;                
        #endif
#endif

        #ifdef ENABLE_FEAT_F4HWN_LBJ
            case MENU_LBJPOP:
                strcpy(String, gSubMenu_LBJ_POP[gSubMenuSelection]);
                break;
            case MENU_LBJLOG:
                strcpy(String, gSubMenu_OFF_ON[gSubMenuSelection]);
                break;
            case MENU_LBJCLR:
                strcpy(String, gSubMenu_LBJ_CLR[gSubMenuSelection]);
                break;
            case MENU_LBJPRI:
            {   /* Which position slot the decode window measures from. While the
                 * item is open this is the pending choice, otherwise the store's
                 * own value - MENU_ShowCurrentSetting() seeds the two alike.
                 * Clamped, because a frame can still be drawn with whatever the
                 * item visited before this one left in gSubMenuSelection. */
                unsigned int sel = 0u;

                if (gSubMenuSelection > 0 &&
                    gSubMenuSelection < (int32_t)ARRAY_SIZE(gSubMenu_LBJ_PRI))
                    sel = (unsigned int)gSubMenuSelection;

                strcpy(String, gSubMenu_LBJ_PRI[sel]);
                break;
            }
            case MENU_LBJNUM:
                strcpy(String, gSubMenu_LBJ_NUM[gSubMenuSelection]);
                break;
#ifdef ENABLE_CN_FONT
            case MENU_LBJ_LANG:
                strcpy(String, gSubMenu_LBJ_LANG[gSubMenuSelection]);
                break;
#endif
            case MENU_LBJLAT:
            case MENU_LBJLON:
            {   /* One half of that slot's coordinate: what has been typed while
                 * the box holds something, the stored half otherwise, and dashes
                 * for a half that was never entered (or is out of range). */
                uint8_t       dig[LBJ_POS_DIGITS];
                const uint8_t prim = LBJ_POS_Primary();
                const bool    lon  = (m == MENU_LBJLON);

                if (gIsInSubMenu && gInputBoxIndex != 0)
                {
                    const char *ascii = INPUTBOX_GetAscii();

                    if (lon)
                        sprintf(String, "%.3s %.2s.%.2s", ascii, ascii + 3, ascii + 5);
                    else
                        sprintf(String, "%.2s %.2s.%.2s", ascii, ascii + 2, ascii + 4);
                }
                else if (prim < LBJ_POS_SLOTS && LBJ_POS_Get(prim, dig) &&
                         (lon ? LBJ_POS_LonOk(dig + 6u) : LBJ_POS_LatOk(dig)))
                {
                    const uint8_t *d = lon ? dig + 6u : dig;

                    if (lon)
                        sprintf(String, "%u%u%u %u%u.%u%u", (unsigned)d[0], (unsigned)d[1],
                                (unsigned)d[2], (unsigned)d[3], (unsigned)d[4],
                                (unsigned)d[5], (unsigned)d[6]);
                    else
                        sprintf(String, "%u%u %u%u.%u%u", (unsigned)d[0], (unsigned)d[1],
                                (unsigned)d[2], (unsigned)d[3], (unsigned)d[4],
                                (unsigned)d[5]);
                }
                else
                    strcpy(String, "--");
                break;
            }
        #endif

        #ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
            case MENU_SET_CFG:
                /* Not a stored setting: the settings bank this firmware will
                 * boot with. Bank 0 is the base config Main itself uses, the
                 * others belong to the user slots of the same number. */
                if (gSubMenuSelection == 0u)
                    strcpy(String, "Main");
                else
                    sprintf(String, "Bank %d", gSubMenuSelection);
                break;
        #endif
    }

    //#if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
    if(gaugeLine != 0)
    {
        ST7565_Gauge(gaugeLine, gaugeMin, gaugeMax, gSubMenuSelection);
    }
    //#endif

    if (!already_printed)
    {   // we now do multi-line text in a single string

        unsigned int y;
        unsigned int lines = 1;
        unsigned int len   = strlen(String);
        bool         small = false;

        if (len > 0)
        {
            // count number of lines
            for (i = 0; i < len; i++)
            {
                if (String[i] == '\n' && i < (len - 1))
                {   // found new line char
                    lines++;
                    String[i] = 0;  // null terminate the line
                }
            }

            if (lines > 3)
            {   // use small text
                small = true;
                if (lines > 7)
                    lines = 7;
            }

            // center vertically'ish
            /*
            if (small)
                y = 3 - ((lines + 0) / 2);  // untested
            else
                y = 2 - ((lines + 0) / 2);
            */

            y = (small ? 3 : 2) - (lines / 2); 

            // draw the text lines
            for (i = 0; i < len && lines > 0; lines--)
            {
                if (small)
                    UI_PrintStringSmallNormal(String + i, menu_item_x1, menu_item_x2, y);
                else
                    UI_PrintString(String + i, menu_item_x1, menu_item_x2, y, 8);

                // look for start of next line
                while (i < len && String[i] >= 32)
                    i++;

                // hop over the null term char(s)
                while (i < len && String[i] < 32)
                    i++;

                y += small ? 1 : 2;
            }
        }
    }

    if (m == MENU_S_PRI_CH_1 || m == MENU_S_PRI_CH_2)
    {

    }

    if ((m == MENU_R_CTCS || m == MENU_R_DCS) && gCssBackgroundScan)
        UI_PrintString("SCAN", menu_item_x1, menu_item_x2, 4, 8);

#ifdef ENABLE_DTMF_CALLING
    if (m == MENU_D_LIST && gIsDtmfContactValid) {
        Contact[11] = 0;
        memcpy(&gDTMF_ID, Contact + 8, 4);
        sprintf(String, "ID:%4s", gDTMF_ID);
        UI_PrintString(String, menu_item_x1, menu_item_x2, 4, 8);
    }
#endif

    if (m == MENU_R_CTCS ||
        m == MENU_T_CTCS) {
        const uint8_t approved_index =
            (gSubMenuSelection > 0) ? DCS_GetCtcssApprovedIndex(gSubMenuSelection - 1) : 0xFF;

        if (gSubMenuSelection == 0)
            sprintf(top_right_badge, "00/00");
        else if (approved_index != 0xFF)
            sprintf(top_right_badge, "%02u/%02u", (unsigned)gSubMenuSelection, (unsigned)approved_index + 1);
        else
            sprintf(top_right_badge, "%02u/--", (unsigned)gSubMenuSelection);
    }
    
    if (m == MENU_R_DCS ||
        m == MENU_T_DCS) {
        const uint8_t approved_index =
            (gSubMenuSelection > 0) ? DCS_GetDcsApprovedIndex(gSubMenuSelection - 1) : 0xFF;

        if (gSubMenuSelection == 0)
            sprintf(top_right_badge, "000/00");
        else if (approved_index != 0xFF)
            sprintf(top_right_badge, "%03u/%02u", (unsigned)gSubMenuSelection, (unsigned)approved_index + 1);
        else
            sprintf(top_right_badge, "%03u/--", (unsigned)gSubMenuSelection);
    }

#ifdef ENABLE_DTMF_CALLING
    if (m == MENU_D_LIST) {
        sprintf(top_right_badge, "%03d", gSubMenuSelection);
    }
#endif

    if (top_right_badge[0] != '\0') {
        UI_MENU_DrawTopRightRoundedBadge(top_right_badge, 1, true, menu_item_x1, menu_item_x2);
    }

    if ((m == MENU_RESET    ||
         m == MENU_MEM_CH   ||
         m == MENU_MEM_NAME ||
         m == MENU_DEL_CH) && gAskForConfirmation)
    {   // display confirmation
        char *pPrintStr = (gAskForConfirmation == 1) ? "SURE?" : "WAIT!";
        UI_PrintString(pPrintStr, menu_item_x1, menu_item_x2, 5, 8);
    }

    ST7565_BlitFullScreen();
}
