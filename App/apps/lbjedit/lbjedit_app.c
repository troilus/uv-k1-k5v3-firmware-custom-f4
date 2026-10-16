/* LBJ Edit - the five positions the resident LBJ popup measures a train from.
 *
 * The store is this app's own config area (cfg_load/cfg_save), laid out by
 * lbjedit_pos.h: a magic, a version, the primary slot and five slots of 13
 * decimal digits - latitude DD MM hh then longitude DDD MM hh, hundredths of a
 * minute, north and east only, exactly the fields APRS Edit edits. A slot still
 * holding erased flash is an unfilled one: the editor draws it as dashes and the
 * popup refuses it, so nothing has to be flagged, and a primary that is not
 * filled in simply leaves the APRS TX profile in charge of the readout.
 *
 * Editing a coordinate snapshots the store - EXIT restores the snapshot, MENU
 * keeps the edit, the way APRS Edit does. The cursor walks the 6 (latitude) or 7
 * (longitude) digit cells and a number key writes the cell under it; the first
 * edit of an unfilled slot starts from zeros, so the whole field does not have to
 * be typed out. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../app_api.h"
#include "lbjedit_pos.h"
#include "lbjedit_assets.h"

#define LOOP_MS      10u
#define HOLD_TICKS   40u
#define LAT_DIGITS    6u
#define LON_DIGITS   (LBJ_POS_DIGITS - LAT_DIGITS)
#define INDEX_SEP_X  48u
#define PANE_X       50u
#define PANE_W       (128u - PANE_X)
#define NO_CURSOR    0xFFu

enum { SCREEN_INDEX = 0, SCREEN_EDIT };

/* A latitude and a longitude per slot, then the picker the popup reads. */
enum {
    ITEM_LAT1 = 0, ITEM_LON1, ITEM_LAT2, ITEM_LON2, ITEM_LAT3,
    ITEM_LON3, ITEM_LAT4, ITEM_LON4, ITEM_LAT5, ITEM_LON5,
    ITEM_PRIMARY, MENU_COUNT
};

_Static_assert(MENU_COUNT == 2u * LBJ_POS_SLOTS + 1u,
               "a latitude and a longitude per slot, then the primary");
_Static_assert(MENU_COUNT == T_MENU_COUNT && ITEM_PRIMARY == T_PRIMARY_INDEX,
               "the menu table and the item list must agree");
_Static_assert(LBJ_POS_LEN <= 96u, "the store must fit an app's own config area");
_Static_assert(T_MENU_STRIDE >= 8u, "every menu label and its NUL must fit");
_Static_assert(LAT_DIGITS + LON_DIGITS == LBJ_POS_DIGITS,
               "the two coordinate fields cover a whole slot");

#define ITEM_SLOT(item)   ((uint8_t)((item) / 2u))
#define ITEM_IS_LON(item) (((item) & 1u) != 0u)

static struct {
    const app_api_t *A;
    uint8_t *store, *backup;      /* the store and the snapshot of one edit */
    uint8_t screen, item, cursor, held_key;
    uint16_t held_ticks;
    bool changed, running, dirty, bad, long_fired;
} g;

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst; const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *dst, int value, size_t n)
{
    uint8_t *d = dst;
    while (n--) *d++ = (uint8_t)value;
    return dst;
}

static char *put_u8(char *out, uint8_t value)
{
    if (value >= 10u) {
        *out = '0';
        while (value >= 10u) { (*out)++; value -= 10u; }
        out++;
    }
    *out++ = (char)('0' + value);
    return out;
}

/* "3/11" */
static char *put_ratio(char *out, uint8_t value, uint8_t total)
{
    out = put_u8(out, value); *out++ = '/';
    return put_u8(out, total);
}

/* v moved one step in direction d within 0..n-1, wrapping at both ends. */
static uint8_t step(uint8_t v, int8_t d, uint8_t n)
{
    if (d > 0) return (uint8_t)(v + 1u < n ? v + 1u : 0u);
    if (d < 0) return (uint8_t)(v ? v - 1u : n - 1u);
    return v;
}

/* The 13 digits of one slot. */
static uint8_t *slot_digits(uint8_t slot)
{
    return g.store + LBJ_POS_FIRST + (uint16_t)slot * LBJ_POS_DIGITS;
}

/* True when every digit of the slot was written: an unfilled slot is erased
 * flash, which the popup refuses because a digit above 9 fails its check. */
static bool slot_filled(uint8_t slot)
{
    const uint8_t *d = slot_digits(slot);
    for (uint8_t i = 0u; i < LBJ_POS_DIGITS; i++)
        if (d[i] > 9u) return false;
    return true;
}

/* The same validity the APRS Edit editor applies, plus the resident's range:
 * minutes below 60, and 90/180 degrees only with nothing after them. */
static bool coord_ok(uint8_t slot)
{
    const uint8_t *d = slot_digits(slot);
    if (!slot_filled(slot)) return false;
    const uint8_t lat = (uint8_t)(d[0] * 10u + d[1]);
    const uint8_t lat_min = (uint8_t)(d[2] * 10u + d[3]);
    const uint8_t lon = (uint8_t)(d[6] * 100u + d[7] * 10u + d[8]);
    const uint8_t lon_min = (uint8_t)(d[9] * 10u + d[10]);
    if (lat > 90u || lat_min > 59u || lon > 180u || lon_min > 59u) return false;
    if (lat == 90u && (lat_min | d[4] | d[5])) return false;
    if (lon == 180u && (lon_min | d[11] | d[12])) return false;
    return true;
}

/* A store we cannot read starts empty: magic, version, no primary, every slot
 * unfilled. It is only written out once something is edited, so an app that is
 * launched and left alone leaves the popup on the APRS TX position. */
static void store_reset(void)
{
    uint32_t magic = LBJ_POS_MAGIC;
    memset(g.store, 0xFF, LBJ_POS_LEN);
    memcpy(g.store, &magic, sizeof(magic));
    g.store[LBJ_POS_VER] = LBJ_POS_VERSION;
    g.store[LBJ_POS_PRIMARY] = LBJ_POS_NONE;
}

static void store_load(void)
{
    uint32_t magic;
    memcpy(&magic, g.store, sizeof(magic));
    if (magic != LBJ_POS_MAGIC || g.store[LBJ_POS_VER] != LBJ_POS_VERSION) {
        store_reset();
        return;
    }
    if (g.store[LBJ_POS_PRIMARY] >= LBJ_POS_SLOTS)   /* 0xFF or a stray value */
        g.store[LBJ_POS_PRIMARY] = LBJ_POS_NONE;
}

static void pane(const char *s, uint8_t line, bool bold)
{
    (bold ? g.A->print_bold : g.A->print_normal)(s, PANE_X, 127u, line);
}

static void hint(const char *s, uint8_t y)
{
    g.A->print_tiny(s, PANE_X, y, false, true);
}

/* A row of len characters centred on line 2 of the pane, the one under the
 * cursor redrawn in bold. NO_CURSOR shows none. */
static void draw_row(const char *row, uint8_t len, uint8_t cursor)
{
    char one[2];
    const uint8_t start = (uint8_t)(PANE_X + (PANE_W - len * 7u) / 2u);
    g.A->print_normal(row, start, 0u, 2u);
    if (cursor < len) {
        one[0] = row[cursor]; one[1] = '\0';
        g.A->print_bold(one, (uint8_t)(start + cursor * 7u), 0u, 2u);
    }
}

/* One coordinate field: a digit per cell with the group space and the decimal
 * point drawn in between, the same shape APRS Edit draws. A cell that has not
 * been filled in is a dash. */
static void draw_coord(uint8_t first, uint8_t count, bool edit)
{
    char row[14]; char *out = row; uint8_t bold = NO_CURSOR;
    const uint8_t *digits = slot_digits(ITEM_SLOT(g.item)) + first;
    for (uint8_t i = 0u; i < count; i++) {
        if (i == count - 4u) *out++ = ' ';
        if (i == count - 2u) *out++ = '.';
        if (edit && i == g.cursor) bold = (uint8_t)(out - row);
        *out++ = (digits[i] <= 9u) ? (char)('0' + digits[i]) : '-';
    }
    *out = '\0';
    draw_row(row, (uint8_t)(out - row), bold);
}

static void draw_menu(const char *assets)
{
    char count[8];
    const char *menu = assets + T_MENU;
    g.A->print_normal(menu + step(g.item, -1, MENU_COUNT) * T_MENU_STRIDE, 0u, 0u, 1u);
    g.A->print_string(menu + g.item * T_MENU_STRIDE, 0u, 0u, 2u, 8u);
    g.A->print_normal(menu + step(g.item, 1, MENU_COUNT) * T_MENU_STRIDE, 0u, 0u, 4u);
    *put_ratio(count, (uint8_t)(g.item + 1u), MENU_COUNT) = '\0';
    g.A->print_normal(count, 6u, 0u, 6u);
    /* The slot the popup is measuring from, flagged on its own lines */
    if (g.item != ITEM_PRIMARY && g.store[LBJ_POS_PRIMARY] == ITEM_SLOT(g.item))
        g.A->print_bold("*", 44u, 0u, 2u);
    g.A->draw_line(g.A->fb, INDEX_SEP_X, 0, INDEX_SEP_X, 55, true);
    for (uint8_t x = 0u; x < INDEX_SEP_X; x += 2u) g.A->fb[5][x] = 0x40u;
}

/* The selected item in the right pane: the digit field, or which slot the popup
 * reads. The editor adds the cursor; the bottom line carries the key hints, or
 * the verdict when the field being edited does not hold. */
static void draw_field(const char *assets)
{
    char value[6]; char *out = value;
    const bool editing = g.screen == SCREEN_EDIT;
    const uint8_t primary = g.store[LBJ_POS_PRIMARY];
    draw_menu(assets);
    if (g.item == ITEM_PRIMARY) {
        if (primary >= LBJ_POS_SLOTS) { *out++ = '-'; *out++ = '-'; }
        else *out++ = (char)('1' + primary);
        *out = '\0';
        pane(value, 2u, editing);
    } else {
        const bool lon = ITEM_IS_LON(g.item);
        draw_coord(lon ? LAT_DIGITS : 0u, lon ? LON_DIGITS : LAT_DIGITS, editing);
    }
    if (editing) hint(g.bad ? assets + T_INVALID : assets + T_HINT_EDIT, 48u);
    else if (primary < LBJ_POS_SLOTS && !slot_filled(primary)) hint(assets + T_EMPTY, 48u);
    else hint(assets + T_HINT_INDEX, 48u);
}

static void draw(void)
{
    char assets[UI_SIZE] __attribute__((aligned(4)));
    const app_api_t *A = g.A;
    A->asset_read(0u, assets, UI_SIZE);
    A->display_clear(); A->status_clear();
    A->print_inverse(assets + T_TITLE, 2u, 0u, true, true,
                     (uint8_t)(2u + T_TITLE_CHARS * 4u));
    A->draw_battery();
    draw_field(assets);
    A->blit_status(); A->blit_full();
}

/* Opening a coordinate snapshots the store. A slot that has never been filled in
 * starts from zeros, so the first edit does not have to type all 13 digits. */
static void open_editor(void)
{
    uint8_t *d = slot_digits(ITEM_SLOT(g.item));
    memcpy(g.backup, g.store, LBJ_POS_LEN);
    if (!slot_filled(ITEM_SLOT(g.item)))
        memset(d, 0, LBJ_POS_DIGITS);
    g.screen = SCREEN_EDIT; g.cursor = 0u; g.bad = false;
}

static void commit_editor(void)
{
    if (!coord_ok(ITEM_SLOT(g.item))) { g.bad = true; return; }
    g.changed = true; g.screen = SCREEN_INDEX; g.bad = false;
}

/* The digit cells of the selected field: a number key writes the cell under the
 * cursor and steps on, the arrows walk it. */
static void value_key(uint8_t key)
{
    const bool lon = ITEM_IS_LON(g.item);
    const uint8_t count = lon ? LON_DIGITS : LAT_DIGITS;
    uint8_t *dig = slot_digits(ITEM_SLOT(g.item)) + (lon ? LAT_DIGITS : 0u);
    int8_t d = g.A->nav_dir(key);
    if (key <= APP_KEY_9) { dig[g.cursor] = key; g.bad = false; d = 1; }
    g.cursor = step(g.cursor, d, count);
}

static void index_key(uint8_t key)
{
    if (key == APP_KEY_MENU) {
        if (g.item != ITEM_PRIMARY) open_editor();
    } else if (key == APP_KEY_EXIT) {
        if (g.changed) g.A->cfg_save(g.store, LBJ_POS_LEN);
        g.running = false;
    } else if (g.item == ITEM_PRIMARY) {
        /* The picker walks none, 1..5 and back, so a position can be handed back
         * to the APRS TX profile without clearing anything. */
        const int8_t d = g.A->nav_dir(key);
        uint8_t v = g.store[LBJ_POS_PRIMARY];
        if (v >= LBJ_POS_SLOTS) v = LBJ_POS_NONE;
        if (d > 0) v = (v == LBJ_POS_NONE) ? 0u : (uint8_t)(v + 1u);
        else if (d < 0) v = (v == LBJ_POS_NONE) ? (uint8_t)(LBJ_POS_SLOTS - 1u)
                                                : (uint8_t)(v - 1u);
        if (v >= LBJ_POS_SLOTS) v = LBJ_POS_NONE;
        if (v != g.store[LBJ_POS_PRIMARY]) { g.store[LBJ_POS_PRIMARY] = v; g.changed = true; }
    } else {
        g.item = step(g.item, g.A->nav_dir(key), MENU_COUNT);
    }
}

static void edit_key(uint8_t key)
{
    if (key == APP_KEY_MENU) commit_editor();
    else if (key == APP_KEY_EXIT) {
        memcpy(g.store, g.backup, LBJ_POS_LEN);
        g.screen = SCREEN_INDEX; g.bad = false;
    } else value_key(key);
}

static void short_key(uint8_t key)
{
    if (g.screen == SCREEN_INDEX) index_key(key); else edit_key(key);
    g.dirty = true;
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    uint8_t store[LBJ_POS_LEN], backup[LBJ_POS_LEN];
    g.A = api; g.store = store; g.backup = backup;
    g.held_key = APP_KEY_INVALID;
    g.running = true; g.dirty = true;
    api->cfg_load(store, sizeof(store));
    store_load();
    api->backlight_on();
    while (g.running) {
        if (g.dirty) { g.dirty = false; draw(); }
        const uint8_t key = api->get_key();
        if (key == APP_KEY_SAVER || key == APP_KEY_WAKE) {
            g.held_key = APP_KEY_INVALID; g.held_ticks = 0u; g.long_fired = false; g.dirty = true;
        } else if (key == APP_KEY_INVALID) {
            if (g.held_key != APP_KEY_INVALID && !g.long_fired) short_key(g.held_key);
            g.held_key = APP_KEY_INVALID; g.held_ticks = 0u; g.long_fired = false;
        } else if (key != g.held_key) {
            g.held_key = key; g.held_ticks = 0u; g.long_fired = false;
            if (api->nav_dir(key)) { g.long_fired = true; short_key(key); }
        } else if (api->nav_dir(key)) {
            if (++g.held_ticks >= 32u && !(g.held_ticks & 7u)) short_key(key);
        }
        api->battery_sample(); api->delay_ms(LOOP_MS); api->backlight_update();
    }
}
