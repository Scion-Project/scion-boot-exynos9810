/*
 * Stage-1 main flow:
 *   1. Console UART init
 *   2. Exception vector installation (VBAR_ELx)
 *   3. Watchdog disarm & ARMv8 Generic Timer / MCT init
 *   4. Crypto self-test (CRC32 + SHA-256)
 *   5. Brick-proof boot-guard & hardware button recovery check (GPA0 / PMU_INFORM1)
 *   6. Clock / PMU inspection & DRAM bring-up + walking-bit verification
 *   7. PSCI spin-table init
 *   8. Interactive UART debug console (on keypress, safe mode, or BOOT_SHELL=1)
 *   9. Stage-2 header/hash verification, fail-safe WDT arm, & EL3 -> EL2 handoff
 */
#include <stdint.h>
#include "boot.h"
#include "platform/exynos9810.h"
#include "lib.h"

static uint32_t g_entry_el = 3;

void stage1_set_entry_el(uint32_t el)
{
	g_entry_el = el;
}

extern void stage2_stub(struct scion_boot_info *bi);

static void fill_static_info(struct scion_boot_info *bi)
{
	memset(bi, 0, sizeof(*bi));
	bi->magic       = SCION_BOOT_MAGIC;
	bi->abi_version = SCION_BOOT_ABI_VERSION;
	bi->size        = sizeof(*bi);
	bi->uart_base   = UART_BASE;
	bi->timebase_hz = timer_get_freq();
	bi->entry_el    = g_entry_el;
	bi->handoff_el  = (g_entry_el >= 2) ? 2 : g_entry_el;
	bi->boot_flags  = platform_read_boot_flags();
}

static void handoff(struct scion_boot_info *bi)
{
	static struct scion_stage2_hdr s2_hdr;
	uint64_t entry_pa = 0;

	/* Build and verify the stage-2 descriptor wrapping stage2_stub
	 * (or a loaded partition image on hardware). */
	stage2_build_From_fn(&s2_hdr, NULL, (const void *)(uintptr_t)stage2_stub,
	                     256, "stage2_stub");

	if (stage2_load_and_verify(bi, &s2_hdr, &entry_pa) != 0) {
		kprintf("stage1: FATAL: stage-2 verification failed\n");
		platform_enter_usbdl("Stage-2 image integrity check failed");
	}

	/* Arm fail-safe deadman timer before jumping to Stage 2 so a hung Stage 2
	 * triggers a warm reset into the PMU_INFORM1 boot-loop guard. */
	platform_wdt_arm_failsafe(15);

	kprintf("stage1: handing off to %p (EL%u -> EL%u), x0=%p magic=0x%08x abi=%u\n",
	        (void *)entry_pa, current_el(), bi->handoff_el,
	        (void *)bi, bi->magic, bi->abi_version);
	uart_flush();

	el_drop_and_jump(entry_pa, (uint64_t)(uintptr_t)bi);
}

void stage1_main(void)
{
	static struct scion_boot_info bi;

	uart_init(UART_BASE);
	kprintf("\nscion-boot-exynos9810 stage1 (entered at EL%u)\n", g_entry_el);
	kprintf("platform: %s\n", PLATFORM_NAME);

	exceptions_init();
	platform_wdt_disable();
	platform_timer_init();

	if (crypto_selftest() != 0) {
		kprintf("stage1: FATAL: crypto self-test failed\n");
		platform_enter_usbdl("Crypto self-test failure");
	}
	kprintf("crypto: CRC32 + SHA-256 self-test PASS\n");

	fill_static_info(&bi);

	/* Check hardware recovery buttons (VolDown+Bixby -> usbdl, VolUp -> shell)
	 * and the PMU_INFORM1 boot-loop deadman counter. */
	int force_shell = platform_bootguard_check(&bi);

	muic_init_and_probe();
	platform_clock_init();

	if (platform_dram_init(&bi) != 0) {
		kprintf("stage1: FATAL: DRAM init failed — entering recovery\n");
		platform_enter_usbdl("DRAM initialization / verification failed");
	}

	psci_init(&bi);
	display_init(&bi);

	if (force_shell) {
		shell_run(&bi);
	} else {
		kputs("Press any key within 50ms to enter stage-1 debug console...\n");
		if (shell_check_abort(50))
			shell_run(&bi);
	}

	handoff(&bi);
}
