/*
 * Interactive early debug shell over UART.
 *
 * Activated when the operator presses any key during the autoboot window (or
 * unconditionally when built with -DBOOT_SHELL=1).
 */
#include <stdint.h>
#include <stddef.h>
#include "boot.h"
#include "exynos9810.h"
#include "lib.h"

extern uint8_t __image_end[];

int shell_check_abort(unsigned timeout_ms)
{
#ifdef BOOT_SHELL
	(void)timeout_ms;
	return 1;
#else
	for (unsigned i = 0; i <= timeout_ms; i++) {
		if (uart_tstc()) {
			return 1;
		}
		if (i < timeout_ms)
			mdelay(1);
	}
	return 0;
#endif
}

static void readline(char *buf, size_t max_len)
{
	size_t pos = 0;
	for (;;) {
		platform_wdt_kick();
		int c = uart_getc();
		if (c == '\r' || c == '\n') {
			uart_putc('\n');
			buf[pos] = '\0';
			return;
		}
		if (c == 0x03) { /* Ctrl-C */
			kputs("^C\n");
			buf[0] = '\0';
			return;
		}
		if (c == '\b' || c == 0x7F) {
			if (pos > 0) {
				pos--;
				kputs("\b \b");
			}
			continue;
		}
		if (c >= 0x20 && c < 0x7F && pos + 1 < max_len) {
			buf[pos++] = (char)c;
			uart_putc((char)c);
		}
	}
}

static int tokenize(char *line, char *argv[], int max_args)
{
	int argc = 0;
	char *p = line;
	while (*p && argc < max_args) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (!*p)
			break;
		argv[argc++] = p;
		while (*p && *p != ' ' && *p != '\t')
			p++;
		if (*p)
			*p++ = '\0';
	}
	return argc;
}

static void cmd_help(void)
{
	kputs("Commands:\n"
	      "  help                     Show this command summary\n"
	      "  info                     Display scion_boot_info & memory map\n"
	      "  safety                   Show brick-proof protection & button status\n"
	      "  regs                     Dump AArch64 control & ID registers\n"
	      "  fbtest                   Render color bars on the framebuffer\n"
	      "  muic                     Probe MAX77705 MUIC over HSI2C\n"
	      "  i2cread <bus> <adr> <rg> Read 8-bit register over Exynos HSI2C\n"
	      "  loadx [addr] [max_len]   Receive binary over UART via XMODEM-CRC\n"
	      "  md32 <addr> [count]      Dump 32-bit words\n"
	      "  md64 <addr> [count]      Dump 64-bit words\n"
	      "  hexdump <addr> [bytes]   Hex + ASCII memory dump\n"
	      "  mw32 <addr> <val>        Write 32-bit word (write-guarded)\n"
	      "  mw64 <addr> <val>        Write 64-bit word (write-guarded)\n"
	      "  memtest [addr] [size]    Run walking-1s/0s & alias test\n"
	      "  crc32 <addr> <len>       Compute IEEE 802.3 CRC32\n"
	      "  sha256 <addr> <len>      Compute FIPS 180-4 SHA-256\n"
	      "  cpuon <id> <entry>       Power on secondary CPU via PMU/PSCI\n"
	      "  usbdl                    Enter Exynos BootROM USB Download (04e8:1234)\n"
	      "  boot                     Continue to stage-2 handoff\n"
	      "  reboot                   Warm-reset the platform\n"
	      "  poweroff                 Cleanly release PS_HOLD to power off\n");
}

static void cmd_info(const struct scion_boot_info *bi)
{
	kprintf("Platform     : %s\n", PLATFORM_NAME);
	kprintf("Boot Info PA : %p (magic=0x%08x abi=%u size=%u)\n",
	        (void *)bi, bi->magic, bi->abi_version, bi->size);
	kprintf("Exception Lv : booted at EL%u, current EL%u\n",
	        bi->entry_el, current_el());
	kprintf("UART Base    : %p\n", (void *)bi->uart_base);
	kprintf("Framebuffer  : %p (%ux%ux%u stride=%u)\n",
	        (void *)bi->fb_base, bi->fb_width, bi->fb_height,
	        bi->fb_bpp, bi->fb_stride);
	kprintf("Spin-Table   : %p (parked secondaries: %u)\n",
	        (void *)bi->cpu_release_addr, psci_secondary_count());
	kprintf("Timer Freq   : %lu Hz (ticks=%lu)\n",
	        bi->timebase_hz, timer_get_ticks());
	kprintf("Boot Flags   : 0x%08x\n", bi->boot_flags);
	kprintf("Memory Map   : %u region(s)\n", bi->nr_mem);
	for (unsigned i = 0; i < bi->nr_mem; i++) {
		kprintf("  [%u] base=0x%016lx size=0x%016lx flags=0x%x\n",
		        i, bi->mem[i].base, bi->mem[i].size, bi->mem[i].flags);
	}
}

static void cmd_regs(void)
{
	uint64_t midr, mpidr, daif, sctlr = 0, vbar = 0;
	uint32_t el = current_el();

	__asm__ volatile("mrs %0, midr_el1"  : "=r"(midr));
	__asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
	__asm__ volatile("mrs %0, daif"      : "=r"(daif));

	if (el == 3) {
		__asm__ volatile("mrs %0, sctlr_el3" : "=r"(sctlr));
		__asm__ volatile("mrs %0, vbar_el3"  : "=r"(vbar));
	} else if (el == 2) {
		__asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
		__asm__ volatile("mrs %0, vbar_el2"  : "=r"(vbar));
	} else {
		__asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
		__asm__ volatile("mrs %0, vbar_el1"  : "=r"(vbar));
	}

	kprintf("  CurrentEL  = EL%u\n", el);
	kprintf("  MIDR_EL1   = 0x%016lx\n", midr);
	kprintf("  MPIDR_EL1  = 0x%016lx\n", mpidr);
	kprintf("  SCTLR_EL%u  = 0x%016lx\n", el, sctlr);
	kprintf("  VBAR_EL%u   = 0x%016lx\n", el, vbar);
	kprintf("  DAIF       = 0x%016lx\n", daif);
	kprintf("  CNTFRQ_EL0 = %lu Hz\n", timer_get_freq());
	kprintf("  CNTPCT_EL0 = %lu\n", timer_get_ticks());
}

static void cmd_hexdump(uint64_t addr, uint64_t len)
{
	const uint8_t *p = (const uint8_t *)(uintptr_t)addr;
	for (uint64_t off = 0; off < len; off += 16) {
		kprintf("0x%016lx: ", addr + off);
		for (unsigned i = 0; i < 16; i++) {
			if (off + i < len)
				kprintf("%02x ", p[off + i]);
			else
				kputs("   ");
		}
		kputs(" |");
		for (unsigned i = 0; i < 16 && (off + i) < len; i++) {
			uint8_t c = p[off + i];
			uart_putc((c >= 0x20 && c < 0x7F) ? (char)c : '.');
		}
		kputs("|\n");
	}
}

void shell_run(struct scion_boot_info *bi)
{
	char line[128];
	char *argv[8];

	kputs("\nEntering scion-boot interactive console (type 'help' or 'boot')\n");
	for (;;) {
		kputs("scion-stage1> ");
		readline(line, sizeof(line));
		int argc = tokenize(line, argv, 8);
		if (argc == 0)
			continue;

		if (strcmp(argv[0], "help") == 0 || strcmp(argv[0], "?") == 0) {
			cmd_help();
		} else if (strcmp(argv[0], "info") == 0) {
			cmd_info(bi);
		} else if (strcmp(argv[0], "safety") == 0) {
			platform_print_safety_status();
		} else if (strcmp(argv[0], "regs") == 0) {
			cmd_regs();
		} else if (strcmp(argv[0], "fbtest") == 0) {
			display_test_pattern();
		} else if (strcmp(argv[0], "muic") == 0) {
			muic_init_and_probe();
		} else if (strcmp(argv[0], "i2cread") == 0 && argc >= 4) {
			uint64_t bus, chip, reg;
			uint8_t val = 0;
			if (parse_u64(argv[1], &bus) == 0 &&
			    parse_u64(argv[2], &chip) == 0 &&
			    parse_u64(argv[3], &reg) == 0) {
				if (hsi2c_read_reg(bus, (uint8_t)chip, (uint8_t)reg, &val) == 0)
					kprintf("i2c[0x%lx @ 0x%02x reg 0x%02x] = 0x%02x\n",
					        bus, (unsigned)chip, (unsigned)reg, val);
				else
					kputs("i2cread: NACK or timeout\n");
			}
		} else if (strcmp(argv[0], "loadx") == 0) {
			uint64_t addr = (uint64_t)(uintptr_t)__image_end + 0x100000ULL;
			uint64_t max_len = 4 * 1024 * 1024;
			if (argc >= 2)
				(void)parse_u64(argv[1], &addr);
			if (argc >= 3)
				(void)parse_u64(argv[2], &max_len);
			kprintf("Ready for XMODEM-CRC transfer into %p (max %lu bytes)...\n",
			        (void *)addr, max_len);
			long rcvd = xmodem_receive((void *)(uintptr_t)addr, (size_t)max_len);
			if (rcvd > 0) {
				kprintf("\nReceived %ld bytes at %p (crc32=0x%08x)\n",
				        rcvd, (void *)addr,
				        crc32((const void *)(uintptr_t)addr, (size_t)rcvd));
			} else {
				kprintf("\nXMODEM transfer failed (rc=%ld)\n", rcvd);
			}
		} else if (strcmp(argv[0], "usbdl") == 0 || strcmp(argv[0], "dnw") == 0) {
			platform_enter_usbdl("Operator 'usbdl' console command");
		} else if (strcmp(argv[0], "boot") == 0 || strcmp(argv[0], "continue") == 0) {
			kputs("Continuing boot sequence...\n");
			return;
		} else if (strcmp(argv[0], "reboot") == 0 || strcmp(argv[0], "reset") == 0) {
			platform_reboot();
		} else if (strcmp(argv[0], "poweroff") == 0 || strcmp(argv[0], "halt") == 0) {
			platform_poweroff();
		} else if (strcmp(argv[0], "md32") == 0 && argc >= 2) {
			uint64_t addr, count = 4;
			if (parse_u64(argv[1], &addr) == 0) {
				if (argc >= 3)
					(void)parse_u64(argv[2], &count);
				for (uint64_t i = 0; i < count; i++) {
					if ((i & 3) == 0)
						kprintf("0x%016lx:", addr + i * 4);
					kprintf(" %08x", mmio_r32(addr + i * 4));
					if ((i & 3) == 3 || i + 1 == count)
						uart_putc('\n');
				}
			}
		} else if (strcmp(argv[0], "md64") == 0 && argc >= 2) {
			uint64_t addr, count = 4;
			if (parse_u64(argv[1], &addr) == 0) {
				if (argc >= 3)
					(void)parse_u64(argv[2], &count);
				for (uint64_t i = 0; i < count; i++) {
					uint64_t v = *(volatile uint64_t *)(uintptr_t)(addr + i * 8);
					kprintf("0x%016lx: 0x%016lx\n", addr + i * 8, v);
				}
			}
		} else if (strcmp(argv[0], "hexdump") == 0 && argc >= 2) {
			uint64_t addr, len = 64;
			if (parse_u64(argv[1], &addr) == 0) {
				if (argc >= 3)
					(void)parse_u64(argv[2], &len);
				cmd_hexdump(addr, len);
			}
		} else if (strcmp(argv[0], "mw32") == 0 && argc >= 3) {
			uint64_t addr, val;
			if (parse_u64(argv[1], &addr) == 0 && parse_u64(argv[2], &val) == 0) {
				if (platform_addr_is_write_protected(addr, 4))
					kprintf("DENIED: 0x%016lx is in a write-protected hardware range\n", addr);
				else
					mmio_w32(addr, (uint32_t)val);
			}
		} else if (strcmp(argv[0], "mw64") == 0 && argc >= 3) {
			uint64_t addr, val;
			if (parse_u64(argv[1], &addr) == 0 && parse_u64(argv[2], &val) == 0) {
				if (platform_addr_is_write_protected(addr, 8))
					kprintf("DENIED: 0x%016lx is in a write-protected hardware range\n", addr);
				else
					*(volatile uint64_t *)(uintptr_t)addr = val;
			}
		} else if (strcmp(argv[0], "memtest") == 0) {
			uint64_t addr = (uint64_t)(uintptr_t)__image_end;
			uint64_t size = 1024 * 1024;
			if (argc >= 2)
				(void)parse_u64(argv[1], &addr);
			if (argc >= 3)
				(void)parse_u64(argv[2], &size);
			if (platform_addr_is_write_protected(addr, size)) {
				kprintf("DENIED: [%p .. %p] overlaps write-protected hardware\n",
				        (void *)addr, (void *)(addr + size));
			} else {
				uint64_t t0 = timer_get_ticks();
				int rc = dram_walk_test(addr, size);
				uint64_t us = timer_ticks_to_us(timer_get_ticks() - t0);
				kprintf("memtest [%p .. %p]: %s (%lu us)\n",
				        (void *)addr, (void *)(addr + size),
				        (rc == 0) ? "PASS" : "FAIL", us);
			}
		} else if (strcmp(argv[0], "crc32") == 0 && argc >= 3) {
			uint64_t addr, len;
			if (parse_u64(argv[1], &addr) == 0 && parse_u64(argv[2], &len) == 0) {
				uint32_t c = crc32((const void *)(uintptr_t)addr, (size_t)len);
				kprintf("crc32: 0x%08x\n", c);
			}
		} else if (strcmp(argv[0], "sha256") == 0 && argc >= 3) {
			uint64_t addr, len;
			if (parse_u64(argv[1], &addr) == 0 && parse_u64(argv[2], &len) == 0) {
				uint8_t digest[32];
				sha256((const void *)(uintptr_t)addr, (size_t)len, digest);
				kputs("sha256: ");
				for (unsigned i = 0; i < 32; i++)
					kprintf("%02x", digest[i]);
				uart_putc('\n');
			}
		} else if (strcmp(argv[0], "cpuon") == 0 && argc >= 3) {
			uint64_t cpu, entry;
			if (parse_u64(argv[1], &cpu) == 0 && parse_u64(argv[2], &entry) == 0) {
				int rc = psci_cpu_on((uint32_t)cpu, entry);
				kprintf("cpuon(%lu, %p) -> %d\n", cpu, (void *)entry, rc);
			}
		} else {
			kprintf("Unknown command '%s' (type 'help')\n", argv[0]);
		}
	}
}
