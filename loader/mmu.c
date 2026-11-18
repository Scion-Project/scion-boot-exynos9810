/* SPDX-License-Identifier: MIT
 * The loader's identity map. S-Boot enters with the MMU off, where every data access is
 * Device-nGnRnE: hashing a 60 MiB payload that way takes minutes. With the map below it runs
 * from the data cache.
 *
 *   below dram_base            Device-nGnRnE, execute never   SoC registers
 *   wb_base .. wb_end          Normal write-back              the loader, its appended images
 *   rest of dram, dram2        Normal non-cacheable           OS windows, framebuffer, retained RAM
 *   anything else              unmapped
 *
 * Only memory the CPU alone writes is cacheable, so nothing needs maintenance while the loader
 * runs; ldr_jump (entry.S) cleans the whole data cache and turns the MMU off before an OS starts.
 */
#include <stdint.h>
#include "plat.h"

#define DESC_BLOCK   0x1ull
#define DESC_TABLE   0x3ull
#define ATTR_IDX(i)  ((uint64_t)(i) << 2)
#define SH_INNER     (3ull << 8)
#define AF           (1ull << 10)
#define XN           ((1ull << 53) | (1ull << 54))
#define M_DEV        (DESC_BLOCK | ATTR_IDX(0) | AF | XN)
#define M_WB         (DESC_BLOCK | ATTR_IDX(1) | SH_INNER | AF)
#define M_NC         (DESC_BLOCK | ATTR_IDX(2) | SH_INNER | AF)
#define MAIR_VAL     ((0x44ull << 16) | (0xffull << 8) | 0x00ull)
#define TCR_VAL      (28ull | (1ull << 8) | (1ull << 10) | (3ull << 12) | (1ull << 23) | (1ull << 32))
#define GIB          (1ull << 30)
#define MIB2         (2ull << 20)

static uint64_t l1[64] __attribute__((aligned(4096)));
static uint64_t l2[512] __attribute__((aligned(4096)));

void ldr_dcache_all(unsigned op);

static int in(uint64_t a, uint64_t lo, uint64_t hi)
{
	return a >= lo && a < hi;
}

static uint64_t attr_for(const struct plat_map *m, uint64_t a)
{
	if (in(a, m->wb_base, m->wb_end))
		return M_WB;
	if (in(a, m->dram_base, m->dram_end) || (m->dram2_end && in(a, m->dram2_base, m->dram2_end)))
		return M_NC;
	if (a < m->dram_base)
		return M_DEV;
	return 0;
}

int mmu_enable(void)
{
	const struct plat_map *m = plat_map();
	uint64_t sctlr;
	__asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
	if (sctlr & 1)
		return 0;
	uint64_t wb_gib = m->wb_base / GIB;
	if ((m->wb_end - 1) / GIB != wb_gib || (m->wb_base | m->wb_end) & (MIB2 - 1))
		return -1;
	for (uint64_t i = 0; i < 64; i++)
		l1[i] = i == wb_gib ? 0 : attr_for(m, i * GIB) | (i * GIB);
	for (uint64_t i = 0; i < 512; i++) {
		uint64_t a = wb_gib * GIB + i * MIB2;
		l2[i] = attr_for(m, a) | a;
	}
	l1[wb_gib] = (uint64_t)(uintptr_t)l2 | DESC_TABLE;
	ldr_dcache_all(0);
	__asm__ volatile(
		"tlbi vmalle1\n"
		"dsb sy\n"
		"msr mair_el1, %0\n"
		"msr tcr_el1, %1\n"
		"msr ttbr0_el1, %2\n"
		"isb\n" :: "r"(MAIR_VAL), "r"(TCR_VAL), "r"((uint64_t)(uintptr_t)l1) : "memory");
	/* M, C, I on; WXN off (the loader's image is writable and executable) and no alignment
	 * faults, whatever the caller left. */
	sctlr |= 1ull | (1ull << 2) | (1ull << 12);
	sctlr &= ~((1ull << 19) | (1ull << 1));
	__asm__ volatile("msr sctlr_el1, %0\nisb\nic iallu\ndsb sy\nisb" :: "r"(sctlr) : "memory");
	return 0;
}
