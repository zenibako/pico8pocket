#ifndef P8P_MENU_H
#define P8P_MENU_H

#include "p8p/runtime.h"
#include "p8p/settings.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct p8p_menu p8p_menu_t;

enum p8p_menu_action {
    P8P_MENU_NONE = 0,
    P8P_MENU_CLOSE,
    P8P_MENU_RESTART,
    P8P_MENU_EXIT,
    /* A cart menuitem() callback raised an error; see p8p_runtime_error. */
    P8P_MENU_CART_ERROR
};

p8p_menu_t *p8p_menu_create(p8p_settings_t *settings,
                            const p8p_cart_hash_t *cart_hash,
                            const char *cart_kind);
void p8p_menu_destroy(p8p_menu_t *menu);
/* cart_running: show the cart's menuitem() entries (not after an error). */
void p8p_menu_open(p8p_menu_t *menu, p8p_runtime_t *runtime,
                   uint16_t physical_buttons, int cart_running);
/* Opens the cart menu (PICO-8's pause menu) over the undimmed game.  Its
 * buttons match Select's: A or X confirm, B closes, and the cart's pause
 * button closes it too.  pico_buttons is unused. */
void p8p_menu_open_pause(p8p_menu_t *menu, p8p_runtime_t *runtime,
                         uint16_t physical_buttons, uint8_t pico_buttons);
enum p8p_menu_action p8p_menu_update(p8p_menu_t *menu,
                                     p8p_runtime_t *runtime,
                                     uint16_t physical_buttons,
                                     uint16_t pressed_buttons,
                                     uint8_t pico_buttons);  /* unused */
const uint8_t *p8p_menu_framebuffer(const p8p_menu_t *menu);
/* A menu character as 15 bits: five rows of three, top left in bit 14. */
uint16_t p8p_menu_glyph(uint32_t character);
/* Draws UTF-8 menu text into a 128-pixel-wide framebuffer: PICO-8 letters,
 * Cyrillic, and the button glyphs of p8p/pocket_glyphs.h. */
void p8p_menu_draw_text(uint8_t *framebuffer, int x, int y, const char *text,
                        uint8_t color);
int p8p_menu_text_width(const char *text);
/* The glyph (font rows) for the first Pocket button in a P8P_PHYS_* mask,
 * or NULL when it holds none; used for carts' O and X prompts. */
const uint8_t *p8p_menu_button_glyph(uint16_t buttons);

#ifdef __cplusplus
}
#endif

#endif
