/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000, 2001 MIPS Technologies, Inc.
 * Copyright (C) 2001 Ralf Baechle
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Routines for generic manipulation of the interrupts found on the MIPS 
 * Malta board.
 * The interrupt controller is located in the South Bridge a PIIX4 device 
 * with two internal 82C95 interrupt controllers.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/random.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/mips-boards/malta.h>
#include <asm/mips-boards/maltaint.h>
#include <asm/mips-boards/piix4.h>
#include <asm/gt64120.h>
#include <asm/mips-boards/generic.h>

extern asmlinkage void mipsIRQ(void);

void malta_hw0_irqdispatch(struct pt_regs *regs)
{
	int irq;

	/*  
	 * Determine highest priority pending interrupt by performing a PCI
	 * Interrupt Acknowledge cycle.
	 */
	GT_READ(GT_PCI0_IACK_OFS, irq);
	irq &= 0xFF;

	/*  
	 * IRQ7 is used to detect spurious interrupts.  The interrupt
	 * acknowledge cycle returns IRQ7, if no interrupts is requested.  We
	 * can differentiate between this situation and a "normal" IRQ7 by
	 * reading the ISR.
	 */
	if (irq == 7) {
		outb(PIIX4_OCW3_SEL | PIIX4_OCW3_ISR, PIIX4_ICTLR1_OCW3);
		if (!(inb(PIIX4_ICTLR1_OCW3) & (1 << 7)))
		        return;    /* Spurious interrupt. */
	}

	do_IRQ(irq, regs);
}

void __init init_IRQ(void)
{
	set_except_vector(0, mipsIRQ);
	init_generic_irq();
	init_i8259_irqs();

#ifdef CONFIG_REMOTE_DEBUG
	if (remote_debug) {
		set_debug_traps();
		breakpoint();
	}
#endif
}
