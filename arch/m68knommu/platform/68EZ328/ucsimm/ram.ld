MEMORY 
	{
	romvec : ORIGIN = 0x10c10000, LENGTH = 0x00000400
	flash  : ORIGIN = 0x10c10400, LENGTH = 0x00200000 - 0x00010400
	eflash : ORIGIN = 0x10e00000, LENGTH = 0
	ramvec : ORIGIN = 0x00000000, LENGTH = 0x00000400
	bvec   : ORIGIN = 0x00020000, LENGTH = 0x00000400
	ram    : ORIGIN = 0x00020400, LENGTH = 0x00800000 - 0x00020400
	eram   : ORIGIN = 0x00800000, LENGTH = 0
	}

jiffies = jiffies_64 + 4;

SECTIONS
{
	.fakevec :
	{
	} > romvec
	.rom :
	{
	__rom_start = . ;
	} > flash
	.eflash :
	{
	_flashend = . ;
	} > eflash
	.realvec :
	{
	_ramvec = . ;
	} > ramvec
	.romvec :
	{
	_romvec = . ;
	} > bvec
        .text :
	{
	__ram_start = . ;
	text_start = . ;
        	*(.text)
        	*(.exit.text)
		*(.text.*)
		*(.rodata)
	. = ALIGN(0x4) ;
		*(.kstrtab)
	. = ALIGN(16);          /* Exception table              */
	__start___ex_table = .;
		*(__ex_table)
	__stop___ex_table = .;

        __start___ksymtab = .;  /* Kernel symbol table          */
		 *(__ksymtab)
	__stop___ksymtab = .;

	_etext = . ;
	__data_rom_start = ALIGN ( 4 ) ;
        } > ram
        .data :
	{
	_sdata = . ;
	__data_start = . ;

        . = ALIGN(0x2000) ;
        *(.data.init_task)
        . = ALIGN(0x2000) ;

        *(.data)
        *(.data.*)      
        *(.exitcall.exit)               

        . = ALIGN(4096) ;
        __init_begin = .;
        *(.init.text)
        *(.init.data)
        . = ALIGN(16);
        __setup_start = .;
        *(.init.setup)
        __setup_end = .;
        __initcall_start = .;
	*(.initcall1.init)
	*(.initcall2.init)
	*(.initcall3.init)
	*(.initcall4.init)
	*(.initcall5.init)
	*(.initcall6.init)
	*(.initcall7.init)
        __initcall_end = .;
        . = ALIGN(4) ;
        __init_end = .;

	_edata = . ;
	edata = ALIGN( 0x10 ) ;
        } > ram
        .bss :
        {
	_sbss = ALIGN( 0x10 ) ;
	__bss_start = ALIGN( 0x10 ) ;
	__data_end = ALIGN( 0x10 ) ;
	*(.bss)
	*(COMMON)
	_ebss = . ;
	__bss_end = . ;
	end = ALIGN( 0x10 ) ;
	_end = ALIGN( 0x10 ) ;
	} > ram
	.eram :
	{
	__ramend = . ;
	_ramend = . ;
	} > eram
}
