/* SPDX-License-Identifier: MIT
 * SWEETXNU payload checks and the address plan: see xnu.h.
 */
#include "xnu.h"

_Static_assert(__builtin_offsetof(struct xnu_payload, cmdline) == 0x48, "payload: cmdline at 0x48");
_Static_assert(__builtin_offsetof(struct xnu_payload, segs) == 0x2a8, "payload: segments at 0x2a8");
_Static_assert(__builtin_offsetof(struct xnu_boot_args, CommandLine) == 108, "boot_args: CommandLine");
_Static_assert(sizeof(struct xnu_boot_args) == 736, "boot_args: size");

static uint64_t page_up(uint64_t v)
{
	return (v + XNU_PAGE - 1) & ~(uint64_t)(XNU_PAGE - 1);
}

const char *xnu_plan(const struct xnu_payload *p, uint64_t len, uint64_t src_pa,
                     const struct xnu_window *w, unsigned nw, struct xnu_plan *out)
{
	static const char magic[8] = XNU_MAGIC;
	if (len < sizeof(*p))
		return "payload shorter than its header";
	for (unsigned i = 0; i < 8; i++)
		if (p->magic[i] != magic[i])
			return "not a SWEETXNU payload";
	if (p->version != 1)
		return "SWEETXNU version is not 1";
	if (!p->nsegs || p->nsegs > XNU_MAX_SEGS ||
	    sizeof(*p) + (uint64_t)p->nsegs * sizeof(struct xnu_seg) > len)
		return "bad segment count";
	if (p->adt_off > len || p->adt_size > len - p->adt_off || !p->adt_size)
		return "device tree outside the payload";
	if (p->adt_size > 0xffffffffu)
		return "device tree too large";
	if (p->cmdline[XNU_LINE - 1] != 0)
		return "command line not terminated";
	if (p->phys_base & (XNU_PAGE - 1) || p->virt_base & (XNU_PAGE - 1))
		return "RAM base not page aligned";
	const struct xnu_window *win = 0;
	for (unsigned i = 0; i < nw; i++)
		if (p->phys_base >= w[i].base && p->phys_base < w[i].end)
			win = &w[i];
	if (!win || !p->mem_size || p->mem_size > win->end - p->phys_base)
		return "payload RAM outside the allowed window";
	if (p->kernel_end < p->virt_base || p->kernel_end - p->virt_base >= p->mem_size)
		return "kernel larger than its RAM";
	uint64_t mem_end = p->phys_base + p->mem_size;
	if (src_pa < mem_end && src_pa + len > p->phys_base)
		return "payload lies inside the RAM it is copied to";
	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct xnu_seg *s = &p->segs[i];
		if (s->vmaddr < p->virt_base || s->file_size > s->vmsize ||
		    s->file_off > len || s->file_size > len - s->file_off ||
		    s->vmsize > p->kernel_end - p->virt_base ||
		    s->vmaddr - p->virt_base > (p->kernel_end - p->virt_base) - s->vmsize)
			return "segment outside the kernel";
	}
	uint64_t kend = p->phys_base + (p->kernel_end - p->virt_base);
	uint64_t adt = page_up(kend);
	uint64_t ba = page_up(adt + p->adt_size);
	if (ba + XNU_PAGE > mem_end)
		return "no room for the device tree and boot_args";
	if (p->entry_va < p->virt_base || p->entry_va >= p->kernel_end)
		return "entry point outside the kernel";
	out->entry_pa = p->phys_base + (p->entry_va - p->virt_base);
	out->adt_pa = adt;
	out->boot_args_pa = ba;
	out->kernel_end_pa = kend;
	out->mem_end_pa = mem_end;
	return 0;
}
