/* Linker script for the FPGA implementation of the V850E2 NA85E2C cpu core
   (CONFIG_V850E2_FPGA85E2C).  */

/* Note, all symbols are prefixed with an extra `_' for compatibility with
   the existing linux sources.  */

_jiffies = _jiffies_64 ;

MEMORY {
       /* Reset vector.  */
       RESET	 : ORIGIN = 0, LENGTH = 0x10
       /* Interrupt vectors.  */
       INTV      : ORIGIN = 0x10, LENGTH = 0x470
       /* The `window' in RAM were we're allowed to load stuff.  */
       RAM_LOW   : ORIGIN = 0x480, LENGTH = 0x0005FB80
       /* Some more ram above the window were we can put bss &c.  */
       RAM_HIGH  : ORIGIN = 0x00060000, LENGTH = 0x000A0000
       /* This is the area visible from the outside world (we can use
	  this only for uninitialized data).  */
       VISIBLE   : ORIGIN = 0x00200000, LENGTH = 0x00060000
}

SECTIONS {
	.reset : {
	        __kram_start = . ;
		__intv_start = . ;
	        	*(.intv.reset)	/* Reset vector */
	} > RESET

	.r0_ram : {
		__r0_ram = . ;
		. = . + 32 ;
	} > RAM_LOW

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
	} > RAM_LOW

	.data : {
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
	} > RAM_LOW

	/* Device contents for the root filesystem.  */
	.root : {
		. =  ALIGN (4096) ;
		__root_fs_image_start = . ;
		*(.root)
		__root_fs_image_end = . ;
	} > RAM_LOW

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
	} > RAM_LOW

        /* Where the interrupt vectors are initially loaded.  */
	__intv_load_start = . ;

	.intv : {
			*(.intv.common)	/* Vectors common to all v850e proc. */
			*(.intv.mach)	/* Machine-specific int. vectors.  */
		__intv_end = . ;
	} > INTV  AT> RAM_LOW

	.bss : {
		/* This is here so that when we free init memory the
		   load-time copy of the interrupt vectors and any empty
		   space at the end of the `RAM_LOW' area is freed too.  */
		. = ALIGN (4096);
		__init_end = . ;

		__sbss = . ;
			*(.bss)
			*(COMMON)
		. = ALIGN (4) ;
		__init_stack_end = . ;
		__ebss = . ;

	        __kram_end = . ;
	} > RAM_HIGH

	.bootmap ALIGN (4096) : {
		__bootmap = . ;
		. = . + 4096 ;		/* enough for 128MB.   */
	} > RAM_HIGH

	.visible : {
		_memcons_output = . ;
		. = . + 0x8000 ;
		_memcons_output_end = . ;
	} > VISIBLE
}
