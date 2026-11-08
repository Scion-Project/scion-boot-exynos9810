/*
 * scion_boot_info — the stage-1 -> stage-2 hand-off ABI.
 *
 * This struct is the single source of truth for the contract described in
 * docs/handoff-contract.md. The first stage fills it and passes its physical
 * address in x0; the second stage reads it and MUST reject an abi_version it
 * does not understand.
 *
 * Keep it POD, fixed-width, and naturally aligned. Never reorder or resize a
 * field without bumping SCION_BOOT_ABI_VERSION.
 */
#ifndef SCION_BOOT_H
#define SCION_BOOT_H

#include <stdint.h>

#define SCION_BOOT_MAGIC        0x53434E42u /* 'SCNB' */
#define SCION_BOOT_ABI_VERSION  1u
#define SCION_MEM_MAX_REGIONS   8u

struct scion_mem_region {
	uint64_t base;   /* physical base */
	uint64_t size;   /* bytes; 0 = unused slot */
	uint32_t flags;  /* SCION_MEM_* */
	uint32_t _pad;
};

#define SCION_MEM_RAM       (1u << 0) /* normal usable DRAM */
#define SCION_MEM_RESERVED  (1u << 1) /* carveout, do not touch */

struct scion_boot_info {
	uint32_t magic;              /* SCION_BOOT_MAGIC */
	uint32_t abi_version;        /* SCION_BOOT_ABI_VERSION */
	uint32_t size;               /* sizeof(struct scion_boot_info) */
	uint32_t boot_flags;         /* SCION_BF_* */

	uint64_t uart_base;          /* console UART the loader initialised */
	uint64_t cpu_release_addr;   /* spin-table secondaries wait on */

	uint32_t nr_mem;             /* valid entries in mem[] */
	uint32_t _pad0;
	struct scion_mem_region mem[SCION_MEM_MAX_REGIONS];

	/* Display, for stage 2's splash. 0 if stage 1 didn't bring up the panel. */
	uint64_t fb_base;
	uint32_t fb_width;
	uint32_t fb_height;
	uint32_t fb_stride;          /* bytes per row */
	uint32_t fb_bpp;             /* bits per pixel */

	uint64_t timebase_hz;        /* CNTFRQ, for stage 2 / the OS */
} __attribute__((aligned(64)));

#define SCION_BF_RECOVERY   (1u << 0) /* operator requested recovery slot */
#define SCION_BF_WARM_BOOT  (1u << 1) /* resuming, DRAM contents valid */

#endif /* SCION_BOOT_H */
