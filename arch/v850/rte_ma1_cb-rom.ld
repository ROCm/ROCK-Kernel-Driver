/* Linker script for the Midas labs RTE-V850E/MA1-CB evaluation board
   (CONFIG_RTE_CB_MA1), with kernel in ROM.  */

/* Note, all symbols are prefixed with an extra `_' for compatibility with
   the existing linux sources.  */

_jiffies = _jiffies_64 ;

MEMORY {
       ROM   : ORIGIN = 0x00000000, LENGTH = 0x00100000
       /* 1MB of SRAM.  This memory is mirrored 4 times.  */
       SRAM  : ORIGIN = 0x00400000, LENGTH = 0x00100000
       /* 32MB of SDRAM.  */
       SDRAM : ORIGIN = 0x00800000, LENGTH = 0x02000000
}

SECTIONS {
	/* Interrupt vector space.  */
	.intv {
		__intv_start = . ;
	        	*(.intv.reset)	/* Reset vector */
			*(.intv.common) /* Vectors common to all v850e proc. */
			*(.intv.mach)   /* Machine-specific int. vectors.  */
		__intv_end = . ;
	} > ROM

	.text : {
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
		. = ALIGN (16) ;	/* Exception table.  */
		___start___ex_table = . ;
			*(__ex_table)
		___stop___ex_table = . ;

		___start___ksymtab = . ;/* Kernel symbol table.  */
			*(__ksymtab)
		___stop___ksymtab = . ;
		. = ALIGN (4) ;
		__etext = . ;
	} > ROM

	__data_load_start = . ;

	.data : {
	        __kram_start = . ;

		__sdata = . ;
		___data_start = . ;
        	*(.data)
			*(.exit.data)	/* 2.5 convention */
			*(.data.exit)	/* 2.4 convention */
		. = ALIGN (16) ;
		*(.data.cacheline_aligned)
		. = ALIGN (0x2000) ;
        	*(.data.init_task)
		. = ALIGN (0x2000) ;
		__edata = . ;
	} > SRAM  AT> ROM

	.bss ALIGN (0x4) : {
		__sbss = . ;
			*(.bss)
			*(COMMON)
		. = ALIGN (4) ;
		__init_stack_end = . ;
		__ebss = . ;
	} > SRAM

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
	} > SRAM

	.bootmap ALIGN (4096) : {
		__bootmap = . ;
		. = . + 4096 ;		/* enough for 128MB.   */
	} > SRAM

	/* device contents for the root filesystem.  */
	.root ALIGN (4096) {
		__root_fs_image_start = . ;
		*(.root)
		__root_fs_image_end = . ;
	} > SDRAM
}
