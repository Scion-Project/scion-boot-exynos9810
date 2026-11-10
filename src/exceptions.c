/*
 * AArch64 exception vector setup and unhandled exception crash dump.
 */
#include <stdint.h>
#include "lib.h"

extern uint8_t __exception_vectors[];

struct exception_frame {
	uint64_t x[31];
	uint64_t sp;
	uint64_t elr;
	uint64_t esr;
	uint64_t far;
	uint64_t spsr;
};

void exceptions_init(void)
{
	uint64_t vbar = (uint64_t)(uintptr_t)__exception_vectors;
	uint32_t el = current_el();

	if (el == 3) {
		__asm__ volatile("msr vbar_el3, %0\n\tisb" :: "r"(vbar));
	} else if (el == 2) {
		__asm__ volatile("msr vbar_el2, %0\n\tisb" :: "r"(vbar));
	} else {
		__asm__ volatile("msr vbar_el1, %0\n\tisb" :: "r"(vbar));
	}
	kprintf("exceptions: VBAR_EL%u installed at %p\n", el, (void *)vbar);
}

static const char *vector_name(uint64_t vec_id)
{
	static const char * const names[16] = {
		"CurrEL_SP0_Sync",   "CurrEL_SP0_IRQ",   "CurrEL_SP0_FIQ",   "CurrEL_SP0_SError",
		"CurrEL_SPx_Sync",   "CurrEL_SPx_IRQ",   "CurrEL_SPx_FIQ",   "CurrEL_SPx_SError",
		"Lower64_Sync",      "Lower64_IRQ",      "Lower64_FIQ",      "Lower64_SError",
		"Lower32_Sync",      "Lower32_IRQ",      "Lower32_FIQ",      "Lower32_SError"
	};
	return (vec_id < 16) ? names[vec_id] : "Unknown";
}

static const char *esr_ec_name(uint32_t ec)
{
	switch (ec) {
	case 0x00: return "Unknown reason";
	case 0x01: return "Trapped WFI/WFE";
	case 0x07: return "SVE/SIMD/FP access trap";
	case 0x0E: return "Illegal Execution State";
	case 0x11: return "SVC (AArch32)";
	case 0x15: return "SVC (AArch64)";
	case 0x16: return "HVC (AArch64)";
	case 0x17: return "SMC (AArch64)";
	case 0x18: return "Trapped MSR/MRS/System inst";
	case 0x20: return "Instruction Abort (lower EL)";
	case 0x21: return "Instruction Abort (same EL)";
	case 0x22: return "PC alignment fault";
	case 0x24: return "Data Abort (lower EL)";
	case 0x25: return "Data Abort (same EL)";
	case 0x26: return "SP alignment fault";
	case 0x2C: return "FP exception (AArch64)";
	case 0x2F: return "SError interrupt";
	case 0x30: return "Breakpoint (lower EL)";
	case 0x31: return "Breakpoint (same EL)";
	case 0x3C: return "BRK instruction (AArch64)";
	default:   return "Other EC";
	}
}

void c_exception_handler(struct exception_frame *f, uint64_t vec_id)
{
	uint32_t el  = current_el();
	uint32_t ec  = (uint32_t)((f->esr >> 26) & 0x3Fu);
	uint32_t iss = (uint32_t)(f->esr & 0x01FFFFFFu);

	kprintf("\n============================================================\n");
	kprintf("FATAL EXCEPTION at EL%u: vector=%lu (%s)\n",
	        el, vec_id, vector_name(vec_id));
	kprintf("  ELR_EL%u  = 0x%016lx   SPSR_EL%u = 0x%016lx\n",
	        el, f->elr, el, f->spsr);
	kprintf("  ESR_EL%u  = 0x%016lx   (EC=0x%02x [%s], ISS=0x%07x)\n",
	        el, f->esr, ec, esr_ec_name(ec), iss);
	kprintf("  FAR_EL%u  = 0x%016lx   SP        = 0x%016lx\n",
	        el, f->far, f->sp);
	kprintf("------------------------------------------------------------\n");

	for (unsigned i = 0; i < 30; i += 2) {
		kprintf("  x%-2u = 0x%016lx   x%-2u = 0x%016lx\n",
		        i, f->x[i], i + 1, f->x[i + 1]);
	}
	kprintf("  x30 = 0x%016lx\n", f->x[30]);
	kprintf("============================================================\n");
	uart_flush();

	/* Fail-safe: fall back to USB Download Mode (usbdl) so the device is
	 * always recoverable over USB via Hubble / Houston after any crash. */
	platform_enter_usbdl("Fatal unhandled CPU exception");
}
