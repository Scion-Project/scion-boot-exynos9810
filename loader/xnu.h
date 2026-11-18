/* SPDX-License-Identifier: MIT
 * Starting XNU from a SWEETXNU payload (the format of scion-kernel's
 * tools/ios27/herolte/mkpayload.py, version 1), written from that format's
 * description: the payload's segments are copied flat to phys_base +
 * (vmaddr - virt_base), the Apple device tree goes right after the kernel's
 * end, and a boot_args page (XNU's struct boot_args, Revision 2 Version 2)
 * after that; XNU is entered at EL1 with the MMU off and x0 = boot_args.
 *
 * xnu_plan() is plain C (host-tested): it checks the payload against the RAM
 * windows the platform allows and works out every address. xnu_place() does
 * the copies (loader only).
 */
#ifndef SCION_XNU_H
#define SCION_XNU_H

#include <stdint.h>
#include "boot.h"

#define XNU_MAGIC          "SWEETXNU"
#define XNU_LINE           SCION_CMDLINE_MAX   /* 608 */
#define XNU_MAX_SEGS       32u
#define XNU_PAGE           4096u

struct xnu_seg {
	uint64_t vmaddr, vmsize, file_off, file_size;
};

struct xnu_payload {
	char     magic[8];
	uint32_t version, nsegs;
	uint64_t virt_base, entry_va, adt_off, adt_size, kernel_end;
	uint64_t phys_base, mem_size;
	char     cmdline[XNU_LINE];
	struct xnu_seg segs[];
};

struct xnu_video {
	uint64_t base, display, row_bytes, width, height, depth;
};

struct xnu_boot_args {
	uint16_t Revision, Version;
	uint64_t virtBase, physBase, memSize, topOfKernelData;
	struct xnu_video Video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char     CommandLine[XNU_LINE];
	uint64_t bootFlags, memSizeActual;
};

/* A RAM range XNU may be given (phys_base .. phys_base + mem_size must lie in one). */
struct xnu_window {
	uint64_t base, end;
};

struct xnu_plan {
	uint64_t entry_pa;
	uint64_t adt_pa;
	uint64_t boot_args_pa;
	uint64_t kernel_end_pa;   /* phys_base + (kernel_end - virt_base) */
	uint64_t mem_end_pa;
};

/* 0 and *out filled, or a reason. len = payload bytes; src_pa = where the payload lies (it must
 * not overlap the window it is copied to). */
const char *xnu_plan(const struct xnu_payload *p, uint64_t len, uint64_t src_pa,
                     const struct xnu_window *w, unsigned nw, struct xnu_plan *out);

#endif
