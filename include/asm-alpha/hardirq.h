#ifndef _ALPHA_HARDIRQ_H
#define _ALPHA_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/cache.h>


/* entry.S is sensitive to the offsets of these fields */
typedef struct {
	unsigned long __softirq_pending;
	unsigned int __syscall_count;
	unsigned long idle_timestamp;
	struct task_struct * __ksoftirqd_task;
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

#define HARDIRQ_BITS	12

/*
 * The hardirq mask has to be large enough to have
 * space for potentially nestable IRQ sources in the system
 * to nest on a single CPU. On Alpha, interrupts are masked at the CPU
 * by IPL as well as at the system level. We only have 8 IPLs (UNIX PALcode)
 * so we really only have 8 nestable IRQs, but allow some overhead
 */
#if (1 << HARDIRQ_BITS) < 16
#error HARDIRQ_BITS is too low!
#endif

#define irq_enter()		(preempt_count() += HARDIRQ_OFFSET)
#define irq_exit()						\
do {								\
		preempt_count() -= IRQ_EXIT_OFFSET;		\
		if (!in_interrupt() &&				\
		    softirq_pending(smp_processor_id()))	\
			do_softirq();				\
		preempt_enable_no_resched();			\
} while (0)

#endif /* _ALPHA_HARDIRQ_H */
