/*
 * Brick-proof hardware safety & guaranteed USB recovery (usbdl / Hubble).
 *
 * Guarantees that scion-boot can never damage or permanently brick the device:
 *   1. Write-protection guard blocking any write to OTP/eFuses (0x10000000),
 *      UFS flash controller (0x11120000), IROM (0x00000000), or the BootROM
 *      IRAM function table (0x02020000..0x02021400).
 *   2. Early hardware button check on Alive GPIO bank GPA0 (0x14050024):
 *        - VolDown + Bixby held -> immediate jump to BootROM USB Download Mode
 *          (04e8:1234 DNW, recoverable via halal-beef/hubble or houston-pub).
 *        - VolUp held -> force recovery flag and enter interactive shell.
 *   3. Boot-loop deadman counter in PMU_INFORM1 (0x14060804):
 *        - Tracks consecutive unconfirmed boot attempts across warm resets /
 *          watchdog resets. If >= 3 attempts fail, automatically aborts normal
 *          boot and drops into recovery / USB Download Mode.
 *   4. Clean PMIC power-hold (PS_HOLD @ 0x1406330C) management.
 */
#include <stdint.h>
#include "boot.h"
#include "exynos9810.h"
#include "lib.h"

static int ranges_overlap(uint64_t a_start, uint64_t a_len,
                          uint64_t b_start, uint64_t b_len)
{
	if (a_len == 0 || b_len == 0)
		return 0;
	uint64_t a_end = a_start + a_len;
	uint64_t b_end = b_start + b_len;
	return (a_start < b_end) && (b_start < a_end);
}

int platform_addr_is_write_protected(uint64_t addr, uint64_t len)
{
#ifdef PLATFORM_STARLTE
	/* Never allow writes to IROM or the BootROM IRAM function pointer table */
	if (ranges_overlap(addr, len, EXYNOS9810_IROM_BASE, EXYNOS9810_IROM_SIZE))
		return 1;
	if (ranges_overlap(addr, len, EXYNOS9810_IRAM_BASE, 0x1400UL))
		return 1;
	/* Never allow writes to OTP/eFuse controller or UFS flash controller */
	if (ranges_overlap(addr, len, OTP_CON_BASE, OTP_CON_SIZE))
		return 1;
	if (ranges_overlap(addr, len, UFS_HCI_BASE, UFS_HCI_SIZE))
		return 1;
#else
	/* On QEMU virt, protect the simulated flash region 0x00000000..0x08000000 */
	if (ranges_overlap(addr, len, 0x00000000UL, 0x08000000UL))
		return 1;
#endif
	return 0;
}

uint32_t platform_buttons_read(void)
{
	uint32_t mask = 0;
#ifdef PLATFORM_STARLTE
	/* Alive GPIO GPA0 pins are active-low (0 = pressed) */
	uint32_t dat = mmio_r32(GPA0_DAT);
	if (!(dat & GPA0_PIN_VOLUP))
		mask |= SCION_BTN_VOLUP;
	if (!(dat & GPA0_PIN_VOLDOWN))
		mask |= SCION_BTN_VOLDOWN;
	if (!(dat & GPA0_PIN_BIXBY))
		mask |= SCION_BTN_BIXBY;
#endif
	return mask;
}

uint32_t platform_bootcount_get(void)
{
#ifdef PLATFORM_STARLTE
	uint32_t raw = mmio_r32(PMU_INFORM1);
	if ((raw & SCION_BOOTCOUNT_MAGIC_MASK) == SCION_BOOTCOUNT_MAGIC_VAL)
		return raw & 0xFFu;
#endif
	return 0;
}

void platform_bootguard_clear(void)
{
#ifdef PLATFORM_STARLTE
	mmio_w32(PMU_INFORM1, 0);
#endif
}

/*
 * Returns:
 *   0 = normal boot allowed
 *   1 = recovery shell requested (VolUp or boot-loop threshold reached)
 *  (Or enters usbdl directly if VolDown + Bixby is held)
 */
int platform_bootguard_check(struct scion_boot_info *bi)
{
	uint32_t btns = platform_buttons_read();

	if ((btns & (SCION_BTN_VOLDOWN | SCION_BTN_BIXBY)) ==
	    (SCION_BTN_VOLDOWN | SCION_BTN_BIXBY)) {
		kputs("safety: VolDown + Bixby held! Entering BootROM USB Download Mode...\n");
		platform_enter_usbdl("Hardware key combo (VolDown + Bixby)");
		return 1;
	}

	if (btns & SCION_BTN_VOLUP) {
		kputs("safety: VolUp held — forcing recovery mode & debug console\n");
		bi->boot_flags |= SCION_BF_RECOVERY | SCION_BF_SAFE_MODE;
		return 1;
	}

#ifdef PLATFORM_STARLTE
	uint32_t count = platform_bootcount_get() + 1;
	if (count >= SCION_MAX_BOOT_ATTEMPTS) {
		kprintf("safety: Boot-loop detected (%u consecutive failed boots)!\n", count);
		kputs("safety: Clearing counter and entering Safe Recovery Mode.\n");
		platform_bootguard_clear();
		bi->boot_flags |= SCION_BF_RECOVERY | SCION_BF_SAFE_MODE;
		return 1;
	}
	mmio_w32(PMU_INFORM1, SCION_BOOTCOUNT_MAGIC_VAL | (count & 0xFFu));
	kprintf("safety: boot-guard attempt %u/%u armed in PMU_INFORM1\n",
	        count, SCION_MAX_BOOT_ATTEMPTS);
#else
	kputs("safety: write-guard active; boot-guard ready\n");
#endif
	return 0;
}

void platform_enter_usbdl(const char *reason)
{
	platform_wdt_disable();
	platform_bootguard_clear();

	kputs("\n============================================================\n");
	kprintf("RECOVERY MODE — Exynos USB Download (usbdl / 04e8:1234 DNW)\n");
	kprintf("Reason: %s\n", reason ? reason : "operator request");
	kputs("Recover with:\n");
	kputs("  - Hubble : https://github.com/halal-beef/hubble\n");
	kputs("             python3 hubble.py -b <STOCK_BL_TAR>\n");
	kputs("  - Houston: https://github.com/halal-beef/houston-pub\n");
	kputs("============================================================\n");
	uart_flush();

#ifdef PLATFORM_STARLTE
	/* Latch Odin / USB download flags in PMU INFORM registers */
	mmio_w32(PMU_INFORM2, EXYNOS_BOOT_ODIN_MAGIC);
	mmio_w32(PMU_INFORM3, EXYNOS_BOOT_USBDL_MAGIC);

	/* Keep PMIC power rails latched while USB enumerates */
	mmio_w32(PMU_PS_HOLD_CONTROL, PMU_PS_HOLD_VAL_LATCH);

	/* Check if BootROM USB function pointers in IRAM (0x020200E0 / 0x020200DC)
	 * are intact and point within the Exynos 9810 IROM (0x00000000..0x00020000). */
	uint32_t fn_reinit  = mmio_r32(PTR_USB_REINIT);
	uint32_t fn_receive = mmio_r32(PTR_USB_RECEIVE);

	if (fn_reinit > 0 && fn_reinit < EXYNOS9810_IROM_SIZE &&
	    fn_receive > 0 && fn_receive < EXYNOS9810_IROM_SIZE) {
		kprintf("usbdl: invoking IROM usb_reinit(0x%08x) & usb_receive(0x%08x)\n",
		        fn_reinit, fn_receive);
		uart_flush();

		/* Enable USB PHY clock gate bit (0x11100004 |= 0x100) per Exynos 9810 BL1 */
		mmio_setbits32(USB_PHY_CTRL_REG, 0x100u);

		typedef void (*irom_usb_reinit_t)(uint32_t, uint32_t, uint32_t);
		typedef uint32_t (*irom_usb_receive_t)(uint32_t, uint32_t);

		((irom_usb_reinit_t)(uintptr_t)fn_reinit)(IRAM_USB_STRUCT_ADDR, 300u, 0u);
		((irom_usb_receive_t)(uintptr_t)fn_receive)(IRAM_USBDL_BUF_ADDR, IRAM_USBDL_BUF_SIZE);

		/* If a recovery payload (e.g. fwbl1 / open-mini-bl1) was uploaded to
		 * 0x02024800 via DNW protocol, branch to it at EL3. */
		void (*payload_entry)(void) = (void (*)(void))(uintptr_t)IRAM_USBDL_BUF_ADDR;
		payload_entry();
	}

	/* Fallback: warm reset into BootROM / S-Boot Download Mode */
	kputs("usbdl: IROM pointers unavailable, resetting into Download Mode...\n");
	uart_flush();
	mmio_w32(PMU_SWRESET, 0x1u);
#endif
	for (;;)
		__asm__ volatile("wfe");
}

void platform_print_safety_status(void)
{
	uint32_t btns = platform_buttons_read();
	kputs("Hardware Safety & Recovery Status:\n");
	kputs("  Storage / Fuse Access : READ-ONLY (OTP & UFS writes blocked)\n");
	kputs("  Protected Ranges      :\n");
#ifdef PLATFORM_STARLTE
	kprintf("    - IROM              : 0x%08lx .. 0x%08lx\n",
	        EXYNOS9810_IROM_BASE, EXYNOS9810_IROM_BASE + EXYNOS9810_IROM_SIZE);
	kprintf("    - IRAM BootROM Tbl  : 0x%08lx .. 0x%08lx\n",
	        EXYNOS9810_IRAM_BASE, EXYNOS9810_IRAM_BASE + 0x1400UL);
	kprintf("    - OTP / eFuse Ctrl  : 0x%08lx .. 0x%08lx\n",
	        OTP_CON_BASE, OTP_CON_BASE + OTP_CON_SIZE);
	kprintf("    - UFS Host Ctrl     : 0x%08lx .. 0x%08lx\n",
	        UFS_HCI_BASE, UFS_HCI_BASE + UFS_HCI_SIZE);
#else
	kputs("    - QEMU Boot Flash   : 0x00000000 .. 0x08000000\n");
#endif
	kprintf("  Boot-Loop Counter     : %u / %u\n",
	        platform_bootcount_get(), SCION_MAX_BOOT_ATTEMPTS);
	kprintf("  Buttons (GPA0)        : VolUp=%d VolDown=%d Bixby=%d\n",
	        (btns & SCION_BTN_VOLUP) != 0,
	        (btns & SCION_BTN_VOLDOWN) != 0,
	        (btns & SCION_BTN_BIXBY) != 0);
}
