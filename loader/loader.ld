/* SPDX-License-Identifier: MIT
 * The second-stage loader's layout. It runs wherever it is loaded (PC-relative code, no
 * absolute relocations); LOAD_ADDR only names an address for listings and debuggers.
 * _image_end is page aligned and covers .bss and the stack: tools/mkloaderimg.py pads the file to
 * it and appends the SCN2 records there.
 */
ENTRY(_start)
LOAD_ADDR = DEFINED(LOAD_ADDR) ? LOAD_ADDR : 0x40080000;

SECTIONS
{
	. = LOAD_ADDR;
	_start_of_image = .;
	.text : {
		KEEP(*(.text.head))
		. = ALIGN(0x800);
		KEEP(*(.text.vectors))
		*(.text .text.*)
	}
	.rodata : ALIGN(16) { *(.rodata .rodata.*) }
	.data : ALIGN(16) { *(.data .data.*) }
	. = ALIGN(4096);
	__bss_start = .;
	.bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) }
	. = ALIGN(16);
	__bss_end = .;
	. = . + 0x4000;
	__stack_top = .;
	. = ALIGN(4096);
	_image_end = .;
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
