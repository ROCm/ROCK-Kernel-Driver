/*
 *  linux/arch/mips/philips/nino/int-handler.S
 *
 *  Copyright (C) 1999 Harald Koerfgen
 *  Copyright (C) 2000 Jim Pick (jim@jimpick.com)
 *  Copyright (C) 2001 Steven J. Hill (sjhill@realitydiluted.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Interrupt handler for Philips Nino.
 */
#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>
#include <asm/tx3912.h>

		.data
	        .globl  HighPriVect

HighPriVect:	.word   spurious        # Reserved
	        .word   io_posnegint0   # IOPOSINT(0) or IONEGINT(0)
	        .word   spurious        # CHIDMACNTINT
	        .word   spurious        # TELDMACNTINT
	        .word   spurious        # SNDDMACNTINT
	        .word   spurious        # Reserved
	        .word   io_negint56     # IONEGINT(6) or IONEGINT(5)
	        .word   spurious        # Reserved
	        .word   io_posint56     # IOPOSINT(6) or IOPOSINT(5)
	        .word   spurious        # Reserved
	        .word   spurious        # UARTBRXINT
	        .word   uarta_rx        # UARTARXINT
	        .word   spurious        # Reserved
	        .word   periodic_timer  # PERINT
	        .word   spurious        # ALARMINT
	        .word   spurious        # POSPWROKINT or NEGPWROKINT

/*
 *  Here is the entry point to handle all interrupts.
 */
		.text
		.set	noreorder
		.align	5
		NESTED(nino_handle_int, PT_SIZE, ra)
		.set	noat
		SAVE_ALL
		CLI
		.set	at
	
		/*
		 * Get pending Interrupts
		 */
		mfc0	t0, CP0_CAUSE		# Get pending interrupts
		andi	t2, t0, IE_IRQ4		# IRQ4 (high priority)
		bne	t2, IE_IRQ4, low_priority
		nop

/*
 *  Ok, we've got a high priority interrupt (a.k.a. an external interrupt).
 *  Read Interrupt Status Register 6 to get vector.
 */
high_priority:
		lui	t0, %hi(IntStatus6)
		lw	t1, %lo(IntStatus6)(t0)
		andi	t1, INT6_INTVECT
		la	t2, HighPriVect
		addu	t1, t1, t2
		lw	t2, 0(t1)
		jr	t2
		nop

/*
 * Ok, we've got one of over a hundred other interupts.
 */
low_priority:
		lui	t0, %hi(IntStatus1)
		lw	t1, %lo(IntStatus1)(t0)
		j	handle_it
		li	a0, 20

/*
 * We don't currently handle spurious interrupts.
 */
spurious:	
		j	spurious_interrupt
		nop

/*
 * We have the IRQ number, dispatch to the real handler.
 */
handle_it:	jal	do_IRQ
		move	a1,sp
		j	ret_from_irq
		nop

/************************************
 * High priority interrupt mappings *
 ************************************/
		
/* 
 *  Periodic timer - IRQ 0
 */
periodic_timer:
		j	handle_it
		li	a0, 0

/* 
 *  UARTA RX - IRQ 3
 */
uarta_rx:
		j	handle_it
		li	a0, 3

/* 
 *  GPIO Pin 0 transition - IRQ 10
 */
io_posnegint0:
		j	handle_it
		li	a0, 10

/* 
 *  GPIO Pin 5 or 6 transition (0-to-1) - IRQ 11
 */
io_posint56:
		j	handle_it
		li	a0, 11

/*
 *  GPIO Pin 5 or 6 transition (1-to-0) - IRQ 12
 */
io_negint56:
		j	handle_it
		li	a0, 12

		END(nino_handle_int)
