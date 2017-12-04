OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)

ENTRY(_start)

SECTIONS
{
	. = 0x1F000000;
	.text : {
		*(.text)
		*(.text*)
	}
	.data : {
		*(.data*)
	}
}
