#include "p8p/menu.h"

#include "p8p/pico8_font.h"
#include "p8p/platform.h"
#include "p8p/pocket_glyphs.h"
#include "p8p/state_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MENU_WIDTH 128
#define MENU_HEIGHT 128

/* Every page puts its button hints on this line. */
#define FOOTER_Y 121

enum menu_screen {
    SCREEN_MAIN,
    SCREEN_STATES,
    SCREEN_CONTROLS,
    SCREEN_DISPLAY,
    SCREEN_AUDIO,
    SCREEN_INFO,
    SCREEN_MODAL,
    SCREEN_PAUSE
};

/* Cart-menu rows other than the cart's menuitem() slots 1-5. */
enum {
    PAUSE_CONTINUE = 0,
    PAUSE_RESET_CART = 6,
    PAUSE_BACK = 7,
    PAUSE_ROW_MAX = 8
};

/* Select's rows.  CART MENU opens the Start overlay, and only appears when
 * the cart has menuitem() entries or a return row. */
enum main_row {
    ROW_CONTINUE,
    ROW_CART_MENU,
    ROW_QUICK_SAVE, ROW_QUICK_LOAD, ROW_STATES, ROW_RESET, ROW_SELECT_CART,
    ROW_CONTROLS, ROW_DISPLAY, ROW_AUDIO, ROW_LANGUAGE, ROW_INFO, ROW_EXIT,
    ROW_COUNT
};

#define MAIN_VISIBLE_ROWS 12

enum modal_kind {
    MODAL_NONE,
    MODAL_RESET,
    MODAL_EXIT,
    MODAL_DELETE,
    MODAL_CART_PICKER
};

struct p8p_menu {
    p8p_settings_t *settings;
    p8p_runtime_t *runtime;
    int cart_running;
    p8p_cart_hash_t cart_hash;
    char cart_kind[16];
    uint8_t framebuffer[MENU_WIDTH * MENU_HEIGHT];
    uint8_t background[MENU_WIDTH * MENU_HEIGHT];
    p8p_state_meta_t states[P8P_STATE_SLOT_COUNT];
    uint16_t previous_buttons;
    uint8_t pause_rows[PAUSE_ROW_MAX];
    int pause_row_count;
    /* The cart menu was opened from Select's CART MENU row, so B goes back
     * there instead of closing. */
    int pause_from_main;
    int screen;
    int cursor;
    int scroll;
    int modal;
    int modal_slot;
    /* Where a modal returns: the Select page or the cart menu. */
    int modal_return;
    int capture_action;
    int notice_frames;
    char notice[32];
};

#define P8P_GLYPH_ROWS(r0, r1, r2, r3, r4) {r0, r1, r2, r3, r4, 0, 0, 0}

/* Each comment spells the rows left to right, '#' set. */
const uint8_t p8p_pocket_glyphs[P8P_GLYPH_COUNT][8] = {
    /* A: right    .#####. ####### ####.## ####### .#####. */
    P8P_GLYPH_ROWS(0x3e, 0x7f, 0x6f, 0x7f, 0x3e),
    /* B: bottom   .#####. ####### ####### ###.### .#####. */
    P8P_GLYPH_ROWS(0x3e, 0x7f, 0x7f, 0x77, 0x3e),
    /* X: top      .#####. ###.### ####### ####### .#####. */
    P8P_GLYPH_ROWS(0x3e, 0x77, 0x7f, 0x7f, 0x3e),
    /* Y: left     .#####. ####### ##.#### ####### .#####. */
    P8P_GLYPH_ROWS(0x3e, 0x7f, 0x7b, 0x7f, 0x3e),
    /* Start       ....... .....## .....## ##.#.## ....... */
    P8P_GLYPH_ROWS(0x00, 0x60, 0x60, 0x6b, 0x00),
    /* Select      ....... ##..... ##..... ##.#.## ....... */
    P8P_GLYPH_ROWS(0x00, 0x03, 0x03, 0x6b, 0x00),
    /* L           ....... ###.### ###.#.# ###.### ....... */
    P8P_GLYPH_ROWS(0x00, 0x77, 0x57, 0x77, 0x00),
    /* R           ....... ###.### #.#.### ###.### ....... */
    P8P_GLYPH_ROWS(0x00, 0x77, 0x75, 0x77, 0x00),
    /* D-pad       ..###.. ###.### ##...## ###.### ..###.. */
    P8P_GLYPH_ROWS(0x1c, 0x77, 0x63, 0x77, 0x1c),
    /* Up, down, left, right: P8SCII's arrows (148, 131, 139, 145). */
    P8P_GLYPH_ROWS(0x3e, 0x77, 0x63, 0x63, 0x3e),
    P8P_GLYPH_ROWS(0x3e, 0x63, 0x63, 0x77, 0x3e),
    P8P_GLYPH_ROWS(0x3e, 0x67, 0x63, 0x67, 0x3e),
    P8P_GLYPH_ROWS(0x3e, 0x73, 0x63, 0x73, 0x3e),
    /* Analogue    ....... ...#... ...#... ##.#.## ....... */
    P8P_GLYPH_ROWS(0x00, 0x08, 0x08, 0x6b, 0x00),
};

#undef P8P_GLYPH_ROWS

/* Letters are always shown in PICO-8's default style, which the font stores at
 * the lowercase codes. */
uint16_t p8p_menu_glyph(uint32_t character) {
    uint16_t bits = 0;
    if (character >= 'A' && character <= 'Z')
        character += 'a' - 'A';
    if (character >= 128)
        return 0;
    const uint8_t *rows = p8p_pico8_font + character * 8;
    for (int row = 0; row < 5; ++row)
        for (int column = 0; column < 3; ++column)
            if (rows[row] & (1u << column))
                bits |= (uint16_t)(1u << (14 - row * 3 - column));
    return bits;
}

/* A Pocket glyph or a P8SCII wide character: 7x5 rows in font layout. */
static const uint8_t *wide_glyph(uint32_t c) {
    if (c >= P8P_GLYPH_FIRST && c < P8P_GLYPH_FIRST + P8P_GLYPH_COUNT)
        return p8p_pocket_glyphs[c - P8P_GLYPH_FIRST];
    if (c >= P8P_GLYPH_P8SCII_FIRST && c < P8P_GLYPH_FIRST + 256)
        return p8p_pico8_font + (c - P8P_GLYPH_FIRST) * 8;
    return NULL;
}

/* Which button a cart's O or X prompt shows when several are mapped: the
 * face buttons first, then the shoulders, Start and the D-pad. */
static int first_button(uint16_t buttons) {
    static const struct { uint16_t bit; uint8_t glyph; } order[] = {
        {P8P_PHYS_A, P8P_GLYPH_A}, {P8P_PHYS_B, P8P_GLYPH_B},
        {P8P_PHYS_X, P8P_GLYPH_X}, {P8P_PHYS_Y, P8P_GLYPH_Y},
        {P8P_PHYS_L, P8P_GLYPH_L}, {P8P_PHYS_R, P8P_GLYPH_R},
        {P8P_PHYS_START, P8P_GLYPH_START},
        {P8P_PHYS_LEFT, P8P_GLYPH_LEFT}, {P8P_PHYS_RIGHT, P8P_GLYPH_RIGHT},
        {P8P_PHYS_UP, P8P_GLYPH_UP}, {P8P_PHYS_DOWN, P8P_GLYPH_DOWN}
    };
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i)
        if (buttons & order[i].bit)
            return order[i].glyph;
    return -1;
}

const uint8_t *p8p_menu_button_glyph(uint16_t buttons) {
    int glyph = first_button(buttons);
    return glyph < 0 ? NULL : p8p_pocket_glyphs[glyph];
}

/* UTF-8 for a codepoint from U+0800 to U+FFFF. */
static void utf8_put3(uint32_t c, char out[4]) {
    out[0] = (char)(0xe0 | (c >> 12));
    out[1] = (char)(0x80 | ((c >> 6) & 0x3f));
    out[2] = (char)(0x80 | (c & 0x3f));
    out[3] = '\0';
}

static uint16_t glyph_cyrillic(uint32_t c) {
#define G(a,b,c,d,e) ((uint16_t)((a)<<12)|(uint16_t)((b)<<9)| \
    (uint16_t)((c)<<6)|(uint16_t)((d)<<3)|(uint16_t)(e))
    if (c >= 0x430 && c <= 0x44f) c -= 0x20;
    switch (c) {
    case 0x410: return G(2,5,7,5,5); /* А */
    case 0x411: return G(7,4,6,5,6); /* Б */
    case 0x412: return G(6,5,6,5,6); /* В */
    case 0x413: return G(7,4,4,4,4); /* Г */
    case 0x414: return G(2,5,5,7,5); /* Д */
    case 0x415: case 0x401: return G(7,4,6,4,7); /* Е Ё */
    case 0x416: return G(5,5,2,5,5); /* Ж */
    case 0x417: return G(6,1,2,1,6); /* З */
    case 0x418: case 0x419: return G(5,5,7,7,5); /* И Й */
    case 0x41a: return G(5,5,6,5,5); /* К */
    case 0x41b: return G(2,5,5,5,5); /* Л */
    case 0x41c: return G(5,7,7,5,5); /* М */
    case 0x41d: return G(5,5,7,5,5); /* Н */
    case 0x41e: return G(2,5,5,5,2); /* О */
    case 0x41f: return G(7,5,5,5,5); /* П */
    case 0x420: return G(6,5,6,4,4); /* Р */
    case 0x421: return G(3,4,4,4,3); /* С */
    case 0x422: return G(7,2,2,2,2); /* Т */
    case 0x423: return G(5,5,3,1,6); /* У */
    case 0x424: return G(2,7,7,7,2); /* Ф */
    case 0x425: return G(5,5,2,5,5); /* Х */
    case 0x426: return G(5,5,5,5,7); /* Ц */
    case 0x427: return G(5,5,3,1,1); /* Ч */
    case 0x428: case 0x429: return G(5,5,5,5,7); /* Ш Щ */
    case 0x42a: return G(4,4,6,5,6); /* Ъ */
    case 0x42b: return G(5,5,6,5,6); /* Ы */
    case 0x42c: return G(4,4,6,5,6); /* Ь */
    case 0x42d: return G(6,1,3,1,6); /* Э */
    case 0x42e: return G(6,5,7,5,6); /* Ю */
    case 0x42f: return G(3,5,3,5,5); /* Я */
    default: return 0;
    }
#undef G
}

static uint32_t utf8_next(const char **text) {
    const uint8_t *p = (const uint8_t *)*text;
    uint32_t c;
    if (*p < 0x80) {
        *text += *p ? 1 : 0;
        return *p;
    }
    if ((*p & 0xe0) == 0xc0 && p[1]) {
        c = ((uint32_t)(p[0] & 0x1f) << 6) | (p[1] & 0x3f);
        *text += 2;
        return c;
    }
    if ((*p & 0xf0) == 0xe0 && p[1] && p[2]) {
        c = ((uint32_t)(p[0] & 0x0f) << 12) |
            ((uint32_t)(p[1] & 0x3f) << 6) | (p[2] & 0x3f);
        *text += 3;
        return c;
    }
    ++*text;
    return '?';
}

static void fill(uint8_t *fb, int x, int y, int w, int h, uint8_t color) {
    for (int py = 0; py < h; ++py) {
        int dy = y + py;
        if ((unsigned)dy >= MENU_HEIGHT) continue;
        for (int px = 0; px < w; ++px) {
            int dx = x + px;
            if ((unsigned)dx < MENU_WIDTH)
                fb[dy * MENU_WIDTH + dx] = color;
        }
    }
}

static void plot(uint8_t *fb, int x, int y, uint8_t color) {
    if ((unsigned)x < MENU_WIDTH && (unsigned)y < MENU_HEIGHT)
        fb[y * MENU_WIDTH + x] = color;
}

/* Letters advance 4 pixels, button glyphs 8, as in PICO-8's print(). */
int p8p_menu_text_width(const char *value) {
    int width = 0;
    while (value && *value)
        width += wide_glyph(utf8_next(&value)) ? 8 : 4;
    return width;
}

void p8p_menu_draw_text(uint8_t *fb, int x, int y, const char *value,
                        uint8_t color) {
    while (value && *value && x <= MENU_WIDTH - 3) {
        uint32_t c = utf8_next(&value);
        const uint8_t *wide = wide_glyph(c);
        if (wide) {
            for (int row = 0; row < 5; ++row)
                for (int column = 0; column < 7; ++column)
                    if (wide[row] & (1u << column))
                        plot(fb, x + column, y + row, color);
            x += 8;
            continue;
        }
        uint16_t bits = c < 128 ? p8p_menu_glyph(c) : glyph_cyrillic(c);
        for (int row = 0; row < 5; ++row)
            for (int column = 0; column < 3; ++column)
                if (bits & (1u << (14 - row * 3 - column)))
                    plot(fb, x + column, y + row, color);
        x += 4;
    }
}

static void text(uint8_t *fb, int x, int y, const char *value, uint8_t color) {
    p8p_menu_draw_text(fb, x, y, value, color);
}

static const char *tr(const p8p_menu_t *menu, const char *english,
                      const char *russian) {
    return menu->settings->language ? russian : english;
}

static void menu_base(p8p_menu_t *menu, const char *title) {
    memcpy(menu->framebuffer, menu->background, sizeof(menu->framebuffer));
    for (int y = 0; y < MENU_HEIGHT; ++y)
        for (int x = (y & 1); x < MENU_WIDTH; x += 2)
            menu->framebuffer[y * MENU_WIDTH + x] = 0;
    fill(menu->framebuffer, 0, 0, 128, 9, 1);
    text(menu->framebuffer, 3, 2, title, 7);
}

static void footer(p8p_menu_t *menu, const char *hint) {
    text(menu->framebuffer, 3, FOOTER_Y, hint, 6);
}

static void selected_row(p8p_menu_t *menu, int row, int y, const char *label,
                         int enabled) {
    if (row == menu->cursor)
        fill(menu->framebuffer, 1, y - 1, 126, 7, enabled ? 12 : 5);
    text(menu->framebuffer, 4, y, label, enabled ? 7 : 6);
}

static void set_notice(p8p_menu_t *menu, const char *message) {
    snprintf(menu->notice, sizeof(menu->notice), "%s", message ? message : "");
    menu->notice_frames = 90;
}

static const char *slot_name(const p8p_menu_t *menu, int slot, char out[16]) {
    if (slot == P8P_STATE_QUICK)
        return tr(menu, "QUICK", "БЫСТРОЕ");
    snprintf(out, 16, "%s %d", tr(menu, "SLOT", "СЛОТ"), slot);
    return out;
}

static void refresh_state(p8p_menu_t *menu, int slot) {
    (void)p8p_state_get_meta(slot, &menu->cart_hash, &menu->states[slot]);
}

static void refresh_states(p8p_menu_t *menu) {
    for (int slot = 0; slot < P8P_STATE_SLOT_COUNT; ++slot)
        refresh_state(menu, slot);
}

static const p8p_control_profile_t *active_controls(p8p_menu_t *menu,
                                                    int *override) {
    return p8p_settings_controls(menu->settings, &menu->cart_hash, override);
}

static const char *continue_label(const p8p_menu_t *menu) {
    return tr(menu, "CONTINUE", "ПРОДОЛЖИТЬ");
}

static const char *reset_label(const p8p_menu_t *menu) {
    return tr(menu, "RESET CART", "ПЕРЕЗАПУСК");
}

/* ---- The cart menu (Start): CONTINUE, the return row, the cart's
 * menuitem() slots in order, RESET CART. ---- */

static int refresh_pause_rows(p8p_menu_t *menu, p8p_runtime_t *runtime) {
    menu->pause_row_count = 0;
    menu->pause_rows[menu->pause_row_count++] = PAUSE_CONTINUE;
    if (runtime && menu->cart_running) {
        if (p8p_runtime_breadcrumb(runtime)[0])
            menu->pause_rows[menu->pause_row_count++] = PAUSE_BACK;
        for (int slot = 1; slot <= 5; ++slot)
            if (p8p_runtime_menu_item(runtime, slot))
                menu->pause_rows[menu->pause_row_count++] = (uint8_t)slot;
    }
    menu->pause_rows[menu->pause_row_count++] = PAUSE_RESET_CART;
    if (menu->cursor >= menu->pause_row_count)
        menu->cursor = menu->pause_row_count - 1;
    return menu->pause_row_count;
}

static int pause_index(const p8p_menu_t *menu, int item) {
    for (int row = 0; row < menu->pause_row_count; ++row)
        if (menu->pause_rows[row] == item)
            return row;
    return 0;
}

/* The cart has rows of its own, so Select offers CART MENU. */
static int has_cart_rows(p8p_menu_t *menu) {
    if (!menu->cart_running || !menu->runtime)
        return 0;
    if (p8p_runtime_breadcrumb(menu->runtime)[0])
        return 1;
    for (int slot = 1; slot <= 5; ++slot)
        if (p8p_runtime_menu_item(menu->runtime, slot))
            return 1;
    return 0;
}

static void draw_box(uint8_t *fb, int x, int y, int width, int height) {
    fill(fb, x, y, width, height, 0);
    fill(fb, x + 1, y + 1, width - 2, 1, 7);
    fill(fb, x + 1, y + height - 2, width - 2, 1, 7);
    fill(fb, x + 1, y + 1, 1, height - 2, 7);
    fill(fb, x + width - 2, y + 1, 1, height - 2, 7);
}

static void draw_pause(p8p_menu_t *menu, p8p_runtime_t *runtime) {
    uint8_t *fb = menu->framebuffer;
    const char *hint;
    int width = 84;
    int height = menu->pause_row_count * 8 + 16;
    int x = (MENU_WIDTH - width) / 2;
    int y = (MENU_HEIGHT - height) / 2;
    memcpy(fb, menu->background, sizeof(menu->framebuffer));
    draw_box(fb, x, y, width, height);
    for (int row = 0; row < menu->pause_row_count; ++row) {
        int text_x = x + 10;
        int text_y = y + 5 + row * 8;
        int item = menu->pause_rows[row];
        const char *label =
            item == PAUSE_CONTINUE ? continue_label(menu) :
            item == PAUSE_RESET_CART ? reset_label(menu) :
            item == PAUSE_BACK ? p8p_runtime_breadcrumb(runtime) :
            p8p_runtime_menu_item(runtime, item);
        text(fb, text_x, text_y, label ? label : "", 7);
        if (row == menu->cursor) {
            fill(fb, text_x - 5, text_y, 1, 5, 7);
            fill(fb, text_x - 4, text_y + 1, 1, 3, 7);
            fill(fb, text_x - 3, text_y + 2, 1, 1, 7);
        }
    }
    hint = menu->pause_from_main ?
        tr(menu, P8P_ICON_A " OK  " P8P_ICON_B " BACK",
                 P8P_ICON_A " ОК  " P8P_ICON_B " НАЗАД") :
        tr(menu, P8P_ICON_A " OK  " P8P_ICON_B " CLOSE",
                 P8P_ICON_A " ОК  " P8P_ICON_B " ЗАКРЫТЬ");
    text(fb, x + (width - p8p_menu_text_width(hint)) / 2,
         y + height - 10, hint, 6);
}

/* Runs a cart row: the return row, or a menuitem() callback with PICO-8's
 * button bits (1 left, 2 right, 112 for O/X). */
static enum p8p_menu_action select_cart_row(p8p_menu_t *menu,
                                            p8p_runtime_t *runtime, int item,
                                            uint16_t pressed) {
    int buttons;
    int result;
    if (item == PAUSE_BACK) {
        if (!(pressed & P8P_PHYS_A))
            return P8P_MENU_NONE;
        return p8p_runtime_request_back(runtime) == 0 ?
               P8P_MENU_CLOSE : P8P_MENU_NONE;
    }
    buttons = (pressed & P8P_PHYS_A) ? 112 :
              (pressed & P8P_PHYS_LEFT) ? 1 :
              (pressed & P8P_PHYS_RIGHT) ? 2 : 0;
    if (!buttons)
        return P8P_MENU_NONE;
    result = p8p_runtime_menu_select(runtime, item, buttons);
    if (result < 0)
        return P8P_MENU_CART_ERROR;
    /* The callback can add or remove items; stay on its row (CONTINUE if
     * it removed itself). */
    refresh_pause_rows(menu, runtime);
    menu->cursor = pause_index(menu, item);
    return result == 0 ? P8P_MENU_CLOSE : P8P_MENU_NONE;
}

/* ---- Select's pages. ---- */

static int main_rows(p8p_menu_t *menu, int rows[ROW_COUNT]) {
    int count = 0;
    rows[count++] = ROW_CONTINUE;
    if (has_cart_rows(menu))
        rows[count++] = ROW_CART_MENU;
    for (int row = ROW_QUICK_SAVE; row < ROW_COUNT; ++row)
        rows[count++] = row;
    return count;
}

static int main_index(p8p_menu_t *menu, int row) {
    int rows[ROW_COUNT];
    int count = main_rows(menu, rows);
    for (int i = 0; i < count; ++i)
        if (rows[i] == row)
            return i;
    return 0;
}

static const char *main_label(const p8p_menu_t *menu, int row) {
    static const char *en[] = {
        "CART MENU", "QUICK SAVE", "QUICK LOAD", "STATES", "RESET CART",
        "SELECT CART", "CONTROLS", "DISPLAY", "AUDIO", "LANGUAGE",
        "CART INFO", "EXIT"
    };
    static const char *ru[] = {
        "МЕНЮ ИГРЫ", "БЫСТ.СОХР", "БЫСТ.ЗАГР", "СОСТОЯНИЯ", "ПЕРЕЗАПУСК",
        "ВЫБРАТЬ ИГРУ", "УПРАВЛЕНИЕ", "ЭКРАН", "ЗВУК", "ЯЗЫК", "ОБ ИГРЕ",
        "ВЫХОД"
    };
    if (row == ROW_CONTINUE)
        return continue_label(menu);
    return menu->settings->language ? ru[row - ROW_CART_MENU] :
                                      en[row - ROW_CART_MENU];
}

static void draw_main(p8p_menu_t *menu) {
    static const char fps_hint[] = P8P_ICON_SELECT "+" P8P_ICON_X " FPS";
    int rows[ROW_COUNT];
    int count = main_rows(menu, rows);
    if (menu->cursor >= count)
        menu->cursor = count - 1;
    if (menu->cursor < menu->scroll)
        menu->scroll = menu->cursor;
    if (menu->cursor >= menu->scroll + MAIN_VISIBLE_ROWS)
        menu->scroll = menu->cursor - MAIN_VISIBLE_ROWS + 1;
    if (menu->scroll > count - MAIN_VISIBLE_ROWS)
        menu->scroll = count > MAIN_VISIBLE_ROWS ? count - MAIN_VISIBLE_ROWS : 0;
    menu_base(menu, "PICO-8 POCKET");
    for (int i = 0; i < MAIN_VISIBLE_ROWS && menu->scroll + i < count; ++i) {
        int index = menu->scroll + i;
        int row = rows[index];
        selected_row(menu, index, 12 + i * 8, main_label(menu, row),
                     row != ROW_QUICK_LOAD ||
                     menu->states[P8P_STATE_QUICK].exists);
        if (row == ROW_CART_MENU)
            fill(menu->framebuffer, 1, 12 + i * 8, 1, 5, 10);
    }
    if (menu->scroll > 0)
        text(menu->framebuffer, 122, 2, "^", 6);
    if (menu->scroll + MAIN_VISIBLE_ROWS < count)
        text(menu->framebuffer, 122, 107, "V", 6);
    footer(menu, tr(menu, P8P_ICON_A " OK  " P8P_ICON_B " CLOSE",
                          P8P_ICON_A " ОК  " P8P_ICON_B " ЗАКРЫТЬ"));
    text(menu->framebuffer, 125 - p8p_menu_text_width(fps_hint), FOOTER_Y,
         fps_hint, 6);
    if (menu->notice_frames > 0) {
        fill(menu->framebuffer, 0, 112, 128, 8, 1);
        text(menu->framebuffer, 3, 114, menu->notice, 10);
    }
}

static void draw_states(p8p_menu_t *menu) {
    char label[16];
    menu_base(menu, tr(menu, "SAVE STATES", "СОСТОЯНИЯ"));
    fill(menu->framebuffer, 63, 11, 65, 66, 1);
    for (int row = 0; row < P8P_STATE_SLOT_COUNT; ++row) {
        const char *name = slot_name(menu, row, label);
        if (row == menu->cursor)
            fill(menu->framebuffer, 1, 11 + row * 7, 61, 7, 12);
        text(menu->framebuffer, 3, 12 + row * 7, name,
             menu->states[row].exists ? 7 : 6);
    }
    if (menu->states[menu->cursor].exists) {
        const uint8_t *thumb = menu->states[menu->cursor].thumbnail;
        for (int y = 0; y < 64; ++y)
            memcpy(menu->framebuffer + (12 + y) * 128 + 64,
                   thumb + y * 64, 64);
    } else {
        text(menu->framebuffer, 83, 40, tr(menu, "EMPTY", "ПУСТО"), 6);
    }
    text(menu->framebuffer, 3, 86,
         tr(menu, P8P_ICON_A " LOAD", P8P_ICON_A " ЗАГР"), 7);
    text(menu->framebuffer, 3, 94,
         tr(menu, P8P_ICON_X " SAVE", P8P_ICON_X " СОХР"), 7);
    text(menu->framebuffer, 3, 102,
         tr(menu, P8P_ICON_Y " DELETE", P8P_ICON_Y " УДАЛ"), 7);
    footer(menu, tr(menu, P8P_ICON_B " BACK", P8P_ICON_B " НАЗАД"));
    if (menu->notice_frames > 0)
        text(menu->framebuffer, 66, 86, menu->notice, 10);
}

/* The Pocket buttons in `mask` as glyphs, in the order the controls page
 * has always listed them. */
static void button_glyphs(uint16_t mask, char out[48]) {
    static const struct { uint16_t bit; const char *glyph; } names[] = {
        {P8P_PHYS_LEFT, P8P_ICON_LEFT}, {P8P_PHYS_RIGHT, P8P_ICON_RIGHT},
        {P8P_PHYS_UP, P8P_ICON_UP}, {P8P_PHYS_DOWN, P8P_ICON_DOWN},
        {P8P_PHYS_A, P8P_ICON_A}, {P8P_PHYS_B, P8P_ICON_B},
        {P8P_PHYS_X, P8P_ICON_X}, {P8P_PHYS_Y, P8P_ICON_Y},
        {P8P_PHYS_L, P8P_ICON_L}, {P8P_PHYS_R, P8P_ICON_R},
        {P8P_PHYS_START, P8P_ICON_START}
    };
    size_t used = 0;
    out[0] = '\0';
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        size_t length = strlen(names[i].glyph);
        if (!(mask & names[i].bit) || used + length >= 48)
            continue;
        memcpy(out + used, names[i].glyph, length + 1);
        used += length;
    }
    if (!used)
        snprintf(out, 48, "NONE");
}

static void draw_controls(p8p_menu_t *menu) {
    static const char *actions_en[] = {
        "LEFT", "RIGHT", "UP", "DOWN", P8P_ICON_P8_O, P8P_ICON_P8_X, "PAUSE"
    };
    static const char *actions_ru[] = {
        "ВЛЕВО", "ВПРАВО", "ВВЕРХ", "ВНИЗ", P8P_ICON_P8_O, P8P_ICON_P8_X,
        "ПАУЗА"
    };
    char row[80];
    char buttons[48];
    int has_override;
    const p8p_control_profile_t *profile = active_controls(menu, &has_override);
    menu_base(menu, tr(menu, "CONTROLS", "УПРАВЛЕНИЕ"));
    snprintf(row, sizeof(row), "%s: %s", tr(menu, "PROFILE", "ПРОФИЛЬ"),
             has_override ? tr(menu, "CART", "ИГРА") :
                            tr(menu, "GLOBAL", "ОБЩИЙ"));
    selected_row(menu, 0, 11, row, 1);
    for (int action = 0; action < P8P_CONTROL_ACTIONS; ++action) {
        button_glyphs(profile->physical[action], buttons);
        snprintf(row, sizeof(row), "%s: %s",
                 menu->settings->language ? actions_ru[action] : actions_en[action],
                 buttons);
        selected_row(menu, action + 1, 19 + action * 7, row, 1);
    }
    selected_row(menu, 8, 70, tr(menu, "COPY GLOBAL", "КОПИЯ ОБЩЕГО"), 1);
    selected_row(menu, 9, 77, tr(menu, "RESET DEFAULT", "СБРОС"), 1);
    selected_row(menu, 10, 84, tr(menu, "MAKE GLOBAL", "СДЕЛАТЬ ОБЩИМ"), 1);
    selected_row(menu, 11, 91, tr(menu, "USE GLOBAL", "УБРАТЬ ПРОФИЛЬ"),
                 has_override);
    selected_row(menu, 12, 98, tr(menu, "BACK", "НАЗАД"), 1);
    if (menu->capture_action >= 0) {
        fill(menu->framebuffer, 6, 106, 116, 19, 1);
        text(menu->framebuffer, 10, 109,
             tr(menu, "PRESS A BUTTON", "НАЖМИ КНОПКУ"), 10);
        text(menu->framebuffer, 10, 117,
             tr(menu, P8P_ICON_SELECT " CANCEL", P8P_ICON_SELECT " ОТМЕНА"), 6);
        return;
    }
    if (menu->notice_frames > 0)
        text(menu->framebuffer, 4, 111, menu->notice, 10);
    footer(menu, tr(menu, P8P_ICON_A " CHANGE  " P8P_ICON_B " BACK",
                          P8P_ICON_A " ИЗМЕНИТЬ  " P8P_ICON_B " НАЗАД"));
}

static void draw_display(p8p_menu_t *menu) {
    char row[48];
    const char *scale = menu->settings->scale == 0 ? "AUTO" :
                        menu->settings->scale == 1 ? "1X" : "2X";
    menu_base(menu, tr(menu, "DISPLAY", "ЭКРАН"));
    snprintf(row, sizeof(row), "%s: %s", tr(menu, "SCALE", "МАСШТАБ"), scale);
    selected_row(menu, 0, 18, row, 1);
    snprintf(row, sizeof(row), "%s: %s",
             tr(menu, "CART ICONS", "ЗНАЧКИ ИГРЫ"),
             menu->settings->pocket_glyphs ? "POCKET" : "PICO-8");
    selected_row(menu, 1, 28, row, 1);
    selected_row(menu, 2, 38, tr(menu, "BACK", "НАЗАД"), 1);
    text(menu->framebuffer, 4, 54,
         tr(menu, "PIXEL PERFECT", "ПИКСЕЛЬ В ПИКСЕЛЬ"), 6);
    /* Shows what CART ICONS does with this cart's buttons. */
    {
        const p8p_control_profile_t *profile = active_controls(menu, NULL);
        char o[4], x[4];
        int o_glyph = first_button(profile->physical[P8P_ACTION_O]);
        int x_glyph = first_button(profile->physical[P8P_ACTION_X]);
        if (menu->settings->pocket_glyphs && o_glyph >= 0)
            utf8_put3(P8P_GLYPH_FIRST + (uint32_t)o_glyph, o);
        else
            snprintf(o, sizeof(o), "%s", P8P_ICON_P8_O);
        if (menu->settings->pocket_glyphs && x_glyph >= 0)
            utf8_put3(P8P_GLYPH_FIRST + (uint32_t)x_glyph, x);
        else
            snprintf(x, sizeof(x), "%s", P8P_ICON_P8_X);
        snprintf(row, sizeof(row), "%s " P8P_ICON_P8_O "=%s " P8P_ICON_P8_X "=%s",
                 tr(menu, "IN CARTS", "В ИГРАХ"), o, x);
        text(menu->framebuffer, 4, 64, row, 6);
    }
    footer(menu, tr(menu, P8P_ICON_A " CHANGE  " P8P_ICON_B " BACK",
                          P8P_ICON_A " ИЗМЕНИТЬ  " P8P_ICON_B " НАЗАД"));
}

static void draw_audio(p8p_menu_t *menu) {
    char row[32];
    menu_base(menu, tr(menu, "AUDIO", "ЗВУК"));
    snprintf(row, sizeof(row), "%s: %u", tr(menu, "VOLUME", "ГРОМКОСТЬ"),
             (unsigned)menu->settings->volume);
    selected_row(menu, 0, 18, row, 1);
    snprintf(row, sizeof(row), "%s: %s", tr(menu, "MUTE", "БЕЗ ЗВУКА"),
             menu->settings->muted ? tr(menu, "ON", "ДА") : tr(menu, "OFF", "НЕТ"));
    selected_row(menu, 1, 28, row, 1);
    selected_row(menu, 2, 38, tr(menu, "BACK", "НАЗАД"), 1);
    footer(menu, tr(menu, P8P_ICON_A " CHANGE  " P8P_ICON_B " BACK",
                          P8P_ICON_A " ИЗМЕНИТЬ  " P8P_ICON_B " НАЗАД"));
}

static void draw_info(p8p_menu_t *menu, p8p_runtime_t *runtime) {
    char line[40];
    menu_base(menu, tr(menu, "CART INFO", "ОБ ИГРЕ"));
    snprintf(line, sizeof(line), "%s: %s", tr(menu, "FORMAT", "ФОРМАТ"),
             menu->cart_kind);
    text(menu->framebuffer, 4, 16, line, 7);
    snprintf(line, sizeof(line), "FPS: %d", p8p_runtime_target_fps(runtime));
    text(menu->framebuffer, 4, 26, line, 7);
    text(menu->framebuffer, 4, 40, "CONTENT HASH", 6);
    for (int half = 0; half < 2; ++half) {
        char hex[17];
        for (int i = 0; i < 8; ++i)
            snprintf(hex + i * 2, 3, "%02X", menu->cart_hash.bytes[half * 8 + i]);
        text(menu->framebuffer, 4, 49 + half * 8, hex, 7);
    }
    footer(menu, tr(menu, P8P_ICON_B " BACK", P8P_ICON_B " НАЗАД"));
}

static void draw_modal(p8p_menu_t *menu) {
    const char *yes_no = tr(menu, P8P_ICON_A " YES   " P8P_ICON_B " NO",
                                  P8P_ICON_A " ДА   " P8P_ICON_B " НЕТ");
    if (menu->modal_return == SCREEN_PAUSE) {
        /* Asked from the cart menu: keep its look. */
        uint8_t *fb = menu->framebuffer;
        const char *question = tr(menu, "RESET CART?", "ПЕРЕЗАПУСТИТЬ?");
        memcpy(fb, menu->background, sizeof(menu->framebuffer));
        draw_box(fb, 22, 46, 84, 36);
        text(fb, 64 - p8p_menu_text_width(question) / 2, 54, question, 7);
        text(fb, 64 - p8p_menu_text_width(yes_no) / 2, 68, yes_no, 10);
        return;
    }
    menu_base(menu, tr(menu, "CONFIRM", "ПОДТВЕРЖДЕНИЕ"));
    fill(menu->framebuffer, 6, 30, 116, 65, 1);
    if (menu->modal == MODAL_RESET) {
        text(menu->framebuffer, 14, 43,
             tr(menu, "RESET CART?", "ПЕРЕЗАПУСТИТЬ?"), 7);
    } else if (menu->modal == MODAL_EXIT) {
        text(menu->framebuffer, 14, 43,
             tr(menu, "EXIT TO OS?", "ВЫЙТИ В ОС?"), 7);
    } else if (menu->modal == MODAL_DELETE) {
        char name[16];
        char line[32];
        snprintf(line, sizeof(line), "%s %s?", tr(menu, "DELETE", "УДАЛИТЬ"),
                 slot_name(menu, menu->modal_slot, name));
        text(menu->framebuffer, 14, 43, line, 7);
    } else if (menu->modal == MODAL_CART_PICKER) {
        text(menu->framebuffer, 10, 36,
             tr(menu, "SELECT NEW CART", "ВЫБЕРИ НОВУЮ ИГРУ"), 10);
        text(menu->framebuffer, 10, 48,
             tr(menu, "PRESS " P8P_ICON_HOME " THEN", "НАЖМИ " P8P_ICON_HOME " И"), 7);
        text(menu->framebuffer, 10, 56, "CORE SETTINGS", 7);
        text(menu->framebuffer, 10, 64, "> CARTRIDGE", 7);
        text(menu->framebuffer, 10, 80,
             tr(menu, P8P_ICON_B " CANCEL", P8P_ICON_B " ОТМЕНА"), 6);
        return;
    }
    text(menu->framebuffer, 18, 72, yes_no, 10);
}

static void draw_menu(p8p_menu_t *menu, p8p_runtime_t *runtime) {
    switch (menu->screen) {
    case SCREEN_MAIN: draw_main(menu); break;
    case SCREEN_STATES: draw_states(menu); break;
    case SCREEN_CONTROLS: draw_controls(menu); break;
    case SCREEN_DISPLAY: draw_display(menu); break;
    case SCREEN_AUDIO: draw_audio(menu); break;
    case SCREEN_INFO: draw_info(menu, runtime); break;
    case SCREEN_MODAL: draw_modal(menu); break;
    case SCREEN_PAUSE: draw_pause(menu, runtime); break;
    }
    if (menu->notice_frames > 0)
        --menu->notice_frames;
}

p8p_menu_t *p8p_menu_create(p8p_settings_t *settings,
                            const p8p_cart_hash_t *cart_hash,
                            const char *cart_kind) {
    p8p_menu_t *menu;
    if (!settings || !cart_hash)
        return NULL;
    menu = (p8p_menu_t *)calloc(1, sizeof(*menu));
    if (!menu)
        return NULL;
    menu->settings = settings;
    menu->cart_hash = *cart_hash;
    menu->capture_action = -1;
    snprintf(menu->cart_kind, sizeof(menu->cart_kind), "%s",
             cart_kind ? cart_kind : "UNKNOWN");
    return menu;
}

void p8p_menu_destroy(p8p_menu_t *menu) {
    free(menu);
}

static void enter_screen(p8p_menu_t *menu, int screen, int cursor) {
    menu->screen = screen;
    menu->cursor = cursor;
    menu->capture_action = -1;
}

static void open_common(p8p_menu_t *menu, p8p_runtime_t *runtime,
                        uint16_t physical_buttons, int cart_running,
                        int dim) {
    const uint8_t *source = p8p_runtime_framebuffer(runtime);
    const uint8_t *palette = p8p_runtime_screen_palette(runtime);
    for (int i = 0; i < MENU_WIDTH * MENU_HEIGHT; ++i) {
        uint8_t color = palette ? palette[source[i] & 15] : source[i];
        menu->background[i] = dim ? p8p_platform_dim_color(color) : color;
    }
    menu->runtime = runtime;
    menu->cart_running = cart_running;
    menu->previous_buttons = physical_buttons;
    menu->scroll = 0;
    menu->modal = MODAL_NONE;
    menu->modal_return = SCREEN_MAIN;
    menu->pause_from_main = 0;
    menu->capture_action = -1;
}

void p8p_menu_open_pause(p8p_menu_t *menu, p8p_runtime_t *runtime,
                         uint16_t physical_buttons, uint8_t pico_buttons) {
    (void)pico_buttons;
    if (!menu || !runtime)
        return;
    open_common(menu, runtime, physical_buttons, 1, 0);
    enter_screen(menu, SCREEN_PAUSE, 0);
    refresh_pause_rows(menu, runtime);
    draw_menu(menu, runtime);
}

void p8p_menu_open(p8p_menu_t *menu, p8p_runtime_t *runtime,
                   uint16_t physical_buttons, int cart_running) {
    if (!menu || !runtime)
        return;
    open_common(menu, runtime, physical_buttons, cart_running, 1);
    enter_screen(menu, SCREEN_MAIN, 0);
    refresh_state(menu, P8P_STATE_QUICK);
    draw_menu(menu, runtime);
}

static void open_reset_modal(p8p_menu_t *menu, int from) {
    menu->modal = MODAL_RESET;
    menu->modal_return = from;
    enter_screen(menu, SCREEN_MODAL, 0);
}

static enum p8p_menu_action update_pause(p8p_menu_t *menu,
                                         p8p_runtime_t *runtime,
                                         uint16_t pressed) {
    const p8p_control_profile_t *controls = active_controls(menu, NULL);
    /* The cart's pause button closes the menu, as in PICO-8, unless it is
     * also a confirm or back button here. */
    uint16_t pause = controls->physical[P8P_ACTION_PAUSE] &
                     (uint16_t)~(P8P_PHYS_A | P8P_PHYS_B | P8P_PHYS_X);
    int item;
    if (pressed & pause)
        return P8P_MENU_CLOSE;
    if (pressed & P8P_PHYS_B) {
        if (!menu->pause_from_main)
            return P8P_MENU_CLOSE;
        menu->pause_from_main = 0;
        enter_screen(menu, SCREEN_MAIN, main_index(menu, ROW_CART_MENU));
        return P8P_MENU_NONE;
    }
    item = menu->pause_rows[menu->cursor];
    if (item == PAUSE_CONTINUE)
        return (pressed & P8P_PHYS_A) ? P8P_MENU_CLOSE : P8P_MENU_NONE;
    if (item == PAUSE_RESET_CART) {
        if (pressed & P8P_PHYS_A)
            open_reset_modal(menu, SCREEN_PAUSE);
        return P8P_MENU_NONE;
    }
    return select_cart_row(menu, runtime, item, pressed);
}

static uint16_t first_remappable_button(uint16_t buttons) {
    static const uint16_t order[] = {
        P8P_PHYS_LEFT, P8P_PHYS_RIGHT, P8P_PHYS_UP, P8P_PHYS_DOWN,
        P8P_PHYS_A, P8P_PHYS_B, P8P_PHYS_X, P8P_PHYS_Y,
        P8P_PHYS_L, P8P_PHYS_R, P8P_PHYS_START
    };
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i)
        if (buttons & order[i]) return order[i];
    return 0;
}

static void save_settings(p8p_menu_t *menu) {
    menu->settings->scale = (uint8_t)p8p_platform_set_scale(
        menu->settings->scale);
    (void)p8p_settings_save(menu->settings);
    p8p_platform_audio_set_volume(menu->settings->muted ? 0 :
                                  menu->settings->volume);
}

static enum p8p_menu_action update_main(p8p_menu_t *menu,
                                        p8p_runtime_t *runtime,
                                        uint16_t pressed) {
    int rows[ROW_COUNT];
    int count = main_rows(menu, rows);
    int row = rows[menu->cursor < count ? menu->cursor : 0];
    if (pressed & P8P_PHYS_B)
        return P8P_MENU_CLOSE;
    if (!(pressed & P8P_PHYS_A))
        return P8P_MENU_NONE;
    switch (row) {
    case ROW_CONTINUE: return P8P_MENU_CLOSE;
    case ROW_CART_MENU:
        enter_screen(menu, SCREEN_PAUSE, 0);
        menu->pause_from_main = 1;
        refresh_pause_rows(menu, runtime);
        break;
    case ROW_QUICK_SAVE:
        if (p8p_state_save(P8P_STATE_QUICK, &menu->cart_hash, runtime) == 0) {
            refresh_state(menu, P8P_STATE_QUICK);
            set_notice(menu, tr(menu, "SAVED", "СОХРАНЕНО"));
        } else set_notice(menu, tr(menu, "SAVE FAILED", "ОШИБКА СОХР"));
        break;
    case ROW_QUICK_LOAD:
        if (menu->states[P8P_STATE_QUICK].exists &&
            p8p_state_load(P8P_STATE_QUICK, &menu->cart_hash, runtime) == 0)
            return P8P_MENU_CLOSE;
        set_notice(menu, tr(menu, "LOAD FAILED", "ОШИБКА ЗАГР"));
        break;
    case ROW_STATES:
        refresh_states(menu);
        enter_screen(menu, SCREEN_STATES, 0);
        break;
    case ROW_RESET:
        open_reset_modal(menu, SCREEN_MAIN);
        break;
    case ROW_SELECT_CART:
        menu->modal = MODAL_CART_PICKER;
        menu->modal_return = SCREEN_MAIN;
        enter_screen(menu, SCREEN_MODAL, 0);
        break;
    case ROW_CONTROLS: enter_screen(menu, SCREEN_CONTROLS, 0); break;
    case ROW_DISPLAY: enter_screen(menu, SCREEN_DISPLAY, 0); break;
    case ROW_AUDIO: enter_screen(menu, SCREEN_AUDIO, 0); break;
    case ROW_LANGUAGE:
        menu->settings->language ^= 1;
        save_settings(menu);
        break;
    case ROW_INFO: enter_screen(menu, SCREEN_INFO, 0); break;
    case ROW_EXIT:
        menu->modal = MODAL_EXIT;
        menu->modal_return = SCREEN_MAIN;
        enter_screen(menu, SCREEN_MODAL, 0);
        break;
    }
    return P8P_MENU_NONE;
}

static enum p8p_menu_action update_states(p8p_menu_t *menu,
                                          p8p_runtime_t *runtime,
                                          uint16_t pressed) {
    int slot = menu->cursor;
    if (pressed & P8P_PHYS_B) {
        enter_screen(menu, SCREEN_MAIN, main_index(menu, ROW_STATES));
    } else if (pressed & P8P_PHYS_X) {
        if (p8p_state_save(slot, &menu->cart_hash, runtime) == 0) {
            refresh_state(menu, slot);
            set_notice(menu, tr(menu, "SAVED", "СОХРАНЕНО"));
        } else set_notice(menu, tr(menu, "FAILED", "ОШИБКА"));
    } else if ((pressed & P8P_PHYS_A) && menu->states[slot].exists) {
        if (p8p_state_load(slot, &menu->cart_hash, runtime) == 0)
            return P8P_MENU_CLOSE;
        set_notice(menu, tr(menu, "FAILED", "ОШИБКА"));
    } else if ((pressed & P8P_PHYS_Y) && menu->states[slot].exists) {
        menu->modal_slot = slot;
        menu->modal = MODAL_DELETE;
        menu->modal_return = SCREEN_STATES;
        enter_screen(menu, SCREEN_MODAL, 0);
    }
    return P8P_MENU_NONE;
}

static void update_controls(p8p_menu_t *menu, uint16_t pressed) {
    p8p_control_profile_t *cart;
    int has_override;
    const p8p_control_profile_t *active = active_controls(menu, &has_override);
    if (menu->capture_action >= 0) {
        uint16_t chosen;
        if (pressed & P8P_PHYS_SELECT) {
            menu->capture_action = -1;
            return;
        }
        chosen = first_remappable_button(pressed & ~P8P_PHYS_SELECT);
        if (chosen) {
            cart = p8p_settings_cart_controls(menu->settings, &menu->cart_hash, 1);
            if (cart) {
                cart->physical[menu->capture_action] = chosen;
                save_settings(menu);
            }
            menu->capture_action = -1;
            set_notice(menu, tr(menu, "MAPPED", "НАЗНАЧЕНО"));
        }
        return;
    }
    if (pressed & P8P_PHYS_B) {
        enter_screen(menu, SCREEN_MAIN, main_index(menu, ROW_CONTROLS));
        return;
    }
    if (!(pressed & P8P_PHYS_A))
        return;
    if (menu->cursor == 0) {
        if (has_override)
            p8p_settings_remove_cart_controls(menu->settings, &menu->cart_hash);
        else
            (void)p8p_settings_cart_controls(menu->settings, &menu->cart_hash, 1);
        save_settings(menu);
    } else if (menu->cursor >= 1 && menu->cursor <= 7) {
        menu->capture_action = menu->cursor - 1;
    } else if (menu->cursor == 8) {
        cart = p8p_settings_cart_controls(menu->settings, &menu->cart_hash, 1);
        if (cart) *cart = menu->settings->global_controls;
        save_settings(menu);
    } else if (menu->cursor == 9) {
        if (has_override) {
            cart = p8p_settings_cart_controls(menu->settings, &menu->cart_hash, 0);
            p8p_control_profile_defaults(cart);
        } else {
            p8p_control_profile_defaults(&menu->settings->global_controls);
        }
        save_settings(menu);
    } else if (menu->cursor == 10) {
        menu->settings->global_controls = *active;
        save_settings(menu);
    } else if (menu->cursor == 11) {
        p8p_settings_remove_cart_controls(menu->settings, &menu->cart_hash);
        save_settings(menu);
    } else if (menu->cursor == 12) {
        enter_screen(menu, SCREEN_MAIN, main_index(menu, ROW_CONTROLS));
    }
}

static void update_display(p8p_menu_t *menu, uint16_t pressed) {
    const uint16_t change = P8P_PHYS_A | P8P_PHYS_LEFT | P8P_PHYS_RIGHT;
    if ((pressed & P8P_PHYS_B) ||
        ((pressed & P8P_PHYS_A) && menu->cursor == 2)) {
        enter_screen(menu, SCREEN_MAIN, main_index(menu, ROW_DISPLAY));
        return;
    }
    if (menu->cursor == 0 && (pressed & change)) {
        if (pressed & P8P_PHYS_LEFT)
            menu->settings->scale = menu->settings->scale == 0 ? 2 :
                                    menu->settings->scale - 1;
        else
            menu->settings->scale = (menu->settings->scale + 1) % 3;
        save_settings(menu);
    } else if (menu->cursor == 1 && (pressed & change)) {
        menu->settings->pocket_glyphs ^= 1;
        save_settings(menu);
    }
}

static void update_audio(p8p_menu_t *menu, uint16_t pressed) {
    if ((pressed & P8P_PHYS_B) ||
        ((pressed & P8P_PHYS_A) && menu->cursor == 2)) {
        enter_screen(menu, SCREEN_MAIN, main_index(menu, ROW_AUDIO));
        return;
    }
    if (menu->cursor == 0 &&
        (pressed & (P8P_PHYS_LEFT | P8P_PHYS_RIGHT | P8P_PHYS_A))) {
        if (pressed & P8P_PHYS_LEFT)
            menu->settings->volume = menu->settings->volume < 10 ? 0 :
                                     menu->settings->volume - 10;
        else
            menu->settings->volume = menu->settings->volume > 90 ? 100 :
                                     menu->settings->volume + 10;
        save_settings(menu);
    } else if (menu->cursor == 1 && (pressed & P8P_PHYS_A)) {
        menu->settings->muted ^= 1;
        save_settings(menu);
    }
}

static void leave_modal(p8p_menu_t *menu, int modal) {
    int back = menu->modal_return;
    menu->modal = MODAL_NONE;
    menu->modal_return = SCREEN_MAIN;
    if (back == SCREEN_STATES) {
        enter_screen(menu, SCREEN_STATES, menu->modal_slot);
    } else if (back == SCREEN_PAUSE) {
        enter_screen(menu, SCREEN_PAUSE, 0);
        refresh_pause_rows(menu, menu->runtime);
        menu->cursor = pause_index(menu, PAUSE_RESET_CART);
    } else {
        enter_screen(menu, SCREEN_MAIN,
                     main_index(menu, modal == MODAL_RESET ? ROW_RESET :
                                      modal == MODAL_EXIT ? ROW_EXIT :
                                      ROW_SELECT_CART));
    }
}

static enum p8p_menu_action update_modal(p8p_menu_t *menu,
                                         uint16_t pressed) {
    int modal = menu->modal;
    if ((pressed & P8P_PHYS_B) ||
        (modal == MODAL_CART_PICKER && (pressed & P8P_PHYS_A))) {
        leave_modal(menu, modal);
        return P8P_MENU_NONE;
    }
    if (!(pressed & P8P_PHYS_A))
        return P8P_MENU_NONE;
    if (modal == MODAL_RESET)
        return P8P_MENU_RESTART;
    if (modal == MODAL_EXIT)
        return P8P_MENU_EXIT;
    if (modal == MODAL_DELETE) {
        (void)p8p_state_delete(menu->modal_slot, &menu->cart_hash);
        refresh_state(menu, menu->modal_slot);
        leave_modal(menu, modal);
        set_notice(menu, tr(menu, "DELETED", "УДАЛЕНО"));
    }
    return P8P_MENU_NONE;
}

enum p8p_menu_action p8p_menu_update(p8p_menu_t *menu,
                                     p8p_runtime_t *runtime,
                                     uint16_t physical_buttons,
                                     uint16_t pressed_buttons,
                                     uint8_t pico_buttons) {
    uint16_t pressed;
    enum p8p_menu_action action = P8P_MENU_NONE;
    int count = 0;
    int rows[ROW_COUNT];
    (void)pico_buttons;
    if (!menu || !runtime)
        return P8P_MENU_CLOSE;
    menu->runtime = runtime;
    pressed = pressed_buttons |
              (physical_buttons & (uint16_t)~menu->previous_buttons);
    menu->previous_buttons = physical_buttons;

    if ((pressed & P8P_PHYS_SELECT) && menu->capture_action < 0)
        return P8P_MENU_CLOSE;

    /* A and X are both PICO-8's X button in play, so X confirms here too.
     * The save-state screen keeps X for Save, and a control capture takes
     * X as the button to assign. */
    if ((pressed & P8P_PHYS_X) && menu->screen != SCREEN_STATES &&
        menu->capture_action < 0)
        pressed |= P8P_PHYS_A;

    switch (menu->screen) {
    case SCREEN_MAIN: count = main_rows(menu, rows); break;
    case SCREEN_STATES: count = P8P_STATE_SLOT_COUNT; break;
    case SCREEN_CONTROLS: count = 13; break;
    case SCREEN_DISPLAY: count = 3; break;
    case SCREEN_AUDIO: count = 3; break;
    case SCREEN_PAUSE: count = refresh_pause_rows(menu, runtime); break;
    default: count = 0; break;
    }
    if (count && menu->capture_action < 0) {
        if (pressed & P8P_PHYS_UP)
            menu->cursor = menu->cursor == 0 ? count - 1 : menu->cursor - 1;
        if (pressed & P8P_PHYS_DOWN)
            menu->cursor = (menu->cursor + 1) % count;
    }

    switch (menu->screen) {
    case SCREEN_MAIN: action = update_main(menu, runtime, pressed); break;
    case SCREEN_STATES: action = update_states(menu, runtime, pressed); break;
    case SCREEN_CONTROLS: update_controls(menu, pressed); break;
    case SCREEN_DISPLAY: update_display(menu, pressed); break;
    case SCREEN_AUDIO: update_audio(menu, pressed); break;
    case SCREEN_INFO:
        if (pressed & (P8P_PHYS_A | P8P_PHYS_B))
            enter_screen(menu, SCREEN_MAIN, main_index(menu, ROW_INFO));
        break;
    case SCREEN_MODAL: action = update_modal(menu, pressed); break;
    case SCREEN_PAUSE: action = update_pause(menu, runtime, pressed); break;
    }
    draw_menu(menu, runtime);
    return action;
}

const uint8_t *p8p_menu_framebuffer(const p8p_menu_t *menu) {
    return menu ? menu->framebuffer : NULL;
}
