OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)

ENTRY(_start)

SECTIONS
{
	. = 0x00000000;
	.text : {
		*(.text)
		*(.text*)
	}
}
