/* Linker script for the Midas labs Anna V850E2 evaluation board
   (CONFIG_V850E2_ANNA), with kernel in ROM (CONFIG_ROM_KERNEL).  */

/* Note, all symbols are prefixed with an extra `_' for compatibility with
   the existing linux sources.  */

_jiffies = _jiffies_64 ;

MEMORY {
       /* 8MB of flash ROM.  */
       ROM   : ORIGIN = 0,          LENGTH = 0x00800000

       /* 1MB of static RAM.  This memory is mirrored 64 times.  */
       SRAM  : ORIGIN = 0x04000000, LENGTH = 0x00100000
       /* 64MB of DRAM.  */
       SDRAM : ORIGIN = 0x08000000, LENGTH = 0x04000000
}

SECTIONS {
	.intv : {
		__intv_start = . ;
			*(.intv.reset)	/* Reset vector */
			*(.intv.common)	/* Vectors common to all v850e proc. */
			*(.intv.mach)	/* Machine-specific int. vectors.  */
		__intv_end = . ;
	} > ROM

	.text ALIGN (0x10) : {
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
	} > ROM

	.init_text ALIGN (4096) : {
			*(.init.text)	/* 2.5 convention */
			*(.text.init)	/* 2.4 convention */
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
	} > ROM

	/* Device contents for the root filesystem.  */
	.root ALIGN (4096) : {
		__root_fs_image_start = . ;
		*(.root)
		__root_fs_image_end = . ;

		. = ALIGN (4) ;
		___initramfs_start = . ;
			*(.init.ramfs)
		___initramfs_end = . ;
	} > ROM

	__rom_copy_src_start = . ;

	.data : {
		__kram_start = . ;
		__rom_copy_dst_start = . ;

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

	.init_data ALIGN (4096) : {
		__init_start = . ;
			*(.init.data)	/* 2.5 convention */
			*(.data.init)	/* 2.4 convention */
		__init_end = . ;
		__rom_copy_dst_end = . ;
	} > SRAM  AT> ROM

	.bss ALIGN (4096) : {
		__sbss = . ;
			*(.bss)
			*(COMMON)
		. = ALIGN (4) ;
		__init_stack_end = . ;
		__ebss = . ;

		__kram_end = . ;
	} > SRAM

	.bootmap ALIGN (4096) : {
		__bootmap = . ;
		. = . + 4096 ;		/* enough for 128MB.   */
	} > SRAM
}
