/* SPDX-License-Identifier: MIT
 * QEMU virt (-M virt -cpu cortex-a57, 2 GiB at 0x40000000, booted at EL1 with -kernel): the
 * board the loader's tests run on (tools/qemu-test.py).
 *
 *   PL011 0x09000000           console and the menu's keys (no buttons)
 *   0x40000000..0x48000000     the loader and its appended images (QEMU puts an arm64 Image at
 *                              0x40080000 and its FDT after it, x0 = FDT, as S-Boot does)
 *   0x5e000000                 a 640x480 framebuffer in RAM (nothing scans it out; the tests
 *                              read the console)
 *   0x5ffff000                 the guard's retained RAM (a test preloads it with -device loader)
 *   0x60000000..0x80000000     XNU's window
 *   GICv2 0x08000000/0x08010000 + the EL1 virtual timer (PPI 27): the loader's watchdog
 *   PSCI over HVC              reset and power off (QEMU -no-reboot ends the run on reset)
 */
#include <stdint.h>
#include "plat.h"
#include "lib.h"

#define UART       0x09000000ul
#define GICD       0x08000000ul
#define GICC       0x08010000ul
#define VTIMER_IRQ 27u
#define GUARD_PA   0x5ffff000ul

static inline void w32(uint64_t a, uint32_t v) { *(volatile uint32_t *)(uintptr_t)a = v; }
static inline uint32_t r32(uint64_t a) { return *(volatile uint32_t *)(uintptr_t)a; }

static const struct plat_fb qfb = { 0x5e000000ul, 640, 480, 640 * 4, 1, 0, 0 };
static const struct plat_map qmap = { 0x40000000ul, 0xc0000000ul, 0x40000000ul, 0x48000000ul, 0, 0 };
static const struct xnu_window qwin[1] = { { 0x60000000ul, 0x80000000ul } };

const char *plat_name(void) { return "qemu-virt"; }

void plat_init(void)
{
	w32(UART + 0x44, 0x7ff);                         /* clear interrupts */
	w32(UART + 0x2c, (3u << 5) | (1u << 4));         /* 8N1, FIFOs */
	w32(UART + 0x30, (1u << 0) | (1u << 8) | (1u << 9));
}

void plat_uart_putc(char c)
{
	while (r32(UART + 0x18) & (1u << 5))
		;
	w32(UART, (uint8_t)c);
}

int plat_uart_getc(void)
{
	if (r32(UART + 0x18) & (1u << 4))
		return -1;
	return (int)(r32(UART) & 0xff);
}

unsigned plat_keys(void) { return 0; }
const struct plat_fb *plat_fb(void) { return &qfb; }
void plat_fb_handoff(void) { }
const struct plat_map *plat_map(void) { return &qmap; }

const struct xnu_window *plat_xnu_windows(unsigned *n)
{
	*n = 1;
	return qwin;
}

void plat_image_area(uint64_t *lo, uint64_t *hi)
{
	*lo = 0x40000000ul;
	*hi = 0x48000000ul;
}

volatile struct scion_guard_rec *plat_guard_slot(void)
{
	return (volatile struct scion_guard_rec *)GUARD_PA;
}

uint64_t plat_ticks(void)
{
	uint64_t v;
	__asm__ volatile("isb\nmrs %0, cntvct_el0" : "=r"(v));
	return v;
}

uint64_t plat_freq(void)
{
	uint64_t v;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
#ifdef LDR_TEST_CNTFRQ0
	v = 0;   /* qemu-test's cntfrq0 build: CNTFRQ_EL0 as the S9's S-Boot leaves it */
#endif
	return v;
}

uint64_t plat_freq_default(void) { return 62500000ull; }   /* QEMU virt's counter */

static void psci(uint64_t fn) __attribute__((noreturn));
static void psci(uint64_t fn)
{
	register uint64_t x0 __asm__("x0") = fn;
	__asm__ volatile("hvc #0" : "+r"(x0) :: "memory");
	for (;;)
		__asm__ volatile("wfi");
}

void plat_reboot(enum plat_reboot why)
{
	static const char names[4][10] = { "normal", "recovery", "download", "poweroff" };
	kprintf("reboot: %s\n", names[why & 3]);
	psci(why == PR_POWEROFF ? 0x84000008ul : 0x84000009ul);
}

/* ---- watchdog: the virtual timer through the GIC --------------------------------------- */

static uint64_t wdt_ticks;

int plat_wdt_start(uint32_t ms)
{
	wdt_ticks = ldr_hz() / 1000 * ms;
	w32(GICD + 0x000, 1);                                 /* distributor on */
	w32(GICD + 0x100, 1u << VTIMER_IRQ);                  /* ISENABLER0 */
	*(volatile uint8_t *)(uintptr_t)(GICD + 0x400 + VTIMER_IRQ) = 0x80;
	w32(GICC + 0x004, 0xf0);                              /* PMR */
	w32(GICC + 0x000, 1);                                 /* CPU interface on */
	__asm__ volatile("msr cntv_tval_el0, %0\nmsr cntv_ctl_el0, %1\nisb\nmsr daifclr, #2"
	                 :: "r"(wdt_ticks), "r"(1ul));
	return 1;
}

void plat_wdt_kick(void)
{
	if (wdt_ticks)
		__asm__ volatile("msr cntv_tval_el0, %0\nisb" :: "r"(wdt_ticks));
}

void plat_wdt_stop(void)
{
	__asm__ volatile("msr daifset, #2\nmsr cntv_ctl_el0, %0\nisb" :: "r"(2ul));
	if (wdt_ticks) {
		w32(GICD + 0x180, 1u << VTIMER_IRQ);              /* ICENABLER0 */
		w32(GICC + 0x000, 0);
	}
	wdt_ticks = 0;
}

void ldr_watchdog_bark(void) __attribute__((noreturn));

int plat_irq(void)
{
	uint32_t iar = r32(GICC + 0x00c);
	if ((iar & 0x3ff) == VTIMER_IRQ && wdt_ticks) {
		__asm__ volatile("msr cntv_ctl_el0, %0\nisb" :: "r"(2ul));
		w32(GICC + 0x010, iar);                           /* EOI */
		ldr_watchdog_bark();
	}
	if ((iar & 0x3ff) < 1020)
		w32(GICC + 0x010, iar);
	return (iar & 0x3ff) < 1020;
}

int plat_test_hooks(void) { return 1; }

/* No hardware watchdog survives into QEMU's loader: these only say what would happen. */
void plat_os_wdt_kick(void) { }
void plat_os_wdt_stop(void) { kprintf("os watchdog: stopped before the next OS\n"); }
uint64_t plat_fdt_save_area(void) { return SCION_FDT_SAVE_QEMU; }
/* QEMU's generated FDT is not packed: 1 MiB. Up to the guard page there is room for 2. */
uint32_t plat_fdt_save_max(void) { return 2u << 20; }
