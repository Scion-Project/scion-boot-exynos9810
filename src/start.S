/*
 * EL3 reset entry for the first stage.
 *
 * Assumes it is entered with the MMU off. It does only what C cannot do for
 * itself: pick a stack, zero BSS, then branch to stage1_main(). Everything
 * else (console, clocks, DRAM) is C.
 *
 * This file is platform-agnostic; the stack top comes from the linker script.
 */
	.section .text.boot, "ax"
	.global _start
_start:
	/* Mask all interrupts for the duration of the loader. */
	msr	DAIFSet, #0xF

	/* If we somehow arrived on a secondary core, park it. Primary = aff0==0
	 * on the first cluster. A real bring-up releases secondaries later via
	 * PSCI; here we just make sure only one core runs C. */
	mrs	x0, mpidr_el1
	and	x0, x0, #0xFF        /* Aff0 */
	cbnz	x0, park_secondary

	/* Set up the C stack (top-of-stack symbol from stage1.ld). */
	ldr	x0, =__stack_top
	mov	sp, x0

	/* Zero .bss */
	ldr	x0, =__bss_start
	ldr	x1, =__bss_end
1:	cmp	x0, x1
	b.ge	2f
	str	xzr, [x0], #8
	b	1b
2:
	/* Save the exception level we booted at, for diagnostics. */
	mrs	x0, CurrentEL
	lsr	x0, x0, #2
	bl	stage1_set_entry_el

	bl	stage1_main

	/* stage1_main should never return. If it does, hang loudly. */
hang:
	wfe
	b	hang

park_secondary:
	/* Spin until the spin-table release address (set up by PSCI code) is
	 * non-zero, then jump to it. Until then, low-power wait. */
	ldr	x1, =__cpu_release_addr
3:	ldr	x2, [x1]
	cbz	x2, 4f
	br	x2
4:	wfe
	b	3b

	.section .data
	.global __cpu_release_addr
	.align 3
__cpu_release_addr:
	.quad 0
