/* Linker script for the NEC AS85EP1 V850E evaluation board
   (CONFIG_V850E_AS85EP1).  */

/* Note, all symbols are prefixed with an extra `_' for compatibility with
   the existing linux sources.  */

_jiffies = _jiffies_64 ;

MEMORY {
       /* 1MB of internal memory (内蔵命令RAM).  */
       iMEM0 : ORIGIN = 0,	    LENGTH = 0x00100000

       /* 1MB of static RAM.  */
       SRAM  : ORIGIN = 0x00400000, LENGTH = 0x00100000

       /* About 58MB of DRAM.  This can actually be at one of two positions,
	  determined by jump JP3; we have to use the first position because the
	  second is partially out of processor instruction addressing range
	  (though in the second position there's actually 64MB available).  */
       SDRAM : ORIGIN = 0x00600000, LENGTH = 0x039F8000
}

SECTIONS {
	.resetv : {
		__intv_start = . ;
			*(.intv.reset)	/* Reset vector */
	} > iMEM0

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
	} > SRAM

	.data ALIGN (0x4) : {
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
	} > SRAM

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

		. = ALIGN (4) ;
		___initramfs_start = . ;
			*(.init.ramfs)
		___initramfs_end = . ;

		/* We stick most of the interrupt vectors here; they'll be
		   copied into the proper location by the early init code (we
		   can't put them directly in the right place because of
		   hardware bugs).  The vectors shouldn't need to be
		   relocated, so we don't have to use `> ...  AT> ...' to
		   split the load/vm addresses (and we can't because of
		   problems with the loader).  */
		. = ALIGN (0x10) ;
		__intv_copy_src_start = . ;
			*(.intv.common)	/* Vectors common to all v850e proc. */
			*(.intv.mach)	/* Machine-specific int. vectors.  */
		. = ALIGN (0x10) ;
		__intv_copy_src_end = . ;

		/* This is here so that when we free init memory, the initial
		   load-area of the interrupt vectors is freed too.  */
		__init_end = . ;
		__kram_end = . ;
		   
		__bootmap = . ;
		. = . + 4096 ;		/* enough for 128MB.   */
	} > SRAM

	/* Where we end up putting the vectors.  */
	__intv_copy_dst_start = 0x10 ;
	__intv_copy_dst_end = __intv_copy_dst_start + (__intv_copy_src_end - __intv_copy_src_start) ;

	__intv_end = __intv_copy_dst_end ;

	/* Device contents for the root filesystem.  */
	.root : {
		. = ALIGN (4096) ;
		__root_fs_image_start = . ;
		*(.root)
		__root_fs_image_end = . ;
	} > SDRAM
}
