/*
 * Clock (CMU) & PMU status inspection / bring-up.
 *
 * On Exynos 9810, the CMU hierarchy is split into domain controllers:
 *   - CMU_TOP    (0x12060000): shared bus & top-level PLLs
 *   - CMU_BUSC   (0x12200000): main bus fabric
 *   - CMU_PERIC0 (0x10400000): USI0 / UART0 debug console clocks
 *   - CMU_MIF0-3 (0x1A000000..0x1A030000): 4-channel LPDDR4x memory interface
 */
#include <stdint.h>
#include "boot.h"
#include "exynos9810.h"
#include "lib.h"

#ifdef PLATFORM_STARLTE
static void check_cmu_pll(const char *name, uint64_t base)
{
	if (IS_PLACEHOLDER(base)) {
		kprintf("clock: %s base is placeholder (%p)\n", name, (void *)base);
		return;
	}
	uint32_t con0 = mmio_r32(base + CMU_PLL_CON0);
	int en = (con0 & CMU_PLL_CON0_ENABLE) != 0;
	int locked = (con0 & CMU_PLL_CON0_STABLE) != 0;
	kprintf("clock: %-10s @ %p PLL_CON0=0x%08x (en=%d stable=%d)\n",
	        name, (void *)base, con0, en, locked);
}
#endif

void platform_clock_init(void)
{
#ifdef PLATFORM_STARLTE
	/* Ensure PS_HOLD remains asserted so PMIC power stays latched. */
	mmio_setbits32(PMU_PS_HOLD_CONTROL, (1u << 8));

	check_cmu_pll("CMU_TOP",    CMU_TOP_BASE);
	check_cmu_pll("CMU_BUSC",   CMU_BUSC_BASE);
	check_cmu_pll("CMU_PERIC0", CMU_PERIC0_BASE);
	check_cmu_pll("CMU_MIF0",   CMU_MIF0_BASE);
	check_cmu_pll("CMU_MIF1",   CMU_MIF1_BASE);
	check_cmu_pll("CMU_MIF2",   CMU_MIF2_BASE);
	check_cmu_pll("CMU_MIF3",   CMU_MIF3_BASE);
#else
	kprintf("clock: qemu virt — fixed virtual clocks active\n");
#endif
}

uint32_t platform_read_boot_flags(void)
{
	uint32_t flags = 0;
#ifdef PLATFORM_STARLTE
	uint32_t om      = mmio_r32(PMU_OM_STAT);
	uint32_t rst     = mmio_r32(PMU_RST_STAT);
	uint32_t inform2 = mmio_r32(PMU_INFORM2);
	uint32_t inform3 = mmio_r32(PMU_INFORM3);

	kprintf("pmu: OM_STAT=0x%08x RST_STAT=0x%08x INFORM2=0x%08x INFORM3=0x%08x\n",
	        om, rst, inform2, inform3);

	if (inform2 == EXYNOS_BOOT_RECOVERY_MAGIC || inform3 == EXYNOS_BOOT_RECOVERY_MAGIC)
		flags |= SCION_BF_RECOVERY;
	if (inform2 == EXYNOS_BOOT_WARM_MAGIC || (rst & 0x10000u))
		flags |= SCION_BF_WARM_BOOT;
#endif
	return flags;
}
