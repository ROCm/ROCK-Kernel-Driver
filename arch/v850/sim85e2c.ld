/* Linker script for the sim85e2c simulator, which is a verilog simulation of
   the V850E2 NA85E2C cpu core (CONFIG_V850E2_SIM85E2C).  */

/* Note, all symbols are prefixed with an extra `_' for compatibility with
   the existing linux sources.  */

_jiffies = _jiffies_64 ;

MEMORY {
       /* 1MB of `instruction RAM', starting at 0.
          Instruction fetches are much faster from IRAM than from DRAM.
	  This should match IRAM_ADDR in "include/asm-v580/sim85e2c.h".    */
       IRAM	: ORIGIN = 0x00000000, LENGTH = 0x00100000

       /* 1MB of `data RAM', below and contiguous with the I/O space.
          Data fetches are much faster from DRAM than from IRAM.
	  This should match DRAM_ADDR in "include/asm-v580/sim85e2c.h".  */
       DRAM	: ORIGIN = 0xfff00000, LENGTH = 0x000ff000
       /* We have to load DRAM at a mirror-address of 0x1ff00000,
          because the simulator's preprocessing script isn't smart
          enough to deal with the above LMA.  */
       DRAM_LOAD : ORIGIN = 0x1ff00000, LENGTH = 0x000ff000

       /* `external ram' (CS1 area), comes after IRAM.
          This should match ERAM_ADDR in "include/asm-v580/sim85e2c.h".  */
       ERAM	: ORIGIN = 0x00100000, LENGTH = 0x07f00000
}

SECTIONS {
	.intv : {
		__intv_start = . ;
		*(.intv)		/* Interrupt vectors.  */
	        	*(.intv.reset)	/* Reset vector */
			*(.intv.common) /* Vectors common to all v850e proc. */
			*(.intv.mach)   /* Machine-specific int. vectors.  */
		__intv_end = . ;
	} > IRAM

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
	} > IRAM

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

		. = ALIGN (4) ;
		___initramfs_start = . ;
			*(.init.ramfs)
		___initramfs_end = . ;

		__init_end = . ;
	} > IRAM

	.data : {
	        __kram_start = . ;

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
	} > DRAM  AT> DRAM_LOAD

	.bss ALIGN (0x4) : {
		__sbss = . ;
			*(.bss)
			*(COMMON)
		. = ALIGN (4) ;
		__init_stack_end = . ;
		__ebss = . ;
	} > DRAM  AT> DRAM_LOAD

	/* Device contents for the root filesystem.  */
	.root ALIGN (4096) : {
		__root_fs_image_start = . ;
		*(.root)
		__root_fs_image_end = . ;
	} > DRAM  AT> DRAM_LOAD

	.memcons : {
		_memcons_output = . ;
		. = . + 0x8000 ;
		_memcons_output_end = . ;

	        __kram_end = . ;
	} > DRAM  AT> DRAM_LOAD

	.bootmap ALIGN (4096) : {
		__bootmap = . ;
		. = . + 4096 ;		/* enough for 128MB.   */
	} > DRAM  AT> DRAM_LOAD
}
