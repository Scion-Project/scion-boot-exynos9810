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

#define RING   0xff1f6feeu
#define RING2  0xff3b5a86u
#define TOP    0xff06080fu

int main(void)
{
	/* the art: the centre is the core, the orbit and the inner ring are where they should be */
	uint32_t cx = W / 2, cy = H * 42u / 100u;
	CHECK(splash_pixel(SPL_DEFAULT, W, H, cx, cy, 256) == SPLASH_DEFAULT_CORE);
	CHECK(splash_pixel(SPL_DEFAULT, W, H, cx, cy, 0) != SPLASH_DEFAULT_CORE);   /* no core at k=0 */
	CHECK(splash_pixel(SPL_DEFAULT, W, H, cx + 330, cy, 256) == RING);           /* the orbit */
	CHECK(splash_pixel(SPL_DEFAULT, W, H, cx + 200, cy, 256) == RING2);          /* the inner ring */
	CHECK(splash_pixel(SPL_DEFAULT, W, H, cx + 250, cy, 256) != RING);           /* between them */
	CHECK(splash_pixel(SPL_DEFAULT, W, H, 0, 0, 256) == TOP);                   /* top-left corner */
	CHECK(splash_pixel(SPL_GOLD, W, H, cx, cy, 256) != SPLASH_DEFAULT_CORE);    /* palettes differ */
	CHECK(splash_pixel(99, W, H, 0, 0, 256) == TOP);                            /* bad variant: default */
	/* the same art on a small screen (QEMU's 640x480) keeps the mark centred */
	CHECK(splash_pixel(SPL_DEFAULT, 640, 480, 320, 201, 256) == SPLASH_DEFAULT_CORE);

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
	CHECK(buf[201 * 640 + 320] == splash_pixel(v, 640, 480, 320, 201, 256));
	/* no text: every pixel is the art's (no glyph colour in the default palette's text white) */
	{
		size_t bad = 0;
		for (uint32_t y = 0; y < 480; y++)
			for (uint32_t x = 0; x < 640; x++)
				if (buf[y * 640 + x] != splash_pixel(v, 640, 480, x, y, 256))
					bad++;
		CHECK(bad == 0);
	}
	free(buf);

	if (fails) {
		printf("splash-test: %d failure(s)\n", fails);
		return 1;
	}
	printf("splash-test: all checks pass\n");
	return 0;
}
