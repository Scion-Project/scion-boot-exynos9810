/* SPDX-License-Identifier: MIT
 * Test payloads for tools/qemu-test.py: a stand-in for XNU (fakexnu) and for a chained arm64
 * Image (chain). Both print what they were given on QEMU virt's PL011 and power off over PSCI.
 */
	.section .text.head, "ax"
	.global _start
_start:
#ifdef CHAIN_IMAGE
	b	1f
	.long	0
	.quad	0, 0, 0xa, 0, 0, 0
	.ascii	"ARMd"
	.long	0
1:
#endif
	mov	x19, x0
	adrp	x1, fake_stack_top
	add	x1, x1, :lo12:fake_stack_top
	mov	sp, x1
	mov	x0, x19
	bl	fake_main
	movz	x0, #0x0008
	movk	x0, #0x8400, lsl #16       /* PSCI SYSTEM_OFF */
	hvc	#0
2:	wfi
	b	2b

	.bss
	.balign	16
	.space	4096
fake_stack_top:
	.space	16
