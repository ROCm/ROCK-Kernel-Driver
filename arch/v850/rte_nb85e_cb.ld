/* Linker script for the Midas labs RTE-NB85E-CB evaluation board
   (CONFIG_RTE_CB_NB85E).  */

MEMORY {
	LOW   : ORIGIN = 0x0,	     LENGTH = 0x00100000
	/* 1MB of SRAM; we can't use the last 96KB, because it's used by
	   the monitor scratch-RAM.  This memory is mirrored 4 times.  */
	SRAM  : ORIGIN = 0x03C00000, LENGTH = 0x000E8000
	/* Monitor scratch RAM; only the interrupt vectors should go here.  */
	MRAM  : ORIGIN = 0x03CE8000, LENGTH = 0x00018000
	/* 16MB of SDRAM.  */
	SDRAM : ORIGIN = 0x01000000, LENGTH = 0x01000000
}

#ifdef CONFIG_RTE_CB_NB85E_KSRAM
# define KRAM SRAM
#else
# define KRAM SDRAM
#endif

SECTIONS {
	.intv : { INTV_CONTENTS } > LOW
	.sram : { RAMK_KRAM_CONTENTS } > KRAM
	.root : { ROOT_FS_CONTENTS } > SDRAM
}
