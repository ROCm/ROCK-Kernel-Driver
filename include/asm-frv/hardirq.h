/* hardirq.h: FRV hardware IRQ management
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>

typedef struct {
	unsigned int __softirq_pending;
	unsigned long idle_timestamp;
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */


#define irq_enter()		(preempt_count() += HARDIRQ_OFFSET)
#define nmi_enter()		(irq_enter())
#define nmi_exit()		(preempt_count() -= HARDIRQ_OFFSET)

#define irq_exit()							\
do {									\
	preempt_count() -= IRQ_EXIT_OFFSET;				\
	if (!in_interrupt() && softirq_pending(smp_processor_id()))	\
		do_softirq();						\
	preempt_enable_no_resched();					\
} while (0)

#ifdef CONFIG_SMP
#error SMP not available on FR-V
#endif /* CONFIG_SMP */


#endif
