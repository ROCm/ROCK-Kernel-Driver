/*
 *  linux/arch/arm/lib/io-acorn.S
 *
 *  Copyright (C) 1995, 1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 27/03/03 Ian Molton Clean up CONFIG_CPU
 *
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/hardware.h>

		.text
		.align

		.equ	diff_pcio_base, PCIO_BASE - IO_BASE

		.macro	outw2	rd
		mov	r8, \rd, lsl #16
		orr	r8, r8, r8, lsr #16
		str	r8, [r3, r0, lsl #2]
		mov	r8, \rd, lsr #16
		orr	r8, r8, r8, lsl #16
		str	r8, [r3, r0, lsl #2]
		.endm

		.macro	inw2	rd, mask, temp
		ldr	\rd, [r0]
		and	\rd, \rd, \mask
		ldr	\temp, [r0]
		orr	\rd, \rd, \temp, lsl #16
		.endm

		.macro	addr	rd
		tst	\rd, #0x80000000
		mov	\rd, \rd, lsl #2
		add	\rd, \rd, #IO_BASE
		addeq	\rd, \rd, #diff_pcio_base
		.endm

.iosl_warning:
		.ascii	"<4>insl/outsl not implemented, called from %08lX\0"
		.align

/*
 * These make no sense on Acorn machines.
 * Print a warning message.
 */
ENTRY(insl)
ENTRY(outsl)
		adr	r0, .iosl_warning
		mov	r1, lr
		b	printk
