/***********************************************************************
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 *  arch/mips/ddb5xxx/common/irq_cpu.c
 *     This file define the irq handler for MIPS CPU interrupts.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 ***********************************************************************
 */

/*
 * Almost all MIPS CPUs define 8 interrupt sources.  They are typically
 * level triggered (i.e., cannot be cleared from CPU; must be cleared from
 * device).  The first two are software interrupts.  The last one is 
 * usually cpu timer interrupt if coutner register is present.
 *
 * This file exports one global function:
 *	mips_cpu_irq_init(u32 irq_base);
 */

#include <linux/irq.h>
#include <linux/types.h>
#include <linux/kernel.h>

#include <asm/mipsregs.h>

/* [jsun] sooner or later we should move this debug stuff to MIPS common */
#include <asm/ddb5xxx/debug.h>

static int mips_cpu_irq_base=-1;

static void 
mips_cpu_irq_enable(unsigned int irq)
{
	MIPS_ASSERT(mips_cpu_irq_base != -1);
	MIPS_ASSERT(irq >= mips_cpu_irq_base);
	MIPS_ASSERT(irq < mips_cpu_irq_base+8);

	clear_cp0_cause( 1 << (irq - mips_cpu_irq_base + 8));
	set_cp0_status(1 << (irq - mips_cpu_irq_base + 8));
}

static void 
mips_cpu_irq_disable(unsigned int irq)
{
	MIPS_ASSERT(mips_cpu_irq_base != -1);
	MIPS_ASSERT(irq >= mips_cpu_irq_base);
	MIPS_ASSERT(irq < mips_cpu_irq_base+8);

	clear_cp0_status(1 << (irq - mips_cpu_irq_base + 8));
}

static unsigned int mips_cpu_irq_startup(unsigned int irq)
{
	mips_cpu_irq_enable(irq);
	return 0;
}

#define	mips_cpu_irq_shutdown	mips_cpu_irq_disable

static void
mips_cpu_irq_ack(unsigned int irq)
{
	MIPS_ASSERT(mips_cpu_irq_base != -1);
	MIPS_ASSERT(irq >= mips_cpu_irq_base);
	MIPS_ASSERT(irq < mips_cpu_irq_base+8);

	/* although we attemp to clear the IP bit in cause reigster, I think
	 * usually it is cleared by device (irq source)
	 */
	clear_cp0_cause( 1 << (irq - mips_cpu_irq_base + 8));

	/* I am not fully convinced that I should disable irq here */
}

static void
mips_cpu_irq_end(unsigned int irq)
{
	MIPS_ASSERT(mips_cpu_irq_base != -1);
	MIPS_ASSERT(irq >= mips_cpu_irq_base);
	MIPS_ASSERT(irq < mips_cpu_irq_base+8);
	/* I am not fully convinced that I should enable irq here */
}

static hw_irq_controller mips_cpu_irq_controller = {
	"CPU_irq",
	mips_cpu_irq_startup,
	mips_cpu_irq_shutdown,
	mips_cpu_irq_enable,
	mips_cpu_irq_disable,
	mips_cpu_irq_ack,
	mips_cpu_irq_end,
	NULL			/* no affinity stuff for UP */
};


void 
mips_cpu_irq_init(u32 irq_base)
{
	extern irq_desc_t irq_desc[];
	u32 i;

	for (i= irq_base; i< irq_base+8; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = NULL;
		irq_desc[i].depth = 1;
		irq_desc[i].handler = &mips_cpu_irq_controller;
	}

	mips_cpu_irq_base = irq_base;
} 
