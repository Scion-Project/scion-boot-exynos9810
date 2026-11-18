/* SPDX-License-Identifier: MIT-0
 * The Scion boot splash. Drawn by the loader as the last thing before an XNU entry starts, so the
 * panel shows it while the kernel boots (the kernel's framebuffer console stays quiet unless -v).
 *
 * The art is original and drawn in code: a dark vertical gradient, an outer ring (the orbit), a
 * thin inner ring and a core disc. The core ignites in four steps (about 30 ms): only the core's
 * square is repainted per step, so the animation costs almost nothing. No text, no bitmaps, no
 * third-party artwork. Integer maths only (no floating point, no tables with pointers).
 *
 * Easter eggs (all observers: the menu's keys are passed on unchanged):
 *   gold     Volume Up, Up, Volume Down, Down, Bixby  (in the menu, any time before the start)
 *   ember    Volume Up, Volume Down, Volume Up, Volume Down, Volume Up
 *   mono     Volume Up and Volume Down held together for one second
 *   halloween / winter   the build date (31 Oct, 25 Dec); the loader has no clock to read
 *   verbose  Bixby held as the loader starts an entry: the kernel gets -v (its log on the screen)
 * An egg pressed in the menu wins over the build date; the last one entered wins.
 */
#include <stdint.h>
#include "splash.h"
#include "plat.h"
#include "fbcon.h"
#include "loader.h"
#include "lib.h"

#define SPLASH_FRAME_MS 10u

struct pal {
	uint32_t top, bot;     /* background gradient, top to bottom */
	uint32_t ring, ring2;  /* outer ring (the orbit), inner ring */
	uint32_t core;         /* the core disc */
};

static const struct pal pals[SPL_COUNT] = {
	[SPL_DEFAULT]   = { 0xff06080fu, 0xff0d1a2eu, 0xff1f6feeu, 0xff3b5a86u, SPLASH_DEFAULT_CORE },
	[SPL_GOLD]      = { 0xff0f0c02u, 0xff2a2006u, 0xffd29922u, 0xff7a5a14u, 0xfffff3c4u },
	[SPL_EMBER]     = { 0xff120404u, 0xff2b0c05u, 0xffd0302au, 0xff7a2a1au, 0xffffd27au },
	[SPL_MONO]      = { 0xff000000u, 0xff101010u, 0xff9aa4b2u, 0xff4a4a4au, 0xffffffffu },
	[SPL_HALLOWEEN] = { 0xff0c0700u, 0xff1f1000u, 0xffff7518u, 0xff5a2a0au, 0xffffd080u },
	[SPL_WINTER]    = { 0xff08101cu, 0xff16283fu, 0xff9fd8ffu, 0xff3f6f9au, 0xffffffffu },
};

/* A switch, not a table: a table of string pointers in .data would be an absolute relocation. */
static const char *name_of(unsigned v)
{
	switch (v) {
	case SPL_GOLD:      return "gold";
	case SPL_EMBER:     return "ember";
	case SPL_MONO:      return "mono";
	case SPL_HALLOWEEN: return "halloween";
	case SPL_WINTER:    return "winter";
	default:            return "default";
	}
}

/* Geometry, in pixels, from the panel's size: the mark sits a little above the centre. */
struct geom {
	int32_t cx, cy, r, r_out2, r2, r2_out2, rc, rc2;
	int32_t ring_w, ring2_w;
};

static uint32_t min32(uint32_t a, uint32_t b) { return a < b ? a : b; }

static struct geom geometry(uint32_t w, uint32_t h)
{
	struct geom g;
	uint32_t R = min32(w, h) * 22u / 100u;
	int32_t rw = (int32_t)(R / 9u);
	if (rw < 2)
		rw = 2;
	g.cx = (int32_t)(w / 2u);
	g.cy = (int32_t)(h * 42u / 100u);
	g.r = (int32_t)R;
	g.ring_w = rw;
	g.r_out2 = (int32_t)(R + (uint32_t)rw) * (int32_t)(R + (uint32_t)rw);
	g.r2 = (int32_t)(R * 62u / 100u);
	g.ring2_w = rw / 3 > 1 ? rw / 3 : 1;
	g.r2_out2 = (g.r2 + g.ring2_w) * (g.r2 + g.ring2_w);
	g.rc = (int32_t)(R * 28u / 100u);
	g.rc2 = g.rc * g.rc;
	return g;
}

/* One channel-wise blend of a and b at t/256. */
static uint32_t blend(uint32_t a, uint32_t b, uint32_t t)
{
	uint32_t out = 0;
	for (int s = 0; s < 32; s += 8) {
		int32_t ca = (int32_t)((a >> s) & 0xffu), cb = (int32_t)((b >> s) & 0xffu);
		int32_t c = ca + (((cb - ca) * (int32_t)t) >> 8);
		out |= (uint32_t)c << s;
	}
	return out;
}

static uint32_t row_bg(const struct pal *p, uint32_t y, uint32_t h)
{
	return blend(p->top, p->bot, h > 1 ? y * 256u / (h - 1u) : 0u);
}

/* The pixel at (x, y) given its row's background. core_k in 0..256 scales the core's radius. */
static uint32_t pixel(const struct pal *p, const struct geom *g, uint32_t bg, uint32_t x, uint32_t y,
                      uint32_t core_k)
{
	int32_t dx = (int32_t)x - g->cx, dy = (int32_t)y - g->cy;
	int32_t d2 = dx * dx + dy * dy;
	if (core_k) {
		int32_t rc = g->rc * (int32_t)core_k / 256;
		if (d2 < rc * rc)
			return p->core;
	}
	if (d2 >= g->r * g->r && d2 < g->r_out2)
		return p->ring;
	int32_t lo = g->r2 * g->r2;
	if (d2 >= lo && d2 < g->r2_out2)
		return p->ring2;
	return bg;
}

uint32_t splash_pixel(unsigned v, uint32_t w, uint32_t h, uint32_t x, uint32_t y, uint32_t core_k)
{
	const struct pal *p = &pals[v < SPL_COUNT ? v : SPL_DEFAULT];
	struct geom g = geometry(w, h);
	return pixel(p, &g, row_bg(p, y, h), x, y, core_k);
}

unsigned splash_variant_for_date(const char *d)
{
	if (!d || strlen(d) < 6)
		return SPL_DEFAULT;
	if (d[0] == 'O' && d[1] == 'c' && d[2] == 't' && d[4] == '3' && d[5] == '1')
		return SPL_HALLOWEEN;
	if (d[0] == 'D' && d[1] == 'e' && d[2] == 'c' && d[4] == '2' && d[5] == '5')
		return SPL_WINTER;
	return SPL_DEFAULT;
}

/* ---- easter eggs ------------------------------------------------------------------------- */

static unsigned egg = SPL_COUNT;      /* SPL_COUNT: none entered */
static unsigned hist[5], hist_n;     /* the last menu keys (up, down, edit) */
static int both_on;
static uint64_t both_since;

static void hist_push(unsigned k)
{
	if (hist_n < 5) {
		hist[hist_n++] = k;
	} else {
		for (unsigned i = 0; i < 4; i++)
			hist[i] = hist[i + 1];
		hist[4] = k;
	}
}

static int hist_is(const unsigned *want)
{
	if (hist_n != 5)
		return 0;
	for (unsigned i = 0; i < 5; i++)
		if (hist[i] != want[i])
			return 0;
	return 1;
}

static void egg_set(unsigned v)
{
	egg = v;
	hist_n = 0;
	kputs("egg: ");
	con_line(name_of(v));
}

void splash_key(unsigned k)
{
	static const unsigned gold[5] = { MK_UP, MK_UP, MK_DOWN, MK_DOWN, MK_EDIT };
	static const unsigned ember[5] = { MK_UP, MK_DOWN, MK_UP, MK_DOWN, MK_UP };
	if (k != MK_UP && k != MK_DOWN && k != MK_EDIT) {
		hist_n = 0;
		return;
	}
	hist_push(k);
	if (hist_is(gold))
		egg_set(SPL_GOLD);
	else if (hist_is(ember))
		egg_set(SPL_EMBER);
}

void splash_held(unsigned keys, uint64_t now)
{
	int both = (keys & PK_UP) && (keys & PK_DOWN);
	if (!both) {
		both_on = 0;
		return;
	}
	if (!both_on) {
		both_on = 1;
		both_since = now;
	} else if (now - both_since >= 1000 && egg != SPL_MONO) {
		egg_set(SPL_MONO);
	}
}

int splash_verbose_held(unsigned keys)
{
	return (keys & PK_EDIT) != 0;
}

unsigned splash_current(void)
{
	if (egg < SPL_COUNT)
		return egg;
	const char *d = __DATE__;   /* the build's date, from the image: no clock is read here */
	return splash_variant_for_date(d);
}

/* ---- painting ---------------------------------------------------------------------------- */

/* Paint the rectangle [x0,x1) x [y0,y1) of the framebuffer. */
static void paint_rect(const struct plat_fb *f, const struct pal *p, const struct geom *g,
                       uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t core_k)
{
	for (uint32_t y = y0; y < y1; y++) {
		volatile uint32_t *line = (volatile uint32_t *)(uintptr_t)(f->base + (uint64_t)y * f->stride);
		uint32_t bg = row_bg(p, y, f->height);
		for (uint32_t x = x0; x < x1; x++)
			line[x] = pixel(p, g, bg, x, y, core_k);
	}
}

static void fmt_dim(char *b, unsigned *n, uint32_t v)
{
	char d[12];
	unsigned k = 0;
	do {
		d[k++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	while (k)
		b[(*n)++] = d[--k];
}

void splash_handoff(void)
{
	const struct plat_fb *f = plat_fb();
	if (!f || !f->base || !f->width || !f->height || !f->stride)
		return;
	unsigned v = splash_current();
	const struct pal *p = &pals[v];
	struct geom g = geometry(f->width, f->height);
	/* The rectangle around the core: the core's radius never reaches its edge. */
	uint32_t P = (uint32_t)g.rc + 2u;
	uint32_t x0 = (uint32_t)(g.cx - (int32_t)P), x1 = (uint32_t)(g.cx + (int32_t)P);
	uint32_t y0 = (uint32_t)(g.cy - (int32_t)P), y1 = (uint32_t)(g.cy + (int32_t)P);
	paint_rect(f, p, &g, 0, 0, f->width, f->height, 0);
	static const uint32_t steps[4] = { 64, 128, 192, 256 };
	for (unsigned s = 0; s < 4; s++) {
		paint_rect(f, p, &g, x0, y0, x1, y1, steps[s]);
		if (s < 3)
			delay_ms(SPLASH_FRAME_MS);
	}
	kputs("splash: ");
	kputs(name_of(v));
	kputs(", ");
	char b[24];
	unsigned n = 0;
	fmt_dim(b, &n, f->width);
	b[n++] = 'x';
	fmt_dim(b, &n, f->height);
	b[n] = 0;
	con_line(b);
}
