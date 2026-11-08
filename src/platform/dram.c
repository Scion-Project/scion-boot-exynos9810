/*
 * DRAM (DMC/DREX + PHY) bring-up — LPDDR4, 4 GB on this board.
 *
 * This is the hardest stub and the one that most needs the datasheet. "Done"
 * means: controller configured, PHY trained (CA/read/write leveling), and a
 * walking-1s/walking-0s test passes across the whole bank.
 *
 * Until it is written for hardware, PLATFORM_STARLTE returns the known memory
 * map WITHOUT having trained anything, and refuses to claim success if a
 * placeholder base is still in place — so a half-finished port fails loudly
 * instead of silently handing stage 2 a dead DRAM map.
 */
#include <stdint.h>
#include "boot.h"
#include "exynos9810.h"
#include "lib.h"

/* starlte DRAM base is the standard Exynos DRAM window. Size from
 * sec_debug.lpddr4_size=4 (GB). Confirm the base from the vendor DT 'memory'
 * node before trusting it on hardware. */
#define STARLTE_DRAM_BASE   0x80000000ULL
#define STARLTE_DRAM_SIZE   (4ULL * 1024 * 1024 * 1024)

/* QEMU virt RAM starts at 1 GiB; size is set by -m. Assume 2 GiB for the test. */
#define QEMU_DRAM_BASE      0x40000000ULL
#define QEMU_DRAM_SIZE      (2ULL * 1024 * 1024 * 1024)

static void add_ram(struct scion_boot_info *bi, uint64_t base, uint64_t size)
{
	unsigned i = bi->nr_mem++;
	bi->mem[i].base = base;
	bi->mem[i].size = size;
	bi->mem[i].flags = SCION_MEM_RAM;
}

int platform_dram_init(struct scion_boot_info *bi)
{
#ifdef PLATFORM_STARLTE
	if (IS_PLACEHOLDER(DMC0_BASE) || IS_PLACEHOLDER(DMC_PHY0_BASE)) {
		kprintf("dram: TODO(starlte) controller/PHY bases unset — refusing\n");
		kprintf("dram: fill DMC0_BASE/DMC_PHY0_BASE and the training seq\n");
		return -1;
	}
	/* TODO(starlte): real DMC init + PHY training here, then verify. */
	add_ram(bi, STARLTE_DRAM_BASE, STARLTE_DRAM_SIZE);
	kprintf("dram: starlte map set (training NOT yet implemented)\n");
	return 0;
#else
	add_ram(bi, QEMU_DRAM_BASE, QEMU_DRAM_SIZE);
	kprintf("dram: qemu %u MiB at %p\n",
	        (unsigned)(QEMU_DRAM_SIZE >> 20), (void *)QEMU_DRAM_BASE);
	return 0;
#endif
}
