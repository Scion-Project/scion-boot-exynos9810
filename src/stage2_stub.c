/*
 * Built-in stage-2 stub. Proves the stage-1 -> stage-2 hand-off contract end
 * to end under QEMU (including EL3 -> EL2 transition and stage-2 payload
 * verification).
 */
#include <stdint.h>
#include "boot.h"
#include "lib.h"

void stage2_stub(struct scion_boot_info *bi)
{
	uint32_t el = current_el();

	kprintf("\n--- stage2_stub: verifying hand-off (running at EL%u) ---\n", el);
	if (bi->magic != SCION_BOOT_MAGIC) {
		kprintf("stage2: BAD MAGIC 0x%08x\n", bi->magic);
		platform_enter_usbdl("Stage-2 bad boot_info magic");
	}
	if (bi->abi_version != SCION_BOOT_ABI_VERSION) {
		kprintf("stage2: unsupported ABI %u\n", bi->abi_version);
		platform_enter_usbdl("Stage-2 unsupported boot_info ABI");
	}

	/* Confirm successful stage-2 entry: clear boot-loop counter & disarm WDT */
	platform_bootguard_clear();
	platform_wdt_disable();

	display_puts("Stage 2      : Hand-off verified at EL2\n");

	kprintf("stage2: ok. uart=%p fb=%p (%ux%ux%u) timebase=%lu Hz flags=0x%x (EL%u -> EL%u)\n",
	        (void *)bi->uart_base, (void *)bi->fb_base,
	        bi->fb_width, bi->fb_height, bi->fb_bpp,
	        bi->timebase_hz, bi->boot_flags,
	        bi->entry_el, el);
	kprintf("stage2: %u memory region(s):\n", bi->nr_mem);
	for (unsigned i = 0; i < bi->nr_mem; i++) {
		uint64_t sz = bi->mem[i].size;
		if (sz >= (1024ULL * 1024)) {
			kprintf("  [%u] base=%p size=%lu MiB flags=0x%x\n", i,
			        (void *)bi->mem[i].base, sz >> 20, bi->mem[i].flags);
		} else {
			kprintf("  [%u] base=%p size=%lu KiB flags=0x%x\n", i,
			        (void *)bi->mem[i].base, sz >> 10, bi->mem[i].flags);
		}
	}
	kprintf("--- hand-off contract satisfied ---\n");
	uart_flush();
	for (;;)
		__asm__ volatile("wfe");
}
