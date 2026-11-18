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

/*
 * ---- Second-stage loader (loader/, PLATFORM=... make loader) --------------
 *
 * The EL1 loader that Samsung's S-Boot starts from the BOOT partition's kernel
 * slot (docs/STATUS.md). It is an arm64 Image; the images it can start are
 * appended to it, each as an SCN2 record (struct scion_stage2_hdr above, padded
 * to SCION_S2_PAGE bytes so the payload that follows is page aligned), one
 * after the other from the first 4 KiB boundary after the loader's own end.
 * The list ends at the first page that does not start with SCION_STAGE2_MAGIC.
 *
 * Flags bits 8..11 say what the payload is; the loader refuses kinds it does
 * not know. SCION_S2_FLAG_SKIP_HASH is refused by the loader: every payload it
 * starts is checked against its SHA-256 (and, when the loader was built with
 * pinned digests, against that list too).
 */
#define SCION_S2_PAGE          4096u
#define SCION_S2_FLAG_SAFE     (1u << 4) /* the boot-loop guard's known-good entry */
#define SCION_S2_FLAG_DEFAULT  (1u << 5) /* the menu's default entry */
#define SCION_S2_KIND_SHIFT    8
#define SCION_S2_KIND_MASK     (0xfu << SCION_S2_KIND_SHIFT)
#define SCION_S2_KIND_RAW      0u  /* stage-1's stage 2 (not started by the loader) */
#define SCION_S2_KIND_XNU      1u  /* a SWEETXNU payload (tools/ios27/herolte/mkpayload.py) */
#define SCION_S2_KIND_IMAGE    2u  /* an arm64 Image entered in place, x0 = S-Boot's FDT */
#define SCION_S2_KIND(flags)   (((flags) & SCION_S2_KIND_MASK) >> SCION_S2_KIND_SHIFT)
#define SCION_S2_MAX_ENTRIES   8u

/*
 * Re-entry: the loader's second entry point, at its Image base + 0x40 (the
 * first instruction after the 64-byte arm64 Image header), followed by the
 * mark "SCNR" at +0x44 and the record version (u32) at +0x48, which the OS
 * checks before it jumps. Cold boot from S-Boot stays at the base.
 * docs/handoff-contract.md, "Re-entering the loader".
 *
 * At re-entry: EL1, MMU off, every byte the loader will read cleaned to the
 * point of coherency, I-cache invalidated, DAIF masked, the GIC CPU interface
 * of the calling core off, one CPU running (the boot CPU; the others PSCI
 * CPU_OFF), x0 = the physical address of this record (8-byte aligned, outside
 * the loader's range and outside the OS RAM it is about to fill), x1..x3 = 0.
 * The OS has put BOOT's kernel slot (this loader and the records appended to
 * it) back where S-Boot had it, read from the BOOT partition: XNU reuses that
 * RAM. The loader trusts none of it: every record is re-checked (SHA-256)
 * before anything in it is started. Agreed with scion-kernel's
 * docs/ios27/WARM-RESTART.md (the OS side).
 */
#define SCION_REENTRY_MAGIC    0x524E4353u /* 'SCNR' */
#define SCION_REENTRY_VERSION  1u
#define SCION_CMDLINE_MAX      608u        /* XNU's BOOT_LINE_LENGTH, NUL included */
#define SCION_REENTRY_OFFSET   0x40u

#define SCION_RE_REASON_USER     0u /* a restart the user asked for */
#define SCION_RE_REASON_PANIC    1u /* the OS panicked and chose to come back */
#define SCION_RE_REASON_WATCHDOG 2u /* the OS's own hang watchdog fired */
#define SCION_RE_REASON_TEST     3u

#define SCION_RE_BOOT_ENTRY  (1u << 0) /* no menu: start entry[] directly */
#define SCION_RE_CMDLINE     (1u << 1) /* cmdline[] replaces the entry's line ... */
#define SCION_RE_MENU        (1u << 2) /* show the menu without the countdown */
#define SCION_RE_RESTAGED    (1u << 3) /* the OS put BOOT's kernel slot back where S-Boot had it */
#define SCION_RE_WDT_ARMED   (1u << 4) /* the OS left a hardware watchdog running: the loader
                                          pets it while it runs and stops it (WTCON = 0) right
                                          before it enters the next OS ... */
#define SCION_RE_WDT_KEEP    (1u << 5) /* ... unless this is set: then it stays armed */
#define SCION_RE_APPEND      (1u << 6) /* ... with CMDLINE: cmdline[] is appended to the entry's line */

#define SCION_RE_METHOD_XNU      0u /* an entry appended to the loader (entry[]) */
#define SCION_RE_METHOD_ANDROID  1u /* kernel + ramdisk at kernel_pa/ramdisk_pa (the current
                                       loader's Android path; this loader shows its menu) */

/* Where a cold-booting loader keeps S-Boot's FDT for a later re-entry, when the started line
 * has sweetwarm=: S-Boot's own copy is XNU RAM by then. A 4 KiB header (u64 SCION_FDT_SAVE_MAGIC,
 * u64 FDT size) and the FDT at +0x1000; 1 MiB in all. On the S9 below the warm restart's own
 * hand-off area (0x91060000..0x910fffff) and the current loader's USB DMA (0x91000000..). */
#define SCION_FDT_SAVE_STARLTE   0x91100000ull
#define SCION_FDT_SAVE_QEMU      0x5f800000ull
#define SCION_FDT_SAVE_MAGIC     0x31544446524e4353ull /* "SCNRFDT1" */
#define SCION_FDT_SAVE_HDR       0x1000u
#define SCION_FDT_SAVE_MAX       ((1u << 20) - SCION_FDT_SAVE_HDR)

struct scion_reentry {
	uint32_t magic;          /* SCION_REENTRY_MAGIC */
	uint16_t version;        /* SCION_REENTRY_VERSION */
	uint16_t size;           /* sizeof(struct scion_reentry) */
	uint32_t reason;         /* SCION_RE_REASON_* */
	uint32_t flags;          /* SCION_RE_* */
	uint32_t entry;          /* index of the appended record, ~0u = the default */
	uint32_t crc32;          /* IEEE CRC32 of the record with this field 0 */
	char     cmdline[SCION_CMDLINE_MAX];
	uint32_t method;         /* SCION_RE_METHOD_* */
	uint32_t wdt_ms;         /* the armed watchdog's period (information only) */
	uint64_t image_pa;       /* where the OS put BOOT's kernel slot back (the loader's base) */
	uint64_t image_size;
	uint64_t kernel_pa;      /* METHOD_ANDROID */
	uint64_t kernel_size;
	uint64_t ramdisk_pa;
	uint64_t ramdisk_size;
	uint64_t fdt_pa;         /* an FDT for IMAGE entries (x0), 0 = none */
	uint8_t  image_sha256[32]; /* SHA-256 of what follows the loader in the restaged slot
	                              (image_pa + its _image_end .. image_pa + image_size): for a
	                              loader without per-record hashes; this one checks each record */
};

/*
 * Boot-loop guard record: 32 bytes of RAM that survive a warm reset (on the
 * S9 a slot of the exynos-ss page, docs/STATUS.md). Written by the loader
 * before every automatic start; the OS acknowledges a good boot by writing
 * strikes = 0 with a matching crc32 (its address is on the command line as
 * scion_guard=). Anything that does not validate reads as "no strikes", which
 * is what a cold power-on looks like.
 */
#define SCION_GUARD_MAGIC    0x474E4353u /* 'SCNG' */
#define SCION_GUARD_VERSION  1u
#define SCION_GUARD_LIMIT    3u

#define SCION_GUARD_F_SAFE_TRIED (1u << 0) /* the known-good entry was started after a trip */

struct scion_guard_rec {
	uint32_t magic;          /* SCION_GUARD_MAGIC */
	uint16_t version;        /* SCION_GUARD_VERSION */
	uint16_t size;           /* sizeof(struct scion_guard_rec) */
	uint32_t strikes;        /* automatic starts not yet acknowledged */
	uint32_t last_entry;     /* index of the last entry started */
	uint32_t flags;          /* SCION_GUARD_F_* */
	uint32_t crc32;          /* IEEE CRC32 of the record with this field 0 */
	uint64_t _reserved;
};

#endif /* SCION_BOOT_H */
