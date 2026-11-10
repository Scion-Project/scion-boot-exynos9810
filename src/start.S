/*
 * EL3/EL2/EL1 reset entry, exception vector table, and EL3 -> EL2 transition.
 */
	.section .text.boot, "ax"
	.global _start
_start:
	/* Mask all interrupts (Debug, SError, IRQ, FIQ) */
	msr	DAIFSet, #0xF

	/* Check MPIDR_EL1 affinity (Aff2:Aff1:Aff0); primary is 0 */
	mrs	x0, mpidr_el1
	mov	x1, #0xFFFF
	movk	x1, #0x00FF, lsl #16
	and	x0, x0, x1
	cbnz	x0, park_secondary

	/* Set up the primary C stack */
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
	/* Record the exception level we entered at */
	mrs	x0, CurrentEL
	lsr	x0, x0, #2
	bl	stage1_set_entry_el

	/* Jump into stage-1 C main */
	bl	stage1_main

hang:
	wfe
	b	hang

park_secondary:
	/* Atomically record that a secondary core arrived at the spin-table */
	ldr	x3, =__secondary_online_count
0:	ldaxr	w4, [x3]
	add	w4, w4, #1
	stlxr	w5, w4, [x3]
	cbnz	w5, 0b
	sev

	/* Spin until __cpu_release_addr is non-zero, then branch to it */
	ldr	x1, =__cpu_release_addr
3:	ldar	x2, [x1]
	cbz	x2, 4f
	mov	x1, #0
	mov	x2, #0
	mov	x3, #0
	ldr	x4, =__cpu_release_addr
	ldr	x4, [x4]
	br	x4
4:	wfe
	b	3b

/* -------------------------------------------------------------------------
 * Exception Vector Table (2048-byte aligned per ARMv8 architecture)
 * ------------------------------------------------------------------------- */
.macro VECTOR_ENTRY id
	.align 7
	sub	sp, sp, #288
	stp	x0, x1, [sp, #0]
	mov	x0, #\id
	b	exception_common
.endm

	.section .text.vectors, "ax"
	.align 11
	.global __exception_vectors
__exception_vectors:
	/* Current EL with SP0 */
	VECTOR_ENTRY 0   /* Sync */
	VECTOR_ENTRY 1   /* IRQ */
	VECTOR_ENTRY 2   /* FIQ */
	VECTOR_ENTRY 3   /* SError */
	/* Current EL with SPx */
	VECTOR_ENTRY 4   /* Sync */
	VECTOR_ENTRY 5   /* IRQ */
	VECTOR_ENTRY 6   /* FIQ */
	VECTOR_ENTRY 7   /* SError */
	/* Lower EL using AArch64 */
	VECTOR_ENTRY 8   /* Sync */
	VECTOR_ENTRY 9   /* IRQ */
	VECTOR_ENTRY 10  /* FIQ */
	VECTOR_ENTRY 11  /* SError */
	/* Lower EL using AArch32 */
	VECTOR_ENTRY 12  /* Sync */
	VECTOR_ENTRY 13  /* IRQ */
	VECTOR_ENTRY 14  /* FIQ */
	VECTOR_ENTRY 15  /* SError */

	.section .text, "ax"
exception_common:
	/* x0 holds vector_id; original x0, x1 are already saved at [sp, #0] */
	stp	x2,  x3,  [sp, #16]
	stp	x4,  x5,  [sp, #32]
	stp	x6,  x7,  [sp, #48]
	stp	x8,  x9,  [sp, #64]
	stp	x10, x11, [sp, #80]
	stp	x12, x13, [sp, #96]
	stp	x14, x15, [sp, #112]
	stp	x16, x17, [sp, #128]
	stp	x18, x19, [sp, #144]
	stp	x20, x21, [sp, #160]
	stp	x22, x23, [sp, #176]
	stp	x24, x25, [sp, #192]
	stp	x26, x27, [sp, #208]
	stp	x28, x29, [sp, #224]
	add	x1, sp, #288
	stp	x30, x1,  [sp, #240]  /* x30 (LR) and pre-exception SP */

	/* Read ELR, ESR, FAR, SPSR for the active Exception Level */
	mrs	x1, CurrentEL
	lsr	x1, x1, #2
	cmp	x1, #3
	b.eq	1f
	cmp	x1, #2
	b.eq	2f

	/* EL1 */
	mrs	x2, elr_el1
	mrs	x3, esr_el1
	mrs	x4, far_el1
	mrs	x5, spsr_el1
	b	3f
1:	/* EL3 */
	mrs	x2, elr_el3
	mrs	x3, esr_el3
	mrs	x4, far_el3
	mrs	x5, spsr_el3
	b	3f
2:	/* EL2 */
	mrs	x2, elr_el2
	mrs	x3, esr_el2
	mrs	x4, far_el2
	mrs	x5, spsr_el2
3:
	stp	x2, x3, [sp, #256]    /* elr, esr */
	stp	x4, x5, [sp, #272]    /* far, spsr */

	mov	x1, x0                /* arg1 = vector_id */
	mov	x0, sp                /* arg0 = struct exception_frame * */
	bl	c_exception_handler
	b	hang

/* -------------------------------------------------------------------------
 * Architectural Exception Level query & EL3 -> EL2 transition
 * ------------------------------------------------------------------------- */
	.global current_el
current_el:
	mrs	x0, CurrentEL
	lsr	x0, x0, #2
	ret

/*
 * void el_drop_and_jump(uint64_t entry_pa, uint64_t arg0)
 *   x0 = entry_pa
 *   x1 = arg0 (pointer to struct scion_boot_info)
 *
 * Ensures I-cache is coherent, drops from EL3 to EL2h if running at EL3, and
 * enters stage 2 with x0 = arg0, x1..x3 = 0, MMU off, DAIF = 0xF.
 */
	.global el_drop_and_jump
el_drop_and_jump:
	mov	x19, x0               /* target entry_pa */
	mov	x20, x1               /* target x0 (boot_info) */

	/* Ensure all stores to stage-2 payload/boot_info are visible and flush I-cache */
	dsb	sy
	ic	iallu
	dsb	sy
	isb

	mrs	x2, CurrentEL
	lsr	x2, x2, #2
	cmp	x2, #3
	b.ne	jump_direct

	/* Configure EL3 -> non-secure EL2h transition:
	 * SCR_EL3: NS (bit 0) | RES1 (bits 4,5) | HCE (bit 8) | RW (bit 10) = 0x531 */
	mov	x3, #0x531
	msr	scr_el3, x3
	msr	cptr_el3, xzr         /* Do not trap FP/SIMD or CP accesses to EL3 */

	/* Prepare clean EL2 state (MMU off, caches off, RW=1 for lower EL) */
	msr	sctlr_el2, xzr
	mov	x3, #(1 << 31)        /* HCR_EL2.RW = 1 (AArch64) */
	msr	hcr_el2, x3

	/* Propagate stack pointer and vector table to EL2 */
	mov	x3, sp
	msr	sp_el2, x3
	ldr	x3, =__exception_vectors
	msr	vbar_el2, x3

	/* SPSR_EL3: D=1, A=1, I=1, F=1 (0x3C0) | M[3:0]=0x9 (EL2h) = 0x3C9 */
	mov	x3, #0x3C9
	msr	spsr_el3, x3
	msr	elr_el3, x19

	mov	x0, x20
	mov	x1, #0
	mov	x2, #0
	mov	x3, #0
	isb
	eret

jump_direct:
	mov	x0, x20
	mov	x1, #0
	mov	x2, #0
	mov	x3, #0
	br	x19

	.section .data
	.global __cpu_release_addr
	.align 3
__cpu_release_addr:
	.quad 0

	.global __secondary_online_count
	.align 2
__secondary_online_count:
	.word 0
