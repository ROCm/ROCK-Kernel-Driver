/* Linker script for the Midas labs RTE-V850E/MA1-CB evaluation board
   (CONFIG_RTE_CB_MA1), with kernel in ROM.  */

MEMORY {
	ROM   : ORIGIN = 0x00000000, LENGTH = 0x00100000
	/* 1MB of SRAM.  This memory is mirrored 4 times.  */
	SRAM  : ORIGIN = 0x00400000, LENGTH = 0x00100000
	/* 32MB of SDRAM.  */
	SDRAM : ORIGIN = 0x00800000, LENGTH = 0x02000000
}

SECTIONS {
	ROMK_SECTIONS(ROM, SRAM)
}
