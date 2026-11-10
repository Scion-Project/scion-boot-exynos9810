/*
 * ARMv8 Generic Timer & Exynos 9810 MCT timekeeping.
 */
#include <stdint.h>
#include "exynos9810.h"
#include "lib.h"

static uint64_t s_timer_freq;

void platform_timer_init(void)
{
	uint64_t frq = 0;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));

	if (frq == 0) {
		frq = ARCH_TIMER_DEFAULT_HZ;
		if (current_el() == 3) {
			__asm__ volatile("msr cntfrq_el0, %0\n\tisb" :: "r"(frq));
		}
	}
	s_timer_freq = frq;

#ifdef PLATFORM_STARLTE
	/* Ensure the Exynos 9810 Multi-Core Timer (MCT) global FRC is started
	 * as the backing counter for the ARM generic timer. */
	uint32_t tcon = mmio_r32(MCT_G_TCON);
	if (!(tcon & MCT_G_TCON_START)) {
		mmio_w32(MCT_G_TCON, tcon | MCT_G_TCON_START);
	}
#endif
}

uint64_t timer_get_freq(void)
{
	if (!s_timer_freq)
		platform_timer_init();
	return s_timer_freq;
}

uint64_t timer_get_ticks(void)
{
	uint64_t pct;
	__asm__ volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(pct));
	return pct;
}

uint64_t timer_ticks_to_us(uint64_t ticks)
{
	uint64_t f = timer_get_freq();
	if (!f)
		return 0;
	return (ticks * 1000000ULL) / f;
}

void udelay(uint64_t us)
{
	uint64_t f = timer_get_freq();
	uint64_t start = timer_get_ticks();
	uint64_t wait_ticks = (us * f) / 1000000ULL;
	if (wait_ticks == 0 && us > 0)
		wait_ticks = 1;
	while ((timer_get_ticks() - start) < wait_ticks)
		__asm__ volatile("nop");
}

void mdelay(uint64_t ms)
{
	while (ms--)
		udelay(1000);
}
