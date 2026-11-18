/* SPDX-License-Identifier: MIT
 * Second-stage loader entry (docs/STATUS.md): an arm64 Image that S-Boot starts at EL1 with the
 * MMU off and x0 = its FDT, from wherever it put the BOOT partition's kernel.
 *
 *   base + 0x00  cold entry (S-Boot, QEMU -kernel)
 *   base + 0x40  re-entry from a running OS, x0 = struct scion_reentry (include/boot.h)
 *
 * Position independent: only PC-relative addressing (adr/adrp), no absolute relocations
 * (the Makefile checks). Everything here runs on one CPU.
 */
	.section .text.head, "ax"
	.global _start
_start:
	b	cold_entry               /* code0 */
	.long	0                        /* code1 */
	.quad	0x80000                  /* text_offset */
	.quad	0                        /* image_size: set by tools/mkloaderimg.py (the whole file) */
	.quad	0xa                      /* flags: little endian, 4 KiB pages, anywhere in RAM */
	.quad	0, 0, 0
	.ascii	"ARMd"                   /* arm64 Image magic */
	.long	0
	/* base + 0x40: re-entry, then its mark: "SCNR" and the record version (include/boot.h) */
	b	reentry_entry
	.ascii	"SCNR"
	.long	1

cold_entry:
	mov	x19, x0                  /* S-Boot's FDT (0 under QEMU without one) */
	mov	x20, #0                  /* cold */
	b	common

reentry_entry:
	mov	x19, x0                  /* struct scion_reentry */
	mov	x20, #1
common:
	msr	daifset, #0xf
	/* MMU and data cache off whatever the caller left (the contract says off already). */
	mrs	x0, sctlr_el1
	bic	x0, x0, #1
	bic	x0, x0, #(1 << 2)
	msr	sctlr_el1, x0
	isb
	mrs	x21, CurrentEL
	lsr	x21, x21, #2
	adrp	x0, __stack_top
	add	x0, x0, :lo12:__stack_top
	mov	sp, x0
	adrp	x0, __bss_start
	add	x0, x0, :lo12:__bss_start
	adrp	x1, __bss_end
	add	x1, x1, :lo12:__bss_end
1:	cmp	x0, x1
	b.hs	2f
	str	xzr, [x0], #8
	b	1b
2:	adrp	x0, ldr_vectors
	add	x0, x0, :lo12:ldr_vectors
	msr	vbar_el1, x0
	isb
	mov	x0, x19
	mov	x1, x20
	mov	x2, x21
	bl	loader_main
3:	wfe
	b	3b

/*
 * ldr_dcache_all(op): the whole data cache by set/way up to the level of coherency.
 * op 0 = invalidate, 1 = clean + invalidate. Leaf: x0-x12.
 */
	.text
	.global ldr_dcache_all
ldr_dcache_all:
	mov	x12, x0
	dmb	sy
	mrs	x0, clidr_el1
	and	w3, w0, #0x07000000
	lsr	w3, w3, #23              /* LoC * 2 */
	cbz	w3, 5f
	mov	w10, #0                  /* cache level * 2 */
1:	add	w2, w10, w10, lsr #1     /* level * 3 */
	lsr	w1, w0, w2
	and	w1, w1, #7               /* cache type at this level */
	cmp	w1, #2
	b.lt	4f                       /* no data cache here */
	msr	csselr_el1, x10
	isb
	mrs	x1, ccsidr_el1
	and	w2, w1, #7
	add	w2, w2, #4               /* log2(line size) */
	ubfx	w4, w1, #3, #10          /* ways - 1 */
	clz	w5, w4                   /* way shift */
	ubfx	w7, w1, #13, #15         /* sets - 1 */
2:	mov	w9, w4
3:	lsl	w6, w9, w5
	orr	w11, w10, w6
	lsl	w6, w7, w2
	orr	w11, w11, w6
	cbz	x12, 6f
	dc	cisw, x11
	b	7f
6:	dc	isw, x11
7:	subs	w9, w9, #1
	b.ge	3b
	subs	w7, w7, #1
	b.ge	2b
4:	add	w10, w10, #2
	cmp	w3, w10
	b.gt	1b
5:	mov	x10, #0
	msr	csselr_el1, x10
	dsb	sy
	isb
	ret

/*
 * ldr_jump(entry, x0): leave the loader for an OS. Data cache cleaned and invalidated, MMU,
 * data and instruction caches off, TLB and I-cache invalidated, x1..x3 = 0, DAIF masked.
 */
	.global ldr_jump
ldr_jump:
	msr	daifset, #0xf
	mov	x16, x0
	mov	x17, x1
	mov	x0, #1
	bl	ldr_dcache_all
	mrs	x0, sctlr_el1
	bic	x0, x0, #1               /* M */
	bic	x0, x0, #(1 << 2)        /* C */
	bic	x0, x0, #(1 << 12)       /* I */
	msr	sctlr_el1, x0
	isb
	ic	iallu
	tlbi	vmalle1
	dsb	sy
	isb
	mov	x0, x17
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	br	x16

/* ---- EL1 exception vectors: every entry saves the frame and calls ldr_exception ---- */
	.macro	VEC kind
	.balign	0x80
	sub	sp, sp, #(36 * 8)
	stp	x0, x1, [sp, #0]
	mov	x0, #\kind
	b	vec_common
	.endm

	.section .text.vectors, "ax"
	.balign	0x800
	.global ldr_vectors
ldr_vectors:
	VEC 0
	VEC 1
	VEC 2
	VEC 3
	VEC 4
	VEC 5
	VEC 6
	VEC 7
	VEC 8
	VEC 9
	VEC 10
	VEC 11
	VEC 12
	VEC 13
	VEC 14
	VEC 15

vec_common:
	/* x0 = vector index; the caller's x0/x1 are at [sp]. Frame: x0..x30, sp, elr, esr, far, spsr. */
	stp	x2, x3, [sp, #16]
	stp	x4, x5, [sp, #32]
	stp	x6, x7, [sp, #48]
	stp	x8, x9, [sp, #64]
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
	add	x1, sp, #(36 * 8)
	stp	x30, x1, [sp, #240]
	mrs	x1, elr_el1
	mrs	x2, esr_el1
	stp	x1, x2, [sp, #256]
	mrs	x1, far_el1
	mrs	x2, spsr_el1
	stp	x1, x2, [sp, #272]
	mov	x1, sp
	bl	ldr_exception            /* returns only for an interrupt it handled */
	ldp	x2, x3, [sp, #16]
	ldp	x4, x5, [sp, #32]
	ldp	x6, x7, [sp, #48]
	ldp	x8, x9, [sp, #64]
	ldp	x10, x11, [sp, #80]
	ldp	x12, x13, [sp, #96]
	ldp	x14, x15, [sp, #112]
	ldp	x16, x17, [sp, #128]
	ldp	x18, x19, [sp, #144]
	ldp	x20, x21, [sp, #160]
	ldp	x22, x23, [sp, #176]
	ldp	x24, x25, [sp, #192]
	ldp	x26, x27, [sp, #208]
	ldp	x28, x29, [sp, #224]
	ldr	x30, [sp, #240]
	ldp	x0, x1, [sp, #0]
	add	sp, sp, #(36 * 8)
	eret
