/*
 * PSCI / secondary-core bring-up (spin-table + Exynos 9810 PMU core sequencing).
 *
 * Exynos 9810 is big.LITTLE:
 *   Cluster 0 (cpus 0..3): Cortex-A55
 *   Cluster 1 (cpus 4..7): Mongoose M3
 */
#include <stdint.h>
#include "boot.h"
#include "exynos9810.h"
#include "lib.h"

extern uint64_t __cpu_release_addr;
extern uint32_t __secondary_online_count;

uint32_t psci_secondary_count(void)
{
	return *(volatile uint32_t *)&__secondary_online_count;
}

int psci_cpu_on(uint32_t cpu_id, uint64_t entry_pa)
{
	if (cpu_id == 0 || cpu_id >= 8)
		return -1;

	*(volatile uint64_t *)&__cpu_release_addr = entry_pa;
	__asm__ volatile("dsb sy\n\tsev" ::: "memory");

#ifdef PLATFORM_STARLTE
	/* Program the secondary entry vector in PMU alive scratchpad and power
	 * up the target CPU domain via PMU_CPU_CONFIG(cpu_id). */
	mmio_w32(PMU_INFORM0, (uint32_t)entry_pa);
	mmio_w32(PMU_SYSIP_DAT0, (uint32_t)entry_pa);

	uint32_t status = mmio_r32(PMU_CPU_STATUS(cpu_id)) & PMU_LOCAL_PWR_CFG;
	if (status != PMU_LOCAL_PWR_CFG) {
		mmio_w32(PMU_CPU_CONFIG(cpu_id), PMU_LOCAL_PWR_CFG);
		/* Poll up to 10 ms for power rail stabilization */
		for (unsigned t = 0; t < 1000; t++) {
			if ((mmio_r32(PMU_CPU_STATUS(cpu_id)) & PMU_LOCAL_PWR_CFG) ==
			    PMU_LOCAL_PWR_CFG)
				return 0;
			udelay(10);
		}
		kprintf("psci: timeout powering CPU%u (STATUS=0x%08x)\n",
		        cpu_id, mmio_r32(PMU_CPU_STATUS(cpu_id)));
		return -2;
	}
	return 0;
#else
	(void)entry_pa;
	return 0;
#endif
}

void psci_init(struct scion_boot_info *bi)
{
	bi->cpu_release_addr = (uint64_t)(uintptr_t)&__cpu_release_addr;
	*(volatile uint64_t *)&__cpu_release_addr = 0;
	__asm__ volatile("dsb sy" ::: "memory");

#ifdef PLATFORM_STARLTE
	kprintf("psci: spin-table at %p (parked secondaries: %u)\n",
	        (void *)bi->cpu_release_addr, psci_secondary_count());
#else
	kprintf("psci: spin-table at %p (qemu, parked secondaries: %u)\n",
	        (void *)bi->cpu_release_addr, psci_secondary_count());
#endif
}
