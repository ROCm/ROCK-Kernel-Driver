/* Linker script for the gdb v850e simulator (CONFIG_V850E_SIM).  */

MEMORY {
	/* Interrupt vectors.  */
	INTV  : ORIGIN = 0x0, LENGTH = 0xe0
	/* 16MB of RAM.
	   This must match RAM_ADDR and RAM_SIZE in include/asm-v850/sim.h  */
	RAM   : ORIGIN = 0x8F000000, LENGTH = 0x01000000
}

SECTIONS {
	.intv : { INTV_CONTENTS } > INTV
	.ram : { RAMK_KRAM_CONTENTS } > RAM
}
