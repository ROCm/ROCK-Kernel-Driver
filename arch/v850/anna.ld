/* Linker script for the Midas labs Anna V850E2 evaluation board
   (CONFIG_V850E2_ANNA).  */

MEMORY {
	/* 256KB of internal memory (followed by one mirror).  */
	iMEM0 : ORIGIN = 0,	    LENGTH = 0x00040000
	/* 256KB of internal memory (followed by one mirror).  */
	iMEM1 : ORIGIN = 0x00040000, LENGTH = 0x00040000

	/* 1MB of static RAM.  This memory is mirrored 64 times.  */
	SRAM  : ORIGIN = 0x04000000, LENGTH = 0x00100000
	/* 64MB of DRAM.  */
	SDRAM : ORIGIN = 0x08000000, LENGTH = 0x04000000
}

SECTIONS {
	.intv : { INTV_CONTENTS } > iMEM0
	.sram : { RAMK_KRAM_CONTENTS } > SRAM
	.root : { ROOT_FS_CONTENTS } > SDRAM
}
