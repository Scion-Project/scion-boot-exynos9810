/* SPDX-License-Identifier: MIT */
ENTRY(_start)
SECTIONS
{
	. = 0;
	.text : { KEEP(*(.text.head)) *(.text .text.*) }
	.rodata : ALIGN(16) { *(.rodata .rodata.*) }
	.data : ALIGN(16) { *(.data .data.*) }
	. = ALIGN(16);
	_file_end = .;
	.bss (NOLOAD) : ALIGN(16) { *(.bss .bss.*) *(COMMON) }
	_mem_end = .;
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
