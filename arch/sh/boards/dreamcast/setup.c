/* arch/sh/kernel/setup_dc.c
 *
 * Hardware support for the Sega Dreamcast.
 *
 * Copyright (c) 2001, 2002 M. R. Brown <mrbrown@linuxdc.org>
 * Copyright (c) 2002 Paul Mundt <lethal@chaoticdreams.org>
 *
 * This file is part of the LinuxDC project (www.linuxdc.org)
 *
 * Released under the terms of the GNU GPL v2.0.
 * 
 * This file originally bore the message (with enclosed-$):
 *	Id: setup_dc.c,v 1.5 2001/05/24 05:09:16 mrbrown Exp
 *	SEGA Dreamcast support
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/dreamcast/sysasic.h>

extern struct hw_interrupt_type systemasic_int;
/* XXX: Move this into it's proper header. */
extern void (*board_time_init)(void);
extern void aica_time_init(void);

const char *get_system_type(void)
{
	return "Sega Dreamcast";
}

#ifdef CONFIG_PCI
extern int gapspci_init(void);
#endif

int __init platform_setup(void)
{
	int i;

	/* Mask all hardware events */
	/* XXX */

	/* Acknowledge any previous events */
	/* XXX */

	/* Assign all virtual IRQs to the System ASIC int. handler */
	for (i = HW_EVENT_IRQ_BASE; i < HW_EVENT_IRQ_MAX; i++)
		irq_desc[i].handler = &systemasic_int;

	board_time_init = aica_time_init;

#ifdef CONFIG_PCI
	if (gapspci_init() < 0)
		printk(KERN_WARNING "GAPSPCI was not detected.\n");
#endif

	return 0;
}
