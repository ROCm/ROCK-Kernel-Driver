#ifdef __KERNEL__
#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/preempt.h>

typedef struct {
	unsigned long __softirq_pending;
	unsigned long __syscall_count;
	struct task_struct * __ksoftirqd_task;
	unsigned long idle_timestamp;
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

#define IRQ_OFFSET 64

/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?
 */
#define in_interrupt() \
		((preempt_count() & ~PREEMPT_ACTIVE) >= IRQ_OFFSET)

#define in_irq in_interrupt

#define hardirq_trylock()	(!in_interrupt())
#define hardirq_endlock()	do { } while (0)

#define irq_enter()		(preempt_count() += IRQ_OFFSET)
#define irq_exit()		(preempt_count() -= IRQ_OFFSET)

#ifndef CONFIG_SMP
# define synchronize_irq(irq)	barrier()
#else
  extern void synchronize_irq(unsigned int irq);
#endif /* CONFIG_SMP */

#endif /* __KERNEL__ */
#endif /* __ASM_HARDIRQ_H */
