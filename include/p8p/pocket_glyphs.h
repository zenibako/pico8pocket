/* Analogue Pocket button glyphs, drawn for the PICO-8 font.
 *
 * The codepoints match Butterscotch Pocket's prompts
 * (src/openfpga/platform/of_buttons.h in zenibako/Butterscotch), so every
 * Pocket port names a button with the same character.  U+E00D (the Analogue
 * button) is new here.  As in Butterscotch, the face buttons are four
 * separate circles in a diamond with the pressed one filled, and the small
 * buttons below the screen are a row of three circles (Select, Analogue,
 * Start) with the pressed one filled.  The shoulders fill the one meant.
 *
 * Each glyph is 9x7 (the small-button row 11x7): seven rows, bit n of a
 * row is column n.  Row 0 sits one
 * pixel above the text line, so a glyph spans the gaps above and below a
 * line of PICO-8's 5-pixel letters. */
#ifndef P8P_POCKET_GLYPHS_H
#define P8P_POCKET_GLYPHS_H

#include <stdint.h>

/* UTF-8 for menu strings. */
#define P8P_ICON_A      "\xee\x80\x80"
#define P8P_ICON_B      "\xee\x80\x81"
#define P8P_ICON_X      "\xee\x80\x82"
#define P8P_ICON_Y      "\xee\x80\x83"
#define P8P_ICON_START  "\xee\x80\x84"
#define P8P_ICON_SELECT "\xee\x80\x85"
#define P8P_ICON_L      "\xee\x80\x86"
#define P8P_ICON_R      "\xee\x80\x87"
#define P8P_ICON_DPAD   "\xee\x80\x88"
#define P8P_ICON_UP     "\xee\x80\x89"
#define P8P_ICON_DOWN   "\xee\x80\x8a"
#define P8P_ICON_LEFT   "\xee\x80\x8b"
#define P8P_ICON_RIGHT  "\xee\x80\x8c"
#define P8P_ICON_HOME   "\xee\x80\x8d"
/* P8SCII's wide characters (128-255) are U+E000 + n in menu strings, so
 * menus can name PICO-8's O and X buttons with the glyphs carts use. */
#define P8P_ICON_P8_O   "\xee\x82\x8e"
#define P8P_ICON_P8_X   "\xee\x82\x97"

enum p8p_pocket_glyph {
    P8P_GLYPH_A, P8P_GLYPH_B, P8P_GLYPH_X, P8P_GLYPH_Y,
    P8P_GLYPH_START, P8P_GLYPH_SELECT, P8P_GLYPH_L, P8P_GLYPH_R,
    P8P_GLYPH_DPAD, P8P_GLYPH_UP, P8P_GLYPH_DOWN, P8P_GLYPH_LEFT,
    P8P_GLYPH_RIGHT, P8P_GLYPH_HOME,
    P8P_GLYPH_COUNT
};

#define P8P_GLYPH_FIRST 0xe000u
#define P8P_GLYPH_P8SCII_FIRST 0xe080u

#ifdef __cplusplus
extern "C" {
#endif

#define P8P_GLYPH_WIDTH 9
/* Select, Analogue and Start are a row of three circles, 11 wide. */
#define P8P_GLYPH_MAX_WIDTH 11
#define P8P_GLYPH_HEIGHT 7

/* Defined in src/app/menu.c. */
extern const uint16_t p8p_pocket_glyphs[P8P_GLYPH_COUNT][P8P_GLYPH_HEIGHT];

#ifdef __cplusplus
}
#endif

#endif
