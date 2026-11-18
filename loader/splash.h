/* SPDX-License-Identifier: MIT-0
 * The Scion boot splash (loader/splash.c): the one frame the loader leaves on the panel before it
 * starts an XNU entry. Original code-drawn art (a mark, the "SCION" wordmark from vector strokes,
 * a progress hairline); no font, no bitmap, no third-party artwork.
 */
#ifndef SCION_SPLASH_H
#define SCION_SPLASH_H

#include <stdint.h>

enum splash_variant {
	SPL_DEFAULT,
	SPL_GOLD,        /* easter egg: Volume Up, Up, Down, Down, Bixby in the menu */
	SPL_EMBER,       /* easter egg: Volume Up, Down, Up, Down, Up in the menu */
	SPL_MONO,        /* easter egg: Volume Up and Down held together for a second in the menu */
	SPL_HALLOWEEN,   /* the loader was built on 31 October */
	SPL_WINTER,      /* the loader was built on 25 December */
	SPL_COUNT
};

/* The colour of the core disc at its centre in the default variant (the centre pixel of a default
 * splash). */
#define SPLASH_DEFAULT_CORE 0xfff4f8ffu

/* The mark is centred at this fraction (per mille) of the panel's height, a little above the middle,
 * so the wordmark fits beneath it. */
#define SPLASH_CY_PERMILLE 455

/* Where the art is on a w x h panel (pixels): the mark's centre, orbit radius and core radius, the
 * wordmark's centre line and cap height and the x of the I's stem, the hairline's left end, length
 * and row, and the satellite's centre. For tests. */
struct splash_geom {
	int32_t cx, cy, R, rc;
	int32_t text_y, i_x, text_h;
	int32_t hair_x0, hair_len, hair_y;
	int32_t sat_x, sat_y;
};
void splash_geometry(uint32_t w, uint32_t h, struct splash_geom *out);

/* Pure: the colour of pixel (x, y) of a w x h splash in variant v at ignition core_k / 256: the core
 * disc's radius, the wordmark's opacity and the hairline's progress (0 = no core, no text, an empty
 * track; 256 = the finished splash). Host-testable. */
uint32_t splash_pixel(unsigned v, uint32_t w, uint32_t h, uint32_t x, uint32_t y, uint32_t core_k);

/* Pure: the variant a loader built on the __DATE__ string d ("Mmm dd yyyy") gets. */
unsigned splash_variant_for_date(const char *d);

/* The variant this boot shows: an easter egg entered in the menu, else the build-date variant. */
unsigned splash_current(void);

/* Passive observers of the menu's keys: they do not consume or change any key. */
void splash_key(unsigned menu_key);                 /* MK_UP, MK_DOWN, MK_EDIT (others reset) */
void splash_held(unsigned plat_keys, uint64_t now_ms);

/* Easter egg at power-on: Bixby held when the loader starts an entry adds -v to its line. */
int splash_verbose_held(unsigned plat_keys);

/* Paint the splash (with its short ignition: four frames, the core growing, the wordmark fading in and the hairline advancing) on the framebuffer, if there is one.
 * Returns at once with no framebuffer. Prints one UART line. */
void splash_handoff(void);

#endif
