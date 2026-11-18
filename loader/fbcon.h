/* SPDX-License-Identifier: MIT
 * Text on the framebuffer (fbcon.c): a grid of 8x8 glyphs drawn `scale` times larger. */
#ifndef SCION_FBCON_H
#define SCION_FBCON_H

#include <stdint.h>

#define FB_BLACK  0xff000000u
#define FB_WHITE  0xffffffffu
#define FB_GREY   0xff9aa4b2u
#define FB_RED    0xffd0302au
#define FB_GREEN  0xff2ea043u
#define FB_BLUE   0xff1f6feeu
#define FB_AMBER  0xffd29922u
#define FB_DARK   0xff0d1117u

void fb_init(void);
int fb_ok(void);
unsigned fb_cols(void);
unsigned fb_rows(void);
void fb_clear(uint32_t bg);
void fb_fill_rows(unsigned row, unsigned n, uint32_t bg);
/* Text at (col,row) of the grid, clipped at the right edge. */
void fb_text(unsigned col, unsigned row, const char *s, uint32_t fg, uint32_t bg);

#endif
