/* SPDX-License-Identifier: MIT
 * What the loader needs from a board (plat_qemu.c, plat_starlte.c).
 */
#ifndef SCION_PLAT_H
#define SCION_PLAT_H

#include <stdint.h>
#include "xnu.h"

/* Physical keys (a phone's buttons), polled. */
#define PK_UP     (1u << 0)
#define PK_DOWN   (1u << 1)
#define PK_SELECT (1u << 2)   /* S9: Power */
#define PK_EDIT   (1u << 3)   /* S9: Bixby */

enum plat_reboot { PR_NORMAL, PR_RECOVERY, PR_DOWNLOAD, PR_POWEROFF };

struct plat_fb {
	uint64_t base;
	uint32_t width, height, stride;   /* stride in bytes, 32 bpp */
	uint32_t scale;                   /* font scale: 8x8 glyphs drawn scale x scale */
};

struct plat_map {        /* the loader's identity map (mmu.c), 2 MiB granularity */
	uint64_t dram_base, dram_end;     /* normal memory, non-cacheable unless in wb_* */
	uint64_t wb_base, wb_end;         /* normal write-back: the loader and its appended images */
	uint64_t dram2_base, dram2_end;   /* a second bank (non-cacheable), or 0 */
};

const char *plat_name(void);
void plat_init(void);
/* Console: a UART when the board has one we may use (else a no-op). */
void plat_uart_putc(char c);
int plat_uart_getc(void);             /* -1 when nothing is waiting */
unsigned plat_keys(void);             /* PK_* currently held */
const struct plat_fb *plat_fb(void);  /* 0: no framebuffer */
void plat_fb_handoff(void);           /* leave the display as the OS expects it */
const struct plat_map *plat_map(void);
const struct xnu_window *plat_xnu_windows(unsigned *n);
/* Where appended images may lie (S-Boot's kernel slot area): they must be in [lo, hi). */
void plat_image_area(uint64_t *lo, uint64_t *hi);
/* The boot-loop guard's retained RAM (32 bytes), or 0 when the board has none. */
volatile struct scion_guard_rec *plat_guard_slot(void);
uint64_t plat_ticks(void);
uint64_t plat_freq(void);
void plat_reboot(enum plat_reboot why) __attribute__((noreturn));
/* The loader's own watchdog: a hang inside the loader resets the board. 0 when absent. */
int plat_wdt_start(uint32_t ms);
void plat_wdt_kick(void);
void plat_wdt_stop(void);
/* A watchdog the previous OS left running (re-entry, SCION_RE_WDT_ARMED): pet it while the
 * loader runs, stop it before the next OS. */
void plat_os_wdt_kick(void);
void plat_os_wdt_stop(void);
/* Where S-Boot's FDT is kept for a later re-entry (SCION_FDT_SAVE_*). */
uint64_t plat_fdt_save_area(void);
uint32_t plat_fdt_save_max(void);    /* bytes of FDT it holds after its 4 KiB header */
/* An interrupt arrived (exceptions.c): 1 when the board handled it. */
int plat_irq(void);
/* Test hooks (QEMU only): 1 when the board compiled them in. */
int plat_test_hooks(void);

#endif
