/*
 * arch/ppc/boot/simple/chrpmap.S
 *
 * Author: Tom Rini <trini@mvista.com>
 *
 * This will go and setup ISA_io to 0xFE00000 and return.
 */

#include <asm/ppc_asm.h>

	.text

	.globl serial_fixups
serial_fixups:
	lis	r3,ISA_io@h	/* Load ISA_io */
	ori	r3,r3,ISA_io@l
	lis	r4,0xFE00	/* Load the value, 0xFE00000 */
	stw	r4,0(r3)	/* store */
	blr
