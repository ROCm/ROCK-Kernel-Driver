/* Linker script for the sim85e2c simulator, which is a verilog simulation of
   the V850E2 NA85E2C cpu core (CONFIG_V850E2_SIM85E2C).  */

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
	.iram : {
		INTV_CONTENTS
		TEXT_CONTENTS
		RAMK_INIT_CONTENTS
	} > IRAM
	.data : {
		__kram_start = . ;
		DATA_CONTENTS
		BSS_CONTENTS
		ROOT_FS_CONTENTS

		/* We stick console output into a buffer here.  */
		_memcons_output = . ;
		. = . + 0x8000 ;
		_memcons_output_end = . ;

		__kram_end = . ;
		BOOTMAP_CONTENTS
	} > DRAM  AT> DRAM_LOAD
}
