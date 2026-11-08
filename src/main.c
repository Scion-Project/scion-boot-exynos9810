/*
 * Stage-1 main flow.
 *
 * Order matters and is deliberate:
 *   1. console up FIRST  — nothing is debuggable without it
 *   2. platform init     — clocks, then DRAM (DRAM needs its clocks)
 *   3. build boot_info   — describe the machine we just set up
 *   4. (optional) panel  — so stage 2 can splash
 *   5. hand off          — jump to stage 2 with x0 = &boot_info
 *
 * On hardware, steps 2 and 4 depend on the TODO(starlte) stubs. Under
 * PLATFORM=qemu the stubs are satisfied by fixed virt values so the whole
 * flow runs end to end and prints the contract.
 */
#include <stdint.h>
#include "boot.h"
#include "platform/exynos9810.h"
#include "lib.h"

extern void psci_init(struct scion_boot_info *bi);
extern void platform_clock_init(void);
extern int  platform_dram_init(struct scion_boot_info *bi);
extern void uart_init(uint64_t base);

static uint32_t g_entry_el = 3;

void stage1_set_entry_el(uint32_t el) { g_entry_el = el; }

static void fill_static_info(struct scion_boot_info *bi)
{
	for (unsigned i = 0; i < sizeof(*bi); i++)
		((volatile uint8_t *)bi)[i] = 0;
	bi->magic       = SCION_BOOT_MAGIC;
	bi->abi_version = SCION_BOOT_ABI_VERSION;
	bi->size        = sizeof(*bi);
	bi->uart_base   = UART_BASE;

	uint64_t cntfrq;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(cntfrq));
	bi->timebase_hz = cntfrq;
}

/* Jump to stage 2 at EL2 with x0 = boot_info. On QEMU we don't have a real
 * stage 2 image, so STAGE2_ENTRY points at a tiny built-in stub (stage2_stub)
 * that echoes the contract and halts — proving the hand-off works. Replace
 * with the real stage-2 load+verify path on hardware. */
extern void stage2_stub(struct scion_boot_info *bi);

static void handoff(struct scion_boot_info *bi)
{
	kprintf("stage1: handing off, x0=%p magic=%x abi=%u\n",
	        (void *)bi, bi->magic, bi->abi_version);
	/* TODO(starlte): drop to EL2 and branch to the loaded stage-2 image.
	 * For the skeleton we call the stub directly. */
	stage2_stub(bi);
}

void stage1_main(void)
{
	static struct scion_boot_info bi;

	uart_init(UART_BASE);
	kprintf("\nscion-boot-starlte stage1 (entered at EL%u)\n", g_entry_el);
	kprintf("platform: %s\n", PLATFORM_NAME);

	fill_static_info(&bi);

	platform_clock_init();

	if (platform_dram_init(&bi) != 0) {
		kprintf("stage1: FATAL: DRAM init failed\n");
		for (;;)
			__asm__ volatile("wfe");
	}

	psci_init(&bi);

	/* Panel bring-up is optional; leave fb_* zero if not done. */
	/* TODO(starlte): bring up s6e3ha8 (1440x2960) and set bi.fb_* */

	handoff(&bi);
}
