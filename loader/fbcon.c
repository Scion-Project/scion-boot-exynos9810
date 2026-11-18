/* SPDX-License-Identifier: MIT
 * Text on the framebuffer: see fbcon.h. The glyphs are stage 1's (src/platform/font8x8.h).
 */
#include <stdint.h>
#include "fbcon.h"
#include "plat.h"
#include "font8x8.h"

static const struct plat_fb *fb;

void fb_init(void)
{
	fb = plat_fb();
}

int fb_ok(void)
{
	return fb != 0;
}

unsigned fb_cols(void)
{
	return fb ? (fb->width - 2 * fb->inset_x) / (8 * fb->scale) : 0;
}

unsigned fb_rows(void)
{
	return fb ? (fb->height - 2 * fb->inset_y) / (8 * fb->scale) : 0;
}

static void fill(uint32_t y0, uint32_t y1, uint32_t px)
{
	for (uint32_t y = y0; y < y1 && y < fb->height; y++) {
		volatile uint32_t *line = (volatile uint32_t *)(uintptr_t)(fb->base + (uint64_t)y * fb->stride);
		for (uint32_t x = 0; x < fb->width; x++)
			line[x] = px;
	}
}

void fb_clear(uint32_t bg)
{
	if (fb)
		fill(0, fb->height, bg);
}

void fb_fill_rows(unsigned row, unsigned n, uint32_t bg)
{
	if (fb)
		fill(fb->inset_y + row * 8 * fb->scale, fb->inset_y + (row + n) * 8 * fb->scale, bg);
}

void fb_text(unsigned col, unsigned row, const char *s, uint32_t fg, uint32_t bg)
{
	if (!fb || row >= fb_rows())
		return;
	unsigned sc = fb->scale, cols = fb_cols();
	for (; *s && col < cols; s++, col++) {
		unsigned c = (unsigned char)*s;
		const uint8_t *g = font8x8[(c >= 0x20 && c <= 0x7e) ? c - 0x20 : '?' - 0x20];
		for (unsigned gy = 0; gy < 8 * sc; gy++) {
			uint8_t bits = g[gy / sc];
			volatile uint32_t *line = (volatile uint32_t *)(uintptr_t)(fb->base +
				(uint64_t)(fb->inset_y + row * 8 * sc + gy) * fb->stride) + fb->inset_x + col * 8 * sc;
			for (unsigned gx = 0; gx < 8 * sc; gx++)
				line[gx] = (bits & (0x80u >> (gx / sc))) ? fg : bg;
		}
	}
}
