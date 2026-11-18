/* SPDX-License-Identifier: MIT
 * The Galaxy S9 (SM-G960F, starlte, Exynos 9810) after S-Boot: EL1, MMU off, one CPU, the panel
 * lit by S-Boot. NOT RUN ON A PHONE YET (docs/STATUS.md, docs/phone-test-plan.md).
 *
 * Every address here is a fact the project already relies on in its current loader
 * (scion-kernel xnuloader/sboot: menu.c, fbtext.c, xnu_boot.c, recovery.c; Samsung's starlte
 * device tree), re-stated, not copied:
 *   keys       ALIVE GPA0 DAT 0x14050024 (bit 3 Volume Up, 4 Volume Down, 6 Bixby) and GPA2 DAT
 *              0x14050064 (bit 4 Power), active low; read only
 *   timer      the generic counter (CNTPCT_EL0) at 26 MHz: S-Boot leaves CNTFRQ_EL0 at 0
 *   display    S-Boot's 1440x2960 ARGB framebuffer at 0xcc000000; DECON_F 0x16030000 + 0x70
 *              written 0x1281 so CPU writes reach the panel (left so for XNU, as today)
 *   reboot     PMU 0x14060000: INFORM2 (+0x808) 0x12345678, INFORM3 (+0x80c) 0x12345670 normal /
 *              0x12345671 Download / 0x12345674 recovery, then SWRESET (+0x400) = 1; power off
 *              clears PS_HOLD (PMU +0x330c bit 8)
 *   RAM        XNU windows 0x92000000..0xaf800000 and 0x880000000..0x900000000; S-Boot's kernel
 *              slot from 0x80000000, its initrd at 0x89000000 and FDT at 0x8a000000 (so
 *              everything appended to the loader must end below 0x89000000)
 *   retained   the exynos-ss page 0xfed17000 survives warm resets (the woa UEFI's latches at
 *              0xfed17f0c..0xfed17fb4, the current loader's boot-next mailbox at 0xfed17fe0); the
 *              guard takes the 32 bytes at 0xfed17fc0 between them (an assumption to confirm on
 *              the phone: test 3 of the phone plan)
 * No UART: the S9's console pins are not reachable, and polling an unclocked UART could hang;
 * plat_uart_putc is a no-op. No hardware watchdog yet: arming WDT_CLUSTER0 needs a PMU reset-mask
 * write the project has not made from EL1 (docs/STATUS.md, open question 2).
 */
#include <stdint.h>
#include "plat.h"

#define GPA0_DAT   0x14050024ul
#define GPA2_DAT   0x14050064ul
#define DECON_TRIG 0x16030070ul
#define TRIG_EN    0x1281u
#define PMU        0x14060000ul
#define GUARD_PA   0xfed17fc0ul

static inline void w32(uint64_t a, uint32_t v) { *(volatile uint32_t *)(uintptr_t)a = v; }
static inline uint32_t r32(uint64_t a) { return *(volatile uint32_t *)(uintptr_t)a; }

/* Font scale 5 (40-pixel glyphs); 96 pixels kept clear left and right and 112 at top and bottom for the
 * panel's rounded corners and curved edges (scale 4 from x = 32 lost the title's first letters). */
static const struct plat_fb sfb = { 0xcc000000ul, 1440, 2960, 1440 * 4, 5, 96, 112 };
static const struct plat_map smap = {
	0x80000000ul, 0x100000000ul, 0x80000000ul, 0x8c000000ul, 0x880000000ul, 0x900000000ul
};
static const struct xnu_window swin[2] = {
	{ 0x92000000ul, 0xaf800000ul }, { 0x880000000ul, 0x900000000ul }
};

const char *plat_name(void) { return "starlte"; }

void plat_init(void)
{
	w32(DECON_TRIG, TRIG_EN);
	__asm__ volatile("dsb sy" ::: "memory");
}

void plat_uart_putc(char c) { (void)c; }
int plat_uart_getc(void) { return -1; }

unsigned plat_keys(void)
{
	uint32_t a = r32(GPA0_DAT), b = r32(GPA2_DAT);
	return (!(a & (1u << 3)) ? PK_UP : 0) | (!(a & (1u << 4)) ? PK_DOWN : 0) |
	       (!(a & (1u << 6)) ? PK_EDIT : 0) | (!(b & (1u << 4)) ? PK_SELECT : 0);
}

const struct plat_fb *plat_fb(void) { return &sfb; }
void plat_fb_handoff(void) { }   /* XNU keeps the free-running trigger, as with the current loader */
const struct plat_map *plat_map(void) { return &smap; }

const struct xnu_window *plat_xnu_windows(unsigned *n)
{
	*n = 2;
	return swin;
}

void plat_image_area(uint64_t *lo, uint64_t *hi)
{
	*lo = 0x80000000ul;
	*hi = 0x89000000ul;
}

volatile struct scion_guard_rec *plat_guard_slot(void)
{
	return (volatile struct scion_guard_rec *)GUARD_PA;
}

uint64_t plat_ticks(void)
{
	uint64_t v;
	__asm__ volatile("isb\nmrs %0, cntpct_el0" : "=r"(v));
	return v;
}

uint64_t plat_freq(void)
{
	uint64_t v;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
}

/* S-Boot leaves CNTFRQ_EL0 at 0; the counter runs at 26 MHz (scion-kernel xnuloader/sboot/menu.c,
 * whose menu and countdown run on this phone with that value). */
uint64_t plat_freq_default(void) { return 26000000ull; }

void plat_reboot(enum plat_reboot why)
{
	if (why == PR_POWEROFF) {
		w32(PMU + 0x330c, r32(PMU + 0x330c) & ~0x100u);
		__asm__ volatile("dsb sy" ::: "memory");
		/* With a cable in the PMIC keeps the phone on: fall through to a normal reset. */
		for (uint64_t t = plat_ticks() + ldr_hz() * 3; plat_ticks() < t;)
			;
		why = PR_NORMAL;
	}
	uint32_t inform3 = why == PR_RECOVERY ? 0x12345674u : why == PR_DOWNLOAD ? 0x12345671u : 0x12345670u;
	w32(PMU + 0x808, 0x12345678u);
	w32(PMU + 0x80c, inform3);
	__asm__ volatile("dsb sy" ::: "memory");
	w32(PMU + 0x400, 1);
	__asm__ volatile("dsb sy" ::: "memory");
	for (;;)
		__asm__ volatile("wfe");
}

int plat_wdt_start(uint32_t ms) { (void)ms; return 0; }
void plat_wdt_kick(void) { }
void plat_wdt_stop(void) { }
int plat_irq(void) { return 0; }
int plat_test_hooks(void) { return 0; }

/* The cluster-1 watchdog (S3C2410-style WDT at 0x10060000: WTCON +0, WTDAT +4, WTCNT +8) that
 * scion-kernel's warm restart arms before it re-enters the loader (docs/ios27/WARM-RESTART.md).
 * Petting reloads the count; stopping clears WTCON. Only used when the re-entry record says the
 * OS armed it. */
#define WDT_CL1 0x10060000ul
void plat_os_wdt_kick(void) { w32(WDT_CL1 + 8, r32(WDT_CL1 + 4)); }
void plat_os_wdt_stop(void) { w32(WDT_CL1 + 0, 0); __asm__ volatile("dsb sy" ::: "memory"); }
uint64_t plat_fdt_save_area(void) { return SCION_FDT_SAVE_STARLTE; }
uint32_t plat_fdt_save_max(void) { return SCION_FDT_SAVE_MAX; }
