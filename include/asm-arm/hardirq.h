#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

#include <linux/config.h>
#include <linux/cache.h>
#include <linux/threads.h>
#include <asm/irq.h>

typedef struct {
	unsigned int __softirq_pending;
	unsigned int __local_irq_count;
	unsigned int __local_bh_count;
	unsigned int __syscall_count;
	struct task_struct * __ksoftirqd_task; /* waitqueue is too large */
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

/*
 * We put the hardirq and softirq counter into the preemption
 * counter.  The bitmask has the following meaning:
 *
 * - bits 0-7 are the preemption count (max depth: 256)
 * - bits 8-15 are the softirq count (max # of softirqs: 256)
 * - bits 16-24 are the hardirq count (max # of hardirqs: 512)
 * - bit 26 is the PREEMPT_ACTIVE flag
 *
 * We optimize HARDIRQ_BITS for immediate constant, and only
 * increase it if really needed.
 */
#define PREEMPT_BITS	8
#define SOFTIRQ_BITS	8

#if NR_IRQS > 256
#define HARDIRQ_BITS	9
#else
#define HARDIRQ_BITS	8
#endif

#define PREEMPT_SHIFT	0
#define SOFTIRQ_SHIFT	(PREEMPT_SHIFT + PREEMPT_BITS)
#define HARDIRQ_SHIFT	(SOFTIRQ_SHIFT + SOFTIRQ_BITS)

/*
 * The hardirq mask has to be large enough to have space
 * for potentially all IRQ sources in the system nesting
 * on a single CPU:
 */
#if (1 << HARDIRQ_BITS) < NR_IRQS
# error HARDIRQ_BITS is too low!
#endif

#define irq_enter()		(preempt_count() += HARDIRQ_OFFSET)

#ifndef CONFIG_SMP
extern asmlinkage void __do_softirq(void);

#define irq_exit()							\
	do {								\
		preempt_count() -= IRQ_EXIT_OFFSET;			\
		if (!in_interrupt() && local_softirq_pending())		\
			__do_softirq();					\
		preempt_enable_no_resched();				\
	} while (0)
#endif

#endif /* __ASM_HARDIRQ_H */
