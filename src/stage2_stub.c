/*
 * Built-in stage-2 stub. NOT the real second stage — just enough to prove the
 * hand-off contract end to end under QEMU: it reads boot_info back and prints
 * it, then halts. The real stage 2 (splash / menu / OS loader) replaces this
 * and lives in its own repo; stage 1 will load+verify it from a partition.
 */
#include <stdint.h>
#include "boot.h"
#include "lib.h"

void stage2_stub(struct scion_boot_info *bi)
{
	kprintf("\n--- stage2_stub: verifying hand-off ---\n");
	if (bi->magic != SCION_BOOT_MAGIC) {
		kprintf("stage2: BAD MAGIC %x\n", bi->magic);
		goto halt;
	}
	if (bi->abi_version != SCION_BOOT_ABI_VERSION) {
		kprintf("stage2: unsupported ABI %u\n", bi->abi_version);
		goto halt;
	}
	kprintf("stage2: ok. uart=%p timebase=%u Hz\n",
	        (void *)bi->uart_base, (unsigned)bi->timebase_hz);
	kprintf("stage2: %u RAM region(s):\n", bi->nr_mem);
	for (unsigned i = 0; i < bi->nr_mem; i++)
		kprintf("  [%u] base=%p size=%u MiB flags=%x\n", i,
		        (void *)bi->mem[i].base,
		        (unsigned)(bi->mem[i].size >> 20), bi->mem[i].flags);
	kprintf("--- hand-off contract satisfied ---\n");
halt:
	for (;;)
		__asm__ volatile("wfe");
}
