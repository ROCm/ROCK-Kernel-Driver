/* AE-3068 (aka. aki3068net) RAM */

OUTPUT_ARCH(h8300h)
ENTRY("__start")

MEMORY 
	{
	ram    : ORIGIN = 0x400000, LENGTH = 0x600000-0x400000-0x80000
	disk   : ORIGIN = 0x600000-0x80000, LENGTH = 0x60000
	eram   : ORIGIN = 0x600000, LENGTH = 0
        iram   : ORIGIN = 0xffbf20, LENGTH = 0x4000
	}

SECTIONS
{
	.bootvec :
	{
	*(.bootvec)
	} > ram
        .text :
	{
	__stext = . ;
        	*(.text)
	. = ALIGN(0x4) ;
		*(.text.*)
	. = ALIGN(0x4) ;
		*(.kstrtab)
	. = ALIGN(0x4) ;
		*(.rodata*)
	. = ALIGN(16);          /* Exception table              */
	___start___ex_table = .;
		*(__ex_table)
	___stop___ex_table = .;

        ___start___ksymtab = .;  /* Kernel symbol table          */
		 *(__ksymtab)
	___stop___ksymtab = .;

	. = ALIGN(0x4) ;
	__etext = . ;
        } > ram
	.data : 
	{
	__sdata = . ;
	___data_start = . ;
		*(.data)
		*(.data.*)	
		*(.exitcall.exit)

	. = ALIGN(0x2000) ;
		*(.data.init_task)
	. = ALIGN(0x2000) ;
        ___init_begin = .;
        *(.text.init)
        *(.data.init)
        . = ALIGN(16);
        ___setup_start = .;
        *(.setup.init)
        ___setup_end = .;
        ___initcall_start = .;
        *(.initcall.init)
        . = ALIGN(4) ;
        ___initcall_end = .;
        ___init_end = .;
	__edata = . ;
	. = ALIGN(0x4) ;
	__sbss = . ;
		*(.bss)
	. = ALIGN(0x4) ;
		*(COMMON)
	. = ALIGN(0x4) ;
	__ebss = . ;
	__end = . ;
	__ramstart = .;
	} > ram
	.blkimg :
	{
	__ramend = . ;
	__blkimg = . ;
	} > disk
	.ram_vec : AT(ADDR(.data) + SIZEOF(.data))
	{
		*(.int_redirect)
	} > iram 
	__vector_lma = LOADADDR(.ram_vec);
        .dummy2 :
        {
        COMMAND_START = . - 0x200 ;
	} > eram
}
