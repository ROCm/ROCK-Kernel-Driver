/*
 * linux/arch/sh/boards/ec3104/setup.c
 *  EC3104 companion chip support
 *
 * Copyright (C) 2000 Philipp Rumpf <prumpf@tux.org>
 *
 */
/* EC3104 note:
 * This code was written without any documentation about the EC3104 chip.  While
 * I hope I got most of the basic functionality right, the register names I use
 * are most likely completely different from those in the chip documentation.
 *
 * If you have any further information about the EC3104, please tell me
 * (prumpf@tux.org).
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/types.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/ec3104/ec3104.h>

int __init setup_ec3104(void)
{
	char str[8];
	int i;
	
	if (!MACH_EC3104)
		printk("!MACH_EC3104\n");

	if (0)
		return 0;

	for (i=0; i<8; i++)
		str[i] = ctrl_readb(EC3104_BASE + i);

	for (i = EC3104_IRQBASE; i < EC3104_IRQBASE + 32; i++)
		irq_desc[i].handler = &ec3104_int;

	printk("initializing EC3104 \"%.8s\" at %08x, IRQ %d, IRQ base %d\n",
	       str, EC3104_BASE, EC3104_IRQ, EC3104_IRQBASE);


	/* mask all interrupts.  this should have been done by the boot
	 * loader for us but we want to be sure ... */
	ctrl_writel(0xffffffff, EC3104_IMR);
	
	return 0;
}

module_init(setup_ec3104);
