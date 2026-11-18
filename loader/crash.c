/* SPDX-License-Identifier: MIT
 * The loader's exceptions: interrupts go to the board (its watchdog), everything else is a crash
 * shown on the screen and the UART, never a silent hang. After a crash or a watchdog bark the
 * guard counts one more strike; a crash then waits for a key (or 60 s) and reboots to recovery.
 */
#include <stdint.h>
#include "loader.h"
#include "fbcon.h"
#include "plat.h"
#include "lib.h"

struct frame {
	uint64_t x[31];
	uint64_t sp, elr, esr, far, spsr;
};

static const char vec_names[4][8] = { "Sync", "IRQ", "FIQ", "SError" };

static const char *ec_name(uint32_t ec)
{
	switch (ec) {
	case 0x00: return "unknown instruction";
	case 0x0e: return "illegal execution state";
	case 0x15: return "SVC";
	case 0x18: return "system register trap";
	case 0x20: return "instruction abort (lower EL)";
	case 0x21: return "instruction abort";
	case 0x22: return "PC alignment";
	case 0x24: return "data abort (lower EL)";
	case 0x25: return "data abort";
	case 0x26: return "SP alignment";
	case 0x2f: return "SError";
	case 0x3c: return "BRK";
	}
	return "other";
}

static void line_hex(char *b, const char *label, uint64_t v)
{
	static const char hx[16] = { '0', '1', '2', '3', '4', '5', '6', '7',
	                             '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
	unsigned n = 0;
	while (*label)
		b[n++] = *label++;
	for (int s = 60; s >= 0; s -= 4)
		b[n++] = hx[(v >> s) & 15];
	b[n] = 0;
}

static void red_screen(const char *title)
{
	fb_clear(FB_DARK);
	fb_fill_rows(0, 2, FB_RED);
	fb_text(1, 0, title, FB_WHITE, FB_RED);
}

void ldr_watchdog_bark(void) __attribute__((noreturn));
void ldr_watchdog_bark(void)
{
	struct ldr *L = ldr_global;
	guard_strike(&L->guard);
	ldr_guard_store(L);
	red_screen("LOADER WATCHDOG");
	fb_text(1, 3, "The loader stopped making progress.", FB_WHITE, FB_DARK);
	fb_text(1, 4, "Resetting; the boot-loop guard counted it.", FB_WHITE, FB_DARK);
	kprintf("\nWATCHDOG: no progress for %u ms; guard strikes now %u; resetting\n",
	        LDR_WDT_MS, L->guard.strikes);
	plat_reboot(PR_NORMAL);
}

void ldr_exception(uint64_t kind, struct frame *f)
{
	if ((kind & 3) == 1 && plat_irq())
		return;
	plat_wdt_stop();
	uint32_t ec = (uint32_t)(f->esr >> 26) & 0x3f;
	char b[64];
	red_screen("LOADER CRASH");
	kprintf("\n==== LOADER CRASH: %s exception (vector %lu), %s ====\n", vec_names[kind & 3], kind,
	        ec_name(ec));
	kprintf("ESR %016lx  ELR %016lx  FAR %016lx\nSPSR %016lx  SP %016lx\n", f->esr, f->elr, f->far,
	        f->spsr, f->sp);
	for (unsigned i = 0; i < 31; i += 2) {
		if (i + 1 < 31)
			kprintf("x%u %016lx  x%u %016lx\n", i, f->x[i], i + 1, f->x[i + 1]);
		else
			kprintf("x%u %016lx\n", i, f->x[i]);
	}
	fb_text(1, 3, vec_names[kind & 3], FB_WHITE, FB_DARK);
	fb_text(10, 3, ec_name(ec), FB_WHITE, FB_DARK);
	line_hex(b, "ESR  ", f->esr);
	fb_text(1, 5, b, FB_WHITE, FB_DARK);
	line_hex(b, "ELR  ", f->elr);
	fb_text(1, 6, b, FB_WHITE, FB_DARK);
	line_hex(b, "FAR  ", f->far);
	fb_text(1, 7, b, FB_WHITE, FB_DARK);
	line_hex(b, "SPSR ", f->spsr);
	fb_text(1, 8, b, FB_WHITE, FB_DARK);
	line_hex(b, "LR   ", f->x[30]);
	fb_text(1, 9, b, FB_WHITE, FB_DARK);
	for (unsigned i = 0; i < 8; i++) {
		char lab[8] = { 'x', (char)('0' + i), ' ', ' ', ' ', ' ', 0, 0 };
		line_hex(b, lab, f->x[i]);
		fb_text(1, 11 + i, b, FB_GREY, FB_DARK);
	}
	struct ldr *L = ldr_global;
	if (L) {
		guard_strike(&L->guard);
		ldr_guard_store(L);
	}
	fb_text(1, 21, "Any key, or 60 s: reboot to recovery (TWRP).", FB_WHITE, FB_DARK);
	con_line("any key, or 60 s: reboot to recovery");
	uint64_t end = now_ms() + 60000;
	while (now_ms() < end && key_poll() == MK_NONE)
		;
	plat_reboot(PR_RECOVERY);
}
