/* Linker script for the gdb v850e simulator (CONFIG_V850E_SIM).  */

/* Note, all symbols are prefixed with an extra `_' for compatibility with
   the existing linux sources.  */

_jiffies = _jiffies_64 ;

MEMORY {
       /* Interrupt vectors.  */
       INTV  : ORIGIN = 0x0, LENGTH = 0xe0
       /* 16MB of RAM.
          This must match RAM_ADDR and RAM_SIZE in include/asm-v580/sim.h  */
       RAM   : ORIGIN = 0x8F000000, LENGTH = 0x01000000
}

SECTIONS {
	.intv : {
		__intv_start = . ;
			*(.intv.reset)	/* Reset vector */
			*(.intv.common)	/* Vectors common to all v850e proc. */
			*(.intv.mach)	/* Machine-specific int. vectors.  */
		__intv_end = . ;
	} > INTV

	.text : {
		__kram_start = . ;

		__stext = . ;
        	*(.text)
			*(.exit.text)	/* 2.5 convention */
			*(.text.exit)	/* 2.4 convention */
			*(.text.lock)
			*(.exitcall.exit)
		__real_etext = . ;	/* There may be data after here.  */
			*(.rodata)
		. = ALIGN (0x4) ;
			*(.kstrtab)

		. = ALIGN (4) ;
		    	*(.call_table_data)
			*(.call_table_text)

		. = ALIGN (16) ;	/* Exception table.  */
		___start___ex_table = . ;
			*(__ex_table)
		___stop___ex_table = . ;

		___start___ksymtab = . ;/* Kernel symbol table.  */
			*(__ksymtab)
		___stop___ksymtab = . ;
		. = ALIGN (4) ;
		__etext = . ;
	} > RAM

	.data ALIGN (0x4) : {
		__sdata = . ;
        	*(.data)
			*(.exit.data)	/* 2.5 convention */
			*(.data.exit)	/* 2.4 convention */
		. = ALIGN (16) ;
		*(.data.cacheline_aligned)
		. = ALIGN (0x2000) ;
        	*(.data.init_task)
		. = ALIGN (0x2000) ;
		__edata = . ;
	} > RAM

	.bss ALIGN (0x4) : {
		__sbss = . ;
			*(.bss)
			*(COMMON)
		. = ALIGN (4) ;
		__init_stack_end = . ;
		__ebss = . ;
	} > RAM

	.init ALIGN (4096) : {
		__init_start = . ;
			*(.init.text)	/* 2.5 convention */
			*(.init.data)
			*(.text.init)	/* 2.4 convention */
			*(.data.init)
		. = ALIGN (16) ;
		___setup_start = . ;
			*(.init.setup)	/* 2.5 convention */
			*(.setup.init)	/* 2.4 convention */
		___setup_end = . ;
		___initcall_start = . ;
			*(.initcall.init)
			*(.initcall1.init)
			*(.initcall2.init)
			*(.initcall3.init)
			*(.initcall4.init)
			*(.initcall5.init)
			*(.initcall6.init)
			*(.initcall7.init)
		. = ALIGN (4) ;
		___initcall_end = . ;
		__init_end = . ;

		__kram_end = . ;
	} > RAM

	.bootmap ALIGN (4096) : {
		__bootmap = . ;
		. = . + 4096 ;		/* enough for 128MB.   */
	} > RAM
}
