/*
 * PSCI / secondary-core bring-up skeleton (spin-table model).
 *
 * The first stage releases secondaries into a spin-table: each secondary waits
 * in start.S on __cpu_release_addr until the OS (or stage 2) writes an entry
 * point there. This file sets up boot_info.cpu_release_addr and, on hardware,
 * is where you'd power the other cores on through the PMU.
 *
 * Exynos 9810 is big.LITTLE: cluster0 = Cortex-A55, cluster1 = Mongoose M3
 * (confirmed from the S-Boot log: "Cortex-A0"/M3 cores booted via boot_exynos).
 */
#include <stdint.h>
#include "boot.h"
#include "exynos9810.h"
#include "lib.h"

extern uint64_t __cpu_release_addr;

void psci_init(struct scion_boot_info *bi)
{
	bi->cpu_release_addr = (uint64_t)(uintptr_t)&__cpu_release_addr;
	__cpu_release_addr = 0;

#ifdef PLATFORM_STARLTE
	/* TODO(starlte): power on the A55 + M3 secondaries via the PMU core
	 * configuration/status registers, each landing at _start -> park_secondary.
	 * Offsets from the PMU chapter of the SoC manual. Until then only the
	 * primary runs, which is fine for the DRAM/handoff bring-up. */
	kprintf("psci: spin-table at %p (secondaries not yet powered)\n",
	        (void *)bi->cpu_release_addr);
#else
	kprintf("psci: spin-table at %p (qemu, primary only)\n",
	        (void *)bi->cpu_release_addr);
#endif
}
