/*
 * Watchdog timer (WDT) control, fail-safe deadman timer, and power control.
 *
 * Exynos 9810 has two S3C6410-compatible watchdog blocks:
 *   - wdt@10050000 (Cluster 0)
 *   - wdt@10060000 (Cluster 1)
 */
#include <stdint.h>
#include "exynos9810.h"
#include "lib.h"

void platform_wdt_disable(void)
{
#ifdef PLATFORM_STARLTE
	mmio_clrbits32(WDT_CL0_BASE + WDT_WTCON,
	               WDT_WTCON_ENABLE | WDT_WTCON_RSTEN | WDT_WTCON_INTEN);
	mmio_clrbits32(WDT_CL1_BASE + WDT_WTCON,
	               WDT_WTCON_ENABLE | WDT_WTCON_RSTEN | WDT_WTCON_INTEN);
	kprintf("wdt: disabled CL0 (%p) and CL1 (%p) watchdogs\n",
	        (void *)WDT_CL0_BASE, (void *)WDT_CL1_BASE);
#else
	kprintf("wdt: qemu, no hardware watchdog\n");
#endif
}

void platform_wdt_kick(void)
{
#ifdef PLATFORM_STARLTE
	uint32_t dat0 = mmio_r32(WDT_CL0_BASE + WDT_WTDAT);
	mmio_w32(WDT_CL0_BASE + WDT_WTCNT, dat0 ? dat0 : 0x8000u);
	mmio_w32(WDT_CL0_BASE + WDT_WTCLRINT, 1u);
#endif
}

/*
 * Arms a fail-safe hardware watchdog before jumping to Stage 2 so an hung
 * second stage cannot leave the device stuck indefinitely with screen/PMIC on.
 */
void platform_wdt_arm_failsafe(uint32_t timeout_sec)
{
#ifdef PLATFORM_STARLTE
	if (timeout_sec == 0)
		timeout_sec = 15;
	/* Prescaler=255, div=128 (~793 Hz at 26 MHz PCLK) */
	uint32_t ticks = timeout_sec * 793u;
	if (ticks > 0xFFFFu)
		ticks = 0xFFFFu;

	mmio_clrbits32(PMU_MASK_WDT_RESET, 1u);
	mmio_w32(WDT_CL0_BASE + WDT_WTDAT, ticks);
	mmio_w32(WDT_CL0_BASE + WDT_WTCNT, ticks);
	mmio_w32(WDT_CL0_BASE + WDT_WTCLRINT, 1u);
	/* WTCON: Prescaler=0xFF (bits 15:8), Div=128 (bits 4:3=0x3), Enable + Reset */
	uint32_t wtcon = (0xFFu << 8) | (0x3u << 3) | WDT_WTCON_ENABLE | WDT_WTCON_RSTEN;
	mmio_w32(WDT_CL0_BASE + WDT_WTCON, wtcon);
	kprintf("wdt: armed %u-second fail-safe deadman timer on CL0\n", timeout_sec);
#else
	(void)timeout_sec;
#endif
}

void platform_reboot(void)
{
	kprintf("platform: initiating system warm reset...\n");
	uart_flush();

#ifdef PLATFORM_STARLTE
	mmio_w32(PMU_SWRESET, 0x1u);
#else
	/* PSCI_0_2_FN_SYSTEM_RESET (0x84000009) */
	register uint64_t x0 __asm__("x0") = 0x84000009ULL;
	__asm__ volatile("hvc #0" :: "r"(x0) : "memory");
#endif
	for (;;)
		__asm__ volatile("wfe");
}

void platform_poweroff(void)
{
	kprintf("platform: powering off cleanly (releasing PS_HOLD)...\n");
	uart_flush();

#ifdef PLATFORM_STARLTE
	platform_wdt_disable();
	/* Clear bit 8 (PS_HOLD output) in PMU_PS_HOLD_CONTROL (0x1406330C) */
	mmio_w32(PMU_PS_HOLD_CONTROL, PMU_PS_HOLD_VAL_OFF);
#else
	/* PSCI_0_2_FN_SYSTEM_OFF (0x84000008) */
	register uint64_t x0 __asm__("x0") = 0x84000008ULL;
	__asm__ volatile("hvc #0" :: "r"(x0) : "memory");
#endif
	for (;;)
		__asm__ volatile("wfe");
}
