/* $Id: setup.c,v 1.1.2.5 2002/03/02 21:57:07 lethal Exp $
 *
 * arch/sh/kernel/setup_cqreek.c
 *
 * Copyright (C) 2000  Niibe Yutaka
 *
 * CqREEK IDE/ISA Bridge Support.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/cqreek/cqreek.h>
#include <asm/io.h>
#include <asm/io_generic.h>
#include <asm/irq.h>
#include <asm/rtc.h>

const char *get_system_type(void)
{
	return "CqREEK";
}

/*
 * Initialize the board
 */
void __init platform_setup(void)
{
	int i;
/* udelay is not available at setup time yet... */
#define DELAY() do {for (i=0; i<10000; i++) ctrl_inw(0xa0000000);} while(0)

	if ((inw (BRIDGE_FEATURE) & 1)) { /* We have IDE interface */
		outw_p(0, BRIDGE_IDE_INTR_LVL);
		outw_p(0, BRIDGE_IDE_INTR_MASK);

		outw_p(0, BRIDGE_IDE_CTRL);
		DELAY();

		outw_p(0x8000, BRIDGE_IDE_CTRL);
		DELAY();

		outw_p(0xffff, BRIDGE_IDE_INTR_STAT); /* Clear interrupt status */
		outw_p(0x0f-14, BRIDGE_IDE_INTR_LVL); /* Use 14 IPR */
		outw_p(1, BRIDGE_IDE_INTR_MASK); /* Enable interrupt */
		cqreek_has_ide=1;
	}

	if ((inw (BRIDGE_FEATURE) & 2)) { /* We have ISA interface */
		outw_p(0, BRIDGE_ISA_INTR_LVL);
		outw_p(0, BRIDGE_ISA_INTR_MASK);

		outw_p(0, BRIDGE_ISA_CTRL);
		DELAY();
		outw_p(0x8000, BRIDGE_ISA_CTRL);
		DELAY();

		outw_p(0xffff, BRIDGE_ISA_INTR_STAT); /* Clear interrupt status */
		outw_p(0x0f-10, BRIDGE_ISA_INTR_LVL); /* Use 10 IPR */
		outw_p(0xfff8, BRIDGE_ISA_INTR_MASK); /* Enable interrupt */
		cqreek_has_isa=1;
	}

	printk(KERN_INFO "CqREEK Setup (IDE=%d, ISA=%d)...done\n", cqreek_has_ide, cqreek_has_isa);
}

