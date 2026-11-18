/* SPDX-License-Identifier: MIT-0
 * Host test of the boot splash (loader/splash.c): the art's pixels, the easter-egg observers, the
 * build-date variant, and splash_handoff on a host buffer (and with no framebuffer at all).
 * Built by tools/host-test.sh. The loader's platform calls are stubbed here.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "splash.h"
#include "plat.h"
#include "policy.h"

/* ---- stubs for the loader's platform and console calls ----------------------------------- */

static struct plat_fb test_fb;
static int no_fb;
static int delays;

const struct plat_fb *plat_fb(void) { return no_fb ? 0 : &test_fb; }
void con_line(const char *s) { printf("  [con] %s\n", s); }
void kputs(const char *s) { printf("%s", s); }
void delay_ms(uint32_t ms) { (void)ms; delays++; }

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define W 1440u
#define H 2960u

static int chan(uint32_t c, int s) { return (int)((c >> s) & 0xffu); }
static int near(uint32_t c, uint32_t want, int tol)
{
	for (int s = 0; s < 24; s += 8)
		if (abs(chan(c, s) - chan(want, s)) > tol)
			return 0;
	return 1;
}

int main(void)
{
	struct splash_geom g, q;
	splash_geometry(W, H, &g);
	splash_geometry(640, 480, &q);
	int cx = g.cx, cy = g.cy;
	CHECK(cx == 720 && cy == (int)(H * SPLASH_CY_PERMILLE / 1000));   /* centred, a little above the middle */
	CHECK(g.R == 331);                                                   /* the orbit is about 46% of the width */

	/* the core: the centre, only once ignited */
	CHECK(splash_pixel(SPL_DEFAULT, W, H, cx, cy, 256) == SPLASH_DEFAULT_CORE);
	CHECK(splash_pixel(SPL_DEFAULT, W, H, cx, cy, 0) != SPLASH_DEFAULT_CORE);
	CHECK(splash_pixel(SPL_GOLD, W, H, cx, cy, 256) != SPLASH_DEFAULT_CORE);   /* palettes differ */
	CHECK(splash_pixel(99, W, H, 0, 0, 256) == splash_pixel(SPL_DEFAULT, W, H, 0, 0, 256));   /* bad variant */
	CHECK(splash_pixel(SPL_DEFAULT, 640, 480, 320, q.cy, 256) == SPLASH_DEFAULT_CORE);   /* a small screen */

	/* the background: near-black at the top, midnight blue at the bottom, within a dither level */
	CHECK(near(splash_pixel(SPL_DEFAULT, W, H, 0, 0, 256), 0xff02040au, 1));
	CHECK(near(splash_pixel(SPL_DEFAULT, W, H, W - 1, H - 1, 256), 0xff0a1a3au, 1));
	/* dithered, not banded: along a row the gradient's fractional part shows as two levels */
	{
		int lo = 255, hi = 0;
		for (uint32_t y = 1400; y < 1404; y++)
			for (uint32_t x = 0; x < 4; x++) {
				int b = chan(splash_pixel(SPL_DEFAULT, W, H, x, y, 256), 0);
				if (b < lo) lo = b;
				if (b > hi) hi = b;
			}
		CHECK(hi - lo <= 1);
	}
	/* the halo: lit around the mark, dark far from it */
	CHECK(chan(splash_pixel(SPL_DEFAULT, W, H, cx - g.R - 40, cy, 256), 0) >
	      chan(splash_pixel(SPL_DEFAULT, W, H, 5, cy, 256), 0) + 8);
	/* the orbit is anti-aliased: across its edge the blue goes up and down through intermediate values */
	{
		int seen[256] = { 0 }, n = 0, peak = 0;
		for (int x = cx - g.R - 20; x < cx - g.R + 20; x++) {
			int b = chan(splash_pixel(SPL_DEFAULT, W, H, (uint32_t)x, (uint32_t)cy, 256), 0);
			if (!seen[b]) { seen[b] = 1; n++; }
			if (b > peak) peak = b;
		}
		CHECK(n >= 8);
		CHECK(peak >= 0xf0 || peak >= 0xe0);                    /* the orbit itself is bright (its blue) */
		CHECK(near(splash_pixel(SPL_DEFAULT, W, H, (uint32_t)(cx - g.R), (uint32_t)cy, 256), 0xff3d8bffu, 24));
	}
	/* the satellite sits on the orbit, in the core's colour */
	CHECK(splash_pixel(SPL_DEFAULT, W, H, (uint32_t)g.sat_x, (uint32_t)g.sat_y, 256) == SPLASH_DEFAULT_CORE);
	/* the wordmark: its I (a vertical stroke) is the text colour at full ignition, absent without it */
	CHECK(splash_pixel(SPL_DEFAULT, W, H, (uint32_t)g.i_x, (uint32_t)g.text_y, 256) == 0xffdfe9fau);
	CHECK(near(splash_pixel(SPL_DEFAULT, W, H, (uint32_t)g.i_x, (uint32_t)g.text_y, 0),
	           splash_pixel(SPL_DEFAULT, W, H, (uint32_t)g.i_x + 60, (uint32_t)g.text_y, 0), 3));
	CHECK(near(splash_pixel(SPL_DEFAULT, W, H, (uint32_t)g.i_x, (uint32_t)(g.text_y - g.text_h), 256),
	           splash_pixel(SPL_DEFAULT, W, H, (uint32_t)g.i_x + 60, (uint32_t)(g.text_y - g.text_h), 256), 3));
	/* the hairline: an empty track at 0, filled to k / 256 of its length */
	{
		uint32_t mid = (uint32_t)(g.hair_x0 + g.hair_len / 2), yy = (uint32_t)g.hair_y;
		uint32_t left = (uint32_t)(g.hair_x0 + g.hair_len / 8), right = (uint32_t)(g.hair_x0 + g.hair_len * 7 / 8);
		CHECK(splash_pixel(SPL_DEFAULT, W, H, mid, yy, 256) == 0xff3d8bffu);
		CHECK(splash_pixel(SPL_DEFAULT, W, H, mid, yy, 0) != 0xff3d8bffu);
		CHECK(splash_pixel(SPL_DEFAULT, W, H, left, yy, 128) == 0xff3d8bffu);
		CHECK(splash_pixel(SPL_DEFAULT, W, H, right, yy, 128) != 0xff3d8bffu);
		CHECK(splash_pixel(SPL_DEFAULT, W, H, mid, yy - 20, 256) == splash_pixel(SPL_DEFAULT, W, H, mid, yy - 21, 256) ||
		      near(splash_pixel(SPL_DEFAULT, W, H, mid, yy - 20, 256), splash_pixel(SPL_DEFAULT, W, H, mid, yy - 21, 256), 1));
	}

	/* the build date */
	CHECK(splash_variant_for_date("Oct 31 2026") == SPL_HALLOWEEN);
	CHECK(splash_variant_for_date("Dec 25 2026") == SPL_WINTER);
	CHECK(splash_variant_for_date("Oct 30 2026") == SPL_DEFAULT);
	CHECK(splash_variant_for_date("Dec  5 2026") == SPL_DEFAULT);
	CHECK(splash_variant_for_date("Nov 18 2026") == SPL_DEFAULT);
	CHECK(splash_variant_for_date("Oct 3") == SPL_DEFAULT);
	CHECK(splash_variant_for_date(NULL) == SPL_DEFAULT);
	CHECK(splash_current() == splash_variant_for_date(__DATE__));   /* no egg yet */

	/* eggs: gold = UP UP DOWN DOWN EDIT */
	splash_key(MK_UP); splash_key(MK_UP); splash_key(MK_DOWN); splash_key(MK_DOWN);
	CHECK(splash_current() != SPL_GOLD);
	splash_key(MK_EDIT);
	CHECK(splash_current() == SPL_GOLD);
	/* a non-egg key resets the history */
	splash_key(MK_SELECT);
	splash_key(MK_UP); splash_key(MK_DOWN); splash_key(MK_UP); splash_key(MK_DOWN); splash_key(MK_UP);
	CHECK(splash_current() == SPL_EMBER);                                        /* the last egg wins */
	/* mono: both volume keys held for one second */
	splash_held(PK_UP | PK_DOWN, 1000);
	CHECK(splash_current() == SPL_EMBER);
	splash_held(PK_UP | PK_DOWN, 1999);
	CHECK(splash_current() == SPL_EMBER);
	splash_held(PK_UP | PK_DOWN, 2000);
	CHECK(splash_current() == SPL_MONO);
	splash_held(PK_UP, 5000);                    /* released: back to a clean state */
	splash_held(PK_UP | PK_DOWN, 6000);
	CHECK(splash_current() == SPL_MONO);         /* still mono until another egg */
	/* bixby held at start: verbose */
	CHECK(splash_verbose_held(PK_EDIT) == 1);
	CHECK(splash_verbose_held(PK_SELECT) == 0);
	CHECK(splash_verbose_held(PK_EDIT | PK_SELECT) == 1);

	/* handoff: no framebuffer -> nothing painted, no crash */
	no_fb = 1;
	splash_handoff();
	CHECK(delays == 0);
	no_fb = 0;

	/* handoff on a host buffer: the whole panel is written, the core ends on the last frame */
	uint32_t *buf = calloc((size_t)640 * 480, 4);
	CHECK(buf != NULL);
	memset(&test_fb, 0, sizeof(test_fb));
	test_fb.base = (uint64_t)(uintptr_t)buf;
	test_fb.width = 640;
	test_fb.height = 480;
	test_fb.stride = 640 * 4;
	test_fb.scale = 1;
	/* the current variant (mono, from the egg above) */
	memset(buf, 0, (size_t)640 * 480 * 4);
	delays = 0;
	splash_handoff();
	CHECK(delays == 3);                                 /* four frames, three pauses */
	CHECK(buf[0] != 0);                                 /* the corner is painted */
	CHECK(buf[(480 - 1) * 640 + 639] != 0);             /* so is the far corner */
	/* the centre holds the core of whatever variant is current */
	unsigned v = splash_current();
	CHECK(buf[(size_t)q.cy * 640 + 320] == splash_pixel(v, 640, 480, 320, (uint32_t)q.cy, 256));
	/* every pixel is the art's, the final frame */
	{
		size_t bad = 0;
		for (uint32_t y = 0; y < 480; y++)
			for (uint32_t x = 0; x < 640; x++)
				if (buf[y * 640 + x] != splash_pixel(v, 640, 480, x, y, 256))
					bad++;
		CHECK(bad == 0);
	}
	free(buf);

	/* the real panel: 1440x2960, stride 5760, the S-Boot leftovers (0x00c0ffee blocks) all overwritten */
	{
		uint32_t *big = malloc((size_t)W * H * 4);
		CHECK(big != NULL);
		for (size_t i = 0; i < (size_t)W * H; i++)
			big[i] = 0x00c0ffeeu;
		memset(&test_fb, 0, sizeof(test_fb));
		test_fb.base = (uint64_t)(uintptr_t)big;
		test_fb.width = W;
		test_fb.height = H;
		test_fb.stride = W * 4;
		test_fb.scale = 5;
		splash_handoff();
		unsigned vv = splash_current();
		size_t bad = 0, left = 0;
		for (uint32_t y = 0; y < H; y++)
			for (uint32_t x = 0; x < W; x++) {
				if (big[(size_t)y * W + x] == 0x00c0ffeeu)
					left++;
				if (big[(size_t)y * W + x] != splash_pixel(vv, W, H, x, y, 256))
					bad++;
			}
		CHECK(left == 0);       /* no leftover pixel anywhere */
		CHECK(bad == 0);        /* and every one is the art's */
		free(big);
	}

	if (fails) {
		printf("splash-test: %d failure(s)\n", fails);
		return 1;
	}
	printf("splash-test: all checks pass\n");
	return 0;
}
