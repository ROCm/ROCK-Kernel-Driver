
MEMORY {
	ram    : ORIGIN = 0x20000, LENGTH = 0x007E0000
}

jiffies = jiffies_64 + 4;

SECTIONS {

	.text : {
		_stext = . ;
        	*(.text)
		*(.exit.text)
		*(.text.lock)
		*(.exitcall.exit)
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
		. = ALIGN(4) ;
		_etext = . ;
	} > ram

	.data BLOCK(0x4) : {
		_sdata = . ;
		__data_start = . ;
        	*(.data)
		*(.data.exit)
		. = ALIGN(0x2000) ;
        	*(.data.init_task)
		. = ALIGN(0x2000) ;
		_edata = . ;
	} > ram

	.init BLOCK(4096) : {
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
	} > ram

	.bss BLOCK(0x4) : {
		_sbss = . ;
		*(.bss)
		*(COMMON)
		. = ALIGN(4) ;
		_ebss = . ;
		_end = . ;
	} > ram
}

