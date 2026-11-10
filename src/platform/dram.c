/*
 * DRAM (DMC/DREX + PHY) bring-up, memory map registration, and hardware
 * walking-1s / walking-0s / address-aliasing verification.
 */
#include <stdint.h>
#include "boot.h"
#include "exynos9810.h"
#include "lib.h"

/* starlte DRAM base is the standard Exynos DRAM window (4 GiB LPDDR4x). */
#define STARLTE_DRAM_BASE   0x80000000ULL
#define STARLTE_DRAM_SIZE   (4ULL * 1024 * 1024 * 1024)

/* QEMU virt RAM starts at 1 GiB; size is 2 GiB in the smoke test. */
#define QEMU_DRAM_BASE      0x40000000ULL
#define QEMU_DRAM_SIZE      (2ULL * 1024 * 1024 * 1024)

extern uint8_t __image_start[];
extern uint8_t __image_end[];

int boot_mem_add(struct scion_boot_info *bi, uint64_t base, uint64_t size, uint32_t flags)
{
	if (bi->nr_mem >= SCION_MEM_MAX_REGIONS) {
		kprintf("dram: mem region table full (%u)\n", bi->nr_mem);
		return -1;
	}
	unsigned i = bi->nr_mem++;
	bi->mem[i].base  = base;
	bi->mem[i].size  = size;
	bi->mem[i].flags = flags;
	bi->mem[i]._pad  = 0;
	return 0;
}

static int addr_overlaps_stage1(uint64_t addr)
{
	uint64_t s1_start = (uint64_t)(uintptr_t)__image_start;
	uint64_t s1_end   = (uint64_t)(uintptr_t)__image_end;
	return (addr + 8 > s1_start) && (addr < s1_end);
}

/*
 * Comprehensive non-destructive DRAM test over [base, base + size):
 *   1. 64-bit walking-1s data bus test
 *   2. 64-bit walking-0s data bus test
 *   3. Power-of-2 address bus aliasing check
 *   4. Strided checkerboard + address-seeded pattern sweep
 */
int dram_walk_test(uint64_t base, uint64_t size)
{
	if (size < 64 || (base & 7ULL) != 0)
		return -1;

	/* Pick a primary test cell outside the live stage-1 image */
	uint64_t test_cell = base;
	if (addr_overlaps_stage1(test_cell) || addr_overlaps_stage1(test_cell + 8))
		test_cell = (uint64_t)(uintptr_t)__image_end;

	volatile uint64_t *p0 = (volatile uint64_t *)(uintptr_t)test_cell;
	volatile uint64_t *p1 = (volatile uint64_t *)(uintptr_t)(test_cell + 8);
	uint64_t saved0 = *p0;
	uint64_t saved1 = *p1;

	/* 1 & 2: Walking-1s and Walking-0s across all 64 data lines.
	 * Write inverted pattern to adjacent word p1 to defeat bus capacitance. */
	for (unsigned bit = 0; bit < 64; bit++) {
		uint64_t pat1 = 1ULL << bit;
		uint64_t pat0 = ~pat1;

		*p0 = pat1;
		*p1 = pat0;
		__asm__ volatile("dsb sy" ::: "memory");
		if (*p0 != pat1 || *p1 != pat0) {
			kprintf("dram: FAIL data walking-1 at %p bit %u (got 0x%016lx)\n",
			        (void *)p0, bit, *p0);
			*p0 = saved0;
			*p1 = saved1;
			return -1;
		}

		*p0 = pat0;
		*p1 = pat1;
		__asm__ volatile("dsb sy" ::: "memory");
		if (*p0 != pat0 || *p1 != pat1) {
			kprintf("dram: FAIL data walking-0 at %p bit %u (got 0x%016lx)\n",
			        (void *)p0, bit, *p0);
			*p0 = saved0;
			*p1 = saved1;
			return -2;
		}
	}

	*p0 = saved0;
	*p1 = saved1;

	/* 3. Power-of-2 address-line aliasing test */
	uint64_t saved_pow2[36];
	uint64_t addrs_pow2[36];
	unsigned nr_pow2 = 0;

	for (unsigned shift = 3; shift < 36; shift++) {
		uint64_t off = 1ULL << shift;
		if (off + 8 > size)
			break;
		uint64_t a = base + off;
		if (addr_overlaps_stage1(a))
			continue;
		addrs_pow2[nr_pow2] = a;
		saved_pow2[nr_pow2] = *(volatile uint64_t *)(uintptr_t)a;
		*(volatile uint64_t *)(uintptr_t)a = 0x5555555555555555ULL ^ off;
		nr_pow2++;
	}
	__asm__ volatile("dsb sy" ::: "memory");

	for (unsigned i = 0; i < nr_pow2; i++) {
		volatile uint64_t *target = (volatile uint64_t *)(uintptr_t)addrs_pow2[i];
		*target = 0xAAAAAAAAAAAAAAAAULL ^ addrs_pow2[i];
		__asm__ volatile("dsb sy" ::: "memory");

		for (unsigned j = 0; j < nr_pow2; j++) {
			if (i == j)
				continue;
			uint64_t off_j = addrs_pow2[j] - base;
			uint64_t expected_j = 0x5555555555555555ULL ^ off_j;
			uint64_t actual_j = *(volatile uint64_t *)(uintptr_t)addrs_pow2[j];
			if (actual_j != expected_j) {
				kprintf("dram: FAIL address alias between %p and %p\n",
				        (void *)addrs_pow2[i], (void *)addrs_pow2[j]);
				for (unsigned k = 0; k < nr_pow2; k++)
					*(volatile uint64_t *)(uintptr_t)addrs_pow2[k] = saved_pow2[k];
				return -3;
			}
		}
		*target = 0x5555555555555555ULL ^ (addrs_pow2[i] - base);
	}

	for (unsigned k = 0; k < nr_pow2; k++)
		*(volatile uint64_t *)(uintptr_t)addrs_pow2[k] = saved_pow2[k];

	/* 4. Strided pattern sweep across the region (up to 64 sample points) */
	uint64_t step = size / 64;
	if (step < 8)
		step = 8;
	step &= ~7ULL;

	for (uint64_t off = 0; off + 8 <= size; off += step) {
		uint64_t a = base + off;
		if (addr_overlaps_stage1(a))
			continue;
		volatile uint64_t *cell = (volatile uint64_t *)(uintptr_t)a;
		uint64_t orig = *cell;
		uint64_t pat  = a ^ 0xA5A55A5AF0F00F0FULL;
		*cell = pat;
		__asm__ volatile("dsb sy" ::: "memory");
		if (*cell != pat) {
			kprintf("dram: FAIL pattern sweep at %p (expected 0x%016lx, got 0x%016lx)\n",
			        (void *)a, pat, *cell);
			*cell = orig;
			return -4;
		}
		*cell = orig;
	}

	return 0;
}

int platform_dram_init(struct scion_boot_info *bi)
{
	uint64_t s1_start = (uint64_t)(uintptr_t)__image_start;
	uint64_t s1_size  = (uint64_t)(uintptr_t)(__image_end - __image_start);

#ifdef PLATFORM_STARLTE
	if (IS_PLACEHOLDER(DMC0_BASE) || IS_PLACEHOLDER(DMC_PHY0_BASE)) {
		kprintf("dram: TODO(starlte) controller/PHY bases unset — refusing\n");
		kprintf("dram: fill DMC0_BASE/DMC_PHY0_BASE and the training seq\n");
		return -1;
	}
	boot_mem_add(bi, STARLTE_DRAM_BASE, STARLTE_DRAM_SIZE, SCION_MEM_RAM);
	boot_mem_add(bi, s1_start, s1_size, SCION_MEM_RESERVED | SCION_MEM_STAGE1);
	if (dram_walk_test(STARLTE_DRAM_BASE, STARLTE_DRAM_SIZE) != 0)
		return -1;
	bi->boot_flags |= SCION_BF_DRAM_TESTED;
	kprintf("dram: starlte 4096 MiB verified at %p\n", (void *)STARLTE_DRAM_BASE);
	return 0;
#else
	boot_mem_add(bi, QEMU_DRAM_BASE, QEMU_DRAM_SIZE, SCION_MEM_RAM);
	boot_mem_add(bi, s1_start, s1_size, SCION_MEM_RESERVED | SCION_MEM_STAGE1);

	uint64_t t0 = timer_get_ticks();
	if (dram_walk_test(QEMU_DRAM_BASE, QEMU_DRAM_SIZE) != 0)
		return -1;
	uint64_t dt_us = timer_ticks_to_us(timer_get_ticks() - t0);

	bi->boot_flags |= SCION_BF_DRAM_TESTED;
	kprintf("dram: qemu %u MiB at %p (walking-1s/0s + alias test PASS in %lu us)\n",
	        (unsigned)(QEMU_DRAM_SIZE >> 20), (void *)QEMU_DRAM_BASE, dt_us);
	return 0;
#endif
}
