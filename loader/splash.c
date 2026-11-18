/* SPDX-License-Identifier: MIT-0
 * The Scion boot splash. Drawn by the loader as the last thing before an XNU entry starts, so the
 * panel shows it while the kernel boots (the kernel's framebuffer console stays quiet unless -v,
 * and the GPU self-test no longer draws on the panel).
 *
 * The art is original and drawn in code, on the panel's own width, height and stride (plat_fb()),
 * never a fixed size. From the back to the front:
 *   - a deep near-black to midnight-blue vertical gradient over the whole panel, ordered-dithered
 *     (2x2 Bayer) so the dark ramp has no visible bands;
 *   - a soft halo around the mark and a tighter bloom around the core (quadratic fall-offs);
 *   - the mark, centred a little above the middle: a thin inner ring, the orbit (brighter towards
 *     its satellite) with a small satellite on it, and the core, a disc with a radial gradient;
 *   - "SCION" in clean, widely tracked geometric capitals under the mark. The glyphs are original
 *     vector strokes (segments and circular arcs), no font, no licence;
 *   - a progress hairline near the bottom.
 * Every edge is anti-aliased from a signed distance in 1/16 pixel (integer maths, one integer
 * square root only where an edge is near). The background is one full fill (a colour per row,
 * 64-bit stores); the per-pixel art is only evaluated inside the bounding boxes of the halo, the
 * wordmark and the hairline, about a million pixels on the S9's 1440x2960. The ignition is four
 * frames (about 30 ms): the core grows, the wordmark fades in and the hairline advances; only the
 * core's square, the wordmark's box and the hairline's box are repainted per frame. No floating
 * point, no tables with pointers, no bitmaps, no third-party artwork.
 *
 * Easter eggs (all observers: the menu's keys are passed on unchanged):
 *   gold     Volume Up, Up, Volume Down, Down, Bixby  (in the menu, any time before the start)
 *   ember    Volume Up, Volume Down, Volume Up, Volume Down, Volume Up
 *   mono     Volume Up and Volume Down held together for one second
 *   halloween / winter   the build date (31 Oct, 25 Dec); the loader has no clock to read
 *   verbose  Bixby held as the loader starts an entry: the kernel gets -v (its log on the screen)
 * An egg pressed in the menu wins over the build date; the last one entered wins. Each one is a
 * palette of the same design.
 */
#pragma GCC optimize("Os")
#include <stdint.h>
#include "splash.h"
#include "plat.h"
#include "fbcon.h"
#include "loader.h"
#include "lib.h"

#define SPLASH_FRAME_MS 10u

struct pal {
	uint32_t top, bot;   /* background gradient, top to bottom */
	uint32_t glow;       /* the halo around the mark (added light) */
	uint32_t ring;       /* the orbit */
	uint32_t hi;         /* the orbit near its satellite */
	uint32_t ring2;      /* the inner ring */
	uint32_t core;       /* the core, at its centre */
	uint32_t edge;       /* the core, at its rim */
	uint32_t text;       /* the wordmark, the hairline's track */
};

static const struct pal pals[SPL_COUNT] = {
	[SPL_DEFAULT]   = { 0xff02040au, 0xff0a1a3au, 0xff1446b4u, 0xff3d8bffu, 0xffa8d0ffu, 0xff4a6fa8u,
	                    SPLASH_DEFAULT_CORE, 0xff9cc2ffu, 0xffdfe9fau },
	[SPL_GOLD]      = { 0xff0a0801u, 0xff241a05u, 0xff8a5c0au, 0xffd29922u, 0xffffe08au, 0xff8a6a1cu,
	                    0xfffff3c4u, 0xffe6b84au, 0xfff3e2b0u },
	[SPL_EMBER]     = { 0xff0d0303u, 0xff2a0a05u, 0xff9a2a10u, 0xffe0402au, 0xffffa070u, 0xff8a3020u,
	                    0xffffd27au, 0xffff8a3au, 0xffffd9b8u },
	[SPL_MONO]      = { 0xff000000u, 0xff101010u, 0xff505050u, 0xff9aa4b2u, 0xffffffffu, 0xff5a5a5au,
	                    0xffffffffu, 0xffbcbcbcu, 0xffe6e6e6u },
	[SPL_HALLOWEEN] = { 0xff080400u, 0xff1e0e00u, 0xff883000u, 0xffff7518u, 0xffffc070u, 0xff7a3a10u,
	                    0xffffd080u, 0xffff8a20u, 0xffffc890u },
	[SPL_WINTER]    = { 0xff040a16u, 0xff10284au, 0xff2a6aa8u, 0xff9fd8ffu, 0xffffffffu, 0xff5a8ab8u,
	                    0xffffffffu, 0xffbfe4ffu, 0xffe8f4ffu },
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

/* ---- geometry ---------------------------------------------------------------------------- */

struct lay {
	int32_t cx, cy, R;             /* the mark's centre, the orbit's centreline radius */
	int32_t ow16;                  /* the orbit's stroke width, 1/16 px */
	int32_t r2, w2_16;             /* the inner ring: centreline radius, stroke width (1/16 px) */
	int32_t rc;                    /* the core's radius, fully ignited */
	int32_t rg, rg2;               /* the halo's radius and its square */
	int32_t gr2;                   /* the bloom's radius squared */
	uint64_t rg_inv, gr_inv;       /* 2^40 / rg2, 2^40 / gr2 */
	int32_t sx, sy, rs;            /* the satellite */
	int32_t H, ts, ty;             /* the wordmark: cap height, stroke, centre line */
	int32_t gx[5], gw[5];          /* the glyphs' left edges and widths */
	int32_t hx0, hlen, hy, hth;    /* the hairline */
};

static uint32_t min32(uint32_t a, uint32_t b) { return a < b ? a : b; }
static int32_t max32(int32_t a, int32_t b) { return a > b ? a : b; }

static void layout(struct lay *g, uint32_t w, uint32_t h)
{
	int32_t u = (int32_t)min32(w, h);
	g->cx = (int32_t)(w / 2u);
	g->cy = (int32_t)(h * SPLASH_CY_PERMILLE / 1000u);
	g->R = u * 23 / 100;
	g->ow16 = max32(32, g->R * 16 / 26);
	g->r2 = g->R * 80 / 100;
	g->w2_16 = max32(1, u / 480) * 16;
	g->rc = g->R * 46 / 100;
	g->rg = g->R * 145 / 100;
	g->rg2 = g->rg * g->rg;
	g->gr2 = (g->R * 90 / 100) * (g->R * 90 / 100);
	g->rg_inv = (1ull << 40) / (uint64_t)g->rg2;
	g->gr_inv = (1ull << 40) / (uint64_t)g->gr2;
	g->sx = g->cx + g->R * 743 / 1000;
	g->sy = g->cy - g->R * 669 / 1000;
	g->rs = max32(3, g->R / 11);
	/* the wordmark */
	g->H = max32(10, u * 48 / 1000);
	g->ts = max32(2, g->H / 8);
	g->ty = g->cy + g->R + u * 15 / 100;
	int32_t gap = g->H * 60 / 100;
	g->gw[0] = 2 * ((g->H - g->ts) / 4) + g->ts;      /* S */
	g->gw[1] = g->H;                                   /* C */
	g->gw[2] = g->ts;                                  /* I */
	g->gw[3] = g->H;                                   /* O */
	g->gw[4] = g->H * 78 / 100;                        /* N */
	int32_t total = 4 * gap;
	for (int i = 0; i < 5; i++)
		total += g->gw[i];
	int32_t x = g->cx - total / 2;
	for (int i = 0; i < 5; i++) {
		g->gx[i] = x;
		x += g->gw[i] + gap;
	}
	/* the hairline */
	g->hth = max32(2, u / 480);
	g->hlen = u * 36 / 100;
	g->hx0 = g->cx - g->hlen / 2;
	g->hy = (int32_t)(h * 93u / 100u);
}

void splash_geometry(uint32_t w, uint32_t h, struct splash_geom *o)
{
	struct lay g;
	layout(&g, w, h);
	o->cx = g.cx; o->cy = g.cy; o->R = g.R; o->rc = g.rc;
	o->text_y = g.ty; o->i_x = g.gx[2] + g.gw[2] / 2; o->text_h = g.H;
	o->hair_x0 = g.hx0; o->hair_len = g.hlen; o->hair_y = g.hy;
	o->sat_x = g.sx; o->sat_y = g.sy;
}

/* ---- maths ------------------------------------------------------------------------------- */

static uint32_t isqrt32(uint32_t v)
{
	uint32_t r = 0, b = 1u << 30;
	while (b > v)
		b >>= 2;
	while (b) {
		if (v >= r + b) {
			v -= r + b;
			r = (r >> 1) + b;
		} else {
			r >>= 1;
		}
		b >>= 2;
	}
	return r;
}

/* One channel-wise blend of a and b at t/256 (t = 256 gives b exactly). */
static uint32_t blend(uint32_t a, uint32_t b, uint32_t t)
{
	uint32_t out = 0xff000000u;
	for (int s = 0; s < 24; s += 8) {
		int32_t ca = (int32_t)((a >> s) & 0xffu), cb = (int32_t)((b >> s) & 0xffu);
		int32_t c = ca + (((cb - ca) * (int32_t)t) >> 8);
		out |= (uint32_t)c << s;
	}
	return out;
}

/* Coverage 0..256 of a shape whose signed distance to its edge is sd16 (1/16 px, negative inside). */
static uint32_t cover(int32_t sd16)
{
	int32_t c = 128 - sd16 * 16;
	return c < 0 ? 0u : c > 256 ? 256u : (uint32_t)c;
}

static uint32_t over(uint32_t dst, uint32_t src, uint32_t cov)
{
	return cov ? blend(dst, src, cov) : dst;
}

/* ---- the background ---------------------------------------------------------------------- */

/* The gradient at row y, per channel (R, G, B) in 8.8 fixed point; eased so the top stays dark. */
static void row_v8(const struct pal *p, uint32_t y, uint32_t h, int32_t *v)
{
	uint32_t t = h > 1 ? (uint32_t)((uint64_t)y * 65536u / (h - 1u)) : 0u;     /* 0..65536 */
	uint32_t te = (t + (uint32_t)(((uint64_t)t * t) >> 16)) >> 1;
	for (int i = 0; i < 3; i++) {
		int s = 16 - 8 * i;
		int32_t a = (int32_t)((p->top >> s) & 0xffu), b = (int32_t)((p->bot >> s) & 0xffu);
		v[i] = (a << 8) + (((b - a) * (int32_t)te) >> 8);
	}
}

/* 8.8 colour to a pixel, ordered-dithered by a 2x2 Bayer matrix at (x, y). */
static uint32_t dither(const int32_t *c, uint32_t x, uint32_t y)
{
	int32_t thr = (y & 1u) ? ((x & 1u) ? 96 : 224) : ((x & 1u) ? 160 : 32);
	uint32_t out = 0xff000000u;
	for (int i = 0; i < 3; i++) {
		int32_t v = c[i] < 0 ? 0 : c[i];
		int32_t b = (v >> 8) + ((v & 255) > thr);
		if (b > 255)
			b = 255;
		out |= (uint32_t)b << (16 - 8 * i);
	}
	return out;
}

/* ---- the wordmark ------------------------------------------------------------------------ */

static uint32_t dist_pt(int32_t dx, int32_t dy)
{
	return isqrt32((uint32_t)(dx * dx + dy * dy));
}

/* Distance from p to the segment a-b (1/16 px). */
static uint32_t dist_seg(int32_t px, int32_t py, int32_t ax, int32_t ay, int32_t bx, int32_t by)
{
	int64_t abx = bx - ax, aby = by - ay, apx = px - ax, apy = py - ay;
	int64_t l2 = abx * abx + aby * aby;
	int64_t t = l2 ? apx * abx + apy * aby : 0;
	if (t < 0)
		t = 0;
	if (t > l2)
		t = l2;
	int32_t qx = (int32_t)(ax + (l2 ? abx * t / l2 : 0)), qy = (int32_t)(ay + (l2 ? aby * t / l2 : 0));
	return dist_pt(px - qx, py - qy);
}

static uint32_t abs_diff(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }

/* The S's two arcs: a circle of radius r with the quarter-ish wedge from -30 to 90 degrees removed;
 * (dx, dy) is relative to the circle's centre. The ends are the wedge's two edges. */
static uint32_t s_arc(int32_t dx, int32_t dy, int32_t r)
{
	if (dx > 0 && dy * 1000 > -577 * dx) {
		uint32_t a = dist_pt(dx - r * 866 / 1000, dy + r / 2), b = dist_pt(dx, dy - r);
		return a < b ? a : b;
	}
	return abs_diff(dist_pt(dx, dy), (uint32_t)r);
}

/* Distance (1/16 px) from (X, Y), in 1/16 px, to the centreline of glyph i of "SCION". */
static uint32_t glyph_dist(const struct lay *L, int i, int32_t X, int32_t Y)
{
	int32_t dx = X - (2 * L->gx[i] + L->gw[i]) * 8, dy = Y - L->ty * 16;
	int32_t hh = (L->H - L->ts) * 8;             /* half the centreline height */
	switch (i) {
	case 0: {                                     /* S: two stacked arcs */
		int32_t r = (L->H - L->ts) * 4;
		uint32_t a = s_arc(dx, dy + r, r), b = s_arc(-dx, -(dy - r), r);
		return a < b ? a : b;
	}
	case 1: {                                     /* C: a ring open on the right */
		int32_t r = hh;
		int32_t ady = dy < 0 ? -dy : dy;
		if (dx > 0 && ady * 100 < dx * 84) {
			uint32_t a = dist_pt(dx - r * 766 / 1000, dy - r * 643 / 1000);
			uint32_t b = dist_pt(dx - r * 766 / 1000, dy + r * 643 / 1000);
			return a < b ? a : b;
		}
		return abs_diff(dist_pt(dx, dy), (uint32_t)r);
	}
	case 2:                                       /* I */
		return dist_seg(dx, dy, 0, -hh, 0, hh);
	case 3:                                       /* O */
		return abs_diff(dist_pt(dx, dy), (uint32_t)hh);
	default: {                                    /* N */
		int32_t hw = (L->gw[4] - L->ts) * 8;
		uint32_t a = dist_seg(dx, dy, -hw, hh, -hw, -hh);
		uint32_t b = dist_seg(dx, dy, -hw, -hh, hw, hh);
		uint32_t c = dist_seg(dx, dy, hw, hh, hw, -hh);
		if (b < a)
			a = b;
		return c < a ? c : a;
	}
	}
}

static int32_t text_x0(const struct lay *L) { return L->gx[0] - L->ts - 2; }
static int32_t text_x1(const struct lay *L) { return L->gx[4] + L->gw[4] + L->ts + 2; }
static int32_t text_y0(const struct lay *L) { return L->ty - L->H / 2 - L->ts - 2; }
static int32_t text_y1(const struct lay *L) { return L->ty + L->H / 2 + L->ts + 2; }

static uint32_t text_cover(const struct lay *L, int32_t x, int32_t y)
{
	int32_t X = x * 16 + 8, Y = y * 16 + 8;
	uint32_t best = 0xffffffu;
	for (int i = 0; i < 5; i++) {
		if (x < L->gx[i] - L->ts - 2 || x > L->gx[i] + L->gw[i] + L->ts + 2)
			continue;
		uint32_t d = glyph_dist(L, i, X, Y);
		if (d < best)
			best = d;
	}
	return best == 0xffffffu ? 0u : cover((int32_t)best - L->ts * 8);
}

/* ---- one pixel --------------------------------------------------------------------------- */

/* The pixel at (x, y), given its row's background v8. k in 0..256 is the ignition: the core's
 * radius, the wordmark's opacity and the hairline's progress, all k / 256. */
static uint32_t pixel_row(const struct pal *p, const struct lay *L, const int32_t *v8, uint32_t x,
                          uint32_t y, uint32_t k)
{
	int32_t dx = (int32_t)x - L->cx, dy = (int32_t)y - L->cy;
	int32_t d2 = dx * dx + dy * dy;
	int32_t c[3] = { v8[0], v8[1], v8[2] };
	if (d2 < L->rg2) {                                 /* the halo, then the bloom, as added light */
		uint32_t u = (uint32_t)(((uint64_t)(uint32_t)(L->rg2 - d2) * L->rg_inv) >> 24);
		uint32_t g = (u * u) >> 16;
		for (int i = 0; i < 3; i++)
			c[i] += (int32_t)((((p->glow >> (16 - 8 * i)) & 0xffu) * g) >> 8);
		if (d2 < L->gr2) {
			u = (uint32_t)(((uint64_t)(uint32_t)(L->gr2 - d2) * L->gr_inv) >> 24);
			g = (u * u) >> 16;
			for (int i = 0; i < 3; i++)
				c[i] += (int32_t)(((((p->core >> (16 - 8 * i)) & 0xffu) * g >> 8) * 77u) >> 8);
		}
	}
	uint32_t col = dither(c, x, y);

	int32_t R = L->R;
	int32_t out_r = R + L->ow16 / 32 + 2;
	if (d2 < out_r * out_r) {
		/* the inner ring: a hairline at 70% */
		int32_t lo = L->r2 - L->w2_16 / 32 - 2, hi = L->r2 + L->w2_16 / 32 + 2;
		if (d2 >= lo * lo && d2 < hi * hi) {
			int32_t r16 = (int32_t)isqrt32((uint32_t)d2 << 8);
			int32_t sd = (r16 > L->r2 * 16 ? r16 - L->r2 * 16 : L->r2 * 16 - r16) - L->w2_16 / 2;
			col = over(col, p->ring2, cover(sd) * 180u >> 8);
		}
		/* the orbit, brighter towards the satellite */
		lo = R - L->ow16 / 32 - 2;
		if (d2 >= lo * lo) {
			int32_t r16 = (int32_t)isqrt32((uint32_t)d2 << 8);
			int32_t sd = (r16 > R * 16 ? r16 - R * 16 : R * 16 - r16) - L->ow16 / 2;
			uint32_t cv = cover(sd);
			if (cv) {
				int32_t sdx = (int32_t)x - L->sx, sdy = (int32_t)y - L->sy;
				int64_t t = (int64_t)(sdx * sdx + sdy * sdy) * 256 / (4 * (int64_t)R * R);
				col = over(col, blend(p->hi, p->ring, t > 256 ? 256u : (uint32_t)t), cv);
			}
		}
	}
	/* the satellite: a soft disc, then the dot */
	{
		int32_t sdx = (int32_t)x - L->sx, sdy = (int32_t)y - L->sy, rs = L->rs;
		int32_t s2 = sdx * sdx + sdy * sdy, ro = rs * 19 / 10 + 2;
		if (s2 < ro * ro) {
			int32_t r16 = (int32_t)isqrt32((uint32_t)s2 << 8);
			col = over(col, p->ring, cover(r16 - (rs * 19 / 10) * 16) * 40u >> 8);
			col = over(col, p->core, cover(r16 - rs * 16));
		}
	}
	/* the core: a disc with a radial gradient, ignited by k */
	if (k) {
		int32_t kr = L->rc * (int32_t)k / 256;
		if (kr > 0 && d2 < (kr + 1) * (kr + 1)) {
			int32_t r16 = (int32_t)isqrt32((uint32_t)d2 << 8);
			uint32_t t = (uint32_t)((int64_t)d2 * 256 / ((int64_t)kr * kr));
			uint32_t cv = cover(r16 - kr * 16);
			col = over(col, blend(p->core, p->edge, t > 256 ? 256u : t), cv);
		}
	}
	/* the wordmark, fading in with k */
	if (k && (int32_t)y >= text_y0(L) && (int32_t)y < text_y1(L) && (int32_t)x >= text_x0(L) &&
	    (int32_t)x < text_x1(L)) {
		uint32_t cv = text_cover(L, (int32_t)x, (int32_t)y);
		col = over(col, p->text, cv * k >> 8);
	}
	/* the hairline: a faint track, and the fill k / 256 of the way along it */
	if ((int32_t)y >= L->hy - L->hth - 2 && (int32_t)y < L->hy + L->hth + 3 && (int32_t)x >= L->hx0 - L->hth - 2 &&
	    (int32_t)x < L->hx0 + L->hlen + L->hth + 2) {
		int32_t X = (int32_t)x * 16 + 8, Y = (int32_t)y * 16 + 8, yc = L->hy * 16 + 8;
		int32_t a = L->hx0 * 16 + L->hth * 8, b = (L->hx0 + L->hlen) * 16 - L->hth * 8;
		int32_t rad = L->hth * 8;
		col = over(col, p->text, cover((int32_t)dist_seg(X, Y, a, yc, b, yc) - rad) * 36u >> 8);
		if (k) {
			int32_t f = a + (int32_t)((int64_t)(b - a) * (int32_t)k / 256);
			col = over(col, p->ring, cover((int32_t)dist_seg(X, Y, a, yc, f, yc) - rad));
		}
	}
	return col;
}

uint32_t splash_pixel(unsigned v, uint32_t w, uint32_t h, uint32_t x, uint32_t y, uint32_t core_k)
{
	const struct pal *p = &pals[v < SPL_COUNT ? v : SPL_DEFAULT];
	struct lay L;
	int32_t v8[3];
	layout(&L, w, h);
	row_v8(p, y, h, v8);
	return pixel_row(p, &L, v8, x, y, core_k);
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

/* Paint the rectangle [x0,x1) x [y0,y1) of the framebuffer, clipped to the panel. */
static void paint_rect(const struct plat_fb *f, const struct pal *p, const struct lay *L, int32_t x0,
                       int32_t y0, int32_t x1, int32_t y1, uint32_t k)
{
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > (int32_t)f->width) x1 = (int32_t)f->width;
	if (y1 > (int32_t)f->height) y1 = (int32_t)f->height;
	for (int32_t y = y0; y < y1; y++) {
		volatile uint32_t *line = (volatile uint32_t *)(uintptr_t)(f->base + (uint64_t)y * f->stride);
		int32_t v8[3];
		row_v8(p, (uint32_t)y, f->height, v8);
		for (int32_t x = x0; x < x1; x++)
			line[x] = pixel_row(p, L, v8, (uint32_t)x, (uint32_t)y, k);
	}
}

/* The whole panel (width x height pixels of every row) in each row's dithered gradient: one fill,
 * 64-bit stores (the 2x2 Bayer pattern repeats every two pixels, so a row is one repeated word),
 * the colours worked out once per row. */
static void fill_background(const struct plat_fb *f, const struct pal *p)
{
	for (uint32_t y = 0; y < f->height; y++) {
		volatile uint64_t *line = (volatile uint64_t *)(uintptr_t)(f->base + (uint64_t)y * f->stride);
		int32_t v8[3];
		row_v8(p, y, f->height, v8);
		uint64_t two = (uint64_t)dither(v8, 0, y) | ((uint64_t)dither(v8, 1, y) << 32);
		uint32_t pairs = f->width / 2u;
		for (uint32_t x = 0; x < pairs; x++)
			line[x] = two;
		if (f->width & 1u)
			((volatile uint32_t *)line)[f->width - 1u] = dither(v8, f->width - 1u, y);
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
	struct lay L;
	layout(&L, f->width, f->height);
	/* the art's boxes: the halo (it holds the mark), the wordmark, the hairline */
	int32_t hx0 = L.cx - L.rg - 1, hx1 = L.cx + L.rg + 1, hy0 = L.cy - L.rg - 1, hy1 = L.cy + L.rg + 1;
	int32_t tx0 = text_x0(&L), tx1 = text_x1(&L), ty0 = text_y0(&L), ty1 = text_y1(&L);
	int32_t bx0 = L.hx0 - L.hth - 2, bx1 = L.hx0 + L.hlen + L.hth + 2, by0 = L.hy - L.hth - 2, by1 = L.hy + L.hth + 3;
	int32_t cp = L.rc + 2;                              /* the core's square: its radius never reaches the edge */
	fill_background(f, p);                              /* clears everything S-Boot left behind */
	paint_rect(f, p, &L, hx0, hy0, hx1, hy1, 0);        /* the halo, the rings, the satellite: no core yet */
	paint_rect(f, p, &L, tx0, ty0, tx1, ty1, 0);
	paint_rect(f, p, &L, bx0, by0, bx1, by1, 0);        /* the empty track */
	static const uint32_t steps[4] = { 64, 128, 192, 256 };
	for (unsigned s = 0; s < 4; s++) {
		paint_rect(f, p, &L, L.cx - cp, L.cy - cp, L.cx + cp, L.cy + cp, steps[s]);
		paint_rect(f, p, &L, tx0, ty0, tx1, ty1, steps[s]);
		paint_rect(f, p, &L, bx0, by0, bx1, by1, steps[s]);
		if (s < 3)
			delay_ms(SPLASH_FRAME_MS);
	}
#if defined(__aarch64__)
	__asm__ volatile("dsb sy" ::: "memory");            /* every store has left the core before the jump */
#endif
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
