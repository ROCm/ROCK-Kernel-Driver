/*
 * linux/arch/arm/boot/compressed/setup-sa1100.S
 *
 * Copyright (C) 2000 Nicolas Pitre <nico@cam.org>
 *
 * SA1100 setup routines, to be used after BSS has been cleared.
 *
 * John G Dorsey <john+@cs.cmu.edu>  2000/05/25 :
 *  Runtime test for Neponset added.
 */

#include <linux/linkage.h>
#include <linux/config.h>
#include <asm/mach-types.h>

		.text

GPIO_BASE:	.long	0x90040000
#define GPLR	0x00
#define GPDR	0x04
#define GPSR	0x08
#define GAFR	0x1c

PPC_BASE:	.long	0x90060000
#define PPAR	0x08

UART1_BASE:	.long	0x80010000
UART3_BASE:	.long	0x80050000
#define UTCR0           0x00
#define UTCR1           0x04
#define UTCR2           0x08
#define UTCR3           0x0c
#define UTSR0           0x1c
#define UTSR1           0x20

#define BAUD_DIV_230400	0x000
#define BAUD_DIV_115200	0x001
#define BAUD_DIV_57600	0x003
#define BAUD_DIV_38400	0x005
#define BAUD_DIV_19200	0x00b
#define BAUD_DIV_9600	0x017
#define BAUD_DIV	BAUD_DIV_9600

SCR_loc:	.long	SYMBOL_NAME(SCR_value)
#define GPIO_2_9	0x3fc


/*
 * void sa1100_setup( int arch_id );
 *
 * This is called from decompress_kernel() with the arch_decomp_setup() macro.
 */

ENTRY(sa1100_setup)
		mov	r3, r0			@ keep machine type in r3

@ Read System Configuration "Register" for Assabet.
@ (taken from "Intel StrongARM SA-1110 Microprocessor Development Board
@ User's Guide," p.4-9)

		teq	r3, #MACH_TYPE_ASSABET
		bne	skip_SCR

		ldr	r0, GPIO_BASE
		ldr	r1, [r0, #GPDR]
		and	r1, r1, #GPIO_2_9
		str	r1, [r0, #GPDR]
		mov	r1, #GPIO_2_9
		str	r1, [r0, #GPSR]
		ldr	r1, [r0, #GPDR]
		bic	r1, r1, #GPIO_2_9
		str	r1, [r0, #GPDR]

		mov	r2, #100
1:		ldr	r1, [r0, #GPLR]
		subs	r2, r2, #1
		bne	1b

		and	r2, r1, #GPIO_2_9
		ldr	r1, SCR_loc
		str	r2, [r1]

		ldr	r1, [r0, #GPDR]
		and	r1, r1, #GPIO_2_9
		str	r1, [r0, #GPDR]

skip_SCR:

		@ Initialize UART (if bootloader has not done it yet)...
		teq	r3, #MACH_TYPE_BRUTUS
		teqne	r3, #MACH_TYPE_ASSABET
		bne	skip_uart

		@ UART3 if Assabet is used with Neponset
		teq	r3, #25			@ if Assabet 
		tsteq	r2, #(1 << 9)		@ ... and Neponset present
		ldreq	r0, UART3_BASE
		beq	uart_init

		@ At least for Brutus, the UART1 is used through
		@ the alternate GPIO function...
		teq	r3, #MACH_TYPE_BRUTUS
		bne	uart1

alt_GPIO_uart:	ldr	r0, GPIO_BASE
		ldr	r1, [r0, #GPDR]
		bic	r1, r1, #1<<15
		orr	r1, r1, #1<<14
		str	r1, [r0, #GPDR]
		ldr	r1, [r0, #GAFR]
		orr	r1, r1, #(1<<15)|(1<<14)
		str	r1, [r0, #GAFR]
		ldr	r0, PPC_BASE
		ldr	r1, [r0, #PPAR]
		orr	r1, r1, #1<<12
		str	r1, [r0, #PPAR]

uart1:		ldr	r0, UART1_BASE

uart_init:	ldr	r1, [r0, #UTSR1]
		tst	r1, #1<<0		@ TBY
		bne	1b
		mov	r1, #0
		str	r1, [r0, #UTCR3]
		mov	r1, #0x08		@ 8N1
		str	r1, [r0, #UTCR0]
		mov	r1, #BAUD_DIV
		str	r1, [r0, #UTCR2]
		mov	r1, r1, lsr #8
		str	r1, [r0, #UTCR1]
		mov	r1, #0x03		@ RXE + TXE
		str	r1, [r0, #UTCR3]
		mov	r1, #0xff		@ flush status reg
		str	r1, [r0, #UTSR0]
skip_uart:

		@ Extra specific setup calls
		@ The machine type is passed in r0
		mov	r0, r3
#ifdef CONFIG_SA1100_NANOENGINE
		teq	r0, #MACH_TYPE_NANOENGINE
		beq	SYMBOL_NAME(bse_setup)
#endif

out:		mov	pc, lr

