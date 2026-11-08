/* Bare-metal layout for the first stage.
 * LOAD_ADDR is where the primary bootloader (or QEMU -kernel) drops us.
 * QEMU virt loads an ELF at its entry; 0x40080000 sits in virt RAM.
 * TODO(starlte): set LOAD_ADDR to the SRAM/DRAM window your stage-1 is
 * placed at on hardware. */
ENTRY(_start)

LOAD_ADDR = DEFINED(LOAD_ADDR) ? LOAD_ADDR : 0x40080000;
STACK_SIZE = 0x4000;

SECTIONS
{
	. = LOAD_ADDR;
	.text : { KEEP(*(.text.boot)) *(.text*) }
	.rodata : { *(.rodata*) }
	.data : { *(.data*) }
	. = ALIGN(16);
	__bss_start = .;
	.bss : { *(.bss*) *(COMMON) }
	__bss_end = .;
	. = ALIGN(16);
	. = . + STACK_SIZE;
	__stack_top = .;
	/DISCARD/ : { *(.comment) *(.note*) }
}
