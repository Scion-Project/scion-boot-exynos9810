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
#define SCION_MEM_STAGE1    (1u << 2) /* stage-1 bootloader image/stack */
#define SCION_MEM_STAGE2    (1u << 3) /* loaded stage-2 payload */
#define SCION_MEM_FB        (1u << 4) /* display framebuffer carveout */

/*
 * Stage-2 image header (prepended to a standalone stage-2 binary in flash or
 * staging RAM). Verified by stage-1 before jumping to EL2.
 */
#define SCION_STAGE2_MAGIC   0x53434E32u /* 'SCN2' */
#define SCION_STAGE2_VERSION 1u

#define SCION_S2_FLAG_SKIP_HASH (1u << 0) /* dev only: only check CRC32 */
#define SCION_S2_FLAG_PIC       (1u << 1) /* position-independent payload */

struct scion_stage2_hdr {
	uint32_t magic;          /* SCION_STAGE2_MAGIC */
	uint32_t version;        /* SCION_STAGE2_VERSION */
	uint32_t header_size;    /* sizeof(struct scion_stage2_hdr) */
	uint32_t flags;          /* SCION_S2_FLAG_* */
	uint64_t load_addr;      /* destination PA (0 = execute in place) */
	uint64_t entry_offset;   /* offset in bytes from payload start to entry */
	uint64_t image_size;     /* payload size in bytes (excluding header) */
	uint32_t crc32;          /* IEEE 802.3 CRC32 over payload */
	uint32_t _pad0;
	uint8_t  sha256[32];     /* SHA-256 digest over payload */
	char     name[32];       /* NUL-terminated descriptive name */
};

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

	/* Diagnostics & stage-2 metadata populated by stage 1 */
	uint32_t entry_el;           /* EL stage-1 booted at (e.g., 3 or 2) */
	uint32_t handoff_el;         /* EL stage-2 is entered at (target: 2) */
	uint64_t stage2_base;        /* physical base of verified stage-2 image */
	uint64_t stage2_size;        /* size in bytes of verified stage-2 image */
} __attribute__((aligned(64)));

#define SCION_BF_RECOVERY    (1u << 0) /* operator requested recovery slot */
#define SCION_BF_WARM_BOOT   (1u << 1) /* resuming, DRAM contents valid */
#define SCION_BF_DRAM_TESTED (1u << 2) /* DRAM walking-bit/pattern test passed */
#define SCION_BF_STAGE2_OK   (1u << 3) /* stage-2 CRC32 + SHA-256 verified */
#define SCION_BF_SAFE_MODE   (1u << 4) /* boot-loop guard or button recovery triggered */

#endif /* SCION_BOOT_H */
