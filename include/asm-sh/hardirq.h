#ifndef __ASM_SH_HARDIRQ_H
#define __ASM_SH_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/irq.h>

/* entry.S is sensitive to the offsets of these fields */
typedef struct {
	unsigned int __softirq_pending;
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

#define HARDIRQ_BITS	8

/*
 * The hardirq mask has to be large enough to have
 * space for potentially all IRQ sources in the system
 * nesting on a single CPU:
 */
#if (1 << HARDIRQ_BITS) < NR_IRQS
# error HARDIRQ_BITS is too low!
#endif

#define nmi_enter()		(irq_enter())
#define nmi_exit()		(preempt_count() -= HARDIRQ_OFFSET)

#define irq_enter()		(preempt_count() += HARDIRQ_OFFSET)
#define irq_exit()							\
do {									\
		preempt_count() -= IRQ_EXIT_OFFSET;			\
		if (!in_interrupt() && softirq_pending(smp_processor_id())) \
			do_softirq();					\
		preempt_enable_no_resched();				\
} while (0)

#endif /* __ASM_SH_HARDIRQ_H */
