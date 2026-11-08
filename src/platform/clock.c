/*
 * Clock (CMU) bring-up.
 *
 * TODO(starlte): program the CMU so that (a) the console UART port has a stable
 * PCLK and (b) the DRAM controller + PHY have their reference clocks before
 * dram_init() runs. Sequence comes from the SoC user manual's CMU chapter and
 * the vendor 'clock-controller@...' DT nodes.
 *
 * Under QEMU there is nothing to do.
 */
#include "exynos9810.h"
#include "lib.h"

void platform_clock_init(void)
{
#ifdef PLATFORM_STARLTE
	/* TODO(starlte): CMU_TOP / CMU_PERIC / CMU_MIF setup.
	 * Must run BEFORE platform_dram_init(). */
	kprintf("clock: TODO(starlte) CMU not yet programmed\n");
#else
	kprintf("clock: qemu, nothing to do\n");
#endif
}
