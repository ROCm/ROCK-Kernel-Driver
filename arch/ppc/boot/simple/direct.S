/*
 * arch/ppc/boot/simple/direct.S
 *
 * Author: Tom Rini <trini@mvista.com>
 *
 * This is an empty function for machines which use SERIAL_IO_MEM
 * and don't need ISA_io set to anything but 0;
 */

	.text

	.globl setup_legacy
setup_legacy:
	blr
