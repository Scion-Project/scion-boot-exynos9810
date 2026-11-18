/*
 * Display / Framebuffer driver & early on-screen status console.
 *
 * On Exynos 9810 (starlte), S-Boot leaves the S6E3HA8 (1440x2960 WQHD+ AMOLED)
 * panel powered and configured with an ARGB8888 scanout buffer at 0xCC000000,
 * but leaves DECON0's HW_SW_TRIG_CONTROL (0x16030070) masked. Writing 0x1281
 * unmasks automatic refresh so Stage 1 and Stage 2 can render directly to
 * 0xCC000000 (credit: Markuss Broks & Ivaylo Ivanov / uniLoader).
 *
 * Under PLATFORM=qemu, a 640x480x32bpp framebuffer carveout at 0x42000000 is
 * used so the entire drawing pipeline and boot_info.fb_* handoff are verified.
 */
#include <stdint.h>
#include <stddef.h>
#include "boot.h"
#include "exynos9810.h"
#include "lib.h"

#define QEMU_FB_BASE    0x42000000ULL
#define QEMU_FB_WIDTH   640u
#define QEMU_FB_HEIGHT  480u
#define QEMU_FB_BPP     32u
#define QEMU_FB_STRIDE  (QEMU_FB_WIDTH * 4u)
#define QEMU_FB_SIZE    (QEMU_FB_STRIDE * QEMU_FB_HEIGHT)

static uint32_t *s_fb;
static uint32_t  s_width;
static uint32_t  s_height;
static uint32_t  s_pitch_px;
static uint32_t  s_cursor_x;
static uint32_t  s_cursor_y;
static uint32_t  s_fg_color = 0xFFE6EDF3u;
static uint32_t  s_bg_color = 0xFF0D1117u;

#include "font8x8.h"

static void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
	if (!s_fb)
		return;
	for (uint32_t row = y; row < y + h && row < s_height; row++) {
		uint32_t *line = s_fb + (size_t)row * s_pitch_px;
		for (uint32_t col = x; col < x + w && col < s_width; col++)
			line[col] = color;
	}
}

static void fb_draw_char2x(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg)
{
	if (!s_fb || x + 16 > s_width || y + 16 > s_height)
		return;
	unsigned idx = (c >= 0x20 && c <= 0x7E) ? (unsigned)(c - 0x20) : 0;
	const uint8_t *glyph = font8x8[idx];

	for (unsigned row = 0; row < 8; row++) {
		uint8_t bits = glyph[row];
		uint32_t *line0 = s_fb + (size_t)(y + row * 2) * s_pitch_px + x;
		uint32_t *line1 = s_fb + (size_t)(y + row * 2 + 1) * s_pitch_px + x;
		for (unsigned col = 0; col < 8; col++) {
			uint32_t px = (bits & (0x80u >> col)) ? fg : bg;
			line0[col * 2]     = px;
			line0[col * 2 + 1] = px;
			line1[col * 2]     = px;
			line1[col * 2 + 1] = px;
		}
	}
}

static void fb_putc(char c)
{
	if (!s_fb)
		return;
	if (c == '\r') {
		s_cursor_x = 16;
		return;
	}
	if (c == '\n') {
		s_cursor_x = 16;
		s_cursor_y += 20;
		if (s_cursor_y + 20 > s_height)
			s_cursor_y = 64;
		return;
	}
	if (s_cursor_x + 16 > s_width - 16) {
		s_cursor_x = 16;
		s_cursor_y += 20;
	}
	if (s_cursor_y + 20 <= s_height)
		fb_draw_char2x(s_cursor_x, s_cursor_y, c, s_fg_color, s_bg_color);
	s_cursor_x += 16;
}

void display_puts(const char *s)
{
	if (!s_fb || !s)
		return;
	while (*s)
		fb_putc(*s++);
}

void display_alert(const char *title, const char *detail)
{
	if (!s_fb)
		return;
	uint32_t box_y = (s_height > 240) ? (s_height - 160) : 64;
	fb_fill_rect(0, box_y, s_width, 120, 0xFF6E1414u);
	fb_fill_rect(0, box_y, s_width, 4, 0xFFFF5555u);

	uint32_t saved_bg = s_bg_color;
	uint32_t saved_fg = s_fg_color;
	s_bg_color = 0xFF6E1414u;
	s_fg_color = 0xFFFFFFFFu;
	s_cursor_x = 16;
	s_cursor_y = box_y + 16;
	display_puts(title ? title : "ALERT");
	display_puts("\n");
	s_fg_color = 0xFFFFD8D8u;
	display_puts(detail ? detail : "");
	display_puts("\n");
	s_bg_color = saved_bg;
	s_fg_color = saved_fg;
}

void display_test_pattern(void)
{
	if (!s_fb) {
		kputs("display: framebuffer not initialized\n");
		return;
	}
	static const uint32_t bars[8] = {
		0xFFFFFFFFu, /* White */
		0xFFFFFF00u, /* Yellow */
		0xFF00FFFFu, /* Cyan */
		0xFF00FF00u, /* Green */
		0xFFFF00FFu, /* Magenta */
		0xFFFF0000u, /* Red */
		0xFF0000FFu, /* Blue */
		0xFF161B22u  /* Dark Slate */
	};
	uint32_t bar_w = s_width / 8;
	uint32_t bar_y = 64;
	uint32_t bar_h = (s_height > 160) ? (s_height - 160) : 64;

	for (unsigned i = 0; i < 8; i++)
		fb_fill_rect(i * bar_w, bar_y, bar_w, bar_h, bars[i]);

	kprintf("display: rendered 8-bar test pattern on %ux%u @ %p\n",
	        s_width, s_height, (void *)s_fb);
}

void display_init(struct scion_boot_info *bi)
{
#ifdef PLATFORM_STARLTE
	/* Unmask DECON0 HW/SW trigger so S-Boot's 1440x2960 framebuffer at
	 * 0xCC000000 refreshes automatically on the S6E3HA8 panel. */
	mmio_w32(DECON_F_BASE + DECON_HW_SW_TRIG_CONTROL, DECON_TRIG_UNMASK_VAL);

	bi->fb_base   = STARLTE_FB_BASE;
	bi->fb_width  = STARLTE_FB_WIDTH;
	bi->fb_height = STARLTE_FB_HEIGHT;
	bi->fb_stride = STARLTE_FB_STRIDE;
	bi->fb_bpp    = STARLTE_FB_BPP;
	boot_mem_add(bi, STARLTE_FB_BASE, STARLTE_FB_SIZE,
	             SCION_MEM_RESERVED | SCION_MEM_FB);
#else
	bi->fb_base   = QEMU_FB_BASE;
	bi->fb_width  = QEMU_FB_WIDTH;
	bi->fb_height = QEMU_FB_HEIGHT;
	bi->fb_stride = QEMU_FB_STRIDE;
	bi->fb_bpp    = QEMU_FB_BPP;
	boot_mem_add(bi, QEMU_FB_BASE, QEMU_FB_SIZE,
	             SCION_MEM_RESERVED | SCION_MEM_FB);
#endif

	s_fb       = (uint32_t *)(uintptr_t)bi->fb_base;
	s_width    = bi->fb_width;
	s_height   = bi->fb_height;
	s_pitch_px = bi->fb_stride / 4u;

	/* Clear screen and draw top status banner */
	fb_fill_rect(0, 0, s_width, s_height, s_bg_color);
	fb_fill_rect(0, 0, s_width, 48, 0xFF161B22u);
	fb_fill_rect(0, 48, s_width, 3, 0xFF58A6FFu);

	uint32_t saved_bg = s_bg_color;
	s_bg_color = 0xFF161B22u;
	s_fg_color = 0xFF58A6FFu;
	s_cursor_x = 16;
	s_cursor_y = 16;
	display_puts("SCION-BOOT EXYNOS 9810 [STAGE 1]");
	s_bg_color = saved_bg;
	s_fg_color = 0xFFE6EDF3u;

	s_cursor_x = 16;
	s_cursor_y = 68;
	display_puts("Platform : " PLATFORM_NAME "\n");
	display_puts("Safety   : Write-Guard ON | USBDL Recovery Armed\n");

	kprintf("display: %ux%ux%u framebuffer ready at %p (stride=%u)\n",
	        bi->fb_width, bi->fb_height, bi->fb_bpp,
	        (void *)bi->fb_base, bi->fb_stride);
}
