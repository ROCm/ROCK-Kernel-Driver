/*
 * Platform dependent support for HP simulator.
 *
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/irq.h>

static unsigned int
hpsim_irq_startup (unsigned int irq)
{
	return 0;
}

static void
hpsim_irq_noop (unsigned int irq)
{
}

static struct hw_interrupt_type irq_type_hp_sim = {
	typename:	"hpsim",
	startup:	hpsim_irq_startup,
	shutdown:	hpsim_irq_noop,
	enable:		hpsim_irq_noop,
	disable:	hpsim_irq_noop,
	ack:		hpsim_irq_noop,
	end:		hpsim_irq_noop,
	set_affinity:	(void (*)(unsigned int, unsigned long)) hpsim_irq_noop,
};

void __init
hpsim_irq_init (void)
{
	int i;

	for (i = IA64_MIN_VECTORED_IRQ; i <= IA64_MAX_VECTORED_IRQ; ++i) {
		if (irq_desc[i].handler == &no_irq_type)
			irq_desc[i].handler = &irq_type_hp_sim;
	}
}
