/* Linker script for the sim85e2c simulator, which is a verilog simulation of
   the V850E2 NA85E2C cpu core (CONFIG_V850E2_SIM85E2C).  */

MEMORY {
	/* 1MB of `instruction RAM', starting at 0.
	   Instruction fetches are much faster from IRAM than from DRAM.
	   This should match IRAM_ADDR in "include/asm-v580/sim85e2c.h".    */
	IRAM		: ORIGIN = 0x00000000, LENGTH = 0x00100000

	/* 1MB of `data RAM', below and contiguous with the I/O space.
	   Data fetches are much faster from DRAM than from IRAM.
	   This should match DRAM_ADDR in "include/asm-v580/sim85e2c.h".  */
	DRAM		: ORIGIN = 0xfff00000, LENGTH = 0x000ff000
	/* We have to load DRAM at a mirror-address of 0x1ff00000,
	   because the simulator's preprocessing script isn't smart
	   enough to deal with the above LMA.  */
	DRAM_LOAD 	: ORIGIN = 0x1ff00000, LENGTH = 0x000ff000

	/* `external ram' (CS1 area), comes after IRAM.
	   This should match ERAM_ADDR in "include/asm-v580/sim85e2c.h".  */
	ERAM		: ORIGIN = 0x00100000, LENGTH = 0x07f00000

	/* Dynamic RAM; uses memory controller.  */
	/* SDRAM	: ORIGIN = 0x10000000, LENGTH = 0x01000000 */
	SDRAM		: ORIGIN = 0x10000000, LENGTH = 0x00200000/*use 2MB*/
}

SECTIONS {
	.iram : {
		INTV_CONTENTS
		*arch/v850/kernel/head.o
		*(.early.text)
	} > IRAM
	.dram : {
		_memcons_output = . ;
		. = . + 0x8000 ;
		_memcons_output_end = . ;
	} > DRAM
	.sdram : {
		/* We stick console output into a buffer here.  */
		RAMK_KRAM_CONTENTS
		ROOT_FS_CONTENTS
	} > SDRAM
}
